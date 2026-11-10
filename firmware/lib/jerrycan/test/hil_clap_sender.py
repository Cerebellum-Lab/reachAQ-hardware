#!/usr/bin/env python3
"""Clap-only sender for the pellet's host-silence check. Development tool, not release tooling.

The pellet takes the stepper enable off 30 s after the last frame from a host. A SYNC_CLAP is a board frame, so it must
not count, whatever address it comes from. hil_timing_check.py cannot show that: it refuses --clapper-address equal to
the pellet's own, and it also sends TIME_SYNC_REQUEST and CAPABILITIES_REQUEST, which are host frames. This sends only
the one frame you choose, so the quiet window stays quiet, and it checks that the pellet heard what it sent.

Run on the rig, with the pellet on the bus, reachAQ closed and no timekeeper clapping:

    python3 firmware/lib/jerrycan/test/hil_clap_sender.py --channel can0 --address 0 --from 31 --duration 60

--from 31 sends claps from 0x1F (CAN id 0x4BF), --from 0 from the pellet's own address (id 0x4A0). Both reach the host
frame hook's address test, through different RX filters; address 8, the timekeeper's, proves nothing. Claps are FD
frames with BRS: a 4-byte tick and the uuid byte, as hil_timing_check.py sends them.

The pellet answers each clap it hears with SYNC_CLAP_OBSERVED, whose clapper_node is the address the clap came from.
That is the receipt: without it, ENABLE_OFF during a silence run could be the sender missing the bus, not the fix working.
The result line gives sent, observed (replies with clapper_node equal to --from) and mismatched (replies with another
clapper_node: a second clapper is on the bus). Exit status: 0 if observed >= sent - 1 and mismatched is 0, 1 otherwise or
if interrupted, 2 for bad arguments (nothing is sent).

--frame heartbeat is the positive control. It sends HEARTBEAT (0x3F) to 0x1F at the same rate, which does count as host
traffic, so a silence run beside it must show no ENABLE_OFF. A HEARTBEAT carries no sender, so --from has no effect.
Its replies are counted for information and do not change the exit status.
"""

import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hil_timing_check as hil  # noqa: E402

BROADCAST = 0x1F
HEARTBEAT = 0x3F
OBSERVED = struct.Struct("<IB")  # tick, clapper_node: jerrycan_cmd_sync_clap_observed_t
DRAIN_S = 1.0  # after the last frame, for its reply
MIN_FRAMES = 2  # with fewer, "observed >= sent - 1" could not fail


def frame_for(kind, sender, tick):
    """(message type, low five id bits, payload) of one frame. The uuid byte is added when it is sent.

    A clap's low id bits are its sender. A HEARTBEAT is a host frame, so they are its destination."""
    if kind == "clap":
        return hil.SYNC_CLAP, sender, struct.pack("<I", tick)
    return HEARTBEAT, BROADCAST, bytes([0xFF])


def reply_clapper(can_id, data, own, address):
    """clapper_node of a SYNC_CLAP_OBSERVED from the pellet at `address`, or None for any other frame."""
    if own or can_id != (hil.SYNC_CLAP_OBSERVED << 5) | address or len(data) < OBSERVED.size:
        return None
    return OBSERVED.unpack_from(data)[1]


def classify(kind, can_id, data, own, address, sender):
    """'observed', 'mismatched' or None for one received frame."""
    if kind == "clap":
        clapper = reply_clapper(can_id, data, own, address)
        if clapper is None:
            return None
        return "observed" if clapper == sender else "mismatched"
    return "observed" if not own and can_id == (HEARTBEAT << 5) | address else None


def exit_code(sent, observed, mismatched):
    """0 if the pellet heard all but at most one clap (the last reply can still be in flight) and no other clapper
    answered, 1 otherwise."""
    return 0 if observed >= sent - 1 and mismatched == 0 else 1


def run(s, kind, address, sender, rate, frames, drain_s=None):
    """Send `frames` frames at `rate` Hz, one every 1/rate s, and count the pellet's replies.

    Returns {"sent", "observed", "mismatched"}. Nothing but the chosen frame is transmitted."""
    tally = {"sent": 0, "observed": 0, "mismatched": 0}

    def listen(seconds):
        for can_id, data, own, _, _ in hil.receive(s, seconds):
            outcome = classify(kind, can_id, data, own, address, sender)
            if outcome is not None:
                tally[outcome] += 1

    start = time.monotonic()
    for tick in range(1, frames + 1):
        listen(start + (tick - 1) / rate - time.monotonic())
        hil.send(s, *frame_for(kind, sender, tick))
        tally["sent"] += 1
    listen(DRAIN_S if drain_s is None else drain_s)
    return tally


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--channel", required=True, help="CAN interface. No default, so that the wrong bus is never "
                        "picked by omission")
    parser.add_argument("--address", type=int, required=True, help="the pellet's address, 0-31")
    parser.add_argument("--from", dest="sender", type=int,
                        help="address the claps are sent from, 0-31: 31 (0x1F), the pellet's own address, or any "
                             "other. Required for --frame clap")
    parser.add_argument("--rate", type=float, default=1.0, help="frames per second (default 1)")
    parser.add_argument("--duration", type=float, required=True, help="seconds to send for")
    parser.add_argument("--frame", choices=("clap", "heartbeat"), default="clap",
                        help="clap (default) or heartbeat, the positive control")
    args = parser.parse_args(argv)
    if not 0 <= args.address <= 31:
        parser.error("--address must be 0-31")
    if args.frame == "clap" and args.sender is None:
        parser.error("--from is required for --frame clap")
    if args.sender is not None and not 0 <= args.sender <= 31:
        parser.error("--from must be 0-31")
    if args.rate <= 0 or args.duration <= 0:
        parser.error("--rate and --duration must be positive")
    args.frames = int(args.duration * args.rate + 1e-9)
    if args.frames < MIN_FRAMES:
        parser.error(f"--duration x --rate gives {args.frames} frame(s); at least {MIN_FRAMES} are needed for the "
                     "receipt check to be able to fail")
    return args


def main(argv=None):
    args = parse_args(argv)
    msg_type, low5, _ = frame_for(args.frame, args.sender, 1)
    name = "SYNC_CLAP" if args.frame == "clap" else "HEARTBEAT"
    print(f"sending {args.frames} x {name} (CAN id 0x{(msg_type << 5) | low5:03x}) at {args.rate:g} Hz on "
          f"{args.channel} for the pellet at address {args.address}; nothing else will be sent", flush=True)
    s = hil.open_socket(args.channel)
    try:
        tally = run(s, args.frame, args.address, args.sender, args.rate, args.frames)
    except KeyboardInterrupt:
        print("interrupted: no verdict", file=sys.stderr)
        return 1
    sent, observed, mismatched = tally["sent"], tally["observed"], tally["mismatched"]
    if args.frame == "heartbeat":
        print(f"[INFO] heartbeat: sent {sent}, HEARTBEAT replies from address {args.address} {observed}")
        return 0
    code = exit_code(sent, observed, mismatched)
    detail = f"sent {sent}, observed {observed}, mismatched {mismatched}; clapper_node must be {args.sender}"
    if code == 0:
        print(f"[PASS] clap_receipt: {detail}")
    else:
        why = []
        if observed < sent - 1:
            why.append(f"the pellet answered {observed} of {sent} claps (need {sent - 1}): it is not hearing them, so "
                       "a silence run now proves nothing. Check the channel, the address, FD with BRS, and that the "
                       "image is a timing build")
        if mismatched:
            why.append(f"{mismatched} answer(s) named another clapper_node: a second clapper is on the bus")
        print(f"[FAIL] clap_receipt: {detail}; " + "; ".join(why))
    return code


if __name__ == "__main__":
    sys.exit(main())

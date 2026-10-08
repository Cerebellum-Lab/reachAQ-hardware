#!/usr/bin/env python3
"""Hardware-in-the-loop check of pellet-board timing. Development tool, not release tooling.

Run on the rig, with the pellet board on can0 running a CONFIG_LIB_JERRYCAN_TIMING build:

    python3 firmware/lib/jerrycan/test/hil_timing_check.py --channel can0 --address 0

It transmits CAPABILITIES_REQUEST and TIME_SYNC_REQUEST to the pellet, SYNC_CLAP
frames from address 8 (standing in for a timekeeper), and - only with
--play-tone - one 200 ms Tone 1, which is audible and drives STIM0.
"""

import argparse
import socket
import struct
import sys
import time

CAN_RAW_FD_FRAMES = 5
CAN_RAW_RECV_OWN_MSGS = 4
SO_TIMESTAMPING = 37
TIMESTAMPING_FLAGS = (1 << 2) | (1 << 3) | (1 << 4) | (1 << 6)  # RX hw, RX sw, sw report, raw hw report
CANFD_BRS = 0x01
CAN_ERR_FLAG = 0x20000000
FRAME = struct.Struct("<IBBBB64s")
TRAILER = struct.Struct("<BBBBIIQ")
FD_LENGTHS = (0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64)

CAPABILITIES_REQUEST, CAPABILITIES_RESPONSE = 0x23, 0x24
TIME_SYNC_REQUEST, TIME_SYNC_RESPONSE = 0x1F, 0x20
SYNC_CLAP, SYNC_CLAP_OBSERVED = 0x25, 0x26
TONE = 0x0C
# Board-to-host payload sizes, so the trailer can be found after payload + uuid.
PAYLOAD_SIZES = {0x01: 8, 0x07: 28, 0x08: 5, 0x0A: 5, 0x0C: 5, 0x0F: 1, 0x13: 3, 0x16: 12, 0x17: 6,
                 0x19: 7, 0x20: 20, 0x22: 12, 0x24: 9, 0x26: 5, 0x30: 4, 0x3F: 1}
OBSERVED, PHYSICAL_START, SAMPLED = 1, 3, 5
TIMEKEEPER_ADDRESS = 8
REQUIRED_CAPABILITIES = 0b10011  # timing_trailer | time_sync | sync_listener

results = []


def report(name, ok, detail):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}")


def open_socket(channel):
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    s.setsockopt(socket.SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1)
    s.setsockopt(socket.SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, 1)
    s.setsockopt(socket.SOL_SOCKET, SO_TIMESTAMPING, TIMESTAMPING_FLAGS)
    s.bind((channel,))
    s.settimeout(0.05)
    return s


def send(s, msg_type, address, payload, uuid=0):
    data = payload + bytes([uuid])
    length = next(n for n in FD_LENGTHS if n >= len(data))
    s.send(FRAME.pack((msg_type << 5) | address, length, CANFD_BRS, 0, 0, data.ljust(64, b"\0")))


def receive(s, seconds):
    """Yield (can_id, data, own, timestamp_ns, hardware) for `seconds`."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            raw, ancillary, flags, _ = s.recvmsg(FRAME.size, 256)
        except socket.timeout:
            continue
        can_id, length, _, _, _, data = FRAME.unpack(raw.ljust(FRAME.size, b"\0"))
        if can_id & CAN_ERR_FLAG:
            continue
        sw_ns = hw_ns = 0
        for level, kind, value in ancillary:
            if level == socket.SOL_SOCKET and kind == SO_TIMESTAMPING and len(value) >= 48:
                sw_s, sw_n, _, _, hw_s, hw_n = struct.unpack_from("<qqqqqq", value)
                sw_ns, hw_ns = sw_s * 10**9 + sw_n, hw_s * 10**9 + hw_n
        # MSG_CONFIRM marks frames this socket sent. MSG_DONTROUTE would also match
        # frames from any other local process, such as a running reachAQ.
        yield (can_id & 0x7FF, data[:length], bool(flags & socket.MSG_CONFIRM),
               hw_ns or sw_ns, hw_ns != 0)


def trailer(msg_type, data):
    size = PAYLOAD_SIZES.get(msg_type)
    if size is None or len(data) < size + 1 + TRAILER.size:
        return None
    tag, version, kind, flags, boot_id, sequence, board_us = TRAILER.unpack_from(data, size + 1)
    if tag != 0xA7 or version != 1:
        return None
    return {"kind": kind, "flags": flags, "boot_id": boot_id, "sequence": sequence, "board_us": board_us}


def check_capabilities(s, address):
    send(s, CAPABILITIES_REQUEST, address, b"\0")
    for can_id, data, own, _, _ in receive(s, 1.0):
        if not own and can_id == (CAPABILITIES_RESPONSE << 5) | address:
            _, caps, boot_id = struct.unpack_from("<BII", data)
            t = trailer(CAPABILITIES_RESPONSE, data)
            ok = caps & REQUIRED_CAPABILITIES == REQUIRED_CAPABILITIES and t is not None and t["boot_id"] == boot_id
            report("capabilities", ok, f"caps=0x{caps:08x} boot_id=0x{boot_id:08x} trailer={'yes' if t else 'no'}")
            # Stop here on a failure: a board without the timing features gets no further traffic.
            return boot_id if ok else None
    report("capabilities", False, "no CAPABILITIES_RESPONSE within 1 s")
    return None


def check_time_sync(s, address, boot_id, count=20):
    # A distinctive id range, so replies to reachAQ's own requests (ids from 1) are not counted
    base = 0x7A000000
    sent, answers = {}, {}
    for request_id in range(base + 1, base + count + 1):
        send(s, TIME_SYNC_REQUEST, address, struct.pack("<IQ", request_id, time.perf_counter_ns()))
        for can_id, data, own, ts, _ in receive(s, 0.05):
            if own and can_id == (TIME_SYNC_REQUEST << 5) | address:
                sent[struct.unpack_from("<I", data)[0]] = ts
            elif not own and can_id == (TIME_SYNC_RESPONSE << 5) | address:
                rid, received_us, queued_us = struct.unpack_from("<IQQ", data)
                answers[rid] = (received_us, queued_us, trailer(TIME_SYNC_RESPONSE, data), ts)
    good = [rid for rid, (rx, q, t, _) in answers.items()
            if rid > base and t is not None and t["boot_id"] == boot_id and rx <= q]
    rtts = sorted((answers[r][3] - sent[r]) / 1000 for r in good if r in sent)
    board_us = sorted(answers[r][1] - answers[r][0] for r in good)
    ok = len(good) >= count - 1
    report("time_sync", ok, f"{len(good)}/{count} valid; round trip min {rtts[0] if rtts else float('nan'):.0f} us, "
           f"board handling median {board_us[len(board_us) // 2] if board_us else float('nan')} us")


def check_claps(s, address, boot_id, count=50):
    host_ts, board_us, hardware, bad = {}, {}, True, 0
    for tick in range(1, count + 1):
        send(s, SYNC_CLAP, TIMEKEEPER_ADDRESS, struct.pack("<I", tick))
        for can_id, data, own, ts, hw in receive(s, 0.1):
            if own and can_id == (SYNC_CLAP << 5) | TIMEKEEPER_ADDRESS:
                host_ts[struct.unpack_from("<I", data)[0]] = ts
                hardware = hardware and hw
            elif not own and can_id == (SYNC_CLAP_OBSERVED << 5) | address:
                heard, clapper = struct.unpack_from("<IB", data)
                t = trailer(SYNC_CLAP_OBSERVED, data)
                if t is None or clapper != TIMEKEEPER_ADDRESS or t["kind"] != OBSERVED or t["boot_id"] != boot_id:
                    bad += 1
                else:
                    board_us[heard] = t["board_us"]
    pairs = [(host_ts[k] / 1e9, board_us[k] / 1e6) for k in board_us if k in host_ts]
    if len(pairs) < 3:
        report("claps", False, f"only {len(pairs)} clap(s) matched")
        return
    n = len(pairs)
    mx, my = sum(x for x, _ in pairs) / n, sum(y for _, y in pairs) / n
    slope = sum((x - mx) * (y - my) for x, y in pairs) / sum((x - mx) ** 2 for x, _ in pairs)
    residual_us = sorted(abs(y - (my + slope * (x - mx))) * 1e6 for x, y in pairs)
    p95 = residual_us[int(0.95 * (n - 1))]
    jitter_ok = p95 < 20.0 if hardware else True
    ok = n >= count - 2 and bad == 0 and abs(slope - 1.0) < 1e-3 and jitter_ok
    source = "hardware" if hardware else "kernel software (jitter not judged)"
    report("claps", ok, f"{n}/{count} observed, {bad} malformed, slope {slope:.9f}, residual p95 {p95:.1f} us, "
           f"max {residual_us[-1]:.1f} us vs {source} timestamps")


def check_periodic_trailers(s, address, boot_id):
    kinds, sequences = {}, []
    for can_id, data, own, _, _ in receive(s, 2.0):
        msg_type, sender = can_id >> 5, can_id & 0x1F
        if own or sender != address:
            continue
        t = trailer(msg_type, data)
        if t is None or t["boot_id"] != boot_id:
            continue
        sequences.append(t["sequence"])
        kinds.setdefault(msg_type, set()).add(t["kind"])
    gaps = sum(1 for a, b in zip(sequences, sequences[1:]) if b != a + 1)
    want = {0x08: SAMPLED, 0x0A: SAMPLED, 0x0F: SAMPLED}
    missing = [f"0x{m:02x}" for m, k in want.items() if k not in kinds.get(m, set())]
    report("periodic_trailers", not missing and gaps == 0,
           f"{len(sequences)} trailer frames, {gaps} sequence gap(s), missing SAMPLED on {missing or 'none'}")


def check_tone(s, address):
    send(s, TONE, address, struct.pack("<BHH", 0, 5000, 200), uuid=0x5A)
    for can_id, data, own, _, _ in receive(s, 0.5):
        if not own and can_id == (TONE << 5) | address:
            t = trailer(TONE, data)
            if t is not None and t["kind"] == PHYSICAL_START:
                frequency = struct.unpack_from("<BHH", data)[1]
                report("tone_physical_start", frequency == 5000, f"PHYSICAL_START at {t['board_us']} us, {frequency} Hz")
                return
    report("tone_physical_start", False, "no TONE status with PHYSICAL_START within 0.5 s")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--channel", default="can0")
    parser.add_argument("--address", type=int, default=0)
    parser.add_argument("--play-tone", action="store_true")
    args = parser.parse_args()

    s = open_socket(args.channel)
    boot_id = check_capabilities(s, args.address)
    if boot_id is not None:
        check_time_sync(s, args.address, boot_id)
        check_claps(s, args.address, boot_id)
        check_periodic_trailers(s, args.address, boot_id)
        if args.play_tone:
            check_tone(s, args.address)
    return 0 if results and all(results) else 1


if __name__ == "__main__":
    sys.exit(main())

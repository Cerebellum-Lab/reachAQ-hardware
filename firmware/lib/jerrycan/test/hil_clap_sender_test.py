#!/usr/bin/env python3
"""Tests of hil_clap_sender.py against a stand-in for the CAN socket and the pellet.

Needs no board: python3 hil_clap_sender_test.py. The point of the sender is that its run can fail, so each fault below
(a pellet that hears nothing, one that names another clapper, one that loses claps) is built in and the exit status
asserted. The frame ids and sizes are also read out of jerrycan_types.h, so a change there fails this test.
"""

import contextlib
import io
import os
import re
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hil_clap_sender as sender  # noqa: E402
import hil_timing_check as hil  # noqa: E402

if not hasattr(socket, "MSG_CONFIRM"):  # Linux only. hil_timing_check reads it to tell its own frames from others
    socket.MSG_CONFIRM = 0x800

TYPES_HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "software",
                            "libjerrycan", "inc", "jerrycan_types.h")
PELLET = 0
FAST = {"rate": 200.0, "frames": 10, "drain_s": 0.05}  # 50 ms of traffic instead of 10 s


class FakeBus:
    """Stands in for the socket. Records every frame sent, hands it back as an own frame (MSG_CONFIRM), and answers
    like the pellet at `address` would. `hears` is the set of clap ticks it hears; None means all of them. `says` is
    the clapper_node it reports, or None for the frame's real sender."""

    def __init__(self, address=PELLET, hears=None, says=None, answer_as=None):
        self.address, self.hears, self.says = address, hears, says
        self.answer_as = address if answer_as is None else answer_as
        self.sent, self.queue, self.own_delivered = [], [], 0

    def send(self, raw):
        self.sent.append(decode(raw))
        self.queue.append((raw, socket.MSG_CONFIRM))
        can_id, _, data, _ = self.sent[-1]
        if can_id >> 5 == hil.SYNC_CLAP:
            tick = int.from_bytes(data[:4], "little")
            if self.hears is None or tick in self.hears:
                clapper = can_id & 0x1F if self.says is None else self.says
                payload = sender.OBSERVED.pack(tick, clapper) + b"\0"  # the uuid byte, as the board sends it
                self.queue.append((frame(((hil.SYNC_CLAP_OBSERVED << 5) | self.answer_as), payload), 0))
        elif can_id >> 5 == sender.HEARTBEAT:
            self.queue.append((frame((sender.HEARTBEAT << 5) | self.answer_as, b"\xff\0"), 0))

    def recvmsg(self, bufsize, ancillary_size):
        if not self.queue:
            time.sleep(0.001)
            raise socket.timeout
        raw, flags = self.queue.pop(0)
        self.own_delivered += flags == socket.MSG_CONFIRM
        return raw, [], flags, None


def frame(can_id, data):
    length = next(n for n in hil.FD_LENGTHS if n >= len(data))
    return hil.FRAME.pack(can_id, length, hil.CANFD_BRS, 0, 0, data.ljust(64, b"\0"))


def decode(raw):
    """(can_id, length, data[:length], flags) of a frame as it went on the wire."""
    can_id, length, flags, _, _, data = hil.FRAME.unpack(raw)
    return can_id, length, data[:length], flags


def run(kind="clap", sender_address=31, bus=None, **overrides):
    bus = bus or FakeBus()
    options = dict(FAST, **overrides)
    tally = sender.run(bus, kind, bus.address, sender_address, options["rate"], options["frames"], options["drain_s"])
    return bus, tally


def exit_of(tally):
    return sender.exit_code(tally["sent"], tally["observed"], tally["mismatched"])


def header_value(name):
    text = open(TYPES_HEADER, encoding="utf-8").read()
    return int(re.search(rf"\b{name}\s*=\s*(0[xX][0-9A-Fa-f]+)", text).group(1), 16)


# --- the frames ---

def test_frame_ids_and_sizes_agree_with_the_types_header():
    assert header_value("JERRYCAN_CMD_SYNC_CLAP") == hil.SYNC_CLAP == 0x25
    assert header_value("JERRYCAN_CMD_SYNC_CLAP_OBSERVED") == hil.SYNC_CLAP_OBSERVED == 0x26
    assert header_value("JERRYCAN_CMD_HEARTBEAT") == sender.HEARTBEAT == 0x3F
    text = open(TYPES_HEADER, encoding="utf-8").read()
    assert "SIZE_CHECK(jerrycan_cmd_sync_clap_t, 4)" in text  # the tick
    assert "SIZE_CHECK(jerrycan_cmd_sync_clap_observed_t, 5)" in text  # tick, clapper_node == sender.OBSERVED.size
    assert sender.OBSERVED.size == 5


def test_clap_id_is_0x4a0_or_the_sender_address():
    assert (hil.SYNC_CLAP << 5) == 0x4A0
    for address in range(32):
        bus, _ = run(sender_address=address, frames=3)
        assert {c[0] for c in bus.sent} == {0x4A0 | address}, address
    bus, _ = run(sender_address=0x1F, frames=3)
    assert bus.sent[0][0] == 0x4BF


def test_clap_frame_is_fd_with_brs_and_carries_tick_then_uuid():
    bus, _ = run(sender_address=0x1F)
    assert [c[1] for c in bus.sent] == [5] * 10  # 4 bytes of tick and the uuid byte
    assert {c[3] for c in bus.sent} == {hil.CANFD_BRS}
    assert [c[2] for c in bus.sent] == [tick.to_bytes(4, "little") + b"\0" for tick in range(1, 11)]


def test_heartbeat_goes_to_0x1f_whatever_the_sender():
    for from_address in (None, 0, 31):
        bus, _ = run(kind="heartbeat", sender_address=from_address)
        assert {c[0] for c in bus.sent} == {0x7FF}, from_address  # 0x3F << 5 | 0x1F
        assert {c[1] for c in bus.sent} == {2} and {c[3] for c in bus.sent} == {hil.CANFD_BRS}
        assert {c[2] for c in bus.sent} == {b"\xff\0"}


def test_sends_only_the_chosen_frame_type():
    bus, _ = run(kind="clap", sender_address=0)
    assert {c[0] >> 5 for c in bus.sent} == {hil.SYNC_CLAP}
    bus, _ = run(kind="heartbeat", sender_address=0)
    assert {c[0] >> 5 for c in bus.sent} == {sender.HEARTBEAT}  # no TIME_SYNC, no CAPABILITIES_REQUEST


def test_frames_are_paced_at_the_rate():
    started = time.monotonic()
    bus, _ = run(rate=100.0, frames=10, drain_s=0.0)
    assert 0.085 < time.monotonic() - started < 0.5 and len(bus.sent) == 10


# --- the replies ---

def test_reply_parsing():
    reply = sender.OBSERVED.pack(7, 31) + b"\0"
    assert sender.reply_clapper(0x4C0 | 2, reply, False, 2) == 31
    assert sender.reply_clapper(0x4C0 | 2, reply + bytes(16), False, 2) == 31  # the board time trailer follows
    assert sender.reply_clapper(0x4C0 | 2, reply, True, 2) is None  # our own frame coming back
    assert sender.reply_clapper(0x4C0 | 1, reply, False, 2) is None  # another pellet's reply
    assert sender.reply_clapper(0x4A0 | 2, reply, False, 2) is None  # a clap, not a reply
    assert sender.reply_clapper(0x4C0 | 2, reply[:4], False, 2) is None  # too short to hold a clapper_node
    assert sender.classify("clap", 0x4C0 | 2, reply, False, 2, 31) == "observed"
    assert sender.classify("clap", 0x4C0 | 2, reply, False, 2, 8) == "mismatched"
    assert sender.classify("clap", 0x7E0 | 2, b"\xff\0", False, 2, 31) is None
    assert sender.classify("heartbeat", 0x7E0 | 2, b"\xff\0", False, 2, None) == "observed"
    assert sender.classify("heartbeat", 0x7E0 | 2, b"\xff\0", True, 2, None) is None


def test_exit_rule_is_observed_at_least_sent_minus_one_and_no_mismatch():
    assert sender.exit_code(60, 60, 0) == 0
    assert sender.exit_code(60, 59, 0) == 0  # the last reply may still be in flight
    assert sender.exit_code(60, 58, 0) == 1
    assert sender.exit_code(60, 0, 0) == 1
    assert sender.exit_code(60, 60, 1) == 1  # a second clapper on the bus
    assert sender.exit_code(2, 1, 0) == 0 and sender.exit_code(2, 0, 0) == 1


# --- runs that can fail ---

def test_pellet_that_hears_every_clap_passes_from_0x1f_and_from_its_own_address():
    for from_address in (0x1F, PELLET, 8):
        bus, tally = run(sender_address=from_address)
        assert tally == {"sent": 10, "observed": 10, "mismatched": 0}, (from_address, tally)
        assert exit_of(tally) == 0


def test_pellet_that_hears_nothing_fails():  # the case the run at address 8 could not tell from success
    _, tally = run(bus=FakeBus(hears=set()))
    assert tally == {"sent": 10, "observed": 0, "mismatched": 0}
    assert exit_of(tally) == 1


def test_own_frames_coming_back_are_not_replies():
    bus, tally = run(bus=FakeBus(hears=set()), sender_address=PELLET)
    assert bus.own_delivered == 10 and tally["observed"] == 0 and exit_of(tally) == 1


def test_one_lost_clap_passes_and_two_fail():
    assert exit_of(run(bus=FakeBus(hears=set(range(1, 10))))[1]) == 0
    assert exit_of(run(bus=FakeBus(hears=set(range(1, 9))))[1]) == 1


def test_pellet_that_names_another_clapper_fails():
    _, tally = run(sender_address=31, bus=FakeBus(says=8))
    assert tally == {"sent": 10, "observed": 0, "mismatched": 10}
    assert exit_of(tally) == 1
    _, tally = run(sender_address=31, bus=FakeBus(says=31))
    assert exit_of(tally) == 0


def test_another_pellets_replies_are_not_counted():
    _, tally = run(bus=FakeBus(address=PELLET, answer_as=1))
    assert tally["observed"] == 0 and exit_of(tally) == 1


def test_heartbeat_replies_are_counted_but_never_fail_the_run():
    bus, tally = run(kind="heartbeat", sender_address=None)
    assert tally == {"sent": 10, "observed": 10, "mismatched": 0}
    _, tally = run(kind="heartbeat", sender_address=None, bus=FakeBus(answer_as=1))
    assert tally["observed"] == 0


def test_main_prints_the_counts_and_returns_the_exit_status():
    real_open, real_drain = hil.open_socket, sender.DRAIN_S
    sender.DRAIN_S = 0.05
    argv = ["--channel", "vcan9", "--address", "0", "--from", "0", "--rate", "100", "--duration", "0.1"]
    try:
        for hears, want, line in ((None, 0, "[PASS] clap_receipt: sent 10, observed 10, mismatched 0"),
                                  (set(), 1, "[FAIL] clap_receipt: sent 10, observed 0, mismatched 0")):
            hil.open_socket = lambda channel, hears=hears: FakeBus(hears=hears)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = sender.main(argv)
            assert code == want and line in out.getvalue() and "CAN id 0x4a0" in out.getvalue(), out.getvalue()
    finally:
        hil.open_socket, sender.DRAIN_S = real_open, real_drain


# --- arguments ---

def rejected(*argv):
    with contextlib.redirect_stderr(io.StringIO()), contextlib.suppress(SystemExit):
        sender.parse_args(list(argv))
        return False
    return True


def test_arguments():
    base = ["--channel", "vcan9", "--address", "0", "--duration", "60"]
    args = sender.parse_args(base + ["--from", "31"])
    assert (args.sender, args.rate, args.frame, args.frames) == (31, 1.0, "clap", 60)
    assert sender.parse_args(base + ["--frame", "heartbeat"]).sender is None
    assert rejected(*base)  # clap needs --from
    assert rejected(*base, "--from", "32") and rejected(*base, "--from", "-1")
    assert rejected("--channel", "vcan9", "--address", "32", "--duration", "60", "--from", "0")
    assert rejected("--address", "0", "--duration", "60", "--from", "0")  # no default channel
    assert rejected(*base, "--from", "0", "--rate", "0")
    assert rejected("--channel", "vcan9", "--address", "0", "--duration", "1", "--from", "0")  # one frame cannot fail
    assert rejected(*base, "--from", "0", "--frame", "tone")


if __name__ == "__main__":
    tests = [(name, fn) for name, fn in sorted(globals().items()) if name.startswith("test_")]
    for name, fn in tests:
        fn()
        print(f"ok  {name}")
    print(f"{len(tests)} passed")

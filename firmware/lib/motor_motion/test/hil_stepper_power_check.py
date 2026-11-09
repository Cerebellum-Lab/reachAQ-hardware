#!/usr/bin/env python3
"""Hardware-in-the-loop check of the pellet stepper enable, driver faults and ESTOP. Development tool, not release tooling.

Run on the rig, with the pellet board on can0, reachAQ closed and nothing in the stage's path:

    python3 firmware/lib/motor_motion/test/hil_stepper_power_check.py --board v1.3 --channel can0 --address 0

THIS MOVES THE MOTORS. It sends CAPABILITIES_REQUEST, CFG_READ (never CFG_WRITE, which persists to flash),
STEPPER_HOME, STEPPER_MOVE, SERVO_MOVE, FIXED_XYZ, a HEARTBEAT every second while it waits for a slow ACK, and ESTOP.
ESTOP goes to the board's own address, never to the broadcast address 0x1F that the host's EStop() uses: ESTOP(1) and
ESTOP(0) on a shared bus would stop and release every module, including an e-stop someone else asserted. The pellet
dispatches on the message type and counts a frame to its own address as a host frame, so the same firmware path runs.
The broadcast form is not exercised. Stepper moves stay within half the configured velocity and acceleration and within
15 turns (the host's _STEPPER_MAX_TURNS). FIXED_XYZ is the exception: the firmware runs it at the configured velocity
to positions the script reads and never changes. Servo moves stay inside the servo's configured minimum and maximum
position.

Nothing here may leave a motor running unwatched. A HOME that gets no ACK may still be homing (it has no travel limit,
and a second HOME answers -EBUSY), so the script sends ESTOP at once, reports it, releases ESTOP and stops the run. On
Ctrl-C, SIGTERM or SIGHUP (a dropped ssh session) it asserts ESTOP and releases it, and every axis then needs homing.
Only firmware from this branch handles ESTOP. On older firmware (v2.3.0 and earlier) it does nothing, and a v1.2 board
reports the same capabilities on both, so the script cannot tell which it is talking to: be ready to cut power.

Each check prints [PASS|FAIL|SKIP] name: detail. A check that cannot run prints SKIP and never PASS. Exit status: 0 only
if every selected check passed, 1 on any FAIL, 2 for bad arguments (nothing is sent), 3 if nothing failed but something
was skipped, with a line naming what was skipped. After each check the script releases ESTOP, re-homes every axis that
needs it, asks the board for its capabilities again, so that one failure does not cascade and a restart of the board is
noticed, and audits the ACKs: one whose uuid the script never sent as a command, or a second one for a command, is a
FAIL naming the uuid. A stale ACK that carries another uuid is otherwise ignored, because a check looks for its own.

estop_multiblock_restart moves its axis just over 1.25 DMA blocks (1280 pulses: 6.67 turns at 24 steps per revolution
and microsteps 8, 3.33 turns at 48), so that a block is still queued when the ESTOP lands, and homes the axis again at
the end. It is skipped if that is further than --max-turns (default 8), or if the ESTOP would not land inside the first
block.

silence_cycles moves one axis +0.25 turn after each of 30 silences, rotating the axes, so that steps lost at the enable
edge add up in one direction, and then finds the limit switch from 0.1 turn short of it. It takes about 17 minutes, 16
of them the 30 silences of 32 s; --cycles must be a multiple of 3, so that every axis moves.
"""

import argparse
import collections
import itertools
import math
import signal
import socket
import struct
import sys
import time
import traceback

CAN_RAW_FD_FRAMES = 5
CANFD_BRS = 0x01
CAN_ERR_FLAG = 0x20000000
CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
FRAME = struct.Struct("<IBBBB64s")
FD_LENGTHS = (0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64)
BROADCAST = 0x1F

ESTOP = 0x00
STEPPER_MOVE, SERVO_MOVE, STEPPER_HOME = 0x02, 0x03, 0x04
CFG_READ, CFG_RESPONSE = 0x06, 0x07
STEPPER_STATUS, SERVO_STATUS = 0x16, 0x17
BOOTLOADER_COMMAND, BOOTLOADER_DATA = 0x18, 0x1A
FIXED_XYZ = 0x1C
CAPABILITIES_REQUEST, CAPABILITIES_RESPONSE = 0x23, 0x24
RSP_ACK = 0x30
HEARTBEAT = 0x3F

# Payload size of every frame this script sends. The uuid byte follows the payload, so a short payload would put it
# where the board does not look.
TX_PAYLOAD_SIZES = {ESTOP: 1, STEPPER_MOVE: 13, SERVO_MOVE: 13, STEPPER_HOME: 1, CFG_READ: 28, FIXED_XYZ: 1,
                    CAPABILITIES_REQUEST: 1, HEARTBEAT: 1}
MOTION_TYPES = frozenset((STEPPER_MOVE, SERVO_MOVE, STEPPER_HOME, FIXED_XYZ))
# Commands the board ACKs, each with the uuid it was sent with. ESTOP, HEARTBEAT and CFG_READ are never ACKed, so they
# carry uuid 0, which the script never uses for a command.
ACKED_TYPES = MOTION_TYPES | {CAPABILITIES_REQUEST}
# Frames only a host sends. Seeing one addressed to the board (or to everyone) means another host is driving it.
HOST_ONLY_TYPES = frozenset((ESTOP, STEPPER_MOVE, SERVO_MOVE, STEPPER_HOME, CFG_READ, BOOTLOADER_COMMAND,
                             BOOTLOADER_DATA, FIXED_XYZ, CAPABILITIES_REQUEST))

AXES = (0, 1, 2)
STEPPER_MOVE_RELATIVE = 0x80  # abs_or_rel, bit 7 of the first payload byte; motor_id is bits 0-1, save is bit 2
CFG_STEPPER, CFG_SERVO = 0, 1
CFG_ERROR_BIT = 0x04
STATUS = struct.Struct("<BBBBff")  # motor_id, status, homing_status, limit_switch, position, send_position
SERVO_STATUS_FIELDS = struct.Struct("<BBf")  # motor_id, status, position
CAPABILITIES = struct.Struct("<BII")  # wire_schema_version, capabilities, boot_id
ACK = struct.Struct("<i")
STEPPER_CFG = struct.Struct("<BBHffff")  # type, bits, microsteps, steps_per_revolution, max vel, max accel, homing vel
SERVO_CFG = struct.Struct("<BBhffffff")  # type, bits, rsvd, min pos, max pos, min pwm, max pwm, max vel, max accel

# jerrycan_types.h
CAP_FINITE_GPIO_PULSE = 1 << 2
CAP_STEPPER_DRIVER_FAULT = 1 << 6
WIRE_SCHEMA_VERSION = 1
STATUS_DRIVER_FAULT = 0x01
STATUS_ENABLE_OFF = 0x02

# Errno values as they travel on the wire. These are the firmware's, from picolibc (CONFIG_PICOLIBC=y): its ETIMEDOUT is
# 116, where Linux's is 110. Do not replace them with Python's errno module.
ACK_EPERM = -1
ACK_EIO = -5
ACK_EBUSY = -16
ACK_ETIMEDOUT = -116
ACK_NAMES = {0: "0", ACK_EPERM: "-EPERM", ACK_EIO: "-EIO", ACK_EBUSY: "-EBUSY", ACK_ETIMEDOUT: "-ETIMEDOUT"}

STEPPER_MAX_TURNS = 15.0  # _STEPPER_MAX_TURNS in the host
SPEED_FRACTION = 0.5  # of the configured velocity and acceleration
STATUS_PERIOD_S = 0.1  # CONFIG_LIB_JERRYCAN_STEPPER_STATUS_TX_PERIOD_MS
STATUS_SETTLE_S = 0.3  # status frames sent before a host frame took effect may still be in flight
ACK_SETTLE_S = 0.5  # after an ACK, keep listening this long so that a second ACK would be counted
ESTOP_SETTLE_S = 1.0
KEEPALIVE_S = 1.0
ESTOP_DELAY_S = 0.6  # from the move frame to the ESTOP
STEPPER_BLOCK_PULSES = 1024  # STEPPER_BUFFER_SIZE, pulses in one DMA block (BUFS_PER_MOTOR 2: both queued at the start)
MULTIBLOCK_PULSES = STEPPER_BLOCK_PULSES * 5 // 4  # 1.25 blocks: one running and a second queued behind it
MULTIBLOCK_MIN_S = 2.0  # slowest the multi-block move is stretched to, so the ESTOP lands in it
MULTIBLOCK_MAX_TURNS = 8.0  # --max-turns: the furthest estop_multiblock_restart may take an axis from its switch
POSITION_TOLERANCE_TURNS = 0.02
FIXED_XYZ_ESTOP_DELAY_S = 0.25  # the firmware steps its X, Z, Y sequence on the 100 ms status tick
FIXED_XYZ_SETTLE_S = 1.5
SEND_POSITION_EPSILON = 1e-6
TMC_CLOCK_HZ = 12e6  # TMC2209 internal clock: TSTEP counts in 1/fCLK per 1/256 microstep
TPWMTHRS = 1500  # firmware's StealthChop threshold. TSTEP above it keeps the driver in StealthChop
DIAG_TSTEP = 2 * TPWMTHRS
SERVO_MIN_SPAN = 2.0
SERVO_MIN_MOVE = 2.0
SERVO_MOVE_FRACTION = 0.4
SERVO_MIN_S = 1.5
SILENCE_EARLY_S = 0.5  # silence_midmove: the motion may stop this long before the timeout
SILENCE_LATE_S = 1.0  # and this long after it
SILENCE_STEP_TURNS = 0.25  # silence_cycles: the move after each silence, on one axis
SILENCE_SHORT_TURNS = 0.1  # then the axis goes back to this far short of its switch
SILENCE_LONG_TURNS = 0.2  # and on this much further, which has to reach the switch
# ESTOP is handled only by firmware from this branch on, and v2.3.0 and v2.3.1 report the same capabilities on a v1.2.
ESTOP_CAVEAT = "Firmware older than this branch ignores ESTOP: check that the motors stopped, be ready to cut power."

results = []  # (name, True | False | None)


def say(text):
    """print(), except that a terminal that has gone away (a dropped ssh session) must not stop the cleanup."""
    try:
        print(text, flush=True)
    except OSError:
        pass


def report(name, ok, detail):
    """ok is True for PASS, False for FAIL and None for SKIP."""
    results.append((name, ok))
    say(f"[{'SKIP' if ok is None else 'PASS' if ok else 'FAIL'}] {name}: {detail}")


class Skip(Exception):
    """The check cannot run. The message is printed as SKIP."""


class Fail(Exception):
    """The check cannot go on and has failed. The message is printed as FAIL."""


class Abort(Exception):
    """A motor may be running that the script cannot account for. ESTOP has been sent; no further check may run."""


def fmt_errors(errors):
    return "[" + ", ".join(ACK_NAMES.get(e, str(e)) for e in errors) + "]"


def open_socket(channel):
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    s.setsockopt(socket.SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1)
    s.bind((channel,))
    s.settimeout(0.05)
    return s


def build_frame(msg_type, address, payload, uuid=0):
    size = TX_PAYLOAD_SIZES[msg_type]
    if len(payload) > size:
        raise ValueError(f"payload of {len(payload)} bytes for type 0x{msg_type:02x}, which carries {size}")
    data = payload.ljust(size, b"\0") + bytes([uuid])
    length = next(n for n in FD_LENGTHS if n >= len(data))
    return FRAME.pack((msg_type << 5) | address, length, CANFD_BRS, 0, 0, data.ljust(64, b"\0"))


def send(s, msg_type, address, payload, uuid=0):
    s.send(build_frame(msg_type, address, payload, uuid))


def decode_message(raw):
    """One received frame as (can_id, data), or None for an error, extended or remote frame."""
    can_id, length, _, _, _, data = FRAME.unpack(raw.ljust(FRAME.size, b"\0"))
    if can_id & (CAN_ERR_FLAG | CAN_EFF_FLAG | CAN_RTR_FLAG):
        return None
    return can_id & 0x7FF, data[:length]


StepperStatus = collections.namedtuple("StepperStatus", "t motor status homing limit position send_position")
Ack = collections.namedtuple("Ack", "t uuid error")
StepperCfg = collections.namedtuple("StepperCfg", "microsteps steps_per_rev max_velocity max_acceleration "
                                                  "homing_velocity")
ServoCfg = collections.namedtuple("ServoCfg", "min_position max_position max_velocity max_acceleration")


def move_timeout(distance, velocity, acceleration):
    return 2.0 * (abs(distance) / velocity + 2.0 * velocity / acceleration) + 5.0


def home_timeout(cfg):
    homing = cfg.homing_velocity
    seconds = STEPPER_MAX_TURNS / homing if math.isfinite(homing) and homing > 0 else 60.0
    return min(max(seconds + 5.0, 10.0), 180.0)


def move_pulses_by(seconds, distance, velocity, acceleration, pulses_per_turn):
    """Pulses a stepper has emitted `seconds` into a move of `distance` turns, from the firmware's sinusoidal profile
    (displacement_hat() in motor_math.c). The firmware truncates the move to whole pulses, so the total is
    int(distance x pulses_per_turn). The board starts a little after the frame was sent, so this errs high."""
    y_f = abs(distance)
    y_s = y_f / 2.0
    y_aux = velocity * velocity / acceleration
    y_a = min(y_s, y_aux)
    v_w = math.sqrt(y_s * acceleration) if y_s < y_aux else velocity
    t_a = 2.0 * v_w / acceleration
    omega = 2.0 * math.pi / t_a
    k_s = t_a * v_w / (4.0 * math.pi ** 2)
    t_s = (y_s - y_a) / velocity + t_a
    t_t = 2.0 * t_s

    def accelerating(t):
        return max(0.0, acceleration / 4.0 * t * t + k_s * (math.cos(omega * t) - 1.0))

    if seconds <= 0.0:
        turns = 0.0
    elif seconds <= t_a:
        turns = accelerating(seconds)
    elif seconds <= t_t - t_a:
        turns = y_s + v_w * (seconds - t_s)
    elif seconds < t_t:
        turns = y_f - accelerating(t_t - seconds)
    else:
        turns = y_f
    return min(int(turns * pulses_per_turn), int(y_f * pulses_per_turn))


class Board:
    """The pellet at --address, seen through a raw CAN socket. Everything that arrives is kept with its time."""

    def __init__(self, s, args):
        self.s = s
        self.args = args
        self.address = args.address
        self.version = args.board
        self.acks = []
        self.status_log = collections.deque(maxlen=20000)
        self.servo_status = {}
        self.caps = []
        self.cfgs = []
        self.host_seen = set()
        self.last_tx = time.monotonic()
        self.estop_asserted = False
        self.motion_sent = False  # a command that can move a motor has gone out
        self.abort_reason = None
        self.needs_home = set()
        self.boot_id = None
        self.ack_counts = {}  # uuid of every command sent that the board ACKs -> ACKs received for it
        self.ack_problems = []
        self.acks_matched = 0
        self._absorbed = 0  # acks[:_absorbed] have been counted
        self._uuids = itertools.cycle(range(1, 256))
        self._stepper_cfgs = {}
        self._servo_cfgs = {}

    # --- receive ----------------------------------------------------------------------------------------------

    def _handle(self, t, can_id, data):
        msg_type, sender = can_id >> 5, can_id & 0x1F
        if msg_type in HOST_ONLY_TYPES and sender in (self.address, BROADCAST):
            self.host_seen.add(msg_type)
        if sender != self.address:
            return
        if msg_type == RSP_ACK and len(data) > ACK.size:
            self.acks.append(Ack(t, data[ACK.size], ACK.unpack_from(data)[0]))
        elif msg_type == STEPPER_STATUS and len(data) >= STATUS.size:
            self.status_log.append(StepperStatus(t, *STATUS.unpack_from(data)))
        elif msg_type == SERVO_STATUS and len(data) >= SERVO_STATUS_FIELDS.size:
            motor, status, position = SERVO_STATUS_FIELDS.unpack_from(data)
            self.servo_status[motor] = (t, status, position)
        elif msg_type == CAPABILITIES_RESPONSE and len(data) > CAPABILITIES.size:
            self.caps.append((t, data[CAPABILITIES.size]) + CAPABILITIES.unpack_from(data))
        elif msg_type == CFG_RESPONSE and len(data) >= SERVO_CFG.size:
            self.cfgs.append((t, bytes(data)))

    def pump_until(self, deadline):
        """Receive until `deadline` (time.monotonic), keeping what arrives."""
        while time.monotonic() < deadline:
            try:
                raw = self.s.recv(FRAME.size)
            except socket.timeout:
                continue
            message = decode_message(raw)
            if message is not None:
                self._handle(time.monotonic(), *message)

    def pump(self, seconds):
        self.pump_until(time.monotonic() + seconds)

    def flush(self):
        """Drop what queued in the kernel while nothing was reading, such as during an operator prompt. Frames read
        late would otherwise be stamped as new."""
        self.s.settimeout(0.0)
        try:
            while True:
                self.s.recv(FRAME.size)
        except (BlockingIOError, socket.timeout):
            pass
        finally:
            self.s.settimeout(0.05)

    # --- send -------------------------------------------------------------------------------------------------

    def count_acks(self):
        """Match the ACKs received since the last call against the commands sent. An ACK with a uuid the script never
        sent as a command, or a further ACK for one command, goes to ack_problems. A uuid is unique per command (they
        wrap only after 255), so a repeat means the board ACKed twice."""
        for ack in self.acks[self._absorbed:]:
            seen = self.ack_counts.get(ack.uuid)
            if seen is None:
                self.ack_problems.append(f"uuid {ack.uuid} was ACKed {fmt_errors([ack.error])} but never sent")
                continue
            if seen:
                self.ack_problems.append(f"uuid {ack.uuid} was ACKed again {fmt_errors([ack.error])}")
            else:
                self.acks_matched += 1
            self.ack_counts[ack.uuid] = seen + 1
        self._absorbed = len(self.acks)

    def audit_acks(self, name):
        """Report, as a FAIL, what count_acks found since the last audit."""
        self.count_acks()
        problems, self.ack_problems = self.ack_problems, []
        if problems:
            counts = {}
            for problem in problems:
                counts[problem] = counts.get(problem, 0) + 1
            report(f"{name}/acks", False, "; ".join(p if n == 1 else f"{p} x{n}" for p, n in counts.items()))

    def send_frame(self, msg_type, payload, dst=None):
        """Send with a fresh non-zero uuid, or uuid 0 for a frame the board never ACKs. Returns (uuid, time sent)."""
        uuid = 0
        if msg_type in ACKED_TYPES:
            self.count_acks()  # before the uuid can be reused
            uuid = next(self._uuids)
            self.ack_counts[uuid] = 0
        if msg_type in MOTION_TYPES:
            self.motion_sent = True
        send(self.s, msg_type, self.address if dst is None else dst, payload, uuid)
        self.last_tx = time.monotonic()
        return uuid, self.last_tx

    def estop(self, asserted):
        """ESTOP to this board's address, not the broadcast one: on a shared bus the broadcast form would stop and
        release every module. Returns the time it was sent."""
        if asserted:
            self.estop_asserted = True
            self.needs_home.update(AXES)  # every axis needs a home after an ESTOP
        sent = self.send_frame(ESTOP, bytes([1 if asserted else 0]))[1]
        if not asserted:
            self.estop_asserted = False
        return sent

    def stop_motion(self):
        """ESTOP, then release it: stops whatever may be moving and leaves the board usable. Every axis needs a home
        afterwards. Does nothing if no command that moves a motor has been sent since the last stop."""
        if not self.motion_sent:
            return
        self.estop(True)
        self.pump(0.3)
        self.estop(False)
        self.motion_sent = False  # stopped: a second call does nothing until another command moves a motor
        say("ESTOP asserted and released at the board: every axis now needs homing. " + ESTOP_CAVEAT)

    def move_stepper(self, axis, turns, velocity, acceleration, relative=True):
        cfg = self.stepper_cfg(axis)
        if abs(turns) > STEPPER_MAX_TURNS or not 0.0 < velocity <= SPEED_FRACTION * cfg.max_velocity * 1.0001 \
                or not 0.0 < acceleration <= SPEED_FRACTION * cfg.max_acceleration * 1.0001:
            raise ValueError(f"refusing stepper move {turns} turns at {velocity}, {acceleration}")
        first = (axis & 3) | (STEPPER_MOVE_RELATIVE if relative else 0)
        return self.send_frame(STEPPER_MOVE, bytes([first]) + struct.pack("<fff", turns, velocity, acceleration))

    def move_servo(self, motor, position, velocity, acceleration):
        cfg = self.servo_cfg(motor)
        if not cfg.min_position <= position <= cfg.max_position \
                or not 0.0 < velocity <= SPEED_FRACTION * cfg.max_velocity * 1.0001 \
                or not 0.0 < acceleration <= SPEED_FRACTION * cfg.max_acceleration * 1.0001:
            raise ValueError(f"refusing servo move to {position} at {velocity}, {acceleration}")
        return self.send_frame(SERVO_MOVE, bytes([motor & 3]) + struct.pack("<fff", position, velocity, acceleration))

    # --- waiting for answers ----------------------------------------------------------------------------------

    def acks_for(self, uuid, since):
        return [a for a in self.acks if a.uuid == uuid and a.t >= since]

    def wait_acks(self, uuid, since, timeout, settle=ACK_SETTLE_S, keepalive=True):
        """Listen until an ACK with `uuid` arrives (at most `timeout` s), then `settle` s more, so that a second ACK
        would be counted. Returns every ACK with that uuid since `since`. With keepalive, a HEARTBEAT goes out each
        second of waiting so that a slow move is not cut short by the host-silence timeout."""
        deadline = time.monotonic() + timeout
        while not self.acks_for(uuid, since) and time.monotonic() < deadline:
            self.pump(0.05)
            if keepalive and time.monotonic() - self.last_tx >= KEEPALIVE_S:
                self.send_frame(HEARTBEAT, bytes([0xFF]))
        if self.acks_for(uuid, since):
            self.pump(settle)
        return self.acks_for(uuid, since)

    def latest_status(self, since):
        """The newest STEPPER_STATUS of each motor received at or after `since`."""
        latest = {}
        for st in self.status_log:
            if st.t >= since:
                latest[st.motor] = st
        return latest

    def statuses_between(self, start, end):
        return [st for st in self.status_log if start <= st.t <= end]

    def settled_status(self):
        """STEPPER_STATUS of each motor as it is once the last frame sent has taken effect."""
        start = max(self.last_tx + STATUS_SETTLE_S, time.monotonic())
        self.pump_until(start + 2 * STATUS_PERIOD_S)
        return self.latest_status(start)

    def quiet(self, seconds):
        """Listen, sending nothing, until `seconds` after the last frame this script sent. Returns that time."""
        end = self.last_tx + seconds
        self.pump_until(end)
        return end

    def request_capabilities(self, timeout=1.0):
        """(schema, capabilities, boot_id), or None if the board does not answer."""
        uuid, sent = self.send_frame(CAPABILITIES_REQUEST, b"\0")
        deadline = sent + timeout
        while time.monotonic() < deadline:
            self.pump(0.05)
            for t, response_uuid, schema, caps, boot_id in self.caps:
                if response_uuid == uuid and t >= sent:
                    return schema, caps, boot_id
        return None

    # --- configuration, read only -----------------------------------------------------------------------------

    def read_cfg(self, kind, motor):
        sent = self.send_frame(CFG_READ, bytes([kind, motor]))[1]
        deadline = sent + 1.0
        while time.monotonic() < deadline:
            self.pump(0.05)
            for t, data in self.cfgs:
                if t >= sent and data[0] == kind and data[1] & 3 == motor:
                    if data[1] & CFG_ERROR_BIT:
                        raise Fail(f"the board reports an error reading the configuration of motor {motor}")
                    return data
        raise Fail(f"no CFG_RESPONSE for {'stepper' if kind == CFG_STEPPER else 'servo'} {motor} within 1 s")

    def stepper_cfg(self, axis):
        if axis not in self._stepper_cfgs:
            _, _, microsteps, spr, vmax, amax, homing = STEPPER_CFG.unpack_from(self.read_cfg(CFG_STEPPER, axis))
            self._stepper_cfgs[axis] = StepperCfg(microsteps, spr, vmax, amax, homing)
        return self._stepper_cfgs[axis]

    def servo_cfg(self, motor):
        if motor not in self._servo_cfgs:
            _, _, _, lo, hi, _, _, vmax, amax = SERVO_CFG.unpack_from(self.read_cfg(CFG_SERVO, motor))
            self._servo_cfgs[motor] = ServoCfg(lo, hi, vmax, amax)
        return self._servo_cfgs[motor]

    def limits(self, axis):
        """The axis's configuration and the speed and acceleration this script may use on it."""
        cfg = self.stepper_cfg(axis)
        v, a = SPEED_FRACTION * cfg.max_velocity, SPEED_FRACTION * cfg.max_acceleration
        if not (v > 0.0 and a > 0.0 and cfg.steps_per_rev > 0.0 and cfg.microsteps > 0
                and math.isfinite(v) and math.isfinite(a) and math.isfinite(cfg.steps_per_rev)):
            raise Skip(f"axis {axis} configuration is not usable: {cfg}")
        return cfg, v, a

    def servo_position(self, motor):
        start = time.monotonic()
        self.pump(0.3)
        entry = self.servo_status.get(motor)
        if entry is None or entry[0] < start:
            raise Fail(f"no SERVO_STATUS for servo {motor}")
        return entry[2]

    # --- commands that need a known state ---------------------------------------------------------------------

    def home(self, axis):
        """Home one axis. Returns (error codes of every ACK for it, seconds to the first one).

        A HOME has no travel limit and a second one answers -EBUSY, so one that gets no ACK may still be moving the
        axis: ESTOP goes out at once, the axis is marked as needing a home, and Abort stops the run. An error ACK also
        leaves the axis needing a home."""
        cfg = self.stepper_cfg(axis)
        timeout = home_timeout(cfg)
        uuid, sent = self.send_frame(STEPPER_HOME, bytes([axis]))
        acks = self.wait_acks(uuid, sent, timeout)
        errors = [a.error for a in acks]
        if errors == [0]:
            self.needs_home.discard(axis)
        else:
            self.needs_home.add(axis)
        if not acks:
            self.estop(True)
            self.pump(0.3)  # let the stop take effect before restore releases it
            self.abort_reason = (f"axis {axis} did not ACK its home within {timeout:.0f} s and may still be moving: "
                                 f"ESTOP sent to the board, run stopped, every axis needs homing. {ESTOP_CAVEAT}")
            raise Abort(self.abort_reason)
        return errors, acks[0].t - sent

    def preflight(self):
        """Nothing is moved. The board must answer, and no other host may be talking to it. True if ready."""
        self.pump(1.0)
        if self.host_seen:
            report("preflight", False, "another host is sending to this board or the bus (frame types "
                   + ", ".join(f"0x{t:02x}" for t in sorted(self.host_seen)) + "); close reachAQ and the updater. "
                   "Nothing was sent.")
            return False
        response = self.request_capabilities()
        if response is None:
            report("preflight", False, f"no CAPABILITIES_RESPONSE from address {self.address} within 1 s; "
                   "no motion command was sent")
            return False
        self.boot_id = response[2]
        return True

    def restore(self, name):
        """Leave the board as the next check expects it: ESTOP released, every axis that needs a home homed, and the
        same boot. False if the run must stop: the board stopped answering, or a home got no ACK (then ESTOP is only
        released, and nothing is homed or sent after that). Last, audit the ACKs received since the previous audit."""
        try:
            if self.estop_asserted:
                self.estop(False)
                self.pump(0.3)
            if self.abort_reason is not None:
                return False
            for axis in sorted(self.needs_home):
                errors, _ = self.home(axis)
                if errors == [0]:
                    say(f"  note: re-homed axis {axis} after {name}")
                else:
                    report(f"{name}/rehome", False, f"axis {axis} home ACK {fmt_errors(errors)}, want [0]")
            response = self.request_capabilities()
            if response is None:
                report(f"{name}/alive", False, "the board stopped answering; no further command will be sent")
                return False
            if response[2] != self.boot_id:
                report(f"{name}/boot_id", False,
                       f"boot_id 0x{self.boot_id:08x} -> 0x{response[2]:08x}: the board restarted during {name}")
                self.boot_id = response[2]
            return True
        except Abort as abort:
            report(f"{name}/rehome", False, str(abort))
            return False
        finally:
            if self.estop_asserted:  # a home inside this method may have sent ESTOP: release it, whatever happened
                self.estop(False)
                self.pump(0.3)
            self.audit_acks(name)


def prompt_operator(text, on_eof):
    say(f"\n>>> {text}")
    try:
        input("    Press Enter when done: ")
    except EOFError:
        raise on_eof("needs an operator at the terminal (stdin is closed)")


# --- the checks -----------------------------------------------------------------------------------------------
# Each takes the Board and returns (problems, info): problems is a list of what did not match, empty for a PASS.
# A check that cannot run raises Skip; one that cannot go on raises Fail.


def check_capabilities(b):
    response = b.request_capabilities()
    if response is None:
        raise Fail("no CAPABILITIES_RESPONSE within 1 s")
    schema, caps, boot_id = response
    want_bit6 = b.version == "v1.3"
    have_bit6 = bool(caps & CAP_STEPPER_DRIVER_FAULT)
    problems = []
    if schema != WIRE_SCHEMA_VERSION:
        problems.append(f"schema {schema}, want {WIRE_SCHEMA_VERSION}")
    if not caps & CAP_FINITE_GPIO_PULSE:
        problems.append("bit 2 (finite GPIO pulse) clear")
    if have_bit6 != want_bit6:
        problems.append(f"bit 6 (stepper driver fault) {'set' if have_bit6 else 'clear'}, want "
                        f"{'set' if want_bit6 else 'clear'} on {b.version}")
    return problems, f"caps=0x{caps:08x} schema={schema} boot_id=0x{boot_id:08x}"


def check_status_idle(b):
    b.send_frame(CAPABILITIES_REQUEST, b"\0")  # any frame addressed to the board is a host frame
    snapshot = b.settled_status()
    problems = []
    for motor in AXES:
        if motor not in snapshot:
            problems.append(f"no STEPPER_STATUS from motor {motor}")
        elif snapshot[motor].status != 0:
            problems.append(f"motor {motor} status 0x{snapshot[motor].status:02x}, want 0")
    return problems, "status " + ", ".join(
        f"{m}=0x{snapshot[m].status:02x}" if m in snapshot else f"{m}=none" for m in AXES)


def check_home_and_move(b):
    problems, notes = [], []
    # All three before anything moves, so that a Skip cannot hide an earlier FAIL.
    axis_limits = {axis: b.limits(axis) for axis in AXES}
    for axis in AXES:
        cfg, v, a = axis_limits[axis]
        errors, took = b.home(axis)
        if errors != [0]:
            problems.append(f"axis {axis} home ACK {fmt_errors(errors)}, want [0]")
            continue
        uuid, sent = b.move_stepper(axis, 1.0, v, a)
        acks = b.wait_acks(uuid, sent, move_timeout(1.0, v, a))
        errors = [x.error for x in acks]
        if errors != [0]:
            b.needs_home.add(axis)
            problems.append(f"axis {axis} +1 turn ACK {fmt_errors(errors)}, want [0] with uuid {uuid}")
            continue
        status = b.settled_status().get(axis)
        if status is None or abs(status.position - 1.0) > POSITION_TOLERANCE_TURNS:
            problems.append(f"axis {axis} reports position "
                            f"{'none' if status is None else format(status.position, '.3f')} after +1 turn, want 1.000")
        notes.append(f"axis {axis}: home {took:.1f} s, +1 turn {acks[0].t - sent:.1f} s")
    return problems, "; ".join(notes)


def check_estop_multiblock_restart(b):
    # Axis 2 by default: a stop that ACKs an idle axis's stale uuid shows only when the mover is not axis 0.
    axis = 2 if b.args.axis is None else b.args.axis
    cfg, v_cap, a = b.limits(axis)
    pulses_per_turn = cfg.steps_per_rev * cfg.microsteps
    # The smallest move over 1.25 blocks, a hair over so that float32 cannot truncate it to 1279 pulses. The check only
    # needs a block still queued when the ESTOP lands, and every turn further is travel the axis may not have.
    distance = MULTIBLOCK_PULSES / pulses_per_turn * (1.0 + 1e-6)
    total = int(distance * pulses_per_turn)  # the firmware truncates a move to whole pulses
    if distance > b.args.max_turns:
        raise Skip(f"over 1.25 blocks is {distance:.2f} turns ({total} pulses at {pulses_per_turn:g} pulses per turn), "
                   f"over --max-turns {b.args.max_turns:g}")
    v = min(v_cap, distance / MULTIBLOCK_MIN_S)
    delay = min(ESTOP_DELAY_S, 0.3 * distance / v)
    expected = move_pulses_by(delay, distance, v, a, pulses_per_turn)
    if expected >= STEPPER_BLOCK_PULSES:
        raise Skip(f"the ESTOP at {delay:.2f} s would land after about {expected} of {total} pulses, not below the "
                   f"first block's {STEPPER_BLOCK_PULSES}: the stale-block case would not be shown")
    problems, notes = [], []
    errors, _ = b.home(axis)
    if errors != [0]:
        raise Fail(f"axis {axis} home ACK {fmt_errors(errors)}, want [0]; nothing else was tried")

    # ESTOP while a multi-block move is running: one ACK, -EPERM, with the move's uuid
    uuid, sent = b.move_stepper(axis, distance, v, a)
    b.pump_until(sent + delay)
    estop_at = b.estop(True)
    acks = b.wait_acks(uuid, sent, 3.0, settle=ESTOP_SETTLE_S)
    errors = [x.error for x in acks]
    if any(x.t <= estop_at for x in acks):
        problems.append("the move was ACKed before the ESTOP, so it was not stopped mid-move (inconclusive)")
    if errors != [ACK_EPERM]:
        problems.append(f"ESTOP mid-move: ACKs {fmt_errors(errors)} for uuid {uuid}, want exactly [-EPERM]")
    else:
        notes.append(f"ESTOP at {estop_at - sent:.2f} s, about {expected} of {total} pulses expected by the planned "
                     f"{delay:.2f} s (block of {STEPPER_BLOCK_PULSES}), ACK -EPERM at {acks[0].t - sent:.2f} s")

    # a move while ESTOP is asserted
    uuid, sent = b.move_stepper(axis, distance, v, a)
    errors = [x.error for x in b.wait_acks(uuid, sent, 3.0)]
    if errors != [ACK_EPERM]:
        problems.append(f"move during ESTOP: ACK {fmt_errors(errors)}, want [-EPERM]")

    # released, but not homed
    b.estop(False)
    b.pump(0.2)
    uuid, sent = b.move_stepper(axis, distance, v, a)
    errors = [x.error for x in b.wait_acks(uuid, sent, 3.0)]
    if errors != [ACK_EBUSY]:
        problems.append(f"move after ESTOP(0) before homing: ACK {fmt_errors(errors)}, want [-EBUSY]")

    # home, then the same move must run
    errors, took = b.home(axis)
    if errors != [0]:
        problems.append(f"home after ESTOP: ACK {fmt_errors(errors)}, want [0]")
        return problems, "; ".join(notes)
    uuid, sent = b.move_stepper(axis, distance, v, a)
    acks = b.wait_acks(uuid, sent, move_timeout(distance, v, a))
    errors = [x.error for x in acks]
    if errors != [0]:
        b.needs_home.add(axis)  # an ACK that never came leaves the move running or stuck: try to home it again
        problems.append(f"move after re-homing: ACK {fmt_errors(errors)}, want [0]")
    else:
        elapsed = acks[0].t - sent
        status = b.settled_status().get(axis)
        if elapsed < 0.5 * distance / v:
            problems.append(f"move after re-homing ACKed after {elapsed:.2f} s, under half of the {distance / v:.2f} s "
                            "it takes: it did not run")
        if status is None or abs(status.position - distance) > max(POSITION_TOLERANCE_TURNS, 4.0 / pulses_per_turn):
            problems.append(f"position after the move {'none' if status is None else format(status.position, '.3f')},"
                            f" want {distance:.3f}")
        notes.append(f"axis {axis}: {distance:.3f} turns ({total} pulses) at {v:.3f} turn/s; "
                     f"restart move ACK 0 after {elapsed:.2f} s")
        # The move left the axis distance turns out. Leave it home, and let a home that fails fail the check.
        errors, took = b.home(axis)
        if errors != [0]:
            problems.append(f"home after the restart move: ACK {fmt_errors(errors)}, want [0]")
        else:
            notes.append(f"home back ACK 0 after {took:.1f} s")
    return problems, "; ".join(notes)


def check_recover_at_limit(b):
    axis = b.args.axis or 0
    cfg, v, a = b.limits(axis)
    errors, _ = b.home(axis)
    if errors != [0]:
        raise Fail(f"axis {axis} home ACK {fmt_errors(errors)}, want [0]; nothing else was tried")
    status = b.settled_status().get(axis)
    if status is None:
        raise Fail(f"no STEPPER_STATUS from motor {axis}")
    if status.limit != 1:
        raise Skip(f"axis {axis} reads limit_switch={status.limit} after homing, so it is not sitting on its switch")
    problems = []
    b.estop(True)
    b.pump(0.3)
    b.estop(False)
    b.pump(0.3)
    errors, _ = b.home(axis)
    if errors != [0]:
        problems.append(f"home at the limit after ESTOP: ACK {fmt_errors(errors)}, want [0]")
        return problems, ""
    uuid, sent = b.move_stepper(axis, 1.0, v, a)
    acks = b.wait_acks(uuid, sent, move_timeout(1.0, v, a))
    errors = [x.error for x in acks]
    if errors != [0]:
        b.needs_home.add(axis)
        problems.append(f"move after re-homing at the limit: ACK {fmt_errors(errors)}, want [0]")
    else:
        status = b.settled_status().get(axis)
        if status is None or abs(status.position - 1.0) > POSITION_TOLERANCE_TURNS:
            problems.append(f"position after the move {'none' if status is None else format(status.position, '.3f')}, "
                            "want 1.000")
        # The position is generated, not measured: only the switch shows that the axis left it.
        if status is not None and status.limit != 0:
            problems.append("limit_switch still reads closed after +1 turn off the switch: the axis did not move")
    return problems, f"axis {axis}: on its switch, ESTOP 1 then 0, home ACK 0, +1 turn ACK 0, switch read after it"


def check_fixed_xyz_interrupted(b):
    snapshot = b.settled_status()
    missing = [m for m in AXES if m not in snapshot]
    if missing:
        raise Fail(f"no STEPPER_STATUS from motor(s) {missing}")
    saved = {m: snapshot[m].send_position for m in AXES}
    bad = [m for m, p in saved.items() if not (math.isfinite(p) and 0.0 <= p <= STEPPER_MAX_TURNS)]
    if bad:
        raise Skip(f"send_position {saved} is outside 0 to {STEPPER_MAX_TURNS:g} turns on motor(s) {bad}, "
                   "and this script does not change it")
    if all(abs(p) < SEND_POSITION_EPSILON for p in saved.values()):
        raise Skip("send_position is 0 on all three axes, so FIXED_XYZ would not move anything")
    for axis in AXES:
        errors, _ = b.home(axis)
        if errors != [0]:
            raise Fail(f"axis {axis} home ACK {fmt_errors(errors)}, want [0]; FIXED_XYZ was not sent")
    problems = []
    uuid, sent = b.send_frame(FIXED_XYZ, b"\0")
    b.pump_until(sent + FIXED_XYZ_ESTOP_DELAY_S)
    estop_at = b.estop(True)
    acks = b.wait_acks(uuid, sent, 5.0, settle=FIXED_XYZ_SETTLE_S)
    errors = [x.error for x in acks]
    if any(x.t <= estop_at for x in acks):
        problems.append("FIXED_XYZ was ACKed before the ESTOP, so it was not interrupted (inconclusive)")
    if len(acks) != 1:
        problems.append(f"{len(acks)} ACKs {fmt_errors(errors)} with uuid {uuid}, want exactly one")
    elif errors[0] == 0:
        problems.append(f"the one ACK for uuid {uuid} is 0, want an error")
    b.estop(False)
    b.pump(0.3)
    for axis in AXES:
        errors_home, _ = b.home(axis)
        if errors_home != [0]:
            problems.append(f"re-home of axis {axis} after the ESTOP: ACK {fmt_errors(errors_home)}, want [0]")
    return problems, (f"send_position {', '.join(f'{p:g}' for p in saved.values())}; ESTOP {estop_at - sent:.2f} s into "
                      f"FIXED_XYZ, ACKs {fmt_errors(errors)} (the firmware runs FIXED_XYZ at the configured velocity)")


def check_servo_after_estop(b):
    motor = b.args.servo
    cfg = b.servo_cfg(motor)
    lo, hi = cfg.min_position, cfg.max_position
    v_cap, a = SPEED_FRACTION * cfg.max_velocity, SPEED_FRACTION * cfg.max_acceleration
    if not (math.isfinite(lo) and math.isfinite(hi) and hi - lo >= SERVO_MIN_SPAN):
        raise Skip(f"servo {motor} range {lo} to {hi} is not usable")
    if not (math.isfinite(v_cap) and v_cap > 0.0 and math.isfinite(a) and a > 0.0):
        raise Skip(f"servo {motor} max velocity {cfg.max_velocity} or acceleration {cfg.max_acceleration} is not usable")
    start = min(max(b.servo_position(motor), lo), hi)
    up = hi - start >= start - lo
    delta = min(SERVO_MOVE_FRACTION * (hi - lo), (hi - start) if up else (start - lo))
    if delta < SERVO_MIN_MOVE:
        raise Skip(f"servo {motor} has under {SERVO_MIN_MOVE:g} units of range to move in at {start:g}")
    target = min(max(start + delta if up else start - delta, lo), hi)
    v = min(v_cap, delta / SERVO_MIN_S)
    problems, notes = [], []

    uuid, sent = b.move_servo(motor, target, v, a)
    b.pump_until(sent + min(ESTOP_DELAY_S, 0.3 * delta / v))
    estop_at = b.estop(True)
    acks = b.wait_acks(uuid, sent, 3.0, settle=ESTOP_SETTLE_S)
    errors = [x.error for x in acks]
    if any(x.t <= estop_at for x in acks):
        problems.append("the servo move was ACKed before the ESTOP, so it was not stopped mid-flight (inconclusive)")
    if errors != [ACK_EPERM]:
        problems.append(f"ESTOP mid-flight: ACKs {fmt_errors(errors)} for uuid {uuid}, want exactly [-EPERM]")
    else:
        notes.append(f"ESTOP at {estop_at - sent:.2f} s, ACK -EPERM at {acks[0].t - sent:.2f} s")

    b.estop(False)
    b.pump(0.3)
    # SERVO_STATUS carries the position the profile has generated, not a measurement. After an ESTOP that is where the
    # next move's profile starts.
    profile_start = b.servo_position(motor)
    back = start if abs(profile_start - start) >= SERVO_MIN_MOVE / 2 else target
    planned = abs(back - profile_start) / v  # seconds at cruise, less than the profile takes
    uuid, sent = b.move_servo(motor, back, v, a)
    acks = b.wait_acks(uuid, sent, move_timeout(delta, v, a))
    errors = [x.error for x in acks]
    if errors != [0]:
        problems.append(f"servo move after ESTOP(0): ACK {fmt_errors(errors)}, want [0]")
    else:
        elapsed = acks[0].t - sent
        if elapsed < 0.5 * planned:
            problems.append(f"servo move after ESTOP(0) ACKed after {elapsed:.2f} s, under half of the {planned:.2f} s "
                            "it takes: it did not run")
        b.pump(0.3)
        final = b.servo_position(motor)  # the planned end, so this shows only that the profile finished
        if abs(final - back) > max(1.0, 0.02 * (hi - lo)):
            problems.append(f"servo reports {final:.1f} after the move to {back:.1f}")
        notes.append(f"servo {motor}: {start:.1f} to {target:.1f} (status {profile_start:.1f} after the ESTOP, "
                     f"not measured), then to {back:.1f}, ACK 0 after {elapsed:.2f} s of {planned:.2f} s planned")
    return problems, "; ".join(notes)


def check_diag_fault(b):
    if b.args.axis is None:
        raise Skip("needs --axis N and an operator at the terminal (Task 8 H5)")
    axis = b.args.axis
    cfg, v_cap, a = b.limits(axis)
    # Slow enough that TSTEP stays well above TPWMTHRS, so the driver is in StealthChop, where stall output is on DIAG.
    full_steps_per_s = TMC_CLOCK_HZ / (256.0 * DIAG_TSTEP)
    v = min(v_cap, full_steps_per_s / cfg.steps_per_rev)
    tstep = TMC_CLOCK_HZ / (256.0 * v * cfg.steps_per_rev)
    if not tstep > TPWMTHRS:
        raise Skip(f"cannot keep TSTEP above {TPWMTHRS} (it would be {tstep:.0f})")
    distance = min(1.0, max(0.02, 3.0 * v))
    errors, _ = b.home(axis)
    if errors != [0]:
        raise Fail(f"axis {axis} home ACK {fmt_errors(errors)}, want [0]; nothing else was tried")
    problems, notes = [], []
    forced = False
    try:
        prompt_operator(
            f"Force DIAG on the driver of axis {axis}, on the board's debug console:\n"
            f"        adi_tmc2209 write {axis} 0x40 0xFF       (SGTHRS = 255)\n"
            f"        adi_tmc2209 write {axis} 0x14 0xFFFFF    (TCOOLTHRS = maximum)\n"
            f"    Axis {axis} will then move {distance:.3f} turn at {v:.3f} turn/s (TSTEP about {tstep:.0f}).", Skip)
        forced = True
        b.flush()
        uuid, sent = b.move_stepper(axis, distance, v, a)
        b.needs_home.update(AXES)  # a driver fault stops the steppers and marks every axis unhomed
        acks = b.wait_acks(uuid, sent, move_timeout(distance, v, a), settle=ESTOP_SETTLE_S)
        errors = [x.error for x in acks]
        if errors != [ACK_EIO]:
            problems.append(f"slow move with DIAG forced: ACKs {fmt_errors(errors)} for uuid {uuid}, want exactly "
                            "[-EIO]" + (" (it ran to the end: was DIAG forced?)" if errors == [0] else ""))
        else:
            notes.append(f"ACK -EIO after {acks[0].t - sent:.2f} s")
        snapshot = b.settled_status()
        for motor in AXES:
            want = STATUS_ENABLE_OFF | (STATUS_DRIVER_FAULT if motor == axis else 0)
            if motor not in snapshot:
                problems.append(f"no STEPPER_STATUS from motor {motor}")
            elif snapshot[motor].status != want:
                problems.append(f"motor {motor} status 0x{snapshot[motor].status:02x}, want 0x{want:02x}")
        notes.append("status " + ", ".join(
            f"{m}=0x{snapshot[m].status:02x}" if m in snapshot else f"{m}=none" for m in AXES))
        uuid, sent = b.move_stepper(axis, distance, v, a)
        errors = [x.error for x in b.wait_acks(uuid, sent, 3.0)]
        if errors != [ACK_EIO]:
            problems.append(f"move while faulted: ACK {fmt_errors(errors)}, want [-EIO]")
        prompt_operator(
            f"Clear the forced DIAG on axis {axis}:\n"
            f"        adi_tmc2209 write {axis} 0x40 0x0\n"
            f"        adi_tmc2209 write {axis} 0x14 0x0", Fail)
        forced = False
        b.flush()
        errors, _ = b.home(axis)
        if errors != [0]:
            problems.append(f"home after clearing the fault: ACK {fmt_errors(errors)}, want [0]")
        snapshot = b.settled_status()
        for motor in AXES:
            if motor not in snapshot:
                problems.append(f"no STEPPER_STATUS from motor {motor} after the home")
            elif snapshot[motor].status != 0:
                problems.append(f"motor {motor} status 0x{snapshot[motor].status:02x} after the home, want 0")
    finally:
        if forced:
            if sys.exc_info()[0] is not None:  # Ctrl-C, a signal or a failure on its way out: stop first, then talk
                try:
                    b.stop_motion()
                except Exception as error:
                    say(f"WARNING: could not stop the motors with ESTOP: {error}")
            say(f"\nWARNING: DIAG is still forced on axis {axis}.")
            try:
                prompt_operator(f"Clear it now: adi_tmc2209 write {axis} 0x40 0x0 and adi_tmc2209 write {axis} 0x14 0x0",
                                Fail)
            except Fail:
                say("Clear the forced DIAG before anything else is run.")
    return problems, "; ".join(notes)


def enable_off_problem(b, end):
    """What is wrong with the STEPPER_STATUS frames of the last 0.6 s before `end`, if anything: every motor must
    report ENABLE_OFF and nothing else."""
    frames = b.statuses_between(end - 0.6, end)
    for motor in AXES:
        seen = {st.status for st in frames if st.motor == motor}
        if not seen:
            return f"no STEPPER_STATUS from motor {motor}"
        if seen != {STATUS_ENABLE_OFF}:
            return f"motor {motor} status {', '.join(f'0x{s:02x}' for s in sorted(seen))}, want 0x{STATUS_ENABLE_OFF:02x}"
    return ""


def silence_run_time_s(cycles, quiet_s, limits_by_axis):
    """About how long silence_cycles takes, not counting the homes."""
    total = 0.0
    for cycle in range(cycles):
        _, v, a = limits_by_axis[AXES[cycle % len(AXES)]]
        total += quiet_s + SILENCE_STEP_TURNS / v + 2.0 * v / a + ACK_SETTLE_S
    for axis in AXES:
        _, v, a = limits_by_axis[axis]
        travel = SILENCE_STEP_TURNS * len(range(axis, cycles, len(AXES)))
        total += (travel + SILENCE_LONG_TURNS) / v + 4.0 * v / a + 2 * ACK_SETTLE_S + 2.0
    return total


def check_silence_cycles(b):
    """Does a move sent after a silence lose steps? Every cycle waits quiet_s with no frame, checks that all three
    motors report ENABLE_OFF, then moves one axis +0.25 turn and wants ACK 0. The axes rotate (cycle mod 3), so with 30
    cycles each travels 10 x 0.25 = 2.5 turns away from its switch, and steps lost at the enable edge add up in one
    direction. Then each axis, with no silence, goes back to 0.1 turn short of the switch (it must read open) and 0.2
    turn further (it must hit). Cycles that went +1 and -1 turn could not show this: they net zero for any constant
    loss."""
    quiet_s, cycles = b.args.quiet_s, b.args.cycles
    axis_limits = {axis: b.limits(axis) for axis in AXES}  # all three before anything moves
    steps = {axis: len(range(axis, cycles, len(AXES))) for axis in AXES}
    travel = {axis: SILENCE_STEP_TURNS * steps[axis] for axis in AXES}
    say(f"  silence_cycles: {cycles} cycles of {quiet_s:g} s quiet, moves per axis {[steps[x] for x in AXES]}, "
        f"expected run time about {silence_run_time_s(cycles, quiet_s, axis_limits) / 60.0:.0f} minutes, "
        "plus the homes")
    for axis in AXES:  # a known start: every axis on its switch
        errors, _ = b.home(axis)
        if errors != [0]:
            raise Fail(f"axis {axis} home ACK {fmt_errors(errors)}, want [0]; nothing else was tried")
    problems = []
    done = 0
    for cycle in range(cycles):
        axis = AXES[cycle % len(AXES)]
        cfg, v, a = axis_limits[axis]
        end = b.quiet(quiet_s)
        wrong = enable_off_problem(b, end)
        if wrong:
            problems.append(f"cycle {cycle + 1}, axis {axis}, after {quiet_s:g} s quiet: {wrong}")
            break
        uuid, sent = b.move_stepper(axis, SILENCE_STEP_TURNS, v, a)
        b.needs_home.add(axis)  # it is off its switch from here on
        errors = [x.error for x in b.wait_acks(uuid, sent, move_timeout(SILENCE_STEP_TURNS, v, a))]
        if errors != [0]:
            problems.append(f"cycle {cycle + 1}, axis {axis}, move +{SILENCE_STEP_TURNS:g} turn after quiet: "
                            f"ACK {fmt_errors(errors)}, want [0]")
            break
        done += 1
        say(f"  cycle {cycle + 1}/{cycles} (axis {axis}) ok")
    if problems:
        return problems, f"{done}/{cycles} cycles"

    # No silence from here on. Lost steps have moved each axis toward its switch (or gained ones away from it).
    for axis in AXES:
        cfg, v, a = axis_limits[axis]
        b.needs_home.add(axis)
        uuid, sent = b.move_stepper(axis, SILENCE_SHORT_TURNS, v, a, relative=False)
        errors = [x.error for x in b.wait_acks(uuid, sent, move_timeout(travel[axis], v, a))]
        status = b.settled_status().get(axis)
        if errors != [0]:
            problems.append(f"axis {axis}: move back to {SILENCE_SHORT_TURNS:g} turn: ACK {fmt_errors(errors)}, "
                            "want [0]")
            continue
        if status is None:
            problems.append(f"axis {axis}: no status received after the move back to {SILENCE_SHORT_TURNS:g} turn, so "
                            "the switch was not read")
            continue  # the switch state is not known, so do not drive further toward it
        if status.limit != 0:
            problems.append(f"axis {axis}: switch closed {SILENCE_SHORT_TURNS:g} turn short of it after {steps[axis]} "
                            f"moves of {SILENCE_STEP_TURNS:g} turn after a silence: steps were lost")
            continue  # the position is no longer known, so do not drive further toward the switch
        uuid, sent = b.move_stepper(axis, -SILENCE_LONG_TURNS, v, a)
        errors = [x.error for x in b.wait_acks(uuid, sent, move_timeout(SILENCE_LONG_TURNS, v, a))]
        status = b.settled_status().get(axis)
        if errors != [0]:
            problems.append(f"axis {axis}: move {SILENCE_LONG_TURNS:g} turn further: ACK {fmt_errors(errors)}, "
                            "want [0]")
        elif status is None:
            problems.append(f"axis {axis}: no status received after the move {SILENCE_LONG_TURNS:g} turn further, so "
                            "the switch was not read")
        elif status.limit != 1:
            problems.append(f"axis {axis}: switch still open {SILENCE_LONG_TURNS:g} turn on from "
                            f"{SILENCE_SHORT_TURNS:g} short of it: steps were gained")
    return problems, (f"{cycles} cycles of {quiet_s:g} s quiet over axes 0, 1, 2 ({steps[0]}, {steps[1]}, {steps[2]} "
                      f"moves of {SILENCE_STEP_TURNS:g} turn); each axis then open at {SILENCE_SHORT_TURNS:g} turn "
                      f"short and hit after {SILENCE_LONG_TURNS:g} more")


def check_silence_midmove(b):
    if b.args.silence_timeout_s is None:
        raise Skip("needs the image built with -DCONFIG_LIB_MOTOR_MOTION_HOST_SILENCE_TIMEOUT_MS=5000 and "
                   "--silence-timeout-s 5")
    axis = b.args.axis or 0
    timeout_s = b.args.silence_timeout_s
    cfg, v_cap, a = b.limits(axis)
    distance = 1.0
    v = min(v_cap, distance / (timeout_s + 4.0))
    errors, _ = b.home(axis)
    if errors != [0]:
        raise Fail(f"axis {axis} home ACK {fmt_errors(errors)}, want [0]; nothing else was tried")
    problems, notes = [], []

    uuid, sent = b.move_stepper(axis, distance, v, a)
    b.needs_home.add(axis)
    # No keepalive: the board has to hear nothing for the timeout while the move runs.
    acks = b.wait_acks(uuid, sent, distance / v + 10.0 + timeout_s, keepalive=False)
    errors = [x.error for x in acks]
    if errors != [ACK_ETIMEDOUT]:
        problems.append(f"ACKs {fmt_errors(errors)} for uuid {uuid}, want exactly [-ETIMEDOUT]"
                        + (f" (a move of {distance / v:.0f} s ran to the end: is this the {timeout_s:g} s build?)"
                           if errors == [0] else ""))
    else:
        took = acks[0].t - sent
        if not timeout_s - SILENCE_EARLY_S <= took <= timeout_s + SILENCE_LATE_S:
            problems.append(f"the motion stopped {took:.2f} s after the move frame, want near {timeout_s:g} s")
        notes.append(f"ACK -ETIMEDOUT after {took:.2f} s of a {distance / v:.0f} s move")
    b.pump(0.3)
    wrong = enable_off_problem(b, time.monotonic())
    if wrong:
        problems.append(f"before any new frame: {wrong}")

    # The next frame ends the silence
    b.send_frame(CAPABILITIES_REQUEST, b"\0")
    snapshot = b.settled_status()
    for motor in AXES:
        if motor not in snapshot:
            problems.append(f"no STEPPER_STATUS from motor {motor} after the next frame")
        elif snapshot[motor].status != 0:
            problems.append(f"motor {motor} status 0x{snapshot[motor].status:02x} after the next frame, want 0")
    uuid, sent = b.move_stepper(axis, 0.2, v_cap, a)
    errors = [x.error for x in b.wait_acks(uuid, sent, 3.0)]
    if errors != [ACK_EBUSY]:
        problems.append(f"move of the axis that was stopped: ACK {fmt_errors(errors)}, want [-EBUSY]")
    errors, _ = b.home(axis)
    if errors != [0]:
        problems.append(f"home after the silence: ACK {fmt_errors(errors)}, want [0]")
    return problems, "; ".join(notes)


CHECKS = collections.OrderedDict([
    ("capabilities", check_capabilities),
    ("status_idle", check_status_idle),
    ("home_and_move", check_home_and_move),
    ("estop_multiblock_restart", check_estop_multiblock_restart),
    ("recover_at_limit", check_recover_at_limit),
    ("fixed_xyz_interrupted", check_fixed_xyz_interrupted),
    ("servo_after_estop", check_servo_after_estop),
    ("diag_fault", check_diag_fault),
    ("silence_cycles", check_silence_cycles),
    ("silence_midmove", check_silence_midmove),
])
V13_ONLY = ("diag_fault", "silence_cycles", "silence_midmove")


def run_check(b, name):
    """Run one check, then put the board back. False if the run should stop."""
    if name in V13_ONLY and b.version != "v1.3":
        report(name, None, "applies to v1.3 boards only")
        return True
    unexpected = False
    try:
        try:
            problems, info = CHECKS[name](b)
            report(name, not problems, "; ".join(problems + ([f"[{info}]"] if info else [])) if problems else info)
        except Skip as skip:
            report(name, None, str(skip))
        except Fail as fail:
            report(name, False, str(fail))
        except Abort as abort:
            report(name, False, str(abort))
    except BaseException:
        # Ctrl-C or a bug: the script no longer knows what the motors are doing. Do not send homing moves; main
        # stops the motion with ESTOP.
        unexpected = True
        raise
    finally:
        alive = False
        if not unexpected:
            try:
                alive = b.restore(name)
            except Exception as error:  # restore must not hide what the check raised
                report(f"{name}/restore", False, f"{type(error).__name__}: {error}")
                try:
                    b.stop_motion()
                except Exception:
                    pass
    return alive


def interrupt(signum, frame):
    raise KeyboardInterrupt


def set_signals(handler, names, saved):
    """Point the named signals at `handler`, remembering what they were in `saved` (the first time only)."""
    for name in names:
        sig = getattr(signal, name, None)  # SIGHUP does not exist everywhere
        if sig is None:
            continue
        try:
            old = signal.signal(sig, handler)
        except (ValueError, OSError):
            continue
        saved.setdefault(sig, old)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                     epilog="checks: " + ", ".join(CHECKS) + ". On v1.2 the last three (diag_fault, "
                                     "silence_cycles, silence_midmove) are v1.3 only. Exit status: 0 if every selected "
                                     "check passed, 1 on any FAIL, 2 for bad arguments, 3 if nothing failed but a "
                                     "check was skipped. ESTOP does nothing on firmware older than this branch "
                                     "(v2.3.0 and earlier), and a v1.2 board reports the same capabilities on both, so "
                                     "the script cannot tell them apart: be ready to cut power.")
    parser.add_argument("--board", required=True, choices=("v1.2", "v1.3"), help="which board is on the bus")
    parser.add_argument("--check", metavar="NAME[,NAME...]",
                        help="checks to run, in this order (default: all for the board; diag_fault and "
                             "silence_midmove print SKIP unless --axis or --silence-timeout-s is given)")
    parser.add_argument("--channel", default="can0")
    parser.add_argument("--address", type=int, default=0)
    parser.add_argument("--axis", type=int, choices=AXES,
                        help="stepper axis for diag_fault (required there), recover_at_limit and silence_midmove "
                             "(default 0) and estop_multiblock_restart (default 2: a stop that ACKs an idle axis's "
                             "stale uuid shows only when the mover is not axis 0). silence_cycles uses all three axes")
    parser.add_argument("--servo", type=int, choices=(0, 1, 2), default=0, help="servo for servo_after_estop")
    parser.add_argument("--max-turns", type=float, default=MULTIBLOCK_MAX_TURNS,
                        help="estop_multiblock_restart: skip it if its move (just over 1.25 blocks, 1280 pulses, so "
                             "6.67 turns at 24 steps per revolution and microsteps 8) is further than this many turns "
                             f"from the switch (default {MULTIBLOCK_MAX_TURNS:g}, at most {STEPPER_MAX_TURNS:g}); keep "
                             "it inside the axis's travel")
    parser.add_argument("--quiet-s", type=float, default=32.0,
                        help="silence_cycles: seconds without a frame before each move; set it to the host silence "
                             "timeout T plus 2 (default 32, for the default T of 30)")
    parser.add_argument("--cycles", type=int, default=30,
                        help="silence_cycles: quiet-and-move cycles, one axis each in turn, +0.25 turn per cycle; a "
                             "multiple of 3, at least 3 (default 30: 10 per axis, 2.5 turns of travel; about 17 "
                             "minutes at --quiet-s 32, 16 of them quiet)")
    parser.add_argument("--silence-timeout-s", type=float,
                        help="silence_midmove: the host silence timeout of the image under test (5 for the "
                             "-DCONFIG_LIB_MOTOR_MOTION_HOST_SILENCE_TIMEOUT_MS=5000 build)")
    args = parser.parse_args()

    if not 0 <= args.address < BROADCAST:
        parser.error(f"--address must be 0 to {BROADCAST - 1}")
    if args.cycles < len(AXES) or args.cycles % len(AXES):
        parser.error(f"--cycles must be a multiple of {len(AXES)} and at least {len(AXES)}: an axis that never moves "
                     "sits at 0, and the 0.1 turn move to it is under the firmware's same-position gate "
                     "(1/microsteps turn, 0.125 at microsteps 8), which answers -EAGAIN")
    if args.quiet_s < 3.0:
        parser.error("--quiet-s must be at least 3")
    if not 0.0 < args.max_turns <= STEPPER_MAX_TURNS:
        parser.error(f"--max-turns must be above 0 and at most {STEPPER_MAX_TURNS:g}")
    if args.silence_timeout_s is not None and args.silence_timeout_s < 5.0:
        parser.error("--silence-timeout-s must be at least 5, the lowest the firmware accepts")
    if args.check is None:
        names = list(CHECKS)
    else:
        names = [n.strip() for n in args.check.split(",") if n.strip()]
        unknown = [n for n in names if n not in CHECKS]
        if unknown or not names:
            parser.error(f"unknown check(s) {unknown}; choose from {', '.join(CHECKS)}")
        if "diag_fault" in names and args.axis is None:
            parser.error("diag_fault needs --axis N")
        if "silence_midmove" in names and args.silence_timeout_s is None:
            parser.error("silence_midmove needs --silence-timeout-s (5 for the 5 s test build)")
    if "silence_cycles" in names and -(-args.cycles // len(AXES)) * SILENCE_STEP_TURNS > STEPPER_MAX_TURNS:
        parser.error(f"--cycles {args.cycles} would move an axis more than {STEPPER_MAX_TURNS:g} turns from its switch")

    s = open_socket(args.channel)
    board = Board(s, args)
    saved_signals = {}
    # A dropped ssh session sends SIGHUP and a kill sends SIGTERM: treat both as Ctrl-C, so the board is not left
    # estopped or a motor running.
    set_signals(interrupt, ("SIGTERM", "SIGHUP"), saved_signals)
    try:
        if board.preflight():
            for name in names:
                if not run_check(board, name):
                    break
    except KeyboardInterrupt:
        # A second signal must not cut the cleanup short. Stop first, then talk: the terminal may be gone.
        set_signals(signal.SIG_IGN, ("SIGINT", "SIGTERM", "SIGHUP"), saved_signals)
        try:
            board.stop_motion()
        except Exception as error:
            say(f"WARNING: could not stop the motors with ESTOP: {error}")
        report("interrupted", False, "stopped by the operator or a signal; ESTOP was asserted and released if a "
               "motion command had gone out, so every axis needs homing")
    except Exception as error:  # report what stopped the run, then release the board below
        try:
            board.stop_motion()  # the script no longer knows what the motors are doing
        except Exception as stop_error:
            say(f"WARNING: could not stop the motors with ESTOP: {stop_error}")
        try:
            traceback.print_exc()
        except OSError:
            pass
        report("error", False, f"{type(error).__name__}: {error}")
    finally:
        if board.estop_asserted:
            try:
                board.estop(False)
            except Exception as error:
                say(f"WARNING: could not release ESTOP: {error}. Release it before using the board.")
        s.close()
        for sig, old in saved_signals.items():
            if old is not None:
                try:
                    signal.signal(sig, old)
                except (ValueError, OSError):
                    pass

    outcomes = [ok for _, ok in results]
    say(f"{outcomes.count(True)} PASS, {outcomes.count(False)} FAIL, {outcomes.count(None)} SKIP; "
        f"{board.acks_matched} ACKs matched to the commands sent")
    if False in outcomes:
        return 1
    skipped = [name for name, ok in results if ok is None]
    not_run = [name for name in names if name not in {done for done, _ in results}]
    if skipped or not_run:
        say("NOT A FULL PASS" + (f", skipped: {', '.join(skipped)}" if skipped else "")
            + (f", not run: {', '.join(not_run)}" if not_run else "") + " (exit status 3)")
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())

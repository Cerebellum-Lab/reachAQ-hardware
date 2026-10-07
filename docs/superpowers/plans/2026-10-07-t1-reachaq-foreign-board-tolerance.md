# T1: reachAQ foreign-board tolerance and sync message codec — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let reachAQ share the CAN bus with boards other than the pellet board without losing pellet data, and teach it the new sync/IO message types (0x25-0x29) and capability bits, with no change to pellet handling.

**Architecture:**
- Today every `CanInterface` handler resolves its target through `_addr2tgt`, which raises `RuntimeError` for any non-pellet address. `read()` translates a whole batch with no exception handling, so one frame from another board discards the pellet frames read with it.
- This plan adds a guard at the top of `_translate` that skips non-pellet addresses, logging once per address.
- It then adds the new message types to the python-can codec (`socketcan_jerrycan.py`), which also adds them to the receive filters, plus no-op handlers so they arrive silently until T3 consumes them.

**Tech Stack:** Python 3.10, python-can 4.6.1, pytest. The repo is `C:\Users\reynoben\Documents\reachAQ` (sibling of this repo).

**Spec:** `C:\Users\reynoben\Documents\reachAQ-hardware\docs\superpowers\specs\2026-10-07-io-module-timing-design.md`, sections 4 and 8.

## Global Constraints

- Work in a new reachAQ worktree on branch `feat/io-timing-host-tolerance`, created from `feature-dev`. Do not touch the `reachAQ` or `reachAQ-feature` checkouts.
- Pellet behaviour must not change. Pellet addresses are 0-3: bits 2-3, the board type, are 0. Their frames must translate exactly as before.
- Do not change any existing message ID, payload layout, payload size, or the receive filter mask (`0x7E0`).
- New message IDs and payload sizes, exactly:
  - `SYNC_CLAP = 0x25`, 4 B `<I` (tick)
  - `SYNC_CLAP_OBSERVED = 0x26`, 5 B `<IB` (tick, clapper_node)
  - `SYNC_BARCODE = 0x27`, 4 B `<I` (tick)
  - `ANALOG_BLOCK = 0x28`, 38 B `<BBBBH16H` (instance, channel_count, scan_count, flags, scan_period_us, samples[16], scan-major)
  - `PWM_SET = 0x29`, 7 B `<BHI` (channel, duty_permyriad, period_us)
- New capability bits, exactly: `sync_listener = 1 << 4`, `timekeeper = 1 << 5`.
- **No allowlist change in this plan.** The pellet `2.4.0` entry is added at that firmware's release, per the allowlist's own rule against predicting capabilities.
- Every commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Test command, written as `$PYTEST` below: `conda run -n reachaq python -m pytest -p no:cacheprovider -q`, run from the worktree root. The repo's `conftest.py` puts each package's `src/` on `sys.path`, so the worktree's code is what gets tested.

---

### Task 0: Workspace and baseline

**Files:** none

- [ ] **Step 1: Create the worktree**

```bash
git -C C:/Users/reynoben/Documents/reachAQ fetch origin
git -C C:/Users/reynoben/Documents/reachAQ worktree add C:/Users/reynoben/Documents/reachAQ-io-timing -b feat/io-timing-host-tolerance feature-dev
```

- [ ] **Step 2: Get a test environment.** Use one of these two options.

  - **Option A, local.** Create the `reachaq` env per the reachAQ README:

    ```bash
    conda create -n reachaq python=3.10 -y
    cd C:/Users/reynoben/Documents/reachAQ-io-timing && conda run -n reachaq python -m pip install -e ".[test]"
    ```

  - **Option B, rig.** christielab10 already has `~/anaconda3/envs/reachaq` (Python 3.10.21, python-can 4.6.1), and 20/20 `socketcan_backend_test.py` pass there. Its `~/Documents/reachAQ` is on `devel` at `f7bda1da`, an ancestor of `feature-dev`, so a bundle based there always has its prerequisite. That holds even before the first commit. Move the branch with a bundle so commit hashes match:

    ```bash
    git -C C:/Users/reynoben/Documents/reachAQ-io-timing bundle create C:/Users/reynoben/t1.bundle f7bda1da..feat/io-timing-host-tolerance
    scp -i ~/.ssh/reachaq_debug C:/Users/reynoben/t1.bundle christielab10@christielab10:/tmp/t1.bundle
    ssh -i ~/.ssh/reachaq_debug christielab10@christielab10 'cd ~/Documents/reachAQ && git fetch /tmp/t1.bundle feat/io-timing-host-tolerance:feat/io-timing-host-tolerance && git worktree add ~/reachAQ-io-timing feat/io-timing-host-tolerance'
    ```

    On the rig, `$PYTEST` is `~/anaconda3/bin/conda run -n reachaq python -m pytest -p no:cacheprovider -q`, run from `~/reachAQ-io-timing`. After each commit, re-create and copy the bundle, then run `git -C ~/reachAQ-io-timing pull --ff-only /tmp/t1.bundle feat/io-timing-host-tolerance`.

- [ ] **Step 3: Baseline.** Run:

```bash
$PYTEST auto-trainer-device/tests/socketcan_backend_test.py tests/firmware_compatibility_test.py
```

Expected: all pass. Record the count. If anything fails before any change, stop and report it; do not fix unrelated failures.

---

### Task 1: Skip traffic from non-pellet board addresses

**Files:**
- Modify: `auto-trainer-device/src/autotrainer/device/can_interface.py`. Add a helper after `_addr2tgt` (around line 106-118), an attribute in `__init__` next to `self._pellet_addr` (around line 409), and a guard at the top of `_translate` (around line 1638).
- Test: `auto-trainer-device/tests/foreign_board_traffic_test.py` (new)

**Interfaces:**
- Produces: `_board_type_of_addr(addr: int) -> int` (module level), `CanInterface._foreign_board_addresses: set`, `CanInterface._ignore_foreign_board(address: int) -> None`. `_translate(message)` returns `None` for any message whose `dst_id` is not a pellet address.

- [ ] **Step 1: Write the failing tests**

Create `auto-trainer-device/tests/foreign_board_traffic_test.py`:

```python
"""reachAQ must share the bus with boards it has no driver for.

A board's frames carry its own 5-bit address; bits 2-3 are the board type and
the pellet board is type 0. Before this guard, every handler resolved its
target through _addr2tgt, which raises for any other type, and read()
translated a whole batch at once - so one frame from another board threw away
the pellet frames read alongside it.
"""

import logging

import pytest

from autotrainer.device import CanTransportConfiguration, Heartbeat, Target
from autotrainer.device.can_interface import CanInterface
from autotrainer.device.socketcan_jerrycan import JerryCANCmdType, JerryCANMsg

IO_MODULE_ADDRESS = 0x08  # board type 2, node 0
PELLET_ADDRESS = 0x00


def _interface():
    interface = CanInterface(
        required_targets=(Target.PELLET_DEVICE,),
        can_transport=CanTransportConfiguration(kind="socketcan", fd=True),
    )
    interface._get_index = lambda item: item.index
    interface._get_timestamp_ns = lambda item: item.timestamp_ns
    return interface


def _message(message_type, address):
    message = JerryCANMsg()
    message.type = message_type
    message.dst_id = address
    return message


@pytest.mark.parametrize("message_type", [
    JerryCANCmdType.HEARTBEAT,
    JerryCANCmdType.GPIO_READ,
    JerryCANCmdType.TONE,
    JerryCANCmdType.PRESSURE_READ,
    JerryCANCmdType.RGB_LED,
    JerryCANCmdType.ANALOG_OUT,
    JerryCANCmdType.TIME_SYNC_RESPONSE,
    JerryCANCmdType.CAPABILITIES_RESPONSE,
    JerryCANCmdType.GPIO_PULSE_STATUS,
])
def test_foreign_board_frame_translates_to_nothing(message_type):
    assert _interface()._translate(_message(message_type, IO_MODULE_ADDRESS)) is None


def test_read_keeps_pellet_frames_when_a_foreign_board_shares_the_batch():
    interface = _interface()
    interface._is_open = True
    batch = [
        _message(JerryCANCmdType.HEARTBEAT, IO_MODULE_ADDRESS),
        _message(JerryCANCmdType.HEARTBEAT, PELLET_ADDRESS),
    ]
    interface._read_msgs = lambda max_count, collect_ms: list(batch)

    result = interface.read(max_count=2)

    assert len(result) == 1
    assert isinstance(result[0], Heartbeat)
    assert result[0].target == Target.PELLET_DEVICE


def test_foreign_board_is_logged_once_per_address(caplog):
    interface = _interface()

    with caplog.at_level(logging.WARNING):
        for _ in range(3):
            interface._translate(_message(JerryCANCmdType.HEARTBEAT, IO_MODULE_ADDRESS))
        interface._translate(_message(JerryCANCmdType.HEARTBEAT, IO_MODULE_ADDRESS + 1))

    notices = [r for r in caplog.records if "no driver for that board" in r.getMessage()]
    assert len(notices) == 2


def test_pellet_frames_are_unaffected():
    translated = _interface()._translate(_message(JerryCANCmdType.HEARTBEAT, PELLET_ADDRESS))

    assert isinstance(translated, Heartbeat)
    assert translated.target == Target.PELLET_DEVICE
```

- [ ] **Step 2: Run to verify failure**

Run: `$PYTEST auto-trainer-device/tests/foreign_board_traffic_test.py`

Expected:
- `test_foreign_board_frame_translates_to_nothing[HEARTBEAT]` (and the other types that call `_addr2tgt`) FAIL with `RuntimeError: unknown addr for _addr2tgt: 8`
- `test_read_keeps_pellet_frames...` FAILS with the same `RuntimeError`
- `test_foreign_board_is_logged_once_per_address` FAILS
- `test_pellet_frames_are_unaffected` PASSES

- [ ] **Step 3: Implement**

In `can_interface.py`, directly after `_addr2tgt`:

```python
def _board_type_of_addr(addr: int) -> int:
    """Board type is bits 2 and 3 of the 5-bit CAN address; the pellet is 0."""
    return (addr >> 2) & 0x3
```

In `CanInterface.__init__`, directly after `self._pellet_addr: Optional[int] = None`:

```python
        self._foreign_board_addresses = set()
```

At the top of `CanInterface._translate`, before `handler = self._handlers.get(...)`:

```python
        if not _is_pellet_by_addr(message.dst_id):
            self._ignore_foreign_board(message.dst_id)
            return None
```

And add the method directly after `_translate`:

```python
    def _ignore_foreign_board(self, address: int) -> None:
        """Skip traffic from a board this host has no driver for.

        Every handler resolves its target through _addr2tgt, which raises for
        any non-pellet address, and read() translates a whole batch at once -
        so a single frame from another board used to discard the pellet frames
        read with it. Logged once per address: an IO board streams hundreds of
        frames a second.
        """
        if address in self._foreign_board_addresses:
            return
        self._foreign_board_addresses.add(address)
        logger.warning(
            "Ignoring CAN traffic from board address %d (board type %d): "
            "this reachAQ has no driver for that board",
            address, _board_type_of_addr(address),
        )
```

- [ ] **Step 4: Run to verify pass, plus the existing backend tests**

Run: `$PYTEST auto-trainer-device/tests/foreign_board_traffic_test.py auto-trainer-device/tests/socketcan_backend_test.py`

Expected: all PASS. The backend count must equal the Task 0 baseline.

- [ ] **Step 5: Commit**

```bash
git add auto-trainer-device/src/autotrainer/device/can_interface.py auto-trainer-device/tests/foreign_board_traffic_test.py
git commit -m "fix(can): skip frames from boards reachAQ has no driver for

One frame from a non-pellet address raised out of _addr2tgt and took the
pellet frames read in the same batch with it. Translate them to nothing and
say so once per address.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Codec for the sync and IO message types

**Files:**
- Modify: `auto-trainer-device/src/autotrainer/device/socketcan_jerrycan.py`
  - `JerryCANCmdType` enum (around line 117-155)
  - new dataclasses after `GPIOPulseStatus` (around line 419-421)
  - `JerryCANMsg.__init__` (around line 424-467)
  - `_PAYLOAD_SIZES` (around line 470-509)
  - `_pack_payload` (around line 583-638)
  - `_unpack_payload` (around line 708-761)
- Test: `auto-trainer-device/tests/jerrycan_sync_messages_test.py` (new)

**Interfaces:**
- Produces enum members `JerryCANCmdType.SYNC_CLAP` (0x25), `SYNC_CLAP_OBSERVED` (0x26), `SYNC_BARCODE` (0x27), `ANALOG_BLOCK` (0x28), `PWM_SET` (0x29).
- Produces dataclasses:
  - `SyncClap(tick: int)`
  - `SyncClapObserved(tick: int, clapper_node: int)`
  - `SyncBarcode(tick: int)`
  - `AnalogBlock(instance: int, channel_count: int, scan_count: int, flags: int, scan_period_us: int, samples: Tuple[int, ...])`
  - `PwmSet(channel: int, duty_permyriad: int, period_us: int)`
- Produces `JerryCANMsg` attributes `sync_clap`, `sync_clap_observed`, `sync_barcode`, `analog_block`, `pwm_set`.

- [ ] **Step 1: Write the failing tests**

Create `auto-trainer-device/tests/jerrycan_sync_messages_test.py`:

```python
"""Wire codec for the CAN sync clap, the NI-DAQ barcode report, IO-module
analog blocks and PWM commands (spec section 4.2)."""

import struct

import pytest

from autotrainer.device import socketcan_jerrycan
from autotrainer.device.socketcan_jerrycan import (
    JerryCANCmdType,
    JerryCANMsg,
    JerryCANTimestampKind,
    PwmSet,
)

IO_MODULE_ADDRESS = 0x08


def _trailer(kind, *, boot_id=0xB007, sequence=5, board_time_us=1_234_567):
    return struct.pack("<BBBBIIQ", 0xA7, 1, int(kind), 0, boot_id, sequence, board_time_us)


def _can_id(message_type, address):
    return (int(message_type) << 5) | address


def test_new_types_have_the_specified_ids():
    assert JerryCANCmdType.SYNC_CLAP == 0x25
    assert JerryCANCmdType.SYNC_CLAP_OBSERVED == 0x26
    assert JerryCANCmdType.SYNC_BARCODE == 0x27
    assert JerryCANCmdType.ANALOG_BLOCK == 0x28
    assert JerryCANCmdType.PWM_SET == 0x29


def test_receive_filters_admit_the_new_types():
    admitted = {f["can_id"] >> 5 for f in socketcan_jerrycan._jerrycan_filters()}

    assert {0x25, 0x26, 0x27, 0x28, 0x29} <= admitted


def test_decode_sync_clap():
    data = struct.pack("<I", 41) + b"\x00" + _trailer(JerryCANTimestampKind.TX_QUEUE)

    message = socketcan_jerrycan.decode_frame(
        _can_id(JerryCANCmdType.SYNC_CLAP, IO_MODULE_ADDRESS), data,
    )

    assert message.sync_clap.tick == 41
    assert message.dst_id == IO_MODULE_ADDRESS


def test_decode_sync_clap_observed_with_its_observation_time():
    data = struct.pack("<IB", 41, IO_MODULE_ADDRESS) + b"\x00" + _trailer(
        JerryCANTimestampKind.OBSERVED, board_time_us=9_000_123,
    )

    message = socketcan_jerrycan.decode_frame(_can_id(JerryCANCmdType.SYNC_CLAP_OBSERVED, 0), data)

    assert message.sync_clap_observed.tick == 41
    assert message.sync_clap_observed.clapper_node == IO_MODULE_ADDRESS
    assert message.board_timing_valid
    assert message.timing.kind == JerryCANTimestampKind.OBSERVED
    assert message.timing.board_time_us == 9_000_123


def test_decode_sync_barcode_with_its_start_edge_time():
    data = struct.pack("<I", 41) + b"\x00" + _trailer(JerryCANTimestampKind.PHYSICAL_START)

    message = socketcan_jerrycan.decode_frame(
        _can_id(JerryCANCmdType.SYNC_BARCODE, IO_MODULE_ADDRESS), data,
    )

    assert message.sync_barcode.tick == 41
    assert message.timing.kind == JerryCANTimestampKind.PHYSICAL_START


def test_decode_analog_block_keeps_scan_major_order_through_dlc_padding():
    samples = list(range(100, 116))  # scan 0 ch0..7, then scan 1 ch0..7
    payload = struct.pack("<BBBBH16H", 0, 8, 2, 0, 1000, *samples)
    # 38 B payload + uuid + 20 B trailer = 59 B, padded by the board to DLC 64.
    data = payload + b"\x00" + _trailer(JerryCANTimestampKind.SAMPLED) + bytes(5)

    message = socketcan_jerrycan.decode_frame(
        _can_id(JerryCANCmdType.ANALOG_BLOCK, IO_MODULE_ADDRESS), data,
    )

    block = message.analog_block
    assert (block.instance, block.channel_count, block.scan_count, block.scan_period_us) == (0, 8, 2, 1000)
    assert block.samples == tuple(samples)
    assert message.timing.kind == JerryCANTimestampKind.SAMPLED


def test_decode_analog_block_trims_unused_sample_slots():
    payload = struct.pack("<BBBBH16H", 1, 4, 2, 0, 500, *range(16))

    message = socketcan_jerrycan.decode_frame(
        _can_id(JerryCANCmdType.ANALOG_BLOCK, IO_MODULE_ADDRESS), payload + b"\x00",
    )

    assert message.analog_block.samples == tuple(range(8))


def test_decode_analog_block_rejects_more_samples_than_slots():
    payload = struct.pack("<BBBBH16H", 0, 8, 3, 0, 1000, *range(16))

    with pytest.raises(ValueError):
        socketcan_jerrycan.decode_frame(
            _can_id(JerryCANCmdType.ANALOG_BLOCK, IO_MODULE_ADDRESS), payload + b"\x00",
        )


def test_encode_pwm_set():
    message = JerryCANMsg()
    message.type = JerryCANCmdType.PWM_SET
    message.uuid = 0x5A
    message.pwm_set = PwmSet(channel=2, duty_permyriad=2500, period_us=1000)

    can_id, data = socketcan_jerrycan.encode_frame(message, IO_MODULE_ADDRESS)

    assert can_id == _can_id(JerryCANCmdType.PWM_SET, IO_MODULE_ADDRESS)
    assert data == struct.pack("<BHI", 2, 2500, 1000) + b"\x5a"
```

- [ ] **Step 2: Run to verify failure**

Run: `$PYTEST auto-trainer-device/tests/jerrycan_sync_messages_test.py`

Expected: collection ERROR, `ImportError: cannot import name 'PwmSet'`.

- [ ] **Step 3: Implement**

In `JerryCANCmdType`, directly after `CAPABILITIES_RESPONSE = 0x24`:

```python
    SYNC_CLAP = 0x25
    SYNC_CLAP_OBSERVED = 0x26
    SYNC_BARCODE = 0x27
    ANALOG_BLOCK = 0x28
    PWM_SET = 0x29
```

Directly after the `GPIOPulseStatus` dataclass:

```python
@dataclasses.dataclass
class SyncClap:
    tick: int = 0


@dataclasses.dataclass
class SyncClapObserved:
    tick: int = 0
    clapper_node: int = 0


@dataclasses.dataclass
class SyncBarcode:
    tick: int = 0


#: Sample slots in one ANALOG_BLOCK frame; channel_count * scan_count may not exceed it.
ANALOG_BLOCK_SAMPLE_SLOTS = 16


@dataclasses.dataclass
class AnalogBlock:
    instance: int = 0
    channel_count: int = 0
    scan_count: int = 0
    flags: int = 0
    scan_period_us: int = 0
    #: Raw ADC counts, scan-major: scan 0 channels 0..n-1, then scan 1, ...
    samples: Tuple[int, ...] = ()


@dataclasses.dataclass
class PwmSet:
    channel: int = 0
    duty_permyriad: int = 0
    #: 0 keeps the current period; non-zero applies to every channel on the timer.
    period_us: int = 0
```

At the end of `JerryCANMsg.__init__`, after `self.gpio_pulse_status = GPIOPulseStatus()`:

```python
        self.sync_clap = SyncClap()
        self.sync_clap_observed = SyncClapObserved()
        self.sync_barcode = SyncBarcode()
        self.analog_block = AnalogBlock()
        self.pwm_set = PwmSet()
```

In `_PAYLOAD_SIZES`, directly after `JerryCANCmdType.CAPABILITIES_RESPONSE: 9,`:

```python
    JerryCANCmdType.SYNC_CLAP: 4,
    JerryCANCmdType.SYNC_CLAP_OBSERVED: 5,
    JerryCANCmdType.SYNC_BARCODE: 4,
    JerryCANCmdType.ANALOG_BLOCK: 38,
    JerryCANCmdType.PWM_SET: 7,
```

In `_pack_payload`, directly before the final `raise NotImplementedError(...)`:

```python
    if message_type == JerryCANCmdType.PWM_SET:
        return struct.pack(
            "<BHI", message.pwm_set.channel, message.pwm_set.duty_permyriad,
            message.pwm_set.period_us,
        )
```

In `_unpack_payload`, after the `GPIO_PULSE_STATUS` branch:

```python
    elif message_type == JerryCANCmdType.SYNC_CLAP:
        message.sync_clap = SyncClap(*struct.unpack("<I", payload))
    elif message_type == JerryCANCmdType.SYNC_CLAP_OBSERVED:
        message.sync_clap_observed = SyncClapObserved(*struct.unpack("<IB", payload))
    elif message_type == JerryCANCmdType.SYNC_BARCODE:
        message.sync_barcode = SyncBarcode(*struct.unpack("<I", payload))
    elif message_type == JerryCANCmdType.ANALOG_BLOCK:
        instance, channel_count, scan_count, flags, scan_period_us = struct.unpack_from("<BBBBH", payload)
        used = channel_count * scan_count
        if used > ANALOG_BLOCK_SAMPLE_SLOTS:
            raise ValueError(
                f"ANALOG_BLOCK claims {channel_count}x{scan_count} samples; "
                f"a frame holds {ANALOG_BLOCK_SAMPLE_SLOTS}"
            )
        samples = struct.unpack_from(f"<{ANALOG_BLOCK_SAMPLE_SLOTS}H", payload, 6)
        message.analog_block = AnalogBlock(
            instance, channel_count, scan_count, flags, scan_period_us, tuple(samples[:used]),
        )
```

`ValueError` is already classified by the receive loop as a `malformed_frame` `CanTransportReadError`, matching how every other malformed payload is treated.

- [ ] **Step 4: Run to verify pass**

Run: `$PYTEST auto-trainer-device/tests/jerrycan_sync_messages_test.py auto-trainer-device/tests/socketcan_backend_test.py`

Expected: all PASS. The backend count equals the baseline.

- [ ] **Step 5: Commit**

```bash
git add auto-trainer-device/src/autotrainer/device/socketcan_jerrycan.py auto-trainer-device/tests/jerrycan_sync_messages_test.py
git commit -m "feat(can): decode the sync clap, barcode report and IO-module frames

Adds 0x25-0x29 to the python-can codec, which also admits them through the
receive filters. Only PWM_SET is host-originated, so it is the only one packed.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Receive the new types from the pellet without warnings

From pellet v2.4.0 on, the pellet board sends `SYNC_CLAP_OBSERVED` once a second whenever a timekeeper is clapping. Without a handler, `_translate` logs "Unhandled message type" for each one. T3 replaces these no-ops with the clock chain.

**Files:**
- Modify: `auto-trainer-device/src/autotrainer/device/can_interface.py`, the `optional_handlers` dict in `__init__` (around line 480-491)
- Test: `auto-trainer-device/tests/foreign_board_traffic_test.py` (append)

**Interfaces:**
- Consumes: `JerryCANCmdType.SYNC_*`, `ANALOG_BLOCK` and `PWM_SET` from Task 2. These are registered through the existing `optional_handlers` name lookup, because the pyjerrycan backend's enum does not have them.

- [ ] **Step 1: Write the failing test.** Append to `foreign_board_traffic_test.py`:

```python
@pytest.mark.parametrize("message_type", [
    JerryCANCmdType.SYNC_CLAP,
    JerryCANCmdType.SYNC_CLAP_OBSERVED,
    JerryCANCmdType.SYNC_BARCODE,
    JerryCANCmdType.ANALOG_BLOCK,
    JerryCANCmdType.PWM_SET,
])
def test_sync_and_io_types_from_the_pellet_arrive_silently(message_type, caplog):
    with caplog.at_level(logging.WARNING):
        translated = _interface()._translate(_message(message_type, PELLET_ADDRESS))

    assert translated is None
    assert "Unhandled message type" not in caplog.text
```

- [ ] **Step 2: Run to verify failure**

Run: `$PYTEST auto-trainer-device/tests/foreign_board_traffic_test.py -k arrive_silently`

Expected: 5 FAIL, with `"Unhandled message type" in caplog.text`.

- [ ] **Step 3: Implement.** In `optional_handlers`, after `"GPIO_PULSE": no_op,`:

```python
            # Clock-chain and IO-module traffic. Received and dropped until the
            # clock chain (T3) and the IO-module driver consume them.
            "SYNC_CLAP": no_op,
            "SYNC_CLAP_OBSERVED": no_op,
            "SYNC_BARCODE": no_op,
            "ANALOG_BLOCK": no_op,
            "PWM_SET": no_op,
```

- [ ] **Step 4: Run to verify pass**

Run: `$PYTEST auto-trainer-device/tests/foreign_board_traffic_test.py auto-trainer-device/tests/socketcan_backend_test.py`

Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add auto-trainer-device/src/autotrainer/device/can_interface.py auto-trainer-device/tests/foreign_board_traffic_test.py
git commit -m "feat(can): accept clock-chain frames from the pellet without warning

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Capability names for the sync roles

**Files:**
- Modify: `tools/acquisition/model/firmware_compatibility.py`, the `CAPABILITY_BITS` dict (lines 13-21)
- Test: `tests/firmware_compatibility_test.py` (append)

**Interfaces:**
- Produces: capability names `"sync_listener"` (bit 4) and `"timekeeper"` (bit 5). `FirmwareCompatibilityPolicy.evaluate()` reports them in `CAPABILITY_BITS` order.

- [ ] **Step 1: Write the failing test.** Append to `tests/firmware_compatibility_test.py`:

```python
def test_policy_names_the_sync_role_capabilities():
    result = FirmwareCompatibilityPolicy.load().evaluate(
        "Pellet: 2.1.0", wire_schema_version=1, capabilities=0b110011,
    )

    assert result.reported_capabilities == (
        "timing_trailer", "time_sync", "sync_listener", "timekeeper",
    )
```

- [ ] **Step 2: Run to verify failure**

Run: `$PYTEST tests/firmware_compatibility_test.py -k sync_role`

Expected: FAIL. The reported tuple is `("timing_trailer", "time_sync")`.

- [ ] **Step 3: Implement.** In `CAPABILITY_BITS`, after `"pellet_precheck": 1 << 3,`:

```python
    # Board reports the CAN sync claps it hears (SYNC_CLAP_OBSERVED).
    "sync_listener": 1 << 4,
    # Board sends the CAN sync clap and drives the NI-DAQ barcode line.
    "timekeeper": 1 << 5,
```

- [ ] **Step 4: Run to verify pass**

Run: `$PYTEST tests/firmware_compatibility_test.py`

Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/acquisition/model/firmware_compatibility.py tests/firmware_compatibility_test.py
git commit -m "feat(firmware): name the sync_listener and timekeeper capabilities

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Verification and handoff

**Files:** none

- [ ] **Step 1: Run the affected suites together**

```bash
$PYTEST auto-trainer-device/tests/foreign_board_traffic_test.py auto-trainer-device/tests/jerrycan_sync_messages_test.py auto-trainer-device/tests/socketcan_backend_test.py auto-trainer-device/tests/board_clock_test.py tests/firmware_compatibility_test.py
```

Expected: all PASS. New tests: 17 in `foreign_board_traffic_test.py` (9 + 3 + 5 parametrised), 9 in `jerrycan_sync_messages_test.py`, and 1 in `firmware_compatibility_test.py`. The pre-existing counts must equal the Task 0 baseline.

- [ ] **Step 2: Report.** Do not merge. Report:
  - the branch name and commits
  - the test counts
  - that `ACKNOWLEDGE` frames from non-pellet boards are now dropped too. That is correct until a host driver sends commands to one, and the IO-module driver plan must route ACKs by address.
  - that `CanInterface` still has a single `BoardClockModel` and `BoardSequenceTracker`. That stays correct while only pellet trailers are translated, and T3 must make them per board before IO-module trailers are processed.

# T3: reachAQ board-clock robustness, per-board timing and the clock chain — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make reachAQ safe for pellet firmware v2.4.0, which advertises `time_sync`, and give every board its own clock state. Then record the IO module's clap and barcode reports, and post-session compose pellet board time → IO-module time → NI-DAQ sample time, written to `alignment.json` with a tone cross-check as the accuracy evidence.

**Architecture:**

- **Live path (`auto-trainer-device`)**
  - `BoardClockModel` stops raising on noisy or closely spaced exchanges.
  - `CanInterface` keeps one clock model and one sequence tracker per board address, and observes the trailer sequence on every frame that carries one.
  - Only the two sync message types pass the foreign-board guard. They pass as new `Source` types, so the existing recorder writes them to `device.csv`.
- **Post-session path (`tools/acquisition/model`)**
  - A pure `board_clock_chain` module decodes the NI-DAQ barcode and fits timekeeper→NI from the barcode edges and pellet→timekeeper from paired clap stamps.
  - It composes the two fits and cross-checks the composition against the pellet's tone edges recorded by the NI-DAQ.
  - `_write_session` adds the result to `alignment.json` as `boardClockChain`.

**Tech Stack:** Python 3.10, numpy, h5py, pytest (with pytest-randomly). The repo is `C:\Users\reynoben\Documents\reachAQ`.

**Spec:** `C:\Users\reynoben\Documents\reachAQ-hardware-timing\docs\superpowers\specs\2026-10-07-io-module-timing-design.md`, sections 3, 4, 8, 9 and 11. **Approved design decisions (user, 2026-11-03):**
- The chain result goes in a new `alignment.json` section.
- IO sync frames are recorded as `device.csv` rows.
- The `events.board_time` validator checks sequence in sequence order.

## Global Constraints

- **Branch.** Work on a new branch `feat/io-timing-clock-chain`, made from `feat/io-timing-host-tolerance` (T1, @ 78a280d4) with current `feature-dev` merged in. T1's files are untouched upstream, and `git merge-tree` reported a clean merge on 2026-11-03.
- **Do not change `alignment.json` `schemaVersion`.** Do not edit the `alignment.frames` validator rule or `_match_camera_nidaq_edge_on_fit`. The user's in-flight branch `fix/alignment-measured-cross-check` changes both.
- **No derived value overwrites raw evidence.** `device.csv` `perf_time`, `event_perf_time`, `event_timestamp_method` and `event_timing_confidence` are unchanged by the chain. The chain lives only in `alignment.json`.
- **`alignment.json` must stay JSON-safe.** No NaN or infinity: use `None`. No numpy scalars: cast to `int`, `float` or `bool`.
- **Session finalization must never fail because of the chain.** Any exception becomes `{"status": "unavailable", "reason": ...}`.
- **Barcode wire format, exact (spec §4.5).** Each frame is a 1 ms high start pulse, then the 32-bit tick LSB-first at 1 ms per bit (high = 1), then low; 33 ms in total. The timekeeper's `SYNC_BARCODE` trailer (kind PHYSICAL_START) is the board time of the start edge. A clap's `SYNC_CLAP_OBSERVED` trailer (kind OBSERVED) is when that board heard it. The timekeeper reports its own claps too, with `clapper_node` equal to its own address.
- **Board identity.** The board type is bits 2-3 of the 5-bit address: pellet = 0, IO module = 2 (addresses 8-11). For board→host frames, `message.dst_id` is the sender's address.
- **Testing.**
  - Tests run in the `reachaq` conda env on christielab10 (`~/anaconda3/bin/conda run -n reachaq python -m pytest -p no:cacheprovider -q`), from the rig worktree `~/reachAQ-clock-chain`.
  - Sync with a bundle whose base is `f7bda1da`. It is an ancestor of every branch here and present on the rig.
  - Iterate by scp'ing changed files into the rig worktree. Run the final test of each task on the committed state.
- **Files** in the reachAQ working tree are CRLF. Preserve each file's endings.
- **Commits** end with your harness-provided `Co-Authored-By` line. Do not push or merge.

**Shorthands:**

```bash
RIG="ssh -i ~/.ssh/reachaq_debug christielab10@christielab10"
PYTEST="~/anaconda3/bin/conda run -n reachaq python -m pytest -p no:cacheprovider -q"
SYNC='git -C C:/Users/reynoben/Documents/reachAQ-clock-chain bundle create C:/Users/reynoben/t3.bundle f7bda1da..feat/io-timing-clock-chain && scp -i ~/.ssh/reachaq_debug C:/Users/reynoben/t3.bundle christielab10@christielab10:/tmp/t3.bundle && ssh -i ~/.ssh/reachaq_debug christielab10@christielab10 "cd ~/reachAQ-clock-chain && git checkout -- . && git clean -fdq -- tests auto-trainer-device/tests && git pull -q --ff-only /tmp/t3.bundle feat/io-timing-clock-chain"'
```

---

### Task 0: Workspace and baseline

**Files:** none (one merge commit)

- [ ] **Step 1: Branch from T1 and merge `feature-dev`**

```bash
git -C C:/Users/reynoben/Documents/reachAQ worktree add C:/Users/reynoben/Documents/reachAQ-clock-chain -b feat/io-timing-clock-chain feat/io-timing-host-tolerance
git -C C:/Users/reynoben/Documents/reachAQ-clock-chain merge --no-ff feature-dev -m "Merge feature-dev into the clock-chain branch

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Expected: a clean merge. If it conflicts, STOP and report the conflicting paths.

- [ ] **Step 2: Rig worktree**

```bash
git -C C:/Users/reynoben/Documents/reachAQ-clock-chain bundle create C:/Users/reynoben/t3.bundle f7bda1da..feat/io-timing-clock-chain
scp -i ~/.ssh/reachaq_debug C:/Users/reynoben/t3.bundle christielab10@christielab10:/tmp/t3.bundle
$RIG 'cd ~/Documents/reachAQ && git fetch -q /tmp/t3.bundle feat/io-timing-clock-chain:feat/io-timing-clock-chain && git worktree add -q ~/reachAQ-clock-chain feat/io-timing-clock-chain'
```

- [ ] **Step 3: Baseline**

```bash
$RIG "cd ~/reachAQ-clock-chain && $PYTEST auto-trainer-device/tests/board_clock_test.py auto-trainer-device/tests/foreign_board_traffic_test.py auto-trainer-device/tests/jerrycan_sync_messages_test.py auto-trainer-device/tests/socketcan_backend_test.py tests/session_validation_test.py tests/session_data_recorder_test.py tests/latency_clocks_test.py tests/firmware_compatibility_test.py"
```

Expected: all pass. Record the count. Stop and report any failure before changing code.

---

### Task 1: The board clock never raises from the receive path (ships pellet v2.4.0)

This is the reachAQ fix for the Critical finding of the T2 final review, and pellet firmware v2.4.0 must not be allowlisted until it ships.
- **The failure.** `open()` runs 4 TIME_SYNC exchanges a few ms apart. With two such points, `_fit` computes a slope from noisy host midpoints, raises `ValueError` outside [0.999, 1.001], and the exception escapes `read()` and aborts `open()`.
- **The fix.** Fit only an offset until the observations span `MIN_SLOPE_SPAN_SECONDS` of board time. Fall back to offset-only when a slope is implausible. Never let `observe` failures escape `_translate_time_sync`.

**Files:**
- Modify: `auto-trainer-device/src/autotrainer/device/board_clock.py`
- Modify: `auto-trainer-device/src/autotrainer/device/can_interface.py` (`_translate_time_sync`)
- Test: `auto-trainer-device/tests/board_clock_test.py` (append), `auto-trainer-device/tests/foreign_board_traffic_test.py` (append)

**Interfaces:**
- Produces: `board_clock.MIN_SLOPE_SPAN_SECONDS = 10.0`. `BoardClockEstimate` gains `slope_fitted: bool`.
- `_translate_time_sync` returns a `BoardTimeSync` with `clock_model == {}` when the exchange is rejected.

- [ ] **Step 1: Write the failing tests.** Append to `auto-trainer-device/tests/board_clock_test.py`:

```python
def _exchange(model, request_id, board_us, host_mid, *, rtt=0.0006, boot_id=7):
    return model.observe(
        request_id=request_id,
        host_send_perf_ns=int((host_mid - rtt / 2) * 1e9),
        host_receive_perf_ns=int((host_mid + rtt / 2) * 1e9),
        board_receive_time_us=board_us - 50,
        board_send_time_us=board_us + 50,
        boot_id=boot_id,
    )


def test_closely_spaced_noisy_exchanges_fit_an_offset_and_never_raise():
    # reachAQ's open() sends 4 exchanges a few ms apart; host receive times are
    # quantized by 5 ms read windows, so a slope fitted to them is noise.
    model = BoardClockModel()
    noise = (0.0, 0.0009, -0.0007, 0.0012)
    for request_id, jitter in enumerate(noise):
        board_us = 2_000_000 + request_id * 5_000
        estimate = _exchange(model, request_id, board_us, 50.0 + board_us / 1e6 + jitter)

    assert estimate.slope == 1.0
    assert not estimate.slope_fitted
    assert model.align(2_010_000)["perf_time"] == pytest.approx(52.01, abs=0.002)


def test_slope_is_fitted_once_the_board_span_is_long_enough():
    model = BoardClockModel()
    for request_id in range(4):
        board_us = 1_000_000 + request_id * 4_000_000  # 12 s span
        estimate = _exchange(model, request_id, board_us, 10.0 + (board_us / 1e6) * 1.00002)

    assert estimate.slope_fitted
    assert estimate.slope == pytest.approx(1.00002, abs=2e-6)


def test_implausible_slope_falls_back_to_an_offset_instead_of_raising():
    model = BoardClockModel()
    for request_id in range(4):
        board_us = 1_000_000 + request_id * 5_000_000
        estimate = _exchange(model, request_id, board_us, 10.0 + (board_us / 1e6) * 1.01)

    assert estimate.slope == 1.0
    assert not estimate.slope_fitted
```

Append to `auto-trainer-device/tests/foreign_board_traffic_test.py`:

```python
def test_a_rejected_time_sync_exchange_does_not_escape_translate():
    interface = _interface()
    message = _message(JerryCANCmdType.TIME_SYNC_RESPONSE, PELLET_ADDRESS)
    message.index = 1_000_000_000
    message.board_timing_valid = True
    message.timing = socketcan_jerrycan.JerryCANTiming(
        version=1, kind=socketcan_jerrycan.JerryCANTimestampKind.TX_QUEUE,
        boot_id=9, sequence=1, board_time_us=2_000,
    )
    # Board send before board receive: observe() rejects it as out of order.
    message.time_sync_response = socketcan_jerrycan.TimeSyncResponse(
        request_id=7, request_receive_time_us=2_000, response_queue_time_us=1_000,
    )
    interface._time_sync_requests[7] = 999_000_000

    translated = interface._translate(message)

    assert translated.clock_model == {}
```

Add `from autotrainer.device import socketcan_jerrycan` to that test file's imports if it is not there.

- [ ] **Step 2: Run them to verify they fail.** scp the two edited test files into `~/reachAQ-clock-chain` at the same paths, then run:

```bash
$RIG "cd ~/reachAQ-clock-chain && $PYTEST auto-trainer-device/tests/board_clock_test.py auto-trainer-device/tests/foreign_board_traffic_test.py -k 'closely or span or implausible or rejected'"
```

Expected failures:
- the closely-spaced test raises `ValueError: Board clock drift estimate is implausible`
- the span test fails with `AttributeError ... slope_fitted`
- the rejected-exchange test raises `ValueError: ... out of order`

- [ ] **Step 3: Implement.** In `board_clock.py`:

After the imports:

```python
#: Fit only an offset until the exchanges span this much board time: a few
#: exchanges milliseconds apart (reachAQ's open() sends 4 back to back) carry
#: host-side scheduling noise far larger than any real drift over that span.
MIN_SLOPE_SPAN_SECONDS = 10.0
#: A crystal-driven board cannot drift more than this from the host clock.
MAX_PLAUSIBLE_DRIFT = 0.001
```

In `BoardClockEstimate`, after `uncertainty_seconds: float`:

```python
    slope_fitted: bool = False
```

In `_fit`, replace:

```python
        if len(selected) < 2 or max(x) == min(x):
            slope = 1.0
        else:
            x_mean = sum(x) / len(x)
            y_mean = sum(y) / len(y)
            slope = sum(
                (x_i - x_mean) * (y_i - y_mean)
                for x_i, y_i in zip(x, y)
            ) / sum((x_i - x_mean) ** 2 for x_i in x)
            if not 0.999 <= slope <= 1.001:
                raise ValueError(f"Board clock drift estimate is implausible: {slope}")
```

with:

```python
        slope, slope_fitted = 1.0, False
        if len(selected) >= 2 and max(x) - min(x) >= MIN_SLOPE_SPAN_SECONDS:
            x_mean = sum(x) / len(x)
            y_mean = sum(y) / len(y)
            candidate = sum(
                (x_i - x_mean) * (y_i - y_mean)
                for x_i, y_i in zip(x, y)
            ) / sum((x_i - x_mean) ** 2 for x_i in x)
            # An implausible slope means the exchanges are bad, not the clock:
            # keep the offset-only fit rather than refusing to align at all.
            if abs(candidate - 1.0) <= MAX_PLAUSIBLE_DRIFT:
                slope, slope_fitted = candidate, True
```

and in the returned `BoardClockEstimate(...)`, add `slope_fitted=slope_fitted,` after `uncertainty_seconds=...`.

In `can_interface.py` `_translate_time_sync`, replace:

```python
            estimate = self._board_clock_model.observe(
                request_id=request_id,
                host_send_perf_ns=host_send_ns,
                board_receive_time_us=int(response.request_receive_time_us),
                board_send_time_us=int(response.response_queue_time_us),
                host_receive_perf_ns=self._get_index(message),
                boot_id=int(timing.boot_id),
            )
            model = dataclasses.asdict(estimate)
```

with:

```python
            try:
                estimate = self._board_clock_model.observe(
                    request_id=request_id,
                    host_send_perf_ns=host_send_ns,
                    board_receive_time_us=int(response.request_receive_time_us),
                    board_send_time_us=int(response.response_queue_time_us),
                    host_receive_perf_ns=self._get_index(message),
                    boot_id=int(timing.boot_id),
                )
            except ValueError as error:
                # One bad exchange must not abort read(): open() and the
                # 30 s resync both run through it.
                logger.warning("Rejected board time-sync exchange %d: %s", request_id, error)
            else:
                model = dataclasses.asdict(estimate)
```

- [ ] **Step 4: Run to verify pass**

```bash
$RIG "cd ~/reachAQ-clock-chain && $PYTEST auto-trainer-device/tests/board_clock_test.py auto-trainer-device/tests/foreign_board_traffic_test.py auto-trainer-device/tests/socketcan_backend_test.py"
```

Expected: all pass, including the three pre-existing board-clock tests.

- [ ] **Step 5: Commit, sync, and run the same command on the committed state**

```bash
git add auto-trainer-device/src/autotrainer/device/board_clock.py auto-trainer-device/src/autotrainer/device/can_interface.py auto-trainer-device/tests/board_clock_test.py auto-trainer-device/tests/foreign_board_traffic_test.py
git commit -m "fix(can): never let a board time-sync exchange abort the CAN reader

open() runs 4 exchanges a few ms apart; a slope fitted to their noisy host
midpoints left [0.999, 1.001] and the ValueError escaped read(), so a board
advertising time_sync (pellet v2.4.0) could not be opened. Fit an offset until
the exchanges span 10 s, fall back to it on an implausible slope, and log a
rejected exchange instead of raising."
```

---

### Task 2: Clap and barcode reports become recorded Sources

**Files:**
- Modify: `auto-trainer-device/src/autotrainer/device/device_interface.py` (new dataclasses after `BoardCapabilities`)
- Modify: `auto-trainer-device/src/autotrainer/device/can_interface.py`
  - `optional_handlers`, around line 486
  - `__init__`: the set of types allowed from any board
  - `_translate`'s guard
  - two translators
- Modify: `auto-trainer-device/src/autotrainer/device/__init__.py` (exports)
- Modify: `auto-trainer-device/src/autotrainer/device/can_device.py`
  - the `.device_interface` import list
  - `_data_handlers`, around line 517
- Modify: `auto-trainer-core/src/autotrainer/core/message/system_status_message.py` (two kinds after `PRESSURE_READING = 310`)
- Modify: `auto-trainer-core/src/autotrainer/core/message/system_message_handler.py` (pass-through set, lines 50-56)
- Test: `auto-trainer-device/tests/foreign_board_traffic_test.py` (adjust and append)

**Interfaces:**
- Produces `SyncClapObservation(Source)` with fields `board_address: int`, `tick: int` and `clapper_node: int`.
- Produces `SyncBarcodeReport(Source)` with fields `board_address: int` and `tick: int`.
- Produces `SystemStatusMessageKind.SYNC_CLAP_OBSERVATION = 311` and `SYNC_BARCODE_REPORT = 312`.
- The recorder writes them as `device.csv` rows with `kind` `SYNC_CLAP_OBSERVATION` / `SYNC_BARCODE_REPORT`. Their `payload_json` has `board_address`, `tick` (and `clapper_node`), and the board envelope columns come from the trailer.

- [ ] **Step 1: Write the failing tests.** In `foreign_board_traffic_test.py`:
- Remove `JerryCANCmdType.SYNC_CLAP_OBSERVED` and `JerryCANCmdType.SYNC_BARCODE` from the parametrize of `test_sync_and_io_types_from_the_pellet_arrive_silently`. They now produce Sources.
- Then append:

```python
from autotrainer.device import SyncBarcodeReport, SyncClapObservation


@pytest.mark.parametrize("address", [PELLET_ADDRESS, IO_MODULE_ADDRESS])
def test_clap_observations_are_recorded_from_any_board(address):
    message = _message(JerryCANCmdType.SYNC_CLAP_OBSERVED, address)
    message.sync_clap_observed = socketcan_jerrycan.SyncClapObserved(tick=41, clapper_node=IO_MODULE_ADDRESS)

    translated = _interface()._translate(message)

    assert isinstance(translated, SyncClapObservation)
    assert (translated.board_address, translated.tick, translated.clapper_node) == (address, 41, IO_MODULE_ADDRESS)


def test_barcode_reports_are_recorded_from_the_timekeeper():
    message = _message(JerryCANCmdType.SYNC_BARCODE, IO_MODULE_ADDRESS)
    message.sync_barcode = socketcan_jerrycan.SyncBarcode(tick=41)

    translated = _interface()._translate(message)

    assert isinstance(translated, SyncBarcodeReport)
    assert (translated.board_address, translated.tick) == (IO_MODULE_ADDRESS, 41)


@pytest.mark.parametrize("message_type", [
    JerryCANCmdType.HEARTBEAT, JerryCANCmdType.ANALOG_BLOCK, JerryCANCmdType.GPIO_READ,
])
def test_other_io_module_traffic_is_still_ignored(message_type):
    assert _interface()._translate(_message(message_type, IO_MODULE_ADDRESS)) is None


def test_the_message_handler_passes_sync_reports_through_silently(caplog):
    from queue import Queue
    from autotrainer.core import SystemStatusMessageKind
    from autotrainer.core.message.system_message_handler import SystemMessageHandler

    handler = SystemMessageHandler(Queue())
    with caplog.at_level(logging.WARNING):
        handler.message_received(SystemStatusMessageKind.SYNC_CLAP_OBSERVATION, object())
        handler.message_received(SystemStatusMessageKind.SYNC_BARCODE_REPORT, object())

    assert "unhandled msg" not in caplog.text
```

Confirm the handler class name (`SystemMessageHandler`) and its constructor in `system_message_handler.py`, and adjust the import if it differs. Keep the test's intent unchanged.

- [ ] **Step 2: Run to verify failure**

Expected: `ImportError: cannot import name 'SyncBarcodeReport'`.

- [ ] **Step 3: Implement.** In `device_interface.py`, after `BoardCapabilities`:

```python
@dataclass
class SyncClapObservation(Source):
    """A board heard the timekeeper's CAN clap; its board timing (OBSERVED) says when."""
    board_address: int = 0
    tick: int = 0
    clapper_node: int = 0


@dataclass
class SyncBarcodeReport(Source):
    """The timekeeper started barcode `tick` on the NI-DAQ line; its board timing (PHYSICAL_START) says when."""
    board_address: int = 0
    tick: int = 0
```

In `__init__.py`, add `SyncClapObservation, SyncBarcodeReport` to the `.device_interface` import. In `can_interface.py`, import them alongside `BoardTimeSync` and `BoardCapabilities`.

In `optional_handlers`, replace `"SYNC_CLAP_OBSERVED": no_op,` and `"SYNC_BARCODE": no_op,` with:

```python
            "SYNC_CLAP_OBSERVED": self._translate_sync_clap_observed,
            "SYNC_BARCODE": self._translate_sync_barcode,
```

Directly after the `for name, handler in optional_handlers.items():` loop, add:

```python
        # Sync reports are evidence for the clock chain whichever board sends
        # them; everything else from a board without a driver stays ignored.
        self._any_board_types = frozenset(
            kind for kind in (
                getattr(cmd_type, "SYNC_CLAP_OBSERVED", None),
                getattr(cmd_type, "SYNC_BARCODE", None),
            )
            if kind is not None
        )
```

In `_translate`, change the guard condition to:

```python
        if not _is_pellet_by_addr(message.dst_id) and message.type not in self._any_board_types:
```

Add the translators next to `_translate_capabilities`:

```python
    @staticmethod
    def _translate_sync_clap_observed(message) -> SyncClapObservation:
        observed = message.sync_clap_observed
        return SyncClapObservation(
            board_address=int(message.dst_id),
            tick=int(observed.tick),
            clapper_node=int(observed.clapper_node),
        )

    @staticmethod
    def _translate_sync_barcode(message) -> SyncBarcodeReport:
        return SyncBarcodeReport(
            board_address=int(message.dst_id), tick=int(message.sync_barcode.tick),
        )
```

In `system_status_message.py`, after `PRESSURE_READING = 310` and its docstring:

```python
    SYNC_CLAP_OBSERVATION = 311
    """A board's SYNC_CLAP_OBSERVED report as a SyncClapObservation (clock-chain evidence)."""

    SYNC_BARCODE_REPORT = 312
    """The timekeeper's SYNC_BARCODE report as a SyncBarcodeReport (clock-chain evidence)."""
```

In `system_message_handler.py`, add `SystemStatusMessageKind.SYNC_CLAP_OBSERVATION,` and `SystemStatusMessageKind.SYNC_BARCODE_REPORT,` to the pass-through set.

In `can_device.py`, add `SyncBarcodeReport` and `SyncClapObservation` to the `.device_interface` import list. In `_data_handlers`, after the `PressureReading` entry, add:

```python
            SyncClapObservation: lambda message: self._api.send_message(
                SystemStatusMessageKind.SYNC_CLAP_OBSERVATION, message,
            ),
            SyncBarcodeReport: lambda message: self._api.send_message(
                SystemStatusMessageKind.SYNC_BARCODE_REPORT, message,
            ),
```

- [ ] **Step 4: Run to verify pass**

```bash
$RIG "cd ~/reachAQ-clock-chain && $PYTEST auto-trainer-device/tests/foreign_board_traffic_test.py auto-trainer-device/tests/board_timing_per_board_test.py auto-trainer-device/tests/socketcan_backend_test.py tests/can_interface_call_contract_test.py"
```

Expected: all pass.

- [ ] **Step 5: Commit, sync, and run the same command on the committed state**

```bash
git add -A auto-trainer-device auto-trainer-core
git commit -m "feat(can): record clap and barcode reports from any board

They are the clock chain's evidence: let only these two types past the
foreign-board guard, as SyncClapObservation and SyncBarcodeReport, so the
recorder writes them to device.csv with their board timing."
```

---

### Task 3: One clock model and one sequence tracker per board

**Files:**
- Modify: `auto-trainer-device/src/autotrainer/device/can_interface.py`
  - `__init__`, around line 418
  - `_translate`, around line 1651
  - `_apply_board_timing`, around line 1696
  - `_translate_time_sync`, around line 1725
- Test: `auto-trainer-device/tests/board_timing_per_board_test.py` (new)

**Interfaces:**
- Consumes:
  - Task 1's non-raising `BoardClockModel`.
  - Task 2's guard change, under which `SYNC_CLAP_OBSERVED` from the IO module reaches `_apply_board_timing`. That is exactly when the single shared model gets thrashed.
- Produces:
  - attributes `CanInterface._board_clocks: Dict[int, BoardClockModel]` and `_board_sequences: Dict[int, BoardSequenceTracker]`
  - methods `_board_clock(address) -> BoardClockModel` and `_observe_board_sequence(message) -> dict`
  - `_apply_board_timing(source, message, sequence_status)`, which now takes the status.
- `_board_clock_model` and `_board_sequence_tracker` are removed. Nothing else references them; this was checked on 2026-11-03.

- [ ] **Step 1: Write the failing tests.** Create `auto-trainer-device/tests/board_timing_per_board_test.py`:

```python
"""Each board keeps its own clock model and sequence tracker.

Before this, one model and one tracker served the whole bus: a second board's
trailer looked like a reboot of the first and reset its clock, and a trailer on
a frame the host drops was never counted, so the next one looked like a gap.
"""

from autotrainer.device import CanTransportConfiguration, Target
from autotrainer.device.can_interface import CanInterface
from autotrainer.device.socketcan_jerrycan import (
    JerryCANCmdType,
    JerryCANMsg,
    JerryCANTimestampKind,
    JerryCANTiming,
    SyncClapObserved,
    TimeSyncResponse,
)

PELLET = 0x00
IO_MODULE = 0x08


def _interface():
    interface = CanInterface(
        required_targets=(Target.PELLET_DEVICE,),
        can_transport=CanTransportConfiguration(kind="socketcan", fd=True),
    )
    interface._get_index = lambda item: item.index
    interface._get_timestamp_ns = lambda item: item.timestamp_ns
    return interface


def _timed(message_type, address, *, boot_id, sequence, board_time_us, index=1, kind=JerryCANTimestampKind.TX_QUEUE):
    message = JerryCANMsg()
    message.type = message_type
    message.dst_id = address
    message.index = index
    message.board_timing_valid = True
    message.timing = JerryCANTiming(
        version=1, kind=kind, boot_id=boot_id, sequence=sequence, board_time_us=board_time_us,
    )
    return message


def _sync_pellet(interface, *, boot_id=7):
    response = _timed(
        JerryCANCmdType.TIME_SYNC_RESPONSE, PELLET,
        boot_id=boot_id, sequence=1, board_time_us=1_000_100, index=101_000_300_000,
    )
    response.time_sync_response = TimeSyncResponse(
        request_id=5, request_receive_time_us=1_000_000, response_queue_time_us=1_000_200,
    )
    interface._time_sync_requests[5] = 101_000_000_000
    assert interface._translate(response).clock_model


def test_another_boards_trailer_does_not_reset_the_pellet_clock():
    interface = _interface()
    _sync_pellet(interface)
    clap = _timed(
        JerryCANCmdType.SYNC_CLAP_OBSERVED, IO_MODULE, boot_id=99, sequence=40, board_time_us=5_000,
        kind=JerryCANTimestampKind.OBSERVED,
    )
    clap.sync_clap_observed = SyncClapObserved(tick=3, clapper_node=IO_MODULE)
    interface._translate(clap)

    tone = interface._translate(_timed(
        JerryCANCmdType.TONE, PELLET, boot_id=7, sequence=2, board_time_us=1_500_000,
        kind=JerryCANTimestampKind.PHYSICAL_START,
    ))

    assert tone.timestamp_method == "board_clock_affine"
    assert tone.board_aligned_perf_time is not None


def test_a_trailer_on_a_dropped_frame_is_not_counted_as_lost():
    interface = _interface()
    interface._translate(_timed(JerryCANCmdType.TONE, PELLET, boot_id=7, sequence=1, board_time_us=10))
    # A trailer-bearing frame the host does not turn into a Source.
    interface._translate(_timed(JerryCANCmdType.DOOR_SENSOR, PELLET, boot_id=7, sequence=2, board_time_us=20))

    tone = interface._translate(_timed(JerryCANCmdType.TONE, PELLET, boot_id=7, sequence=3, board_time_us=30))

    assert tone.board_sequence_status["gap"] == 0


def test_a_reboot_invalidates_only_that_boards_clock():
    interface = _interface()
    _sync_pellet(interface, boot_id=7)

    tone = interface._translate(_timed(
        JerryCANCmdType.TONE, PELLET, boot_id=8, sequence=1, board_time_us=1_500_000,
        kind=JerryCANTimestampKind.PHYSICAL_START,
    ))

    assert tone.board_aligned_perf_time is None
    assert tone.timestamp_method == "host_receive_perf_time"
```

- [ ] **Step 2: Run them to verify they fail**

Run: scp the new file to the rig worktree, then `$RIG "cd ~/reachAQ-clock-chain && $PYTEST auto-trainer-device/tests/board_timing_per_board_test.py"`.

Expected:
- `test_another_boards_trailer...` fails. The IO clap now reaches `_apply_board_timing` (Task 2). Its boot id reads as a reboot to the single tracker, which resets the single model, so the pellet tone is no longer aligned.
- `test_a_trailer_on_a_dropped_frame...` fails with `gap == 1`.
- `test_a_reboot...` may already pass; it pins behaviour that must survive the change.

- [ ] **Step 3: Implement.** In `__init__`, replace:

```python
        self._board_clock_model = BoardClockModel()
        self._board_sequence_tracker = BoardSequenceTracker()
```

with:

```python
        # One clock and one sequence per board address: a second board's
        # trailer is not a reboot of the first.
        self._board_clocks: Dict[int, BoardClockModel] = {}
        self._board_sequences: Dict[int, BoardSequenceTracker] = {}
```

Make sure `Dict` is imported from `typing` in that module; add it if missing.

Add these methods directly before `_apply_board_timing`:

```python
    def _board_clock(self, address: int) -> BoardClockModel:
        model = self._board_clocks.get(address)
        if model is None:
            model = self._board_clocks[address] = BoardClockModel()
        return model

    def _observe_board_sequence(self, message) -> dict:
        """Track every trailer, whatever becomes of its frame.

        A trailer-bearing frame the host does not turn into a Source - a no-op
        type, a foreign board's frame - still used up a sequence number on the
        board; skipping it made the next tracked frame report a lost message.
        """
        if not bool(getattr(message, "board_timing_valid", False)):
            return {}
        tracker = self._board_sequences.get(message.dst_id)
        if tracker is None:
            tracker = self._board_sequences[message.dst_id] = BoardSequenceTracker()
        timing = message.timing
        return tracker.observe(int(timing.boot_id), int(timing.sequence))
```

In `_translate`, make the first statement:

```python
        sequence_status = self._observe_board_sequence(message)
```

and change the call `self._apply_board_timing(res, message)` to `self._apply_board_timing(res, message, sequence_status)`.

In `_apply_board_timing`:
- change the signature to `def _apply_board_timing(self, source: Source, message, sequence_status: dict) -> None:`
- replace from `source.board_sequence_status = self._board_sequence_tracker.observe(` through `aligned = self._board_clock_model.align(source.board_time_us)` with:

```python
        source.board_sequence_status = sequence_status
        model = self._board_clock(message.dst_id)
        estimate = model.estimate
        # Compare boot ids directly: the tracker's reboot flag is spent on
        # whichever frame of the new boot arrived first, which may not be this one.
        if estimate is not None and estimate.boot_id != source.board_boot_id:
            model.reset(source.board_boot_id)
        aligned = model.align(source.board_time_us)
```

In `_translate_time_sync`, change `self._board_clock_model.observe(` to `self._board_clock(message.dst_id).observe(`.

- [ ] **Step 4: Run to verify pass**

```bash
$RIG "cd ~/reachAQ-clock-chain && $PYTEST auto-trainer-device/tests/board_timing_per_board_test.py auto-trainer-device/tests/foreign_board_traffic_test.py auto-trainer-device/tests/socketcan_backend_test.py auto-trainer-device/tests/board_clock_test.py"
```

Expected: all pass.

- [ ] **Step 5: Commit, sync, and run the same command on the committed state**

```bash
git add auto-trainer-device/src/autotrainer/device/can_interface.py auto-trainer-device/tests/board_timing_per_board_test.py
git commit -m "feat(can): keep a clock model and sequence tracker per board

Observe every trailer's sequence before deciding what to do with its frame, and
reset a board's clock on a direct boot-id change, so a second board on the bus
neither resets the pellet's clock nor manufactures lost-message gaps."
```

---

### Task 4: `events.board_time` checks each board in sequence order

`device.csv` rows are sorted by event time. For OBSERVED and PHYSICAL_START stamps, the event time precedes the frame's transmission, so a board's rows are not in sequence order. Checking them in row order reports false "moved backward" errors. Pellet tone stamps already risk this.

**Files:**
- Modify: `tools/session_validation/engine.py` (`_board_time_rule`, lines 1112-1211)
- Test: `tests/session_validation_test.py` (append)

**Interfaces:**
- Produces: the same rule id and observed keys. Duplicates are still errors and gaps are still warnings, both judged in sequence order.
- "Board clock moved backward" is now judged only among `tx_queue` rows, in sequence order, with a 1 ms tolerance for a stamp-then-queue preemption.

- [ ] **Step 1: Write the failing tests.** Append to `tests/session_validation_test.py`:

```python
_BOARD_COLUMNS = (
    "perf_time", "host_receive_perf_time", "board_boot_id",
    "board_sequence", "board_time_us", "board_timestamp_kind",
    "board_aligned_perf_time", "board_clock_model_id",
    "board_clock_uncertainty_seconds", "estimated_transport_delay_seconds",
    "event_perf_time", "event_timestamp_method", "event_timing_confidence",
)


def _board_time_result(tmp_path, rows):
    import csv
    root = _session(tmp_path)
    path = root / "streams/device.csv"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(_BOARD_COLUMNS)
        writer.writerows(rows)
    report = validate_session(root, profile=ValidationProfile.FAST)
    return next(item for item in report.results if item.rule_id == "events.board_time")


def _host_row(perf, boot, sequence, board_us, kind):
    return (perf, perf + 0.01, boot, sequence, board_us, kind, "", "", "", "", perf,
            "host_receive_perf_time", "host_timestamp")


def test_board_rows_out_of_sequence_order_by_event_time_are_valid(tmp_path):
    # A clap heard (sequence 2) is stamped before the queued frame (sequence 1) it follows.
    rows = (
        _host_row(10.000, 7, 2, 900, "observed"),
        _host_row(10.001, 7, 1, 1000, "tx_queue"),
        _host_row(10.002, 7, 3, 2000, "tx_queue"),
    )
    assert _board_time_result(tmp_path, rows).status.value == "pass"


def test_duplicate_board_sequence_still_fails(tmp_path):
    rows = (_host_row(10.0, 7, 1, 1000, "tx_queue"), _host_row(10.1, 7, 1, 2000, "tx_queue"))
    assert _board_time_result(tmp_path, rows).status.value == "fail"


def test_queue_clock_moving_backward_in_sequence_order_fails(tmp_path):
    rows = (_host_row(10.0, 7, 1, 50_000, "tx_queue"), _host_row(10.1, 7, 2, 10_000, "tx_queue"))
    assert _board_time_result(tmp_path, rows).status.value == "fail"


def test_board_sequence_gap_is_a_warning(tmp_path):
    rows = (_host_row(10.0, 7, 1, 1000, "tx_queue"), _host_row(10.1, 7, 4, 2000, "tx_queue"))
    result = _board_time_result(tmp_path, rows)
    assert result.status.value == "warning"
    assert "2 board message(s) missing" in result.message
```

If `_result` objects expose the text under a name other than `message`, use the name the existing tests use.

- [ ] **Step 2: Run to verify failure**

Run: `$RIG "cd ~/reachAQ-clock-chain && $PYTEST tests/session_validation_test.py -k 'board'"`.

Expected: `test_board_rows_out_of_sequence_order...` FAILS with "board sequence moved backward" or "board clock moved backward".

- [ ] **Step 3: Implement.** In `engine.py`:

Above `_board_time_rule`, add:

```python
# Queue stamps are taken before the queue insert, so an interrupt that queues
# between them can put a later stamp first; anything beyond this is a real reset.
_QUEUE_CLOCK_TOLERANCE_US = 1000
```

Inside `_board_time_rule`:
- Replace `previous = {}` with `envelopes = {}`.
- Replace the block from `prior = previous.get(boot)` through `previous[boot] = (sequence, board_us)` with:

```python
                envelopes.setdefault(boot, []).append(
                    (line_number, sequence, board_us, row.get("board_timestamp_kind") or "")
                )
```

- After the `with path.open(...)` block, and before `if not timestamped:`, add:

```python
    # device.csv is ordered by event time, and a board stamps observed and
    # physical events before the frame that reports them is queued, so a
    # board's rows are judged in the order the board numbered them.
    for entries in envelopes.values():
        entries.sort(key=lambda entry: entry[1])
        previous_sequence = None
        latest_queue_us = None
        for line_number, sequence, board_us, kind in entries:
            if previous_sequence is not None:
                if sequence == previous_sequence:
                    errors.append(f"row {line_number}: duplicate board sequence {sequence}")
                elif sequence - previous_sequence > 1:
                    warnings.append(
                        f"row {line_number}: {sequence - previous_sequence - 1} board message(s) missing"
                    )
            previous_sequence = sequence
            if kind == "tx_queue":
                if latest_queue_us is not None and board_us < latest_queue_us - _QUEUE_CLOCK_TOLERANCE_US:
                    errors.append(f"row {line_number}: board clock moved backward")
                latest_queue_us = board_us if latest_queue_us is None else max(latest_queue_us, board_us)
```

- In the observed dict, change `"boot_epochs": len(previous),` to `"boot_epochs": len(envelopes),`.

- [ ] **Step 4: Run to verify pass**

Run: `$RIG "cd ~/reachAQ-clock-chain && $PYTEST tests/session_validation_test.py"`.

Expected: all pass, including the pre-existing `test_fast_validator_checks_board_clock_and_sequence_evidence`.

- [ ] **Step 5: Commit, sync, and run the same command on the committed state**

```bash
git add tools/session_validation/engine.py tests/session_validation_test.py
git commit -m "fix(validate): judge a board's sequence in its own order, not event order

Observed and physical stamps precede the frame that reports them, so rows
sorted by event time are not in sequence order; checking them in row order
reported false backward moves. Clock monotonicity is checked among queue stamps
only, with 1 ms for a stamp-then-queue preemption."
```

---

### Task 5: Barcode decoder

**Files:**
- Create: `tools/acquisition/model/board_clock_chain.py` (decoder part)
- Create: `tests/board_clock_chain_synthetic.py`. These are shared synthetic-rig helpers. Test helpers in this repo are imported as top-level modules, like `from frame_alignment_synthetic import ...`.
- Test: `tests/board_clock_chain_test.py` (new)

**Interfaces:**
- Produces:
  - constants `BARCODE_CHANNEL`, `BARCODE_BIT_SECONDS`, `BARCODE_BITS`, `BARCODE_FRAME_SECONDS`, `MIN_BARCODE_RATE_HZ`
  - `BarcodeFrame(tick: int, start_position: int, edges: Tuple[Tuple[int, float], ...])`
  - `barcode_edges(tick) -> Tuple[Tuple[float, bool], ...]`
  - `decode_barcode_frames(values, sample_rate_hz) -> Tuple[Tuple[BarcodeFrame, ...], int]`
- Test helpers in `board_clock_chain_synthetic`: `RATE`, `barcode_line(rate, total_seconds, frames)`.

- [ ] **Step 1: Write the failing tests.** Create `tests/board_clock_chain_synthetic.py`:

```python
"""Synthetic NI-DAQ lines and device rows for the board clock chain tests."""

import math

import numpy as np

RATE = 10_000.0


def barcode_line(rate, total_seconds, frames):
    """frames: (start_seconds, tick). Each edge at the first sample at or after its time."""
    line = np.zeros(int(math.ceil(total_seconds * rate)), dtype=np.float32)
    for start, tick in frames:
        levels = [1] + [(tick >> bit) & 1 for bit in range(32)]
        for segment, level in enumerate(levels):
            if level:
                first = int(math.ceil((start + segment * 0.001) * rate))
                last = int(math.ceil((start + (segment + 1) * 0.001) * rate))
                line[first:last] = 1.0
    return line
```

Then create `tests/board_clock_chain_test.py`:

```python
import json

import numpy as np
import pytest

from board_clock_chain_synthetic import RATE, barcode_line
from tools.acquisition.model import board_clock_chain as chain


def test_barcode_edges_follow_the_start_pulse_and_bits():
    assert chain.barcode_edges(0) == ((0.0, True), (0.001, False))
    # Bit 0 set merges with the start pulse; bit 31 set ends at 33 ms.
    edges = chain.barcode_edges(0x80000001)
    assert edges[0] == (0.0, True)
    assert edges[1] == pytest.approx((0.002, False))
    assert edges[-2] == pytest.approx((0.032, True))
    assert edges[-1] == pytest.approx((0.033, False))


@pytest.mark.parametrize("tick", [0, 1, 0x80000001, 0xFFFFFFFF, 0x12345678])
def test_frames_decode_by_position(tick):
    line = barcode_line(RATE, 2.0, [(0.5, tick), (1.5, tick + 1 & 0xFFFFFFFF)])

    frames, rejected = chain.decode_barcode_frames(line, RATE)

    assert [frame.tick for frame in frames] == [tick, (tick + 1) & 0xFFFFFFFF]
    assert rejected == 0
    assert frames[0].start_position == 5000
    assert frames[0].edges[0] == (5000, 0.0)
    assert len(frames[0].edges) == len(chain.barcode_edges(tick))


def test_a_frame_cut_by_the_recording_start_is_not_decoded():
    full = barcode_line(RATE, 2.0, [(0.010, 0x00F0F0F0), (1.0, 7)])
    line = full[int(0.025 * RATE):]  # starts 15 ms into the first frame

    frames, _ = chain.decode_barcode_frames(line, RATE)

    assert [frame.tick for frame in frames] == [7]


def test_a_glitch_inside_a_frame_rejects_it():
    line = barcode_line(RATE, 2.0, [(0.5, 0), (1.5, 9)])
    line[int(0.512 * RATE)] = 1.0  # a one-sample spike in a low bit of the first frame

    frames, rejected = chain.decode_barcode_frames(line, RATE)

    assert [frame.tick for frame in frames] == [9]
    assert rejected == 1


def test_a_truncated_last_frame_is_ignored():
    line = barcode_line(RATE, 1.52, [(0.5, 3), (1.5, 4)])  # second frame cut at 20 ms

    frames, _ = chain.decode_barcode_frames(line, RATE)

    assert [frame.tick for frame in frames] == [3]


def test_rates_below_the_minimum_decode_nothing():
    line = barcode_line(1000.0, 2.0, [(0.5, 3)])
    assert chain.decode_barcode_frames(line, 1000.0) == ((), 0)


def test_five_kilohertz_still_decodes():
    line = barcode_line(5000.0, 2.0, [(0.5, 0xA5A5A5A5)])
    frames, _ = chain.decode_barcode_frames(line, 5000.0)
    assert [frame.tick for frame in frames] == [0xA5A5A5A5]
```

- [ ] **Step 2: Run to verify failure**

Run: `$RIG "cd ~/reachAQ-clock-chain && $PYTEST tests/board_clock_chain_test.py"`.

Expected: `ImportError` (no module `board_clock_chain`).

- [ ] **Step 3: Implement.** Create `tools/acquisition/model/board_clock_chain.py`. Match `session_data_recorder.py`'s import of `tools.latency.clocks`; it is used in Task 6.

```python
"""Pellet-board time to NI-DAQ time, through the IO module's clap and barcode.

The IO module (the timekeeper) claps on CAN once a second and plays the same
tick as a barcode on the NI-DAQ `barcode` line. Every board stamps the end of
the clap frame on its own clock and reports SYNC_CLAP_OBSERVED; the timekeeper
reports SYNC_BARCODE with the board time of the barcode's start edge. Pairing a
board's clap stamps with the timekeeper's gives board -> timekeeper time;
pairing the timekeeper's barcode times with the edges the NI-DAQ sampled gives
timekeeper -> NI time. Composing them puts pellet events on the NI sample
timeline the cameras and tones are already on.

Post-session and read-only over the recorded evidence: it never rewrites a
device row's event time.
"""

from __future__ import annotations

import dataclasses
from typing import Tuple

import numpy

BARCODE_CHANNEL = "barcode"
BARCODE_BIT_SECONDS = 0.001
BARCODE_BITS = 32
BARCODE_FRAME_SECONDS = (1 + BARCODE_BITS) * BARCODE_BIT_SECONDS
#: A start edge follows at least this much low line. A frame holds at most 32 ms
#: of low (tick 0), so no edge inside a frame qualifies.
BARCODE_QUIET_SECONDS = 0.040
#: How far a sampled edge may sit from where the decoded tick puts it.
BARCODE_EDGE_TOLERANCE_SECONDS = 0.0003
#: Five samples per bit: below this a bit's middle is not reliably inside it.
MIN_BARCODE_RATE_HZ = 5000.0


@dataclasses.dataclass(frozen=True)
class BarcodeFrame:
    tick: int
    start_position: int
    #: (sample position, seconds after the start edge) of every edge in the frame.
    edges: Tuple[Tuple[int, float], ...]


def barcode_edges(tick: int) -> Tuple[Tuple[float, bool], ...]:
    """Seconds after the start edge, and whether it rises, of every edge of barcode `tick`."""
    levels = [1] + [(int(tick) >> bit) & 1 for bit in range(BARCODE_BITS)] + [0]
    edges = [(0.0, True)]
    for segment in range(1, len(levels)):
        if levels[segment] != levels[segment - 1]:
            edges.append((segment * BARCODE_BIT_SECONDS, levels[segment] == 1))
    return tuple(edges)


def decode_barcode_frames(values, sample_rate_hz) -> Tuple[Tuple[BarcodeFrame, ...], int]:
    """Barcode frames on one sampled line, and how many candidate starts were rejected.

    Bits are read at the middle of their millisecond, by position from the start
    edge: bit 0 set merges with the start pulse, so edges alone cannot delimit
    bits. A frame is kept only if every edge its tick implies is where it should
    be, which rejects glitches and frames the recording cut.
    """
    if not sample_rate_hz or float(sample_rate_hz) < MIN_BARCODE_RATE_HZ:
        return (), 0
    rate = float(sample_rate_hz)
    samples = numpy.asarray(values, dtype=numpy.float64)
    high = numpy.isfinite(samples) & (samples > 0.5)
    if high.size < 2:
        return (), 0
    changes = numpy.flatnonzero(high[1:] != high[:-1]) + 1
    bit = BARCODE_BIT_SECONDS * rate
    quiet = BARCODE_QUIET_SECONDS * rate
    tolerance = max(1.0, BARCODE_EDGE_TOLERANCE_SECONDS * rate)
    frames, rejected = [], 0
    next_start = 0
    for index, position in enumerate(changes.tolist()):
        if not high[position] or position < next_start:
            continue
        low_since = int(changes[index - 1]) if index else 0
        if position - low_since < quiet:
            continue
        trailing = position + int(round((1 + BARCODE_BITS + 0.5) * bit))
        if trailing >= high.size:
            break
        next_start = position + int(round(BARCODE_FRAME_SECONDS * rate))
        if not high[position + int(round(0.5 * bit))] or high[trailing]:
            rejected += 1
            continue
        tick = 0
        for bit_index in range(BARCODE_BITS):
            if high[position + int(round((1.5 + bit_index) * bit))]:
                tick |= 1 << bit_index
        expected_edges = barcode_edges(tick)
        # Bit-centre sampling misses a glitch on a bit boundary; the frame must
        # also hold exactly the edges its tick implies, no more.
        frame_end = position + BARCODE_FRAME_SECONDS * rate + tolerance
        within = int(numpy.searchsorted(changes, frame_end, side="right")) - index
        if within != len(expected_edges):
            rejected += 1
            continue
        edges = []
        for offset, _rises in expected_edges:
            expected = position + offset * rate
            nearest = int(numpy.searchsorted(changes, expected - tolerance, side="left"))
            if nearest >= changes.size or changes[nearest] > expected + tolerance:
                break
            edges.append((int(changes[nearest]), offset))
        else:
            frames.append(BarcodeFrame(tick=tick, start_position=int(position), edges=tuple(edges)))
            continue
        rejected += 1
    return tuple(frames), rejected
```

- [ ] **Step 4: Run to verify pass.** Run the same command. Expected: all pass.

- [ ] **Step 5: Commit, sync, and run the same command on the committed state**

```bash
git add tools/acquisition/model/board_clock_chain.py tests/board_clock_chain_synthetic.py tests/board_clock_chain_test.py
git commit -m "feat(sync): decode the timekeeper's NI-DAQ barcode by position

A 1 ms start pulse then 32 LSB-first 1 ms bits; read at bit centres from the
start edge, and keep a frame only when it holds exactly the edges its tick
implies."
```

---

### Task 6: Chain fits and the tone cross-check

**Files:**
- Modify: `tools/acquisition/model/board_clock_chain.py` (append)
- Modify: `tests/board_clock_chain_synthetic.py` (append)
- Test: `tests/board_clock_chain_test.py` (append)

**Interfaces:**
- Consumes: Task 5's decoder. Device-row tuples in the recorder's normalized 23-field layout; this task uses indices 3 (kind name), 8 (`payload_json`), 12 (`board_boot_id`), 14 (`board_time_us`) and 15 (`board_timestamp_kind`). `clocks.fit_line(x, y, *, max_residual, min_points)` returns a `ClockFit` with `offset`, `drift`, `residual_rms`, `points`, `valid`, `reason`, `x_min` and `x_max`.
- Produces:
  - `NIDAQ_CHANNELS = ("barcode", "tone1", "tone2")`
  - `board_clock_chain(device_rows, *, sample_indices, sample_rate_hz, channels) -> dict`, a JSON-safe record with keys `status`, `reason`, `method`, `niTime`, `timekeeper` and `boards`
  - each board entry has `address`, `bootId`, `clapsMatched`, `toTimekeeper`, `toNi` and `toneCrossCheck`

- [ ] **Step 1: Write the failing tests.** Add `import json` to the imports of `tests/board_clock_chain_synthetic.py`, then append to it:

```python
def device_row(perf, kind, payload, boot, sequence, board_us, stamp_kind):
    return (perf, perf, "inbound", kind, "", None, None, None, json.dumps(payload),
            perf, perf, 0, boot, sequence, board_us, stamp_kind, None, None, None, "{}", perf,
            "host_receive_perf_time", "host_timestamp")


IO_ADDRESS, PELLET_ADDRESS, IO_BOOT, PELLET_BOOT = 8, 0, 0xB007, 0x7E11


def io_to_ni(io_s):
    return 2.0 + 1.00002 * io_s


def pellet_to_io(pellet_s):
    return 0.5 + 0.99999 * pellet_s


def synthetic_rig(*, seconds=30, tone_pellet_times=(15.25, 21.75)):
    """Claps and barcodes once a second, two pellet tones, sampled at RATE."""
    rows, frames, sequence = [], [], 1
    for tick in range(1, seconds):
        io_barcode_s = 10.0 + tick
        frames.append((io_to_ni(io_barcode_s), tick))
        rows.append(device_row(1000 + tick, "SYNC_BARCODE_REPORT", {"board_address": IO_ADDRESS, "tick": tick},
                               IO_BOOT, sequence, int(round(io_barcode_s * 1e6)), "physical_start"))
        io_clap_s = io_barcode_s + 0.2
        pellet_clap_s = (io_clap_s - 0.5) / 0.99999
        rows.append(device_row(1000 + tick, "SYNC_CLAP_OBSERVATION",
                               {"board_address": IO_ADDRESS, "tick": tick, "clapper_node": IO_ADDRESS},
                               IO_BOOT, sequence + 1, int(round(io_clap_s * 1e6)), "observed"))
        rows.append(device_row(1000 + tick, "SYNC_CLAP_OBSERVATION",
                               {"board_address": PELLET_ADDRESS, "tick": tick, "clapper_node": IO_ADDRESS},
                               PELLET_BOOT, sequence, int(round(pellet_clap_s * 1e6)), "observed"))
        sequence += 2
    total = io_to_ni(10.0 + seconds) + 0.5
    tone1 = np.zeros(int(math.ceil(total * RATE)), dtype=np.float32)
    for pellet_s in tone_pellet_times:
        onset = int(math.ceil(io_to_ni(pellet_to_io(pellet_s)) * RATE))
        tone1[onset:onset + 2000] = 1.0
        rows.append(device_row(1000 + pellet_s, "TONE_STATUS", {"frequency_hz": 5000, "time_remaining_ms": 200},
                               PELLET_BOOT, 9000, int(round(pellet_s * 1e6)), "physical_start"))
    channels = {"barcode": barcode_line(RATE, total, frames)[: tone1.size], "tone1": tone1}
    return tuple(rows), channels, np.arange(tone1.size, dtype=np.int64)
```

Then change the helper import in `tests/board_clock_chain_test.py` to `from board_clock_chain_synthetic import IO_ADDRESS, IO_BOOT, PELLET_ADDRESS, PELLET_BOOT, RATE, barcode_line, synthetic_rig` and append:

```python
def test_chain_composes_pellet_to_ni_and_tones_land_within_a_sample():
    rows, channels, indices = synthetic_rig()

    record = chain.board_clock_chain(rows, sample_indices=indices, sample_rate_hz=RATE, channels=channels)

    assert record["status"] == "fitted"
    timekeeper = record["timekeeper"]
    assert (timekeeper["address"], timekeeper["bootId"]) == (IO_ADDRESS, IO_BOOT)
    assert timekeeper["barcodeFramesMatched"] == 29
    assert timekeeper["toNi"]["drift"] == pytest.approx(1.00002, abs=2e-6)
    (board,) = record["boards"]
    assert (board["address"], board["bootId"]) == (PELLET_ADDRESS, PELLET_BOOT)
    assert board["toNi"]["valid"]
    check = board["toneCrossCheck"]
    assert check["matched"] == 2 and check["unmatched"] == 0
    assert check["maxAbsResidualSeconds"] <= 1.0 / RATE
    json.dumps(record, allow_nan=False)  # alignment.json rejects NaN


def test_no_barcode_reports_means_no_timekeeper():
    record = chain.board_clock_chain((), sample_indices=np.arange(10), sample_rate_hz=RATE, channels={})
    assert record["status"] == "unavailable"
    assert "no timekeeper" in record["reason"]


def test_missing_barcode_channel_is_unavailable():
    rows, channels, indices = synthetic_rig(seconds=5)
    del channels["barcode"]
    record = chain.board_clock_chain(rows, sample_indices=indices, sample_rate_hz=RATE, channels=channels)
    assert record["status"] == "unavailable"
    assert "barcode" in record["reason"]


def test_too_few_barcodes_leave_the_fit_invalid_and_json_safe():
    rows, channels, indices = synthetic_rig(seconds=2)
    record = chain.board_clock_chain(rows, sample_indices=indices, sample_rate_hz=RATE, channels=channels)
    assert record["status"] == "unavailable"
    json.dumps(record, allow_nan=False)
```

- [ ] **Step 2: Run to verify failure**

Run: `$RIG "cd ~/reachAQ-clock-chain && $PYTEST tests/board_clock_chain_test.py"`.

Expected: `AttributeError: module ... has no attribute 'board_clock_chain'`.

- [ ] **Step 3: Implement.** Append to `board_clock_chain.py`. Add `import json` and `import math` and `from typing import Dict, Mapping, Optional`, plus the `clocks` import, to the module's imports.

```python
NIDAQ_CHANNELS = (BARCODE_CHANNEL, "tone1", "tone2")
TONE_CHANNEL_BY_FREQUENCY_HZ = {5000: "tone1", 6000: "tone2"}
METHOD = "board_clap_and_nidaq_barcode"
#: 33 edges a frame at most; 20 already spans more than one frame.
BARCODE_FIT_MIN_POINTS = 20
#: Edge quantization alone is about 29 us rms at 10 kHz.
BARCODE_FIT_MAX_RESIDUAL_SECONDS = 0.0001
CLAP_FIT_MIN_POINTS = 5
#: Both clap stamps are taken in a CAN RX interrupt: a few us of jitter.
CLAP_FIT_MAX_RESIDUAL_SECONDS = 0.0001
TONE_MATCH_WINDOW_SECONDS = 0.005
#: The spec's end-to-end acceptance: |error| 95th percentile under 0.1 ms.
TONE_TARGET_P95_SECONDS = 0.0001

_KIND_CLAP, _KIND_BARCODE, _KIND_TONE = "SYNC_CLAP_OBSERVATION", "SYNC_BARCODE_REPORT", "TONE_STATUS"
_ROW_KIND, _ROW_PAYLOAD, _ROW_BOOT, _ROW_BOARD_US, _ROW_STAMP_KIND = 3, 8, 12, 14, 15


@dataclasses.dataclass(frozen=True)
class _Envelope:
    address: Optional[int]
    boot_id: int
    board_us: int
    stamp_kind: str
    payload: Mapping


def _int_or_none(value) -> Optional[int]:
    if value is None or value == "":
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _finite(value) -> Optional[float]:
    value = float(value)
    return value if math.isfinite(value) else None


def _fit_record(fit) -> dict:
    return {
        "offset": _finite(fit.offset), "drift": _finite(fit.drift),
        "residualRms": _finite(fit.residual_rms), "points": int(fit.points),
        "valid": bool(fit.valid), "reason": str(fit.reason),
        "xMin": _finite(fit.x_min), "xMax": _finite(fit.x_max),
    }


def _unavailable(reason: str) -> dict:
    return {"status": "unavailable", "reason": reason, "method": METHOD, "boards": []}


def _envelopes(device_rows, kind_name):
    found = []
    for row in device_rows:
        if len(row) <= _ROW_STAMP_KIND or row[_ROW_KIND] != kind_name:
            continue
        boot, board_us = _int_or_none(row[_ROW_BOOT]), _int_or_none(row[_ROW_BOARD_US])
        if boot is None or board_us is None:
            continue
        try:
            payload = json.loads(row[_ROW_PAYLOAD] or "{}")
        except (TypeError, ValueError):
            continue
        if isinstance(payload, dict):
            found.append(_Envelope(
                _int_or_none(payload.get("board_address")), boot, board_us,
                str(row[_ROW_STAMP_KIND] or ""), payload,
            ))
    return tuple(found)


def _tone_cross_check(tones, offset, drift, indices, rate, channels) -> dict:
    residuals, unmatched, considered = [], 0, 0
    edges_by_channel: Dict[str, numpy.ndarray] = {}
    for tone in tones:
        channel = TONE_CHANNEL_BY_FREQUENCY_HZ.get(_int_or_none(tone.payload.get("frequency_hz")))
        if tone.stamp_kind != "physical_start" or channel not in channels:
            continue
        considered += 1
        if channel not in edges_by_channel:
            samples = numpy.asarray(channels[channel], dtype=numpy.float64)
            high = numpy.isfinite(samples) & (samples > 0.5)
            rising = numpy.flatnonzero(high[1:] & ~high[:-1]) + 1
            edges_by_channel[channel] = indices[rising] / rate
        edges = edges_by_channel[channel]
        predicted = offset + drift * (tone.board_us / 1e6)
        if edges.size == 0:
            unmatched += 1
            continue
        nearest = float(edges[int(numpy.argmin(numpy.abs(edges - predicted)))])
        if abs(nearest - predicted) > TONE_MATCH_WINDOW_SECONDS:
            unmatched += 1
            continue
        residuals.append(predicted - nearest)
    summary = {
        "tonesConsidered": considered, "matched": len(residuals), "unmatched": unmatched,
        "targetP95Seconds": TONE_TARGET_P95_SECONDS, "meetsTarget": None,
    }
    if residuals:
        magnitude = numpy.abs(numpy.asarray(residuals, dtype=numpy.float64))
        p95 = float(numpy.percentile(magnitude, 95))
        summary.update({
            "meanResidualSeconds": float(numpy.mean(residuals)),
            "p50AbsResidualSeconds": float(numpy.percentile(magnitude, 50)),
            "p95AbsResidualSeconds": p95,
            "maxAbsResidualSeconds": float(magnitude.max()),
            "meetsTarget": bool(p95 < TONE_TARGET_P95_SECONDS),
        })
    return summary


def board_clock_chain(device_rows, *, sample_indices, sample_rate_hz, channels) -> dict:
    """Fit every clapping board's time to NI seconds through the timekeeper.

    NI seconds are NI sample index / sample rate; cameraNidaqAlignment.niToHost
    maps them to host perf time.
    """
    reports = _envelopes(device_rows, _KIND_BARCODE)
    if not reports:
        return _unavailable("no SYNC_BARCODE reports: no timekeeper was on the bus")
    if BARCODE_CHANNEL not in channels or not sample_rate_hz:
        return _unavailable("the NI-DAQ recorded no barcode channel")
    rate = float(sample_rate_hz)
    if rate < MIN_BARCODE_RATE_HZ:
        return _unavailable(f"NI-DAQ rate {rate:g} Hz is below the {MIN_BARCODE_RATE_HZ:g} Hz a barcode needs")
    indices = numpy.asarray(sample_indices, dtype=numpy.float64)
    frames, rejected = decode_barcode_frames(channels[BARCODE_CHANNEL], rate)

    by_timekeeper: Dict[tuple, Dict[int, int]] = {}
    for report in reports:
        tick = _int_or_none(report.payload.get("tick"))
        if tick is not None:
            by_timekeeper.setdefault((report.address, report.boot_id), {})[tick] = report.board_us
    timekeeper, ticks = max(
        by_timekeeper.items(), key=lambda item: sum(frame.tick in item[1] for frame in frames),
    )
    x, y, matched = [], [], 0
    for frame in frames:
        start_us = ticks.get(frame.tick)
        if start_us is None:
            continue
        matched += 1
        for position, offset in frame.edges:
            x.append(start_us / 1e6 + offset)
            y.append(indices[position] / rate)
    to_ni = clocks.fit_line(
        x, y, max_residual=BARCODE_FIT_MAX_RESIDUAL_SECONDS, min_points=BARCODE_FIT_MIN_POINTS,
    )
    record = {
        "status": "fitted" if to_ni.valid else "unavailable",
        "reason": "" if to_ni.valid else f"timekeeper to NI-DAQ: {to_ni.reason}",
        "method": METHOD,
        "niTime": "NI sample index / sample rate (s); cameraNidaqAlignment.niToHost maps it to host perf time",
        "timekeeper": {
            "address": timekeeper[0], "bootId": int(timekeeper[1]),
            "barcodeFramesDecoded": len(frames), "barcodeFramesRejected": int(rejected),
            "barcodeFramesMatched": matched, "toNi": _fit_record(to_ni),
        },
        "boards": [],
    }
    if not to_ni.valid:
        return record

    claps = _envelopes(device_rows, _KIND_CLAP)
    own: Dict[int, int] = {}
    heard: Dict[tuple, Dict[int, int]] = {}
    for clap in claps:
        tick = _int_or_none(clap.payload.get("tick"))
        if tick is None or _int_or_none(clap.payload.get("clapper_node")) != timekeeper[0]:
            continue
        if (clap.address, clap.boot_id) == timekeeper:
            own[tick] = clap.board_us
        else:
            heard.setdefault((clap.address, clap.boot_id), {})[tick] = clap.board_us
    tones = _envelopes(device_rows, _KIND_TONE)
    for (address, boot_id), board_ticks in sorted(heard.items(), key=lambda item: (item[0][0] or 0, item[0][1])):
        common = sorted(set(board_ticks) & set(own))
        to_timekeeper = clocks.fit_line(
            [board_ticks[tick] / 1e6 for tick in common], [own[tick] / 1e6 for tick in common],
            max_residual=CLAP_FIT_MAX_RESIDUAL_SECONDS, min_points=CLAP_FIT_MIN_POINTS,
        )
        board = {
            "address": address, "bootId": int(boot_id), "clapsMatched": len(common),
            "toTimekeeper": _fit_record(to_timekeeper),
            "toNi": {"offset": None, "drift": None, "valid": False}, "toneCrossCheck": None,
        }
        if to_timekeeper.valid:
            offset = float(to_ni.offset + to_ni.drift * to_timekeeper.offset)
            drift = float(to_ni.drift * to_timekeeper.drift)
            board["toNi"] = {"offset": offset, "drift": drift, "valid": True}
            board["toneCrossCheck"] = _tone_cross_check(
                [tone for tone in tones if tone.boot_id == boot_id], offset, drift, indices, rate, channels,
            )
        record["boards"].append(board)
    return record
```

- [ ] **Step 4: Run to verify pass.** Run `$RIG "cd ~/reachAQ-clock-chain && $PYTEST tests/board_clock_chain_test.py"`. Expected: all pass.

- [ ] **Step 5: Commit, sync, and run the same command on the committed state**

```bash
git add tools/acquisition/model/board_clock_chain.py tests/board_clock_chain_synthetic.py tests/board_clock_chain_test.py
git commit -m "feat(sync): fit board time to NI time through claps and barcodes

Timekeeper to NI from every barcode edge, each board to the timekeeper from
paired clap stamps, composed; pellet tone stamps checked against the tone edges
the NI-DAQ recorded give the accuracy evidence."
```

---

### Task 7: `alignment.json` gains `boardClockChain`

**Files:**
- Modify: `tools/acquisition/model/session_data_recorder.py`
  - import the new module
  - `_write_session`: capture untrimmed rows before the trim (around line 1723), compute the chain after `ni_fit` (around line 1761), add it to the `alignment` dict (around line 2294-2394) and to the return dict (around line 2436)
  - a new static `_board_clock_chain`
- Test: `tests/session_data_recorder_test.py` (append)

**Interfaces:**
- Consumes `board_clock_chain.board_clock_chain` and `NIDAQ_CHANNELS`, plus `SessionDataRecorder._nidaq_arrays(chunks, requested_names)` and `_normalize_device_row`.
- Produces `alignment.json["boardClockChain"]` and `_write_session(...)["boardClockChain"]`.

- [ ] **Step 1: Write the failing tests.** Append to `tests/session_data_recorder_test.py`:

```python
def test_alignment_records_the_board_clock_chain(tmp_path):
    from board_clock_chain_synthetic import RATE, synthetic_rig

    rows, channels, indices = synthetic_rig()
    perf = 1000.0 + indices / RATE
    names = ("barcode", "tone1")
    chunk = (
        indices, perf, 2000.0 + (perf - 1000.0),
        np.vstack([channels["barcode"], channels["tone1"]]).astype(np.float32),
        names, RATE, 1, 0, 0,
    )
    project = ProjectInfo(root=str(tmp_path), device_id="test", when=datetime(2026, 1, 2, 3, 4, 5), session=4)

    result = SessionDataRecorder._write_session(
        project, 1000.0, 2000.0, float(perf[-1]), rows, (), (), (chunk,),
    )

    assert result["boardClockChain"]["status"] == "fitted"
    session_dir = next(Path(tmp_path).rglob("alignment.json")).parent
    alignment = json.loads((session_dir / "alignment.json").read_text(encoding="utf-8"))
    assert alignment["boardClockChain"]["boards"][0]["toneCrossCheck"]["matched"] == 2
    assert alignment["schemaVersion"] == 3


def test_alignment_without_a_timekeeper_says_so_and_still_finalizes(tmp_path):
    project = ProjectInfo(root=str(tmp_path), device_id="test", when=datetime(2026, 1, 2, 3, 4, 5), session=5)

    result = SessionDataRecorder._write_session(project, 10.0, 100.0, 11.0, (), (), (), ())

    assert result["boardClockChain"]["status"] == "unavailable"
```

- [ ] **Step 2: Run to verify failure**

Run: `$RIG "cd ~/reachAQ-clock-chain && $PYTEST tests/session_data_recorder_test.py -k board_clock_chain"`.

Expected: `KeyError: 'boardClockChain'`.

- [ ] **Step 3: Implement.** In `session_data_recorder.py`, import the module the way that file imports its sibling models (e.g. `from tools.acquisition.model import board_clock_chain`).

Add a static method near `_fit_nidaq_to_host`:

```python
    @staticmethod
    def _board_clock_chain(device_rows, nidaq_chunks):
        """Pellet-to-NI clock chain, or why there is none; never raised.

        Read from the whole spool and the untrimmed rows: the chain is a fit,
        and pre-roll and post-roll claps and barcodes are as good as any.
        """
        try:
            names, indices, _perf, values, rate = SessionDataRecorder._nidaq_arrays(
                nidaq_chunks, requested_names=board_clock_chain.NIDAQ_CHANNELS,
            )
            channels = {name: values[row] for row, name in enumerate(names)}
            return board_clock_chain.board_clock_chain(
                device_rows, sample_indices=indices, sample_rate_hz=rate, channels=channels,
            )
        except Exception as error:  # finalization must never fail on this evidence
            logger.exception("Board clock chain failed")
            return {
                "status": "unavailable",
                "reason": f"board clock chain failed: {error}",
                "method": board_clock_chain.METHOD,
                "boards": [],
            }
```

In `_write_session`, directly before `device_rows = tuple(row for row in device_rows if start_perf <= row[0] <= end_perf)`, add:

```python
        untrimmed_device_rows = tuple(
            SessionDataRecorder._normalize_device_row(row) for row in device_rows
        )
```

After `ni_fit = SessionDataRecorder._fit_nidaq_to_host(nidaq_chunks)`, add:

```python
        board_clock_chain_record = SessionDataRecorder._board_clock_chain(
            untrimmed_device_rows, nidaq_chunks,
        )
```

In the `alignment = {...}` dict, add `"boardClockChain": board_clock_chain_record,` immediately after the `"toneConfirmation": ...` entry. In the final returned dict, add the same key after `"toneConfirmation": tone_confirmation,`.

If `logger` is named differently in that module, use its name.

- [ ] **Step 4: Run to verify pass, plus the suites around it**

```bash
$RIG "cd ~/reachAQ-clock-chain && $PYTEST tests/session_data_recorder_test.py tests/board_clock_chain_test.py tests/frame_alignment_test.py tests/session_latency_finalize_test.py tests/pressure_stream_recording_test.py"
```

Expected: all pass.

- [ ] **Step 5: Commit, sync, and run the same command on the committed state**

```bash
git add tools/acquisition/model/session_data_recorder.py tests/session_data_recorder_test.py
git commit -m "feat(session): record the board clock chain in alignment.json

A new boardClockChain section: the timekeeper's NI fit, each board's fit to it,
their composition to NI seconds, and the pellet-tone cross-check. device.csv
event times are untouched, and schemaVersion stays 3."
```
---

### Task 8: Docs and verification

**Files:**
- Modify: `docs/acquisition/session-recording-and-synchronization.md` (a new subsection near the NI-DAQ synchronization section)
- Modify: `docs/acquisition/pellet-firmware-compatibility.md` (a note under the current policy)

- [ ] **Step 1: Document**

In `session-recording-and-synchronization.md`, add a subsection **Board clock chain (`alignment.json` `boardClockChain`)**. It should say:
- what the timekeeper sends: a clap every second on CAN, and the same tick as a barcode on the NI `barcode` line, frame format as in Global Constraints
- how the fits are made: timekeeper→NI from every barcode edge; board→timekeeper from paired clap stamps; composed into NI seconds; host perf through `cameraNidaqAlignment.niToHost`
- that `toneCrossCheck` is the accuracy evidence (target: p95 |residual| < 0.1 ms)
- that device event times are never rewritten
- the `status: unavailable` reasons
- the validator change: `events.board_time` judges each board in its own sequence order

In `pellet-firmware-compatibility.md`, add one paragraph:
- pellet firmware 2.4.0 is the first to advertise `time_sync`
- reachAQ releases before this branch fail to open such a board, because the board-clock fit raised on the 4 closely spaced exchanges in `open()`
- the 2.4.0 allowlist entry therefore requires this branch

- [ ] **Step 2: Final verification on the committed state**

```bash
$RIG "cd ~/reachAQ-clock-chain && $PYTEST auto-trainer-device/tests tests/session_validation_test.py tests/session_data_recorder_test.py tests/board_clock_chain_test.py tests/frame_alignment_test.py tests/latency_clocks_test.py tests/firmware_compatibility_test.py tests/can_interface_call_contract_test.py tests/session_latency_finalize_test.py"
```

Expected: all pass. Compare against the Task 0 baseline.

- [ ] **Step 3: Commit and report**

```bash
git add docs/acquisition/session-recording-and-synchronization.md docs/acquisition/pellet-firmware-compatibility.md
git commit -m "docs: the board clock chain, and why pellet 2.4.0 needs this reachAQ"
```

Report:
- branch and commits
- test counts against the baseline
- that end-to-end validation needs the IO module (H1/H3) on the bus

Do not merge or push.

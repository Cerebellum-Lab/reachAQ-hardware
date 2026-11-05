# H1: IO-module firmware on a NUCLEO-G474RE — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Firmware for the IO module ("Balanced 32": 12 DI, 8 TTL out, 8 analog in, 4 power out), which is also the system timekeeper. It is developed against a NUCLEO-G474RE, so it is ready before the PCB exists.

**Architecture:**
- **New app `firmware/io_module/`**, built for Zephyr's `nucleo_g474re` with an app overlay. It reuses jerrycan's core, the T2 `timing` module, and the `ll,generic-gpios` driver.
- **Two new LL drivers:**
  - `ll,barcode-generator`: a TIM3 PWM output whose level is preloaded every 1 ms from its update interrupt.
  - `ll,analog-scanner`: TIM6 TRGO triggers an ADC1 scan, read by circular DMA.
- **Three new jerrycan modules:**
  - `timekeeper.c`: clap, barcode and their reports
  - `analog_stream.c`: `ANALOG_BLOCK`
  - `power_out.c`: `PWM_SET`, ESTOP and the heartbeat watchdog
- **One change to `gpio.c`:** change events are stamped `OBSERVED` and rate-capped.
- **Zephyr-free logic has host C tests:** the barcode levels, the change-event limiter, `ANALOG_BLOCK` packing and the power-output rules. Everything else is verified by building for the board now, and on hardware in Tasks 9-11.

**Tech Stack:** Zephyr 3.7, STM32G474 (FDCAN, TIM, ADC, DMA via LL), C11. Host tests use CMake, gcc and ctest. The hardware check uses Python 3 (standard library only).

**Spec:** `docs/superpowers/specs/2026-10-07-io-module-timing-design.md`, sections 4, 6, 7, 9 and 11.

## Decisions this plan makes that the spec leaves open or changes

Reviewed by the user on 2026-11-03: decisions 1 (barcode from the update interrupt) and 5 (heartbeat-gated power outputs) were explicitly approved, and none was vetoed. The spec now records 1, 2 and 5.

1. **Barcode from the timer update interrupt, not DMA (changes spec §4.5/§7).**
   - Edges still come from the hardware timer: TIM3 runs PWM with CCR preload, and each edge happens at an update event.
   - The 1 kHz update ISR only writes the *next* slot's level into the preload register, with ~999 µs of slack.
   - This removes the DMA ring and its refill race. The cost is a 1 kHz ISR of under 1 µs.
   - The failure mode is an ISR held off by more than 1 ms. That gives one wrong bit, which T3's decoder rejects through its edge-count check; it cannot be mis-decoded.
2. **One timekeeper per bus.**
   - The board claps and plays the barcode only when its node id equals `CONFIG_LIB_JERRYCAN_TIMEKEEPER_NODE_ID` (default 8, so DIP 0).
   - A second IO module (addresses 9-11, needed above 32 I/O) only listens.
   - The `timekeeper` capability bit follows the role.
3. **MCUboot and CAN DFU are deferred to H2.** The NUCLEO is flashed by ST-Link. `jerrycan_run()`'s call to `confirm_image()` becomes conditional on `CONFIG_BOOTLOADER_MCUBOOT` so an app without MCUboot links.
4. **The `cerebellumlab_io_module` board is deferred to H2,** where the PCB pin map is final. H1 targets `nucleo_g474re` only.
5. **`PWM_SET` with a non-zero duty is refused (`-ETIMEDOUT`) unless a `HEARTBEAT` arrived in the last 2 s.**
   - A watchdog forces every output off 2 s after the last heartbeat.
   - **reachAQ sends no periodic heartbeat today** (`send_heartbeat()` has no caller), so the host IO-module plan must add one at ≥ 1 Hz.
6. **`ANALOG_BLOCK` streams from boot** at 1 kHz × 8 channels, with `flags` 0. Host-controlled enable is out of scope.
7. **The version string comes from the repo's tags,** which are the pellet's (`gen_version.py`). An IO-module version and release scheme is for H2/H3.
8. **GPIO index map** (the readable-pin index used by `GPIO_READ`, `GPIO_WRITE` and `GPIO_PULSE`):
   - 0-1: `CAN_ID[0..1]`
   - 2-13: `DI0`-`DI11`
   - 14-21: `TTL0`-`TTL7`

   The node number stays in readable bits 0-1, as on the pellet.

## Global Constraints

- **Wire contract (unchanged from T2):**
  - Trailer `<BBBBIIQ` after the uuid when payload + uuid + 20 ≤ 64. Kinds: `TX_QUEUE 0`, `OBSERVED 1`, `PHYSICAL_START 3`, `COMPLETED 4`, `SAMPLED 5`. Flag bit 0 is `COALESCED`.
  - IDs and sizes:

    | Message | ID | Payload |
    |---|---|---|
    | `SYNC_CLAP` | 0x25 | 4 B `<I` |
    | `SYNC_CLAP_OBSERVED` | 0x26 | 5 B `<IB` |
    | `SYNC_BARCODE` | 0x27 | 4 B `<I` |
    | `ANALOG_BLOCK` | 0x28 | 38 B `<BBBBH16H` |
    | `PWM_SET` | 0x29 | 7 B `<BHI` |

  - Capabilities: `TIMING_TRAILER 1<<0`, `TIME_SYNC 1<<1`, `FINITE_GPIO_PULSE 1<<2`, `SYNC_LISTENER 1<<4`, `TIMEKEEPER 1<<5`.
  - Expected capabilities: the timekeeper IO module reports **`0x37`**, and any other IO module **`0x17`**.
- **Addresses:** the IO module is board type 2 (addresses 8-11). The type comes from devicetree (`zephyr,user` `jerrycan-board-type = <2>`). The low 2 bits come from `CAN_ID[0..1]`.
- **Barcode:**
  - Each tick: a 1 ms high start pulse, then the 32-bit tick LSB-first at 1 ms per bit, then low until the next tick. Ticks are 1 s apart.
  - Tick N's start edge is at `start_us + N × 1 000 000` on the board clock (`board_clock_cycles_to_us(board_clock_cycles())`, the same clock as `jerrycan_board_time_us()`).
  - On 2026-11-05, fix wave A replaced `k_cycle_get_64()` with the DWT board clock (`firmware/lib/board_clock/`, spec §5). Task steps below that still name `k_cycle_get_64()` were written before that change.
  - Tick 0 starts when the barcode starts.
- **Analog:** 1 kHz scans of 8 channels on ADC1, 2 scans per `ANALOG_BLOCK` (500 frames/s), raw 12-bit counts. The trailer is `SAMPLED` at the first scan's trigger time.
- **Rate cap:** change-triggered `GPIO_READ` is capped at 2000 frames/s with a burst of 8. Merged changes carry `COALESCED`. The 100 ms periodic `GPIO_READ` (`SAMPLED`) is unchanged.
- **Safety (spec §9):**
  - Power outputs are off at boot, on ESTOP, and 2 s after the last `HEARTBEAT`.
  - TTL outputs are low at boot and otherwise hold their state.
  - Bus-off (user decision 2026-11-05: the spec follows the firmware, which is unchanged): a transient bus-off recovers in about 1.4 ms and the heartbeats continue, so the power outputs stay on, because host control survives. A persistent bus-off stops the heartbeats, so the outputs go off within 2 s.
  - Faults and hangs (IO module only; user decision 2026-11-05, final fix F5):
    - A 1 s hardware watchdog (IWDG) is fed every 250 ms from the system workqueue, the thread that also runs the power-output off path. It pauses while a debugger halts the core. A hang, or a DI signal fast enough to starve the workqueue, resets the board within about 1 s, with the outputs off.
    - A strong `k_sys_fatal_error_handler()` clears TIM1's main output enable at the register first, then logs and resets. On a fault the outputs go off within tens of microseconds (Zephyr logs before it calls the handler), well under a PWM period.
    - A reset forgets ESTOP, but the outputs boot off. F5 does not change the pellet or magnet image.
- **Additive for the other boards:**
  - The pellet and magnet builds keep their behaviour. Every shared-library change is a no-op unless the board opts in through Kconfig or devicetree.
  - `CONFIG_LIB_JERRYCAN_TIMEKEEPER` and `CONFIG_LIB_JERRYCAN_GPIO_EVENT_RATE_HZ` default to off.
  - Pellet `text` may grow by at most 512 B over Task 0's baseline, and `data`/`bss` by at most 16 B.
  - **Ruled exception: the board clock (user decision 2026-11-04).** The DWT board clock is shared code, so it changes the pellet too (fix waves A and D, ported to `feat/pellet-board-timing` as bc394f1 and 35d27f7). It is outside the budget above.
    - Task 0's baseline plus the board clock is `216064/37093/42888` (`215588/37033/42879` plus `476/60/9`). That is the pellet as built on `feat/pellet-board-timing` after the port.
    - The budget applies to the rest of this branch's pellet change: the head's pellet minus `216064/37093/42888` must be at most `512/16/16`.
    - At bb8a13a the pellet is `216284/37093/42888`, so the rest is `220/0/0`: Task 4's limiter hook `116` and fix wave D's by-index GPIO read `104`. The final fix replaces that read with a per-port read and reports the new pellet figure.
- **Builds only on christielab10, in its designated checkout `~/Documents/reachAQ-hardware`**, because its west manifest is that checkout's own `firmware/west.yml`.
  - Record its ref in Task 0. Restore it (detached `08c1b41`, clean) at the end of **every** working session and before any pause.
  - Build outputs go under `/tmp/h1-*`.
- **Never flash the rig's pellet board.** This plan does not need it.
- **Hardware tasks (9-11) need the user's explicit go-ahead in that session:**
  - The NUCLEO and a CAN transceiver must exist.
  - Joining the live rig bus is an outward action.
  - **reachAQ stays closed while the IO module is on the bus** unless reachAQ's T1 tolerance is deployed. Today's reachAQ loses whole receive batches on a known type from a non-pellet address (spec §2).
- **Generic-gpios callback sharing:** `ll,generic-gpios` adds one `gpio_callback` to every port it uses. That is safe only while it is the first callback added on each of those ports. Do not register a GPIO callback earlier in boot than `CONFIG_GPIO_INIT_PRIORITY`.
- **Style:** `firmware/.clang-format`, 120 columns. Files are LF.
- **Commits** end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Do not push or merge.

## Review Focus

The spec implies these, but they are easy to leave untested. Each is pinned by a test in the named task.

1. **A DI edge storm** (contact bounce, a 10-20 kHz signal). The bus load stays at about 2000 frames/s, the last state is never lost, and merged frames say so.
   - Task 1: `test_a_sustained_burst_is_capped_at_the_rate`, `test_no_change_overtakes_a_held_one`.
   - Task 11, Step 2: on the wire.
2. **`PWM_SET` with out-of-range values** (channel ≥ 4, duty > 10000, a period outside 10-6553 µs). The request is refused with `-EINVAL` and no output changes.
   - Task 2: `test_out_of_range_requests_are_invalid`.
   - Task 11, Step 4.
3. **Today's host, which sends no heartbeats, or an ESTOP during output.** Outputs never come on, or they go off, and the refusal says why.
   - Task 2: `test_estop_refuses_any_output_but_allows_off`, `test_without_a_recent_heartbeat_outputs_stay_off`.
   - Task 11, Step 4.
4. **A second IO module on the bus.** Only address 8 claps. Address 9 reports `0x17` and plays no barcode. Task 10, Step 4.
5. **Barcode integrity over minutes**, with ISR load from DI storms and analog DMA. Ticks are continuous and the DAQ decoder rejects nothing. Task 10, Step 3.

---

**Shorthands:**

```bash
RIG="ssh -i ~/.ssh/reachaq_debug christielab10@christielab10"
# On the rig, from ~/Documents/reachAQ-hardware:
ENV='source .build-host.env && export ZEPHYR_SDK_INSTALL_DIR && export PATH=$PWD/firmware/.venv/bin:$PATH'
SIZE=~/Documents/zephyr-sdk-0.16.8/arm-zephyr-eabi/bin/arm-zephyr-eabi-size
BUILD_IO='west build --build-dir /tmp/h1-io --board nucleo_g474re --no-sysbuild --pristine always firmware/io_module'
BUILD_PELLET='west build --build-dir /tmp/h1-pellet --sysbuild --board cerebellumlab_pellet_module --pristine always firmware/pellet_module'
BUILD_MAGNET='west build --build-dir /tmp/h1-magnet --sysbuild --board cerebellumlab_magnet_module --pristine always firmware/magnet_module'
HOST_TEST='cmake -S firmware/lib/jerrycan/test -B /tmp/h1-host-test -GNinja && cmake --build /tmp/h1-host-test && ctest --test-dir /tmp/h1-host-test --output-on-failure'
```

**Sync to the rig after each commit:**

```bash
git -C C:/Users/reynoben/Documents/reachAQ-hardware-io bundle create C:/Users/reynoben/h1.bundle 08c1b41..feat/io-module-firmware
scp -i ~/.ssh/reachaq_debug C:/Users/reynoben/h1.bundle christielab10@christielab10:/tmp/h1.bundle
$RIG 'cd ~/Documents/reachAQ-hardware && git pull --ff-only /tmp/h1.bundle feat/io-module-firmware'
```

**File map:**

| File | Responsibility |
|---|---|
| `firmware/lib/jerrycan/gpio_event_limiter.{c,h}` | Token-bucket cap with merge-and-hold for change events (Zephyr-free) |
| `firmware/lib/jerrycan/analog_block.{c,h}` | `ANALOG_BLOCK` byte packing and scan times (Zephyr-free) |
| `firmware/lib/jerrycan/power_out_policy.{c,h}` | `PWM_SET` validation, ESTOP/heartbeat rules, pulse width (Zephyr-free) |
| `firmware/drivers/barcode/barcode_frame.{c,h}` | Level of the barcode line in any 1 ms slot (Zephyr-free) |
| `firmware/lib/jerrycan/test/*_test.c`, `CMakeLists.txt` | Host tests, registered with ctest |
| `firmware/drivers/barcode/` (+ `dts/bindings/barcode/`) | `ll,barcode-generator`: TIM3 output and per-tick callback |
| `firmware/drivers/analog_scanner/` (+ `dts/bindings/analog_scanner/`) | `ll,analog-scanner`: TIM6 → ADC1 scan → DMA, 2 scans per callback |
| `firmware/lib/jerrycan/modules/timekeeper.c` | Tick N: `SYNC_BARCODE`, `SYNC_CLAP`, own `SYNC_CLAP_OBSERVED` |
| `firmware/lib/jerrycan/modules/analog_stream.c` | Scanner callback → `ANALOG_BLOCK` |
| `firmware/lib/jerrycan/modules/power_out.c` (+ `dts/bindings/power_outputs/`) | `ll,power-outputs`: `PWM_SET`, ESTOP, heartbeat watchdog |
| `firmware/lib/jerrycan/modules/gpio.c` | Change events `OBSERVED` and rate-capped |
| `firmware/lib/jerrycan/jerrycan.{c,h}` | Board type from devicetree, `jerrycan_node_id()`, clap TX-done hook, `confirm_image()` guard |
| `firmware/lib/jerrycan/modules/capabilities.c` | `TIMEKEEPER` bit when the role is active |
| `firmware/io_module/` | App, prj.conf, NUCLEO overlay, README, hardware check |

---

### Task 0: Workspace, rig checkout, baseline

**Files:** none

- [ ] **Step 1: Local worktree from the T2 branch.** It carries the timing core this plan builds on.

```bash
git -C C:/Users/reynoben/Documents/reachAQ-hardware worktree add C:/Users/reynoben/Documents/reachAQ-hardware-io -b feat/io-module-firmware feat/pellet-board-timing
```

- [ ] **Step 2: Record the rig checkout.** Expected: detached at `08c1b41`, clean. If it is not clean, STOP and report. Do not stash someone else's work.

```bash
$RIG 'cd ~/Documents/reachAQ-hardware && git rev-parse --short HEAD && git status --short | wc -l'
```

- [ ] **Step 3: Put the branch on the rig**

```bash
git -C C:/Users/reynoben/Documents/reachAQ-hardware-io bundle create C:/Users/reynoben/h1.bundle 08c1b41..feat/io-module-firmware
scp -i ~/.ssh/reachaq_debug C:/Users/reynoben/h1.bundle christielab10@christielab10:/tmp/h1.bundle
$RIG 'cd ~/Documents/reachAQ-hardware && git fetch /tmp/h1.bundle feat/io-module-firmware:feat/io-module-firmware && git switch feat/io-module-firmware'
```

- [ ] **Step 4: Baselines.** Record all three outputs in the task report. Later tasks compare against them.

```bash
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $BUILD_PELLET 2>&1 | tail -2 && $SIZE /tmp/h1-pellet/pellet_module/zephyr/zephyr.elf"
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $BUILD_MAGNET 2>&1 | grep -iE 'error|FAILED' | sort -u > /tmp/h1-magnet-baseline.txt; cat /tmp/h1-magnet-baseline.txt"
$RIG "cd ~/Documents/reachAQ-hardware && cmake -S firmware/lib/jerrycan/test -B /tmp/h1-host-test -GNinja >/dev/null && cmake --build /tmp/h1-host-test && /tmp/h1-host-test/timing_trailer_test"
```

Expected:
- pellet `text/data/bss` is `215588/37033/42879`, as at T2's final fix wave.
- the magnet's error lines are only the pre-existing link failure (no `CONFIG_ENTROPY_GENERATOR` behind `sys_rand32_get`).
- `timing_trailer_test` passes.

---

### Task 1: Change-event rate limiter (host-tested)

**Files:**
- Create: `firmware/lib/jerrycan/gpio_event_limiter.h`, `firmware/lib/jerrycan/gpio_event_limiter.c`
- Create: `firmware/lib/jerrycan/test/gpio_event_limiter_test.c`
- Modify: `firmware/lib/jerrycan/test/CMakeLists.txt`

**Interfaces:**
- Produces, consumed by Task 4:

```c
typedef struct {
    uint32_t rate_hz;      /* 0: no cap */
    uint64_t token_us;     /* 1e6 / rate_hz */
    uint64_t capacity_us;  /* burst * token_us */
    uint64_t credit_us;    /* starts full */
    uint64_t last_us;
    bool held;
    uint32_t held_state;
    uint64_t held_time_us;
    uint32_t held_count;
} gpio_event_limiter_t;

void gpio_event_limiter_init(gpio_event_limiter_t *limiter, uint32_t rate_hz, uint32_t burst);
/* True: send this change now, flags 0, at its own time. False: it is held, merged with any change already held;
   *release_us is when gpio_event_limiter_release() can send it. */
bool gpio_event_limiter_offer(gpio_event_limiter_t *limiter, uint64_t now_us, uint32_t state, uint64_t *release_us);
/* True: *state/*time_us are the latest merged change, *flags is JERRYCAN_TIMING_FLAG_COALESCED when more than one
   change was merged, else 0. False: nothing sendable yet; *release_us is when to retry, or 0 when nothing is held. */
bool gpio_event_limiter_release(gpio_event_limiter_t *limiter, uint64_t now_us, uint32_t *state, uint64_t *time_us,
                                uint8_t *flags, uint64_t *release_us);
```

- [ ] **Step 1: Write the failing test.** Create `gpio_event_limiter_test.c` with the same `CHECK` macro and `main` shape as `timing_trailer_test.c`, including `jerrycan_timing.h` for the flag. These tests:

```c
static void test_rate_zero_sends_every_change(void) {
    gpio_event_limiter_t limiter;
    gpio_event_limiter_init(&limiter, 0, 8);
    uint64_t release_us = 0;
    for (int i = 0; i < 10000; i++) {
        CHECK(gpio_event_limiter_offer(&limiter, 1000, (uint32_t)i, &release_us));
    }
}

static void test_a_burst_passes_then_changes_are_held(void) {
    gpio_event_limiter_t limiter;
    gpio_event_limiter_init(&limiter, 2000, 8);
    uint64_t release_us = 0;
    for (int i = 0; i < 8; i++) {
        CHECK(gpio_event_limiter_offer(&limiter, 1000, 1, &release_us));
    }
    CHECK(!gpio_event_limiter_offer(&limiter, 1000, 0, &release_us));
    CHECK(release_us == 1500);
}

static void test_a_release_carries_the_latest_merged_change(void) {
    gpio_event_limiter_t limiter;
    gpio_event_limiter_init(&limiter, 2000, 1);
    uint64_t release_us = 0;
    uint32_t state = 0;
    uint64_t time_us = 0;
    uint8_t flags = 0xFF;
    CHECK(gpio_event_limiter_offer(&limiter, 1000, 0x1, &release_us));
    CHECK(!gpio_event_limiter_offer(&limiter, 1100, 0x0, &release_us));
    CHECK(!gpio_event_limiter_offer(&limiter, 1200, 0x3, &release_us));
    CHECK(release_us == 1500);
    CHECK(!gpio_event_limiter_release(&limiter, 1499, &state, &time_us, &flags, &release_us));
    CHECK(release_us == 1500);
    CHECK(gpio_event_limiter_release(&limiter, 1500, &state, &time_us, &flags, &release_us));
    CHECK(state == 0x3);
    CHECK(time_us == 1200);
    CHECK(flags == JERRYCAN_TIMING_FLAG_COALESCED);
    CHECK(!gpio_event_limiter_release(&limiter, 1600, &state, &time_us, &flags, &release_us));
    CHECK(release_us == 0);
}

static void test_a_single_held_change_keeps_its_own_time_unflagged(void) {
    gpio_event_limiter_t limiter;
    gpio_event_limiter_init(&limiter, 2000, 1);
    uint64_t release_us = 0;
    uint32_t state = 0;
    uint64_t time_us = 0;
    uint8_t flags = 0xFF;
    CHECK(gpio_event_limiter_offer(&limiter, 1000, 0x1, &release_us));
    CHECK(!gpio_event_limiter_offer(&limiter, 1100, 0x0, &release_us));
    CHECK(gpio_event_limiter_release(&limiter, 1500, &state, &time_us, &flags, &release_us));
    CHECK(state == 0x0);
    CHECK(time_us == 1100);
    CHECK(flags == 0);
}

static void test_no_change_overtakes_a_held_one(void) {
    gpio_event_limiter_t limiter;
    gpio_event_limiter_init(&limiter, 2000, 1);
    uint64_t release_us = 0;
    CHECK(gpio_event_limiter_offer(&limiter, 1000, 0x1, &release_us));
    CHECK(!gpio_event_limiter_offer(&limiter, 1100, 0x0, &release_us));
    /* A token is free again by 1600, but the held change must go first */
    CHECK(!gpio_event_limiter_offer(&limiter, 1600, 0x1, &release_us));
    CHECK(release_us <= 1600);
}

static void test_a_sustained_burst_is_capped_at_the_rate(void) {
    gpio_event_limiter_t limiter;
    gpio_event_limiter_init(&limiter, 2000, 8);
    uint64_t release_us = 0;
    uint64_t pending_release = 0;
    uint32_t state = 0;
    uint64_t time_us = 0;
    uint8_t flags = 0;
    uint32_t last_sent = 0xFFFFFFFF;
    int sent = 0;
    int coalesced = 0;
    /* A 10 kHz edge train for one second, with the release timer checked every 100 us */
    for (uint64_t now = 0; now < 1000000; now += 100) {
        if (pending_release && now >= pending_release &&
            gpio_event_limiter_release(&limiter, now, &state, &time_us, &flags, &pending_release)) {
            sent++;
            coalesced += flags == JERRYCAN_TIMING_FLAG_COALESCED;
            last_sent = state;
            pending_release = 0;
        }
        const uint32_t level = (uint32_t)((now / 100) & 1);
        if (gpio_event_limiter_offer(&limiter, now, level, &release_us)) {
            sent++;
            last_sent = level;
        } else {
            pending_release = release_us;
        }
    }
    while (gpio_event_limiter_release(&limiter, 2000000, &state, &time_us, &flags, &release_us)) {
        sent++;
        last_sent = state;
    }
    CHECK(sent <= 2000 + 8 + 1);
    CHECK(sent >= 1990);
    CHECK(coalesced > 0);
    CHECK(last_sent == ((999900 / 100) & 1));
}

static void test_time_going_backwards_grants_no_credit(void) {
    gpio_event_limiter_t limiter;
    gpio_event_limiter_init(&limiter, 2000, 1);
    uint64_t release_us = 0;
    CHECK(gpio_event_limiter_offer(&limiter, 5000, 0x1, &release_us));
    CHECK(!gpio_event_limiter_offer(&limiter, 4000, 0x0, &release_us));
    CHECK(release_us == 5500);
}
```

In `test/CMakeLists.txt`:
- add `enable_testing()`
- register the existing `timing_trailer_test` with `add_test`
- add `gpio_event_limiter_test` built from `gpio_event_limiter_test.c ../gpio_event_limiter.c`, with the same include directories, plus its `add_test`
- update the header comment to the ctest command in `HOST_TEST`

- [ ] **Step 2: Run to verify failure.** Sync, then `$RIG "cd ~/Documents/reachAQ-hardware && $HOST_TEST"`. Expected: the build fails because `gpio_event_limiter.c` does not exist.

- [ ] **Step 3: Implement** the token bucket in `gpio_event_limiter.c`.
  - Refill adds `now − last` to the credit, capped at capacity. It only moves `last_us` forward; a time earlier than `last_us` adds nothing.
  - An offer is admitted only when nothing is held and the credit covers one token.
  - Otherwise the offer becomes, or merges into, the held change: latest state, latest time, count + 1.
  - `release_us` is when the credit will cover one token. That is `last_us` if it already does.
  - Include `jerrycan_timing.h` for `JERRYCAN_TIMING_FLAG_COALESCED`.
  - No Zephyr headers.

- [ ] **Step 4: Run to verify pass.** Same command. Expected: `100% tests passed` (2 tests).

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/jerrycan/gpio_event_limiter.c firmware/lib/jerrycan/gpio_event_limiter.h firmware/lib/jerrycan/test/gpio_event_limiter_test.c firmware/lib/jerrycan/test/CMakeLists.txt
git commit -m "feat(jerrycan): rate limiter for GPIO change events

Token bucket (rate, burst) that holds and merges changes past the cap and
releases the latest one, flagged COALESCED when more than one was merged.
Never reorders: a change offered while one is held joins it.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Barcode levels, ANALOG_BLOCK packing, power-output rules (host-tested)

**Files:**
- Create: `firmware/drivers/barcode/barcode_frame.{c,h}`
- Create: `firmware/lib/jerrycan/analog_block.{c,h}`, `firmware/lib/jerrycan/power_out_policy.{c,h}`
- Create: `firmware/lib/jerrycan/test/barcode_frame_test.c`, `analog_block_test.c`, `power_out_policy_test.c`
- Modify: `firmware/lib/jerrycan/test/CMakeLists.txt` (3 executables + `add_test`; `barcode_frame_test` compiles `../../../drivers/barcode/barcode_frame.c` and adds that directory to its includes)

**Interfaces** (produced; Tasks 5-7 consume them). None of these headers includes `jerrycan_types.h`: it pulls in `<cassert>` off-target and cannot be compiled as C on the host.

```c
/* barcode_frame.h */
#define BARCODE_SLOT_US 1000U
#define BARCODE_SLOTS_PER_TICK 1000U
#define BARCODE_BITS 32U
/* Level of the barcode line during `slot`, counted in 1 ms slots from the start of tick 0: slot 0 of each tick is
   the start pulse, slots 1-32 are the tick's bits LSB first, the rest are low. */
bool barcode_slot_level(uint64_t slot);
uint32_t barcode_slot_tick(uint64_t slot); /* (uint32_t)(slot / BARCODE_SLOTS_PER_TICK) */

/* analog_block.h */
#define ANALOG_BLOCK_PAYLOAD_SIZE 38
#define ANALOG_BLOCK_SAMPLE_SLOTS 16
/* Little-endian <BBBBH16H>: instance, channel_count, scan_count, flags (0), scan_period_us, samples scan-major, unused
   slots 0. Returns 0, or -EINVAL when channel_count * scan_count is 0 or exceeds 16. */
int analog_block_pack(uint8_t out[ANALOG_BLOCK_PAYLOAD_SIZE], uint8_t instance, uint8_t channel_count,
                      uint8_t scan_count, uint16_t scan_period_us, const uint16_t *scans);
/* TRGO fires on each timer update, the first one period after the counter starts: scan k at start + (k+1) periods */
uint64_t analog_scan_time_us(uint64_t start_us, uint32_t period_us, uint64_t scan_index);

/* power_out_policy.h */
#define POWER_OUT_DUTY_FULL 10000U
/* 0, or -EINVAL (channel, duty or a non-zero period out of range; checked first), then for a non-zero duty
   -EPERM while estopped and -ETIMEDOUT without a fresh heartbeat. Duty 0 (off) is always allowed. period_us 0 keeps
   the current period. */
int power_out_check(uint8_t channel, uint8_t channel_count, uint16_t duty_permyriad, uint32_t period_us,
                    uint32_t min_period_us, uint32_t max_period_us, bool estopped, bool heartbeat_fresh);
uint32_t power_out_pulse_ns(uint32_t period_us, uint16_t duty_permyriad); /* period * duty / 10000, 64-bit math */
```

- [ ] **Step 1: Write the failing tests** (same `CHECK`/`main` shape as Task 1).

`barcode_frame_test.c`. The edge vectors are the ones reachAQ's T3 decoder derives (`board_clock_chain.barcode_edges`):

```c
/* Edges during one second as (ms after its start edge, rising), collected from the slot levels */
static int edges_of(uint64_t second, unsigned edges[][2], int max) {
    int count = 0;
    bool previous = barcode_slot_level(second * BARCODE_SLOTS_PER_TICK - 1);
    for (unsigned slot = 0; slot < BARCODE_SLOTS_PER_TICK; slot++) {
        const bool level = barcode_slot_level(second * BARCODE_SLOTS_PER_TICK + slot);
        if (level != previous && count < max) {
            edges[count][0] = slot;
            edges[count][1] = level;
            count++;
        }
        previous = level;
    }
    return count;
}

static void check_edges(uint64_t second, const unsigned expected[][2], int expected_count) {
    unsigned edges[40][2];
    const int count = edges_of(second, edges, 40);
    CHECK(count == expected_count);
    CHECK(count == expected_count && memcmp(edges, expected, sizeof(unsigned) * 2 * (size_t)count) == 0);
}

static void test_edges_match_the_host_decoder(void) {
    const unsigned tick_0[][2] = {{0, 1}, {1, 0}};
    const unsigned tick_1[][2] = {{0, 1}, {2, 0}};
    const unsigned tick_80000001[][2] = {{0, 1}, {2, 0}, {32, 1}, {33, 0}};
    const unsigned tick_ffffffff[][2] = {{0, 1}, {33, 0}};
    check_edges(0x100000000ULL, tick_0, 2); /* tick numbers are 32-bit: second 2^32 plays tick 0 */
    check_edges(1, tick_1, 2);
    check_edges(0x80000001ULL, tick_80000001, 4);
    check_edges(0xFFFFFFFFULL, tick_ffffffff, 2);
}

static void test_bits_read_back_as_the_tick(void) {
    const uint32_t ticks[] = {0, 1, 0x80000001u, 0xFFFFFFFFu, 0x12345678u};
    for (size_t i = 0; i < sizeof(ticks) / sizeof(ticks[0]); i++) {
        const uint64_t start = (uint64_t)ticks[i] * BARCODE_SLOTS_PER_TICK;
        uint32_t read = 0;
        for (unsigned bit = 0; bit < BARCODE_BITS; bit++) {
            read |= (uint32_t)barcode_slot_level(start + 1 + bit) << bit;
        }
        CHECK(barcode_slot_level(start));
        CHECK(read == ticks[i]);
        CHECK(!barcode_slot_level(start + 33));
        CHECK(!barcode_slot_level(start + 999));
        CHECK(barcode_slot_tick(start + 999) == ticks[i]);
    }
}
```

`analog_block_test.c`. The expected bytes are what reachAQ unpacks with `<BBBBH16H`:

```c
static void test_two_scans_of_eight_fill_every_slot_scan_major(void) {
    uint16_t scans[16];
    for (int i = 0; i < 16; i++) {
        scans[i] = (uint16_t)(0x0100 * i + 0x0FF0 - i); /* distinct, both bytes non-zero */
    }
    uint8_t out[ANALOG_BLOCK_PAYLOAD_SIZE];
    memset(out, 0xEE, sizeof(out));

    CHECK(analog_block_pack(out, 3, 8, 2, 1000, scans) == 0);

    const uint8_t header[6] = {0x03, 0x08, 0x02, 0x00, 0xE8, 0x03}; /* instance, channels, scans, flags, 1000 us */
    CHECK(memcmp(out, header, sizeof(header)) == 0);
    for (int i = 0; i < 16; i++) {
        CHECK(out[6 + 2 * i] == (uint8_t)(scans[i] & 0xFF));
        CHECK(out[7 + 2 * i] == (uint8_t)(scans[i] >> 8));
    }
}

static void test_unused_slots_are_zero(void) {
    const uint16_t scans[6] = {1, 2, 3, 4, 5, 6};
    uint8_t out[ANALOG_BLOCK_PAYLOAD_SIZE];
    memset(out, 0xEE, sizeof(out));

    CHECK(analog_block_pack(out, 0, 3, 2, 1000, scans) == 0);

    CHECK(out[6 + 2 * 5] == 6);
    for (int i = 6 + 2 * 6; i < ANALOG_BLOCK_PAYLOAD_SIZE; i++) {
        CHECK(out[i] == 0);
    }
}

static void test_blocks_that_do_not_fit_are_refused(void) {
    const uint16_t scans[18] = {0};
    uint8_t out[ANALOG_BLOCK_PAYLOAD_SIZE];
    CHECK(analog_block_pack(out, 0, 9, 2, 1000, scans) == -EINVAL);
    CHECK(analog_block_pack(out, 0, 0, 2, 1000, scans) == -EINVAL);
    CHECK(analog_block_pack(out, 0, 8, 0, 1000, scans) == -EINVAL);
}

static void test_scan_k_is_triggered_k_plus_one_periods_after_start(void) {
    CHECK(analog_scan_time_us(5, 1000, 0) == 1005);
    CHECK(analog_scan_time_us(5, 1000, 3) == 4005);
    CHECK(analog_scan_time_us(0, 1000, 3600000000ULL) == 3600000001000ULL); /* 1000 h of 1 kHz scans */
}
```

`power_out_policy_test.c`:

```c
/* The NUCLEO and PCB limits: TIM1 at 10 MHz with a 16-bit counter */
#define MIN_US 10U
#define MAX_US 6553U

static void test_a_valid_request_is_accepted(void) {
    CHECK(power_out_check(0, 4, 5000, 1000, MIN_US, MAX_US, false, true) == 0);
    CHECK(power_out_check(3, 4, POWER_OUT_DUTY_FULL, 0, MIN_US, MAX_US, false, true) == 0); /* 0 keeps the period */
}

static void test_out_of_range_requests_are_invalid(void) {
    CHECK(power_out_check(4, 4, 5000, 1000, MIN_US, MAX_US, false, true) == -EINVAL);
    CHECK(power_out_check(0, 4, POWER_OUT_DUTY_FULL + 1, 1000, MIN_US, MAX_US, false, true) == -EINVAL);
    CHECK(power_out_check(0, 4, 5000, MIN_US - 1, MIN_US, MAX_US, false, true) == -EINVAL);
    CHECK(power_out_check(0, 4, 5000, MAX_US + 1, MIN_US, MAX_US, false, true) == -EINVAL);
    /* A bad channel is reported as such even while stopped */
    CHECK(power_out_check(4, 4, 5000, 1000, MIN_US, MAX_US, true, false) == -EINVAL);
}

static void test_estop_refuses_any_output_but_allows_off(void) {
    CHECK(power_out_check(0, 4, 1, 0, MIN_US, MAX_US, true, true) == -EPERM);
    CHECK(power_out_check(0, 4, 0, 0, MIN_US, MAX_US, true, true) == 0);
}

static void test_without_a_recent_heartbeat_outputs_stay_off(void) {
    CHECK(power_out_check(0, 4, 1, 0, MIN_US, MAX_US, false, false) == -ETIMEDOUT);
    CHECK(power_out_check(0, 4, 0, 1000, MIN_US, MAX_US, false, false) == 0);
}

static void test_pulse_width_is_the_duty_share_of_the_period(void) {
    CHECK(power_out_pulse_ns(1000, 2500) == 250000);
    CHECK(power_out_pulse_ns(MAX_US, POWER_OUT_DUTY_FULL) == MAX_US * 1000U);
    CHECK(power_out_pulse_ns(1000, 0) == 0);
    CHECK(power_out_pulse_ns(1000, 1) == 100);
}
```

- [ ] **Step 2: Run to verify failure.** `$RIG "cd ~/Documents/reachAQ-hardware && $HOST_TEST"`. Expected: the build fails on the three missing sources.
- [ ] **Step 3: Implement the three files** to the interfaces above. Write the `analog_block_pack` bytes explicitly, little-endian; do not `memcpy` a struct. No Zephyr headers.
- [ ] **Step 4: Run to verify pass.** Expected: `100% tests passed` (5 tests).
- [ ] **Step 5: Commit** the six source files, three tests and CMake, as `feat(io): host-tested barcode levels, ANALOG_BLOCK packing and power-output rules`.

---

### Task 3: The IO-module app on the NUCLEO, with its address from devicetree

**Files:**
- Modify: `firmware/lib/jerrycan/jerrycan.c`, `firmware/lib/jerrycan/jerrycan.h`
- Create: `firmware/io_module/CMakeLists.txt`, `Kconfig`, `prj.conf`, `src/main.c`, `boards/nucleo_g474re.overlay`, `README.md`

**Interfaces:**
- Produces: `uint8_t jerrycan_node_id(void)`. It returns this board's 5-bit node id and is valid after `jerrycan_init`. Tasks 5 and 10 use it.

- [ ] **Step 1: Core changes in `jerrycan.c`:**
  1. **Board type from devicetree.** In `get_can_node_id()`, when `DT_NODE_HAS_PROP(DT_PATH(zephyr_user), jerrycan_board_type)`, take the type from that property instead of the `0xC` strap read. `BUILD_ASSERT` that it is ≤ 3. Keep the node-number read (mask `0x3`) and the `LOG_INF` unchanged. Without the property the code is byte-identical, which keeps the pellet and magnet unchanged.
  2. **`jerrycan_node_id()`.** Return `can_node_id`. Declare it in `jerrycan.h` next to `jerrycan_boot_id()`.
  3. **`confirm_image()`.** Wrap the declaration and the call in `jerrycan_run()` in `#if CONFIG_BOOTLOADER_MCUBOOT`. `bootloader.c`, which defines it, is only built with MCUboot.

- [ ] **Step 2: The app.**
  - **`CMakeLists.txt`, `Kconfig` and `src/main.c`:** mirror `firmware/pellet_module/`. Use project `io_module_firmware`, mainmenu `Autotrainer IO Module Firmware`, and log `Autotrainer IO Module v%s`.
  - **No `sysbuild.conf` and no key:** see decision 3.
  - **`prj.conf`:**

```
CONFIG_LOG=y
CONFIG_SHELL=y
CONFIG_CONSOLE=y
CONFIG_CAN=y
CONFIG_CAN_FD_MODE=y
CONFIG_LIB_JERRYCAN=y
CONFIG_LIB_JERRYCAN_TIMING=y
# TIM16 counter, used to time the JERRYCAN_CMD_GPIO_PULSE return-low edge
CONFIG_COUNTER=y
# Hardware source for the boot id
CONFIG_ENTROPY_GENERATOR=y
# CAN I/O runs in main(); interrupt paths and these threads queue 96 B TX items
CONFIG_MAIN_THREAD_PRIORITY=-2
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048
```

`boards/nucleo_g474re.overlay`. The pins differ from spec §6's PCB map wherever the NUCLEO already uses a pin:
- LPUART1 VCP on PA2/PA3
- LD2 on PA5
- B1 on PC13
- HSE on PF0/PF1
- LSE on PC14/PC15

```dts
/ {
	zephyr,user {
		jerrycan-board-type = <2>; /* IO module: addresses 8-11 */
	};

	chosen {
		ll,gpio-pulse-counter = &ttl_pulse_counter;
	};

	generic-gpios {
		compatible = "ll,generic-gpios";
		status = "okay";
		/* Readable index: 0-1 CAN_ID, 2-13 DI0-DI11 (12 distinct EXTI lines), 14-21 TTL0-TTL7 */
		input-gpios = <&gpiob 6 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>,  /* CAN_ID[0]: open reads 0 -> address 8 */
			      <&gpiob 7 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>,  /* CAN_ID[1] */
			      <&gpiob 2 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>,  /* DI0  EXTI2 */
			      <&gpioc 4 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>,  /* DI1  EXTI4 */
			      <&gpioc 5 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>,  /* DI2  EXTI5 */
			      <&gpioa 6 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>,  /* DI3  EXTI6 */
			      <&gpioa 7 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>,  /* DI4  EXTI7 */
			      <&gpioc 8 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>,  /* DI5  EXTI8 */
			      <&gpioc 9 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>,  /* DI6  EXTI9 */
			      <&gpiob 10 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>, /* DI7  EXTI10 */
			      <&gpioc 11 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>, /* DI8  EXTI11 */
			      <&gpioc 12 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>, /* DI9  EXTI12 */
			      <&gpiob 13 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>, /* DI10 EXTI13 */
			      <&gpioa 15 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>; /* DI11 EXTI15 */
		input-gpio-names = "CAN_ID[0]", "CAN_ID[1]", "DI0", "DI1", "DI2", "DI3", "DI4", "DI5",
				   "DI6", "DI7", "DI8", "DI9", "DI10", "DI11";
		input-gpio-interrupt-enable = "DISABLE", "DISABLE", "BOTH", "BOTH", "BOTH", "BOTH", "BOTH",
					      "BOTH", "BOTH", "BOTH", "BOTH", "BOTH", "BOTH", "BOTH";
		output-gpios = <&gpiob 14 GPIO_ACTIVE_HIGH>, <&gpiob 15 GPIO_ACTIVE_HIGH>,
			       <&gpioc 6 GPIO_ACTIVE_HIGH>, <&gpioc 7 GPIO_ACTIVE_HIGH>,
			       <&gpioa 10 GPIO_ACTIVE_HIGH>, <&gpiod 2 GPIO_ACTIVE_HIGH>,
			       <&gpiob 5 GPIO_ACTIVE_HIGH>, <&gpiob 9 GPIO_ACTIVE_HIGH>;
		output-gpio-names = "TTL0", "TTL1", "TTL2", "TTL3", "TTL4", "TTL5", "TTL6", "TTL7";
		output-gpio-interrupt-enable = "DISABLE", "DISABLE", "DISABLE", "DISABLE",
					       "DISABLE", "DISABLE", "DISABLE", "DISABLE";
		pulse-enable-gpio-names = "TTL0", "TTL1", "TTL2", "TTL3", "TTL4", "TTL5", "TTL6", "TTL7";
	};

	/* B1 is not an IO-module input; disabled, door_sensor.c is not built */
	gpio_keys {
		status = "disabled";
	};

	/* TIM2 PWM on PA5 would fight LD2, which is the heartbeat LED */
	pwmleds {
		status = "disabled";
	};
};

/* 170 MHz PCLK, as on the pellet: 5 Mbit/s data needs a whole number of time quanta, and the 24 MHz HSE gives 4.8 */
&fdcan1 {
	clocks = <&rcc STM32_CLOCK_BUS_APB1 0x02000000>,
		 <&rcc STM32_SRC_PCLK FDCAN_SEL(1)>;
};

&timers2 {
	status = "disabled";
};

/*
 * A disabled timer does not disable its pwm child: drivers instantiate per node status, so with CONFIG_PWM=y
 * (Task 7) pwm2 would take PA5 (LD2) and pwm3 would initialise TIM3, which the barcode driver owns (Task 5).
 */
&pwm2 {
	status = "disabled";
};

&pwm3 {
	status = "disabled";
};

/*
 * The NUCLEO enables these for its Arduino/Morpho headers. Their drivers apply pinctrl after the GPIO driver has
 * configured the IO pins and would take them: usart1 PC4/PC5 (DI1, DI2), i2c1 PB8/PB9 (TTL7), spi1 PA5-PA7 and
 * cs PB6 (DI3, DI4, CAN_ID[0]), spi2 PB12-PB15 (an ADC input, DI10, TTL0, TTL1), spi3 PA15/PC10-PC12 (DI11, DI8, DI9).
 */
&usart1 {
	status = "disabled";
};

&i2c1 {
	status = "disabled";
};

&spi1 {
	status = "disabled";
};

&spi2 {
	status = "disabled";
};

&spi3 {
	status = "disabled";
};

&timers16 {
	status = "okay";
	st,prescaler = <169>; /* 1 MHz */

	ttl_pulse_counter: counter {
		status = "okay";
	};
};
```

`README.md`: what the app is, the build and flash commands, the GPIO index map (decision 8), and a pin table with the NUCLEO pin next to spec §6's PCB pin for every signal. Later tasks each add one section.

- [ ] **Step 3: Build all three.**

```bash
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $BUILD_IO 2>&1 | grep -iE 'warning|error' | head; $SIZE /tmp/h1-io/zephyr/zephyr.elf"
$RIG "cd ~/Documents/reachAQ-hardware && grep -E 'jerrycan_board_type|DT_N_S_zephyr_user_P_jerrycan_board_type ' /tmp/h1-io/zephyr/include/generated/zephyr/devicetree_generated.h | head -3; grep -E '^CONFIG_(LIB_JERRYCAN_TIMING|BOOTLOADER_MCUBOOT|LIB_MOTOR_MOTION|LL_TONE_GENERATOR|SPI|I2C)=' /tmp/h1-io/zephyr/.config"
# Every enabled node that claims pins through pinctrl, with the pins it claims
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && python -c \"import pickle, sys; sys.path.insert(0, 'deps/zephyr/scripts/dts/python-devicetree/src'); edt = pickle.load(open('/tmp/h1-io/zephyr/edt.pickle', 'rb')); [print(n.path, [c.name for p in n.pinctrls for c in p.conf_nodes]) for n in edt.nodes if n.status == 'okay' and n.pinctrls]\""
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $BUILD_PELLET 2>&1 | tail -1 && $SIZE /tmp/h1-pellet/pellet_module/zephyr/zephyr.elf"
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $BUILD_MAGNET 2>&1 | grep -iE 'error|FAILED' | sort -u | diff - /tmp/h1-magnet-baseline.txt && echo 'magnet: same as baseline'"
```

Expected:
- **IO:** builds with no warnings. The board type is `2`, `LIB_JERRYCAN_TIMING=y`, and there is no MCUboot, motor, tone, SPI or I2C code.
- **Pin claims:** the listing shows `lpuart1` (PA2/PA3) and `fdcan1` (PA11/PA12), plus the board's `adc1` (PA0) and `dac1` (PA4) until Tasks 6 and 5 disable them. No `pwm` node may appear. Tasks 5-7 add `barcode` (PA4), `analog_inputs` (the 8 ADC pins) and `pwm` (PA8/PA9/PC2/PC3); each of those tasks re-runs this listing. No claimed pin may be a generic-gpios pin. If the `edt.pickle` API differs in this Zephyr, find the equivalent with `grep -B2 -A8 'pinctrl-0' /tmp/h1-io/zephyr/zephyr.dts` and say so in the report.
- **Pellet:** within the size budget.
- **Magnet:** same as baseline.

- [ ] **Step 4: Commit** as `feat(io): IO-module app on the NUCLEO-G474RE, board type from devicetree`.

---

### Task 4: DI change events stamped OBSERVED and rate-capped

**Files:**
- Modify: `firmware/lib/jerrycan/Kconfig`, `firmware/lib/jerrycan/modules/gpio.c`, `firmware/lib/jerrycan/CMakeLists.txt`
- Modify: `firmware/io_module/prj.conf`, `firmware/io_module/README.md`

**Interfaces:** consumes Task 1's limiter.

- [ ] **Step 1: Kconfig** (in `lib/jerrycan/Kconfig`, after `LIB_JERRYCAN_GPIO_TX_PERIOD_MS`):

```kconfig
config LIB_JERRYCAN_GPIO_EVENT_RATE_HZ
    int "Cap on GPIO change messages per second (0 = no cap)"
    default 0
    range 0 100000
    depends on LIB_JERRYCAN
    help
      Interrupt-driven GPIO_READ messages beyond this rate are held and merged:
      the latest state goes out when the cap allows, flagged COALESCED in the
      timing trailer if changes were merged. 0 sends every change at once, as
      before; boards opt in from prj.conf.

config LIB_JERRYCAN_GPIO_EVENT_BURST
    int "Changes allowed back to back before the cap applies"
    default 8
    range 1 64
    depends on LIB_JERRYCAN_GPIO_EVENT_RATE_HZ > 0
```

Add `gpio_event_limiter.c` to `zephyr_library_sources` in `lib/jerrycan/CMakeLists.txt`.

- [ ] **Step 2: `gpio.c`.**
  - Register a new `jerrycan_generic_gpio_changed()` as the state-change handler, replacing `jerrycan_generic_gpio_tx`. It runs in the GPIO ISR.
  - Its first statement stamps `jerrycan_board_time_us()`. It then reads each instance's state with the same loop as `jerrycan_generic_gpio_tx()`; factor that loop into one helper used by both.
  - **With `CONFIG_LIB_JERRYCAN_GPIO_EVENT_RATE_HZ == 0`:** send at once, `OBSERVED` at the stamp.
  - **Otherwise:** offer the change to a per-instance `gpio_event_limiter_t`, under a `k_spinlock`. If admitted, send `OBSERVED` with flags 0. If held, (re)start a one-shot `k_timer` for `release_us − now`, at least one tick. Its expiry calls `gpio_event_limiter_release()`, sends `OBSERVED` at the merged time with the returned flags, and re-arms while something is still held.
  - The periodic timer and the pulse paths keep calling `jerrycan_generic_gpio_tx()` (`SAMPLED`) unchanged.
  - Update the file header's "any state change on GPIOs triggers an immediate transmission" to describe the cap.
  - **Why the other boards are unaffected:**
    - The pellet enables no input interrupt, so the handler never runs there.
    - The magnet builds without `LIB_JERRYCAN_TIMING`, so it has no trailer and its frames stay identical.

- [ ] **Step 3: Configure the IO module.** Add `CONFIG_LIB_JERRYCAN_GPIO_EVENT_RATE_HZ=2000` to `io_module/prj.conf`, and a "Digital inputs" section to the README.

- [ ] **Step 4: Build all three** (Task 3 Step 3's commands) and `$HOST_TEST`. Expected: as in Task 3, plus `grep CONFIG_LIB_JERRYCAN_GPIO_EVENT_RATE_HZ=2000 /tmp/h1-io/zephyr/.config`, and the pellet's `.config` has `=0`.

- [ ] **Step 5: Commit** as `feat(jerrycan): stamp GPIO change events OBSERVED and cap their rate`.

---

### Task 5: Timekeeper — barcode driver, clap, and their reports

**Files:**
- Create: `firmware/dts/bindings/barcode/ll,barcode-generator.yaml`
- Create: `firmware/drivers/barcode/{CMakeLists.txt,Kconfig,barcode.h,barcode.c}`
- Modify: `firmware/drivers/CMakeLists.txt`, `firmware/drivers/Kconfig`
- Modify: `firmware/lib/jerrycan/Kconfig`, `firmware/lib/jerrycan/CMakeLists.txt`, `firmware/lib/jerrycan/jerrycan.{c,h}`, `firmware/lib/jerrycan/modules/capabilities.c`
- Create: `firmware/lib/jerrycan/modules/timekeeper.c`
- Modify: `firmware/io_module/prj.conf`, `firmware/io_module/boards/nucleo_g474re.overlay`, `firmware/io_module/README.md`

**Interfaces:**
- Consumes `barcode_slot_level()`, `barcode_slot_tick()` and `BARCODE_*` (Task 2), and `jerrycan_node_id()` (Task 3).
- Produces:

```c
/* barcode.h */
#define BARCODE_CALLBACK_SLOT 500U /* ms into each tick */
/* Runs in the timer interrupt once per tick, BARCODE_CALLBACK_SLOT ms after that tick's start edge.
   start_us is the start edge on the board clock (k_cyc_to_us_floor64 of k_cycle_get_64). */
typedef void (*barcode_tick_cb_t)(const struct device *dev, uint32_t tick, uint64_t start_us, void *user_data);
/* Tick 0's start edge is now. Returns -EALREADY if already started. */
int barcode_start(const struct device *dev, barcode_tick_cb_t cb, void *user_data);

/* jerrycan.h, under CONFIG_LIB_JERRYCAN_TIMEKEEPER */
/* Runs in the CAN TX-complete interrupt for every SYNC_CLAP this board sends; sent_time_us is stamped first thing in
   that interrupt. error is the can_send callback's error (0 = on the wire). */
typedef void (*jerrycan_clap_sent_t)(uint32_t tick, uint64_t sent_time_us, int error);
void jerrycan_set_clap_sent_handler(jerrycan_clap_sent_t handler);
/* True when this board claps and plays the barcode */
bool jerrycan_timekeeper_active(void);
```

- [ ] **Step 1: The binding and driver** (pattern: `drivers/tone_generator/`, `dts/bindings/tone_generator/`).
  - **Binding properties:**
    - `timer` (phandle, required)
    - `channel` (int, required, enum 1-4)
    - `pinctrl-0` / `pinctrl-names`, via `include: [base.yaml, pinctrl-device.yaml]`
  - **Kconfig:** `LL_BARCODE_GENERATOR`, default y, depends on `DT_HAS_LL_BARCODE_GENERATOR_ENABLED`, selects `USE_STM32_LL_TIM`. Add `LL_BARCODE_GENERATOR_LOG_LEVEL`. CMake builds `barcode.c` and `barcode_frame.c`.
  - **Timer setup:** the driver owns the timer through LL; the overlay disables the Zephyr node. Enable its clock from the node's `clocks`. Set PSC 169 (1 MHz) and ARR 999 (1 ms). Use PWM mode 1 on `channel` with CCR preload, ARR preload and the output enabled. CCR = 1000 holds the line high for a slot, and 0 holds it low. Apply the pinctrl.
  - **Start sequence** (makes slot k begin exactly at update k):
    1. Write CCR = level(slot 0) and generate UG with the update interrupt still disabled. This copies the value to active and zeroes CNT and the prescaler.
    2. Write the preload = level(slot 1).
    3. Under `irq_lock`, read `k_cycle_get_64()` and set CEN immediately after.
    4. `start_us = k_cyc_to_us_floor64(cycles)`.
    5. Enable the update interrupt (`IRQ_CONNECT` to the timer node's `global` IRQ, priority 0).
  - **Update ISR:**
    1. Clear UIF.
    2. `slot++`. The slot now beginning is `slot`.
    3. Write the preload = level(`slot + 1`).
    4. If `slot % BARCODE_SLOTS_PER_TICK == BARCODE_CALLBACK_SLOT`, call `cb(dev, barcode_slot_tick(slot), start_us + (slot / BARCODE_SLOTS_PER_TICK) × 1 000 000, user_data)`. Use 64-bit arithmetic: the start time keeps counting after the 32-bit tick number wraps.
  - **Rule:** nothing else touches the timer after start.

- [ ] **Step 2: Clap TX-done hook in `jerrycan.c`**, under `#if CONFIG_LIB_JERRYCAN_TIMEKEEPER`.
  - In `jerrycan_run`'s TX loop, a frame whose `msg.type == JERRYCAN_CMD_SYNC_CLAP` goes out with `can_send(can_dev, &frame, K_FOREVER, jerrycan_clap_tx_done, UINT_TO_POINTER(msg.sync_clap.tick))`.
  - `jerrycan_clap_tx_done` stamps `jerrycan_board_time_us()` first. It then calls the registered handler if one is set, and logs a non-zero error once.
  - Other frames keep the existing synchronous `can_send(..., NULL, NULL)`.

- [ ] **Step 3: Kconfig** (lib):

```kconfig
config LIB_JERRYCAN_TIMEKEEPER
    bool "Act as the bus timekeeper: SYNC_CLAP and the NI-DAQ barcode"
    default n
    depends on LIB_JERRYCAN_TIMING
    depends on DT_HAS_LL_BARCODE_GENERATOR_ENABLED
    help
      Once a second, play barcode N on the ll,barcode-generator output, report it
      as SYNC_BARCODE (PHYSICAL_START at its start edge), send SYNC_CLAP N, and
      report this board's own SYNC_CLAP_OBSERVED, stamped when the clap left.
      Only the board whose node id is LIB_JERRYCAN_TIMEKEEPER_NODE_ID does this,
      so a bus with several such boards still has one timekeeper.

config LIB_JERRYCAN_TIMEKEEPER_NODE_ID
    int "Node id that acts as timekeeper"
    default 8
    range 0 31
    depends on LIB_JERRYCAN_TIMEKEEPER
```

CMake: `zephyr_library_sources_ifdef(CONFIG_LIB_JERRYCAN_TIMEKEEPER modules/timekeeper.c)`.

- [ ] **Step 4: `modules/timekeeper.c`.**
  - **Init** (`SYS_INIT` at `CONFIG_LIB_JERRYCAN_INIT_PRIORITY`, which runs after `jerrycan_init`):
    - If `jerrycan_node_id() != CONFIG_LIB_JERRYCAN_TIMEKEEPER_NODE_ID`, log that this board only listens and return 0.
    - Otherwise set the clap-sent handler, then `barcode_start()` the `ll,barcode-generator` device. Use `DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(ll_barcode_generator))`.
    - The role is active only if both succeed.
  - **Tick callback** (timer ISR, `K_NO_WAIT`):
    1. `SYNC_BARCODE{tick}` via `jerrycan_tx_timed(..., PHYSICAL_START, 0, start_us, ...)`.
    2. `SYNC_CLAP{tick}` via `jerrycan_tx()`, which gives a `TX_QUEUE` trailer.
  - **Clap-sent handler:** on error 0, `SYNC_CLAP_OBSERVED{tick, clapper_node = jerrycan_node_id()}` via `jerrycan_tx_timed(..., OBSERVED, 0, sent_time_us, ...)`.
  - **No acknowledgements:** nothing here is a command.

- [ ] **Step 5: `capabilities.c`.** Under `CONFIG_LIB_JERRYCAN_TIMEKEEPER`, OR `JERRYCAN_CAPABILITY_TIMEKEEPER` into the response when `jerrycan_timekeeper_active()`. That bit makes the value runtime rather than the constant `BOARD_CAPABILITIES`; keep the rest constant. Extend the header comment's timing-bit paragraph.

- [ ] **Step 6: Configure the IO module.**
  - `prj.conf`: `CONFIG_LIB_JERRYCAN_TIMEKEEPER=y`.
  - Overlay:

```dts
/ {
	barcode: barcode {
		compatible = "ll,barcode-generator";
		status = "okay";
		timer = <&timers3>;
		channel = <2>;
		pinctrl-0 = <&tim3_ch2_pa4>; /* -> NI-DAQ port0/line3 */
		pinctrl-names = "default";
	};
};

&dac1 {
	status = "disabled"; /* PA4 is the barcode */
};

&timers3 {
	status = "disabled"; /* owned by ll,barcode-generator through LL; its pwm3 child is disabled in Task 3 */
};
```

  - README: add a "Timekeeper" section.

- [ ] **Step 7: Build all three and run host tests.**
  - Expected: as before.
  - `nm /tmp/h1-io/zephyr/zephyr.elf | grep -E 'jerrycan_clap_tx_done|jerrycan_timekeeper_active'` shows both symbols.
  - The same `nm` on the pellet shows neither.
  - Pellet sizes stay within budget.

- [ ] **Step 8: Commit** as `feat(io): timekeeper — barcode on TIM3, SYNC_CLAP with its own sent-time report`.

---

### Task 6: Analog scanner and ANALOG_BLOCK

**Files:**
- Create: `firmware/dts/bindings/analog_scanner/ll,analog-scanner.yaml`
- Create: `firmware/drivers/analog_scanner/{CMakeLists.txt,Kconfig,analog_scanner.h,analog_scanner.c}`
- Modify: `firmware/drivers/CMakeLists.txt`, `firmware/drivers/Kconfig`
- Create: `firmware/lib/jerrycan/modules/analog_stream.c`
- Modify: `firmware/lib/jerrycan/CMakeLists.txt`
- Modify: `firmware/io_module/boards/nucleo_g474re.overlay`, `firmware/io_module/README.md`

**Interfaces:**
- Consumes `analog_block_pack()` and `analog_scan_time_us()` (Task 2).
- Produces:

```c
/* analog_scanner.h */
/* Runs in the DMA interrupt with 2 complete scans, scan-major, channel order as in adc-channels.
   first_scan counts scans from start; start_us is the board time the trigger timer started. */
typedef void (*analog_scanner_cb_t)(const struct device *dev, const uint16_t *scans, uint8_t channel_count,
                                    uint8_t scan_count, uint64_t first_scan, uint64_t start_us, void *user_data);
int analog_scanner_start(const struct device *dev, analog_scanner_cb_t cb, void *user_data);
uint32_t analog_scanner_period_us(const struct device *dev);
```

- [ ] **Step 1: The binding.**
  - **Properties:**
    - `adc` (phandle, required)
    - `trigger-timer` (phandle, required)
    - `adc-channels` (array, required, 1-8 entries)
    - `scan-rate-hz` (int, required)
    - `dmas` / `dma-names` (`"rx"`)
    - pinctrl
  - **Kconfig:** `LL_ANALOG_SCANNER`, default y, depends on `DT_HAS_LL_ANALOG_SCANNER_ENABLED`, selects `USE_STM32_LL_ADC`, `USE_STM32_LL_TIM` and `DMA`.

- [ ] **Step 2: The driver.** It owns the ADC and timer through LL; the overlay disables the Zephyr `adc1` node.
  - **ADC:**
    - Enable the ADC and timer clocks from their nodes' `clocks`.
    - ADC12 common clock: synchronous PCLK/4 (42.5 MHz). Run the voltage regulator start-up and single-ended calibration.
    - Regular sequence of the `adc-channels`, each sampled for 47.5 cycles: (47.5 + 12.5) / 42.5 MHz ≈ 1.4 µs, or ≈ 11 µs for 8 channels.
    - External trigger: TIM6 TRGO, rising edge.
    - DMA: circular, 12-bit right-aligned.
  - **DMA:** Zephyr DMA API (as `tone_generator.c` does), peripheral-to-memory from the ADC data register. Use a static buffer of 4 scans, with half-transfer and transfer-complete callbacks. Each callback hands over 2 scans with `first_scan` advanced by 2.
  - **TIM6:** PSC 169 (1 MHz), ARR `1 000 000 / scan-rate-hz − 1`, TRGO on update.
  - **Start sequence:**
    1. Start the ADC regular conversions, so it waits for triggers.
    2. Generate UG on TIM6.
    3. Under `irq_lock`, read `k_cycle_get_64()` and set CEN immediately after.
    4. `start_us = k_cyc_to_us_floor64(cycles)`.
  - **Overrun:** an ADC overrun or DMA error stops the scanner and logs an error. H1 has no restart.
  - **Check:** `BUILD_ASSERT` that `1 000 000 % scan-rate-hz == 0`.

- [ ] **Step 3: `modules/analog_stream.c`.**
  - Build it with `zephyr_library_sources_ifdef(CONFIG_DT_HAS_LL_ANALOG_SCANNER_ENABLED ...)`.
  - **Init:** `analog_scanner_start()` on the `ll,analog-scanner` instance.
  - **Callback:**
    1. `analog_block_pack(msg.payload, 0, channel_count, scan_count, period, scans)`.
    2. `jerrycan_tx_timed(&msg, SAMPLED, 0, analog_scan_time_us(start_us, period, first_scan), K_NO_WAIT)`.
  - `BUILD_ASSERT(sizeof(jerrycan_cmd_analog_block_t) == ANALOG_BLOCK_PAYLOAD_SIZE)`.

- [ ] **Step 4: Overlay and README.**

```dts
/ {
	analog_inputs: analog_inputs {
		compatible = "ll,analog-scanner";
		status = "okay";
		adc = <&adc1>;
		trigger-timer = <&timers6>;
		adc-channels = <1 2 6 7 15 12 14 11>; /* PA0 PA1 PC0 PC1 PB0 PB1 PB11 PB12 */
		scan-rate-hz = <1000>;
		dmas = <&dmamux1 0 5 STM32_DMA_PRIORITY_HIGH>; /* DMAMUX request 5 = ADC1 */
		dma-names = "rx";
		pinctrl-0 = <&adc1_in1_pa0 &adc1_in2_pa1 &adc1_in6_pc0 &adc1_in7_pc1
			     &adc1_in15_pb0 &adc1_in12_pb1 &adc1_in14_pb11 &adc1_in11_pb12>;
		pinctrl-names = "default";
	};
};

&adc1 {
	status = "disabled"; /* owned by ll,analog-scanner through LL */
};

&dma1 {
	status = "okay";
};

&dmamux1 {
	status = "okay";
};
```

  - Confirm each channel number against its `adc1_inN_pXX` pinctrl label; the build fails on a missing label.
  - Confirm DMAMUX request 5 = ADC1 against RM0440's DMAMUX table. The pellet's `DAC1_CH1 = 6` comes from the same table.
  - README: add an "Analog inputs" section (counts → volts per spec §4.2).

- [ ] **Step 5: Build all three and run host tests.** Expected: as before, and `nm` shows `analog_scanner_start` in the IO image only.

- [ ] **Step 6: Commit** as `feat(io): 1 kHz ADC1 scans on a TIM6 trigger, streamed as ANALOG_BLOCK`.

---

### Task 7: Power outputs — PWM_SET, ESTOP, heartbeat watchdog

**Files:**
- Create: `firmware/dts/bindings/power_outputs/ll,power-outputs.yaml`
- Create: `firmware/lib/jerrycan/modules/power_out.c`
- Modify: `firmware/lib/jerrycan/CMakeLists.txt` (adds `power_out_policy.c` and, `_ifdef(CONFIG_DT_HAS_LL_POWER_OUTPUTS_ENABLED ...)`, `modules/power_out.c`)
- Modify: `firmware/lib/jerrycan/Kconfig`
- Modify: `firmware/io_module/prj.conf`, `firmware/io_module/boards/nucleo_g474re.overlay`, `firmware/io_module/README.md`

**Interfaces:** consumes `power_out_check()` and `power_out_pulse_ns()` (Task 2).

- [ ] **Step 1: Binding `ll,power-outputs`:** `pwms` (phandle-array, required, 1-4 entries). The period cell of entry 0 is the default period. Kconfig in lib:

```kconfig
config LIB_JERRYCAN_POWER_OUT_HEARTBEAT_TIMEOUT_MS
    int "Power outputs switch off this long after the last HEARTBEAT"
    default 2000
    depends on LIB_JERRYCAN
```

- [ ] **Step 2: `modules/power_out.c`.** It uses the Zephyr PWM API (`pwm_set_dt` on each `PWM_DT_SPEC_GET_BY_IDX`).
  - **State:** `period_us` (from DT), `duty[4]`, `estopped`, and `heartbeat_fresh`. It is false at boot, true on each `HEARTBEAT`, and false when a one-shot `k_timer` of the timeout expires.
  - **Limits for this board:** `min_period_us` 10. `max_period_us` is ARR 65535 at the timer's prescaler: with `st,prescaler = <16>`, (16 + 1) × 65536 / 170 → 6553 µs. Derive it from the timer node's `st,prescaler` and `CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC` in a macro.
  - **Init:** every output at duty 0.
  - **`PWM_SET` handler:**
    1. `power_out_check(...)`; return its error, which becomes the ACK.
    2. If `period_us != 0`, store it and re-apply **all** channels from their stored duties, because the channels share one timer.
    3. Apply this channel. Return 0.
  - **`ESTOP` handler** (`estop.rsvd != 0` asserts):
    - On assert: set every duty to 0, both stored and applied, and set `estopped`.
    - On release: clear `estopped`. Outputs stay off until commanded.
    - Return `SEND_NO_ACKNOWLEDGEMENT`: no board answered ESTOP before, so no host waits for an ACK.
  - **`HEARTBEAT` handler:** registered alongside `heartbeat.c`'s, since RX callbacks fan out. It sets `heartbeat_fresh`, restarts the timer, and returns `SEND_NO_ACKNOWLEDGEMENT`.
  - **Timer expiry:** submit a work item (`pwm_set` is not documented ISR-safe) that sets every duty to 0 and clears `heartbeat_fresh`.

- [ ] **Step 3: Configure the IO module.**
  - `prj.conf`: `CONFIG_PWM=y`.
  - Overlay:

```dts
/ {
	power_outputs: power_outputs {
		compatible = "ll,power-outputs";
		status = "okay";
		pwms = <&pwm1 1 PWM_USEC(1000) PWM_POLARITY_NORMAL>, <&pwm1 2 PWM_USEC(1000) PWM_POLARITY_NORMAL>,
		       <&pwm1 3 PWM_USEC(1000) PWM_POLARITY_NORMAL>, <&pwm1 4 PWM_USEC(1000) PWM_POLARITY_NORMAL>;
	};
};

&timers1 {
	status = "okay";
	st,prescaler = <16>; /* 10 MHz: periods 10-6553 us */

	pwm1: pwm {
		status = "okay";
		pinctrl-0 = <&tim1_ch1_pa8 &tim1_ch2_pa9 &tim1_ch3_pc2 &tim1_ch4_pc3>;
		pinctrl-names = "default";
	};
};
```

  - README: add a "Power outputs" section. It must state decision 5's host requirement prominently.

- [ ] **Step 4: Build all three and run host tests.** Expected: as before, and `CONFIG_PWM=y` appears in the IO `.config` only.

- [ ] **Step 5: Commit** as `feat(io): PWM_SET power outputs, off on ESTOP and 2 s after the last heartbeat`.

---

### Task 8: Hardware-free milestone check

**Files:** `firmware/io_module/README.md` only, if anything is out of date.

- [ ] **Step 1: Full verification on the committed state.**
  1. Sync.
  2. `$HOST_TEST`. Expected: every test passes. Do not pin the count: it is whatever the head has (8/8 at bb8a13a, more after the final fix). Record it.
  3. Build all three.
  4. Run `$SIZE` on the IO and pellet ELFs. Expected for the pellet: the head's figure minus `216064/37093/42888` is at most `512/16/16`, per the ruled exception in Global Constraints (at bb8a13a: `216284/37093/42888`; the final fix reports the figure for the head).
  5. Run the magnet diff.
  6. Run `grep -o BOTH /tmp/h1-io/zephyr/zephyr.dts | wc -l` (expected: 12 DI). `grep -c` counts lines and prints 1.
  7. Run `grep -E '^CONFIG_(LIB_JERRYCAN_(TIMING|TIMEKEEPER|GPIO_EVENT_RATE_HZ)|WATCHDOG)=' /tmp/h1-io/zephyr/.config`. Expected: `TIMING=y`, `TIMEKEEPER=y`, `GPIO_EVENT_RATE_HZ=2000` and `CONFIG_WATCHDOG=y`.
  8. Run `grep '^CONFIG_WATCHDOG=' /tmp/h1-pellet/pellet_module/zephyr/.config /tmp/h1-magnet/magnet_module/zephyr/.config`. Expected: no output, because F5 is IO-module only. If a line appears, report it as a finding.

  Record every result in the task report.
- [ ] **Step 2: Restore the rig checkout.** Expected `08c1b41 0`.

```bash
$RIG "cd ~/Documents/reachAQ-hardware && git switch --detach 08c1b41 && git rev-parse --short HEAD && git status --short | wc -l"
```

- [ ] **Step 3:** The whole-branch review runs here (SDD final review, scope `af4ede9..feat/io-module-firmware`: `feat/pellet-board-timing` has moved on and carries cherry-picks of the board-clock commits, so its tip no longer marks where this branch started). Then **stop**. Tasks 9-11 wait for the hardware and the user's go-ahead.

---

## Hardware phase

Each task starts by asking the user, in that session, for:
- the NUCLEO-G474RE and a CAN transceiver on PA11/PA12, wired to the rig's CAN bus. It must be rated for CAN FD at 5 Mbit/s (the bus's data phase) and take 3.3 V logic. Classic 1 Mbit/s parts such as the SN65HVD230 are not enough;
- confirmation that reachAQ is closed (unless T1 is deployed) and the pellet is left as it is.

**Flashing:**
- The ELF comes from `/tmp/h1-io/zephyr/zephyr.hex` on the rig.
- The user flashes it with ST-Link from the machine the NUCLEO is plugged into, or approves `west flash` there.
- Rebuild with Task 8's commands first, and restore the rig checkout at the end of the session.

### Before Task 9: the SWD machine, SWD hygiene, joining the bus, the instrumentation build

Settle these with the user at the start of the hardware session, and record each answer in Task 9's report.

**Flash and SWD machine (undecided).**
- The rig has no usable SWD tool for the G474: pyOCD lacks the target pack, and OpenOCD and STM32CubeProgrammer are not on `PATH`. The Flashing note above cannot be followed on the rig as it stands.
- Decide which machine the NUCLEO's ST-LINK USB is plugged into, and which tool runs there. The tool must flash `zephyr.hex`, read and write registers while the core runs, halt, single-step and resume, and reset the board. Tasks 9-11 use all five.
- The ST-LINK's virtual COM port is the console, so the boot log and `kernel stacks` come from that machine. The checker and the CAN capture run on the rig.
- The rig is also the pellet's flash rig. Select the NUCLEO's probe by its serial number, and never flash the rig's pellet board.

**Reset after every SWD session, before any timing check.**
- pyOCD writes `DEMCR` = 0 when it closes a session, which clears `TRCENA`. `CYCCNT` then freezes for up to 10 s, until the board clock's keep-alive restores it.
- In that window the analog stream can go `LOST` and stay stopped, and the barcode phase can shift. A read-only register check would otherwise look like a firmware failure.
- After the last SWD session of a sequence (halts and single-steps included), reset the board and confirm a new `boot_id` before the next timing check.
- Task 9 Step 6 is the one place the keep-alive is tested on purpose, without the reset.

**Before joining the live bus.**
- The module transmits from boot and has no quiet mode. Its claps, barcode reports, `ANALOG_BLOCK` and DI frames start the moment it is powered on the bus.
- So check the CAN setup on the bench first, with the transceiver not connected to the bus (Task 9 Step 3). Read FDCAN1 `NBTP`, `DBTP` and `TDCR`; `RCC_CCIPR.FDCANSEL`, which must read `01` (PLL Q); and `RCC_PLLCFGR.PLLQEN`, which must be set. Join the bus only when `NBTP` and `DBTP` decode to 1 Mbit/s nominal and 5 Mbit/s data at the FDCAN kernel clock of 170 MHz.
- Add no third terminator: the bus already has its two, so leave the breakout's own termination open. Keep the stub from the bus to the breakout short.

**Instrumentation build (build it before Task 9).**
- Several checks need faults and timing hooks that the default image must not contain. They live in one dev-only image, built from the same sources with an extra Kconfig fragment and an extra devicetree overlay that only this build passes, in its own build directory: `$BUILD_IO` with `--build-dir /tmp/h1-io-dev` and `-- -DEXTRA_CONF_FILE=<fragment> -DEXTRA_DTC_OVERLAY_FILE=<overlay>`.
- It must be built, and shown to be separate, before Task 9 starts. It is the only image with these features. Every check that does not say "instrumentation image", and every pass/fail timing measurement, runs on the default image (`/tmp/h1-io`).
- Its code is not part of the product. Where it lives afterwards (its own branch, or behind the Kconfig option) is the user's decision after Task 11.

| Feature | Used by |
|---|---|
| Free-running reference timer: `TIM2` at 1 MHz, 32-bit (71 minutes per wrap). The overlay disables `timers2`, so this build enables it with no PWM child, leaving PA5 and LD2 alone | Task 9 Steps 1 and 5; Task 11 Step 2 |
| `CYCCNT` (as board time) and the reference timer read back to back, on a shell command and at a set interval | Task 9 Steps 1 and 5; Task 11 Step 2 |
| DWT cycle counts around `read_state`, the DI change handler, the TIM3 update ISR (ordinary updates and the once-a-second tick update) and the priority-0 handlers in total | Task 10 Step 8; Task 11 Step 2 |
| Injected `irq_lock`: hold off interrupts for a set time at a set phase of the 1 ms TIM3 slot, once or repeatedly | Task 10 Step 5; Task 11 Step 3 |
| Forced DMA stop: clear the analog scanner DMA channel's `EN` | Task 11 Step 3 |
| `CYCCNT` offset: add N cycles to the counter | Task 11 Step 3 |
| `CNDTR` poll, flagging a value of 0 or above one lap (32) | Task 11 Step 3 |
| Spare-GPIO markers around the analog read gap and callback | Task 11 Step 3 |
| TTL0 raised from the tick callback | Task 10 Step 2 |
| Forced fault: a deliberate fatal error on a shell command | Task 11 Step 4 |
| Forced hang (`irq_lock` spin) and starved workqueue (a work item that blocks it), on shell commands | Task 11 Step 4 |

- [ ] **Build both images on the rig and show the split.** Expected:
  - the default image's `nm` shows none of the instrumentation symbols, its `.config` lacks the instrumentation option, and its `$SIZE` equals Task 8's;
  - the instrumentation image's `nm` shows them.

### Task 9: Hardware check script and bring-up

**Files:** Create `firmware/io_module/test/io_hil_check.py`. Standard library only, raw `AF_CAN` socket. Reuse `firmware/lib/jerrycan/test/hil_timing_check.py`'s `open_socket`, frame and trailer parsing by importing it via `sys.path`, not by copying.

- [ ] **Step 1: Does `CYCCNT` count through WFI? Run this first.** Fix waves B and C (the barcode and analog numbering) rest on it, so a failure invalidates Tasks 10 and 11. Instrumentation image, bench, no bus, console only.
  - Idle the board for 60 s. The image reads the board clock and the reference timer back to back once a second.
  - Pass: over the run, the board clock and the reference timer advance by the same number of µs, within 1 µs (the timer's resolution), and the board clock shows no 25.26 s step.
  - Fail: if `CYCCNT` stops in WFI, the signature is an analog `LOST` stop within milliseconds of boot and a stalled barcode. Stop and report; Tasks 10 and 11 are meaningless until the board clock is fixed.
  - Then read `DWT_CYCCNT` over SWD before and after a 1 s core halt. Pass: the difference is about 0, because `CYCCNT` stops with the core (Task 10 Step 6's `DBG_TIM3_STOP` rests on this). Reset the board afterwards.
- [ ] **Step 2: Write the checker.**
  - **Subcommands:** `caps`, `sync`, `timekeeper`, `gpio`, `analog`, `power`. It reports one `[PASS]`/`[FAIL]` line per check and exits non-zero on any failure.
  - **Arguments:** `--address` (default 8).
  - **Safety:** it sends only requests addressed to `--address`, `HEARTBEAT` and `ESTOP` included: the node filter accepts them, so they are never broadcast. It sends no claps, and it never addresses the pellet.
  - **Heartbeat:** `gpio` (and `power`) can send a single `HEARTBEAT` or `ESTOP` to `--address`, and can run a background 1 Hz `HEARTBEAT` for the length of a run (`--heartbeat`), stoppable on command so that a run can stop it and time the outputs going off. Task 11 Steps 2-3 need it: without a heartbeat, a non-zero `PWM_SET` is refused with `-ETIMEDOUT`.
  - **Bus load:** it sums the bits of every received frame (FD format at 1/5 Mbit/s) and reports the total for all frames and for the frames from `--address` alone.
- [ ] **Step 3: Bench bring-up, no bus.** Default image. The transceiver is not connected to the bus, or absent.
  - Before power: the NUCLEO's solder bridges that tie A4/A5 (PC1/PC0) to PB9/PB8 are open. PB9 is `TTL7`, a GPIO line.
  - Console boot log: `CAN_ID: 0x8`, capabilities `0x37`, then no errors from `jerrycan_init` or the drivers. With no bus, TX-queue-full lines are expected, not failures: nothing ACKs, `can_send` never returns, and the analog stream fills the 150-item TX queue in 0.3 s.
  - `kernel stacks`: record the interrupt stack and each thread's peak at idle. Task 11 Step 2 records them again under load.
  - Analog registers over SWD, core running:
    - TIM6: `PSC` 169, `ARR` 999, `CR2.MMS` 010.
    - DMAMUX1 `C0CR`: request id 5.
    - DMA1 `CCR1`: `CIRC`, `MINC`, `HTIE` and `TCIE` set, 16-bit sizes.
    - ADC1 `CFGR`: `EXTSEL` 13, `EXTEN` 01, `DMACFG` and `DMAEN` set; `SQR1.L` 7.
    - NVIC `IPR` for IRQ 11 (DMA1 channel 1) and IRQ 18 (ADC1_2): priority 1 (`0x10`, four priority bits).
  - DMA1 `CNDTR` reads 32 in the first millisecond after the scanner starts, so no extra scan ran before the first trigger. Reset and halt, run to the end of `analog_scanner_start`, and read it (TIM6 is frozen in a halt).
  - CAN setup, per "Before joining the live bus": `NBTP`, `DBTP`, `TDCR`, `FDCANSEL` (expect `01`) and `PLLQEN`. Record the decoded bit rates and sample points.
  - Reset the board.
- [ ] **Step 4: Join the bus and bring up.** Only after Step 3's CAN setup reads pass and the user has said go.
  - Console boot log as in Step 3, now without TX-queue-full lines.
  - `caps`: `0x37`, wire schema 1, a non-zero `boot_id`.
  - `sync`: 20 `TIME_SYNC` exchanges, each response with a `TX_QUEUE` trailer whose time equals `response_queue_time_us`.
  - `HEARTBEAT` is echoed and LD2 blinks.
- [ ] **Step 5: The board clock over time, idle.** No DI changes and no outputs; the checker sends only what a bullet below asks for.
  - Instrumentation image, ≥ 10 min: the board clock and the reference timer, read back to back at the start, every 60 s and at the end, advance by the same number of µs within 1 µs (no drift).
  - Instrumentation image, ≥ 5 min with no traffic and the pair read only at the start and the end: no 25.26 s step against the reference timer (the keep-alive is the only reader).
  - Capture node 8's frames for ≥ 320 s (12 wraps of 25.26 s). `board_time_us` in the trailers never decreases and has no 25.26 s step.
  - Three cold boots (power cycle) and three warm boots (NRST): the first `board_time_us` after each is below 25.3 s plus the time to the first frame, and each boot's `boot_id` differs from the last, so a host that fits per `boot_id` starts a new model (reachAQ stays closed; the checker prints the pairs).
  - `sync` for ≥ 10 min: fit board time against host time and record the slope in ppm. Task 11 Step 2 repeats it under load.
- [ ] **Step 6: The keep-alive after an SWD flash, without the reset.** Default image. This is the one deliberate exception to "reset after every SWD session".
  - Flash over SWD and let the tool close without resetting. Within 10 s the keep-alive restores `TRCENA`, `CYCCNTENA` and `DBG_SLEEP`.
  - Pass: the log shows exactly one warning about it; `board_time_us` has no 25.3 s step; a second disconnect without a reset logs no second warning; a cold boot logs none.
  - Then reset the board.
- [ ] **Step 7: Commit the checker** with the measured lines in the message.

### Task 10: Timekeeper on the bus and on the NI-DAQ

- [ ] **Step 1: `timekeeper` (60 s).**
  - One `SYNC_CLAP` and one `SYNC_BARCODE` per second from address 8. Ticks are consecutive and equal between the two.
  - The `SYNC_CLAP_OBSERVED` for each tick has `clapper_node` 8 and an `OBSERVED` time 0 < t < 2 ms after that clap's `TX_QUEUE` trailer time.
  - `SYNC_BARCODE`'s `PHYSICAL_START` times step by exactly 1 000 000 µs.
  - Nothing overtakes a clap. Capture the bus for the 60 s while a 1 kHz square drives DI0 (`GPIO_READ`, id 0x0A) and `sync` requests run (`TIME_SYNC_RESPONSE`, 0x20); both ids are lower than `SYNC_CLAP`'s 0x25, so they would win arbitration against a clap queued before them. Pass: node 8's trailer `sequence` never decreases in capture order (a gap from a dropped frame is allowed), including across every clap.
- [ ] **Step 2: Scope check on PA4 against the predicted time.**
  - Drive TTL0 high from the tick callback, in the instrumentation image (not the default one). Or use the PEAK hardware RX time of `SYNC_BARCODE` as a coarse cross-check.
  - Confirm the start pulse is 1.000 ms, the bits are 1.000 ms, and the bit order is LSB first for a known tick.
- [ ] **Step 3: NI-DAQ.** Only if the user wires PA4 to PXI-6221 `port0/line3` and T3 is on the rig.
  - PA4's high level at the DAQ input reads as high: above the PXI-6221 digital input's VIH of 2.2 V (the NUCLEO drives 3.3 V directly).
  - Record 10 minutes while a 10 kHz square drives DI0 and the analog stream runs.
  - T3's `decode_barcode_frames` must decode every tick with 0 rejected, and the timekeeper→NI fit residual must be ≤ 0.1 ms.
- [ ] **Step 4: Second-module behaviour.** Strap `CAN_ID[0]` (PB6) to GND, giving address 9, and reboot.
  - `caps --address 9` reads `0x17`, and the boot log prints `0x17` (node 8 printed `0x37` in Task 9).
  - No `SYNC_CLAP` or `SYNC_BARCODE` appears for 5 s.
  - PA4 stays low.
  - Remove the strap afterwards.
- [ ] **Step 5: A held-off barcode update.** Instrumentation image, wired as in Step 3. Its injected `irq_lock` holds off the TIM3 update interrupt for a set time at a set phase.
  - Lock for ≥ 1 ms and for ≥ 3 ms, inside slots 1-32 of a tick (the bits) and across slot 500 (idle). Pass, for each: only that tick is rejected by `decode_barcode_frames`; the late `SYNC_BARCODE` and `SYNC_CLAP` carry the right tick and start time; the ticks after it fit the same line as before, with no 1 ms offset.
  - Sweep the lock length over k × 1 ms ± a few µs (k = 1 to 3), and its start phase across one slot. Pass: each missed update costs at most one wrong slot, and there is never a persistent slip.
- [ ] **Step 6: Debug halts and single-steps.** Reset the board after this step.
  - Read `DBGMCU_APB1FZR1` over SWD: bit 1 (`DBG_TIM3_STOP`) is set.
  - Halt the core for ≥ 5 s, then resume. Pass: the ticks after the resume continue the numbering without a gap, and the DAQ decodes them. Board time stopped during the halt, so the clock fit is not expected to survive it.
  - Single-step about 1000 times, then compare an edge's measured time with the computed `start_us`, before and after. Record the drift per step. The ledger sets no bar; the system target is 0.1 ms.
- [ ] **Step 7: Bus faults.** Neither check runs on the live rig bus: a CANH-CANL short there would take the pellet's link down too.
  - No-ACK bench: the module and its transceiver alone, with nothing on the bus to ACK. The log shows exactly one clap-timeout line, then the pre-existing TX hang (`can_send` never returns). Nothing else changes, and the board does not reset.
  - CANH-CANL short: on a bench segment with only the module and the checker's PEAK (no pellet), short CANH to CANL at the module's transceiver, then remove the short. During the short the module does not hang (console alive, LD2 blinking). After it, frames reach the checker again without a reset: same `boot_id`, and the tick numbering continues.
- [ ] **Step 8: Barcode ISR time.** Instrumentation image. Record the min, mean and max cycles of the TIM3 update ISR over ≥ 60 s, for ordinary updates and for the once-a-second tick update (which includes the 64-bit divide on the report path). No pass bar is set in the ledger; the plan's original estimate for an ordinary update was under 1 µs.

### Task 11: DI, TTL, analog and power outputs

- [ ] **Step 1: TTL→DI loopback.** Wire TTL0 (PB14) to DI0 (PB2) and send `GPIO_PULSE` TTL0 for 1 ms, 100 times.
  - `GPIO_PULSE_STATUS` `PHYSICAL_START`/`COMPLETED` and DI0's two `GPIO_READ` `OBSERVED` times must agree within 20 µs (spec §11, acceptance 2).
  - Report both edges, each against 20 µs: `PHYSICAL_START` against DI0's rising `OBSERVED`, and `COMPLETED` against DI0's falling `OBSERVED`, with the worst case and the median of each. The falling edge is the one at risk: the pulse's counter interrupt reads the pins and queues two messages before DI0's interrupt, at the same priority, can run. With the per-port read (final fix F1) the estimate is 10-17 µs, counted from the disassembly and unmeasured, against 1-3 µs for the rising edge.
  - Index map: drive each of the 22 readable lines in turn and confirm which bit of `GPIO_READ` changes. `CAN_ID[0..1]` are bits 0-1 (strap to GND; they are active low, so the bit reads 1 while strapped), `DI0`-`DI11` are bits 2-13 (drive from 3.3 V), and `TTL0`-`TTL7` are bits 14-21 (`GPIO_WRITE`).
- [ ] **Step 2: Rate cap on the wire.** Run the checker's background heartbeat throughout (`gpio --heartbeat`, Task 9 Step 2): without one, a non-zero `PWM_SET` is refused with `-ETIMEDOUT`. Wire power output CH1 (PA8) to DI1 (PC4) and set `PWM_SET` CH1 to a 100 µs period at 50 %, which gives 20 kHz edges.
  - Over 10 s, change-triggered `GPIO_READ` from address 8 runs at ≤ 2010 frames/s, and `COALESCED` frames are present.
  - After `PWM_SET` duty 0, the last frame's DI1 bit reads 0.
  - 40 k edges/s: repeat both checks with a 50 µs period at 50 %.
  - Load, at 20 k and at 40 k edges/s (instrumentation image): record the share of CPU time in priority-0 handlers (DWT cycles per second against 170 M), and the cycles around `read_state` and the DI change handler. Pass: the whole handler fits well inside the 50 µs edge period of 20 kHz edges (8500 cycles). Compare `read_state` with the final fix's count of 500-615 cycles (counted from the disassembly, unmeasured), and extrapolate the edge rate at which the CPU saturates against the README's estimate.
  - ESTOP latency, at each rate: send `ESTOP` to address 8, and time from the end of the `ESTOP` frame on the bus (PEAK hardware stamp) to the output line going low on a scope. Pass: all outputs off. Report the latency; the README's estimate is the TX-queue drain (about 30 ms) plus up to one 6.5 ms period.
  - Heartbeats stopped under load: with the 40 k run going, stop the heartbeats. Pass: the output is off 2.0 s plus at most one period after the last heartbeat.
  - Saturation from the board's own output: wire PA8 to two or more DIs and set the minimum 10 µs period at 50 % (100 kHz, at least 400 k edges/s in total). One DI's 200 k edges/s is only about 5 % above the top of the ceiling estimate (145-190 k edges/s, unmeasured), so it might not saturate. The source is the output itself, so the storm ends with the reset. Pass: the board resets (new `boot_id`, the boot log names the watchdog), PA8 is low after the reset and stays low. If it does not reset, record the load and say so.
  - Saturation from an external signal (signal generator on a DI, an output on and heartbeats running). Above the ceiling the output goes off when the watchdog resets the board, and then the board stalls during boot, with no watchdog and no CAN, because EXTI is enabled at APPLICATION 91 and the watchdog starts at 99. Just below the ceiling it resets repeatedly. Pass in both cases: the output stays off throughout, and the board boots normally when the signal stops.
  - Cross-port skew: drive one DI on each of two GPIO ports from the same edge. Each port's bits are one snapshot, but the state word is not one across ports. Record how often a `GPIO_READ` shows one line changed and not the other, and that the next message shows both.
  - `kernel stacks`, after the 40 k run, with the analog stream running: the interrupt stack's peak is below its 2048 B (a static walk of the nested interrupts gave about 1.34 kB).
  - The board clock under load (instrumentation image), ≥ 10 min at 20 k edges/s with CAN traffic: the board clock and the reference timer advance together within 1 µs (no drift), and the `sync` slope equals Task 9 Step 5's within the fit's own uncertainty.
- [ ] **Step 3: Analog.** Tie the channels to GND, 3V3, and a divider at about mid-scale.
  - The counts are within ±40 of the expected values.
  - `ANALOG_BLOCK` arrives at 500 ± 1 frames/s with no trailer sequence gaps.
  - `SAMPLED` times step by exactly 2000 µs, over a run of at least 1 h.
  - **Bus load** (spec §11, acceptance 3). Sum each received frame's bits from the checker, using the FD frame format at 1/5 Mbit/s, and run two captures:
    - **Acceptance run:** the analog stream with pellet traffic, no DI storm. Pass: < 30 % sustained, and the PEAK `dropped` counter (`ip -s -d link show can0`) is unchanged. Expected: about 10 %.
    - **Storm run:** the analog stream and Step 2's storm together, with pellet traffic. Pass: the `dropped` counter is unchanged. Record the load without a pass bar, because the arithmetic already gives about 29-32 %, so 30 % here would fail on arithmetic alone.
    - Report each capture's load twice: for all frames, and for the frames from address 8 alone. The pellet's traffic counts toward acceptance 3, so the all-frames figure is the one that decides. With no pellet traffic on the bus the two are equal, and the report says so.
  - Absolute label: loop TTL0 to AIN0 (PA0) and send `GPIO_PULSE`. The first scan whose AIN0 count shows the edge must be the first scan with a `SAMPLED` time after the edge's `PHYSICAL_START`, within one scan period (1000 µs). The checks above cannot see a constant 1 ms label slip; this one can.
  - Edge-phase sweep: repeat the absolute-label check with the pulse start swept across one 1 ms scan period in 50 µs steps. The phase at which the first scan to show the edge changes bounds the `start_us` offset. Record the bound.
  - Held-off DMA interrupt (instrumentation image): inject an `irq_lock` of 2.5 ms and of 5 ms, at least 20 times each. Pass: a 2.5 ms lock skips one pair in about a quarter of trials and none in the rest, and a 5 ms lock skips one or two pairs, depending on phase. Each skip shows as one `SAMPLED` delta of 4000 or 6000 µs with exactly 2000 µs either side. The log shows one warning for the first skip, and the stream does not stop.
  - Forced stops (instrumentation image): clearing the DMA channel's `EN` stops the stream on an ADC overrun, with an error in the log. Adding 2 scan periods to `CYCCNT` stops it `LOST`. Adding exactly 1 scan period (a persistent one-scan slip) gives 3 more frames and then a stop with the "disagrees" error.
  - `CNDTR` poll (instrumentation image), ≥ 10 min: the value is never 0 and never above one lap (32). The firmware treats a bad position as fatal, and the DMA reload's atomicity is unconfirmed without RM0440.
  - Read gap and callback timing (instrumentation image): put the spare-GPIO markers on a scope, and record the gap between the `CNDTR` read and the `CYCCNT` read, and the callback's length. Pass: the gap is inside the 988 µs tolerance the re-review derived at 1 kHz.
  - Debug halts (reset the board afterwards): halt the core about 100 times and compare TIM6's `CNT` phase with `CYCCNT` before and after. Pass: the phase does not grow with the number of halts. If TIM6 gains on `CYCCNT` per halt, an on-time read turns `LOST` after about 2300 cycles at 8 channels, which is a few hundred single-steps. This is debug-only; record the drift per halt.
- [ ] **Step 4: Power rules.** **No real load goes on PA8/PA9/PC2/PC3 until the forced-fault test and the `DBG_TIM1_STOP` gate below have both passed.** Until then, check each rule on a scope or meter, with at most a 10 k pull-down on the pin. Keep the heartbeat running as in Step 2 unless the check is about the heartbeat.
  - Boot: scope each of PA8/PA9/PC2/PC3 from reset to the first `PWM_SET`, with and without a 10 k pull-down. Pass: with the pull-down the pin stays low throughout. Without it, record the level before and after the PWM driver's init, because the MCU's internal pull-down applies only from then on. This is the evidence for the H2 pull-down item.
  - `PWM_SET` with no heartbeat → ACK `-ETIMEDOUT` (−116 in Zephyr's errno numbering, not Linux's −110), and the output stays off.
  - With heartbeats at 1 Hz, 25 % at 1000 µs shows a 250 µs pulse.
  - A period change on one channel keeps the others' duty.
  - Period 1000 → 10 → 6553 µs with four different duties: after each change every channel's pulse is its duty times the new period. Watch all four on a scope in persistence mode. Pass: a channel written after the others may show one period of its old compare value (README), no more, and a channel at duty 0 never moves.
  - `ESTOP` assert → all off within one period, and non-zero `PWM_SET` → `-EPERM`. After release, outputs stay off until set.
  - Stop the heartbeats → all off 2.0 s plus at most one period after the last frame (so within 2.0-2.1 s).
  - Heartbeat spacing 1990-2010 ms (the expiry race): with an output on, sweep the spacing in 2 ms steps. Pass: at 1990 ms the output never drops, at 2010 ms it drops every time, and at any spacing in between it either stays on or is off by 2.0 s plus one period after the last heartbeat, never later.
  - Out-of-range requests → `-EINVAL`, and nothing changes: channel 4, duty 10001, period 9, and period 6554.
  - Refusal log limiter: send refused `PWM_SET`s at 100/s for 10 s with one reason, and again alternating two reasons (for example `-EINVAL` and `-ETIMEDOUT`, with no heartbeat running). Pass: the first refusal of each reason is logged at once, then at most one line per reason per 5 s, each saying how many refusals it left out.
  - Bus-off: on Task 10 Step 7's bench segment, with an output on and heartbeats running, short CANH to CANL at the module's transceiver. Pass: the output is off within 2 s of the short, plus at most one period.
  - **Fault safety gates** (the instrumentation image unless it says default). Run them before any load is connected:
    - `DBG_TIM1_STOP` (default image): with an output at about 50 % duty, halt the core about 20 times and look at the line at each halt. Pass: the line never freezes high, and `DBGMCU_APB2FZR` bit 11 is set. The README states "TIM1's outputs are disabled while its counter is stopped" as ST's documented behaviour for advanced timers, checked against neither RM0440 nor hardware; this is that check. Reset the board afterwards.
    - Forced fault at 50 % duty, with an output running: the line is low within tens of microseconds of the trigger (put a marker on the trigger and scope both; a delay near a PWM period means the main output enable was not cleared first), then the board resets: a new `boot_id`, and the boot log names a software reset.
    - Forced hang (`irq_lock` spin) with an output running: the output is off and the board has reset between 0.69 s and 1.09 s after the hang started (a timeout of 0.94-1.09 s from the LSI tolerance, less up to 250 ms since the last feed; the tolerance comes from sibling datasheets, so an out-of-range result means re-checking it against the G474's). Repeat at least 5 times and record the range.
    - Starved workqueue (a work item that blocks it) with an output running: the board resets within the same range, and the output is off.
    - Debug halt longer than the timeout (default image): halt the core for 5 s and resume. Pass: no reset (same `boot_id`), because the watchdog pauses under a debugger. Reset the board afterwards.
    - No-bus `can_send` hang (default image): turn an output on with heartbeats running, then take the module off the bus, so that nothing ACKs and `can_send` blocks. Pass: no reset for at least 10 s (same `boot_id`), and the output goes off 2.0 s after the last heartbeat, by the heartbeat timer.
    - Reset reason: after each reset above, the boot log shows one warning naming the cause (watchdog or software reset). A power cycle shows none.
    - A TTL pulse in flight through a forced fault and through a forced hang: the line ends low in both, at the reset.
    - Pin level after the main output enable is cleared, with the internal pull-down only: low, not floating high.
    - No false watchdog reset: the `boot_id` is unchanged through the 20 k and 40 k edges/s runs and the ≥ 1 h analog run.
    - Read `DBGMCU_APB1FZR1` bit 12 back after an SWD attach. The tool is undecided and could rewrite it.
    - The boot log names a software reset after an SWD reset or `west flash`. That is expected, not a fault.
    - Worst case from the last HEARTBEAT: a hang starting just before the 2 s timer expires takes up to about 3.09 s. Check the README states it.
  - Only when the forced-fault test and the `DBG_TIM1_STOP` gate have both passed, a real load may be connected. Record both results in the report first.

- [ ] **Step 5: H1a checks, only once H1a has run.** They come from "Hardware checks to add to H1 Task 11" in `docs/superpowers/plans/2026-11-05-h1a-independent-ttl-pulses.md` and apply only when its commit is on the branch under test. Until then the one-pulse limit stands and these are skipped.
  - Overlap accuracy: loop TTL0 and TTL1 to two DIs, and run overlapping pulses of 100 µs to 5 s. Each measured width is within ±20 µs of commanded, with the shorter one ending first.
  - Coincident ends: start pulses so that their ends coincide. Measure the spread between the lines on a scope, and confirm every `COMPLETED` arrives.
  - All 8 at once: run all 8 outputs at once, repeated, under the DI storm and the analog stream.
  - Refusals: a same-output overlap and a 9th request are refused with `-EBUSY`, and nothing else is disturbed.
  - Physical width against stamps: judge the overlap, coincident and staggered checks on a scope, and record the DI stamps separately. EXTI shares TIM16's priority, so a falling edge's stamp waits for the counter interrupt to exit (estimated +7-10 µs with one pulse ending, up to about +37 µs with eight, unmeasured).
  - Staggered ends: N pulses with coincident ends (N = 1, 3, 7) and one more ending 2-5 ticks later, under the analog stream. Compare the last edge against the estimated bound of about 14 + 4.3N µs (N=1 18, N=2 23, N=3 27, N=7 44; unmeasured) and against +20 µs. Expect it to pass at N=1 and possibly fail at N=3 or more. If it fails, move the status queuing out of the interrupt (estimated 8 + 1.3N); the stamps survive, so nothing else changes.
  - The failsafe with several in flight: pulses of 30 ms, 100 ms and 3 s. Clear `TIM16_DIER.CC1IE` over SWD: the soonest is forced low with a failure status, and the rest are re-armed and end on time with status 0. Repeat with `CR1.CEN` cleared: each is forced low at its own deadline. Reset the board afterwards.
  - Hop boundaries: widths at 32767, 32768, 65535, 65536, 65537 and 98304 µs, alone and with a 100 µs pulse started during the last hop.
  - Refusal timing: same-output requests during the last 50 µs of a 1 ms pulse, on the IO module and the pellet. The running pulse's width is unchanged.
  - Stack: during "all 8 at once", read the ISR and thread stack high-water marks (680 of 2048 B is static).
  - Pellet unchanged: a pellet second pulse is still refused with `-EBUSY`. `qualify_pellet_firmware.py` cannot do this check (its `check_refusals` tests only `-EPERM`), so add a step to it or send the two pulses by hand. Also scope pellet widths at 100 µs and 1 ms against v2.3.0, predicting about +0.9 µs (counted, unmeasured). It needs a pellet running the H1a build; this plan flashes no pellet, so that needs the user's explicit approval in that session.
- [ ] **Step 6: Record and stop.** Put the results in the ledger.
  - Fixes found here go through the SDD fix round and a scoped re-review.
  - Decide with the user, from Step 2's measured load, whether masking EXTI while a change is held is needed. It is a design change, deferred until the storm was measured.
  - Say what becomes of the instrumentation build (see "Before Task 9").
  - Carry to T2's hardware-in-the-loop run, which needs the pellet (this plan never flashes it): the regression of the clap, tone and pulse residuals against load, from fix wave A.
  - Restore the rig checkout.
  - The branch stays unmerged until the user decides.

---

## H2 checklist

Findings from H1 that the PCB plan must carry:
- **Pull-downs on the TBD62783A inputs.** Between the PWM driver's init and `power_out`'s init, the level of PA8/PA9/PC2/PC3 depends on TIM1's idle state, and duty 0 reaches the pin only at the next update (up to 6.5 ms). H1 adds only the MCU's weak internal pull-down, so the PCB needs a pull-down on each input. Task 11 Step 4's boot scope is the evidence.
- **`FDCAN_SEL` wording.** `FDCAN_SEL(1)` selects PLL Q in hardware (`LL_RCC_FDCAN_CLKSOURCE_PLL`), not PCLK. It gives 170 MHz because `div-q` makes PLL Q 170 MHz. The PCB board's devicetree comment and README must say that, and keep the selection.
- **`iwdg` stays enabled.** The IO app asserts at build time that the `iwdg` node is okay. The PCB board (`cerebellumlab_io_module`) must keep it enabled, and the assert must not be removed to make a board build.

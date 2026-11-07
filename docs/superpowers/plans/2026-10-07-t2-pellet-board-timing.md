# T2: Board timing and pellet firmware v2.4.0 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the pellet board the board-side half of reachAQ's timing contract: a µs board clock, a timing trailer on every frame with room, a TIME_SYNC responder, and a listener that reports CAN sync claps. All of it is additive and capability-gated, and released as pellet firmware v2.4.0.

**Architecture:**
- **Core (`firmware/lib/jerrycan/jerrycan.c`):**
  - board clock: `k_cycle_get_64()` → µs
    - Superseded on 2026-11-04 by the DWT cycle counter extended to 64 bits (`firmware/lib/board_clock/`, ported from the H1 fix wave; spec §5).
    - The tickless SysTick count loses cycles. Steps below that name `k_cycle_get_64()` were written before that change.
  - per-boot `boot_id`
  - **one** CAN RX callback, shared by every filter, that stamps each frame's arrival in the interrupt
  - a TX queue item carrying the trailer's kind and time; the trailer is appended where every frame is built
- **New module `modules/timing.c`:** answers `TIME_SYNC_REQUEST` and reports `SYNC_CLAP`.
- **Existing modules** pass better times where the board knows when something physically happened. These are the tone marker edge, the pressure ADC sample, and the pulse edges.

**Tech Stack:** Zephyr 3.7, STM32G474 (FDCAN), C11. Host-run C tests use CMake and gcc. Python 3 (standard library only) is used for the hardware-in-the-loop check.

**Spec:** `docs/superpowers/specs/2026-10-07-io-module-timing-design.md`, sections 2, 4, 5, 10 and 11.

## Global Constraints

- **Wire contract:**
  - The trailer is 20 bytes after the uuid byte, layout `<BBBBIIQ`: tag `0xA7`, version `1`, kind, flags, boot_id, sequence, board_time_us.
  - Kinds: `TX_QUEUE 0`, `OBSERVED 1`, `COMMAND_RECEIVED 2`, `PHYSICAL_START 3`, `COMPLETED 4`, `SAMPLED 5`. Flag bit 0 is `COALESCED`.
  - It is appended only when payload + uuid + 20 ≤ 64.
- **Message IDs and sizes:**
  - `TIME_SYNC_REQUEST 0x1F` (12 B `<IQ`)
  - `TIME_SYNC_RESPONSE 0x20` (20 B `<IQQ`)
  - `SYNC_CLAP 0x25` (4 B)
  - `SYNC_CLAP_OBSERVED 0x26` (5 B `<IB`)
  - `SYNC_BARCODE 0x27` (4 B)
  - `ANALOG_BLOCK 0x28` (38 B)
  - `PWM_SET 0x29` (7 B)
- Capability bits: `TIMING_TRAILER 1<<0`, `TIME_SYNC 1<<1`, `SYNC_LISTENER 1<<4`.
- **`TIME_SYNC_RESPONSE` must carry a trailer.** reachAQ ignores a time-sync response without one.
- **Additive only.** No change to pins, existing message IDs, existing payload layouts, send rates, motor, servo, tone or pulse behaviour, MCUboot, or the flash layout. `JERRYCAN_WIRE_SCHEMA_VERSION` stays 1.
- `CONFIG_LIB_JERRYCAN_TIMING` defaults to **n** and is enabled only in `firmware/pellet_module/prj.conf`. The magnet module shares this library and must stay wire-identical.
- **Builds happen only on christielab10, in its designated checkout `~/Documents/reachAQ-hardware`.** Its west manifest is that checkout's own `firmware/west.yml`, so a worktree there cannot build. Record the checkout's starting ref in Task 0 and restore it in Task 6.
- Firmware unit tests are host C only. A native_sim ztest cannot build in this workspace, because `drivers/motor/motor_common.c` compiles unconditionally against STM32 LL headers.
- **Flashing the rig's pellet board requires explicit user approval at that moment** (Task 6), and the board is restored to the v2.3.0 release afterwards.
- Every commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

**Shorthands used below:**

```bash
RIG="ssh -i ~/.ssh/reachaq_debug christielab10@christielab10"
# On the rig, from ~/Documents/reachAQ-hardware:
ENV='source .build-host.env && export ZEPHYR_SDK_INSTALL_DIR && export PATH=$PWD/firmware/.venv/bin:$PATH'
BUILD_FW='west build --build-dir /tmp/t2-pellet --sysbuild --board cerebellumlab_pellet_module --pristine always firmware/pellet_module'
BUILD_UPDATER='cmake -S . -B /tmp/t2-updater -GNinja && cmake --build /tmp/t2-updater --target jerrycan_updater'
HOST_TEST='cmake -S firmware/lib/jerrycan/test -B /tmp/t2-host-test -GNinja && cmake --build /tmp/t2-host-test && /tmp/t2-host-test/timing_trailer_test'
```

**Sync to the rig after each commit:**

```bash
git -C C:/Users/reynoben/Documents/reachAQ-hardware-timing bundle create C:/Users/reynoben/t2.bundle 08c1b41..feat/pellet-board-timing
scp -i ~/.ssh/reachaq_debug C:/Users/reynoben/t2.bundle christielab10@christielab10:/tmp/t2.bundle
$RIG 'cd ~/Documents/reachAQ-hardware && git pull --ff-only /tmp/t2.bundle feat/pellet-board-timing'
```

---

### Task 0: Workspace, rig checkout, baseline build

**Files:** none

- [ ] **Step 1: Local worktree**

```bash
git -C C:/Users/reynoben/Documents/reachAQ-hardware worktree add C:/Users/reynoben/Documents/reachAQ-hardware-timing -b feat/pellet-board-timing design/io-module-timing
```

- [ ] **Step 2: Record the rig checkout's state.** Expected: detached at `08c1b41`, with a clean tree.

```bash
$RIG 'cd ~/Documents/reachAQ-hardware && git rev-parse HEAD && git status --short | wc -l'
```

Write the hash in your task report. If the tree is not clean, STOP and report; do not stash someone else's work.

- [ ] **Step 3: Put the branch on the rig**

```bash
git -C C:/Users/reynoben/Documents/reachAQ-hardware-timing bundle create C:/Users/reynoben/t2.bundle 08c1b41..feat/pellet-board-timing
scp -i ~/.ssh/reachaq_debug C:/Users/reynoben/t2.bundle christielab10@christielab10:/tmp/t2.bundle
$RIG 'cd ~/Documents/reachAQ-hardware && git fetch /tmp/t2.bundle feat/pellet-board-timing:feat/pellet-board-timing && git switch feat/pellet-board-timing'
```

- [ ] **Step 4: Baseline builds**

```bash
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $BUILD_FW 2>&1 | tail -5 && ~/Documents/zephyr-sdk-0.16.8/arm-zephyr-eabi/bin/arm-zephyr-eabi-size /tmp/t2-pellet/pellet_module/zephyr/zephyr.elf"
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $BUILD_UPDATER 2>&1 | tail -2"
```

Expected: both succeed. The size is near `text 213816 data 37001 bss 38435`; record it.

---

### Task 1: Trailer format and packer, with host tests

**Files:**
- Create: `software/libjerrycan/inc/jerrycan_timing.h`
- Create: `firmware/lib/jerrycan/timing_trailer.h`, `firmware/lib/jerrycan/timing_trailer.c`
- Create: `firmware/lib/jerrycan/test/CMakeLists.txt`, `firmware/lib/jerrycan/test/timing_trailer_test.c`
- Modify: `software/libjerrycan/CMakeLists.txt`, the header `FILES` list (lines 30-37)

**Interfaces:**
- Produces in `jerrycan_timing.h` (plain C, no Zephyr or Linux includes):
  - `JERRYCAN_TIMING_TRAILER_TAG`, `JERRYCAN_TIMING_TRAILER_VERSION`, `JERRYCAN_TIMING_TRAILER_SIZE`
  - `jerrycan_timestamp_kind_t` (`JERRYCAN_TIMESTAMP_TX_QUEUE` … `JERRYCAN_TIMESTAMP_SAMPLED`)
  - `JERRYCAN_TIMING_FLAG_COALESCED`
  - `jerrycan_timing_t {uint8_t kind; uint8_t flags; uint32_t boot_id; uint32_t sequence; uint64_t board_time_us;}`
- Produces `size_t jerrycan_timing_append_trailer(uint8_t *data, size_t used, size_t capacity, const jerrycan_timing_t *timing)`. It returns the new length, or `used` unchanged when the trailer does not fit.

- [ ] **Step 1: Write the failing test**

Create `firmware/lib/jerrycan/test/CMakeLists.txt`:

```cmake
# Host-run tests for the parts of JerryCAN that do not depend on Zephyr.
# Zephyr's native_sim cannot build in this workspace (drivers/motor/motor_common.c
# needs STM32 LL headers), so these compile with the host C compiler instead.
cmake_minimum_required(VERSION 3.20)

project(jerrycan_host_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Werror")

add_executable(timing_trailer_test timing_trailer_test.c ../timing_trailer.c)

target_include_directories(timing_trailer_test
    PRIVATE
        ..
        ../../../../software/libjerrycan/inc
)
```

Create `firmware/lib/jerrycan/test/timing_trailer_test.c`:

```c
/*
    Host-run checks of the timing trailer packer.

        cmake -S firmware/lib/jerrycan/test -B /tmp/t2-host-test -GNinja
        cmake --build /tmp/t2-host-test && /tmp/t2-host-test/timing_trailer_test

    The expected bytes are what reachAQ's decoder unpacks with struct "<BBBBIIQ";
    the first vector is the one its socketcan_backend_test uses.
*/
#include <stdio.h>
#include <string.h>

#include "timing_trailer.h"

static int failures;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static void test_layout_matches_host_struct(void) {
    uint8_t frame[64];
    memset(frame, 0xEE, sizeof(frame));
    const jerrycan_timing_t timing = {
        .kind = JERRYCAN_TIMESTAMP_PHYSICAL_START,
        .flags = JERRYCAN_TIMING_FLAG_COALESCED,
        .boot_id = 0x12345678u,
        .sequence = 99u,
        .board_time_us = 7654321u,
    };
    const uint8_t expected[JERRYCAN_TIMING_TRAILER_SIZE] = {
        0xA7, 0x01, 0x03, 0x01,                         /* tag, version, kind, flags */
        0x78, 0x56, 0x34, 0x12,                         /* boot_id */
        0x63, 0x00, 0x00, 0x00,                         /* sequence */
        0xB1, 0xCB, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, /* board_time_us */
    };

    const size_t used = jerrycan_timing_append_trailer(frame, 6, sizeof(frame), &timing);

    CHECK(used == 26);
    CHECK(memcmp(frame + 6, expected, sizeof(expected)) == 0);
    CHECK(frame[5] == 0xEE);
    CHECK(frame[26] == 0xEE);
}

static void test_largest_payload_that_still_fits(void) {
    uint8_t frame[64] = {0};
    const jerrycan_timing_t timing = {0};

    /* 43-byte payload + uuid = 44, + 20 = 64: fits exactly */
    CHECK(jerrycan_timing_append_trailer(frame, 44, sizeof(frame), &timing) == 64);
    /* 44-byte payload + uuid = 45: no room, length unchanged */
    CHECK(jerrycan_timing_append_trailer(frame, 45, sizeof(frame), &timing) == 45);
}

static void test_full_frame_is_left_alone(void) {
    uint8_t frame[64];
    memset(frame, 0x5A, sizeof(frame));
    const jerrycan_timing_t timing = {.kind = JERRYCAN_TIMESTAMP_OBSERVED};

    CHECK(jerrycan_timing_append_trailer(frame, 64, sizeof(frame), &timing) == 64);
    for (size_t i = 0; i < sizeof(frame); i++) {
        CHECK(frame[i] == 0x5A);
    }
}

static void test_used_beyond_capacity_is_left_alone(void) {
    uint8_t frame[64] = {0};
    const jerrycan_timing_t timing = {0};

    CHECK(jerrycan_timing_append_trailer(frame, 70, sizeof(frame), &timing) == 70);
}

static void test_board_time_uses_all_eight_bytes(void) {
    uint8_t frame[64] = {0};
    const jerrycan_timing_t timing = {.board_time_us = 0x0102030405060708ull};
    const uint8_t expected[8] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};

    jerrycan_timing_append_trailer(frame, 0, sizeof(frame), &timing);

    CHECK(memcmp(frame + 12, expected, sizeof(expected)) == 0);
}

int main(void) {
    test_layout_matches_host_struct();
    test_largest_payload_that_still_fits();
    test_full_frame_is_left_alone();
    test_used_beyond_capacity_is_left_alone();
    test_board_time_uses_all_eight_bytes();

    if (failures != 0) {
        fprintf(stderr, "timing_trailer_test: %d check(s) failed\n", failures);
        return 1;
    }
    puts("timing_trailer_test: all checks passed");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails.** Commit locally, sync to the rig (see the Global Constraints procedure), then run:

```bash
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $HOST_TEST"
```

Expected: the build FAILS with `timing_trailer.h: No such file or directory`.

Committing a failing test is acceptable here. Squash it into Step 5's commit with `git commit --amend` before syncing again, or keep both commits; do not push either.

- [ ] **Step 3: Implement**

Create `software/libjerrycan/inc/jerrycan_timing.h`:

```c
#pragma once

/*
    The timing trailer: 20 bytes a board appends after the uuid byte of any frame
    with room for them, saying when the thing the frame describes happened on the
    board's clock. The layout is the host's - reachAQ decodes it in
    auto-trainer-device/src/autotrainer/device/socketcan_jerrycan.py as struct
    "<BBBBIIQ" (tag, version, kind, flags, boot_id, sequence, board_time_us).

    Plain C with no Zephyr or Linux dependency, so the firmware, the host library
    and host-run tests can all include it.
*/

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JERRYCAN_TIMING_TRAILER_TAG 0xA7
#define JERRYCAN_TIMING_TRAILER_VERSION 1
#define JERRYCAN_TIMING_TRAILER_SIZE 20

/* What board_time_us marks. The values are reachAQ's JerryCANTimestampKind. */
typedef enum {
    JERRYCAN_TIMESTAMP_TX_QUEUE = 0,         /* when the frame was queued for sending */
    JERRYCAN_TIMESTAMP_OBSERVED = 1,         /* when the board saw an external event (an edge, a clap) */
    JERRYCAN_TIMESTAMP_COMMAND_RECEIVED = 2, /* when the command this frame answers arrived */
    JERRYCAN_TIMESTAMP_PHYSICAL_START = 3,   /* when an output physically started */
    JERRYCAN_TIMESTAMP_COMPLETED = 4,        /* when an output physically finished */
    JERRYCAN_TIMESTAMP_SAMPLED = 5,          /* when the reported value was sampled */
} jerrycan_timestamp_kind_t;

/* Events were merged by a rate cap; board_time_us is the latest of them. */
#define JERRYCAN_TIMING_FLAG_COALESCED (1U << 0)

typedef struct {
    uint8_t kind; /* jerrycan_timestamp_kind_t */
    uint8_t flags;
    uint32_t boot_id;
    uint32_t sequence;
    uint64_t board_time_us;
} jerrycan_timing_t;

#ifdef __cplusplus
}
#endif
```

Create `firmware/lib/jerrycan/timing_trailer.h`:

```c
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "jerrycan_timing.h"

/*
    Append the timing trailer at data[used], little-endian. Returns the new length,
    or `used` unchanged when the trailer does not fit in `capacity`: a frame whose
    payload leaves fewer than 20 bytes free simply carries no trailer.
*/
size_t jerrycan_timing_append_trailer(uint8_t *data, size_t used, size_t capacity, const jerrycan_timing_t *timing);
```

Create `firmware/lib/jerrycan/timing_trailer.c`:

```c
#include "timing_trailer.h"

static void put_le(uint8_t *out, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; i++) {
        out[i] = (uint8_t)(value >> (8U * i));
    }
}

size_t jerrycan_timing_append_trailer(uint8_t *data, size_t used, size_t capacity, const jerrycan_timing_t *timing) {
    if (used > capacity || capacity - used < JERRYCAN_TIMING_TRAILER_SIZE) {
        return used;
    }

    uint8_t *out = data + used;
    out[0] = JERRYCAN_TIMING_TRAILER_TAG;
    out[1] = JERRYCAN_TIMING_TRAILER_VERSION;
    out[2] = timing->kind;
    out[3] = timing->flags;
    put_le(out + 4, timing->boot_id, 4);
    put_le(out + 8, timing->sequence, 4);
    put_le(out + 12, timing->board_time_us, 8);

    return used + JERRYCAN_TIMING_TRAILER_SIZE;
}
```

In `software/libjerrycan/CMakeLists.txt`, add the header to the public set:

```cmake
        FILES
            inc/libjerrycan.h
            inc/jerrycan_types.h
            inc/jerrycan_timing.h
```

- [ ] **Step 4: Run to verify it passes.** Sync, then run:

```bash
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $HOST_TEST"
```

Expected: `timing_trailer_test: all checks passed`, exit code 0.

- [ ] **Step 5: Commit**

```bash
git add software/libjerrycan/inc/jerrycan_timing.h software/libjerrycan/CMakeLists.txt firmware/lib/jerrycan/timing_trailer.h firmware/lib/jerrycan/timing_trailer.c firmware/lib/jerrycan/test/CMakeLists.txt firmware/lib/jerrycan/test/timing_trailer_test.c
git commit -m "feat(jerrycan): pack the timing trailer reachAQ already decodes

Plain C, so it is tested on the host against the exact bytes reachAQ's
decoder expects.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Message definitions in the shared protocol header

**Files:**
- Modify: `software/libjerrycan/inc/jerrycan_types.h`
  - include block (lines 1-20)
  - `jerrycan_cmd_type_t` (lines 27-67)
  - capability defines (around line 271-274)
  - new structs after `SIZE_CHECK(jerrycan_cmd_capabilities_response_t, 9);`
  - `jerrycan_msg_t` inner union (around line 450-484)
- Modify: `firmware/lib/jerrycan/jerrycan.c`, `jerrycan_size_map` (lines 43-80)

**Interfaces:**
- Produces enum values:
  - `JERRYCAN_CMD_TIME_SYNC_REQUEST` (0x1F), `JERRYCAN_CMD_TIME_SYNC_RESPONSE` (0x20)
  - `JERRYCAN_CMD_SYNC_CLAP` (0x25), `JERRYCAN_CMD_SYNC_CLAP_OBSERVED` (0x26), `JERRYCAN_CMD_SYNC_BARCODE` (0x27)
  - `JERRYCAN_CMD_ANALOG_BLOCK` (0x28), `JERRYCAN_CMD_PWM_SET` (0x29)
- Produces structs `jerrycan_cmd_time_sync_request_t`, `jerrycan_cmd_time_sync_response_t`, `jerrycan_cmd_sync_clap_t`, `jerrycan_cmd_sync_clap_observed_t`, `jerrycan_cmd_sync_barcode_t`, `jerrycan_cmd_analog_block_t` and `jerrycan_cmd_pwm_set_t`. The matching `jerrycan_msg_t` members are `time_sync_request`, `time_sync_response`, `sync_clap`, `sync_clap_observed`, `sync_barcode`, `analog_block` and `pwm_set`.
- Produces `JERRYCAN_CAPABILITY_SYNC_LISTENER (1U << 4)`, `JERRYCAN_CAPABILITY_TIMEKEEPER (1U << 5)` and `JERRYCAN_ANALOG_BLOCK_SAMPLE_SLOTS 16`.

This task has no runtime behaviour, so there is no unit test. Its checks are the header's `SIZE_CHECK` build assertions, which run in both the firmware build and the host C++ updater build.

- [ ] **Step 1: Implement.** Make the following edits to `jerrycan_types.h`.

After the `#endif` that closes the `#if !defined(__ZEPHYR__)` block (before `typedef uint8_t uuid_t;`):

```c
#include "jerrycan_timing.h"
```

In `jerrycan_cmd_type_t`, after `JERRYCAN_CMD_SERVO_DETACH = 0x1E,`:

```c
    JERRYCAN_CMD_TIME_SYNC_REQUEST = 0x1F,
    JERRYCAN_CMD_TIME_SYNC_RESPONSE = 0x20,
```

and after `JERRYCAN_CMD_CAPABILITIES_RESPONSE = 0x24,`:

```c
    JERRYCAN_CMD_SYNC_CLAP = 0x25,
    JERRYCAN_CMD_SYNC_CLAP_OBSERVED = 0x26,
    JERRYCAN_CMD_SYNC_BARCODE = 0x27,
    JERRYCAN_CMD_ANALOG_BLOCK = 0x28,
    JERRYCAN_CMD_PWM_SET = 0x29,
```

After `#define JERRYCAN_CAPABILITY_PELLET_PRECHECK   (1U << 3)`:

```c
#define JERRYCAN_CAPABILITY_SYNC_LISTENER     (1U << 4)
#define JERRYCAN_CAPABILITY_TIMEKEEPER        (1U << 5)
```

After `SIZE_CHECK(jerrycan_cmd_capabilities_response_t, 9);`:

```c
/*
    reachAQ's NTP-style clock exchange. The host fits board time to its own clock
    from many of these (auto-trainer-device board_clock.py), and only uses a
    response that carries a timing trailer - the trailer supplies boot_id.
*/
typedef struct __attribute__((packed)) {
    uint32_t request_id;
    uint64_t host_send_perf_ns;
} jerrycan_cmd_time_sync_request_t;

SIZE_CHECK(jerrycan_cmd_time_sync_request_t, 12);

typedef struct __attribute__((packed)) {
    uint32_t request_id;
    uint64_t request_receive_time_us; /* board time the request arrived, stamped in the CAN RX interrupt */
    uint64_t response_queue_time_us;  /* board time this response was queued */
} jerrycan_cmd_time_sync_response_t;

SIZE_CHECK(jerrycan_cmd_time_sync_response_t, 20);

/*
    The timekeeper's once-a-second clap, sent from the timekeeper's own address.
    A CAN frame finishes arriving at the same instant on every node, so the board
    times at which each node heard clap N differ only by their clock offsets.
*/
typedef struct __attribute__((packed)) {
    uint32_t tick;
} jerrycan_cmd_sync_clap_t;

SIZE_CHECK(jerrycan_cmd_sync_clap_t, 4);

/* "I heard clap `tick` from `clapper_node`"; the trailer (OBSERVED) says when. */
typedef struct __attribute__((packed)) {
    uint32_t tick;
    uint8_t clapper_node;
} jerrycan_cmd_sync_clap_observed_t;

SIZE_CHECK(jerrycan_cmd_sync_clap_observed_t, 5);

/* The timekeeper started barcode `tick` on the NI-DAQ line; the trailer (PHYSICAL_START) says when. */
typedef struct __attribute__((packed)) {
    uint32_t tick;
} jerrycan_cmd_sync_barcode_t;

SIZE_CHECK(jerrycan_cmd_sync_barcode_t, 4);

#define JERRYCAN_ANALOG_BLOCK_SAMPLE_SLOTS 16

/* channel_count x scan_count raw ADC counts, scan-major; the trailer (SAMPLED) is the first scan's trigger time. */
typedef struct __attribute__((packed)) {
    uint8_t instance;
    uint8_t channel_count;
    uint8_t scan_count;
    uint8_t flags;
    uint16_t scan_period_us;
    uint16_t samples[JERRYCAN_ANALOG_BLOCK_SAMPLE_SLOTS];
} jerrycan_cmd_analog_block_t;

SIZE_CHECK(jerrycan_cmd_analog_block_t, 38);

/* period_us 0 keeps the current period; a non-zero period applies to every channel on the timer. */
typedef struct __attribute__((packed)) {
    uint8_t channel;
    uint16_t duty_permyriad;
    uint32_t period_us;
} jerrycan_cmd_pwm_set_t;

SIZE_CHECK(jerrycan_cmd_pwm_set_t, 7);
```

In `jerrycan_msg_t`'s inner union, after `jerrycan_rsp_ack_t ack;`:

```c
                jerrycan_cmd_time_sync_request_t time_sync_request;
                jerrycan_cmd_time_sync_response_t time_sync_response;
                jerrycan_cmd_sync_clap_t sync_clap;
                jerrycan_cmd_sync_clap_observed_t sync_clap_observed;
                jerrycan_cmd_sync_barcode_t sync_barcode;
                jerrycan_cmd_analog_block_t analog_block;
                jerrycan_cmd_pwm_set_t pwm_set;
```

`analog_block` (38 B) becomes the largest inner member, so `uuid`'s offset inside `jerrycan_msg_t` moves from 28 to 38. That is harmless: every copy, in both `jerrycan_run` and libjerrycan's `ReceiveMessage`/`SendMessage`, goes through the `uuid` field by name, never by offset. The existing `BUILD_ASSERT` on `sizeof(jerrycan_msg_t)` is unchanged, because the outer union is still 64 B.

In `jerrycan.c` `jerrycan_size_map`, after `[JERRYCAN_CMD_CAPABILITIES_RESPONSE] = sizeof(jerrycan_cmd_capabilities_response_t),`:

```c
        [JERRYCAN_CMD_TIME_SYNC_REQUEST] = sizeof(jerrycan_cmd_time_sync_request_t),
        [JERRYCAN_CMD_TIME_SYNC_RESPONSE] = sizeof(jerrycan_cmd_time_sync_response_t),
        [JERRYCAN_CMD_SYNC_CLAP] = sizeof(jerrycan_cmd_sync_clap_t),
        [JERRYCAN_CMD_SYNC_CLAP_OBSERVED] = sizeof(jerrycan_cmd_sync_clap_observed_t),
        [JERRYCAN_CMD_SYNC_BARCODE] = sizeof(jerrycan_cmd_sync_barcode_t),
        [JERRYCAN_CMD_ANALOG_BLOCK] = sizeof(jerrycan_cmd_analog_block_t),
        [JERRYCAN_CMD_PWM_SET] = sizeof(jerrycan_cmd_pwm_set_t),
```

- [ ] **Step 2: Verify with both builds and the host test.** Sync, then run:

```bash
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $BUILD_FW 2>&1 | tail -3 && $BUILD_UPDATER 2>&1 | tail -2 && $HOST_TEST"
```

Expected: both builds succeed with no `SIZE_CHECK` failure, and the host test passes.

- [ ] **Step 3: Commit**

```bash
git add software/libjerrycan/inc/jerrycan_types.h firmware/lib/jerrycan/jerrycan.c
git commit -m "feat(jerrycan): define the time-sync, sync and IO-module messages

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Core timing (board clock, RX stamping, TX trailer)

**Files:**
- Modify: `firmware/lib/jerrycan/jerrycan.h`, `firmware/lib/jerrycan/jerrycan.c`
- Modify: `firmware/lib/jerrycan/Kconfig` (new option after the `LIB_JERRYCAN_LOG_LEVEL` block, lines 12-16)
- Modify: `firmware/lib/jerrycan/CMakeLists.txt`
- Modify: `firmware/lib/jerrycan/modules/capabilities.c` (`boot_id` moves to the core)
- Modify: `firmware/pellet_module/prj.conf`

**Interfaces:**
- Consumes `jerrycan_timing_append_trailer` (Task 1), plus `JERRYCAN_CMD_SYNC_CLAP` and the new size-map entries (Task 2).
- Produces in `jerrycan.h`:
  - `uint64_t jerrycan_board_time_us(void)`
  - `uint64_t jerrycan_cycles_to_board_time_us(uint64_t cycles)`
  - `uint32_t jerrycan_boot_id(void)`
  - `uint64_t jerrycan_rx_time_us(void)`, valid only inside an RX callback
  - `int jerrycan_tx_timed(jerrycan_msg_t *msg, jerrycan_timestamp_kind_t kind, uint8_t flags, uint64_t board_time_us, k_timeout_t timeout)`
  - In RX callbacks, `msg->dst_id` now holds the sender's 5-bit address.
- Produces Kconfig `CONFIG_LIB_JERRYCAN_TIMING`.

Not host-testable: everything here calls Zephyr. It is verified by build here and on hardware in Task 6.

- [ ] **Step 1: Kconfig.** After the `LIB_JERRYCAN_LOG_LEVEL` block (`help` / `Set the log level for the tone generator`), add:

```kconfig
config LIB_JERRYCAN_TIMING
    bool "Board timestamps: timing trailer, TIME_SYNC and the sync clap"
    default n
    depends on LIB_JERRYCAN
    help
      Append the 20-byte timing trailer to every frame with room for it, answer
      reachAQ's TIME_SYNC_REQUEST, and report SYNC_CLAP frames from a timekeeper.
      Off by default so boards that share this library keep their wire format;
      a board opts in from its prj.conf. With it off the bytes on the wire are
      exactly those of pellet firmware v2.3.0.
```

- [ ] **Step 2: CMake.** In `firmware/lib/jerrycan/CMakeLists.txt`, after the closing `)` of the first `zephyr_library_sources(...)` block, add:

```cmake
zephyr_library_sources_ifdef(CONFIG_LIB_JERRYCAN_TIMING
	       timing_trailer.c
	       modules/timing.c
)
```

`modules/timing.c` is filled in by Task 4. Until then, create it with just an include, so this task builds without an empty translation unit:

```c
/* Filled in by the time-sync and sync-clap task. */
#include <jerrycan.h>
```

- [ ] **Step 3: `jerrycan.h`.** Add after `#include "jerrycan_types.h"`:

```c
/*
    Board clock: microseconds since boot, from the 64-bit CPU cycle counter. The same
    clock stamps every timing trailer. Safe in any context, including interrupts.
*/
uint64_t jerrycan_board_time_us(void);

/* Convert a k_cycle_get_64() reading taken elsewhere (e.g. in a driver ISR) to board time. */
uint64_t jerrycan_cycles_to_board_time_us(uint64_t cycles);

/* Random per boot. Reported in CAPABILITIES_RESPONSE and in every timing trailer. */
uint32_t jerrycan_boot_id(void);

/*
    Board time at which the frame now being handled finished arriving, stamped in the
    CAN RX interrupt. Valid only inside a jerrycan RX callback.
*/
uint64_t jerrycan_rx_time_us(void);
```

and after `int jerrycan_tx(jerrycan_msg_t *msg, k_timeout_t timeout);`:

```c
/*
    Queue a message whose timing trailer reports `kind` at `board_time_us`, instead of
    the moment it was queued (which is what jerrycan_tx() reports, as TX_QUEUE).
    Use it when the board knows when the thing the message describes happened.
*/
int jerrycan_tx_timed(jerrycan_msg_t *msg, jerrycan_timestamp_kind_t kind, uint8_t flags, uint64_t board_time_us,
                      k_timeout_t timeout);
```

- [ ] **Step 4: `jerrycan.c`.** These are seven edits.

**(a) Includes.** After `#include <zephyr/sys/slist.h>`:

```c
#include <zephyr/random/random.h>

#if CONFIG_LIB_JERRYCAN_TIMING
#include "timing_trailer.h"
#endif
```

**(b) Queues and state.** Replace exactly these two lines:

```c
CAN_MSGQ_DEFINE(jerrycan_rx_msgq, 10);
K_MSGQ_DEFINE(jerrycan_tx_msgq, sizeof(jerrycan_msg_t), 150, 4);
```

with:

```c
BUILD_ASSERT(IS_ENABLED(CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER), "JerryCAN board time needs a 64-bit cycle counter");

/*
    Every received frame is stamped in the CAN RX interrupt, before the jerrycan thread
    reaches it, so a TIME_SYNC request or a clap is timed when it arrived rather than
    when the thread got round to it. One callback serves every filter: the FDCAN hands
    a frame only to the first filter it matches, so a separate "timing" filter would
    silently lose to the node filter.
*/
typedef struct {
    uint64_t rx_time_us;
    struct can_frame frame;
} jerrycan_rx_item_t;

/* A queued message and what its timing trailer reports. */
typedef struct {
    uint64_t board_time_us;
    uint8_t kind;
    uint8_t flags;
    jerrycan_msg_t msg;
} jerrycan_tx_item_t;

K_MSGQ_DEFINE(jerrycan_rx_msgq, sizeof(jerrycan_rx_item_t), 10, 8);
K_MSGQ_DEFINE(jerrycan_tx_msgq, sizeof(jerrycan_tx_item_t), 150, 8);

static uint32_t boot_id;
static uint64_t current_rx_time_us;

uint64_t jerrycan_cycles_to_board_time_us(uint64_t cycles) { return k_cyc_to_us_floor64(cycles); }

uint64_t jerrycan_board_time_us(void) { return jerrycan_cycles_to_board_time_us(k_cycle_get_64()); }

uint32_t jerrycan_boot_id(void) { return boot_id; }

uint64_t jerrycan_rx_time_us(void) { return current_rx_time_us; }

static void jerrycan_rx_isr(const struct device *dev, struct can_frame *frame, void *user_data) {
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    /* Stamp first: the copy below is not part of the arrival time */
    const uint64_t rx_time_us = jerrycan_board_time_us();
    const jerrycan_rx_item_t item = {.rx_time_us = rx_time_us, .frame = *frame};

    if (k_msgq_put(&jerrycan_rx_msgq, &item, K_NO_WAIT) != 0) {
        LOG_ERR("JerryCAN RX queue overflowed. Frame ID: 0x%x", frame->id);
    }
}
```

The TX item is 96 B (8 + 1 + 1 + 82, padded). 150 items cost about 2.1 KB more RAM than before; Task 0's size report showed about 53 KB free.

**(c) `jerrycan_tx`.** Replace the whole function (from `int jerrycan_tx(jerrycan_msg_t *msg, k_timeout_t timeout) {` to its closing `}`) with:

```c
int jerrycan_tx(jerrycan_msg_t *msg, k_timeout_t timeout) {
    return jerrycan_tx_timed(msg, JERRYCAN_TIMESTAMP_TX_QUEUE, 0, jerrycan_board_time_us(), timeout);
}

int jerrycan_tx_timed(jerrycan_msg_t *msg, jerrycan_timestamp_kind_t kind, uint8_t flags, uint64_t board_time_us,
                      k_timeout_t timeout) {
    static size_t failureCount = 0;

    const jerrycan_tx_item_t item = {
        .board_time_us = board_time_us,
        .kind = (uint8_t)kind,
        .flags = flags,
        .msg = *msg,
    };

    // Place message in queue
    const int ret = k_msgq_put(&jerrycan_tx_msgq, &item, timeout);
    if (ret != 0) {
        if (!failureCount++) {
            LOG_ERR("jerrycan_tx failed to place message in queue: %d", ret);
        }
    } else {
        if (failureCount) {
            LOG_ERR("jerrycan transmissions restored. %d lost packets.", failureCount);
        }
        failureCount = 0;
    }

    return ret;
}
```

**(d) `jerrycan_run` locals.** Replace:

```c
    static jerrycan_msg_t msg;
    static struct can_frame frame;
    int ret;
```

with:

```c
    static jerrycan_msg_t msg;
    static struct can_frame frame;
    static jerrycan_rx_item_t rx_item;
    static jerrycan_tx_item_t tx_item;
#if CONFIG_LIB_JERRYCAN_TIMING
    /* Counts trailer-bearing frames, so the host can tell a lost frame from a quiet board */
    static uint32_t trailer_sequence;
#endif
    int ret;
```

**(e) TX loop.** Replace:

```c
        while (k_msgq_get(&jerrycan_tx_msgq, &msg, K_NO_WAIT) == 0) {
            uint8_t payload_size = jerrycan_msg_get_payload_size(msg.type);
```

with:

```c
        while (k_msgq_get(&jerrycan_tx_msgq, &tx_item, K_NO_WAIT) == 0) {
            msg = tx_item.msg;
            uint8_t payload_size = jerrycan_msg_get_payload_size(msg.type);
```

and replace:

```c
            frame.dlc = can_bytes_to_dlc(payload_size);
```

with:

```c
#if CONFIG_LIB_JERRYCAN_TIMING
            const jerrycan_timing_t timing = {
                .kind = tx_item.kind,
                .flags = tx_item.flags,
                .boot_id = boot_id,
                .sequence = trailer_sequence,
                .board_time_us = tx_item.board_time_us,
            };
            const size_t with_trailer =
                jerrycan_timing_append_trailer(frame.data, payload_size, sizeof(frame.data), &timing);
            if (with_trailer != payload_size) {
                payload_size = (uint8_t)with_trailer;
                trailer_sequence++;
            }
#endif

            frame.dlc = can_bytes_to_dlc(payload_size);
```

**(f) RX loop.** Replace:

```c
        while (k_msgq_get(&jerrycan_rx_msgq, &frame, K_NO_WAIT) == 0) {
            msg.type = frame.id >> 5;
```

with:

```c
        while (k_msgq_get(&jerrycan_rx_msgq, &rx_item, K_NO_WAIT) == 0) {
            frame = rx_item.frame;
            current_rx_time_us = rx_item.rx_time_us;
            msg.type = frame.id >> 5;
            msg.dst_id = frame.id & NODE_ID_MASK;
```

**(g) `jerrycan_init`.**

- Directly after `sys_slist_init(&can_rx_callbacks_list);`, add:

  ```c
      // Drawn before any module can queue a frame, so every trailer of this boot carries it
      boot_id = sys_rand32_get();
  ```

- Replace both occurrences of `ret = can_add_rx_filter_msgq(can_dev, &jerrycan_rx_msgq, &jerrycan_rx_filter);` and `ret = can_add_rx_filter_msgq(can_dev, &jerrycan_rx_msgq, &jerrycan_broadcast_filter);` with `can_add_rx_filter(can_dev, jerrycan_rx_isr, NULL, &…)`, keeping each filter variable.

- Directly before the final `return 0;`, add:

  ```c
  #if CONFIG_LIB_JERRYCAN_TIMING
      // Claps are sent from the timekeeper's own address, so neither filter above sees them
      const struct can_filter jerrycan_clap_filter = {
          .flags = 0,
          .id = (uint32_t)JERRYCAN_CMD_SYNC_CLAP << 5,
          .mask = 0x3F << 5,
      };

      ret = can_add_rx_filter(can_dev, jerrycan_rx_isr, NULL, &jerrycan_clap_filter);
      if (ret < 0) {
          LOG_ERR("Failed to add CAN clap filter: %d", ret);
          return ret;
      }
  #endif
  ```

- [ ] **Step 5: `capabilities.c` uses the core's `boot_id`.**
  - Delete `static uint32_t boot_id;` and its preceding comment block ("Drawn once at init rather than per request…").
  - Delete `boot_id = sys_rand32_get();` in `jerrycan_capabilities_init`.
  - Replace the two remaining uses of `boot_id` with `jerrycan_boot_id()`.
  - Remove `#include <zephyr/random/random.h>` if nothing else in the file uses it.

  Capabilities stay as they are until Task 4.

- [ ] **Step 6: Enable it for the pellet.** In `firmware/pellet_module/prj.conf`, after `CONFIG_COUNTER=y`, add:

```
# Board timestamps: timing trailer, TIME_SYNC responder, sync-clap listener
CONFIG_LIB_JERRYCAN_TIMING=y
```

- [ ] **Step 7: Build and check for silent fallbacks.** Sync, then run:

```bash
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $BUILD_FW 2>&1 | grep -iE 'warning|error' | head; grep -E '^CONFIG_LIB_JERRYCAN_TIMING|^CONFIG_TIMER_HAS_64BIT' /tmp/t2-pellet/pellet_module/zephyr/.config; ~/Documents/zephyr-sdk-0.16.8/arm-zephyr-eabi/bin/arm-zephyr-eabi-size /tmp/t2-pellet/pellet_module/zephyr/zephyr.elf"
```

Expected:
- no errors, and no new warnings from `firmware/lib/jerrycan`
- both configs `=y`
- `bss` grows by roughly 2-3 KB over the Task 0 baseline

Run `$HOST_TEST` too; it must still pass.

- [ ] **Step 8: Commit**

```bash
git add firmware/lib/jerrycan/jerrycan.h firmware/lib/jerrycan/jerrycan.c firmware/lib/jerrycan/Kconfig firmware/lib/jerrycan/CMakeLists.txt firmware/lib/jerrycan/modules/capabilities.c firmware/lib/jerrycan/modules/timing.c firmware/pellet_module/prj.conf
git commit -m "feat(jerrycan): stamp frames on arrival and append the timing trailer

One RX callback serves every filter and stamps in the interrupt; the FDCAN
gives a frame only to its first matching filter, so a second filter could
never see a time-sync request. Behind CONFIG_LIB_JERRYCAN_TIMING, which only
the pellet enables.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: TIME_SYNC responder, clap listener, capability bits

**Files:**
- Modify (fill in): `firmware/lib/jerrycan/modules/timing.c`
- Modify: `firmware/lib/jerrycan/modules/capabilities.c`, the header comment (lines 19-26) and the `BOARD_CAPABILITIES` define (line 51)

**Interfaces:**
- Consumes `jerrycan_rx_time_us()`, `jerrycan_board_time_us()` and `jerrycan_tx_timed()` (Task 3), and the message structs (Task 2).
- Produces: the board answers 0x1F with 0x20, carrying a trailer of kind `TX_QUEUE` at `response_queue_time_us`. It answers 0x25 from any address with 0x26, kind `OBSERVED` at the clap's arrival. Neither sends an ACK. Capabilities report `TIMING_TRAILER | TIME_SYNC | SYNC_LISTENER` when `CONFIG_LIB_JERRYCAN_TIMING=y`.

Verified by build here and on hardware in Task 6.

- [ ] **Step 1: Write `modules/timing.c`**

```c
/**
 * @file timing.c
 * @brief Answer TIME_SYNC requests and report the sync claps this board hears.
 *
 * Both answers use times stamped in the CAN RX interrupt (jerrycan_rx_time_us()),
 * so however long the jerrycan thread takes to reach a frame does not enter them.
 *
 * - TIME_SYNC_REQUEST (0x1F): reachAQ's NTP-style exchange. The response says when
 *   the request arrived and when the response was queued. Its timing trailer
 *   carries boot_id, without which reachAQ ignores the response.
 * - SYNC_CLAP (0x25): the timekeeper's once-a-second clap, accepted from any
 *   address. Answered with SYNC_CLAP_OBSERVED, stamped OBSERVED at arrival.
 *
 * Neither is acknowledged: the time-sync response is the answer, and a clap is a
 * broadcast that nobody waits on.
 */

#include <jerrycan.h>
#include <jerrycan_types.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

static int time_sync_request(const jerrycan_msg_t *msg) {
    jerrycan_msg_t response = {
        .type = JERRYCAN_CMD_TIME_SYNC_RESPONSE,
        .time_sync_response =
            {
                .request_id = msg->time_sync_request.request_id,
                .request_receive_time_us = jerrycan_rx_time_us(),
            },
        .uuid = msg->uuid,
    };

    const uint64_t queued_us = jerrycan_board_time_us();
    response.time_sync_response.response_queue_time_us = queued_us;

    const int ret = jerrycan_tx_timed(&response, JERRYCAN_TIMESTAMP_TX_QUEUE, 0, queued_us, K_NO_WAIT);
    if (ret != 0) {
        LOG_ERR("Failed to answer TIME_SYNC request %u - %d", msg->time_sync_request.request_id, ret);
    }

    return SEND_NO_ACKNOWLEDGEMENT;
}

static int sync_clap_heard(const jerrycan_msg_t *msg) {
    jerrycan_msg_t observed = {
        .type = JERRYCAN_CMD_SYNC_CLAP_OBSERVED,
        .sync_clap_observed =
            {
                .tick = msg->sync_clap.tick,
                .clapper_node = msg->dst_id,
            },
    };

    const int ret = jerrycan_tx_timed(&observed, JERRYCAN_TIMESTAMP_OBSERVED, 0, jerrycan_rx_time_us(), K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("Failed to report clap %u - %d", msg->sync_clap.tick, ret);
    }

    return SEND_NO_ACKNOWLEDGEMENT;
}

static jerrycan_rx_callback_t time_sync_callback = {
    .filter_msg_type = JERRYCAN_CMD_TIME_SYNC_REQUEST,
    .func = time_sync_request,
};

static jerrycan_rx_callback_t sync_clap_callback = {
    .filter_msg_type = JERRYCAN_CMD_SYNC_CLAP,
    .func = sync_clap_heard,
};

static int jerrycan_timing_init() {
    jerrycan_register_rx_callback(&time_sync_callback);
    jerrycan_register_rx_callback(&sync_clap_callback);
    return 0;
}

SYS_INIT(jerrycan_timing_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
```

- [ ] **Step 2: Capabilities.**

In the header comment of `capabilities.c`, replace these lines:

```
 * Two bits the host defines are deliberately never set:
 *
 *   - TIMING_TRAILER, because nothing in this firmware appends one, and
 *     nothing in the host reads one. It is a name reserved on both sides.
 *   - TIME_SYNC, because JERRYCAN_CMD_TIME_SYNC_REQUEST (0x1F) has no handler
 *     in this firmware and no entry in the command enum. A board that claimed
 *     it would be asked for a sync it cannot perform.
```

with:

```
 * The timing bits (TIMING_TRAILER, TIME_SYNC, SYNC_LISTENER) follow
 * CONFIG_LIB_JERRYCAN_TIMING - the same switch that compiles the trailer, the
 * TIME_SYNC responder and the clap listener - so a board reports them exactly
 * when it has them.
```

Replace `#define BOARD_CAPABILITIES (CAPABILITY_FINITE_GPIO_PULSE)` with:

```c
#if CONFIG_LIB_JERRYCAN_TIMING
#define CAPABILITY_TIMING \
    (JERRYCAN_CAPABILITY_TIMING_TRAILER | JERRYCAN_CAPABILITY_TIME_SYNC | JERRYCAN_CAPABILITY_SYNC_LISTENER)
#else
#define CAPABILITY_TIMING 0U
#endif

#define BOARD_CAPABILITIES (CAPABILITY_FINITE_GPIO_PULSE | CAPABILITY_TIMING)
```

- [ ] **Step 3: Build.** Sync, then run:

```bash
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $BUILD_FW 2>&1 | grep -iE 'warning|error' | head; grep -c jerrycan_timing_init /tmp/t2-pellet/pellet_module/zephyr/zephyr.map"
```

Expected: no errors or new warnings, and a count ≥ 1, which shows the module is linked.

- [ ] **Step 4: Commit**

```bash
git add firmware/lib/jerrycan/modules/timing.c firmware/lib/jerrycan/modules/capabilities.c
git commit -m "feat(jerrycan): answer TIME_SYNC and report the sync claps a board hears

Advertises timing_trailer, time_sync and sync_listener on boards built with
CONFIG_LIB_JERRYCAN_TIMING.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Physical times from the tone, pressure, GPIO and door paths

**Files:**
- Modify: `firmware/drivers/tone_generator/tone_generator.c` (data struct lines 96-102; `ll_tone_generator_play_tone` lines 339-343; a new getter after `ll_tone_generator_get_frequency`) and `tone_generator.h`
- Modify: `firmware/drivers/pressure_sensor/pressure_sensor.c` (includes; data struct lines 22-25; `adc_cb` lines 29-32; a new getter after `ll_pressure_sensor_get_pressure`; macro line 90) and `pressure_sensor.h`
- Modify: `firmware/lib/jerrycan/modules/tone.c`, `pressure.c`, `gpio.c`, `door_sensor.c`

**Interfaces:**
- Consumes `jerrycan_tx_timed`, `jerrycan_board_time_us` and `jerrycan_cycles_to_board_time_us` (Task 3).
- Produces driver API:
  - `uint64_t ll_tone_generator_get_start_cycles(const struct device *dev)` returns the `k_cycle_get_64()` reading at the latest tone's marker edge, or 0 if no tone has started.
  - `void ll_pressure_sensor_get_sample(const struct device *dev, uint16_t *value, uint64_t *sample_cycles)` returns the latest sample and its completion time as one consistent pair.
  - Both are additive; `ll_pressure_sensor_get_pressure` is unchanged.
- **Trailer kinds on the wire after this task:**
  - `TONE`: `PHYSICAL_START` for the status sent right after a play command, at the STIM marker edge the NI-DAQ records; `SAMPLED` for periodic status
  - `PRESSURE_READ`: `SAMPLED`, at ADC sample completion
  - `GPIO_READ`: `SAMPLED`, when the pins were read
  - `DOOR_SENSOR`: `SAMPLED`, at send
  - `GPIO_PULSE_STATUS`: `PHYSICAL_START` (asserted edge), `COMPLETED` (return-low edge), or `TX_QUEUE` (rejected)

The drivers record raw cycle counts (`k_cycle_get_64()`), not board time, so they don't depend on the JerryCAN library.

- [ ] **Step 1: Tone driver.**

In `ll_tone_generator_data_t`, after `unsigned int frequency_hz;`, add:

```c
    uint64_t start_cycles; /* k_cycle_get_64() at the latest tone's marker edge; 0 before the first tone */
```

In `ll_tone_generator_play_tone`, replace:

```c
    /* Set the matching acquisition marker before starting the audio hardware. */
    ret = ll_tone_generator_set_tone_output(cfg, frequency_hz);
    if (ret != 0) {
        return ret;
    }
```

with:

```c
    /* Set the matching acquisition marker before starting the audio hardware. */
    ret = ll_tone_generator_set_tone_output(cfg, frequency_hz);
    if (ret != 0) {
        return ret;
    }

    /* The marker edge is what the NI-DAQ records as the tone's start */
    data->start_cycles = k_cycle_get_64();
```

After `ll_tone_generator_get_frequency`, add:

```c
/* k_cycle_get_64() at the latest tone's marker edge; 0 if no tone has started */
uint64_t ll_tone_generator_get_start_cycles(const struct device *dev) {
    const ll_tone_generator_data_t *data = dev->data;

    return data->start_cycles;
}
```

In `tone_generator.h`, append:

```c
/* k_cycle_get_64() at the latest tone's marker edge (the STIM line the NI-DAQ records); 0 if no tone has started */
uint64_t ll_tone_generator_get_start_cycles(const struct device *dev);
```

- [ ] **Step 2: Tone module.** In `tone.c`, change `jerrycan_tone_generator_tx`.

Its signature becomes:

```c
static void jerrycan_tone_generator_tx(jerrycan_tone_context_t *context, jerrycan_timestamp_kind_t kind,
                                       uint64_t board_time_us) {
```

and its send becomes:

```c
    int ret = jerrycan_tx_timed(&msg, kind, 0, board_time_us, K_NO_WAIT);
```

In `jerrycan_tone_generator_bulk_tx`, change the call to:

```c
        jerrycan_tone_generator_tx(&contexts[i], JERRYCAN_TIMESTAMP_SAMPLED, jerrycan_board_time_us());
```

In `jerrycan_tone_generator_write_handler`, replace:

```c
        /* Transmit state immediately after starting tone */
        jerrycan_tone_generator_tx(&contexts[idx]);
```

with:

```c
        /* Transmit state immediately after starting tone, stamped at the marker edge when it started */
        if (rc == 0) {
            jerrycan_tone_generator_tx(
                &contexts[idx], JERRYCAN_TIMESTAMP_PHYSICAL_START,
                jerrycan_cycles_to_board_time_us(ll_tone_generator_get_start_cycles(tone_generator)));
        } else {
            jerrycan_tone_generator_tx(&contexts[idx], JERRYCAN_TIMESTAMP_SAMPLED, jerrycan_board_time_us());
        }
```

- [ ] **Step 3: Pressure driver.**

Add `#include <zephyr/kernel.h>` after `#include <zephyr/drivers/adc.h>`.

Replace the data struct:

```c
/* Pressure Sensor data structure */
typedef struct {
    uint16_t raw_data;
} ll_pressure_sensor_data_t;
```

with:

```c
/* Pressure Sensor data structure */
typedef struct {
    uint16_t raw_data;
    /* The latest completed sample and its k_cycle_get_64() time, published together by adc_cb */
    struct k_spinlock lock;
    uint16_t published;
    uint64_t published_cycles;
} ll_pressure_sensor_data_t;
```

Replace the callback:

```c
/* ADC callback */
static enum adc_action adc_cb(const struct device *dev, const struct adc_sequence *, uint16_t) {
    return ADC_ACTION_REPEAT;
}
```

with:

```c
/* ADC callback: runs as each sample completes, so its time is the sample's time */
static enum adc_action adc_cb(const struct device *dev, const struct adc_sequence *sequence, uint16_t) {
    ARG_UNUSED(dev);
    ll_pressure_sensor_data_t *data = sequence->options->user_data;
    const uint64_t now = k_cycle_get_64();

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    data->published = data->raw_data;
    data->published_cycles = now;
    k_spin_unlock(&data->lock, key);

    return ADC_ACTION_REPEAT;
}
```

After `ll_pressure_sensor_get_pressure`, add:

```c
/* The latest completed sample and the k_cycle_get_64() time it completed, read as one consistent pair */
void ll_pressure_sensor_get_sample(const struct device *dev, uint16_t *value, uint64_t *sample_cycles) {
    ll_pressure_sensor_data_t *data = dev->data;

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    *value = data->published;
    *sample_cycles = data->published_cycles;
    k_spin_unlock(&data->lock, key);
}
```

In the `PRESSURE_SENSOR_INST(idx)` macro, change the line `.user_data = NULL,` to `.user_data = &pressure_sensor_data_##idx,`. Keep the line's trailing `\` as its last character. The data instance is defined earlier in the same macro, so the address is in scope.

In `pressure_sensor.h`, append:

```c
/* The latest completed sample and the k_cycle_get_64() time it completed, read as one consistent pair */
void ll_pressure_sensor_get_sample(const struct device *dev, uint16_t *value, uint64_t *sample_cycles);
```

- [ ] **Step 4: Pressure module.** In `pressure.c`'s `jerrycan_pressure_sensor_tx`, replace the loop body with:

```c
        const jerrycan_pressure_sensor_context_t *context = &contexts[i];
        uint16_t pressure;
        uint64_t sample_cycles;

        ll_pressure_sensor_get_sample(context->pressure_sensor, &pressure, &sample_cycles);

        jerrycan_msg_t msg = {
            .type = JERRYCAN_CMD_PRESSURE_READ,
            .pressure_read =
                {
                    .instance = context->instance_number,
                    .pressure = pressure,
                },
        };

        jerrycan_tx_timed(&msg, JERRYCAN_TIMESTAMP_SAMPLED, 0, jerrycan_cycles_to_board_time_us(sample_cycles),
                          K_NO_WAIT);
```

- [ ] **Step 5: GPIO module.** Make these edits in `gpio.c`.

**Periodic state (`jerrycan_generic_gpio_tx`).**
- Directly before `uint64_t state = 0;`, add `const uint64_t sampled_us = jerrycan_board_time_us();`.
- Replace `jerrycan_tx(&msg, K_NO_WAIT);` at the end of that loop with `jerrycan_tx_timed(&msg, JERRYCAN_TIMESTAMP_SAMPLED, 0, sampled_us, K_NO_WAIT);`.

**`gpio_pulse_emit_status`** gains the timing. Its signature becomes:

```c
static void gpio_pulse_emit_status(uint8_t instance, uint16_t gpio_idx, uint32_t duration_us, uint8_t phase,
                                   int32_t error, uint8_t uuid, jerrycan_timestamp_kind_t kind,
                                   uint64_t board_time_us) {
```

and its send becomes `jerrycan_tx_timed(&msg, kind, 0, board_time_us, K_NO_WAIT);`.

**Alarm callback.**
- Add `uint64_t low_cycles = 0;` to the locals.
- Directly after `(void)ll_generic_gpio_write_pin(gpio_pulse.generic_gpio, gpio_pulse.writable_idx, 0);` inside the `ticks_remaining == 0` branch, add `low_cycles = k_cycle_get_64();`.
- The completed emit becomes:

  ```c
          gpio_pulse_emit_status(instance, gpio_idx, duration_us, JERRYCAN_GPIO_PULSE_PHASE_COMPLETED, 0, uuid,
                                 JERRYCAN_TIMESTAMP_COMPLETED, jerrycan_cycles_to_board_time_us(low_cycles));
  ```

**Failsafe handler.**
- Directly after its `(void)ll_generic_gpio_write_pin(gpio_pulse.generic_gpio, gpio_pulse.writable_idx, 0);`, add `const uint64_t low_cycles = k_cycle_get_64();`.
- Its emit becomes:

  ```c
      gpio_pulse_emit_status(instance, gpio_idx, duration_us, JERRYCAN_GPIO_PULSE_PHASE_COMPLETED, -ETIME, uuid,
                             JERRYCAN_TIMESTAMP_COMPLETED, jerrycan_cycles_to_board_time_us(low_cycles));
  ```

**`gpio_pulse_start`** gains an out parameter in **both** definitions, the `GPIO_PULSE_SUPPORTED` one and the stub:

```c
static int gpio_pulse_start(uint16_t instance, uint16_t gpio_idx, uint32_t duration_us, uint8_t uuid,
                            uint64_t *asserted_us) {
```

- In the supported definition, inside `if (rc == 0) {` right after `ll_generic_gpio_write_pin(generic_gpio, writable_idx, 1)` succeeds, add `*asserted_us = jerrycan_board_time_us();` as the first statement.
- In the stub, add `ARG_UNUSED(asserted_us);`.

**Handler.** In `jerrycan_generic_gpio_pulse_handler`:
- Add `uint64_t asserted_us = 0;`.
- Call `gpio_pulse_start(instance, gpio_idx, duration_us, uuid, &asserted_us)`.
- The rejected emit gets `JERRYCAN_TIMESTAMP_TX_QUEUE, jerrycan_board_time_us()`.
- The asserted emit gets `JERRYCAN_TIMESTAMP_PHYSICAL_START, asserted_us`.

- [ ] **Step 6: Door module.** In `door_sensor.c`'s `door_sensor_transmit_msg`, replace `jerrycan_tx(&msg, K_NO_WAIT);` with:

```c
    jerrycan_tx_timed(&msg, JERRYCAN_TIMESTAMP_SAMPLED, 0, jerrycan_board_time_us(), K_NO_WAIT);
```

- [ ] **Step 7: Build both boards.** The magnet build proves the shared drivers and modules still compile where timing is off:

```bash
$RIG "cd ~/Documents/reachAQ-hardware && $ENV && $BUILD_FW 2>&1 | grep -iE 'warning|error' | head; west build --build-dir /tmp/t2-magnet --sysbuild --board cerebellumlab_magnet_module --pristine always firmware/magnet_module 2>&1 | grep -iE 'warning|error|FAILED' | head; grep -E '^CONFIG_LIB_JERRYCAN_TIMING' /tmp/t2-magnet/magnet_module/zephyr/.config || echo 'magnet: timing not set (expected)'"
```

Expected:
- pellet builds clean
- magnet builds clean
- magnet prints `timing not set (expected)`, or `CONFIG_LIB_JERRYCAN_TIMING is not set`

If the magnet build fails on **unchanged** code, it was already broken. Report it; do not fix it here.

- [ ] **Step 8: Commit**

```bash
git add firmware/drivers/tone_generator firmware/drivers/pressure_sensor firmware/lib/jerrycan/modules/tone.c firmware/lib/jerrycan/modules/pressure.c firmware/lib/jerrycan/modules/gpio.c firmware/lib/jerrycan/modules/door_sensor.c
git commit -m "feat(jerrycan): stamp tone, pressure, pulse and state frames with when they happened

Tone status carries the STIM marker edge the NI-DAQ records, pressure its ADC
sample time, pulse status its two edges.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Hardware-in-the-loop check on the rig

**Files:**
- Create: `firmware/lib/jerrycan/test/hil_timing_check.py`. This is a development tool. Its findings decide whether it later moves into reachAQ's `qualify_pellet_firmware.py`; that is the user's call, not this plan's.

**Interfaces:**
- Consumes the v2.4.0-dev image built in Task 5, and the rig's v2.3.0 bundle at `~/Documents/reachAQ-hardware/dist/reachaq-pellet-v2.3.0-linux-x86_64/`.

- [x] **Step 1: Write the checker.** It uses only the standard library, so it runs with the rig's system `python3`.

> **Done; the committed file is authoritative.** The checker was committed in deb4bea and corrected in 2086ce3. The fixes were: a TX_QUEUE kind and queue-time match on the TIME_SYNC response, RTT only on same-source stamps, clap jitter judged only on hardware stamps, `--clapper-address`, and no claps when a timekeeper is already clapping. The listing below is the original draft and lacks those fixes. Do not re-create the file from it.

```python
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
            return boot_id
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
```

- [ ] **Step 2: Baseline against v2.3.0, which is what the rig runs now.** Sync, then run:

```bash
$RIG "cd ~/Documents/reachAQ-hardware && python3 firmware/lib/jerrycan/test/hil_timing_check.py"
```

Expected: `capabilities` FAILS with `caps=0x00000004 ... trailer=no` and the script exits 1. This proves the checker can tell v2.3.0 from v2.4.0. After a failed `capabilities` check the checker stops, so it sends no TIME_SYNC requests and no SYNC_CLAP frames.

- [ ] **Step 3: STOP. Get explicit user approval to flash.** Ask the user, and report their answer:

  > Flash the v2.4.0-dev pellet image onto christielab10's board (address 0) now?
  > - The flash takes about a minute.
  > - **reachAQ on the rig must stay closed until the board is restored.** The dev image reports version 2.3.0 (its version comes from `git describe`), so reachAQ's allowlist would not stop it. But the image advertises `time_sync`, so reachAQ's `open()` runs 4 back-to-back time syncs, and its board-clock fit currently raises on those closely spaced observations and aborts `open()`. The reachAQ fix is planned as T3 Task 1.
  > - The board is restored to the v2.3.0 release right after the check.

  Do not proceed without a yes.

- [ ] **Step 4: Flash, check, restore.**

```bash
$RIG "cd ~/Documents/reachAQ-hardware && tools/flash_pellet_module.sh --firmware /tmp/t2-pellet/pellet_module/zephyr/zephyr.signed.bin --updater /tmp/t2-updater/software/jerrycan_updater/jerrycan_updater --allow-unqualified"
$RIG "cd ~/Documents/reachAQ-hardware && python3 firmware/lib/jerrycan/test/hil_timing_check.py --play-tone"
```

The flash script asks the operator to type `FLASH`. Run it in an interactive terminal (for example `mcp__terminal__run_in_terminal`), not with `--yes`.

Expected: every check PASSES and the script exits 0. `claps` reports `NOT JUDGED`, with its software-stamp residual: this PEAK card puts no hardware stamp on echoes and batches its RX interrupts, so it cannot measure clap stamps to 20 µs. Clap accuracy is measured against the NI-DAQ by spec acceptance 1. The approved run also adds a clock soak, the round-trip report and a T3 bench, through `rig_run.sh` in the T2 ledger folder. If anything fails, record the full output before restoring. Record the `boot_id` from the `capabilities` line.

**Then restore, whether the check passed or failed:**

```bash
$RIG "cd ~/Documents/reachAQ-hardware/dist/reachaq-pellet-v2.3.0-linux-x86_64 && ./reachaq-update --2.3.0"
$RIG "cd ~/Documents/reachAQ-hardware && python3 firmware/lib/jerrycan/test/hil_timing_check.py"
```

Expected: the v2.3.0 baseline result from Step 2. `capabilities` FAILS with `caps=0x00000004 ... trailer=no`, the script exits 1, and the reported `boot_id` differs from the dev run's. This is the restore check: the dev image would report `caps=0x00000017` with a trailer, and a different `boot_id` proves the board rebooted into the restored image.

Secondary check only:

```bash
$RIG "cd ~/Documents/reachAQ && conda run -n reachaq python tools/hardware/validate_can_hardware.py --transport socketcan --channel can0 --action version"
```

Expected: the version reads 2.3.0. This cannot tell the dev image from the release, because the dev image also reports 2.3.0 (its version comes from `git describe`). Do not run it while the dev image is on the board: it opens the board through reachAQ, which fails on a board that advertises `time_sync` (see Step 3).

**Finally, put the rig checkout back on the ref recorded in Task 0:**

```bash
$RIG "cd ~/Documents/reachAQ-hardware && git switch --detach 08c1b41 && git status --short | wc -l"
```

Expected: `0`.

- [ ] **Step 5: Record the measurements.** The checker is already committed (Step 1). Paste the Step 3 PASS lines into the SDD ledger and into Task 7's release notes. Commit the checker again only if the run forced a change to it:

```bash
git add firmware/lib/jerrycan/test/hil_timing_check.py
git commit -m "test(jerrycan): <what the hardware run showed the checker needed>

On christielab10 it measured: <paste the PASS lines>.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Release notes and the reachAQ allowlist handoff

**Files:**
- Create: `docs/releases/v2.4.0.md`
- No reachAQ edits in this plan.

- [ ] **Step 1: Write `docs/releases/v2.4.0.md`.** Follow `docs/releases/v2.3.0.md`'s structure, with these sections:
  - **What changed:**
    - the timing trailer on every frame with room
    - TIME_SYNC responder
    - SYNC_CLAP listener (`SYNC_CLAP_OBSERVED`)
    - physical times on tone, pressure, pulse, GPIO and door frames
    - capabilities `0x00000017` (`timing_trailer | time_sync | finite_stim3_pulse | sync_listener`)
  - **What did not change:** pins, IDs, payloads, rates, behaviour, wire schema 1; the magnet module's wire format
  - **Compatibility:** v2.4.0 is **not** safe on current reachAQ. It advertises `time_sync`, so reachAQ's `open()` runs 4 back-to-back time syncs, and reachAQ's board-clock fit raises on those closely spaced, noisy observations and aborts `open()`: current reachAQ cannot open a v2.4.0 board. reachAQ's board-clock robustness fix (T3 Task 1) must ship first. Until it has, the allowlist entry below is not added and v2.4.0 is not tagged or deployed. reachAQ already decodes the trailer. The board sends `SYNC_CLAP_OBSERVED` only when a timekeeper claps.
  - **Evidence:** the Task 6 PASS output. Note that the spec's pellet-regression acceptance, reachAQ's `tools/hardware/qualify_pellet_firmware.py --expect-version 2.4.0`, runs against the tagged release bundle per the deployment doc, after the user tags. It is not run on the dev image.
  - **Allowlist entry for reachAQ:** add this to `config/pellet-firmware-compatibility.yaml` after rig qualification. Do not add it before reachAQ's board-clock robustness fix (T3 Task 1) has shipped and the release is tagged.

    ```yaml
      - version: 2.4.0
        wire_schema_version: 1
        required_capabilities:
          - finite_stim3_pulse
          - timing_trailer
          - time_sync
        optional_capabilities:
          - sync_listener
        hardware_release: v2.4.0
        tested_reachaq_release: pre-release-devel
        qualification_status: rollout_pending
        qualification_date: null
    ```

    `sync_listener` is optional because reachAQ consumes claps only after T3.

- [ ] **Step 2: Commit and report.** Do not tag; the release tag and build follow `docs/pellet-firmware-release-and-deployment.md` and are the user's step, and they wait until reachAQ's board-clock robustness fix (T3 Task 1) has shipped.

```bash
git add docs/releases/v2.4.0.md
git commit -m "docs: release notes for pellet firmware v2.4.0

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Report:
- the branch and its commits
- the Task 0 and Task 5 sizes
- the Task 6 output
- that the rig is back on v2.3.0 and `08c1b41`
- that `get_can_node_id()` reading the board type from devicetree, listed in spec §5, moved to H1, because only the IO module needs it

# H1a: Independent timed pulses per TTL output — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Each of the IO module's 8 TTL outputs can run its own `GPIO_PULSE` at the same time. A second pulse on the *same* output is still refused with `-EBUSY`. The pellet and magnet keep one pulse board-wide.

**Architecture:** Keep the one 1 MHz alarm channel the shared pulse code already uses (TIM16, `ll,gpio-pulse-counter`). Replace the single in-flight record in `gpio.c` with a small table of in-flight pulses, held in a pure helper with no Zephyr include, so it can be host-tested. The alarm is always armed for the soonest return-low edge, or for the longest allowed hop. No new timers and no wire changes.

**Why not more timer channels:** TIM16 has one channel, so a hardware-channel design needs a 4-channel timer per 4 outputs, and it still does not reach all 8 outputs without two timers. It would also tie the design to pins and timers on the PCB (H2). A software deadline table needs none of that. Its cost is that edges falling in the same interrupt go low one after another, a few µs apart, and a pulse start can delay an edge by the time its short lock is held. Spec acceptance 2 allows ±20 µs, and hardware Task 11 measures it.

**Tech Stack:** Zephyr 3.7, C11, the counter API, the host CMake/ctest project in `firmware/lib/jerrycan/test`.

**Spec:** `docs/superpowers/specs/2026-10-07-io-module-timing-design.md` (§7 "TTL out", §11 acceptance 2). User decision 2026-11-05: plan concurrent pulses rather than document the one-pulse limit.

**Branch and worktree:** `feat/io-module-firmware`, worktree `C:\Users\reynoben\Documents\reachAQ-hardware-io`. Runs after H1's final fix and before H1 Task 8's verification, so that Task 8 and the hardware tasks cover it.

## Global Constraints

- **Wire contract unchanged.** `GPIO_PULSE` 0x21 `<BHI`; `GPIO_PULSE_STATUS` 0x22 `<BHIBi`, with `PHYSICAL_START` on assert and `COMPLETED` on return-low. One status pair per pulse, matched by `uuid` and `gpio_idx`. The host decodes these per message and needs no change.
- **Refusals unchanged in kind:**
  - `-EBUSY`: a pulse is already in flight on this output (on the pellet and magnet: on any output);
  - `-EPERM`: not pulse-capable;
  - `-EINVAL`: duration outside 100 µs to 5 s.
- **Pellet and magnet behaviour unchanged.** `CONFIG_LIB_JERRYCAN_GPIO_PULSE_MAX_CONCURRENT` defaults to 1, which keeps "one pulse board-wide". The IO module sets it to 8. Report the pellet's size delta.
- **A line left asserted is the worst outcome.** Every in-flight pulse stays covered by the failsafe (`GPIO_PULSE_FAILSAFE_MARGIN_MS`), the failsafe is armed before the line moves, and no path can strand a line high.
- **Lock discipline.** `gpio_pulse_lock` is a spinlock that masks priority 0. Hold it for table work and pin writes only. Queue messages outside it, as today.
- **Style and builds:**
  - `firmware/.clang-format`, 120 columns, LF. Do not use GNU `sed -i`.
  - Builds and host tests run only on the rig, in its designated checkout, as in H1.
  - Commits end with the harness-provided Co-Authored-By line. No push or merge. Never flash anything.

## Review Focus

These are the inputs the spec implies and that are easy to leave untested. Each is pinned by a test in Task 1 or a check in Task 2.

1. **Two pulses that end in the same tick, or within one interrupt's latency.** Both lines go low in that interrupt and both report `COMPLETED`. Neither is lost.
2. **A pulse that starts while another is in flight, including just before the other's alarm fires.** The running pulse's length does not change by more than the start's lock time, and no alarm is left armed twice (`counter_set_channel_alarm` returns `-EBUSY` if one is).
3. **A 5 s pulse alongside 100 µs pulses.** Long pulses chain hops (the counter is 16-bit, and the hop limit is half the range), and short ones cut the hop short. Time charged against each pulse does not drift.
4. **A second pulse on the same output, and a 9th concurrent pulse.** `-EBUSY` both times, with nothing else disturbed.
5. **The failsafe with several pulses in flight.** It forces only overdue lines low, keeps the others running, and re-arms for the next.

---

### Task 1: The in-flight pulse table (pure, host-tested)

**Files:**
- Create: `firmware/lib/jerrycan/gpio_pulse_table.{c,h}` (no Zephyr include)
- Create: `firmware/lib/jerrycan/test/gpio_pulse_table_test.c`
- Modify: `firmware/lib/jerrycan/test/CMakeLists.txt`

**Interfaces, produced for Task 2:**

```c
struct gpio_pulse_entry {
    bool in_flight;
    uint16_t gpio_idx;
    uint8_t writable_idx;
    uint8_t instance;
    uint8_t uuid;
    uint32_t duration_us;
    uint32_t ticks_remaining;
};

/* All functions take the entry array and its capacity; the caller owns the lock. */
int gpio_pulse_table_add(struct gpio_pulse_entry *t, size_t cap, const struct gpio_pulse_entry *e);
    /* 0, or -EBUSY when the output is already in flight or the table is full */
void gpio_pulse_table_charge(struct gpio_pulse_entry *t, size_t cap, uint32_t elapsed_ticks);
    /* subtracts elapsed from every in-flight entry, clamped at 0 */
bool gpio_pulse_table_take_due(struct gpio_pulse_entry *t, size_t cap, struct gpio_pulse_entry *out);
    /* removes and returns one entry at 0 remaining; false when none */
uint32_t gpio_pulse_table_next_hop(const struct gpio_pulse_entry *t, size_t cap, uint32_t max_hop);
    /* min(soonest remaining, max_hop); 0 when nothing is in flight */
```

- [ ] **Step 1: Write the failing tests** in `gpio_pulse_table_test.c`, in the existing host-test style:
  - `test_add_refuses_same_output_and_full_table`: same `gpio_idx` gives `-EBUSY`; a different one gives 0; capacity + 1 gives `-EBUSY`. With capacity 1, any second pulse gives `-EBUSY` (the pellet rule).
  - `test_next_hop_is_the_soonest_edge_capped_at_max`: entries 5000 and 100, max 32768 → 100; entry 100000 alone → 32768; empty → 0.
  - `test_charge_clamps_and_due_entries_come_out_once`: charging past an entry leaves 0; `take_due` returns it once, then false; others keep their remainder.
  - `test_two_edges_in_one_charge_both_come_out`: two entries at 100 and 100; charge 100; two `take_due` calls succeed.
  - `test_long_pulse_alongside_short_ones_does_not_drift`: simulate 5 000 000 ticks with hops from `next_hop` (max 32768), plus 10 entries of 100-5000 ticks started at varied times, charging actual elapsed ticks with 0-3 ticks of random late skew per hop. Each entry must end within its skew bound of its true deadline. No hop may be 0 while anything is in flight.
- [ ] **Step 2: Run them and see them fail.** `$HOST_TEST`. Expected: the new test fails to build (no `gpio_pulse_table.h`).
- [ ] **Step 3: Implement** `gpio_pulse_table.{c,h}` per the interfaces.
- [ ] **Step 4: Run them.** `$HOST_TEST`. Expected: all tests pass, including `gpio_pulse_table_test`.
- [ ] **Step 5: Commit** as `feat(jerrycan): a table of in-flight GPIO pulses, host-tested`.

### Task 2: Use the table in `gpio.c`; the IO module allows 8

**Files:**
- Modify: `firmware/lib/jerrycan/modules/gpio.c`, `firmware/lib/jerrycan/Kconfig`, `firmware/lib/jerrycan/CMakeLists.txt`
- Modify: `firmware/io_module/prj.conf`, `firmware/io_module/README.md`
- Modify: `docs/superpowers/specs/2026-10-07-io-module-timing-design.md` §7 "TTL out", in the timing worktree (one sentence on per-output pulses)

**Interfaces:** consumes Task 1's table.

- [ ] **Step 1: Kconfig.**

```kconfig
config LIB_JERRYCAN_GPIO_PULSE_MAX_CONCURRENT
    int "Timed GPIO pulses in flight at once, at most one per output"
    default 1
    range 1 32
    depends on LIB_JERRYCAN
```

  IO module `prj.conf`: `CONFIG_LIB_JERRYCAN_GPIO_PULSE_MAX_CONCURRENT=8`. BUILD_ASSERT that it does not exceed the number of writable pins where that is known at build time. Otherwise, document the capacity rule.

- [ ] **Step 2: `gpio_pulse_alarm_cb` (counter ISR).** Under the lock:
  1. Compute `elapsed = ticks − last_cc`, set `last_cc = ticks`, and call `gpio_pulse_table_charge`.
  2. `take_due` in a loop: write each line low, stamp it, and collect it in a local array (capacity = the Kconfig maximum).
  3. Compute `next_hop` and re-arm when anything is still in flight.
  4. Reschedule or cancel the failsafe.

  Outside the lock: one `jerrycan_generic_gpio_tx()` if anything ended, and one `COMPLETED` status per ended pulse, each with its own stamp.

- [ ] **Step 3: `gpio_pulse_start`.** Validation is unchanged. Under the lock:
  1. If other pulses are in flight:
     - cancel the alarm;
     - read the counter and charge `now − last_cc`;
     - set `last_cc = now`;
     - take out and end any entry now due, exactly as the ISR does, so a pulse whose alarm was just cancelled cannot be lost.
  2. `gpio_pulse_table_add`; on `-EBUSY`, re-arm for what is in flight and return `-EBUSY`.
  3. Arm the failsafe.
  4. Write the line high.
  5. Re-arm for `next_hop` and stamp the asserted edge.
  6. On a failed write or arm, undo only this entry and leave the others armed.

  Check in `deps/zephyr/drivers/counter/counter_ll_stm32_timer.c` what `cancel` does to an alarm flag already pending, and how the relative-alarm late detection behaves below half the range (`:222-236`). Cite the lines in the report.

- [ ] **Step 4: Failsafe.** One delayable work item covers the table. It is scheduled for the soonest remaining deadline plus `GPIO_PULSE_FAILSAFE_MARGIN_MS`. The handler forces overdue entries low, sends each the same status today's handler sends, and reschedules for the rest. Entries not overdue are left alone. While anything remains in flight, the alarm must stay armed.

- [ ] **Step 5: Docs.**
  - IO README, TTL section: per-output pulses; `-EBUSY` only for the same output; same-interrupt edges go low a few µs apart (estimate, unmeasured).
  - Update the comment above the old `gpio_pulse` struct.
  - Spec §7: add per-output pulses on the IO module; the pellet stays board-wide.
  - Host doc `reachAQ/auto-trainer-device/src/autotrainer/device/device_interface.py:482` still reads correctly for the pellet. It says "a second pulse while one is in flight". List it for the host IO-module plan and do not edit it here.

- [ ] **Step 6: Build and test.** Build all three, and run host tests. Expected:
  - IO: 0 warnings, `CONFIG_LIB_JERRYCAN_GPIO_PULSE_MAX_CONCURRENT=8`.
  - Pellet: `=1`, and report the size delta and symbols.
  - Magnet: errors same as baseline.
  - Host: all pass.
  - Pin claims unchanged.

- [ ] **Step 7: Commit** as `feat(io): each TTL output runs its own timed pulse`.

### Hardware checks to add to H1 Task 11

These can run only on a board:
- **Overlap accuracy.** Loop TTL0 and TTL1 to two DIs, and run overlapping pulses of 100 µs to 5 s. Each measured width must be within ±20 µs of commanded, with the shorter one ending first.
- **Coincident ends.** Start pulses so that their ends coincide. Measure the spread between lines on a scope, and confirm every `COMPLETED` arrives.
- **All 8 at once.** Run all 8 at once, repeated, under the DI storm and the analog stream.
- **Refusals.** A same-output overlap and a 9th request are refused with `-EBUSY`, and nothing else is disturbed.
- **Pellet unchanged.** A pellet second pulse is still refused (`qualify_pellet_firmware.py`).

#pragma once

#include <stdint.h>

/*
    The board clock: the Cortex-M DWT cycle counter (CYCCNT), which counts every core clock cycle, extended to 64 bits
    in software. Every board timestamp is taken from it.

    It replaces k_cycle_get_64(). With a tickless kernel, Zephyr 3.7's SysTick driver drops the cycles that pass while
    it reprograms the timer, so that count runs slow by an amount that depends on the workload, and drifts against the
    hardware timers that run from the same clock. CYCCNT is never reprogrammed and loses nothing.

    CYCCNT is not reset at boot, and a reset that keeps power may leave it running, so the first reading after boot is
    anywhere below 2^32 cycles (25.3 s at 170 MHz) rather than 0. It is 32 bits and wraps every 25.3 s at 170 MHz; a
    keep-alive timer reads it every 10 s so that no wrap goes uncounted.
*/

/* What the extension remembers between readings: CYCCNT as last read, and how many times it has wrapped. */
typedef struct {
    uint32_t last;
    uint32_t wraps;
} board_clock_ext_t;

/*
    One extension step: CYCCNT reading `now`, extended to 64 bits. A reading below the last one means the counter
    wrapped in between, so it is counted once; that holds only if consecutive readings are less than one wrap apart.
    The caller serialises the calls. Plain C with no Zephyr dependency, so the host-run tests can build it. Start from
    a zeroed state.
*/
static inline uint64_t board_clock_extend(board_clock_ext_t *ext, uint32_t now) {
    if (now < ext->last) {
        ext->wraps++;
    }
    ext->last = now;

    return ((uint64_t)ext->wraps << 32) | now;
}

/*
    Core clock cycles on the board clock, 64 bits, monotonic across wraps. Safe in any context, including an interrupt
    at priority 0: the read and its extension run with interrupts locked.
*/
uint64_t board_clock_cycles(void);

/* A board_clock_cycles() reading in whole microseconds, rounded down. */
uint64_t board_clock_cycles_to_us(uint64_t cycles);

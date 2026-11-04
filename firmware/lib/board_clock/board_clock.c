/**
 * @file board_clock.c
 * @brief The board clock (board_clock.h): the DWT cycle counter, extended to 64 bits.
 *
 * Every reader takes the same lock around the CYCCNT read and the extension step, so the extension state only ever
 * moves forward with CYCCNT, whether the reader is a thread, a priority-0 interrupt or the keep-alive.
 */

#include "board_clock.h"

#include <soc.h>
#include <stm32_ll_system.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

/* A wrap is 2^32 cycles, 25.26 s at 170 MHz; a reading every 10 s counts each one with more than half a wrap spare */
#define BOARD_CLOCK_KEEPALIVE_S 10U

#define BOARD_CLOCK_CYCLES_PER_US (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / USEC_PER_SEC)

/*
    CYCCNT counts core clock (HCLK) cycles, and the conversion divides by CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC. Zephyr
    3.7 takes HCLK on STM32 from the rcc node's clock-frequency: it sets SystemCoreClock to this Kconfig value, whose
    default is that property, and computes the flash wait states from it as SYSCLK. With a core prescaler of 1 all of
    these are the one rate.
*/
BUILD_ASSERT(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC == DT_PROP(STM32_CLOCK_CONTROL_NODE, clock_frequency),
             "The board clock converts at CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC, which must be HCLK");
BUILD_ASSERT(STM32_CORE_PRESCALER == 1, "The board clock needs HCLK, which CYCCNT counts, to equal SYSCLK");
BUILD_ASSERT(!IS_ENABLED(CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME),
             "The board clock needs CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC to be the actual rate");
/* At a whole number of MHz the conversion is one exact divide, which is what the CAN RX interrupt should pay */
BUILD_ASSERT(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC % USEC_PER_SEC == 0,
             "The board clock needs a core clock that is a whole number of MHz");
BUILD_ASSERT((uint64_t)BOARD_CLOCK_KEEPALIVE_S * CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC <= (1ULL << 32) / 2,
             "The keep-alive must read CYCCNT at least twice per wrap");
/* Stop mode halts the core clock, and CYCCNT with it */
BUILD_ASSERT(!IS_ENABLED(CONFIG_PM), "The board clock needs the core clock to run in every idle state");
/* Zephyr's DWT timing backend zeroes CYCCNT in timing_init() and timing_start(), and stops it in timing_stop() */
BUILD_ASSERT(!(IS_ENABLED(CONFIG_TIMING_FUNCTIONS) && IS_ENABLED(CONFIG_CORTEX_M_DWT)),
             "Timing functions on the DWT would reset the board clock");
/* irq_lock() does not mask a zero-latency interrupt, which could then read between another reader's read and update */
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZERO_LATENCY_IRQS), "The board clock's lock must mask every interrupt that reads it");
/* Zephyr's dwt_access() unlocks the DWT (LAR) only on a Cortex-M7; the Cortex-M4 DWT has no lock */
BUILD_ASSERT(IS_ENABLED(CONFIG_CPU_CORTEX_M4), "The board clock is written for the Cortex-M4 DWT");

static struct k_spinlock board_clock_lock;
static board_clock_ext_t board_clock_ext;

uint64_t board_clock_cycles(void) {
    k_spinlock_key_t key = k_spin_lock(&board_clock_lock);
    const uint64_t cycles = board_clock_extend(&board_clock_ext, DWT->CYCCNT);
    k_spin_unlock(&board_clock_lock, key);

    return cycles;
}

uint64_t board_clock_cycles_to_us(uint64_t cycles) { return cycles / BOARD_CLOCK_CYCLES_PER_US; }

static void board_clock_keepalive(struct k_timer *timer) {
    ARG_UNUSED(timer);

    (void)board_clock_cycles();
}

K_TIMER_DEFINE(board_clock_keepalive_timer, board_clock_keepalive, NULL);

/*
    Before anything reads the clock: every driver and JerryCAN stamps at POST_KERNEL or later. CYCCNT keeps whatever
    value it has; only its enables are set.
*/
static int board_clock_init(void) {
    /* TRCENA powers the DWT; the Cortex-M4 needs no unlock before CYCCNTENA */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /*
        Zephyr's idle is WFI, which is Sleep mode. Without DBG_SLEEP the core's HCLK stops in Sleep, and CYCCNT with
        it. Zephyr 3.7's stm32g4_init() also sets this bit, to allow reflashing; the board clock does not rely on that.
    */
    LL_DBGMCU_EnableDBGSleepMode();

    return 0;
}

/*
    POST_KERNEL is the first level at which Zephyr allows kernel services such as k_timer_start(); the system clock
    driver starts at PRE_KERNEL_2. Priority 0 starts it before every driver that stamps. Readings before its first
    expiry are at most boot plus 10 s apart, well under a wrap.
*/
static int board_clock_keepalive_start(void) {
    k_timer_start(&board_clock_keepalive_timer, K_SECONDS(BOARD_CLOCK_KEEPALIVE_S), K_SECONDS(BOARD_CLOCK_KEEPALIVE_S));

    return 0;
}

SYS_INIT(board_clock_init, PRE_KERNEL_1, 0);
SYS_INIT(board_clock_keepalive_start, POST_KERNEL, 0);

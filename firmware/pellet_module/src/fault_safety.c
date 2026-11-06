/**
 * @file fault_safety.c
 * @brief What takes the shared stepper enable off after a fatal firmware error.
 *
 * Zephyr 3.7's default fatal error handler logs and halts with interrupts locked. With interrupts locked, the DMA
 * completion that stops a stepper's timer cannot run, so STEP likely keeps pulsing at its last rate until the board is
 * power cycled (reasoned from the code; not measured), and with the drivers enabled the motors keep stepping. A fatal
 * error (a CPU exception, a stack overflow, k_oops(), k_panic(), an unhandled interrupt) calls
 * k_sys_fatal_error_handler(); the one here takes EN off first, then halts exactly as Zephyr's does.
 *
 * EN only exists on a v1.3 board. Without an enable net, stepper_power_force_off_for_fault() returns at once and
 * touches no pin, so a v1.2 board halts exactly as it did before this file. That is also why the handler halts and does
 * not reset: a reset would change v1.2 behaviour.
 *
 * EN goes off before anything is written to the UART. Zephyr reports the fault, a few lines, before it calls the
 * handler, and with deferred logging that only queues them. In immediate mode they would go out the UART first, so the
 * BUILD_ASSERT below keeps the log deferred.
 *
 * Built only into the pellet app, so the magnet and IO module images keep Zephyr's default handler. Nothing can
 * exercise the handler off target: it is compile-checked only until it is triggered on a board.
 */

#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#include "stepper_power.h"

LOG_MODULE_REGISTER(fault_safety, LOG_LEVEL_INF);

BUILD_ASSERT(IS_ENABLED(CONFIG_LOG_MODE_DEFERRED),
             "The fatal error handler takes the stepper enable off after Zephyr reports the fault. With deferred "
             "logging that only queues the report. In immediate mode the report goes out the UART first, and EN would "
             "stay on for all of it. CONFIG_LOG_MODE_DEFERRED must stay on");

/*
    Replaces Zephyr's weak handler. Runs in the faulting context with interrupts locked (z_fatal_error() locks them
    before it calls this), after Zephyr has queued its own report of the fault; it never returns.

    The body is Zephyr's (LOG_PANIC(), "Halting system", then k_fatal_halt(), which is arch_system_halt()) with EN taken
    off first. stepper_power_force_off_for_fault() takes no spinlock and does one GPIO write, since a spinlock held by
    the faulting context would never be released.
*/
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf) {
    ARG_UNUSED(esf);

    stepper_power_force_off_for_fault();

    LOG_PANIC();
    LOG_ERR("Halting system");
    k_fatal_halt(reason);
}

#pragma once

#include "motor_math.h"
#include "motor_motion_workq.h"

/* ***** Helper Functions ***** */

/*
 * Set the current position of the stepper motor. Useful especially when the motor is at 'home'
 * to set this position as zero without continuing to move the motor.
 */
void motor_motion_stepper_set_current_position(stepper_motor_context_t *context, float position);

/*
 * Set the current position of the servo motor. It does this by setting the angle_adjustment.
 */
void motor_motion_servo_set_current_position(servo_motor_context_t *context, float position);

/*
 * Stop the motor by stopping the timer and the DMA. This does not affect the internal state of the motion library.
 */
void stepper_motor_stop(const struct device *dev);
void servo_motor_stop(const struct device *dev);

/*
 * Stop every stepper that is moving: stop the DMA, disable the timer's output channel, stop its
 * counter, cancel the table calculation and purge the block queue so the motor can move again.
 * Steppers that are not moving are left untouched. Does not block. Returns a mask of the steppers
 * stopped, bit n for stepper n.
 */
uint8_t steppers_stop_moving(void);

/*
 * Stop all motors that are moving! Uses a static list of all motors to stop them, so is very fast.
 */
void motors_all_stop(void);

/*
 * Trigger the emergency stop (e-stop) functionality. Calls `motors_all_stop` and
 * sets the flags in the relevant context structs.
 */
void trigger_e_stop(void);

/*
 * Get the stepper motor device by its ID. This is useful for the shell commands and CAN commands.
 */
const struct device *stepper_motor_by_id(size_t id);

/*
 * Get the servo motor device by its ID. This is useful for the shell commands and CAN commands.
 */
const struct device *servo_motor_by_id(size_t id);

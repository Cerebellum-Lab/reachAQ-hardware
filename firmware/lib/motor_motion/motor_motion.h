#pragma once

#include "motor_math.h"
#include "motor_motion_workq.h"

/* ***** Helper Functions ***** */

/*
 * Set the radius of the rotor of any motor.
 */
void motor_motion_set_radius(motor_motion_profile_t *motion_profile, float radius);

/*
 * Simple helper function to convert meters to steps.
 */
float motor_motion_stepper_length_to_steps(const stepper_motor_context_t *context, float length);

/*
 * Set the current position of the stepper motor. Useful especially when we have 'home' and wish
 * to set this position as zero without continuing to move the motor.
 */
void motor_motion_stepper_set_current_position(stepper_motor_context_t *context, float position);

/*
 * Simple helper function to convert meters to degrees (for servos).
 */
float motor_motion_servo_length_to_degrees(const servo_motor_context_t *context, float length);

/*
 * Set the current position of the stepper motor. It does this by setting the angle_adjustment.
 */
void motor_motion_servo_set_current_position(servo_motor_context_t *context, float position);

/*
 * Stop the motor by stopping the timer and the DMA. This does not affect the internal state of the motion library.
 */
void stepper_motor_stop(const struct device *dev);
void servo_motor_stop(const struct device *dev);

/*
 * Stop all motors! Uses a static list of all motors to stop them, so is very fast.
 */
void motors_all_stop(void);
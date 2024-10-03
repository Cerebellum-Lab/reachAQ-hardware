#pragma once

#include <stdint.h>
#include <zephyr/device.h>

/*
 * These parameters are more constant than the positions so abstracted out here. If the `float`
 * parameters are lower than or equal to 0.0f or if the `uint32_t` parameters are equal to
 * `(uint32_t) -1`, then those parameters are unchanged.
 * Returns -ENODEV if the device is not found among the static context structs.
 */
int servo_set_parameters(const struct device *dev, float max_velocity, float max_acceleration,
                         uint32_t servo_multiplier, uint32_t servo_offset);

/*
 * Move to the position specified, using the motion profiles in `motor_math.*`.
 *
 * Returns -ENODEV if the device is not found in the list.
 * Returns -EBUSY if another motion profile is already running.
 */
int servo_move_to_position(const struct device *dev, float target_position);

/*
 * These parameters are usually constant across movements of the motor, so we abstract them to a
 * separate function. If any of these parameters have values lower than or equal to 0.0f,
 * it is unchanged.
 * Returns -ENODEV if the device is not found among the static context structs.
 */
int stepper_set_parameters(const struct device *dev, float max_velocity, float max_acceleration, float min_step,
                           float steps_per_revolution);

/*
 * Move to the position specified, using the motion profiles in `motor_math.*`.
 *
 * Returns -ENODEV if the device is not found in the list.
 * Returns -EBUSY if another motion profile is already running.
 */
int stepper_move_to_position(const struct device *dev, float target_position);

/*
 * Choose some "impossible position" which is very large and approach it
 * or its negative, depending on `forward` up to a very slow velocity. We will
 * never approach the other end of this curve, so will approach this velocity
 * and stay there until we hit the limit switch.
 */
int stepper_go_home_slowly(const struct device *dev, bool forward);

/*
 * Cancel all work on the motor.
 */
void stepper_cancel_all_work(const struct device *dev);
void servo_cancel_all_work(const struct device *dev);

/*
 * Zero out the internal state of this library, as after hitting a limit switch. (Presumably after the
 * motors have been stopped.)
 */
void stepper_set_position_to_zero(const struct device *dev);
void servo_set_position_to_zero(const struct device *dev);

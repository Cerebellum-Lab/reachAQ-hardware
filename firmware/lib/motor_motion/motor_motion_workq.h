#pragma once

#include <stdatomic.h>
#include <stdint.h>
#include <zephyr/device.h>

#include "motor_callbacks.h"
#include "motor_math.h"
#include "servo.h"
#include "stepper.h"

/* ***** Struct Declarations and Defaults ***** */
#define SERVO_BUFFER_SIZE 256
#define STEPPER_BUFFER_SIZE 1024
#define BUFS_PER_MOTOR 2

typedef uint32_t servo_buffer_set[BUFS_PER_MOTOR][SERVO_BUFFER_SIZE];
typedef uint32_t stepper_buffer_set[BUFS_PER_MOTOR][STEPPER_BUFFER_SIZE];

struct servo_work_context {
    const struct device *dev;
    servo_motor_context_t context;
    servo_buffer_set buffers;
    size_t current_buffer;
    ssize_t last_calculation_ret;
    atomic_flag e_stop_triggered;
    bool motion_done;
    bool motion_calculation_done;
    struct k_work_delayable calculation_work;
    struct k_work_delayable submission_work;
    ll_servo_cb_t servo_cb;
};

struct stepper_work_context {
    const struct device *dev;
    stepper_motor_context_t context;
    stepper_buffer_set buffers;
    size_t current_buffer;
    ssize_t last_calculation_ret;
    atomic_flag e_stop_triggered;
    _Atomic enum {
        NOT_HOMING,
        HOMING_TOWARDS_LIMIT_SWITCH,
        MOVING_FROM_LIMIT_SWITCH,
    } homing;
    _Atomic ll_stepper_dir_t homing_direction;
    bool motion_done;
    bool motion_calculation_done;
    struct k_work_delayable calculation_work;
    struct k_work_delayable check_driver_work;
    ll_stepper_cb_t stepper_cb;
};

/**
 * These parameters are more constant than the positions so abstracted out here. If the `float`
 * parameters are lower than or equal to 0.0f, then those parameters are unchanged.
 *
 * @retval -ENODEV if the device is not found among the static context structs.
 */
int servo_set_parameters(const struct device *dev, float max_velocity, float max_acceleration, float min_angle_pwm,
                         float max_angle_pwm);

/*
 * Set the minimum and maximum angles of a servo. Both must be set at the same time.
 */
int servo_set_angle_parameters(const struct device *dev, const float min_angle, const float max_angle);

/**
 * Move to the position specified, using the motion profiles in `motor_math.*`.
 *
 * @retval -ENODEV if the device is not found in the list.
 * @retval -EBUSY if another motion profile is already running.
 */
int servo_move_to_position(const struct device *dev, float target_position);

/*
 * Move relative to the current position. Wrapper around `servo_move_to_position`.
 */
int servo_move_relative(const struct device *dev, float delta_position);

/**
 * These parameters are usually constant across movements of the motor, so we abstract them to a
 * separate function. If any of these parameters have values lower than or equal to 0.0f,
 * it is unchanged.
 *
 * @retval -ENODEV if the device is not found among the static context structs.
 */
int stepper_set_parameters(const struct device *dev, float max_velocity, float max_acceleration, float min_step,
                           float steps_per_revolution);

/**
 * Move to the position specified, using the motion profiles in `motor_math.*`.
 *
 * @retval -ENODEV if the device is not found in the list.
 * @retval -EBUSY if another motion profile is already running.
 */
int stepper_move_to_position(const struct device *dev, float target_position);

/*
 * Move relative to the current position. Wrapper around `stepper_move_to_position`.
 */
int stepper_move_relative(const struct device *dev, float delta_position);

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

/*
 * Cancel all work on the motor.
 */
void servo_cancel_all_work(const struct device *dev);

/*
 * Zero out the internal state of this library, as after hitting a limit switch. (Presumably after the
 * motors have been stopped.) This does not move the motor, just sets the internal "zero point" of this
 * library.
 */
void stepper_set_position_to_zero(const struct device *dev);
void servo_set_position_to_zero(const struct device *dev);

/*
 * Find the work contexts, given the device.
 */
struct servo_work_context *find_servo_context_from_device(const struct device *dev);

/*
 * Find the work contexts, given the device.
 */
struct stepper_work_context *find_stepper_context_from_device(const struct device *dev);

/*
 * Set the per-device radii and update the current position based on the new radius.
 */
void motor_motion_stepper_set_radius(const struct device *dev, float new_radius);

/*
 * Set the per-device radii and update the current position based on the new radius.
 */
void motor_motion_servo_set_radius(const struct device *dev, float new_radius);

/**
 * Get the minimum step size for the stepper motor.
 *
 * @returns 0 on success, -ENODEV if the device is not found.
 */
int motor_motion_stepper_get_min_step(const struct device *dev, float *min_step);

/**
 * Get the number of steps per revolution for the stepper motor.
 *
 * @returns 0 on success, -ENODEV if the device is not found.
 */
int motor_motion_stepper_get_steps_per_revolution(const struct device *dev, float *steps_per_revolution);

/**
 * Get the minimum PWM duration for the minimum angle of the servo motor.
 *
 * @returns 0 on success, -ENODEV if the device is not found.
 */
int motor_motion_servo_get_min_angle_pwm(const struct device *dev, float *min_angle_pwm);

/**
 * Get the maximum PWM duration for the maximum angle of the servo motor.
 *
 * @returns 0 on success, -ENODEV if the device is not found.
 */
int motor_motion_servo_get_max_angle_pwm(const struct device *dev, float *max_angle_pwm);

/**
 * Get the minimum angle of the servo motor.
 *
 * @returns 0 on success, -ENODEV if the device is not found.
 */
int motor_motion_servo_get_min_angle(const struct device *dev, float *min_angle);

/**
 * Get the maximum angle of the servo motor.
 *
 * @returns 0 on success, -ENODEV if the device is not found.
 */
int motor_motion_servo_get_max_angle(const struct device *dev, float *max_angle);

/**
 * Internally invoked by the library during an e-stop so that no motion happens
 * unless the homing procedure is followed.
 */
void set_all_e_stop_flags(void);
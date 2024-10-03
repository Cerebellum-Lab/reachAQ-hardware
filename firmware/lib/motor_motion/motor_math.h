#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifndef M_PI
#define M_PI 3.1415927f
#endif

typedef struct motor_motion_profile {
    /* Parameters from the paper:
     * https://studylib.net/doc/8267759/sinusoidal-velocity-profiles-for-motion-control
     *
     * For stepper motors, these parameters are in units of steps, steps per second, etc.
     * For servo motors, these parameters are in units of degrees, degrees per second, etc.
     */

    float start_pos;
    float end_pos;
    float a_max;
    float v_max;
    float sgn;
    float y_f;    // end position, unsigned
    float y_s;    // half-way position
    float y_a;    // position when acceleration ends
    float v_w;    // velocity of the middle, 'velocity' region of the curve
    float t_o;    // half duration of the 'acceleration' region
    float t_a;    // duration of the acceleration region (and deceleration region)
    float omega;  // angular velocity of the cosine term in the acceleration & deceleration regions
    float k_s;    // positional scaling factor for the cosine term
    float t_k;    // duration of the velocity region
    float t_s;    // half-way time of the velocity region (and the total motion profile)
    float t_t;    // total time of the motion profile

    float radius;  // Radius of whatever the rotor is attached to. This is not used by the motion profile,
                   // but is used for certain helper functions throughout the library.
} motor_motion_profile_t;

typedef struct servo_motor_context {
    motor_motion_profile_t motion_profile;
    // Servo-specific parameters
    float min_angle;         // Nominal minimum angle
    float max_angle;         // Nominal maximum angle
    float angle_adjustment;  // Nominal angle for "home". Sometimes they are a few degrees off of 0.0.

    // PWM Parameters
    uint32_t servo_multiplier;
    uint32_t servo_offset;

    // Servo-specific internal state
    float last_time_generated;
    float last_position_generated;
} servo_motor_context_t;

typedef struct stepper_motor_context {
    motor_motion_profile_t motion_profile;
    // Stepper-specific parameters
    // STEP pin parameters
    float min_step;              // Usually 1, 0.5, 0.25, 0.125, etc., includes microstepping
    float timer_increment;       // inverse of the frequency of the timer peripheral used for the stepper
    float steps_per_revolution;  // Number of steps per 360 degree/2pi radian revolution of the stepper motor

    // Stepper-specific internal state
    float last_time_generated;
    float last_position_generated;
} stepper_motor_context_t;

/*
 * Initializes the context struct with the parameters from the paper. `start` and `end`
 * should be the position in degrees of the servo at the start and end of the motion. `max_velocity`
 * and `max_acceleration` are the maximum velocity and acceleration of the servo in degrees per second
 * and degrees per second squared respectively. `servo_multiplier` and `servo_offset` are the parameters
 * for converting degrees to pulse width. Returns 0 on success, -errno on error.
 */
int motor_motion_servo_init_context_struct(float start, float end, float max_velocity, float max_acceleration,
                                           uint32_t servo_multiplier, uint32_t servo_offset,
                                           servo_motor_context_t *context);

/*
 * Initializes the context struct with the parameters from the paper. `start` and `end`
 * should be the position in steps of the stepper at the start and end of the motion. `max_velocity`
 * and `max_acceleration` are the maximum velocity and acceleration of the stepper in steps per second
 * and steps per second squared respectively. `min_step` is the minimum step size of the stepper (usually
 * 1, 0.5, 0.25, 0.125, etc.). Returns 0 on success, -errno on error.
 */
int motor_motion_stepper_init_context_struct(float start, float end, float max_velocity, float max_acceleration,
                                             float min_step, float timer_increment, stepper_motor_context_t *context);

/*
 * Generates a table of servo displacements for a sinusoidal motion profile
 * uses a fixed increment of 0.02 seconds but will never exceed the size of the table given.
 * Runs based on the `servo_last_time_generated` field in the context struct, to allow for multiple
 * calls (for instance, one can send a pre-generated table to the servo driver, then call this function on
 * a different buffer to generate the next table).
 * Returns the number of entries generated or -1 on error.
 */
ssize_t motor_motion_servo_generate_displacement_table(uint32_t *table, size_t table_size,
                                                       servo_motor_context_t *context);

/*
 * Generates a table of values with a pulse at the correct time for each stamp.
 */
ssize_t motor_motion_stepper_generate_timing_table(uint32_t *table, size_t table_size,
                                                   stepper_motor_context_t *context);
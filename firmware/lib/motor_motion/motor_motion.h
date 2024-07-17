#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct motor_motion_context {
    // Parameters from the paper
    float start_pos;
    float end_pos;
    float a_max;
    float v_max;
    float sgn;
    float y_f;
    float y_s;
    float y_aux;
    float y_a;
    float v_w;
    float t_o;
    float t_a;
    float omega;
    float k_s;
    float t_k;
    float t_s;
    float t_t;

    // Servo-specific parameters
    uint32_t servo_multiplier;
    uint32_t servo_offset;

    // Internal state
    float servo_last_time_generated;
} motor_motion_context_t;

/*
 * Initializes the context struct with the parameters from the paper. `start` and `end`
 * should be the position in degrees of the servo at the start and end of the motion. `max_velocity`
 * and `max_acceleration` are the maximum velocity and acceleration of the servo in degrees per second
 * and degrees per second squared respectively. `servo_multiplier` and `servo_offset` are the parameters
 * for converting degrees to pulse width. Returns 0 on success, -errno on error.
 */
int motor_motion_init_context_struct(float start, float end, float max_velocity, float max_acceleration,
                                     uint32_t servo_multiplier, uint32_t servo_offset, motor_motion_context_t *context);

/*
 * Generates a table of servo displacements for a sinusoidal motion profile
 * uses a fixed increment of 0.02 seconds but will never exceed the size of the table given.
 * Runs based on the `servo_last_time_generated` field in the context struct, to allow for multiple
 * calls (for instance, one can send a pre-generated table to the servo driver, then call this function on
 * a different buffer to generate the next table).
 * Returns the number of entries generated or -1 on error.
 */
ssize_t motor_motion_generate_servo_displacement_table(uint32_t *table, size_t table_size,
                                                       motor_motion_context_t *context);
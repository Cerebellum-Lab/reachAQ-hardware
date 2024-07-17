#include "motor_motion.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(motor_motion, LOG_LEVEL_DBG);

#ifndef M_PI
#define M_PI 3.141593f
#endif

static uint32_t degrees_to_pulse_width(motor_motion_context_t *context, float degree) {
    return degree * context->servo_multiplier / 180 + context->servo_offset;
}

int motor_motion_init_context_struct(float start, float end, float max_velocity, float max_acceleration,
                                     uint32_t servo_multiplier, uint32_t servo_offset,
                                     motor_motion_context_t *context) {
    float displacement = end - start;
    context->start_pos = start;
    context->end_pos = end;

    context->servo_multiplier = servo_multiplier;
    context->servo_offset = servo_offset;

    context->a_max = max_acceleration;
    context->v_max = max_velocity;

    context->sgn = signbit(displacement) ? -1.0f : 1.0f;
    context->y_f = fabs(displacement);
    context->y_s = context->y_f / 2.0f;
    context->y_aux = max_velocity * max_velocity / max_acceleration;
    context->y_a = context->y_s < context->y_aux ? context->y_s : context->y_aux;

    context->v_w = context->y_s <= context->y_aux ? sqrtf(context->y_s * max_acceleration) : max_velocity;

    context->t_o = context->v_w / max_acceleration;
    context->t_a = 2.0f * context->t_o;

    context->omega = 2.0f * M_PI / context->t_a;

    context->k_s = context->t_a * context->v_w / (4.0f * M_PI * M_PI);

    context->t_k = 2.0f * (context->y_s - context->y_a) / max_velocity;
    context->t_s = context->t_k / 2.0f + context->t_a;
    context->t_t = 2.0f * context->t_s;

    context->servo_last_time_generated = 0;
    return 0;
}

static float motor_cos(float phi, motor_motion_context_t *context) {
    // This is a placeholder for the implementation using the CORDIC co-processor
    (void)context;
    return cosf(phi);
}

static float displacement_hat(float time, motor_motion_context_t *context) {
    if (time <= 0) {
        return 0;
    } else if (time <= context->t_a) {
        return context->a_max / 4.0f * time * time + context->k_s * (motor_cos(context->omega * time, context) - 1.0f);
    } else if (time <= context->t_s) {
        return context->y_s + context->v_w * (time - context->t_s);
    } else {
        return context->y_f - displacement_hat(context->t_t - time, context);
    }
}

static float displacement(float time, motor_motion_context_t *context) {
    return context->sgn * displacement_hat(time, context);
}

ssize_t motor_motion_generate_servo_displacement_table(uint32_t *table, size_t table_size,
                                                       motor_motion_context_t *context) {
    const float time_step = 0.02;
    size_t n_entries = (context->t_t - context->servo_last_time_generated) * 50;  // 50 = 1 / time_step

    if (n_entries > table_size) {
        n_entries = table_size;
    }

    for (size_t i = 0; i < n_entries; i++) {
        float time = time_step * i + context->servo_last_time_generated;
        float displacement_now = displacement(time, context) + context->start_pos;
        table[i] = degrees_to_pulse_width(context, displacement_now);
    }

    context->servo_last_time_generated += time_step * n_entries;

    return n_entries;
}

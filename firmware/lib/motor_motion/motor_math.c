#include "motor_math.h"

#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stddef.h>

#ifdef BENCH_TEST
// For bench testing
#include "test.h"
#else
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(motor_motion, CONFIG_LIB_MOTOR_MOTION_LOG_LEVEL);
#endif

#define FE_ALL_BUT_INEXACT (FE_ALL_EXCEPT & ~FE_INEXACT)

static void print_fp_error(const int errs) {
    if (errs & FE_DIVBYZERO) {
        LOG_ERR("Floating point error encountered: Division by zero");
    }

    if (errs & FE_INVALID) {
        LOG_ERR("Floating point error encountered: Invalid operation");
    }

    if (errs & FE_OVERFLOW) {
        LOG_ERR("Floating point error encountered: Overflow");
    }

    if (errs & FE_UNDERFLOW) {
        LOG_ERR("Floating point error encountered: Underflow");
    }

    if (errs & FE_INEXACT) {
        // This happens all the time, so only a warning here.
        LOG_WRN("Floating point error encountered: Inexact result");
    }
}

static int motor_motion_init_context_struct(const float start, const float end, const float max_velocity,
                                            const float max_acceleration, motor_motion_profile_t *context) {
    // clear floating point errors
    int ret = feclearexcept(FE_ALL_EXCEPT);
    if (ret != 0) {
        LOG_ERR("Failed to clear floating point exceptions");
        return -EINVAL;
    }

    const float displacement = end - start;
    context->start_pos = start;
    context->end_pos = end;

    context->a_max = max_acceleration;
    context->v_max = max_velocity;

    context->sgn = displacement < 0.0f ? -1.0f : 1.0f;
    context->y_f = fabsf(displacement);
    context->y_s = context->y_f / 2.0f;
    const float y_aux = max_velocity * max_velocity / max_acceleration;
    context->y_a = context->y_s < y_aux ? context->y_s : y_aux;

    context->v_w = context->y_s < y_aux ? sqrtf(context->y_s * max_acceleration) : max_velocity;

    context->t_o = context->v_w / max_acceleration;
    context->t_a = 2.0f * context->t_o;

    context->omega = 2.0f * M_PI / context->t_a;

    context->k_s = context->t_a * context->v_w / (4.0f * M_PI * M_PI);

    context->t_k = 2.0f * (context->y_s - context->y_a) / max_velocity;
    context->t_s = context->t_k / 2.0f + context->t_a;
    context->t_t = 2.0f * context->t_s;

    ret = fetestexcept(FE_ALL_BUT_INEXACT);
    if (ret != 0) {
        print_fp_error(ret);
        LOG_ERR("Floating point error encountered. Errno: %d", errno);
        return -ERANGE;
    }

    return 0;
}

int motor_motion_servo_init_context_struct(const float start, const float end, const float max_velocity,
                                           const float max_acceleration, const float min_angle_pwm,
                                           const float max_angle_pwm, servo_motor_context_t *context) {
    const int ret =
        motor_motion_init_context_struct(start, end, max_velocity, max_acceleration, &context->motion_profile);
    if (ret != 0) {
        return ret;
    }

    context->last_position_generated = start;
    context->last_time_generated = 0.0f;
    context->min_angle_pwm = min_angle_pwm;
    context->max_angle_pwm = max_angle_pwm;
    return 0;
}

int motor_motion_stepper_init_context_struct(const float start, const float end, const float max_velocity,
                                             const float max_acceleration, const float min_step,
                                             const float timer_increment, stepper_motor_context_t *context) {
    if (motor_motion_init_context_struct(start, end, max_velocity, max_acceleration, &context->motion_profile) != 0) {
        return -EINVAL;
    }

    LOG_ERR("Parameters: y_f: %f y_s: %f y_a: %f v_w: %f t_o: %f t_a: %f t_k: %f t_s: %f t_t: %f",
        (double)context->motion_profile.y_f, (double)context->motion_profile.y_s, (double)context->motion_profile.y_a,
        (double)context->motion_profile.v_w, (double)context->motion_profile.t_o, (double)context->motion_profile.t_a,
        (double)context->motion_profile.t_k, (double)context->motion_profile.t_s, (double)context->motion_profile.t_t);

    context->last_position_generated = start;
    context->last_time_generated = 0.0f;
    context->min_step = min_step;
    context->timer_increment = timer_increment;
    return 0;
}

/*
 * **** Mathematical Functions *****
 * Note that there are five distinct regions of displacement_hat/velocity_hat/acceleration_hat:
 * 1. t < 0, stationary
 * 2. 0 <= t <= t_a, acceleration_hat is positive.
 * 3. t_a < t < t - t_a, acceleration_hat is zero but velocity_hat is positive.
 * 4. t - t_a <= t <= t_t, acceleration_hat is negative (this is just a reflection of region 2).
 * 5. t > t_t, stationary.
 *
 * The third region takes up the vast majority of total time for our applications, and is very easy to
 * compute. It is also symmetric about t_s = t_t / 2.  I have called region 2 the acceleration region
 * and region 3 the velocity region.
 *
 * We have to use Halley's method to find the inverse function of displacement in regions 2 and 4.
 */

static float motor_cos(const float phi, const motor_motion_profile_t *context) {
    // This is a placeholder for the implementation using the CORDIC coprocessor
    (void)context;
    return cosf(phi);
}

static float motor_sin(const float phi, const motor_motion_profile_t *context) {
    // This is a placeholder for the implementation using the CORDIC coprocessor
    (void)context;
    return sinf(phi);
}

static float acceleration_region_displacement_hat(const float time, const motor_motion_profile_t *context) {
    return context->a_max / 4.0f * time * time + context->k_s * (motor_cos(context->omega * time, context) - 1);
}

static float acceleration_region_velocity_hat(const float time, const motor_motion_profile_t *context) {
    return context->k_s * context->omega * (context->omega * time - motor_sin(context->omega * time, context));
}

static float acceleration_region_acceleration_hat(const float time, const motor_motion_profile_t *context) {
    return context->a_max / 2.0f * (1 - motor_cos(context->omega * time, context));
}

static float displacement_hat(const float time, const motor_motion_profile_t *context) {
    if (time <= 0) {
        return 0.0f;
    } else if (time <= context->t_a) {
        return acceleration_region_displacement_hat(time, context);
    } else if (time <= context->t_t - context->t_a) {
        return context->y_s + context->v_w * (time - context->t_s);
    } else if (time < context->t_t) {
        const float reflected_time = context->t_t - time;
        return context->y_f - acceleration_region_displacement_hat(reflected_time, context);
    } else {
        /* time >= context->t_t */
        return context->y_f;
    }
}

static float displacement(const float time, const motor_motion_profile_t *context) {
    return context->sgn * displacement_hat(time, context);
}

/*
 * Use Halley's method in the *first* region of displacement_hat. (I.e., t <= context->t_a).
 * We are really using Halley's method on the function `difference = displacement_hat(t) - displacement_hat`.
 */
static float acceleration_region_halleys_method(const float displacement_hat, const float min_step,
                                                const float timer_increment, const motor_motion_profile_t *context) {
    if (displacement_hat <= 0.0f || displacement_hat > context->y_a) {
        /* Invalid outside of this region */
        return NAN;
    }

    const size_t max_iters = 16; /* Prevent pathological case where this never converges. With a good guess,
                                  * this should never happen. Testing (see the `test/` folder) shows that we
                                  * converge in 3 iterations or fewer! */

    const float time_tolerance = timer_increment / 2.0f > FLT_EPSILON ? timer_increment / 2.0f : FLT_EPSILON;
    const float position_tolerance = min_step / 8.0f;

    /* The cosine term is, on average, 0 so `time` is a good first guess. Though it uses an expensive `sqrtf`,
     * it provides such an excellent guess that it will save us a lot of computation later. */
    float time = sqrtf((displacement_hat) * 4.0f / context->a_max + context->k_s);

    float displacement_now = acceleration_region_displacement_hat(time, context);
    float difference = displacement_now - displacement_hat;  // This is the actual function we are finding the root of.

    size_t n_iters = 0;
    while (fabsf(difference) > position_tolerance && n_iters < max_iters) {
        const float acceleration_now = acceleration_region_acceleration_hat(time, context);
        const float velocity_now = acceleration_region_velocity_hat(time, context);
        const float adjustment =
            2.0f * difference * velocity_now / (2.0f * velocity_now * velocity_now - difference * acceleration_now);

        if (isnan(adjustment) || isinf(adjustment) || fabsf((time - adjustment) - time) < time_tolerance) {
            /* Inspection reveals that in this region, the denominator in `adjustment` should never be zero or invalid.
             * Nevertheless, we check for it here. The adjustment is monotonic and decreasing in magnitude when we
             * are close to the root (and the guess puts us very close to the root), so we just break out of the loop
             * if the time adjustment is too small or if floating point loss of precision makes the adjustment
             * meaningless. */
            LOG_ERR("Invalid adjustment in Halley's method: %f", (double)adjustment);
            break;
        }

        time -= adjustment;
        displacement_now = acceleration_region_displacement_hat(time, context);
        difference = displacement_now - displacement_hat;

        n_iters++;
    }

#ifdef BENCH_TEST
    if (print_n_iterations) {
        printf("%04.4f, %04.4f, %zu\n", displacement_hat, time, n_iters);
    }
#endif

    return time;
}

/*
 * Find the inverse of displacement_hat (i.e., find time, given a displacement_hat).
 * But we are going to be smart about it to avoid places where the derivatives are discontinuous.
 * For most regions of displacement (see long comment above), there is a closed-form, easily-calculated
 * solution. For the regions where acceleration is nonzero, we use Halley's function helper method and
 * use total versions of the velocity and acceleration in those regions so that it always converges.
 */
static float time_at_displacement_hat(const float displacement_hat, const float min_step, const float timer_increment,
                                      const motor_motion_profile_t *context) {
    if (displacement_hat <= 0.0f) {
        return 0.0f;
    } else if (displacement_hat <= context->y_a) {
        const float ret = acceleration_region_halleys_method(displacement_hat, min_step, timer_increment, context);
        if (ret == NAN) {
            LOG_ERR("Halley's method returned NAN");
            /* Apparently invalid. Try the next region. N.B.--This should never happen. */
            return (displacement_hat - context->y_s) / context->v_w + context->t_s;
        }
        return ret;
    } else if (displacement_hat <= context->y_f - context->y_a) {
        return (displacement_hat - context->y_s) / context->v_w + context->t_s;
    } else if (displacement_hat < context->y_f) {
        const float reflected_displacement = context->y_f - displacement_hat;
        return context->t_t -
               acceleration_region_halleys_method(reflected_displacement, min_step, timer_increment, context);
    } else {
        /* displacement >= context->y_f */
        return context->t_t;
    }
}

static float time_at_position(const float position, const float min_step, const float timer_increment,
                              const motor_motion_profile_t *context) {
    const float displacement = position - context->start_pos;
    const float displacement_hat = context->sgn * displacement;
    const float time = time_at_displacement_hat(displacement_hat, min_step, timer_increment, context);
    return time;
}

/**
 *
 * Convert (actual) degrees to the PWM count.
 */
static uint32_t degrees_to_pwm_count(const servo_motor_context_t *context, const float degree) {
    const float nominal_degrees = degree + context->min_angle - context->angle_adjustment;
    return (uint32_t)roundf(
        (((nominal_degrees - context->min_angle) * (context->max_angle_pwm - context->min_angle_pwm)) /
             (context->max_angle - context->min_angle) +
         context->min_angle_pwm) /
        context->pwm_timer_increment);
}

ssize_t motor_motion_servo_generate_displacement_table(uint32_t *table, const size_t table_size,
                                                       servo_motor_context_t *context) {
    const float servo_time_step = 0.02f;
    const float servo_entries_per_second = 50.0f;
    BUILD_ASSERT(servo_time_step == 1.0f / servo_entries_per_second,
                 "Servo time step must be 1 / servo_entries_per_second");
    size_t n_entries =
        (size_t)((context->motion_profile.t_t - context->last_time_generated) * servo_entries_per_second);
    if (n_entries > table_size) {
        n_entries = table_size;
    }

    float time = context->last_time_generated;
    float displacement_now = displacement(time, &context->motion_profile);

    for (size_t i = 0; i < n_entries; i++) {
        time = servo_time_step * (float)(i + 1) + context->last_time_generated;
        displacement_now = displacement(time, &context->motion_profile) + context->motion_profile.start_pos;
        table[i] = degrees_to_pwm_count(context, displacement_now);
    }

    context->last_time_generated = time;
    context->last_position_generated = displacement_now;

    return (ssize_t)n_entries;
}

static float revolutions_to_steps(const stepper_motor_context_t *context, const float revolutions) {
    return revolutions * context->steps_per_revolution;
}

ssize_t motor_motion_stepper_generate_timing_table(uint32_t *table, const size_t table_size,
                                                   stepper_motor_context_t *context) {
    const float time_step = context->timer_increment;
    const float n_steps_to_finish =
        revolutions_to_steps(context, context->motion_profile.end_pos - context->last_position_generated);
    ssize_t n_entries = (ssize_t)fabsf(n_steps_to_finish / context->min_step);

    if (n_entries > table_size) {
        n_entries = (ssize_t)table_size;
    }

    float last_time = context->last_time_generated;

    float this_time = context->last_time_generated;
    float this_position = context->last_position_generated;
    for (size_t i = 0; i < n_entries; i++) {
        this_position = context->last_position_generated + context->motion_profile.sgn * (float)i *
                                                               context->min_step / context->steps_per_revolution;
        this_time =
            time_at_position(this_position, context->min_step, context->timer_increment, &context->motion_profile);

        // Take the ceiling to be conservative about velocity & acceleration.
        if (this_time < last_time) {
            LOG_ERR("This time (%f) less than last time (%f) at position %f (pulse %f)", (double)this_time, (double)last_time, (double)this_position, (double) (this_position * context->steps_per_revolution / context->min_step));
        }
        const float time_delta = ceilf((this_time - last_time) / time_step);

        if (time_delta >= (float)UINT16_MAX) {
            // This is the maximum value that can be sent over the DMA. The upper word is completely unused.
            LOG_ERR("Time_delta (%f) exceeded UINT16_MAX at position %f", (double)time_delta, (double)this_position);
            table[i] = UINT16_MAX;
        } else if (time_delta <= 1.0f || isnan(time_delta) || isinf(time_delta)) {
            // Sending even a single zero over the DMA will stop the timer, and negative numbers
            // should (theoretically) never happen. In either case, just send the minimum value.
            LOG_ERR("Time_delta is out of range (%f) at position %f", (double)time_delta, (double)this_position);
            table[i] = 10;
        } else {
            table[i] = (uint32_t)lroundf(time_delta);
        }

        last_time = this_time;
    }

    context->last_position_generated = this_position;
    context->last_time_generated = this_time;

    return n_entries;
}
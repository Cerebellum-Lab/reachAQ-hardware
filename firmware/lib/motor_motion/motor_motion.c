#include "motor_motion.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <zephyr/logging/log.h>

#include "servo.h"
#include "stepper.h"

LOG_MODULE_REGISTER(motor_motion);

void motor_motion_set_radius(motor_motion_profile_t *motion_profile, const float radius) {
    motion_profile->radius = radius;
}

float motor_motion_stepper_length_to_steps(const stepper_motor_context_t *context, const float length) {
    const float radius = context->motion_profile.radius;
    if (radius == 0.0f) {
        LOG_ERR("Radius not set for motor context.");
        return NAN;
    }
    return length * context->steps_per_revolution / (2.0f * M_PI * radius);
}

void motor_motion_stepper_set_current_position(stepper_motor_context_t *context, const float position) {
    context->motion_profile.start_pos = position;
}

float motor_motion_servo_length_to_degrees(const servo_motor_context_t *context, const float length) {
    const float radius = context->motion_profile.radius;
    if (radius == 0.0f) {
        LOG_ERR("Radius not set for motor context.");
        return NAN;
    }
    return length * 180.0f / (M_PI * radius);
}

void motor_motion_servo_set_current_position(servo_motor_context_t *context, const float position) {
    context->angle_adjustment = context->motion_profile.start_pos - position;
}

void stepper_motor_stop(const struct device *dev) {
    ll_stepper_disable(dev);
    ll_stepper_dma_stop(dev);
}

void servo_motor_stop(const struct device *dev) {
    ll_servo_enable(dev, false);
    ll_servo_dma_stop(dev);
}

#define DT_GET_COMMA(id) DEVICE_DT_GET(id),
static struct device *stepper_motors[] = {DT_FOREACH_STATUS_OKAY(ll_stepper, DT_GET_COMMA)};
static struct device *servo_motors[] = {DT_FOREACH_STATUS_OKAY(ll_servo, DT_GET_COMMA)};

void motors_all_stop(void) {
    for (size_t i = 0; i < ARRAY_SIZE(stepper_motors); i++) {
        stepper_motor_stop(stepper_motors[i]);
    }

    for (size_t i = 0; i < ARRAY_SIZE(servo_motors); i++) {
        servo_motor_stop(stepper_motors[i]);
    }
}
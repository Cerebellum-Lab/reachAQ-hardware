#include "motor_motion.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <zephyr/logging/log.h>

#include "servo.h"
#include "stepper.h"

LOG_MODULE_REGISTER(motor_motion);

void motor_motion_stepper_set_current_position(stepper_motor_context_t *context, const float position) {
    context->motion_profile.start_pos = position;
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
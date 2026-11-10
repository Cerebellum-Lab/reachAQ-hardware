#include "motor_motion.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <zephyr/logging/log.h>

#include "servo.h"
#include "stepper.h"

LOG_MODULE_REGISTER(motor_motion);

void motor_motion_stepper_set_current_position(stepper_motor_context_t *context, const float position) {
    context->motion_profile.end_pos = position;
    context->last_position_generated = position;
}

void motor_motion_servo_set_current_position(servo_motor_context_t *context, const float position) {
    context->angle_adjustment = context->motion_profile.start_pos - position;
}

void stepper_motor_stop(const struct device *dev) {
    struct stepper_work_context *context = find_stepper_context_from_device(dev);
    context->motion_mode = MOTION_DONE;

    ll_stepper_disable(dev);
}

void servo_motor_stop(const struct device *dev) { ll_servo_enable(dev, false); }

#define DT_GET_COMMA(id) DEVICE_DT_GET(id),
static const struct device *const stepper_motors[] = {DT_FOREACH_STATUS_OKAY(ll_stepper, DT_GET_COMMA)};
static const struct device *const servo_motors[] = {DT_FOREACH_STATUS_OKAY(ll_servo, DT_GET_COMMA)};

uint8_t steppers_stop_moving(void) {
    uint8_t stopped = 0;

    for (size_t i = 0; i < ARRAY_SIZE(stepper_motors); i++) {
        struct stepper_work_context *context = find_stepper_context_from_device(stepper_motors[i]);

        // stepper_motor_stop() marks the motor DONE, which makes the status loop ACK a stale uuid.
        if (context == NULL || context->motion_mode != MOTION_IN_PROGESS) {
            continue;
        }

        stepper_motor_stop(stepper_motors[i]);
        k_work_cancel_delayable(&context->calculation_work);
        context->motion_calculation_done = true;
        ll_motor_queue_stop(stepper_motors[i]);

        stopped |= BIT(i);
    }

    return stopped;
}

void motors_all_stop(void) {
    steppers_stop_moving();

    for (size_t i = 0; i < ARRAY_SIZE(servo_motors); i++) {
        const struct servo_work_context *context = find_servo_context_from_device(servo_motors[i]);

        if (context == NULL || context->motion_mode != MOTION_IN_PROGESS) {
            continue;
        }

        servo_motor_stop(servo_motors[i]);
        servo_cancel_all_work(servo_motors[i]);
        ll_motor_queue_stop(servo_motors[i]);
    }
}

void trigger_e_stop(void) {
    motors_all_stop();

    set_all_e_stop_flags();
}

const struct device *stepper_motor_by_id(const size_t id) {
    if (id >= ARRAY_SIZE(stepper_motors)) {
        LOG_ERR("Invalid stepper motor id: %d", id);
        return NULL;
    }

    return stepper_motors[id];
}

const struct device *servo_motor_by_id(const size_t id) {
    if (id >= ARRAY_SIZE(servo_motors)) {
        LOG_ERR("Invalid servo motor id: %d", id);
        return NULL;
    }

    return servo_motors[id];
}
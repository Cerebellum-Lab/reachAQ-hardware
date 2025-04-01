#include "motor_settings.h"

#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "motor_motion_workq.h"

LOG_MODULE_REGISTER(motor_settings);

// We have to use small names here because the nvs is not very large.
#define SETTINGS_MODULE_NAME "motor"
#define SETTINGS_SERVO_MODULE_NAME "ser"
#define SETTINGS_STEPPER_MODULE_NAME "ste"

// Common keys
#define MAX_VELOCITY_KEY "v_max"
#define MAX_ACCELERATION_KEY "a_max"

// Servo keys
#define MIN_ANGLE_KEY "o_min"
#define MAX_ANGLE_KEY "o_max"
#define ANGLE_ADJUSTMENT_KEY "o_adj"
#define SERVO_MIN_ANGLE_PWM_KEY "pwm_min"
#define SERVO_MAX_ANGLE_PWM_KEY "pwm_max"

// Stepper keys
#define MICRO_STEP_KEY "s_mstep"
#define STEPS_PER_REVOLUTION_KEY "s_rev"
#define FLIP_LIMIT_ORIENTATION_KEY "s_flo"

#if CONFIG_DT_HAS_LL_SERVO_ENABLED
/* ***** Settings Handler ***** */
static int servo_settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
                              struct servo_work_context *context) {
    if (strncmp(key, MAX_VELOCITY_KEY, sizeof(MAX_VELOCITY_KEY) - 1) == 0) {
        float max_velocity;
        const int ret = read_cb(cb_arg, &max_velocity, sizeof(max_velocity));
        if (ret < 0) {
            LOG_ERR("Failed to read max_velocity from settings: %d", ret);
            return ret;
        }
        LOG_DBG("restored max_velocity: %f", (double)max_velocity);
        if (max_velocity > 0.0f) {
            context->motor_max_velocity = max_velocity;
        }
    } else if (strncmp(key, MAX_ACCELERATION_KEY, sizeof(MAX_ACCELERATION_KEY) - 1) == 0) {
        float max_acceleration;
        const int ret = read_cb(cb_arg, &max_acceleration, sizeof(max_acceleration));
        if (ret < 0) {
            LOG_ERR("Failed to read max_acceleration from settings: %d", ret);
            return ret;
        }
        LOG_DBG("restored max_acceleration: %f", (double)max_acceleration);
        if (max_acceleration > 0.0f) {
            context->motor_max_acceleration = max_acceleration;
        }
    } else if (strncmp(key, MIN_ANGLE_KEY, sizeof(MIN_ANGLE_KEY) - 1) == 0) {
        float min_angle;
        const int ret = read_cb(cb_arg, &min_angle, sizeof(min_angle));
        if (ret < 0) {
            LOG_ERR("Failed to read min_angle from settings: %d", ret);
            return ret;
        }
        LOG_DBG("restored min_angle: %f", (double)min_angle);
        context->context.min_angle = min_angle;
    } else if (strncmp(key, MAX_ANGLE_KEY, sizeof(MAX_ANGLE_KEY) - 1) == 0) {
        float max_angle;
        const int ret = read_cb(cb_arg, &max_angle, sizeof(max_angle));
        if (ret < 0) {
            LOG_ERR("Failed to read max_angle from settings: %d", ret);
            return ret;
        }
        LOG_DBG("restored max_angle: %f", (double)max_angle);
        context->context.max_angle = max_angle;
    } else if (strncmp(key, ANGLE_ADJUSTMENT_KEY, sizeof(ANGLE_ADJUSTMENT_KEY) - 1) == 0) {
        float angle_adjustment;
        const int ret = read_cb(cb_arg, &angle_adjustment, sizeof(angle_adjustment));
        if (ret < 0) {
            LOG_ERR("Failed to read angle_adjustment from settings: %d", ret);
            return ret;
        }
        LOG_DBG("Restored angle_adjustment: %f", (double)angle_adjustment);
        if (angle_adjustment > 0.0f) {
            context->context.angle_adjustment = angle_adjustment;
        }
    } else if (strncmp(key, SERVO_MIN_ANGLE_PWM_KEY, sizeof(SERVO_MIN_ANGLE_PWM_KEY) - 1) == 0) {
        float pwm_min_angle;
        const int ret = read_cb(cb_arg, &pwm_min_angle, sizeof(pwm_min_angle));
        if (ret < 0) {
            LOG_ERR("Failed to read min_angle_pwm from settings: %d", ret);
            return ret;
        }
        LOG_DBG("restored min_angle_pwm: %f", (double)pwm_min_angle);
        if (pwm_min_angle > 0.0f) {
            context->context.min_angle_pwm = pwm_min_angle;
        }
    } else if (strncmp(key, SERVO_MAX_ANGLE_PWM_KEY, sizeof(SERVO_MAX_ANGLE_PWM_KEY) - 1) == 0) {
        float pwm_max_angle;
        const int ret = read_cb(cb_arg, &pwm_max_angle, sizeof(pwm_max_angle));
        if (ret < 0) {
            LOG_ERR("Failed to read max_angle_pwm from settings: %d", ret);
            return ret;
        }
        LOG_DBG("restored max_angle_pwm: %f", (double)pwm_max_angle);
        if (pwm_max_angle > 0.0f) {
            context->context.max_angle_pwm = pwm_max_angle;
        }
    } else {
        LOG_WRN("Unknown key: %s", key);
    }

    return 0;
}

static char itoa(const size_t value) {
    if (value > 9) {
        return '\0';
    } else {
        return value + '0';
    }
}

#define GENERATE_SERVO_TEMPLATE(name) SETTINGS_MODULE_NAME "/" SETTINGS_SERVO_MODULE_NAME "/0/" name

int servo_settings_export(const struct device *dev, const size_t dt_id,
                          int (*storage_func)(const char *name, const void *value, size_t val_len)) {
    const struct servo_work_context *const context = find_servo_context_from_device(dev);
    static const size_t id_number_index = sizeof(SETTINGS_MODULE_NAME "/" SETTINGS_SERVO_MODULE_NAME "/") - 1;
    if (context == NULL) {
        LOG_ERR("Failed to find context for device: %s", dev->name);
        return -ENODEV;
    }

    if (dt_id > 9) {
        LOG_ERR("Invalid device id: %d", dt_id);
        return -EINVAL;
    }

    // Save max velocity
    static char max_velocity_key[] = GENERATE_SERVO_TEMPLATE(MAX_VELOCITY_KEY);
    max_velocity_key[id_number_index] = itoa(dt_id);
    int ret = storage_func(max_velocity_key, &context->motor_max_velocity, sizeof(context->motor_max_velocity));
    if (ret < 0) {
        LOG_ERR("Failed to write max_velocity to settings: %d", ret);
        return ret;
    }
    LOG_INF("Saved max_velocity: %f", (double)context->motor_max_velocity);

    static char max_acceleration_key[] = GENERATE_SERVO_TEMPLATE(MAX_ACCELERATION_KEY);
    max_acceleration_key[id_number_index] = itoa(dt_id);
    ret = storage_func(max_acceleration_key, &context->motor_max_acceleration, sizeof(context->motor_max_acceleration));
    if (ret < 0) {
        LOG_ERR("Failed to write max_acceleration to settings: %d", ret);
        return ret;
    }
    LOG_INF("Saved max_acceleration: %f", (double)context->motor_max_acceleration);

    static char min_angle_key[] = GENERATE_SERVO_TEMPLATE(MIN_ANGLE_KEY);
    min_angle_key[id_number_index] = itoa(dt_id);
    ret = storage_func(min_angle_key, &context->context.min_angle, sizeof(context->context.min_angle));
    if (ret < 0) {
        LOG_ERR("Failed to write min_angle to settings: %d", ret);
        return ret;
    }
    LOG_DBG("Saved min_angle: %f", (double)context->context.min_angle);

    static char max_angle_key[] = GENERATE_SERVO_TEMPLATE(MAX_ANGLE_KEY);
    max_angle_key[id_number_index] = itoa(dt_id);
    ret = storage_func(max_angle_key, &context->context.max_angle, sizeof(context->context.max_angle));
    if (ret < 0) {
        LOG_ERR("Failed to write max_angle to settings: %d", ret);
        return ret;
    }
    LOG_DBG("Saved max_angle: %f", (double)context->context.max_angle);

    static char angle_adjustment_key[] = GENERATE_SERVO_TEMPLATE(ANGLE_ADJUSTMENT_KEY);
    angle_adjustment_key[id_number_index] = itoa(dt_id);
    ret = storage_func(angle_adjustment_key, &context->context.angle_adjustment,
                       sizeof(context->context.angle_adjustment));
    if (ret < 0) {
        LOG_ERR("Failed to write angle_adjustment to settings: %d", ret);
        return ret;
    }
    LOG_DBG("Saved angle_adjustment: %f", (double)context->context.angle_adjustment);

    static char max_angle_pwm_key[] = GENERATE_SERVO_TEMPLATE(SERVO_MAX_ANGLE_PWM_KEY);
    max_angle_pwm_key[id_number_index] = itoa(dt_id);
    ret = storage_func(max_angle_pwm_key, &context->context.max_angle_pwm, sizeof(context->context.max_angle_pwm));
    if (ret < 0) {
        LOG_ERR("Failed to write max_angle_pwm to settings: %d", ret);
        return ret;
    }
    LOG_DBG("Saved max_angle_pwm: %f", (double)context->context.max_angle_pwm);

    static char servo_offset_key[] = GENERATE_SERVO_TEMPLATE(SERVO_MIN_ANGLE_PWM_KEY);
    servo_offset_key[id_number_index] = itoa(dt_id);
    ret = storage_func(servo_offset_key, &context->context.min_angle_pwm, sizeof(context->context.min_angle_pwm));
    if (ret < 0) {
        LOG_ERR("Failed to write min_angle_pwm to settings: %d", ret);
        return ret;
    }
    LOG_DBG("Saved min_angle_pwm: %f", (double)context->context.min_angle_pwm);

    return 0;
}

#define DEFINE_SERVO_DEVICE_SETTINGS_FUNCTION(id)                                                            \
    static int servo_settings_set##id(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg) { \
        const struct device *dev = DEVICE_DT_GET(DT_INST(id, ll_servo));                                     \
        struct servo_work_context *context = find_servo_context_from_device(dev);                            \
        return servo_settings_set(key, len, read_cb, cb_arg, context);                                       \
    }

#define DEFINE_SERVO_DEVICE_SETTINGS_EXPORT_FUNCTION(id)                                                             \
    static int servo_settings_export##id(int (*storage_func)(const char *name, const void *value, size_t val_len)) { \
        const struct device *dev = DEVICE_DT_GET(DT_INST(id, ll_servo));                                             \
        return servo_settings_export(dev, id, storage_func);                                                         \
    }

#define DEFINE_SERVO_DEVICE_SETTINGS_HANDLERS(id)                                                                \
    SETTINGS_STATIC_HANDLER_DEFINE(servo##id, SETTINGS_MODULE_NAME "/" SETTINGS_SERVO_MODULE_NAME "/" #id, NULL, \
                                   servo_settings_set##id, NULL, servo_settings_export##id);

DT_FOREACH_OKAY_INST_ll_servo(DEFINE_SERVO_DEVICE_SETTINGS_FUNCTION);
DT_FOREACH_OKAY_INST_ll_servo(DEFINE_SERVO_DEVICE_SETTINGS_EXPORT_FUNCTION);

DT_FOREACH_OKAY_INST_ll_servo(DEFINE_SERVO_DEVICE_SETTINGS_HANDLERS);
#endif

#if CONFIG_DT_HAS_LL_STEPPER_ENABLED
static int stepper_settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
                                struct stepper_work_context *context) {
    if (strncmp(key, MAX_VELOCITY_KEY, sizeof(MAX_VELOCITY_KEY) - 1) == 0) {
        float max_velocity;
        const int ret = read_cb(cb_arg, &max_velocity, sizeof(max_velocity));
        if (ret < 0) {
            LOG_ERR("Failed to read max_velocity from settings: %d", ret);
            return -EINVAL;
        }
        LOG_DBG("restored max_velocity: %f", (double)max_velocity);
        if (max_velocity > 0.0f) {
            context->motor_max_velocity = max_velocity;
        }
    } else if (strncmp(key, MAX_ACCELERATION_KEY, sizeof(MAX_ACCELERATION_KEY) - 1) == 0) {
        float max_acceleration;
        const int ret = read_cb(cb_arg, &max_acceleration, sizeof(max_acceleration));
        if (ret < 0) {
            LOG_ERR("Failed to read max_acceleration from settings: %d", ret);
            return -EINVAL;
        }
        LOG_DBG("restored max_acceleration: %f", (double)max_acceleration);
        if (max_acceleration > 0.0f) {
            context->motor_max_acceleration = max_acceleration;
        }
    } else if (strncmp(key, MICRO_STEP_KEY, sizeof(MICRO_STEP_KEY) - 1) == 0) {
        uint16_t microstep;
        const int ret = read_cb(cb_arg, &microstep, sizeof(microstep));
        if (ret < 0) {
            LOG_ERR("Failed to read min_step from settings: %d", ret);
            return -EINVAL;
        }
        LOG_DBG("restored min_step: %d", microstep);
        if (microstep > 0.0f) {
            context->microsteps = microstep;
        }
    } else if (strncmp(key, STEPS_PER_REVOLUTION_KEY, sizeof(STEPS_PER_REVOLUTION_KEY) - 1) == 0) {
        float steps_per_revolution;
        const int ret = read_cb(cb_arg, &steps_per_revolution, sizeof(steps_per_revolution));
        if (ret < 0) {
            LOG_ERR("Failed to read steps_per_revolution from settings: %d", ret);
            return -EINVAL;
        }
        LOG_DBG("restored steps_per_revolution: %f", (double)steps_per_revolution);
        if (steps_per_revolution > 0.0f) {
            context->motor_steps_per_revolution = steps_per_revolution;
        }
    } else if (strncmp(key, FLIP_LIMIT_ORIENTATION_KEY, sizeof(FLIP_LIMIT_ORIENTATION_KEY) - 1) == 0) {
        bool flip_limit_orientation;
        const int ret = read_cb(cb_arg, &flip_limit_orientation, sizeof(flip_limit_orientation));
        if (ret < 0) {
            LOG_ERR("Failed to read flip_limit_orientation from settings: %d", ret);
            return -EINVAL;
        }
        LOG_INF("restored flip_limit_orientation: %d", flip_limit_orientation);
        if (flip_limit_orientation == 0 || flip_limit_orientation == 1) {
            context->flip_limit_orientation = flip_limit_orientation;
        }
    } else {
        LOG_WRN("Unknown key: %s", key);
        return -EINVAL;
    }
    return 0;
}

#define GENERATE_STEPPER_TEMPLATE(name) SETTINGS_MODULE_NAME "/" SETTINGS_STEPPER_MODULE_NAME "/0/" name

static int stepper_settings_export(const struct device *dev, const size_t dt_id,
                                   int (*storage_func)(const char *name, const void *value, size_t val_len)) {
    int ret = 0;
    struct stepper_work_context *context = find_stepper_context_from_device(dev);
    static const size_t id_number_index = sizeof(SETTINGS_MODULE_NAME "/" SETTINGS_STEPPER_MODULE_NAME "/") - 1;

    if (context == NULL) {
        LOG_ERR("No context found for the stepper device.");
        return -ENODEV;
    }

    if (dt_id > 9) {
        LOG_ERR("Invalid device tree id: %zu", dt_id);
        return -EINVAL;
    }

    static char max_velocity_key[] = GENERATE_STEPPER_TEMPLATE(MAX_VELOCITY_KEY);
    max_velocity_key[id_number_index] = itoa(dt_id);
    ret = storage_func(max_velocity_key, &context->motor_max_velocity, sizeof(context->motor_max_velocity));
    if (ret < 0) {
        LOG_ERR("Failed to write max_velocity to settings: %d", ret);
        return ret;
    }
    LOG_DBG("Saved max_velocity: %f", (double)context->motor_max_velocity);

    static char max_acceleration_key[] = GENERATE_STEPPER_TEMPLATE(MAX_ACCELERATION_KEY);
    max_acceleration_key[id_number_index] = itoa(dt_id);
    ret = storage_func(max_acceleration_key, &context->motor_max_acceleration, sizeof(context->motor_max_acceleration));
    if (ret < 0) {
        LOG_ERR("Failed to write max_acceleration to settings: %d", ret);
        return ret;
    }
    LOG_DBG("Saved max_acceleration: %f", (double)context->motor_max_acceleration);

    static char microstep_key[] = GENERATE_STEPPER_TEMPLATE(MICRO_STEP_KEY);
    microstep_key[id_number_index] = itoa(dt_id);
    ret = storage_func(microstep_key, &context->microsteps, sizeof(context->microsteps));
    if (ret < 0) {
        LOG_ERR("Failed to write microsteps to settings: %d", ret);
        return ret;
    }
    LOG_DBG("Saved microstep: %d", context->microsteps);

    static char steps_per_revolution_key[] = GENERATE_STEPPER_TEMPLATE(STEPS_PER_REVOLUTION_KEY);
    steps_per_revolution_key[id_number_index] = itoa(dt_id);
    ret = storage_func(steps_per_revolution_key, &context->motor_steps_per_revolution,
                       sizeof(context->motor_steps_per_revolution));
    if (ret < 0) {
        LOG_ERR("Failed to write steps_per_revolution to settings: %d", ret);
        return ret;
    }
    LOG_DBG("Saved steps_per_revolution: %f", (double)context->motor_steps_per_revolution);

    static char flip_orientation_key[] = GENERATE_STEPPER_TEMPLATE(FLIP_LIMIT_ORIENTATION_KEY);
    flip_orientation_key[id_number_index] = itoa(dt_id);
    ret = storage_func(flip_orientation_key, &context->flip_limit_orientation, sizeof(context->flip_limit_orientation));
    if (ret < 0) {
        LOG_ERR("Failed to write flip_limit_orientation key to settings: %d", ret);
        return ret;
    }
    LOG_DBG("Saved flip_limit_orientation: %d", context->flip_limit_orientation);

    return 0;
}

#define DEFINE_STEPPER_DEVICE_SETTINGS_FUNCTION(id)                                                            \
    static int stepper_settings_set##id(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg) { \
        const struct device *dev = DEVICE_DT_GET(DT_INST(id, ll_stepper));                                     \
        struct stepper_work_context *context = find_stepper_context_from_device(dev);                          \
        return stepper_settings_set(key, len, read_cb, cb_arg, context);                                       \
    }

#define DEFINE_STEPPER_DEVICE_SETTINGS_EXPORT_FUNCTION(id)                                                             \
    static int stepper_settings_export##id(int (*storage_func)(const char *name, const void *value, size_t val_len)) { \
        const struct device *dev = DEVICE_DT_GET(DT_INST(id, ll_stepper));                                             \
        return stepper_settings_export(dev, id, storage_func);                                                         \
    }

#define DEFINE_STEPPER_DEVICE_SETTINGS_HANDLERS(id)                                                                  \
    SETTINGS_STATIC_HANDLER_DEFINE(stepper##id, SETTINGS_MODULE_NAME "/" SETTINGS_STEPPER_MODULE_NAME "/" #id, NULL, \
                                   stepper_settings_set##id, NULL, stepper_settings_export##id);

DT_FOREACH_OKAY_INST_ll_stepper(DEFINE_STEPPER_DEVICE_SETTINGS_FUNCTION);
DT_FOREACH_OKAY_INST_ll_stepper(DEFINE_STEPPER_DEVICE_SETTINGS_EXPORT_FUNCTION);

DT_FOREACH_OKAY_INST_ll_stepper(DEFINE_STEPPER_DEVICE_SETTINGS_HANDLERS);
#endif

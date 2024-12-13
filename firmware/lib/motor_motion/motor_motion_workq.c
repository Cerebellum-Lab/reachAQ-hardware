#include "motor_motion_workq.h"

#include <math.h>
#include <stdatomic.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "adi_tmc2209.h"
#include "motor_common.h"
#include "motor_motion.h"
#include "servo.h"
#include "stepper.h"

LOG_MODULE_DECLARE(motor_motion, CONFIG_LIB_MOTOR_MOTION_LOG_LEVEL);

/* ***** Forward Declaration of Callbacks ***** */
static void servo_motor_event_callback(const struct device *const dev, ll_motor_events_t event, void *arg,
                                       void *user_data);
static void stepper_motor_event_callback(const struct device *const dev, ll_motor_events_t event, void *arg,
                                         void *user_data);

/* ***** Static Context Structs Used Throughout ***** */

// Default pwm duration of the minimum angle
#define SERVO_DEFAULT_MIN_ANGLE_PWM 1000.0f
// Default pwm duration of the maximum angle
#define SERVO_DEFAULT_MAX_ANGLE_PWM 2000.0f
// Default minimum angle of servo
#define SERVO_DEFAULT_MIN_ANGLE 0.0f
// Default maximum angle of servo
#define SERVO_DEFAULT_MAX_ANGLE 180.0f
// Default angular adjustment of servo
#define SERVO_DEFAULT_ANGLE_ADJUSTMENT SERVO_DEFAULT_MIN_ANGLE

// Period between successive status checks of the stepper drivers
#define STEPPER_DRIVER_CHECK_PERIOD 100U
// Default 'min_step' of stepper (number of steps, incl. microstepping, done per pulse)
#define STEPPER_DEFAULT_MIN_STEP 1.0f
// Default 'steps_per_revolution' of stepper
#define STEPPER_DEFAULT_STEPS_PER_REVOLUTION 48

#define DEV_DEFINE_SERVO_CONTEXT(id)                                                         \
    {.dev = DEVICE_DT_GET(id),                                                               \
     .context =                                                                              \
         {                                                                                   \
             .motion_profile =                                                               \
                 {                                                                           \
                     .start_pos = 0,                                                         \
                     .end_pos = 0,                                                           \
                     .a_max = 0,                                                             \
                     .v_max = 0,                                                             \
                     .sgn = 0,                                                               \
                     .y_f = 0,                                                               \
                     .y_s = 0,                                                               \
                     .y_a = 0,                                                               \
                     .v_w = 0,                                                               \
                     .t_o = 0,                                                               \
                     .t_a = 0,                                                               \
                     .omega = 0,                                                             \
                     .k_s = 0,                                                               \
                     .t_k = 0,                                                               \
                     .t_s = 0,                                                               \
                     .t_t = 0,                                                               \
                 },                                                                          \
             .min_angle = SERVO_DEFAULT_MIN_ANGLE,                                           \
             .max_angle = SERVO_DEFAULT_MAX_ANGLE,                                           \
             .min_angle_pwm = SERVO_DEFAULT_MIN_ANGLE_PWM,                                   \
             .max_angle_pwm = SERVO_DEFAULT_MAX_ANGLE_PWM,                                   \
             .pwm_timer_increment = (DT_PROP(DT_PARENT(id), st_prescaler) + 1.0f) / 170.0f,  \
             .last_time_generated = 0.0f,                                                    \
             .last_position_generated = 0.0f,                                                \
         },                                                                                  \
     .buffers = {{0}},                                                                       \
     .current_buffer = 0,                                                                    \
     .last_calculation_ret = 0,                                                              \
     .e_stop_triggered = {.__val = 0},                                                       \
     .motion_done = true,                                                                    \
     .motion_calculation_done = true,                                                        \
     .calculation_work =                                                                     \
         {                                                                                   \
             .work =                                                                         \
                 {                                                                           \
                     .node = {.next = NULL},                                                 \
                     .handler = NULL,                                                        \
                     .queue = NULL,                                                          \
                     .flags = 0,                                                             \
                 },                                                                          \
             .timeout = {.node = {{.head = NULL}, {.tail = NULL}}, .fn = NULL, .dticks = 0}, \
             .queue = NULL,                                                                  \
         },                                                                                  \
     .servo_cb = {                                                                           \
         .func = servo_motor_event_callback,                                                 \
         .user_data = NULL,                                                                  \
         .node = {.next = NULL},                                                             \
     }},

#define DEV_DEFINE_STEPPER_CONTEXT(id)                                                       \
    {.dev = DEVICE_DT_GET(id),                                                               \
     .context =                                                                              \
         {                                                                                   \
             .motion_profile =                                                               \
                 {                                                                           \
                     .start_pos = 0,                                                         \
                     .end_pos = 0,                                                           \
                     .a_max = 0,                                                             \
                     .v_max = 0,                                                             \
                     .sgn = 0,                                                               \
                     .y_f = 0,                                                               \
                     .y_s = 0,                                                               \
                     .y_a = 0,                                                               \
                     .v_w = 0,                                                               \
                     .t_o = 0,                                                               \
                     .t_a = 0,                                                               \
                     .omega = 0,                                                             \
                     .k_s = 0,                                                               \
                     .t_k = 0,                                                               \
                     .t_s = 0,                                                               \
                     .t_t = 0,                                                               \
                 },                                                                          \
             .min_step = STEPPER_DEFAULT_MIN_STEP,                                           \
             .timer_increment = (DT_PROP(DT_PARENT(id), st_prescaler) + 1.0f) / 170e6f,      \
             .steps_per_revolution = STEPPER_DEFAULT_STEPS_PER_REVOLUTION,                   \
             .last_time_generated = 0.0f,                                                    \
             .last_position_generated = 0.0f,                                                \
         },                                                                                  \
     .buffers = {{0}},                                                                       \
     .current_buffer = 0,                                                                    \
     .last_calculation_ret = 0,                                                              \
     .e_stop_triggered = {.__val = 0},                                                       \
     .motion_done = true,                                                                    \
     .motion_calculation_done = true,                                                        \
     .calculation_work =                                                                     \
         {                                                                                   \
             .work =                                                                         \
                 {                                                                           \
                     .node = {.next = NULL},                                                 \
                     .handler = NULL,                                                        \
                     .queue = NULL,                                                          \
                     .flags = 0,                                                             \
                 },                                                                          \
             .timeout = {.node = {{.head = NULL}, {.tail = NULL}}, .fn = NULL, .dticks = 0}, \
             .queue = NULL,                                                                  \
         },                                                                                  \
     .check_driver_work =                                                                    \
         {                                                                                   \
             .work =                                                                         \
                 {                                                                           \
                     .node = {.next = NULL},                                                 \
                     .handler = NULL,                                                        \
                     .queue = NULL,                                                          \
                     .flags = 0,                                                             \
                 },                                                                          \
             .timeout = {.node = {{.head = NULL}, {.tail = NULL}}, .fn = NULL, .dticks = 0}, \
             .queue = NULL,                                                                  \
         },                                                                                  \
     .stepper_cb = {                                                                         \
         .func = stepper_motor_event_callback,                                               \
         .user_data = NULL,                                                                  \
         .node = {.next = NULL},                                                             \
     }},

struct stepper_work_context stepper_contexts[] = {DT_FOREACH_STATUS_OKAY(ll_stepper, DEV_DEFINE_STEPPER_CONTEXT)};
struct servo_work_context servo_contexts[] = {DT_FOREACH_STATUS_OKAY(ll_servo, DEV_DEFINE_SERVO_CONTEXT)};

static struct k_work_q motor_workq;
static K_THREAD_STACK_DEFINE(motor_workq_stack, 1024);

/* ***** Helper Functions ***** */

struct servo_work_context *find_servo_context_from_device(const struct device *dev) {
    for (size_t i = 0; i < ARRAY_SIZE(servo_contexts); i++) {
        if (servo_contexts[i].dev == dev) {
            return &servo_contexts[i];
        }
    }

    return NULL;
}

struct stepper_work_context *find_stepper_context_from_device(const struct device *dev) {
    for (size_t i = 0; i < ARRAY_SIZE(stepper_contexts); i++) {
        if (stepper_contexts[i].dev == dev) {
            return &stepper_contexts[i];
        }
    }

    return NULL;
}

void stepper_set_position_to_zero(const struct device *dev) {
    struct stepper_work_context *context = find_stepper_context_from_device(dev);
    if (context == NULL) {
        LOG_ERR("Stepper context not found for device");
        return;
    }

    motor_motion_stepper_set_current_position(&context->context, 0.0f);
    context->motion_done = true;
}

void servo_set_position_to_zero(const struct device *dev) {
    struct servo_work_context *context = find_servo_context_from_device(dev);
    if (context == NULL) {
        LOG_ERR("Servo context not found for device");
        return;
    }

    motor_motion_servo_set_current_position(&context->context, 0.0f);
    context->motion_calculation_done = true;
}

/* ***** Callbacks ***** */
#ifdef CONFIG_DT_HAS_LL_STEPPER_ENABLED

#define REVOLUTIONS_BACKWARDS_AFTER_HOMING 2.0f
static void stepper_motor_event_callback(const struct device *const dev, ll_motor_events_t event, void *arg,
                                         void *user_data) {
    // Due to the limitations of the GPIO callback mechanism, this data is only available
    // in DMA queue events, not the limit switch event.
    struct stepper_work_context *context = user_data;

    switch (event) {
        case LL_MOTOR_EVENT_DMA_BLOCK_COMPLETE:
            // When a block has been completed, the buffer is free to use for the next calculation, if needed
            LOG_DBG("DMA block complete");
            if (context != NULL && !context->motion_calculation_done) {
                k_work_schedule_for_queue(&motor_workq, &context->calculation_work, K_NO_WAIT);
            }
            break;
        case LL_MOTOR_EVENT_DMA_QUEUE_EMPTY:
            LOG_DBG("MOTION_DONE");
            if (context != NULL) {
                // When the driver runs out of data to send, the motion is done
                context->motion_done = true;

                // If homing was in progress, mark the final position as 0 (which will be
                // +/- REVOLUTIONS_BACKWARDS_AFTER_HOMING away from the 0 to which it was set below
                // during the limit switch event itself).
                if (atomic_flag_test_and_set(&context->homing_in_progress)) {
                    stepper_set_position_to_zero(context->dev);
                    // For homing, the v_max is cut in 4 so restore its former value.
                    stepper_set_parameters(context->dev, context->context.motion_profile.v_max * 4.0f, -1.0f, -1.0f,
                                           -1.0f);
                }
                atomic_flag_clear(&context->homing_in_progress);
            }
            break;
        case LL_MOTOR_EVENT_LIMIT_SWITCH:
            LOG_DBG("Limit switch event");
            stepper_motor_stop(dev);
            stepper_cancel_all_work(dev);
            stepper_set_position_to_zero(context->dev);

            context = find_stepper_context_from_device(dev);

            if (context != NULL) {
                context->motion_done = true;
                context->motion_calculation_done = true;
                // If we are homing, then a limit switch event is expected. Go "backwards" from the direction in which
                // we were going a certain number of revolutions to make sure that the limit switch is no longer
                // being triggered.
                if (atomic_flag_test_and_set(&context->homing_in_progress)) {
                    stepper_move_to_position(dev, context->homing_direction == LL_STEPPER_DIR_FORWARD
                                                      ? -REVOLUTIONS_BACKWARDS_AFTER_HOMING
                                                      : REVOLUTIONS_BACKWARDS_AFTER_HOMING);
                } else {
                    atomic_flag_clear(&context->homing_in_progress);
                }
            }
            const ll_motor_cfg_t *cfg = dev->config;
            LOG_ERR("Limit switch event for stepper %d", cfg->motor_id);
            break;
        default:
            LOG_WRN("Unknown stepper motor event: %d", event);
            break;
    }
}
#endif

#ifdef CONFIG_DT_HAS_LL_SERVO_ENABLED
static void servo_motor_event_callback(const struct device *const dev, ll_motor_events_t event, void *arg,
                                       void *user_data) {
    struct servo_work_context *context = user_data;

    switch (event) {
        case LL_MOTOR_EVENT_DMA_BLOCK_COMPLETE:
            // When a block has been completed, the buffer is free to use for the next calculation, if needed
            LOG_WRN("DMA block complete");
            if (context != NULL && !context->motion_calculation_done) {
                k_work_schedule_for_queue(&motor_workq, &context->calculation_work, K_NO_WAIT);
            }
            break;
        case LL_MOTOR_EVENT_DMA_QUEUE_EMPTY: {
            LOG_WRN("MOTION_DONE");
            if (context != NULL) {
                context->motion_done = true;
            }
            break;
        }
        case LL_MOTOR_EVENT_LIMIT_SWITCH:
            servo_motor_stop(dev);
            servo_cancel_all_work(dev);
            servo_set_position_to_zero(dev);
            break;
        default:
            LOG_WRN("Unknown servo motor event: %d", event);
            break;
    }
}
#endif

/* ***** Work Handlers ***** */

void stepper_cancel_all_work(const struct device *dev) {
    struct stepper_work_context *context = find_stepper_context_from_device(dev);
    k_work_cancel_delayable(&context->calculation_work);
    k_work_cancel_delayable(&context->check_driver_work);
    context->motion_done = true;
    context->motion_calculation_done = true;
}

void servo_cancel_all_work(const struct device *dev) {
    struct servo_work_context *context = find_servo_context_from_device(dev);
    k_work_cancel_delayable(&context->calculation_work);
    context->motion_done = true;
    context->motion_calculation_done = true;
}

static void servo_work_calculation_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct servo_work_context *context = CONTAINER_OF(dwork, struct servo_work_context, calculation_work);

    const ssize_t ret = motor_motion_servo_generate_displacement_table(context->buffers[context->current_buffer],
                                                                       SERVO_BUFFER_SIZE, &context->context);
    context->last_calculation_ret = ret;
    if (ret < 0) {
        LOG_ERR("Error generating servo table: %d", ret);
        return;
    }

    // Entire buffer wasn't needed which indicates all the steps have been planned and no more calculations are required
    // If nothing was calculated, then the motor is already at the target position
    if (ret < STEPPER_BUFFER_SIZE) {
        context->motion_calculation_done = true;
    }

    // Add the buffer onto the servo driver queue
    ll_queue_servo_positions(context->dev, context->buffers[context->current_buffer],
                             context->last_calculation_ret * sizeof(uint32_t), K_FOREVER);

    // Increment the buffer pointer
    context->current_buffer = (context->current_buffer + 1) % BUFS_PER_MOTOR;
}

static void stepper_work_calculation_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct stepper_work_context *context = CONTAINER_OF(dwork, struct stepper_work_context, calculation_work);

    const ssize_t ret = motor_motion_stepper_generate_timing_table(context->buffers[context->current_buffer],
                                                                   STEPPER_BUFFER_SIZE, &context->context);
    context->last_calculation_ret = ret;
    if (ret < 0) {
        LOG_ERR("Error generating stepper table.");
        return;
    }

    // Entire buffer wasn't needed which indicates all the steps have been planned and no more calculations are required
    // If nothing was calculated, then the motor is already at the target position
    if (ret < STEPPER_BUFFER_SIZE) {
        context->motion_calculation_done = true;
    }

    // Add the buffer onto the stepper driver queue
    LOG_DBG("Q buf %d [%p]", context->current_buffer, (void *)context->buffers[context->current_buffer]);
    ll_queue_stepper_positions(context->dev, context->buffers[context->current_buffer],
                               context->last_calculation_ret * sizeof(uint32_t), K_FOREVER);

    // Increment the buffer pointer
    context->current_buffer = (context->current_buffer + 1) % BUFS_PER_MOTOR;
}

static void stepper_work_check_driver_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    // struct stepper_work_context *context = CONTAINER_OF(dwork, struct stepper_work_context, check_driver_work);

    // TODO: IMPLEMENT

    k_work_reschedule_for_queue(&motor_workq, dwork, K_MSEC(STEPPER_DRIVER_CHECK_PERIOD));
}

/* ***** Initialization ***** */
static int motor_workq_init_and_start(void) {
    for (size_t i = 0; i < ARRAY_SIZE(stepper_contexts); i++) {
        struct stepper_work_context *context = &stepper_contexts[i];
        context->stepper_cb.user_data = context;
        const int ret = ll_stepper_register_callback(context->dev, &context->stepper_cb);
        if (ret < 0) {
            LOG_ERR("Error registering stepper callback: %d", ret);
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(servo_contexts); i++) {
        struct servo_work_context *context = &servo_contexts[i];
        context->servo_cb.user_data = context;
        const int ret = ll_servo_register_callback(context->dev, &context->servo_cb);
        if (ret < 0) {
            LOG_ERR("Error registering servo callback: %d", ret);
        }
    }

    k_work_queue_init(&motor_workq);

    for (size_t i = 0; i < ARRAY_SIZE(stepper_contexts); i++) {
        k_work_init_delayable(&stepper_contexts[i].calculation_work, stepper_work_calculation_handler);
        k_work_init_delayable(&stepper_contexts[i].check_driver_work, stepper_work_check_driver_handler);
    }

    for (size_t i = 0; i < ARRAY_SIZE(servo_contexts); i++) {
        k_work_init_delayable(&servo_contexts[i].calculation_work, servo_work_calculation_handler);
    }

    int ret = settings_subsys_init();
    if (ret < 0) {
        LOG_ERR("error: settings_subsys_init: %d", ret);
    } else {
        ret = settings_load();
        if (ret < 0) {
            LOG_ERR("error: settings_load: %d", ret);
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(stepper_contexts); i++) {
        struct stepper_work_context *context = &stepper_contexts[i];
        const struct device *motor_dev = context->dev;
        const ll_motor_cfg_t *motor_data = motor_dev->config;
        const struct device *stepper_driver_dev = motor_data->stepper_driver_device;
        if (stepper_driver_dev != NULL) {
            adi_tmc2209_set_microstep(stepper_driver_dev, (uint32_t)(1.0f / context->context.min_step));
        }
    }

    k_work_queue_start(&motor_workq, motor_workq_stack, K_THREAD_STACK_SIZEOF(motor_workq_stack), K_PRIO_COOP(7), NULL);
    return 0;
}

SYS_INIT(motor_workq_init_and_start, APPLICATION, 99);

/* ***** Initialize New Movements ***** */

#define UNCHANGED_UINT32 ((uint32_t) - 1)
int servo_set_parameters(const struct device *dev, const float max_velocity, const float max_acceleration,
                         const float min_angle_pwm, const float max_angle_pwm) {
    struct servo_work_context *const context = find_servo_context_from_device(dev);
    if (context == NULL) {
        return -ENODEV;
    }

    if (max_velocity > 0.0f) {
        context->context.motion_profile.v_max = max_velocity;
    }

    if (max_acceleration > 0.0f) {
        context->context.motion_profile.a_max = max_acceleration;
    }

    if (min_angle_pwm > 0.0f) {
        context->context.min_angle_pwm = min_angle_pwm;
    }

    if (max_angle_pwm > 0.0f) {
        context->context.max_angle_pwm = max_angle_pwm;
    }

    settings_save();

    return 0;
}

int servo_set_angle_parameters(const struct device *dev, const float min_angle, const float max_angle) {
    struct servo_work_context *const context = find_servo_context_from_device(dev);
    if (context == NULL) {
        return -ENODEV;
    }

    context->context.min_angle = min_angle;
    context->context.max_angle = max_angle;
    return 0;
}

int servo_move_to_position(const struct device *dev, const float target_position) {
    struct servo_work_context *context = find_servo_context_from_device(dev);
    /*
     * The library is currently not set up to allow servos to have limit switches
     * and homing. Thus, we do not check if the e-stop flag is set, because there
     * is no way to unset it.
     */

    if (context == NULL) {
        LOG_ERR("Stepper context not found for device");
        return -ENODEV;
    }

    if (context->motion_done == false) {
        LOG_ERR("Attempted to move motor while already in motion.");
        return -EBUSY;
    }

    const int ret = motor_motion_servo_init_context_struct(
        context->context.last_position_generated, target_position, context->context.motion_profile.v_max,
        context->context.motion_profile.a_max, context->context.min_angle_pwm, context->context.max_angle_pwm,
        &context->context);

    if (ret != 0) {
        LOG_ERR("Failed to initialize context struct: %d", ret);
        return -EDOM;
    }

    context->motion_done = false;
    context->motion_calculation_done = false;

    // Start calculating the motion profile and load as many blocks as possible
    for (int i = 0; i < BUFS_PER_MOTOR; i++) {
        const ssize_t gen_table_ret = motor_motion_servo_generate_displacement_table(
            context->buffers[context->current_buffer], SERVO_BUFFER_SIZE, &context->context);
        context->last_calculation_ret = gen_table_ret;
        if (ret < 0) {
            LOG_ERR("Error generating servo table: %d", gen_table_ret);
            return gen_table_ret;
        }

        // Queue the buffer to the driver to start the motor motion
        ll_queue_servo_positions(context->dev, context->buffers[i], gen_table_ret * sizeof(uint32_t), K_FOREVER);

        // If the buffer wasn't full, then this is done and the next buffer isn't needed
        if (gen_table_ret < SERVO_BUFFER_SIZE) {
            context->motion_calculation_done = true;
            break;
        }
    }

    // Increment the buffer pointer
    context->current_buffer = (context->current_buffer + 1) % BUFS_PER_MOTOR;

    return 0;
}

int servo_move_relative(const struct device *dev, const float delta_position) {
    struct servo_work_context *context = find_servo_context_from_device(dev);
    return context == NULL ? -ENODEV
                           : servo_move_to_position(dev, context->context.last_position_generated + delta_position);
}

int stepper_set_parameters(const struct device *dev, const float max_velocity, const float max_acceleration,
                           const float min_step, const float steps_per_revolution) {
    struct stepper_work_context *const context = find_stepper_context_from_device(dev);
    if (context == NULL) {
        return -ENODEV;
    }

    if (max_velocity > 0.0f) {
        context->context.motion_profile.v_max = max_velocity;
    }

    if (max_acceleration > 0.0f) {
        context->context.motion_profile.a_max = max_acceleration;
    }

    if (min_step > 0.0f) {
        context->context.min_step = min_step;
    }

    if (steps_per_revolution > 0.0f) {
        context->context.steps_per_revolution = steps_per_revolution;
    }

    settings_save();
    return 0;
}

int stepper_move_to_position(const struct device *dev, const float target_position) {
    struct stepper_work_context *context = find_stepper_context_from_device(dev);
    if (context == NULL) {
        LOG_ERR("Stepper context not found for device");
        return -ENODEV;
    }

    if (!context->motion_done) {
        LOG_ERR("Attempted to move while move is active");
        return -EBUSY;
    }

    if (atomic_flag_test_and_set(&context->e_stop_triggered)) {
        LOG_ERR("Attempted to move motor after e-stop without homing!");
        return -EBUSY;
    }
    atomic_flag_clear(&context->e_stop_triggered);

    if (fabsf(target_position - context->context.last_position_generated) < context->context.min_step) {
        LOG_WRN("Target position is the same as current position.");
        return 0;
    }

    if (target_position < context->context.last_position_generated) {
        ll_stepper_set_direction(dev, LL_STEPPER_DIR_BACKWARD);
    } else {
        ll_stepper_set_direction(dev, LL_STEPPER_DIR_FORWARD);
    }

    const int ret = motor_motion_stepper_init_context_struct(
        context->context.last_position_generated, target_position, context->context.motion_profile.v_max,
        context->context.motion_profile.a_max, context->context.min_step, context->context.timer_increment,
        &context->context);

    if (ret != 0) {
        LOG_ERR("Failed to initialize context struct: %d", ret);
        return -EDOM;
    }

    context->motion_done = false;
    context->motion_calculation_done = false;

    ll_stepper_enable(dev);

    // Start calculating the motion profile and load as many blocks as possible
    for (int i = 0; i < BUFS_PER_MOTOR; i++) {
        context->current_buffer = i;
        const ssize_t gen_table_ret =
            motor_motion_stepper_generate_timing_table(context->buffers[i], STEPPER_BUFFER_SIZE, &context->context);
        context->last_calculation_ret = gen_table_ret;

        // Error out if calculation didn't succeed
        if (gen_table_ret < 0) {
            LOG_ERR("Error generating stepper table.");
            return gen_table_ret;
        }

        // Submit the buffer to the driver to start the motor motion
        LOG_WRN("INIT:Q buf %d [%p]", context->current_buffer, (void *)context->buffers[context->current_buffer]);
        ll_queue_stepper_positions(dev, context->buffers[i], gen_table_ret * sizeof(uint32_t), K_FOREVER);

        // If the buffer wasn't full, then this is done and the next buffer isn't needed
        if (gen_table_ret < STEPPER_BUFFER_SIZE) {
            context->motion_calculation_done = true;
            break;
        }
    }

    // Increment the buffer pointer
    context->current_buffer = (context->current_buffer + 1) % BUFS_PER_MOTOR;

    return 0;
}

int stepper_move_relative(const struct device *dev, const float delta_position) {
    struct stepper_work_context *context = find_stepper_context_from_device(dev);
    return context == NULL ? -ENODEV
                           : stepper_move_to_position(dev, context->context.last_position_generated + delta_position);
}

int stepper_go_home_slowly(const struct device *dev, bool forward) {
    struct stepper_work_context *work_context = find_stepper_context_from_device(dev);
    stepper_motor_context_t *context = &work_context->context;
    if (context == NULL) {
        return -ENODEV;
    }

    const ll_motor_cfg_t *cfg = dev->config;

    if (cfg->limit_switch_pin.port == NULL) {
        LOG_ERR("Limit switch pin not set");
        return -ENOTSUP;
    }

    if (work_context->motion_done == false) {
        LOG_ERR("Attempted to move motor while already in motion.");
        return -EBUSY;
    }

    /*
     * Choose an "impossible position" that is far enough away from the current position that the motor will never
     * reach it (or, indeed, leave the constant velocity section of the motor motion profile). This is based on the
     * 500 revolutions of the motor. Approach this via a slow velocity (1/4 of the max).
     *
     * Then, rely on the limit switch/callback behavior to stop the motor and set the position (and restore the old
     * maxes).
     */
    const float IMPOSSIBLE_POSITION = 500;
    const float SLOW_VELOCITY = context->motion_profile.v_max * 0.25f;

    const int ret = stepper_set_parameters(dev, SLOW_VELOCITY, -1.0f, -1.0f, -1.0f);
    if (ret != 0) {
        LOG_ERR("Failed to set SLOW VELOCITY parameter for homing.");
        return -EDOM;
    }

    atomic_flag_clear(&work_context->e_stop_triggered);
    atomic_flag_test_and_set(&work_context->homing_in_progress);
    work_context->homing_direction = forward ? LL_STEPPER_DIR_FORWARD : LL_STEPPER_DIR_BACKWARD;

    return stepper_move_to_position(dev, forward ? IMPOSSIBLE_POSITION : -IMPOSSIBLE_POSITION);
}

/* ***** Getters ***** */
int motor_motion_stepper_get_min_step(const struct device *dev, float *min_step) {
    struct stepper_work_context *context = find_stepper_context_from_device(dev);
    if (context == NULL) {
        return -ENODEV;
    }

    *min_step = context->context.min_step;
    return 0;
}

int motor_motion_stepper_get_steps_per_revolution(const struct device *dev, float *steps_per_revolution) {
    struct stepper_work_context *context = find_stepper_context_from_device(dev);
    if (context == NULL) {
        return -ENODEV;
    }

    *steps_per_revolution = context->context.steps_per_revolution;
    return 0;
}

int motor_motion_servo_get_min_angle_pwm(const struct device *dev, float *min_angle_pwm) {
    struct servo_work_context *context = find_servo_context_from_device(dev);
    if (context == NULL) {
        return -ENODEV;
    }

    *min_angle_pwm = context->context.min_angle_pwm;
    return 0;
}

int motor_motion_servo_get_max_angle_pwm(const struct device *dev, float *max_angle_pwm) {
    struct servo_work_context *context = find_servo_context_from_device(dev);
    if (context == NULL) {
        return -ENODEV;
    }

    *max_angle_pwm = context->context.max_angle_pwm;
    return 0;
}

int motor_motion_servo_get_min_angle(const struct device *dev, float *min_angle) {
    struct servo_work_context *context = find_servo_context_from_device(dev);
    if (context == NULL) {
        return -ENODEV;
    }

    *min_angle = context->context.min_angle;
    return 0;
}

int motor_motion_servo_get_max_angle(const struct device *dev, float *max_angle) {
    struct servo_work_context *context = find_servo_context_from_device(dev);
    if (context == NULL) {
        return -ENODEV;
    }

    *max_angle = context->context.max_angle;
    return 0;
}

void set_all_e_stop_flags(void) {
    for (size_t i = 0; i < ARRAY_SIZE(stepper_contexts); i++) {
        atomic_flag_test_and_set(&stepper_contexts[i].e_stop_triggered);
    }

    for (size_t i = 0; i < ARRAY_SIZE(servo_contexts); i++) {
        atomic_flag_test_and_set(&stepper_contexts[i].e_stop_triggered);
    }
}
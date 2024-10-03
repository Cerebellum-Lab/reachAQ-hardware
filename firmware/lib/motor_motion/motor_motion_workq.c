#include "motor_motion_workq.h"

#include <stdatomic.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "motor_common.h"
#include "motor_motion.h"
#include "servo.h"
#include "stepper.h"

LOG_MODULE_DECLARE(motor_motion, CONFIG_LIB_MOTOR_MOTION_LOG_LEVEL);

/* ***** Structs ***** */
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
    atomic_flag dma_in_use;
    bool motion_calculation_done;
    struct k_work calculation_work;
    struct k_work_delayable submission_work;
    ll_servo_cb_t servo_cb;
};

struct stepper_work_context {
    const struct device *dev;
    stepper_motor_context_t context;
    stepper_buffer_set buffers;
    size_t current_buffer;
    ssize_t last_calculation_ret;
    atomic_flag dma_in_use;
    bool motion_calculation_done;
    struct k_work calculation_work;
    struct k_work_delayable submission_work;
    struct k_work_delayable check_driver_work;
    ll_stepper_cb_t stepper_cb;
};

/* ***** Callbacks ***** */

static void stepper_motor_event_callback(const struct device *const dev, ll_motor_events_t event, void *arg,
                                         void *user_data) {
    switch (event) {
        case LL_MOTOR_EVENT_DMA_BLOCK_COMPLETE:
            break;
        case LL_MOTOR_EVENT_DMA_QUEUE_EMPTY: {
            // Due to the limitations of the GPIO callback mechanism, this data is only available
            // in DMA queue events, not the limit switch event.
            struct stepper_work_context *context = user_data;
            if (context != NULL) {
                atomic_flag_clear(&context->dma_in_use);
            }
            break;
        }
        case LL_MOTOR_EVENT_LIMIT_SWITCH:
            stepper_motor_stop(dev);
            stepper_cancel_all_work(dev);
            stepper_set_position_to_zero(dev);
            // For stepper, no need to re-enable.
            break;
        default:
            LOG_WRN("Unknown stepper motor event: %d", event);
            break;
    }
}

static void servo_motor_event_callback(const struct device *const dev, ll_motor_events_t event, void *arg,
                                       void *user_data) {
    switch (event) {
        case LL_MOTOR_EVENT_DMA_BLOCK_COMPLETE:
            break;
        case LL_MOTOR_EVENT_DMA_QUEUE_EMPTY: {
            struct servo_work_context *context = user_data;
            atomic_flag_clear(&context->dma_in_use);
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

/* ***** Static Context Structs Used Throughout ***** */

// Default 'offset' of servo PWM (minimal number of pulses in the period)
#define SERVO_DEFAULT_OFFSET 1000U
// Default 'multiplier' of servo PWM (number of pulses per second)
#define SERVO_DEFAULT_MULTIPLIER 2000U
// Default minimum angle of servo
#define SERVO_DEFAULT_MIN_ANGLE 0.0f
// Default maximum angle of servo
#define SERVO_DEFAULT_MAX_ANGLE 180.0f
// Default angular adjustment of servo
#define SERVO_DEFAULT_ANGLE_ADJUSTMENT 0.0f

// Period between successive status checks of the stepper drivers
#define STEPPER_DRIVER_CHECK_PERIOD 100U
// Default 'min_step' of stepper (number of steps, incl. microstepping, done per pulse)
#define STEPPER_DEFAULT_MIN_STEP 0.25f
// Default 'steps_per_revolution' of stepper
#define STEPPER_DEFAULT_STEPS_PER_REVOLUTION 400

#define DEV_DEFINE_SERVO_CONTEXT(id)                             \
    {.dev = DEVICE_DT_GET(id),                                   \
     .context =                                                  \
         {                                                       \
             .motion_profile = {0},                              \
             .min_angle = SERVO_DEFAULT_MIN_ANGLE,               \
             .max_angle = SERVO_DEFAULT_MAX_ANGLE,               \
             .angle_adjustment = SERVO_DEFAULT_ANGLE_ADJUSTMENT, \
             .servo_multiplier = SERVO_DEFAULT_MULTIPLIER,       \
             .servo_offset = SERVO_DEFAULT_OFFSET,               \
             .last_time_generated = 0.0f,                        \
             .last_position_generated = 0.0f,                    \
         },                                                      \
     .buffers = {0},                                             \
     .current_buffer = 0,                                        \
     .last_calculation_ret = 0,                                  \
     .dma_in_use = ATOMIC_FLAG_INIT,                             \
     .motion_calculation_done = true,                            \
     .calculation_work = {0},                                    \
     .submission_work = {0},                                     \
     .servo_cb = {                                               \
         .func = servo_motor_event_callback,                     \
         .user_data = NULL,                                      \
         .node = {0},                                            \
     }},

#define DEV_DEFINE_STEPPER_CONTEXT(id)                                                  \
    {.dev = DEVICE_DT_GET(id),                                                          \
     .context =                                                                         \
         {                                                                              \
             .motion_profile = {0},                                                     \
             .min_step = STEPPER_DEFAULT_MIN_STEP,                                      \
             .timer_increment = (DT_PROP(DT_PARENT(id), st_prescaler) + 1.0f) / 170e6f, \
             .steps_per_revolution = STEPPER_DEFAULT_STEPS_PER_REVOLUTION,              \
             .last_time_generated = 0.0f,                                               \
             .last_position_generated = 0.0f,                                           \
         },                                                                             \
     .buffers = {0},                                                                    \
     .current_buffer = 0,                                                               \
     .last_calculation_ret = 0,                                                         \
     .dma_in_use = ATOMIC_FLAG_INIT,                                                    \
     .motion_calculation_done = true,                                                   \
     .calculation_work = {0},                                                           \
     .submission_work = {0},                                                            \
     .check_driver_work = {0},                                                          \
     .stepper_cb = {                                                                    \
         .func = stepper_motor_event_callback,                                          \
         .user_data = NULL,                                                             \
         .node = {0},                                                                   \
     }},

struct stepper_work_context stepper_contexts[] = {DT_FOREACH_STATUS_OKAY(ll_stepper, DEV_DEFINE_STEPPER_CONTEXT)};
struct servo_work_context servo_contexts[] = {DT_FOREACH_STATUS_OKAY(ll_servo, DEV_DEFINE_SERVO_CONTEXT)};

static struct k_work_q motor_workq;
static K_THREAD_STACK_DEFINE(motor_workq_stack, 1024);

/* ***** Helper Functions ***** */

static struct servo_work_context *find_servo_context_from_device(const struct device *dev) {
    for (size_t i = 0; i < ARRAY_SIZE(servo_contexts); i++) {
        if (servo_contexts[i].dev == dev) {
            return &servo_contexts[i];
        }
    }

    return NULL;
}

static struct stepper_work_context *find_stepper_context_from_device(const struct device *dev) {
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
    atomic_flag_clear(&context->dma_in_use);
    context->motion_calculation_done = true;
}

void servo_set_position_to_zero(const struct device *dev) {
    struct servo_work_context *context = find_servo_context_from_device(dev);
    if (context == NULL) {
        LOG_ERR("Servo context not found for device");
        return;
    }

    motor_motion_servo_set_current_position(&context->context, 0.0f);
    atomic_flag_clear(&context->dma_in_use);
    context->motion_calculation_done = true;
}

/* ***** Work Handlers ***** */

void stepper_cancel_all_work(const struct device *dev) {
    struct stepper_work_context *context = find_stepper_context_from_device(dev);
    k_work_cancel(&context->calculation_work);
    k_work_cancel_delayable(&context->submission_work);
    k_work_cancel_delayable(&context->check_driver_work);
}

void servo_cancel_all_work(const struct device *dev) {
    struct servo_work_context *context = find_servo_context_from_device(dev);
    k_work_cancel(&context->calculation_work);
    k_work_cancel_delayable(&context->submission_work);
}

static void servo_work_calculation_handler(struct k_work *work) {
    struct servo_work_context *context = CONTAINER_OF(work, struct servo_work_context, calculation_work);
    const ssize_t ret = motor_motion_servo_generate_displacement_table(context->buffers[context->current_buffer],
                                                                       SERVO_BUFFER_SIZE, &context->context);
    context->last_calculation_ret = ret;
    if (ret < 0) {
        LOG_ERR("Error generating servo table: %d", ret);
        return;
    }

    if (ret < SERVO_BUFFER_SIZE) {
        context->motion_calculation_done = true;
    }

    k_work_schedule_for_queue(&motor_workq, &context->submission_work, K_NO_WAIT);
}

static void servo_work_submission_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct servo_work_context *context = CONTAINER_OF(dwork, struct servo_work_context, submission_work);

    if (atomic_flag_test_and_set(&context->dma_in_use)) {
        k_work_reschedule_for_queue(&motor_workq, dwork, K_MSEC(10));
        return;
    }

    ll_queue_servo_positions(context->dev, context->buffers[context->current_buffer],
                             context->last_calculation_ret * sizeof(uint32_t), K_FOREVER);
    context->current_buffer = 1 - context->current_buffer;  // Alternate buffers

    if (!context->motion_calculation_done) {
        k_work_submit_to_queue(&motor_workq, &context->calculation_work);
    }
}

static void stepper_work_calculation_handler(struct k_work *work) {
    struct stepper_work_context *context = CONTAINER_OF(work, struct stepper_work_context, calculation_work);
    const ssize_t ret = motor_motion_stepper_generate_timing_table(context->buffers[context->current_buffer],
                                                                   SERVO_BUFFER_SIZE, &context->context);
    context->last_calculation_ret = ret;
    if (ret < 0) {
        LOG_ERR("Error generating servo table.");
        return;
    }

    if (ret < SERVO_BUFFER_SIZE) {
        context->motion_calculation_done = true;
    }

    k_work_schedule_for_queue(&motor_workq, &context->submission_work, K_NO_WAIT);
}

static void stepper_work_submission_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct stepper_work_context *context = CONTAINER_OF(dwork, struct stepper_work_context, submission_work);

    if (atomic_flag_test_and_set(&context->dma_in_use)) {
        k_work_reschedule_for_queue(&motor_workq, dwork, K_MSEC(10));
        return;
    }

    ll_queue_servo_positions(context->dev, context->buffers[context->current_buffer],
                             context->last_calculation_ret * sizeof(uint32_t), K_FOREVER);
    context->current_buffer = 1 - context->current_buffer;  // Alternate buffers
    if (!context->motion_calculation_done) {
        k_work_submit_to_queue(&motor_workq, &context->calculation_work);
    }
}

static void stepper_work_check_driver_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct stepper_work_context *context = CONTAINER_OF(dwork, struct stepper_work_context, check_driver_work);

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
        k_work_init(&stepper_contexts[i].calculation_work, stepper_work_calculation_handler);
        k_work_init_delayable(&stepper_contexts[i].submission_work, stepper_work_submission_handler);
        k_work_init_delayable(&stepper_contexts[i].check_driver_work, stepper_work_check_driver_handler);
    }

    for (size_t i = 0; i < ARRAY_SIZE(servo_contexts); i++) {
        k_work_init(&servo_contexts[i].calculation_work, servo_work_calculation_handler);
        k_work_init_delayable(&servo_contexts[i].submission_work, servo_work_submission_handler);
    }

    k_work_queue_start(&motor_workq, motor_workq_stack, K_THREAD_STACK_SIZEOF(motor_workq_stack), K_PRIO_COOP(7), NULL);
    return 0;
}

SYS_INIT(motor_workq_init_and_start, APPLICATION, 99);

/* ***** Initialize New Movements ***** */

#define UNCHANGED_UINT32 ((uint32_t) - 1)
int servo_set_parameters(const struct device *dev, const float max_velocity, const float max_acceleration,
                         const uint32_t servo_multiplier, const uint32_t servo_offset) {
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

    if (servo_multiplier != UNCHANGED_UINT32) {
        context->context.servo_multiplier = servo_multiplier;
    }

    if (servo_offset != UNCHANGED_UINT32) {
        context->context.servo_offset = servo_offset;
    }

    return 0;
}

int servo_move_to_position(const struct device *dev, const float target_position) {
    struct servo_work_context *context = find_servo_context_from_device(dev);
    if (context == NULL) {
        LOG_ERR("Stepper context not found for device");
        return -ENODEV;
    }

    if (context->motion_calculation_done == false) {
        LOG_ERR("Attempted to move motor while already in motion.");
        return -EBUSY;
    }

    const int ret = motor_motion_servo_init_context_struct(
        context->context.last_position_generated, target_position, context->context.motion_profile.v_max,
        context->context.motion_profile.a_max, context->context.servo_multiplier, context->context.servo_offset,
        &context->context);

    if (ret != 0) {
        LOG_ERR("Failed to initialize context struct: %d", ret);
        return -EDOM;
    }

    context->motion_calculation_done = false;

    k_work_submit_to_queue(&motor_workq, &context->calculation_work);

    return 0;
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
    return 0;
}

int stepper_move_to_position(const struct device *dev, const float target_position) {
    struct stepper_work_context *context = find_stepper_context_from_device(dev);
    if (context == NULL) {
        LOG_ERR("Stepper context not found for device");
        return -ENODEV;
    }

    if (context->motion_calculation_done == false) {
        LOG_ERR("Attempted to move motor while already in motion.");
        return -EBUSY;
    }

    const int ret = motor_motion_stepper_init_context_struct(
        context->context.last_position_generated, target_position, context->context.motion_profile.v_max,
        context->context.motion_profile.a_max, context->context.min_step, context->context.timer_increment,
        &context->context);

    if (ret != 0) {
        LOG_ERR("Failed to initialize context struct: %d", ret);
        return -EDOM;
    }

    context->motion_calculation_done = false;

    k_work_submit_to_queue(&motor_workq, &context->calculation_work);

    return 0;
}

int stepper_go_home_slowly(const struct device *dev, bool forward) {
    const float IMPOSSIBLE_POSITION = 1e6f;
    const float SLOW_VELOCITY = 100.0f;
    const float SLOW_ACCELERATION = 10.0f;
    struct stepper_work_context *context = find_stepper_context_from_device(dev);
    const ll_motor_cfg_t *cfg = dev->config;

    if (context == NULL) {
        LOG_ERR("Stepper context not found for device");
        return -ENODEV;
    }

    if (cfg->limit_switch_pin.port == NULL) {
        LOG_ERR("Limit switch pin not set");
        return -ENOTSUP;
    }

    if (context->motion_calculation_done == false) {
        LOG_ERR("Attempted to move motor while already in motion.");
        return -EBUSY;
    }

    const float last_position = context->context.last_position_generated;

    const int ret = motor_motion_stepper_init_context_struct(
        last_position, forward ? IMPOSSIBLE_POSITION : -IMPOSSIBLE_POSITION, SLOW_VELOCITY, SLOW_ACCELERATION,
        context->context.min_step, context->context.timer_increment, &context->context);

    if (ret != 0) {
        LOG_ERR("Failed to initialize context struct: %d", ret);
        return -EDOM;
    }

    context->motion_calculation_done = false;

    k_work_submit_to_queue(&motor_workq, &context->calculation_work);

    return 0;
}

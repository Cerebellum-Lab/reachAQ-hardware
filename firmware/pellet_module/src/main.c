#include <app_version.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "generic_gpios.h"
#include "servo.h"
#include "stepper.h"
#include "tone_generator.h"

LOG_MODULE_REGISTER(app);

static const struct device *gpio_dev = DEVICE_DT_GET_ANY(ll_generic_gpios);

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),

static const struct device *stepper_devs[] = {DT_FOREACH_STATUS_OKAY(ll_stepper, DEV_GET_COMMA)};

static const struct device *servo_devs[] = {DT_FOREACH_STATUS_OKAY(ll_servo, DEV_GET_COMMA)};

const struct device *tone_generator = DEVICE_DT_GET_ANY(ll_tone_generator);

K_SEM_DEFINE(thread_startup_sem, 0, 1);

static void on_input_func(struct input_event *evt) {
    LOG_INF("Input event: type=%d, code=%d, value=%d", evt->type, evt->code, evt->value);
}

INPUT_CALLBACK_DEFINE(NULL, on_input_func);

static uint32_t saw_start[50];
static uint32_t saw_ramp[90];
static uint32_t saw_end[50];

static uint32_t fast_steps[4096];
static uint32_t step_range[8192];

int main() {
    LOG_INF("Autotrainer Pellet Module v%s", APP_VERSION_STRING);

    // Read the DeviceType bits to make sure this is a Pellet Module
    uint32_t device_type;
    ll_generic_gpio_read(gpio_dev, 0x3, &device_type);

    // Read the NodeID to program up the CAN_ID properly
    uint32_t node_id;
    ll_generic_gpio_read(gpio_dev, 0xC, &node_id);
    node_id = (node_id >> 2) & 0x3;

    // Print out the device type and node ID
    LOG_INF("CAN_ID: 0x%X", (node_id << 2) | device_type);

    // Create a saw tooth wave for 0 to 180 degrees on the servo with a hold at the start and end
    for (int i = 0; i < ARRAY_SIZE(saw_start); i++) {
        saw_start[i] = 1000;
    }
    sys_cache_data_flush_range(saw_start, sizeof(saw_start));

    for (int i = 0; i < ARRAY_SIZE(saw_ramp); i++) {
        saw_ramp[i] = (i * (4000 / (ARRAY_SIZE(saw_ramp) - 1))) + 1000;
    }
    sys_cache_data_flush_range(saw_ramp, sizeof(saw_ramp));

    for (int i = 0; i < ARRAY_SIZE(saw_end); i++) {
        saw_end[i] = 5000;
    }
    sys_cache_data_flush_range(saw_end, sizeof(saw_end));

    // Create a pattern of fast steps
    for (int i = 0; i < ARRAY_SIZE(fast_steps); i++) {
        fast_steps[i] = 1;
    }
    sys_cache_data_flush_range(fast_steps, sizeof(fast_steps));

    // Create a wide range of steps
    for (int i = 0; i < ARRAY_SIZE(step_range); i++) {
        step_range[i] = 0xFFFF / (i + 1);
    }
    sys_cache_data_flush_range(step_range, sizeof(step_range));

    k_sem_give(&thread_startup_sem);

    while (true) {
        k_msleep(500);
    }
}

void servo_callback_func(const struct device *dev, ll_servo_events_t event, void *arg, void *user_data) {
    LOG_INF("Servo %p event: %d", dev, event);
}

ll_servo_cb_t servo_cb = {
    .func = servo_callback_func,
};

void servo_thread_entry(void *p1, void *p2, void *p3) {
    // Wait for the main thread to populate the data buffers before using them
    k_sem_take(&thread_startup_sem, K_FOREVER);
    k_sem_give(&thread_startup_sem);

    for (int i = 0; i < ARRAY_SIZE(servo_devs); i++) {
        ll_servo_register_callback(servo_devs[i], &servo_cb);
    }

    while (true) {
        int ret;
        LOG_INF("Queueing servo positions");

        for (int i = 0; i < ARRAY_SIZE(servo_devs); i++) {
            ret = ll_queue_servo_positions(servo_devs[i], saw_start, sizeof(saw_start), K_FOREVER);
            ret |= ll_queue_servo_positions(servo_devs[i], saw_ramp, sizeof(saw_ramp), K_FOREVER);
            ret |= ll_queue_servo_positions(servo_devs[i], saw_end, sizeof(saw_end), K_FOREVER);
            if (ret < 0) {
                LOG_ERR("Failed to start servo DMA: %d", ret);
                break;
            }
        }
    }
}

void stepper_callback_func(const struct device *dev, ll_stepper_events_t event, void *arg, void *user_data) {
    LOG_INF("Stepper %p event: %d", dev, event);
}

ll_servo_cb_t stepper_cb = {
    .func = stepper_callback_func,
};

void stepper_thread_entry(void *p1, void *p2, void *p3) {
    // Wait for the main thread to populate the data buffers before using them
    k_sem_take(&thread_startup_sem, K_FOREVER);
    k_sem_give(&thread_startup_sem);

    // Register the stepper callback
    for (int i = 0; i < ARRAY_SIZE(stepper_devs); i++) {
        ll_stepper_register_callback(stepper_devs[i], &stepper_cb);
    }

    ll_stepper_set_direction(stepper_devs[0], LL_STEPPER_DIR_FORWARD);
    ll_stepper_set_direction(stepper_devs[1], LL_STEPPER_DIR_BACKWARD);

    while (true) {
        int ret;
        LOG_INF("Queueing stepper positions");
        for (int i = 0; i < ARRAY_SIZE(stepper_devs); i++) {
            // ret = ll_queue_stepper_positions(stepper_devs[i], saw_ramp, sizeof(saw_ramp), K_FOREVER);
            // ret = ll_queue_stepper_positions(stepper_devs[i], fast_steps, sizeof(fast_steps), K_FOREVER);
            ret = ll_queue_stepper_positions(stepper_devs[i], step_range, sizeof(step_range), K_FOREVER);
            if (ret < 0) {
                LOG_ERR("Failed to start stepper DMA: %d", ret);
                break;
            }
        }
    }
}

void tone_generator_thread_entry(void *p1, void *p2, void *p3) {
    // Wait for the main thread to populate the data buffers before using them
    k_sem_take(&thread_startup_sem, K_FOREVER);
    k_sem_give(&thread_startup_sem);

    /**
     * Tone Generator Example usage
     */

    /* Non-blocking usage example - playing 1KHz tone for 5 seconds */
    ll_tone_generator_play_tone(tone_generator, 1000, 5000);
}

#if DT_HAS_COMPAT_STATUS_OKAY(ll_servo)
K_THREAD_DEFINE(servo_tid, 1024, servo_thread_entry, NULL, NULL, NULL, 5, 0, 0);
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(ll_stepper)
K_THREAD_DEFINE(stepper_tid, 1024, stepper_thread_entry, NULL, NULL, NULL, 5, 0, 0);
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(ll_tone_generator)
K_THREAD_DEFINE(tone_generator_tid, 1024, tone_generator_thread_entry, NULL, NULL, NULL, 5, 0, 0);
#endif
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "jerrycan.h"

LOG_MODULE_DECLARE(jerrycan, LOG_LEVEL_DBG);

#define DOOR_1_NODE_ID DT_NODELABEL(door_sensor_1)
#define DOOR_1 DEVICE_DT_GET(DOOR_1_NODE_ID)
#define DOOR_2_NODE_ID DT_NODELABEL(door_sensor_2)
#define DOOR_2 DEVICE_DT_GET(DOOR_2_NODE_ID)
#define DOOR_3_NODE_ID DT_NODELABEL(door_sensor_3)
#define DOOR_3 DEVICE_DT_GET(DOOR_3_NODE_ID)

#if DT_NODE_EXISTS(DOOR_1_NODE_ID)  // a reasonable check to see if the doors are defined
enum {
    DOOR_IDX_1 = 0,
    DOOR_IDX_2 = 1,
    DOOR_IDX_3 = 2,
};

typedef struct {
    const struct gpio_dt_spec spec;  // access point to get current state of GPIOs
    bool value;                      // current value
} door_data_t;

static door_data_t gDoors[DOOR_SENSOR_COUNT] = {{.spec = GPIO_DT_SPEC_GET_OR(DOOR_1_NODE_ID, gpios, {0}), .value = 0},
                                                {.spec = GPIO_DT_SPEC_GET_OR(DOOR_2_NODE_ID, gpios, {0}), .value = 0},
                                                {.spec = GPIO_DT_SPEC_GET_OR(DOOR_3_NODE_ID, gpios, {0}), .value = 0}};

/**
 * Transmit message on CAN for the door status data
 */
static void door_sensor_transmit_msg() {
    jerrycan_msg_t msg;

    msg.type = JERRYCAN_CMD_DOOR_SENSOR;

    uint8_t opened = 0;
    for (int i = 0; i < DOOR_SENSOR_COUNT; ++i) {
        opened |= (gDoors[i].value & 0x1) << i;
    }
    msg.doors.opened = opened;

    jerrycan_tx(&msg, K_NO_WAIT);
}

/**
 * Handle events (opened/closed transitions) for the doors.
 * Event codes match those in the .dts file for the project.
 *
 * @param event
 */
static void door_sensor_handler(struct input_event *event) {
    switch (event->code) {
        case INPUT_KEY_5:
            gDoors[DOOR_IDX_1].value = event->value;
            break;

        case INPUT_KEY_6:
            gDoors[DOOR_IDX_2].value = event->value;
            break;

        case INPUT_KEY_7:
            gDoors[DOOR_IDX_3].value = event->value;
            break;

        default:
            break;
    }

    LOG_DBG("Got Door Sensor: #%d -> %d\n", event->code, event->value);

    door_sensor_transmit_msg();
}

INPUT_CALLBACK_DEFINE(NULL, door_sensor_handler);

/**
 * Timer callback to transmit the current state of the doors (opened/closed).
 *
 * @param (timer)
 */
static void door_sensor_timer_expired(struct k_timer *) { door_sensor_transmit_msg(); }

K_TIMER_DEFINE(gPeriodicTimer, door_sensor_timer_expired, NULL);

/**
 * Initialize the door sensor module. Intialization occurs automatically on
 * application start-up.
 *
 * Reads the current state of each door; all other updates will be via events.
 *
 * @retval <0 - Error
 * @retval 0 - OK
 */
static int door_init() {
    k_timer_start(&gPeriodicTimer, K_MSEC(1000), K_MSEC(CONFIG_LIB_JERRYCAN_DOOR_SENSOR_TX_PERIOD_MS));

    gDoors[DOOR_IDX_1].value = gpio_pin_get_dt(&gDoors[DOOR_IDX_1].spec);
    gDoors[DOOR_IDX_2].value = gpio_pin_get_dt(&gDoors[DOOR_IDX_2].spec);
    gDoors[DOOR_IDX_3].value = gpio_pin_get_dt(&gDoors[DOOR_IDX_3].spec);

    for (int i = 0; i < DOOR_SENSOR_COUNT; ++i) {
        LOG_INF("Inital Door #%d Sensor: %d", i, gDoors[i].value);
    }

    LOG_INF("Door Sensor Initialized!");

    return 0;
}

SYS_INIT(door_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);

#else
#warning "Door Sensor support disabled."
#endif
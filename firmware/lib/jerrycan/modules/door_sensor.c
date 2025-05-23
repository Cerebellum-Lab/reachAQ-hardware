#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "jerrycan.h"

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

#define DOOR_1_NODE_ID DT_NODELABEL(door_sensor_1)
#define DOOR_1 DEVICE_DT_GET(DOOR_1_NODE_ID)
#define DOOR_2_NODE_ID DT_NODELABEL(door_sensor_2)
#define DOOR_2 DEVICE_DT_GET(DOOR_2_NODE_ID)
#define DOOR_3_NODE_ID DT_NODELABEL(door_sensor_3)
#define DOOR_3 DEVICE_DT_GET(DOOR_3_NODE_ID)
#define EXT_BUTTON_1_NODE_ID DT_NODELABEL(ext_button_1)
#define EXT_BUTTON_1 DEVICE_DT_GET(EXT_BUTTON_1_NODE_ID)

enum {
    DOOR_1_IDX = 0,
    DOOR_2_IDX = 1,
    DOOR_3_IDX = 2,
    EXT_BUTTON_1_IDX = 3,
};

typedef struct {
    const struct gpio_dt_spec spec;  // access point to get current state of GPIOs
    bool value;                      // current value
} door_data_t;

static door_data_t gKeys[] = {{.spec = GPIO_DT_SPEC_GET_OR(DOOR_1_NODE_ID, gpios, {0}), .value = 0},
                              {.spec = GPIO_DT_SPEC_GET_OR(DOOR_2_NODE_ID, gpios, {0}), .value = 0},
                              {.spec = GPIO_DT_SPEC_GET_OR(DOOR_3_NODE_ID, gpios, {0}), .value = 0},
                              {.spec = GPIO_DT_SPEC_GET_OR(EXT_BUTTON_1_NODE_ID, gpios, {0}), .value = 0}};

/**
 * Transmit message on CAN for the door status data
 */
static void door_sensor_transmit_msg(struct k_timer *) {
    jerrycan_msg_t msg;

    msg.type = JERRYCAN_CMD_DOOR_SENSOR;

    msg.doors.door1 = gKeys[DOOR_1_IDX].value;
    msg.doors.door2 = gKeys[DOOR_2_IDX].value;
    msg.doors.door3 = gKeys[DOOR_3_IDX].value;
    msg.doors.external_button = !gKeys[EXT_BUTTON_1_IDX].value;

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
        case INPUT_KEY_0:
            gKeys[EXT_BUTTON_1_IDX].value = event->value ? 0 : 1;
            break;

        case INPUT_KEY_5:
            gKeys[DOOR_1_IDX].value = event->value ? 0 : 1;
            break;

        case INPUT_KEY_6:
            gKeys[DOOR_2_IDX].value = event->value ? 0 : 1;
            break;

        case INPUT_KEY_7:
            gKeys[DOOR_3_IDX].value = event->value ? 0 : 1;
            break;

        default:
            break;
    }

    LOG_INF("Got Door Sensor: #%d -> %d", event->code, event->value);
}

INPUT_CALLBACK_DEFINE(NULL, door_sensor_handler);

K_TIMER_DEFINE(gPeriodicTimer, door_sensor_transmit_msg, NULL);

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

    for (int i = 0; i < sizeof(gKeys) / sizeof(gKeys[0]); ++i) {
        gKeys[i].value = !gpio_pin_get_dt(&gKeys[i].spec);
        LOG_INF("Inital Door #%d Sensor: %d", i, gKeys[i].value);
    }

    LOG_INF("Door Sensor Initialized!");

    return 0;
}

SYS_INIT(door_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);

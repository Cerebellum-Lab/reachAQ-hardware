#include <app_version.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "jerrycan.h"

LOG_MODULE_REGISTER(app);

/*
 * Heartbeat LED
 */
static const struct gpio_dt_spec heartbeat_led_dev = GPIO_DT_SPEC_GET(DT_NODELABEL(status_led), gpios);

static void heartbeat_led_off(struct k_timer *timer) { gpio_pin_set_dt(&heartbeat_led_dev, 0); }

K_TIMER_DEFINE(heartbeat_led_timer, heartbeat_led_off, NULL);

static void heartbeat_led_start() {
    // Turn on the LED and then start a timer that will turn it off in 100ms
    gpio_pin_configure_dt(&heartbeat_led_dev, GPIO_OUTPUT);
    gpio_pin_set_dt(&heartbeat_led_dev, 1);
    k_timer_start(&heartbeat_led_timer, K_MSEC(100), K_NO_WAIT);
}

/*
 * E-STOP
 */
static void estop_handler(bool active) {
    // TODO: Make sure that all of the motors are stopped and status messages reflect the E-STOP state

    if (active) {
        LOG_ERR("!!! E-STOP !!! E-STOP !!! E-STOP !!! E-STOP !!!");
    } else {
        LOG_INF("E-STOP cleared");
    }
}

/*
 * Message handling
 */
static int handle_message(jerrycan_msg_t *msg) {
    LOG_INF("CAN RX: type=%d", msg->type);
    LOG_HEXDUMP_INF(msg->payload, sizeof(msg->payload), "Payload");

    switch (msg->type) {
        // Device will only send these messages, not receive them
        case JERRYCAN_CMD_STATUS:
            LOG_WRN("Received unexpected CAN message type: %d", msg->type);
            break;

        case JERRYCAN_CMD_ESTOP: {
            jerrycan_cmd_estop_t *estop = &msg->estop;

            // If any byte of the payload is not 0xFF, trigger the E-STOP
            for (int i = 0; i < ARRAY_SIZE(estop->payload); i++) {
                if (estop->payload[i] != 0xFF) {
                    estop_handler(true);
                    return 0;
                }
            }

            // If all bytes of the payload are 0xFF, clear the E-STOP
            estop_handler(false);
            break;
        }

        case JERRYCAN_CMD_HEARTBEAT: {
            LOG_INF("Received heartbeat message");
            heartbeat_led_start();
            break;
        }

        case JERRYCAN_CMD_STEPPER_MOVE: {
            jerrycan_cmd_stepper_move_t *move = &msg->stepper_move;
            LOG_INF(
                "Received stepper move message: motor_id=%d, abs_or_rel=%d, position=%d, max_velocity=%d, "
                "max_acceleration=%d",
                move->motor_id, move->abs_or_rel, move->position, move->max_velocity, move->max_acceleration);
            break;
        }

        case JERRYCAN_CMD_SERVO_MOVE: {
            jerrycan_cmd_servo_move_t *move = &msg->servo_move;
            LOG_INF(
                "Received servo move message: motor_id=%d, abs_or_rel=%d, position=%d, max_velocity=%d, "
                "max_acceleration=%d",
                move->motor_id, move->abs_or_rel, move->position, move->max_velocity, move->max_acceleration);
            break;
        }

        default: {
            LOG_WRN("Received unknown message type: %d", msg->type);
            break;
        }
    }

    return 0;
}

int main() {
    LOG_INF("Autotrainer Pellet Module v%s", APP_VERSION_STRING);

    while (true) {
        jerrycan_msg_t msg;
        if (jerrycan_rx_poll(&msg, K_MSEC(100)) == 0) {
            handle_message(&msg);
        }

        jerrycan_msg_t status_msg = {.type = JERRYCAN_CMD_STATUS};

        jerrycan_tx(&status_msg);
    }
}

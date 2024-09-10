#include <zephyr/drivers/gpio.h>

#include "jerrycan.h"

void jerrycan_status_tx() {
    // Send a status message
    // FIXME: TODO: Actually read the status of the motors and sensors and populate this message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_STATUS,
        .status =
            {
                .estop_active = 0,
                .stepper_status0 = 0,
                .stepper_status1 = 0,
                .stepper_status2 = 0,
                .servo_status0 = 0,
                .servo_status1 = 0,
                .servo_status2 = 0,
                .limit_switch0 = 0,
                .limit_switch1 = 0,
                .limit_switch2 = 0,
                .button0 = 0,
                .stim0 = 0,
                .stim1 = 0,
                .stim2 = 0,
                .stim3 = 0,
            },
    };

    jerrycan_tx(&msg);
}

K_TIMER_DEFINE(jerrycan_status_timer, jerrycan_status_tx, NULL);

static int jerrycan_status_init() {
    // Start the timer that will send the status message periodically
    k_timer_start(&jerrycan_status_timer, K_MSEC(100), K_MSEC(CONFIG_LIB_JERRYCAN_STATUS_TX_RATE));
    return 0;
}

SYS_INIT(jerrycan_status_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);

#pragma once
#include <zephyr/sys/slist.h>

typedef enum {
    LL_MOTOR_EVENT_ERROR,
    LL_MOTOR_EVENT_DMA_BLOCK_COMPLETE,
    LL_MOTOR_EVENT_DMA_QUEUE_EMPTY,
    LL_MOTOR_EVENT_LIMIT_SWITCH,
} ll_motor_events_t;

typedef void(ll_motor_event_callback_t)(const struct device *dev, ll_motor_events_t event, void *arg, void *user_data);

typedef struct {
    ll_motor_event_callback_t *func;
    void *user_data;
    sys_snode_t node;
} ll_motor_cb_t;
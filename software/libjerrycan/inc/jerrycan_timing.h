#pragma once

/*
    The timing trailer: 20 bytes a board appends after the uuid byte of any frame
    with room for them, saying when the thing the frame describes happened on the
    board's clock. The layout is the host's - reachAQ decodes it in
    auto-trainer-device/src/autotrainer/device/socketcan_jerrycan.py as struct
    "<BBBBIIQ" (tag, version, kind, flags, boot_id, sequence, board_time_us).

    Plain C with no Zephyr or Linux dependency, so the firmware, the host library
    and host-run tests can all include it.
*/

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JERRYCAN_TIMING_TRAILER_TAG 0xA7
#define JERRYCAN_TIMING_TRAILER_VERSION 1
#define JERRYCAN_TIMING_TRAILER_SIZE 20

/* What board_time_us marks. The values are reachAQ's JerryCANTimestampKind. */
typedef enum {
    JERRYCAN_TIMESTAMP_TX_QUEUE = 0,         /* when the frame was queued for sending */
    JERRYCAN_TIMESTAMP_OBSERVED = 1,         /* when the board saw an external event (an edge, a clap) */
    JERRYCAN_TIMESTAMP_COMMAND_RECEIVED = 2, /* when the command this frame answers arrived */
    JERRYCAN_TIMESTAMP_PHYSICAL_START = 3,   /* when an output physically started */
    JERRYCAN_TIMESTAMP_COMPLETED = 4,        /* when an output physically finished */
    JERRYCAN_TIMESTAMP_SAMPLED = 5,          /* when the reported value was sampled */
} jerrycan_timestamp_kind_t;

/* Events were merged by a rate cap; board_time_us is the latest of them. */
#define JERRYCAN_TIMING_FLAG_COALESCED (1U << 0)

typedef struct {
    uint8_t kind; /* jerrycan_timestamp_kind_t */
    uint8_t flags;
    uint32_t boot_id;
    uint32_t sequence;
    uint64_t board_time_us;
} jerrycan_timing_t;

#ifdef __cplusplus
}
#endif

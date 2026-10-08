#include "timing_trailer.h"

static void put_le(uint8_t *out, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; i++) {
        out[i] = (uint8_t)(value >> (8U * i));
    }
}

size_t jerrycan_timing_append_trailer(uint8_t *data, size_t used, size_t capacity, const jerrycan_timing_t *timing) {
    if (used > capacity || capacity - used < JERRYCAN_TIMING_TRAILER_SIZE) {
        return used;
    }

    uint8_t *out = data + used;
    out[0] = JERRYCAN_TIMING_TRAILER_TAG;
    out[1] = JERRYCAN_TIMING_TRAILER_VERSION;
    out[2] = timing->kind;
    out[3] = timing->flags;
    put_le(out + 4, timing->boot_id, 4);
    put_le(out + 8, timing->sequence, 4);
    put_le(out + 12, timing->board_time_us, 8);

    return used + JERRYCAN_TIMING_TRAILER_SIZE;
}

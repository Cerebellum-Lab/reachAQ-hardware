#pragma once

#include <stddef.h>
#include <stdint.h>

#include "jerrycan_timing.h"

/*
    Append the timing trailer at data[used], little-endian. Returns the new length,
    or `used` unchanged when the trailer does not fit in `capacity`: a frame whose
    payload leaves fewer than 20 bytes free simply carries no trailer.
*/
size_t jerrycan_timing_append_trailer(uint8_t *data, size_t used, size_t capacity, const jerrycan_timing_t *timing);

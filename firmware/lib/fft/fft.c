#include "fft.h"

#include <arm_math.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <zephyr/posix/unistd.h>

LOG_MODULE_REGISTER(fft);

/**
 * Initialize the FFT structure based on the given size.
 *
 * @param fft - Non-NULL pointer to a Fft instance
 * @param length [16..4096], powers of 2
 *
 * @return Flag: Success/Fail
 *
 */
[[nodiscard]] bool fft_initialize(Fft* fft, const int length) {
    arm_status rc;

    switch (length) {
        case 16:
            rc = arm_cfft_init_16_f32(&(fft->fft));
            break;
        case 32:
            rc = arm_cfft_init_32_f32(&(fft->fft));
            break;
        case 64:
            rc = arm_cfft_init_64_f32(&(fft->fft));
            break;
        case 128:
            rc = arm_cfft_init_128_f32(&(fft->fft));
            break;
        case 256:
            rc = arm_cfft_init_256_f32(&(fft->fft));
            break;
        case 512:
            rc = arm_cfft_init_512_f32(&(fft->fft));
            break;
        case 1024:
            rc = arm_cfft_init_1024_f32(&(fft->fft));
            break;
        case 2048:
            rc = arm_cfft_init_2048_f32(&(fft->fft));
            break;
        case 4096:
            rc = arm_cfft_init_4096_f32(&(fft->fft));
            break;
        default:
            LOG_ERR("FFT Init failed! Invalid length = %d", length);
            return false;
    }

    if (rc != ARM_MATH_SUCCESS) {
        fft->length = 0;
        LOG_ERR("FFT Init failed! Status = %d", rc);
        return false;
    }

    fft->length = length;

    return true;
}

void fft_calculate_frequency(const Fft* fft, float* fftBuffer) { arm_cfft_f32(&(fft->fft), fftBuffer, 0, 1); }

void fft_calculate_magnitude(const Fft* fft, const float* fftBuffer, float* fftMagnitude) {
    // As the FFT results are mirrored, calculate only DC [0], freq[1..N/2), and Nyquest [N/2]
    arm_cmplx_mag_f32(fftBuffer, fftMagnitude, fft->length / 2 + 1);
}

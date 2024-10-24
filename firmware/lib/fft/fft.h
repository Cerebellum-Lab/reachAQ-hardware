#ifndef FFT_H
#define FFT_H

#include <arm_math.h>
#include <stdbool.h>

#define FFT_MAXIMUM_SAMPLE_SIZE 4096

/**
 * Structure to manage resources for an initialized FFT data set
 */
typedef struct _fft {
    arm_cfft_instance_q31 fft;
    int length;
} Fft;

/**
 * Initialize the FFT system to use a specific length of buffer.
 *
 * @param fft - Non-null pointer
 * @param length - Number of elements to process on each FFT
 *
 * @return Flag: Success/Fail
 */
[[nodiscard]] bool fft_initialize(Fft* fft, int length);

/**
 * Perform an FFT on the given buffer (must be a set of complex values, of init::length.
 * Processing happens in-place.
 *
 * Result is in first length elements of the buffer.
 *
 * @param fft - Initialized Fft structure
 * @param[in,out] fftBuffer - In: data set; Out: FFT of data set. Can not be NULL.
 */
void fft_process(const Fft* fft, q31_t* fftBuffer);

/**
 * Calculate the magnitudes of an FFT result.
 *
 * @param fft - Initialized Fft structure
 * @param fftBuffer - FFT result (from fft_process). Buffer must be of size init::length. Can not be NULL.
 * @param[out] fftMagnitude - Magnitude. Buffer must be of size init::length. Can not be NULL.
 */
void fft_process_magnitude(const Fft* fft, const q31_t* fftBuffer, q31_t* fftMagnitude);

#endif

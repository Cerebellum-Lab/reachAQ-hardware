/**
 * Module to handle Audio (Microphone) input, calculate FFT frequency and
 * magnitude on the sampled data, and transmit the data on the CAN bus to the
 * host controller.
 *
 * Data collection and processing is performed in its own thread. The thread
 * yields control when it is waiting for new data.
 *
 * Data is collected at all times, as the data from the microphone is streaming.
 * Each data set is AUDIO_FFT_SIZE elements long.
 * Data is processed and transmitted on the CAN bus at AUDIO_UPDATE_RATE.
 *
 * When transmitting the data on the CAN bus, it sends the data set in a series
 * of CAN messages:
 * + The first message has an ID of JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_START;
 *   its payload is a unique packet number.
 * + The next set of messages has an ID of JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_CONT;
 *   its payload is a set of 2 frequency magnitude numbers as IEEE 32-bit floating
 *   point number.
 * + The last message has an ID of JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_END;
 *   its payload is the same unique packet number in the _START message.
 *
 * The receiving entity should accumulate the packets, ensuring that the _START
 * and _END packets have the same packet number, and that the correct number of
 * _CONT messages are also received. The protocol assumes there is no out-of-
 * order transmissions, but that there could be dropped packets.
 */

#include <sys/param.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fft.h"
#include "generic_gpios.h"
#include "jerrycan.h"
#include "microphone.h"

#define AUDIO_FFT_SIZE DT_PROP(DT_N_NODELABEL_microphone, block_size)
#define AUDIO_UPDATE_RATE 10                            // (Hz)
#define AUDIO_UPDATE_PERIOD (1000 / AUDIO_UPDATE_RATE)  // msec
#define AUDIO_PRIORITY 10

LOG_MODULE_REGISTER(audio_in, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

/**
 * Process the sampled audio data, transmitting it on the CAN bus when it's
 * done.
 *
 * @param packetNumber
 * @param rawData - Can NOT be NULL.
 */
static void audio_process_data(uint64_t packetNumber, const uint32_t* rawData);

/**
 * Translate the raw data (32-bit integers) to a complex data set, where the
 * imaginary potion is 0.
 *
 * @param rawData - Can NOT be NULL
 * @param[out] fftBuffer - This buffer must be 2x larger than the rawData buffer. Can NOT be NULL
 * @param length - Number of elements in rawData buffer
 */
static void audio_populate_complex_vector(const uint32_t* rawData, float* fftBuffer, size_t length);

/**
 * Transmit the given payload (N bytes; where N is defined by the CAN payload
 * size) on the CAN bus.
 *
 * @param payload - Can NOT be NULL
 * @param length - Length of payload to transfer. Must be <= JERRYCAN_MAX_PAYLOAD_SIZE.
 */
static void audio_transmit_data(const void* payload, size_t length);

/**
 * Report the calculated magnitude data on the CAN bus.
 *
 * @param packetNumber - Packet number
 * @param magnitude - Magnitude data. Can NOT be NULL
 * @param length - Number of elements in the magnitude vector.
 */
static void audio_report_data(uint64_t packetNumber, const float* magnitude, size_t length);

/**
 * Thread method.
 *
 * + Initialize the system
 * + Wait for data from the microphone
 * + At AUDIO_UPDATE_RATE, process and transmit magnitude data on the CAN bus
 *
 */
static void audio_thread(void*, void*, void*);

/* -------------------------------------------------------------------------- */

static void audio_thread(void*, void*, void*) {
    struct k_timer timer;
    const struct device* microphone = DEVICE_DT_GET_ANY(ll_microphone);

    if (!microphone) {
        LOG_ERR("Microphone not defined in the DTS");
        return;
    }

    LOG_INF("THREAD: AudioIn:: STARTED.");

    // Set up periodic timer
    k_timer_init(&timer, NULL, NULL);
    k_timer_start(&timer, K_MSEC(1000), K_MSEC(AUDIO_UPDATE_PERIOD));

    if (ll_microphone_enable_reads(microphone, true)) {
        uint64_t packetNumber = 0;

        while (true) {
            void* rawData;
            uint32_t length;

            const int rc = ll_microphone_read(microphone, &rawData, &length);

            if ((0 == rc) && (length >= AUDIO_FFT_SIZE)) {
                // status_get returns the # of times timer expired since last call
                if (k_timer_status_get(&timer) > 0) {
                    audio_process_data(++packetNumber, rawData);
                }

                ll_microphone_release_buffer(microphone, (void*)rawData);
            } else if (rc != -EAGAIN) {
                LOG_ERR("Microphone Read Failed. Bailing");
                break;
            }
        }
    }

    LOG_INF("THREAD: AudioIn:: STOPPED.");

    k_timer_stop(&timer);
    (void)ll_microphone_enable_reads(microphone, false);
}

K_THREAD_DEFINE(gThread, 1024, audio_thread, NULL, NULL, NULL, AUDIO_PRIORITY, 0, 1000);

/* -------------------------------------------------------------------------- */

static void audio_process_data(const uint64_t packetNumber, const uint32_t* rawData) {
    Fft fft;

    if (fft_initialize(&fft, AUDIO_FFT_SIZE)) {
        static float fftBuffer[AUDIO_FFT_SIZE * 2];  // Complex data set
        static float magnitude[AUDIO_FFT_SIZE];

        audio_populate_complex_vector(rawData, fftBuffer, AUDIO_FFT_SIZE);
        fft_calculate_frequency(&fft, fftBuffer);
        fft_calculate_magnitude(&fft, fftBuffer, magnitude);

        audio_report_data(packetNumber, magnitude, AUDIO_FFT_SIZE);
    }
}

/* -------------------------------------------------------------------------- */

static void audio_populate_complex_vector(const uint32_t* rawData, float* fftBuffer, const size_t length) {
    const struct device* microphone = DEVICE_DT_GET_ANY(ll_microphone);
    const int channel_count = ll_microphone_channel_count(microphone);

    if (microphone) {
        for (size_t i = 0; i < length; i++) {
            *fftBuffer++ = (float)*rawData;
            *fftBuffer++ = 0.0f;  // Complex portion is 0

            rawData += channel_count;
        }
    }
}

/* -------------------------------------------------------------------------- */

static void audio_report_data(uint64_t packetNumber, const float* magnitude, size_t length) {
    const int ELEMENTS_PER_MESSAGE = JERRYCAN_MAX_PAYLOAD_SIZE / sizeof(float);

    ++packetNumber;

    {
        jerrycan_msg_t msg;

        msg.type = JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_BEGIN;
        msg.audio_data_cmd.stream_id = packetNumber;

        jerrycan_tx(&msg, K_NO_WAIT);  // dont' wait; OK if a data sets drops
    }

    // Only the 1st half of the FFT results are needed; 2nd half mirrors the first.
    length = length / 2;

    for (int k = 0; k < length; k += ELEMENTS_PER_MESSAGE) {
        audio_transmit_data(magnitude, MIN(length - ELEMENTS_PER_MESSAGE, ELEMENTS_PER_MESSAGE) * sizeof(float));
        magnitude += ELEMENTS_PER_MESSAGE;
    }

    {
        jerrycan_msg_t msg;

        msg.type = JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_END;
        msg.audio_data_cmd.stream_id = packetNumber;

        jerrycan_tx(&msg, K_NO_WAIT);  // dont' wait; OK if a data sets drops
    }
}

/* -------------------------------------------------------------------------- */

static void audio_transmit_data(const void* payload, const size_t length) {
    jerrycan_msg_t msg;

    msg.type = JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_CONT;
    memcpy(msg.audio_data.payload, payload, length);

    jerrycan_tx(&msg, K_NO_WAIT);  // dont' wait; OK if a data sets drops
}
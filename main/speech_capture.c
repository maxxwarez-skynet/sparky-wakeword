#include "speech_capture.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "SPARKY_CAPTURE";

typedef enum {
    SPARKY_SPEECH_CAPTURE_WAIT_FOR_WAKE,
    SPARKY_SPEECH_CAPTURE_WAIT_FOR_SPEECH,
    SPARKY_SPEECH_CAPTURE_RECORDING,
    SPARKY_SPEECH_CAPTURE_COMPLETE,
} sparky_speech_capture_state_t;

/* The audio loop reads 1024 stereo samples per channel at a time. */
#define SPARKY_SPEECH_CAPTURE_PREROLL_SAMPLES 2048
#define SPARKY_SPEECH_CAPTURE_MAX_SAMPLES \
    ((SPARKY_SPEECH_CAPTURE_SAMPLE_RATE * \
      SPARKY_SPEECH_CAPTURE_CHANNELS * \
      SPARKY_SPEECH_CAPTURE_MAX_DURATION_MS) / 1000)
#define SPARKY_SPEECH_CAPTURE_TRAILING_SILENCE_SAMPLES \
    ((SPARKY_SPEECH_CAPTURE_SAMPLE_RATE * \
      SPARKY_SPEECH_CAPTURE_TRAILING_SILENCE_MS) / 1000)

static sparky_speech_capture_state_t s_state =
    SPARKY_SPEECH_CAPTURE_WAIT_FOR_WAKE;
static int16_t *s_buffer = NULL;
static size_t s_sample_count = 0;
static size_t s_trailing_silence_samples = 0;
static int16_t s_preroll[SPARKY_SPEECH_CAPTURE_PREROLL_SAMPLES];
static size_t s_preroll_sample_count = 0;

static void sparky_speech_capture_complete(const char *reason)
{
    s_state = SPARKY_SPEECH_CAPTURE_COMPLETE;

    ESP_LOGI(
        TAG,
        "Capture complete: %s, %u samples, %u ms, %u bytes",
        reason,
        (unsigned)s_sample_count,
        (unsigned)(s_sample_count * 1000 /
                   (SPARKY_SPEECH_CAPTURE_SAMPLE_RATE *
                    SPARKY_SPEECH_CAPTURE_CHANNELS)),
        (unsigned)(s_sample_count * sizeof(int16_t))
    );
}

static esp_err_t sparky_speech_capture_append(
    const int16_t *samples,
    size_t sample_count
)
{
    size_t remaining_samples =
        SPARKY_SPEECH_CAPTURE_MAX_SAMPLES - s_sample_count;

    if (sample_count > remaining_samples) {
        if (remaining_samples > 0) {
            memcpy(
                &s_buffer[s_sample_count],
                samples,
                remaining_samples * sizeof(int16_t)
            );
            s_sample_count += remaining_samples;
        }

        sparky_speech_capture_complete("maximum duration");
        return ESP_OK;
    }

    memcpy(&s_buffer[s_sample_count], samples, sample_count * sizeof(int16_t));
    s_sample_count += sample_count;

    if (s_sample_count == SPARKY_SPEECH_CAPTURE_MAX_SAMPLES) {
        sparky_speech_capture_complete("maximum duration");
    }

    return ESP_OK;
}

esp_err_t sparky_speech_capture_start(void)
{
    if (s_buffer == NULL) {
        s_buffer = heap_caps_malloc(
            SPARKY_SPEECH_CAPTURE_MAX_SAMPLES * sizeof(int16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );

        if (s_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate %u-byte PSRAM capture buffer",
                     (unsigned)(SPARKY_SPEECH_CAPTURE_MAX_SAMPLES * sizeof(int16_t)));
            return ESP_ERR_NO_MEM;
        }
    }

    s_state = SPARKY_SPEECH_CAPTURE_WAIT_FOR_SPEECH;
    s_sample_count = 0;
    s_trailing_silence_samples = 0;
    s_preroll_sample_count = 0;

    ESP_LOGI(
        TAG,
        "Capture started: waiting for speech, max %u ms, %u-byte PSRAM buffer",
        SPARKY_SPEECH_CAPTURE_MAX_DURATION_MS,
        (unsigned)(SPARKY_SPEECH_CAPTURE_MAX_SAMPLES * sizeof(int16_t))
    );

    return ESP_OK;
}

esp_err_t sparky_speech_capture_add_samples(
    const int16_t *samples,
    size_t sample_count
)
{
    if (s_state != SPARKY_SPEECH_CAPTURE_WAIT_FOR_SPEECH &&
        s_state != SPARKY_SPEECH_CAPTURE_RECORDING) {
        return ESP_OK;
    }

    if (samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_state == SPARKY_SPEECH_CAPTURE_WAIT_FOR_SPEECH) {
        s_preroll_sample_count = sample_count;
        if (s_preroll_sample_count > SPARKY_SPEECH_CAPTURE_PREROLL_SAMPLES) {
            s_preroll_sample_count = SPARKY_SPEECH_CAPTURE_PREROLL_SAMPLES;
        }
        memcpy(s_preroll, samples,
               s_preroll_sample_count * sizeof(int16_t));
        return ESP_OK;
    }

    return sparky_speech_capture_append(samples, sample_count);
}

esp_err_t sparky_speech_capture_handle_vad(int vad_state)
{
    if (s_state != SPARKY_SPEECH_CAPTURE_WAIT_FOR_SPEECH &&
        s_state != SPARKY_SPEECH_CAPTURE_RECORDING) {
        return ESP_OK;
    }

    if (s_state == SPARKY_SPEECH_CAPTURE_WAIT_FOR_SPEECH) {
        if (vad_state == 1) {
            s_state = SPARKY_SPEECH_CAPTURE_RECORDING;
            s_trailing_silence_samples = 0;

            ESP_LOGI(TAG, "Speech detected: recording");
            if (s_preroll_sample_count > 0) {
                return sparky_speech_capture_append(
                    s_preroll,
                    s_preroll_sample_count
                );
            }
        }
        return ESP_OK;
    }

    if (vad_state == 1) {
        s_trailing_silence_samples = 0;
        return ESP_OK;
    }

    if (s_trailing_silence_samples == 0) {
        ESP_LOGI(TAG, "Speech ended: trailing silence");
    }

    s_trailing_silence_samples += 512;
    if (s_trailing_silence_samples >=
        SPARKY_SPEECH_CAPTURE_TRAILING_SILENCE_SAMPLES) {
        sparky_speech_capture_complete("trailing silence");
    }

    return ESP_OK;
}

bool sparky_speech_capture_is_recording(void)
{
    return s_state == SPARKY_SPEECH_CAPTURE_WAIT_FOR_SPEECH ||
           s_state == SPARKY_SPEECH_CAPTURE_RECORDING;
}

bool sparky_speech_capture_is_complete(void)
{
    return s_state == SPARKY_SPEECH_CAPTURE_COMPLETE;
}

bool sparky_speech_capture_can_start(void)
{
    return s_state == SPARKY_SPEECH_CAPTURE_WAIT_FOR_WAKE ||
           s_state == SPARKY_SPEECH_CAPTURE_COMPLETE;
}

esp_err_t sparky_speech_capture_get_data(
    const int16_t **samples,
    size_t *sample_count,
    int *sample_rate,
    int *channels
)
{
    if (samples == NULL || sample_count == NULL ||
        sample_rate == NULL || channels == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_state != SPARKY_SPEECH_CAPTURE_COMPLETE || s_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    *samples = s_buffer;
    *sample_count = s_sample_count;
    *sample_rate = SPARKY_SPEECH_CAPTURE_SAMPLE_RATE;
    *channels = SPARKY_SPEECH_CAPTURE_CHANNELS;
    return ESP_OK;
}

#include "speech_capture.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "SPARKY_CAPTURE";
typedef enum { SPARKY_SPEECH_CAPTURE_WAIT_FOR_WAKE, SPARKY_SPEECH_CAPTURE_WAIT_FOR_SPEECH,
               SPARKY_SPEECH_CAPTURE_RECORDING, SPARKY_SPEECH_CAPTURE_COMPLETE } sparky_speech_capture_state_t;

#define SPARKY_SPEECH_CAPTURE_MAX_SAMPLES ((SPARKY_SPEECH_CAPTURE_SAMPLE_RATE * SPARKY_SPEECH_CAPTURE_MAX_DURATION_MS) / 1000)
#define SPARKY_SPEECH_CAPTURE_TRAILING_SILENCE_SAMPLES ((SPARKY_SPEECH_CAPTURE_SAMPLE_RATE * SPARKY_SPEECH_CAPTURE_TRAILING_SILENCE_MS) / 1000)

static sparky_speech_capture_state_t s_state = SPARKY_SPEECH_CAPTURE_WAIT_FOR_WAKE;
static int16_t *s_buffer;
static size_t s_sample_count;
static size_t s_trailing_silence_samples;

static void complete(const char *reason)
{
    s_state = SPARKY_SPEECH_CAPTURE_COMPLETE;
    ESP_LOGI(TAG, "Capture complete: %s, %u samples, %u bytes, %u ms", reason,
             (unsigned)s_sample_count, (unsigned)(s_sample_count * sizeof(*s_buffer)),
             (unsigned)(s_sample_count * 1000 / SPARKY_SPEECH_CAPTURE_SAMPLE_RATE));
}

static esp_err_t append(const int16_t *samples, size_t byte_count)
{
    if (samples == NULL || byte_count == 0 || byte_count % sizeof(*samples) != 0) return ESP_ERR_INVALID_ARG;
    size_t samples_to_append = byte_count / sizeof(*samples);
    size_t remaining = SPARKY_SPEECH_CAPTURE_MAX_SAMPLES - s_sample_count;
    size_t to_copy = samples_to_append < remaining ? samples_to_append : remaining;
    if (to_copy) {
        memcpy(&s_buffer[s_sample_count], samples, to_copy * sizeof(*samples));
        s_sample_count += to_copy;
    }
    if (to_copy != samples_to_append || s_sample_count == SPARKY_SPEECH_CAPTURE_MAX_SAMPLES) complete("maximum duration");
    return ESP_OK;
}

esp_err_t sparky_speech_capture_start(void)
{
    if (s_state != SPARKY_SPEECH_CAPTURE_WAIT_FOR_WAKE) return ESP_ERR_INVALID_STATE;
    if (s_buffer == NULL) {
        s_buffer = heap_caps_malloc(SPARKY_SPEECH_CAPTURE_MAX_SAMPLES * sizeof(*s_buffer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_buffer == NULL) return ESP_ERR_NO_MEM;
    }
    s_sample_count = 0; s_trailing_silence_samples = 0;
    s_state = SPARKY_SPEECH_CAPTURE_WAIT_FOR_SPEECH;
    ESP_LOGI(TAG, "Capture started: waiting for speech, max %u ms, %u-byte PSRAM buffer",
             SPARKY_SPEECH_CAPTURE_MAX_DURATION_MS, (unsigned)(SPARKY_SPEECH_CAPTURE_MAX_SAMPLES * sizeof(*s_buffer)));
    return ESP_OK;
}

esp_err_t sparky_speech_capture_process_fetch_result(const afe_fetch_result_t *result)
{
    if (!sparky_speech_capture_is_active()) return ESP_OK;
    if (result == NULL || result->data == NULL || result->data_size <= 0 || result->data_size % sizeof(int16_t) != 0) return ESP_ERR_INVALID_ARG;
    if (s_state == SPARKY_SPEECH_CAPTURE_WAIT_FOR_SPEECH) {
        if (result->vad_state != VAD_SPEECH) return ESP_OK;
        s_state = SPARKY_SPEECH_CAPTURE_RECORDING;
        s_trailing_silence_samples = 0;
        ESP_LOGI(TAG, "Speech detected: recording");
        /* VAD cache contains the delayed leading PCM and precedes data. */
        if (result->vad_cache != NULL && result->vad_cache_size > 0) {
            esp_err_t ret = append(result->vad_cache, (size_t)result->vad_cache_size);
            if (ret != ESP_OK || s_state == SPARKY_SPEECH_CAPTURE_COMPLETE) return ret;
        }
    }
    esp_err_t ret = append(result->data, (size_t)result->data_size);
    if (ret != ESP_OK || s_state == SPARKY_SPEECH_CAPTURE_COMPLETE) return ret;
    if (result->vad_state == VAD_SPEECH) {
        s_trailing_silence_samples = 0;
    } else {
        if (s_trailing_silence_samples == 0) ESP_LOGI(TAG, "Speech ended: trailing silence");
        s_trailing_silence_samples += (size_t)result->data_size / sizeof(int16_t);
        if (s_trailing_silence_samples >= SPARKY_SPEECH_CAPTURE_TRAILING_SILENCE_SAMPLES) complete("trailing silence");
    }
    return ESP_OK;
}

bool sparky_speech_capture_is_active(void) { return s_state == SPARKY_SPEECH_CAPTURE_WAIT_FOR_SPEECH || s_state == SPARKY_SPEECH_CAPTURE_RECORDING; }
bool sparky_speech_capture_is_complete(void) { return s_state == SPARKY_SPEECH_CAPTURE_COMPLETE; }
bool sparky_speech_capture_can_start(void) { return s_state == SPARKY_SPEECH_CAPTURE_WAIT_FOR_WAKE; }
esp_err_t sparky_speech_capture_get_data(const int16_t **samples, size_t *sample_count, size_t *byte_count)
{
    if (samples == NULL || sample_count == NULL || byte_count == NULL) return ESP_ERR_INVALID_ARG;
    if (!sparky_speech_capture_is_complete() || s_buffer == NULL) return ESP_ERR_INVALID_STATE;
    *samples = s_buffer; *sample_count = s_sample_count; *byte_count = s_sample_count * sizeof(*s_buffer);
    return ESP_OK;
}
void sparky_speech_capture_reset(void) { s_sample_count = 0; s_trailing_silence_samples = 0; s_state = SPARKY_SPEECH_CAPTURE_WAIT_FOR_WAKE; }

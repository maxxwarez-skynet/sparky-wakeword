#include "tts_player.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "micro_mp3/mp3_decoder.h"

extern "C" {
#include "audio.h"
}

static const char *TAG = "SPARKY_TTS";

namespace {

constexpr uint32_t OUTPUT_SAMPLE_RATE = 16000;
constexpr size_t MP3_PCM_BUFFER_BYTES = 4608;
constexpr size_t MP3_INPUT_CHUNK_BYTES = 1536;
constexpr size_t OUTPUT_FRAMES = MP3_PCM_BUFFER_BYTES / (2 * sizeof(int16_t));
constexpr size_t PLAYBACK_QUEUE_LENGTH = 8;

struct playback_chunk_t {
    size_t frame_count;
    int16_t pcm[OUTPUT_FRAMES * 2];
};

QueueHandle_t s_playback_ready_queue = nullptr;
QueueHandle_t s_playback_free_queue = nullptr;
TaskHandle_t s_playback_task = nullptr;
TaskHandle_t s_producer_task = nullptr;
playback_chunk_t *s_playback_pool[PLAYBACK_QUEUE_LENGTH] = {};
bool s_playback_task_running = false;

micro_mp3::Mp3Decoder s_decoder;

alignas(4) int16_t s_mp3_pcm[MP3_PCM_BUFFER_BYTES / sizeof(int16_t)];
alignas(4) int16_t s_output_pcm[OUTPUT_FRAMES * 2];

uint8_t s_mp3_input[MP3_INPUT_CHUNK_BYTES];
size_t s_mp3_input_length = 0;

char s_b64_quartet[4];
size_t s_b64_quartet_length = 0;

bool s_active = false;
bool s_failed = false;
bool s_stream_info_logged = false;

uint32_t s_input_sample_rate = 0;
uint8_t s_input_channels = 0;

/*
 * Fractional sample-rate conversion using a phase accumulator.
 *
 * The board's I2S bus is fixed at 16 kHz. micro-mp3 may return another
 * sample rate (Google TTS commonly uses rates such as 24 kHz). This converter
 * performs streaming nearest-neighbour rate conversion and preserves timing
 * across decoder-frame boundaries. For speech this is intentionally small
 * and dependency-free.
 */
uint32_t s_resample_accumulator = 0;
size_t s_output_frame_count = 0;

int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

void playback_task(void *arg)
{
    (void)arg;

    for (;;) {
        playback_chunk_t *chunk = nullptr;

        if (xQueueReceive(
                s_playback_ready_queue,
                &chunk,
                portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (chunk == nullptr) {
            xTaskNotifyGive(s_producer_task);
            continue;
        }

        esp_err_t ret = sparky_audio_play_pcm(
            chunk->pcm,
            chunk->frame_count);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Audio output failed: %s",
                     esp_err_to_name(ret));
            s_failed = true;
        }

        // Return the buffer to the producer pool. No large object is
        // allocated on the playback task's stack.
        if (xQueueSend(
                s_playback_free_queue,
                &chunk,
                portMAX_DELAY) != pdTRUE) {
            s_failed = true;
        }
    }
}

esp_err_t ensure_playback_task()
{
    if (s_playback_task_running) {
        return ESP_OK;
    }

    s_playback_ready_queue = xQueueCreate(
        PLAYBACK_QUEUE_LENGTH,
        sizeof(playback_chunk_t *));

    s_playback_free_queue = xQueueCreate(
        PLAYBACK_QUEUE_LENGTH,
        sizeof(playback_chunk_t *));

    if (s_playback_ready_queue == nullptr ||
        s_playback_free_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create TTS playback queues");
        if (s_playback_ready_queue != nullptr) {
            vQueueDelete(s_playback_ready_queue);
            s_playback_ready_queue = nullptr;
        }
        if (s_playback_free_queue != nullptr) {
            vQueueDelete(s_playback_free_queue);
            s_playback_free_queue = nullptr;
        }
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < PLAYBACK_QUEUE_LENGTH; ++i) {
        s_playback_pool[i] = static_cast<playback_chunk_t *>(
            heap_caps_malloc(
                sizeof(playback_chunk_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

        if (s_playback_pool[i] == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate TTS playback buffer %u",
                     (unsigned)i);

            for (size_t j = 0; j < i; ++j) {
                heap_caps_free(s_playback_pool[j]);
                s_playback_pool[j] = nullptr;
            }

            vQueueDelete(s_playback_ready_queue);
            vQueueDelete(s_playback_free_queue);
            s_playback_ready_queue = nullptr;
            s_playback_free_queue = nullptr;
            return ESP_ERR_NO_MEM;
        }

        playback_chunk_t *chunk = s_playback_pool[i];

        if (xQueueSend(
                s_playback_free_queue,
                &chunk,
                0) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to initialize TTS buffer pool");
            return ESP_FAIL;
        }
    }

    s_producer_task = xTaskGetCurrentTaskHandle();

    BaseType_t result = xTaskCreate(
        playback_task,
        "sparky_tts",
        6144,
        nullptr,
        6,
        &s_playback_task);

    if (result != pdPASS) {
        for (size_t i = 0; i < PLAYBACK_QUEUE_LENGTH; ++i) {
            heap_caps_free(s_playback_pool[i]);
            s_playback_pool[i] = nullptr;
        }
        vQueueDelete(s_playback_ready_queue);
        vQueueDelete(s_playback_free_queue);
        s_playback_ready_queue = nullptr;
        s_playback_free_queue = nullptr;
        s_playback_task = nullptr;
        s_producer_task = nullptr;
        return ESP_ERR_NO_MEM;
    }

    s_playback_task_running = true;
    return ESP_OK;
}

void flush_output()
{
    if (s_output_frame_count == 0 || s_failed) {
        return;
    }

    if (s_playback_ready_queue == nullptr ||
        s_playback_free_queue == nullptr) {
        ESP_LOGE(TAG, "TTS playback queues are not initialized");
        s_failed = true;
        s_output_frame_count = 0;
        return;
    }

    playback_chunk_t *chunk = nullptr;

    // Wait for a free PSRAM buffer. The producer task never allocates a
    // multi-kilobyte playback object on its own stack.
    if (xQueueReceive(
            s_playback_free_queue,
            &chunk,
            pdMS_TO_TICKS(5000)) != pdTRUE ||
        chunk == nullptr) {
        ESP_LOGE(TAG, "No free TTS playback buffer");
        s_failed = true;
        s_output_frame_count = 0;
        return;
    }

    chunk->frame_count = s_output_frame_count;

    memcpy(
        chunk->pcm,
        s_output_pcm,
        s_output_frame_count * 2 * sizeof(int16_t));

    if (xQueueSend(
            s_playback_ready_queue,
            &chunk,
            pdMS_TO_TICKS(5000)) != pdTRUE) {

        ESP_LOGE(TAG, "TTS playback queue is full");
        s_failed = true;

        // Do not leak the buffer if enqueue fails.
        xQueueSend(s_playback_free_queue, &chunk, 0);
    }

    s_output_frame_count = 0;
}

void emit_frame(int16_t left, int16_t right)
{
    if (s_output_frame_count == OUTPUT_FRAMES) {
        flush_output();
        if (s_failed) {
            return;
        }
    }

    s_output_pcm[s_output_frame_count * 2] = left;
    s_output_pcm[s_output_frame_count * 2 + 1] = right;
    ++s_output_frame_count;
}

void process_decoded_pcm(size_t samples_decoded)
{
    if (s_input_sample_rate == 0 ||
        (s_input_channels != 1 && s_input_channels != 2)) {
        return;
    }

    const int16_t *pcm = s_mp3_pcm;

    for (size_t frame = 0; frame < samples_decoded; ++frame) {
        int16_t left = pcm[frame * s_input_channels];
        int16_t right = (s_input_channels == 2)
                      ? pcm[frame * 2 + 1]
                      : left;

        /*
         * Emit zero, one, or multiple output frames depending on the
         * input/output rate ratio.
         */
        s_resample_accumulator += OUTPUT_SAMPLE_RATE;

        while (s_resample_accumulator >= s_input_sample_rate) {
            emit_frame(left, right);
            if (s_failed) {
                return;
            }
            s_resample_accumulator -= s_input_sample_rate;
        }
    }
}

size_t decode_mp3_bytes(const uint8_t *data, size_t length)
{
    size_t offset = 0;

    while (offset < length && !s_failed) {
        size_t consumed = 0;
        size_t samples_decoded = 0;

        micro_mp3::Mp3Result result = s_decoder.decode(
            data + offset,
            length - offset,
            reinterpret_cast<uint8_t *>(s_mp3_pcm),
            sizeof(s_mp3_pcm),
            consumed,
            samples_decoded
        );

        if (consumed > length - offset) {
            ESP_LOGE(TAG, "MP3 decoder reported invalid consumption");
            s_failed = true;
            return length;
        }

        offset += consumed;

        if (result == micro_mp3::MP3_STREAM_INFO_READY) {
            s_input_sample_rate = s_decoder.get_sample_rate();
            s_input_channels = s_decoder.get_channels();

            if (s_input_sample_rate == 0 ||
                (s_input_channels != 1 && s_input_channels != 2)) {
                ESP_LOGE(TAG, "Unsupported MP3 format: %u Hz, %u channels",
                         (unsigned)s_input_sample_rate,
                         (unsigned)s_input_channels);
                s_failed = true;
                return length - offset;
            }

            s_resample_accumulator = 0;

            ESP_LOGI(TAG, "MP3 stream: %u Hz / %u channel%s -> 16000 Hz stereo",
                     (unsigned)s_input_sample_rate,
                     (unsigned)s_input_channels,
                     s_input_channels == 1 ? "" : "s");
            s_stream_info_logged = true;
        } else if (result == micro_mp3::MP3_OK) {
            if (samples_decoded > 0) {
                if (!s_stream_info_logged) {
                    s_input_sample_rate = s_decoder.get_sample_rate();
                    s_input_channels = s_decoder.get_channels();
                    s_stream_info_logged = true;
                }
                process_decoded_pcm(samples_decoded);
            }
        } else if (result == micro_mp3::MP3_NEED_MORE_DATA) {
            /*
             * The decoder has retained the partial frame internally.
             * If it consumed nothing, more input is required before
             * another decode attempt can make progress.
             */
            if (consumed == 0) {
                break;
            }
        } else if (result == micro_mp3::MP3_DECODE_ERROR) {
            ESP_LOGW(TAG, "Recoverable MP3 decode error");
            if (consumed == 0) {
                break;
            }
        } else if (result < 0) {
            ESP_LOGE(TAG, "Fatal MP3 decode error: %d", (int)result);
            s_failed = true;
            return length - offset;
        } else if (samples_decoded > 0) {
            process_decoded_pcm(samples_decoded);
        }

        if (consumed == 0 && samples_decoded == 0) {
            break;
        }
    }
        return length - offset;
}

void feed_decoded_byte(uint8_t byte)
{
    s_mp3_input[s_mp3_input_length++] = byte;

    if (s_mp3_input_length == sizeof(s_mp3_input)) {
        size_t remaining = decode_mp3_bytes(s_mp3_input, s_mp3_input_length);
        if (remaining > 0 && remaining < s_mp3_input_length) {
            memmove(s_mp3_input,
                    s_mp3_input + (s_mp3_input_length - remaining),
                    remaining);
        }
        s_mp3_input_length = remaining;
    }
}

esp_err_t decode_base64_quartet()
{
    int a = base64_value(s_b64_quartet[0]);
    int b = base64_value(s_b64_quartet[1]);

    if (a < 0 || b < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int c = s_b64_quartet[2] == '=' ? 0 : base64_value(s_b64_quartet[2]);
    int d = s_b64_quartet[3] == '=' ? 0 : base64_value(s_b64_quartet[3]);

    if (c < 0 || d < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    feed_decoded_byte((uint8_t)((a << 2) | (b >> 4)));

    if (s_b64_quartet[2] != '=') {
        feed_decoded_byte((uint8_t)((b << 4) | (c >> 2)));
    }

    if (s_b64_quartet[3] != '=') {
        feed_decoded_byte((uint8_t)((c << 6) | d));
    }

    s_b64_quartet_length = 0;
    return s_failed ? ESP_FAIL : ESP_OK;
}

} // namespace

extern "C" esp_err_t sparky_tts_player_begin(void)
{
    if (s_active) {
        return ESP_ERR_INVALID_STATE;
    }

    s_decoder.reset();

    s_mp3_input_length = 0;
    s_b64_quartet_length = 0;
    s_output_frame_count = 0;

    s_active = true;
    s_failed = false;
    s_stream_info_logged = false;
    s_input_sample_rate = 0;
    s_input_channels = 0;
    s_resample_accumulator = 0;

    esp_err_t ret = ensure_playback_task();
    if (ret != ESP_OK) {
        s_active = false;
        return ret;
    }

    s_producer_task = xTaskGetCurrentTaskHandle();

    ESP_LOGI(TAG, "TTS playback started");
    return ESP_OK;
}

extern "C" esp_err_t sparky_tts_player_feed_base64(
    const char *data,
    size_t length
)
{
    if (!s_active) {
        return ESP_ERR_INVALID_STATE;
    }

    if (data == nullptr || length == 0) {
        return ESP_OK;
    }

    for (size_t i = 0; i < length; ++i) {
        const char c = data[i];

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }

        /*
         * Base64 audio is a JSON string, so the only valid characters here
         * are Base64 characters and optional '=' padding.
         */
        if (base64_value(c) < 0 && c != '=') {
            ESP_LOGE(TAG, "Invalid Base64 character 0x%02X", (unsigned char)c);
            s_failed = true;
            return ESP_ERR_INVALID_ARG;
        }

        if (s_b64_quartet_length >= sizeof(s_b64_quartet)) {
            ESP_LOGE(TAG, "Base64 quartet overflow");
            s_failed = true;
            return ESP_ERR_INVALID_SIZE;
        }

        s_b64_quartet[s_b64_quartet_length++] = c;

        if (s_b64_quartet_length == 4) {
            esp_err_t ret = decode_base64_quartet();
            if (ret != ESP_OK) {
                return ret;
            }
        }
    }

    return s_failed ? ESP_FAIL : ESP_OK;
}

extern "C" esp_err_t sparky_tts_player_end(void)
{
    if (!s_active) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;

    if (s_b64_quartet_length != 0) {
        ESP_LOGE(TAG, "Incomplete Base64 audio at end of response");
        s_failed = true;
    }

    if (s_mp3_input_length > 0 && !s_failed) {
        size_t remaining = decode_mp3_bytes(s_mp3_input, s_mp3_input_length);
        if (remaining > 0 && remaining < s_mp3_input_length) {
            memmove(s_mp3_input,
                    s_mp3_input + (s_mp3_input_length - remaining),
                    remaining);
        }
        s_mp3_input_length = remaining;
    }

    flush_output();

    if (!s_failed && s_playback_ready_queue != nullptr) {
        // Discard any stale notification before waiting for this playback
        // stream to drain.
        ulTaskNotifyTake(pdTRUE, 0);

        playback_chunk_t *end_marker = nullptr;

        if (xQueueSend(
                s_playback_ready_queue,
                &end_marker,
                pdMS_TO_TICKS(5000)) != pdTRUE) {

            ESP_LOGE(TAG, "Failed to queue TTS end marker");
            s_failed = true;

        } else if (ulTaskNotifyTake(
                       pdTRUE,
                       pdMS_TO_TICKS(10000)) == 0) {

            ESP_LOGE(TAG, "Timed out waiting for TTS playback");
            s_failed = true;
        }
    }

    if (s_failed) {
        ret = ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "TTS playback complete");
    }

    s_active = false;
    return ret;
}

extern "C" bool sparky_tts_player_is_active(void)
{
    return s_active;
}

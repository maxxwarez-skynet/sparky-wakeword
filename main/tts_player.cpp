#include "tts_player.h"

#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "micro_mp3/mp3_decoder.h"

extern "C" {
#include "audio.h"
}

static const char *TAG = "SPARKY_TTS";

namespace {

constexpr uint32_t OUTPUT_SAMPLE_RATE = 16000;
constexpr size_t MP3_STREAM_BYTES = 8192;
constexpr size_t MP3_ACCUM_BYTES = 2048;
constexpr size_t MP3_PCM_BUFFER_BYTES = 4608;
constexpr size_t OUTPUT_FRAMES = 256;
constexpr UBaseType_t TTS_TASK_STACK = 24576;
constexpr UBaseType_t TTS_TASK_PRIORITY = 6;

micro_mp3::Mp3Decoder s_decoder;

StreamBufferHandle_t s_mp3_stream = nullptr;
SemaphoreHandle_t s_start_sem = nullptr;
SemaphoreHandle_t s_done_sem = nullptr;
TaskHandle_t s_tts_task = nullptr;

char s_b64_quartet[4];
size_t s_b64_quartet_length = 0;

volatile bool s_session_active = false;
volatile bool s_input_closed = false;
volatile bool s_failed = false;

int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool push_mp3_bytes(const uint8_t *data, size_t length)
{
    while (length > 0 && !s_failed) {
        size_t sent = xStreamBufferSend(
            s_mp3_stream, data, length, pdMS_TO_TICKS(30000));
        if (sent == 0) {
            ESP_LOGE(TAG, "MP3 stream send timed out");
            s_failed = true;
            return false;
        }
        data += sent;
        length -= sent;
    }
    return !s_failed;
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

    uint8_t out[3];
    size_t n = 1;
    out[0] = (uint8_t)((a << 2) | (b >> 4));
    if (s_b64_quartet[2] != '=') {
        out[n++] = (uint8_t)((b << 4) | (c >> 2));
    }
    if (s_b64_quartet[3] != '=') {
        out[n++] = (uint8_t)((c << 6) | d);
    }

    s_b64_quartet_length = 0;
    return push_mp3_bytes(out, n) ? ESP_OK : ESP_FAIL;
}

void play_stereo_frames(const int16_t *pcm, size_t frame_count)
{
    if (frame_count == 0 || s_failed) {
        return;
    }

    sparky_audio_pa_enable(true);

    esp_err_t ret = sparky_audio_play_pcm(pcm, frame_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio output failed: %s", esp_err_to_name(ret));
        s_failed = true;
    }
}

void tts_task(void *arg)
{
    (void)arg;

    auto *mp3_pcm = static_cast<int16_t *>(heap_caps_malloc(
        MP3_PCM_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *out_pcm = static_cast<int16_t *>(heap_caps_malloc(
        OUTPUT_FRAMES * 2 * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *acc = static_cast<uint8_t *>(heap_caps_malloc(
        MP3_ACCUM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (mp3_pcm == nullptr || out_pcm == nullptr || acc == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate TTS decoder buffers");
        for (;;) {
            xSemaphoreTake(s_start_sem, portMAX_DELAY);
            s_failed = true;
            sparky_audio_pa_enable(false);
            xSemaphoreGive(s_done_sem);
        }
    }

    for (;;) {
        xSemaphoreTake(s_start_sem, portMAX_DELAY);

        s_decoder.reset();

        size_t acc_len = 0;
        size_t out_frames = 0;
        uint32_t input_rate = 0;
        uint8_t input_channels = 0;
        uint32_t resample_acc = 0;
        bool logged_info = false;
        bool pa_on = false;

        auto flush_out = [&]() {
            if (out_frames == 0) {
                return;
            }
            if (!pa_on) {
                sparky_audio_pa_enable(true);
                pa_on = true;
            }
            play_stereo_frames(out_pcm, out_frames);
            out_frames = 0;
        };

        auto emit_frame = [&](int16_t left, int16_t right) {
            if (out_frames == OUTPUT_FRAMES) {
                flush_out();
            }
            if (s_failed) {
                return;
            }
            out_pcm[out_frames * 2] = left;
            out_pcm[out_frames * 2 + 1] = right;
            ++out_frames;
        };

        auto decode_available = [&]() {
            size_t offset = 0;
            int spins = 0;
            while (offset < acc_len && !s_failed && spins++ < 64) {
                size_t consumed = 0;
                size_t samples_decoded = 0;
                micro_mp3::Mp3Result result = s_decoder.decode(
                    acc + offset,
                    acc_len - offset,
                    reinterpret_cast<uint8_t *>(mp3_pcm),
                    MP3_PCM_BUFFER_BYTES,
                    consumed,
                    samples_decoded);

                if (consumed > acc_len - offset) {
                    ESP_LOGE(TAG, "MP3 decoder reported invalid consumption");
                    s_failed = true;
                    break;
                }
                offset += consumed;

                if (result == micro_mp3::MP3_STREAM_INFO_READY) {
                    input_rate = s_decoder.get_sample_rate();
                    input_channels = s_decoder.get_channels();
                    resample_acc = 0;
                    if (input_rate == 0 ||
                        (input_channels != 1 && input_channels != 2)) {
                        ESP_LOGE(TAG, "Unsupported MP3 format: %u Hz, %u ch",
                                 (unsigned)input_rate,
                                 (unsigned)input_channels);
                        s_failed = true;
                        break;
                    }
                    if (!logged_info) {
                        ESP_LOGI(TAG,
                                 "MP3 stream: %u Hz / %u channel%s -> 16000 Hz stereo",
                                 (unsigned)input_rate,
                                 (unsigned)input_channels,
                                 input_channels == 1 ? "" : "s");
                        logged_info = true;
                    }
                    continue;
                }

                if (result == micro_mp3::MP3_DECODE_ERROR) {
                    ESP_LOGW(TAG, "Recoverable MP3 decode error");
                    if (consumed == 0) {
                        break;
                    }
                    continue;
                }

                if (result == micro_mp3::MP3_NEED_MORE_DATA) {
                    if (consumed == 0) {
                        break;
                    }
                    continue;
                }

                if (result < 0) {
                    ESP_LOGE(TAG, "Fatal MP3 decode error: %d", (int)result);
                    s_failed = true;
                    break;
                }

                if (samples_decoded == 0) {
                    if (consumed == 0) {
                        break;
                    }
                    continue;
                }

                if (!logged_info) {
                    input_rate = s_decoder.get_sample_rate();
                    input_channels = s_decoder.get_channels();
                    logged_info = true;
                }
                if (input_rate == 0 ||
                    (input_channels != 1 && input_channels != 2)) {
                    continue;
                }

                for (size_t frame = 0; frame < samples_decoded && !s_failed; ++frame) {
                    int16_t left = mp3_pcm[frame * input_channels];
                    int16_t right = (input_channels == 2)
                                  ? mp3_pcm[frame * 2 + 1]
                                  : left;
                    resample_acc += OUTPUT_SAMPLE_RATE;
                    while (resample_acc >= input_rate && !s_failed) {
                        emit_frame(left, right);
                        resample_acc -= input_rate;
                    }
                }
            }

            if (offset > acc_len) {
                offset = acc_len;
            }
            if (offset > 0 && offset < acc_len) {
                memmove(acc, acc + offset, acc_len - offset);
            }
            acc_len -= offset;
        };

        while (!s_failed) {
            size_t space = MP3_ACCUM_BYTES - acc_len;
            TickType_t wait = s_input_closed ? 0 : pdMS_TO_TICKS(20);
            size_t got = 0;
            if (space > 0) {
                got = xStreamBufferReceive(s_mp3_stream, acc + acc_len, space, wait);
                acc_len += got;
            }

            if (acc_len > 0) {
                decode_available();
            }

            if (s_input_closed &&
                got == 0 &&
                xStreamBufferBytesAvailable(s_mp3_stream) == 0) {
                if (acc_len > 0) {
                    decode_available();
                }
                break;
            }
        }

        flush_out();
        if (pa_on) {
            vTaskDelay(pdMS_TO_TICKS(20));
            sparky_audio_pa_enable(false);
        }

        if (!s_failed) {
            ESP_LOGI(TAG, "TTS playback complete");
        }

        s_session_active = false;
        xSemaphoreGive(s_done_sem);
    }
}

esp_err_t ensure_tts_task(void)
{
    if (s_tts_task != nullptr) {
        return ESP_OK;
    }

    s_mp3_stream = xStreamBufferCreate(MP3_STREAM_BYTES, 1);
    s_start_sem = xSemaphoreCreateBinary();
    s_done_sem = xSemaphoreCreateBinary();
    if (s_mp3_stream == nullptr || s_start_sem == nullptr || s_done_sem == nullptr) {
        ESP_LOGE(TAG, "Failed to create TTS IPC");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        tts_task,
        "sparky_tts",
        TTS_TASK_STACK,
        nullptr,
        TTS_TASK_PRIORITY,
        &s_tts_task,
        1);
    if (ok != pdPASS) {
        s_tts_task = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

} // namespace

extern "C" esp_err_t sparky_tts_player_begin(void)
{
    if (s_session_active) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ensure_tts_task();
    if (ret != ESP_OK) {
        return ret;
    }

    xStreamBufferReset(s_mp3_stream);
    xSemaphoreTake(s_done_sem, 0);

    s_b64_quartet_length = 0;
    s_failed = false;
    s_input_closed = false;
    s_session_active = true;

    xSemaphoreGive(s_start_sem);
    ESP_LOGI(TAG, "TTS playback started");
    return ESP_OK;
}

extern "C" esp_err_t sparky_tts_player_feed_base64(const char *data, size_t length)
{
    if (!s_session_active || s_input_closed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == nullptr || length == 0) {
        return ESP_OK;
    }

    for (size_t i = 0; i < length && !s_failed; ++i) {
        const char c = data[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }
        if (base64_value(c) < 0 && c != '=') {
            ESP_LOGE(TAG, "Invalid Base64 character 0x%02X", (unsigned char)c);
            s_failed = true;
            return ESP_ERR_INVALID_ARG;
        }
        s_b64_quartet[s_b64_quartet_length++] = c;
        if (s_b64_quartet_length == 4) {
            esp_err_t ret = decode_base64_quartet();
            if (ret != ESP_OK) {
                s_failed = true;
                return ret;
            }
        }
    }

    return s_failed ? ESP_FAIL : ESP_OK;
}

extern "C" esp_err_t sparky_tts_player_close_input(void)
{
    if (!s_session_active) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_b64_quartet_length != 0) {
        ESP_LOGE(TAG, "Incomplete Base64 audio at end of string");
        s_failed = true;
    }

    s_input_closed = true;
    return s_failed ? ESP_FAIL : ESP_OK;
}

extern "C" esp_err_t sparky_tts_player_wait(void)
{
    if (s_tts_task == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_session_active && !s_input_closed) {
        sparky_tts_player_close_input();
    }

    if (xSemaphoreTake(s_done_sem, pdMS_TO_TICKS(60000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timed out waiting for TTS playback");
        s_session_active = false;
        s_failed = true;
        return ESP_ERR_TIMEOUT;
    }

    return s_failed ? ESP_FAIL : ESP_OK;
}

extern "C" bool sparky_tts_player_is_active(void)
{
    return s_session_active;
}

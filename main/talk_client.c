#include "talk_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tts_player.h"

static const char *TAG = "SPARKY_TALK";

/* Device configuration, not credentials. Network setup is owned elsewhere. */
#define SPARKY_TALK_URL "https://ai-toy-backend-dev-392025844550.asia-south1.run.app/talk"
#define SPARKY_TALK_USER_ID "abhinav"
#define SPARKY_TALK_BOUNDARY "----SparkyTalkBoundary7MA4YWxkTrZu0gW"
#define SPARKY_TALK_MAX_WAV_BYTES 2000000U
#define SPARKY_TALK_RESPONSE_MAX_BYTES 16384U

typedef enum {
    RESPONSE_SCAN_NORMAL,
    RESPONSE_SCAN_STRING,
    RESPONSE_SCAN_AUDIO_COLON,
    RESPONSE_SCAN_AUDIO_VALUE,
    RESPONSE_SCAN_AUDIO_STRING,
} response_scan_state_t;

typedef struct {
    const int16_t *pcm;
    size_t pcm_bytes;
    char *response;
    size_t response_length;
    response_scan_state_t response_scan_state;
    char string_token[6];
    size_t string_token_length;
    bool string_escape;
    bool response_overflow;
    bool audio_error;
    bool tts_started;
    bool busy;
    bool complete;
} talk_state_t;

static talk_state_t s_talk;

static void put_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static void make_wav_header(uint8_t header[44], size_t pcm_bytes)
{
    memcpy(header, "RIFF", 4);
    put_le32(&header[4], (uint32_t)(36 + pcm_bytes));

    memcpy(&header[8], "WAVEfmt ", 8);
    put_le32(&header[16], 16);
    put_le16(&header[20], 1);
    put_le16(&header[22], 1);
    put_le32(&header[24], 16000);
    put_le32(&header[28], 32000);
    put_le16(&header[32], 2);
    put_le16(&header[34], 16);

    memcpy(&header[36], "data", 4);
    put_le32(&header[40], (uint32_t)pcm_bytes);
}

static void append_response_byte(char byte)
{
    if (s_talk.response_overflow) {
        return;
    }

    if (s_talk.response_length + 1 >= SPARKY_TALK_RESPONSE_MAX_BYTES) {
        s_talk.response_overflow = true;

        ESP_LOGW(TAG,
                 "Response metadata exceeds %u bytes; truncating",
                 SPARKY_TALK_RESPONSE_MAX_BYTES - 1);

        return;
    }

    s_talk.response[s_talk.response_length++] = byte;
    s_talk.response[s_talk.response_length] = '\0';
}

/*
 * The response includes a potentially large base64 "audio" string.
 *
 * Keep the JSON structure in the small metadata buffer, but stream the
 * contents of the audio string directly to the TTS player.
 *
 * This prevents the MP3/Base64 payload from consuming the metadata buffer.
 */
static void collect_response_metadata(const char *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        char byte = data[i];

        switch (s_talk.response_scan_state) {

        case RESPONSE_SCAN_NORMAL:
            append_response_byte(byte);

            if (byte == '"') {
                s_talk.response_scan_state = RESPONSE_SCAN_STRING;
                s_talk.string_token_length = 0;
                s_talk.string_escape = false;
            }

            break;

        case RESPONSE_SCAN_STRING:
            append_response_byte(byte);

            if (s_talk.string_escape) {
                s_talk.string_escape = false;

            } else if (byte == '\\') {
                s_talk.string_escape = true;

            } else if (byte == '"') {

                /*
                 * Match the JSON key "audio" exactly.
                 * "audioFormat" also starts with those five letters.
                 */
                s_talk.response_scan_state =
                    s_talk.string_token_length == 5 &&
                    memcmp(s_talk.string_token, "audio", 5) == 0
                        ? RESPONSE_SCAN_AUDIO_COLON
                        : RESPONSE_SCAN_NORMAL;

            } else if (s_talk.string_token_length < 5) {

                s_talk.string_token[
                    s_talk.string_token_length++
                ] = byte;

            } else {
                /* Key is longer than "audio"; it cannot be the audio field. */
                s_talk.string_token_length = 6;
            }

            break;

        case RESPONSE_SCAN_AUDIO_COLON:
            append_response_byte(byte);

            if (byte == ':') {
                s_talk.response_scan_state =
                    RESPONSE_SCAN_AUDIO_VALUE;

            } else if (byte != ' ' &&
                       byte != '\t' &&
                       byte != '\r' &&
                       byte != '\n') {

                s_talk.response_scan_state =
                    RESPONSE_SCAN_NORMAL;
            }

            break;

        case RESPONSE_SCAN_AUDIO_VALUE:
            append_response_byte(byte);

            if (byte == '"') {

                /*
                 * Preserve the opening quote in the metadata JSON,
                 * then stream the Base64 contents rather than storing them.
                 */
                if (!s_talk.tts_started && !s_talk.audio_error) {
                    esp_err_t ret = sparky_tts_player_begin();

                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG,
                                 "Failed to start TTS player: %s",
                                 esp_err_to_name(ret));

                        s_talk.audio_error = true;
                    } else {
                        s_talk.tts_started = true;
                    }
                }

                s_talk.response_scan_state =
                    RESPONSE_SCAN_AUDIO_STRING;

                s_talk.string_escape = false;

            } else if (byte != ' ' &&
                       byte != '\t' &&
                       byte != '\r' &&
                       byte != '\n') {

                s_talk.response_scan_state =
                    RESPONSE_SCAN_NORMAL;
            }

            break;

        case RESPONSE_SCAN_AUDIO_STRING:

            if (s_talk.string_escape) {
                s_talk.string_escape = false;
                s_talk.audio_error = true;
                break;
            }

            if (byte == '\\') {
                s_talk.string_escape = true;
                break;
            }

            if (byte == '"') {
                if (s_talk.tts_started) {
                    esp_err_t ret = sparky_tts_player_close_input();
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG,
                                 "TTS input ended with error: %s",
                                 esp_err_to_name(ret));
                        s_talk.audio_error = true;
                    }
                }

                append_response_byte(byte);
                s_talk.response_scan_state = RESPONSE_SCAN_NORMAL;
                break;
            }

            if (s_talk.audio_error || !s_talk.tts_started) {
                break;
            }

            {
                size_t start = i;
                while (i + 1 < length) {
                    char next = data[i + 1];
                    if (next == '"' || next == '\\') {
                        break;
                    }
                    ++i;
                }

                esp_err_t ret = sparky_tts_player_feed_base64(
                    data + start, i - start + 1);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "TTS Base64/decode error: %s",
                             esp_err_to_name(ret));
                    s_talk.audio_error = true;
                }
            }

            break;
        }
    }
}

/*
 * Response bytes are consumed by talk_task() via esp_http_client_read().
 * A user event handler is not used. The HTTP client still posts
 * ESP_HTTP_CLIENT_EVENT to the default loop internally.
 */

static void log_field(cJSON *root, const char *name)
{
    cJSON *item =
        cJSON_GetObjectItemCaseSensitive(root, name);

    if (cJSON_IsString(item) &&
        item->valuestring != NULL) {

        ESP_LOGI(TAG,
                 "  %s: %s",
                 name,
                 item->valuestring);

    } else if (cJSON_IsNumber(item)) {

        ESP_LOGI(TAG,
                 "  %s: %.3f",
                 name,
                 item->valuedouble);

    } else {

        ESP_LOGW(TAG,
                 "  %s: <missing>",
                 name);
    }
}

static void log_response(void)
{
    if (s_talk.response_overflow) {
        ESP_LOGE(TAG,
                 "Response metadata is too large to parse");
        return;
    }

    cJSON *root =
        cJSON_Parse(s_talk.response);

    if (root == NULL) {
        ESP_LOGE(TAG,
                 "Malformed JSON response");
        return;
    }

    ESP_LOGI(TAG, "TALK RESPONSE");

    log_field(root, "heard");
    log_field(root, "language");
    log_field(root, "confidence");
    log_field(root, "intent");
    log_field(root, "reply");
    log_field(root, "emotion");
    log_field(root, "sound");
    log_field(root, "action");
    log_field(root, "audioFormat");

    cJSON_Delete(root);
}

static bool write_all(
    esp_http_client_handle_t client,
    const char *data,
    size_t length)
{
    while (length > 0) {

        int written =
            esp_http_client_write(
                client,
                data,
                (int)length);

        if (written <= 0) {
            return false;
        }

        data += written;
        length -= (size_t)written;
    }

    return true;
}

static void talk_task(void *arg)
{
    (void)arg;

    char multipart_header[256];

    const char *multipart_footer =
        "\r\n--" SPARKY_TALK_BOUNDARY "--\r\n";

    int header_length =
        snprintf(
            multipart_header,
            sizeof(multipart_header),

            "--" SPARKY_TALK_BOUNDARY "\r\n"
            "Content-Disposition: form-data; "
            "name=\"audio\"; filename=\"capture.wav\"\r\n"
            "Content-Type: audio/wav\r\n\r\n");

    uint8_t wav_header[44];

    make_wav_header(
        wav_header,
        s_talk.pcm_bytes);

    size_t body_length =
        (size_t)header_length +
        sizeof(wav_header) +
        s_talk.pcm_bytes +
        strlen(multipart_footer);

    esp_http_client_config_t config = {
        .url = SPARKY_TALK_URL,

        /*
         * No event handler.
         *
         * Response data is consumed directly below by
         * esp_http_client_read().
         */
        .event_handler = NULL,

        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 120000,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL) {

        ESP_LOGE(TAG,
                 "Failed to create HTTP client");

        goto done;
    }

    char content_type[96];

    snprintf(
        content_type,
        sizeof(content_type),
        "multipart/form-data; boundary=%s",
        SPARKY_TALK_BOUNDARY);

    esp_http_client_set_method(
        client,
        HTTP_METHOD_POST);

    esp_http_client_set_header(
        client,
        "Content-Type",
        content_type);

    esp_http_client_set_header(
        client,
        "x-user-id",
        SPARKY_TALK_USER_ID);

    ESP_LOGI(
        TAG,
        "Sending %u-byte WAV to /talk",
        (unsigned)(sizeof(wav_header) +
                   s_talk.pcm_bytes));

    esp_err_t ret =
        esp_http_client_open(
            client,
            (int)body_length);

    if (ret != ESP_OK ||
        !write_all(
            client,
            multipart_header,
            (size_t)header_length) ||
        !write_all(
            client,
            (const char *)wav_header,
            sizeof(wav_header)) ||
        !write_all(
            client,
            (const char *)s_talk.pcm,
            s_talk.pcm_bytes) ||
        !write_all(
            client,
            multipart_footer,
            strlen(multipart_footer))) {

        ESP_LOGE(
            TAG,
            "HTTP upload failed: %s",
            esp_err_to_name(ret));

        esp_http_client_cleanup(client);

        goto done;
    }

    esp_http_client_fetch_headers(client);

    int status =
        esp_http_client_get_status_code(client);

    char read_buffer[256];
    int read_len;

    /*
     * Read the response directly.
     *
     * Each chunk is passed to the JSON/audio streaming parser.
     */
    while ((read_len =
                esp_http_client_read(
                    client,
                    read_buffer,
                    sizeof(read_buffer))) > 0) {

        collect_response_metadata(
            read_buffer,
            (size_t)read_len);
    }

    ESP_LOGI(TAG,
             "HTTP %d",
             status);

    if (status >= 200 &&
        status < 300) {

        log_response();

        if (s_talk.audio_error) {
            ESP_LOGE(
                TAG,
                "TTS audio playback encountered an error");
        }

    } else {

        ESP_LOGE(
            TAG,
            "Backend returned HTTP %d",
            status);
    }

    esp_http_client_cleanup(client);

done:

    if (s_talk.tts_started) {
        if (sparky_tts_player_is_active()) {
            ESP_LOGW(TAG, "Closing TTS input after HTTP response");
            sparky_tts_player_close_input();
        }

        esp_err_t tts_ret = sparky_tts_player_wait();
        if (tts_ret != ESP_OK) {
            ESP_LOGE(TAG, "TTS playback failed: %s",
                     esp_err_to_name(tts_ret));
            s_talk.audio_error = true;
        }
        s_talk.tts_started = false;
    }

    s_talk.busy = false;
    s_talk.complete = true;

    vTaskDelete(NULL);
}

esp_err_t sparky_talk_start(
    const int16_t *pcm,
    size_t pcm_bytes)
{
    if (s_talk.busy) {
        return ESP_ERR_INVALID_STATE;
    }

    if (pcm == NULL ||
        pcm_bytes == 0 ||
        pcm_bytes >
            SPARKY_TALK_MAX_WAV_BYTES - 44 ||
        pcm_bytes % sizeof(*pcm) != 0) {

        return ESP_ERR_INVALID_SIZE;
    }

    s_talk.pcm = pcm;
    s_talk.pcm_bytes = pcm_bytes;

    s_talk.response_length = 0;

    s_talk.response_scan_state =
        RESPONSE_SCAN_NORMAL;

    s_talk.string_token_length = 0;
    s_talk.string_escape = false;

    s_talk.response_overflow = false;
    s_talk.audio_error = false;
    s_talk.tts_started = false;
    s_talk.complete = false;

    s_talk.response =
        heap_caps_malloc(
            SPARKY_TALK_RESPONSE_MAX_BYTES,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT);

    if (s_talk.response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_talk.response[0] = '\0';

    s_talk.busy = true;

    if (xTaskCreate(
            talk_task,
            "sparky_talk",
            8192,
            NULL,
            5,
            NULL) != pdPASS) {

        free(s_talk.response);
        s_talk.response = NULL;
        s_talk.busy = false;

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool sparky_talk_is_busy(void)
{
    return s_talk.busy;
}

bool sparky_talk_is_complete(void)
{
    return s_talk.complete;
}

void sparky_talk_finish(void)
{
    free(s_talk.response);

    s_talk.response = NULL;
    s_talk.complete = false;
}
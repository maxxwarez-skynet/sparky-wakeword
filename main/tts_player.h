#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Incremental TTS playback of the V1 /talk "audio" JSON string.
 *
 * The HTTP reader feeds Base64 characters. Decode and I2S playback run on a
 * dedicated task so the MP3 decoder cannot overflow the HTTP task stack.
 */
esp_err_t sparky_tts_player_begin(void);
esp_err_t sparky_tts_player_feed_base64(const char *data, size_t length);
esp_err_t sparky_tts_player_close_input(void);
esp_err_t sparky_tts_player_wait(void);
bool sparky_tts_player_is_active(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start consuming one MP3 response.
 *
 * The caller then feeds the contents of the JSON "audio" string through
 * sparky_tts_player_feed_base64(). The player decodes and plays audio
 * incrementally; the complete MP3 is never buffered in RAM.
 */
esp_err_t sparky_tts_player_begin(void);
esp_err_t sparky_tts_player_feed_base64(const char *data, size_t length);
esp_err_t sparky_tts_player_end(void);
bool sparky_tts_player_is_active(void);

#ifdef __cplusplus
}
#endif

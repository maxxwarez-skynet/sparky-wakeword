#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Starts one asynchronous /talk request. PCM must remain valid until completion. */
esp_err_t sparky_talk_start(const int16_t *pcm, size_t pcm_bytes);
bool sparky_talk_is_busy(void);
bool sparky_talk_is_complete(void);
void sparky_talk_finish(void);

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t sparky_audio_init(void);

/* Speaker output uses 16 kHz, signed 16-bit stereo PCM. */
esp_err_t sparky_audio_output_init(void);
esp_err_t sparky_audio_play_pcm(const int16_t *samples, size_t frame_count);
esp_err_t sparky_audio_pa_enable(bool enable);
esp_err_t sparky_audio_output_test_tone(void);

esp_err_t sparky_audio_test(void);

esp_err_t sparky_audio_read(
    int16_t *samples,
    size_t sample_count
);

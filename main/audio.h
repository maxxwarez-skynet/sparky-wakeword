#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t sparky_audio_init(void);

esp_err_t sparky_audio_test(void);

esp_err_t sparky_audio_read(
    int16_t *samples,
    size_t sample_count
);
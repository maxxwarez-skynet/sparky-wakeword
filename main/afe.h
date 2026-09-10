#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t sparky_afe_init(void);

esp_err_t sparky_afe_feed(
    const int16_t *samples,
    size_t sample_count
);

esp_err_t sparky_afe_fetch(void);
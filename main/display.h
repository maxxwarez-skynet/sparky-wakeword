#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t sparky_display_init(void);
esp_err_t sparky_display_test(void);

esp_err_t sparky_display_touch_marker(uint16_t x, uint16_t y);
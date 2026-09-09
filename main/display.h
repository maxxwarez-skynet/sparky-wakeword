#pragma once

#include "esp_err.h"

/**
 * Initialize the Waveshare ST7789 display.
 */
esp_err_t sparky_display_init(void);

/**
 * Draw a temporary LCD test pattern.
 */
esp_err_t sparky_display_test(void);
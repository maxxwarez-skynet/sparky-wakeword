#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * Initialize the CST816T touch controller.
 */
esp_err_t sparky_touch_init(void);

/**
 * Read the current touch state.
 *
 * pressed = true  -> finger is currently touching
 * pressed = false -> no touch
 */
esp_err_t sparky_touch_read(
    uint16_t *x,
    uint16_t *y,
    bool *pressed
);
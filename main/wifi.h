#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t sparky_wifi_init(void);
bool sparky_wifi_is_connected(void);

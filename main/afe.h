#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include <stdbool.h>


esp_err_t sparky_afe_init(void);

esp_err_t sparky_afe_feed(
    const int16_t *samples,
    size_t sample_count
);

esp_err_t sparky_afe_fetch(void);

/* VAD state from the most recent successful AFE fetch: 0=silence, 1=speech. */
int sparky_afe_get_vad_state(void);

bool sparky_afe_wake_word_detected(void);

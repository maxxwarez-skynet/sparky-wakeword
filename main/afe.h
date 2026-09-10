#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_afe_sr_iface.h"

#include <stdbool.h>


esp_err_t sparky_afe_init(void);

esp_err_t sparky_afe_feed(
    const int16_t *samples,
    size_t sample_count
);

esp_err_t sparky_afe_fetch(void);

/* Valid until the next successful sparky_afe_fetch() call. */
const afe_fetch_result_t *sparky_afe_get_fetch_result(void);

/* VAD state from the most recent successful AFE fetch: 0=silence, 1=speech. */
int sparky_afe_get_vad_state(void);

bool sparky_afe_wake_word_detected(void);

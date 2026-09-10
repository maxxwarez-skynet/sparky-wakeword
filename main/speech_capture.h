#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_afe_sr_iface.h"

/* Processed AFE PCM: signed 16-bit, 16 kHz, mono. */
#define SPARKY_SPEECH_CAPTURE_SAMPLE_RATE          16000
#define SPARKY_SPEECH_CAPTURE_CHANNELS             1
#define SPARKY_SPEECH_CAPTURE_MAX_DURATION_MS      10000
#define SPARKY_SPEECH_CAPTURE_TRAILING_SILENCE_MS  800

esp_err_t sparky_speech_capture_start(void);
esp_err_t sparky_speech_capture_process_fetch_result(const afe_fetch_result_t *result);
bool sparky_speech_capture_is_active(void);
bool sparky_speech_capture_is_complete(void);
bool sparky_speech_capture_can_start(void);

/* The returned PSRAM buffer remains valid until sparky_speech_capture_reset(). */
esp_err_t sparky_speech_capture_get_data(const int16_t **samples, size_t *sample_count,
                                         size_t *byte_count);
void sparky_speech_capture_reset(void);

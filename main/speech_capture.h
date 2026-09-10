#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Raw I2S PCM capture: 16 kHz, signed 16-bit, stereo interleaved. */
#define SPARKY_SPEECH_CAPTURE_SAMPLE_RATE       16000
#define SPARKY_SPEECH_CAPTURE_CHANNELS          2
#define SPARKY_SPEECH_CAPTURE_MAX_DURATION_MS   10000
#define SPARKY_SPEECH_CAPTURE_TRAILING_SILENCE_MS 800

esp_err_t sparky_speech_capture_start(void);

/* Stores an input frame while recording; the buffer must be stereo PCM. */
esp_err_t sparky_speech_capture_add_samples(
    const int16_t *samples,
    size_t sample_count
);

/* Consumes the VAD state from one AFE fetch result. */
esp_err_t sparky_speech_capture_handle_vad(int vad_state);

bool sparky_speech_capture_is_recording(void);
bool sparky_speech_capture_is_complete(void);
bool sparky_speech_capture_can_start(void);

esp_err_t sparky_speech_capture_get_data(
    const int16_t **samples,
    size_t *sample_count,
    int *sample_rate,
    int *channels
);

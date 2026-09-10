# PROJECT_STATUS.md — Sparky

## Current milestone
Hardware + voice wake pipeline is working.

Current environment:
- ESP32-S3
- 16 MB Flash
- 8 MB OPI PSRAM
- ESP-IDF 6.1
- ESP-SR 2.5.1

## Working

### Display
LCD initialization and display test work. Wake currently produces a visible reaction. Cosmetics are not currently a priority.

### Touch
CST816 touch input works.

### Audio input
ES7210 microphone input works.
I2S capture works at 16 kHz, 16-bit, stereo.
`sparky_audio_read()` provides PCM samples.

### ESP-SR / AFE
Models load from the `model` partition.
AFE initializes successfully.

Current AFE:
- 16 kHz
- 2 feed channels
- 1024 samples/channel/feed
- 2048 `int16_t` samples per feed
- 512-sample mono fetch output
- two fetches per feed

### Wake word
WakeNet successfully detects **Hi Jolly**.

The WakeNet result becomes a one-shot application event:
```c
sparky_afe_wake_word_detected()
```

This has been tested successfully.

## Backend
The existing V1 backend must not be modified.

Its `/talk` endpoint accepts an audio upload and returns:
- transcription (`heard`)
- AI response (`reply`)
- emotion
- action
- synthesized audio
- metadata

Firmware is not yet connected to `/talk`.

## Current task
### Post-wake speech capture

Target flow:
```text
Wake word
   ↓
Start recording
   ↓
Capture user's utterance
   ↓
Use existing AFE/VAD information to detect end of speech
   ↓
Stop recording
   ↓
Produce audio data suitable for future /talk upload
```

HTTP networking is out of scope for this task.
MP3 playback is out of scope.
Cosmetic display work is out of scope.

## Immediate roadmap
1. Reliable post-wake speech capture.
2. Establish exact PCM/audio representation suitable for V1 `/talk`.
3. Firmware HTTP client for `/talk`.
4. Parse V1 response.
5. Play returned synthesized audio.
6. Use returned emotion/action for device behavior.

## Important open technical question
The exact relationship between raw microphone PCM, AFE feed input, AFE processed/fetched output, and the audio format expected by V1 transcription must be established before finalizing the upload path. Do not assume they are interchangeable.

## Maintenance
Update this file when a meaningful milestone is completed or architecture materially changes. It describes current state, not permanent project rules.

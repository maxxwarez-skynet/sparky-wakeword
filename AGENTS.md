# AGENT.md — Sparky Firmware Project

## Project
Learning Toy / Sparky is an ESP32-S3-based interactive learning device. This repository contains device firmware. The existing V1 backend is an external system and is **out of scope for modification**.

## Working principles
- Inspect the current repository and current files before changing code.
- Make the smallest change that accomplishes the current task.
- Preserve working functionality.
- Do not modify unrelated code.
- Prefer simple, explicit architecture over premature abstraction.
- Build after implementation.
- Report changed files, assumptions, build/test result, and remaining issues.
- Use PSRAM for large runtime buffers where appropriate.
- Never assume previous implementation is still present; inspect it.

## Hardware
Board: Waveshare ESP32-S3-Touch-LCD-1.54
- ESP32-S3R8
- 8 MB OPI PSRAM
- 16 MB Flash

Display: 240x240 ST7789
- SCLK 38, MOSI 39, CS 21, DC 45, RST 40, BL 46

Touch: CST816
- I2C 0x15
- SDA 42, SCL 41, INT 48, RST 47

Audio:
- ES7210 dual-microphone ADC
- ES8311 codec
- I2S MCLK 8, BCLK 9, WS 10, DIN 11, DOUT 12
- 16 kHz, 16-bit, stereo input
- ES7210 I2C 0x40

Shared I2C:
- SDA 42, SCL 41, I2C_NUM_0
- Do not create competing I2C master buses on these pins.

## Software
- ESP-IDF 6.1
- ESP-SR 2.5.1
- Target: esp32s3
- ESP-SR AFE V1
- WakeNet model: `wn9_hijolly_tts2`
- Model partition: `model`
- 8 MB OPI PSRAM is required by the current ESP-SR configuration.

## Current firmware modules
- `sparky-wakeword.c` — application/orchestration
- `board.h` — board/pins
- `display.c/.h` — LCD
- `touch.c/.h` — touch
- `audio.c/.h` — microphone/I2S and ES7210
- `i2c_bus.c/.h` — shared I2C
- `afe.c/.h` — ESP-SR AFE/WakeNet

## Important invariants

### Audio / AFE
Audio input works. The current ES7210 configuration is based on the official Waveshare configuration. ES7210 MAINCLK register 0x02 is `0xC1`; do not casually replace it.

AFE configuration:
- 16 kHz
- feed channels: 2
- feed chunk: 1024 samples/channel
- each feed: 2048 `int16_t` samples
- fetch channels: 1
- fetch chunk: 512 samples

The application currently performs **two AFE fetches after each 2048-sample feed**. This is intentional; one fetch caused AFE feed ringbuffer-full warnings. Preserve this unless deliberately redesigning and verifying the AFE flow.

### Wake word
WakeNet successfully detects **Hi Jolly**.
The firmware exposes a one-shot event:
```c
sparky_afe_wake_word_detected()
```
Do not break this.

## V1 backend boundary
**Do not modify the V1 backend.**

Existing `/talk` contract:
```text
POST /talk
multipart/form-data
  audio = audio file
```

The backend transcribes audio, obtains the AI reply, synthesizes speech, and determines an action.

Legacy JSON response fields:
```text
heard
language
confidence
intent
reply
emotion
sound
action
audio
audioFormat
```

Treat this as an external API contract.

## Intended product pipeline
```text
Hi Jolly
   ↓
WakeNet
   ↓
Wake event
   ↓
Capture user's utterance
   ↓
Send audio to existing V1 /talk
   ↓
Receive reply + emotion + action + audio
   ↓
Present/play response
```

This is implemented incrementally.

## Scope discipline
For each task, work only on the explicitly assigned objective.
Do not redesign the whole architecture, add speculative features, modify V1, replace working hardware configuration without evidence, or spend effort on cosmetic UI unless requested.

## Definition of done
- Requested functionality implemented.
- Existing functionality intact.
- Project builds successfully.
- Relevant runtime behavior tested when practical.
- Logs/errors are diagnostic.
- Changed files, assumptions, and results reported.

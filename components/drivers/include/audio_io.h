// Microphone (INMP441 on I2S0) and amplifier (MAX98357A on I2S1).
// Both run at 16 kHz. Reads and writes block until the DMA has room, so the
// 20 ms frame clock comes from the I2S hardware, not from a software timer.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define AUDIO_RATE_HZ       16000
#define AUDIO_FRAME_SAMPLES 320  // 20 ms

esp_err_t audio_init(void);

// Blocks until a full 20 ms frame is captured. Output is 16 bit mono with the DC
// offset removed and the mic gain applied (menuconfig: Walkie Talkie > mic shift).
void audio_read_frame(int16_t pcm[AUDIO_FRAME_SAMPLES]);

// Blocks until the frame is queued to the amplifier DMA.
void audio_write_frame(const int16_t pcm[AUDIO_FRAME_SAMPLES]);

// SD_MODE pin: false shuts the amp down (saves 2-3 mA and avoids hiss when idle).
void audio_amp_enable(bool on);

// Helpers.
int audio_peak(const int16_t *pcm, int n);
void audio_scale(int16_t *pcm, int n, int gain_q8);  // gain 256 = unity
void audio_tone(int16_t *pcm, int n, int freq_hz, int amplitude, uint32_t *phase);

// IMA ADPCM codec, 4:1 compression, self-contained frames.
// Portable C (no ESP-IDF dependencies) so it can be unit tested on a laptop.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADPCM_FRAME_SAMPLES 320                                    // 20 ms at 16 kHz
#define ADPCM_HDR_BYTES     4                                      // predictor (2), index (1), reserved (1)
#define ADPCM_FRAME_BYTES   (ADPCM_HDR_BYTES + ADPCM_FRAME_SAMPLES / 2)  // 164

typedef struct {
    int16_t predictor;
    uint8_t index;  // 0..88
} adpcm_state_t;

void adpcm_init(adpcm_state_t *s);

// Encode n samples (n must be even). The codec state at the start of the frame is
// written into the 4 byte header, so every frame can be decoded on its own and a
// lost packet never corrupts the frames after it. Returns bytes written (4 + n/2).
size_t adpcm_encode_frame(adpcm_state_t *s, const int16_t *pcm, size_t n, uint8_t *out);

// Decode one self-contained frame. Returns the number of samples written, or 0 if
// the frame is malformed or does not fit in max_samples.
size_t adpcm_decode_frame(const uint8_t *in, size_t len, int16_t *pcm, size_t max_samples);

#ifdef __cplusplus
}
#endif

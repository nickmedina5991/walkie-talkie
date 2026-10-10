#include "adpcm.h"

static const int8_t s_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

static const int16_t s_step_table[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

static inline int clamp_i(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Shared by encoder and decoder so both sides track exactly the same state.
static inline void update_state(adpcm_state_t *s, uint8_t code)
{
    int step = s_step_table[s->index];
    int vpdiff = step >> 3;
    if (code & 4) vpdiff += step;
    if (code & 2) vpdiff += step >> 1;
    if (code & 1) vpdiff += step >> 2;
    int p = s->predictor + ((code & 8) ? -vpdiff : vpdiff);
    s->predictor = (int16_t)clamp_i(p, -32768, 32767);
    s->index = (uint8_t)clamp_i(s->index + s_index_table[code], 0, 88);
}

static inline uint8_t encode_sample(adpcm_state_t *s, int16_t sample)
{
    int step = s_step_table[s->index];
    int diff = sample - s->predictor;
    uint8_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= step) {
        code |= 4;
        diff -= step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 2;
        diff -= step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 1;
    }
    update_state(s, code);
    return code;
}

void adpcm_init(adpcm_state_t *s)
{
    s->predictor = 0;
    s->index = 0;
}

size_t adpcm_encode_frame(adpcm_state_t *s, const int16_t *pcm, size_t n, uint8_t *out)
{
    n &= ~(size_t)1;
    out[0] = (uint8_t)(s->predictor & 0xFF);
    out[1] = (uint8_t)((uint16_t)s->predictor >> 8);
    out[2] = s->index;
    out[3] = 0;
    uint8_t *p = out + ADPCM_HDR_BYTES;
    for (size_t i = 0; i < n; i += 2) {
        uint8_t lo = encode_sample(s, pcm[i]);
        uint8_t hi = encode_sample(s, pcm[i + 1]);
        *p++ = (uint8_t)(lo | (hi << 4));  // low nibble first
    }
    return ADPCM_HDR_BYTES + n / 2;
}

size_t adpcm_decode_frame(const uint8_t *in, size_t len, int16_t *pcm, size_t max_samples)
{
    if (len < ADPCM_HDR_BYTES) return 0;
    adpcm_state_t s;
    s.predictor = (int16_t)(in[0] | (in[1] << 8));
    s.index = in[2];
    if (s.index > 88) return 0;
    size_t n = (len - ADPCM_HDR_BYTES) * 2;
    if (n > max_samples) return 0;
    const uint8_t *p = in + ADPCM_HDR_BYTES;
    for (size_t i = 0; i < n; i += 2) {
        uint8_t b = *p++;
        update_state(&s, b & 0x0F);
        pcm[i] = s.predictor;
        update_state(&s, b >> 4);
        pcm[i + 1] = s.predictor;
    }
    return n;
}

#include <math.h>
#include <string.h>

#include "adpcm.h"
#include "test_main.h"

#define N_FRAMES 50
#define TOTAL (N_FRAMES * ADPCM_FRAME_SAMPLES)

static int16_t s_in[TOTAL];
static int16_t s_out[TOTAL];
static uint8_t s_enc[N_FRAMES][ADPCM_FRAME_BYTES];

static double snr_db(const int16_t *a, const int16_t *b, int n, int skip)
{
    double sig = 0, err = 0;
    for (int i = skip; i < n; i++) {
        double d = (double)a[i] - (double)b[i];
        sig += (double)a[i] * a[i];
        err += d * d;
    }
    if (err == 0) return 200.0;
    return 10.0 * log10(sig / err);
}

static void encode_all(void)
{
    adpcm_state_t st;
    adpcm_init(&st);
    for (int f = 0; f < N_FRAMES; f++) {
        size_t n = adpcm_encode_frame(&st, &s_in[f * ADPCM_FRAME_SAMPLES], ADPCM_FRAME_SAMPLES, s_enc[f]);
        CHECK_EQ(n, ADPCM_FRAME_BYTES);
    }
}

static void test_sizes(void)
{
    CHECK_EQ(ADPCM_FRAME_BYTES, 164);
}

static void test_sine_sweep_snr(void)
{
    // 200 Hz to 4 kHz sweep at about -6 dBFS, 16 kHz sample rate.
    double phase = 0;
    for (int i = 0; i < TOTAL; i++) {
        double f = 200.0 + (4000.0 - 200.0) * i / TOTAL;
        phase += 2 * M_PI * f / 16000.0;
        s_in[i] = (int16_t)(16000.0 * sin(phase));
    }
    encode_all();
    for (int f = 0; f < N_FRAMES; f++) {
        size_t n = adpcm_decode_frame(s_enc[f], ADPCM_FRAME_BYTES, &s_out[f * ADPCM_FRAME_SAMPLES], ADPCM_FRAME_SAMPLES);
        CHECK_EQ(n, ADPCM_FRAME_SAMPLES);
    }
    double snr = snr_db(s_in, s_out, TOTAL, ADPCM_FRAME_SAMPLES);  // skip the warm-up frame
    printf("  sweep SNR = %.1f dB\n", snr);
    CHECK(snr > 20.0);
}

static void test_frames_decode_independently(void)
{
    // Decoding frame 30 on its own must give exactly what a continuous decode gives,
    // because each frame carries the codec state in its header.
    int16_t alone[ADPCM_FRAME_SAMPLES];
    size_t n = adpcm_decode_frame(s_enc[30], ADPCM_FRAME_BYTES, alone, ADPCM_FRAME_SAMPLES);
    CHECK_EQ(n, ADPCM_FRAME_SAMPLES);
    CHECK(memcmp(alone, &s_out[30 * ADPCM_FRAME_SAMPLES], sizeof(alone)) == 0);
}

static void test_full_scale_square_does_not_overflow(void)
{
    for (int i = 0; i < TOTAL; i++) s_in[i] = ((i / 20) & 1) ? 32767 : -32768;
    encode_all();
    for (int f = 0; f < N_FRAMES; f++) {
        adpcm_decode_frame(s_enc[f], ADPCM_FRAME_BYTES, &s_out[f * ADPCM_FRAME_SAMPLES], ADPCM_FRAME_SAMPLES);
    }
    // Late in the signal the decoder should follow the square closely in sign.
    int sign_errors = 0;
    for (int i = TOTAL / 2; i < TOTAL; i++) {
        if ((i % 20) < 5) continue;  // allow settling after each edge
        if ((s_in[i] > 0) != (s_out[i] > 0)) sign_errors++;
    }
    CHECK(sign_errors < TOTAL / 100);
}

static void test_silence_stays_quiet(void)
{
    memset(s_in, 0, sizeof(s_in));
    encode_all();
    int peak = 0;
    for (int f = 0; f < N_FRAMES; f++) {
        adpcm_decode_frame(s_enc[f], ADPCM_FRAME_BYTES, &s_out[f * ADPCM_FRAME_SAMPLES], ADPCM_FRAME_SAMPLES);
    }
    for (int i = 0; i < TOTAL; i++) {
        int a = s_out[i] < 0 ? -s_out[i] : s_out[i];
        if (a > peak) peak = a;
    }
    CHECK(peak < 16);
}

static void test_rejects_bad_frames(void)
{
    int16_t out[ADPCM_FRAME_SAMPLES];
    uint8_t bad[ADPCM_FRAME_BYTES] = {0};
    bad[2] = 89;  // index out of range
    CHECK_EQ(adpcm_decode_frame(bad, sizeof(bad), out, ADPCM_FRAME_SAMPLES), 0);
    CHECK_EQ(adpcm_decode_frame(bad, 3, out, ADPCM_FRAME_SAMPLES), 0);
    bad[2] = 0;
    CHECK_EQ(adpcm_decode_frame(bad, sizeof(bad), out, 100), 0);  // does not fit
}

void adpcm_tests(void)
{
    RUN(test_sizes);
    RUN(test_sine_sweep_snr);
    RUN(test_frames_decode_independently);
    RUN(test_full_scale_square_does_not_overflow);
    RUN(test_silence_stays_quiet);
    RUN(test_rejects_bad_frames);
}

#include "audio_io.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

static const char *TAG = "audio";

static i2s_chan_handle_t s_rx;
static i2s_chan_handle_t s_tx;
static int32_t s_raw[AUDIO_FRAME_SAMPLES];
static int16_t s_stereo[AUDIO_FRAME_SAMPLES * 2];
// Mic cleanup, run on every sample (the ESP32 has a hardware float unit):
//  1. High-pass filter at MIC_HPF_HZ, 2nd order Butterworth biquad. Removes the DC
//     offset plus breath, wind and handling rumble. Speech energy is mostly above 300 Hz.
//  2. Soft limiter above LIMIT_KNEE. Loud peaks are squashed smoothly toward full
//     scale instead of being chopped off, which is what made loud speech distort.
#define MIC_HPF_HZ  200.0f
#define LIMIT_KNEE  16000.0f
#define LIMIT_RANGE (32767.0f - LIMIT_KNEE)
static float s_b0, s_b1, s_b2, s_a1, s_a2;  // biquad coefficients (a0 normalized to 1)
static float s_z1, s_z2;                    // biquad state (transposed direct form II)

static void hpf_init(void)
{
    // RBJ audio EQ cookbook high-pass, Q = 1/sqrt(2)
    float w0 = 2.0f * (float)M_PI * MIC_HPF_HZ / (float)AUDIO_RATE_HZ;
    float cw = cosf(w0), alpha = sinf(w0) / (2.0f * 0.70710678f);
    float a0 = 1.0f + alpha;
    s_b0 = (1.0f + cw) / 2.0f / a0;
    s_b1 = -(1.0f + cw) / a0;
    s_b2 = s_b0;
    s_a1 = -2.0f * cw / a0;
    s_a2 = (1.0f - alpha) / a0;
    s_z1 = s_z2 = 0.0f;
}

static inline float soft_limit(float x)
{
    float a = fabsf(x);
    if (a <= LIMIT_KNEE) return x;
    float y = LIMIT_KNEE + LIMIT_RANGE * tanhf((a - LIMIT_KNEE) / LIMIT_RANGE);  // slope 1 at the knee
    return x < 0 ? -y : y;
}

static inline int16_t sat16(int32_t v)
{
    return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
}

esp_err_t audio_init(void)
{
    // Mic: I2S0 master RX. 10 ms DMA buffers, two per 20 ms frame.
    i2s_chan_config_t rx_chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    rx_chan.dma_desc_num = 4;
    rx_chan.dma_frame_num = AUDIO_FRAME_SAMPLES / 2;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&rx_chan, NULL, &s_rx), TAG, "rx channel");

    i2s_std_config_t rx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE_HZ),
        // INMP441 sends 24 bit samples left aligned in 32 bit Philips slots.
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_MIC_SCK,
            .ws = PIN_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = PIN_MIC_SD,
        },
    };
#if CONFIG_WT_MIC_RIGHT_SLOT
    rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
#else
    rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
#endif
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &rx_cfg), TAG, "rx init");

    // Amp: I2S1 master TX, 16 bit stereo with the same sample on both sides, so the
    // MAX98357A plays it whichever channel SD_MODE selects. Short DMA queue keeps
    // output latency to about 30 ms.
    i2s_chan_config_t tx_chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    tx_chan.dma_desc_num = 3;
    tx_chan.dma_frame_num = AUDIO_FRAME_SAMPLES / 2;
    tx_chan.auto_clear = true;  // send silence if we ever fall behind
    ESP_RETURN_ON_ERROR(i2s_new_channel(&tx_chan, &s_tx, NULL), TAG, "tx channel");

    i2s_std_config_t tx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_AMP_BCLK,
            .ws = PIN_AMP_LRC,
            .dout = PIN_AMP_DIN,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &tx_cfg), TAG, "tx init");

    hpf_init();
    gpio_config_t sd = {.pin_bit_mask = 1ULL << PIN_AMP_SD, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&sd);
    audio_amp_enable(false);

    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "rx enable");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "tx enable");
    ESP_LOGI(TAG, "I2S ready: mic on I2S0, amp on I2S1, %d Hz, %d sample frames", AUDIO_RATE_HZ,
             AUDIO_FRAME_SAMPLES);
    return ESP_OK;
}

void audio_read_frame(int16_t pcm[AUDIO_FRAME_SAMPLES])
{
    size_t got = 0;
    i2s_channel_read(s_rx, s_raw, sizeof(s_raw), &got, portMAX_DELAY);
    int n = (int)(got / sizeof(int32_t));
    const float scale = 1.0f / (float)(1u << CONFIG_WT_MIC_SHIFT);  // mic gain
    for (int i = 0; i < n; i++) {
        float x = (float)s_raw[i] * scale;
        float y = s_b0 * x + s_z1;  // high-pass biquad
        s_z1 = s_b1 * x - s_a1 * y + s_z2;
        s_z2 = s_b2 * x - s_a2 * y;
        pcm[i] = sat16((int32_t)lrintf(soft_limit(y)));
    }
    for (int i = n; i < AUDIO_FRAME_SAMPLES; i++) pcm[i] = 0;
}

void audio_write_frame(const int16_t pcm[AUDIO_FRAME_SAMPLES])
{
    for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++) {
        s_stereo[2 * i] = pcm[i];
        s_stereo[2 * i + 1] = pcm[i];
    }
    size_t written = 0;
    i2s_channel_write(s_tx, s_stereo, sizeof(s_stereo), &written, portMAX_DELAY);
}

void audio_amp_enable(bool on)
{
    gpio_set_level(PIN_AMP_SD, on ? 1 : 0);
}

int audio_peak(const int16_t *pcm, int n)
{
    int peak = 0;
    for (int i = 0; i < n; i++) {
        int a = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (a > peak) peak = a;
    }
    return peak;
}

void audio_scale(int16_t *pcm, int n, int gain_q8)
{
    for (int i = 0; i < n; i++) pcm[i] = sat16((pcm[i] * gain_q8) >> 8);
}

// One quarter of a sine wave, 0 to 32767, 65 points. The other three quarters are mirrored.
static const int16_t s_quarter_sine[65] = {
    0,     804,   1608,  2410,  3212,  4011,  4808,  5602,  6393,  7179,  7962,  8739,  9512,
    10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868,
    19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811, 25329, 25832, 26319,
    26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571, 30852, 31113,
    31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757, 32767,
};

static inline int sine_q15(uint32_t phase)
{
    uint32_t idx = phase >> 24;  // 0..255 around the circle
    uint32_t q = idx >> 6;       // which quarter
    uint32_t i = idx & 63;
    int v = (q & 1) ? s_quarter_sine[64 - i] : s_quarter_sine[i];
    return (q & 2) ? -v : v;
}

void audio_tone(int16_t *pcm, int n, int freq_hz, int amplitude, uint32_t *phase)
{
    // Sine wave from a 32 bit phase accumulator. A square wave has sharp edges that
    // pull current spikes from the amp on every transition and can trip its protection.
    uint32_t step = (uint32_t)(((uint64_t)freq_hz << 32) / AUDIO_RATE_HZ);
    for (int i = 0; i < n; i++) {
        *phase += step;
        pcm[i] = (int16_t)((sine_q15(*phase) * amplitude) >> 15);
    }
}

#include "audio_io.h"

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
// DC blocker state: y[n] = x[n] - x[n-1] + R * y[n-1], R = 0.995 in Q15.
static int32_t s_dc_x1, s_dc_y1;
#define DC_R_Q15 32604

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
    for (int i = 0; i < n; i++) {
        int32_t x = s_raw[i] >> CONFIG_WT_MIC_SHIFT;
        int32_t y = x - s_dc_x1 + (int32_t)(((int64_t)DC_R_Q15 * s_dc_y1) >> 15);  // 64 bit: x can exceed 16 bits
        s_dc_x1 = x;
        s_dc_y1 = y;
        pcm[i] = sat16(y);
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

void audio_tone(int16_t *pcm, int n, int freq_hz, int amplitude, uint32_t *phase)
{
    // Square wave from a 32 bit phase accumulator. Cheap and loud enough for beeps.
    uint32_t step = (uint32_t)(((uint64_t)freq_hz << 32) / AUDIO_RATE_HZ);
    for (int i = 0; i < n; i++) {
        *phase += step;
        pcm[i] = (*phase & 0x80000000u) ? (int16_t)amplitude : (int16_t)-amplitude;
    }
}

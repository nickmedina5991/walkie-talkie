// Mode 2: audio loopback through the real codec path, on one unit.
//   capture task: mic -> 20 ms frame -> ADPCM encode -> queue (only while PTT is held,
//                 so the speaker does not feed back into the mic)
//   playback task: queue -> drop some frames on purpose -> decode or conceal -> speaker
// Turn the encoder for volume. Press it to cycle simulated packet loss 0/5/10/20/30%
// and hear what the concealment sounds like. Encode/decode times print every second.
#include <stdio.h>
#include <string.h>

#include "adpcm.h"
#include "app_common.h"
#include "audio_io.h"
#include "dbg_pins.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "input.h"
#include "oled.h"
#include "sdkconfig.h"

static const char *TAG = "loopback";

typedef struct {
    uint8_t data[ADPCM_FRAME_BYTES];
} enc_frame_t;

static QueueHandle_t s_q;
static volatile int s_volume = 5;  // 0..10
static volatile int s_loss_pct;
static volatile int s_peak;
static volatile uint32_t s_enc_us, s_dec_us, s_frames, s_dropped, s_concealed, s_q_full;

static int volume_gain_q8(int v)
{
    return v * v * 256 / 25;  // 5 = unity, 10 = 4x
}

static void capture_task(void *arg)
{
    int16_t pcm[AUDIO_FRAME_SAMPLES];
    adpcm_state_t st;
    adpcm_init(&st);
    for (;;) {
        audio_read_frame(pcm);
        DBG_HI(PIN_DBG_CAPTURE);
        int p = audio_peak(pcm, AUDIO_FRAME_SAMPLES);
        s_peak = p > s_peak ? p : (s_peak * 7) / 8;
        if (input_ptt_held()) {
            enc_frame_t f;
            int64_t t0 = esp_timer_get_time();
            adpcm_encode_frame(&st, pcm, AUDIO_FRAME_SAMPLES, f.data);
            s_enc_us = (uint32_t)(esp_timer_get_time() - t0);
            if (xQueueSend(s_q, &f, 0) != pdTRUE) s_q_full++;
        }
        DBG_LO(PIN_DBG_CAPTURE);
    }
}

static void playback_task(void *arg)
{
    int16_t out[AUDIO_FRAME_SAMPLES];
    int16_t last[AUDIO_FRAME_SAMPLES] = {0};
    int conceal = 0, idle = 0;
    for (;;) {
        enc_frame_t f;
        // Wait up to one frame for audio; otherwise play silence to keep the clock going.
        bool got = xQueueReceive(s_q, &f, pdMS_TO_TICKS(15)) == pdTRUE;
        DBG_HI(PIN_DBG_PLAYBACK);
        if (got) {
            s_frames++;
            idle = 0;
            audio_amp_enable(true);
            if ((int)(esp_random() % 100) < s_loss_pct) {
                s_dropped++;
                if (conceal < 3) {
                    for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++) last[i] /= 2;
                    memcpy(out, last, sizeof(out));
                    conceal++;
                    s_concealed++;
                } else {
                    memset(out, 0, sizeof(out));
                }
            } else {
                int64_t t0 = esp_timer_get_time();
                adpcm_decode_frame(f.data, sizeof(f.data), out, AUDIO_FRAME_SAMPLES);
                s_dec_us = (uint32_t)(esp_timer_get_time() - t0);
                memcpy(last, out, sizeof(out));
                conceal = 0;
            }
            audio_scale(out, AUDIO_FRAME_SAMPLES, volume_gain_q8(s_volume));
        } else {
            memset(out, 0, sizeof(out));
            if (++idle == 15) audio_amp_enable(false);
        }
        DBG_LO(PIN_DBG_PLAYBACK);
        audio_write_frame(out);
    }
}

void app_loopback(void)
{
    static const int loss_steps[] = {0, 5, 10, 20, 30};
    int loss_idx = 0;
    bool oled_ok = oled_init(CONFIG_WT_OLED_ADDR) == ESP_OK;
    QueueHandle_t events = xQueueCreate(8, sizeof(input_event_t));
    ESP_ERROR_CHECK(input_init(events));
    ESP_ERROR_CHECK(audio_init());
    s_q = xQueueCreate(4, sizeof(enc_frame_t));
    xTaskCreatePinnedToCore(playback_task, "playback", 4096, NULL, PRIO_PLAYBACK, NULL, CORE_AUDIO);
    xTaskCreatePinnedToCore(capture_task, "capture", 4096, NULL, PRIO_CAPTURE, NULL, CORE_AUDIO);
    ESP_LOGI(TAG, "Hold PTT and talk. Keep the speaker away from the mic or turn the volume down.");

    uint32_t last_log = 0;
    for (;;) {
        input_event_t ev;
        while (xQueueReceive(events, &ev, 0)) {
            if (ev == INPUT_ENC_SHORT) {
                loss_idx = (loss_idx + 1) % 5;
                s_loss_pct = loss_steps[loss_idx];
                ESP_LOGI(TAG, "simulated loss %d%%", s_loss_pct);
            }
        }
        int d = input_encoder_delta();
        if (d) {
            int v = s_volume + d;
            s_volume = v < 0 ? 0 : (v > 10 ? 10 : v);
        }
        if (oled_ok) {
            oled_clear();
            oled_text(0, 0, "LOOPBACK", 1);
            oled_text(0, 12, input_ptt_held() ? "TALKING" : "Hold PTT to talk", 1);
            oled_printf(0, 24, 1, "VOL %d  LOSS %d%%", s_volume, s_loss_pct);
            oled_printf(0, 34, 1, "ENC %luus DEC %luus", (unsigned long)s_enc_us, (unsigned long)s_dec_us);
            oled_text(0, 54, "MIC", 1);
            oled_bar(22, 53, 106, 10, s_peak, 8000);
            oled_flush();
        }
        uint32_t t = now_ms();
        if (t - last_log >= 1000) {
            last_log = t;
            printf("frames=%lu dropped=%lu concealed=%lu q_full=%lu enc=%luus dec=%luus peak=%d vol=%d loss=%d%%\n",
                   (unsigned long)s_frames, (unsigned long)s_dropped, (unsigned long)s_concealed,
                   (unsigned long)s_q_full, (unsigned long)s_enc_us, (unsigned long)s_dec_us, s_peak, s_volume,
                   s_loss_pct);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

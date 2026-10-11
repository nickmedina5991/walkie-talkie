// Mode 1: hardware test. Proves every part is wired right before any real firmware.
//   OLED      shows this screen (if it stays dark, check the serial log for the I2C probe)
//   Encoder   turn it: the count changes. Press it: short/long press is shown and beeps
//   PTT       hold it: a 1 kHz tone plays on the speaker
//   Mic       talk or tap it: the level bar moves
//   Battery   voltage and percent (shows about 4.2 V on USB with no battery plugged in)
#include <stdio.h>

#include "app_common.h"
#include "audio_io.h"
#include "battery.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "input.h"
#include "oled.h"
#include "sdkconfig.h"

static const char *TAG = "hwtest";

static volatile int s_mic_peak;
static volatile int s_beep_frames;
static volatile int s_beep_hz = 1000;

static void hw_audio_task(void *arg)
{
    int16_t in[AUDIO_FRAME_SAMPLES];
    int16_t out[AUDIO_FRAME_SAMPLES];
    uint32_t phase = 0;
    int idle_frames = 0;
    for (;;) {
        audio_read_frame(in);
        int p = audio_peak(in, AUDIO_FRAME_SAMPLES);
        // Fast attack, slow release so the bar is readable.
        s_mic_peak = p > s_mic_peak ? p : (s_mic_peak * 7) / 8;

        if (s_beep_frames > 0 || input_ptt_held()) {
            audio_amp_enable(true);
            audio_tone(out, AUDIO_FRAME_SAMPLES, input_ptt_held() ? 1000 : s_beep_hz, 3000, &phase);
            if (s_beep_frames > 0) s_beep_frames--;
            idle_frames = 0;
        } else {
            for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++) out[i] = 0;
            if (++idle_frames == 10) audio_amp_enable(false);
        }
        audio_write_frame(out);
    }
}

void app_hwtest(void)
{
    bool oled_ok = oled_init(CONFIG_WT_OLED_ADDR) == ESP_OK;
    QueueHandle_t events = xQueueCreate(8, sizeof(input_event_t));
    ESP_ERROR_CHECK(input_init(events));
    ESP_ERROR_CHECK(battery_init());
    ESP_ERROR_CHECK(audio_init());
    xTaskCreatePinnedToCore(hw_audio_task, "hw_audio", 4096, NULL, PRIO_CAPTURE, NULL, CORE_AUDIO);

    ESP_LOGI(TAG, "OLED: %s", oled_ok ? "found" : "NOT FOUND");
    ESP_LOGI(TAG, "Turn the encoder, press it, hold PTT, talk into the mic.");

    const char *last_event = "-";
    int presses = 0;
    uint32_t last_log = 0;
    int mv = battery_mv();
    for (int tick = 0;; tick++) {
        input_event_t ev;
        while (xQueueReceive(events, &ev, 0)) {
            switch (ev) {
            case INPUT_PTT_DOWN: last_event = "PTT down"; break;
            case INPUT_PTT_UP: last_event = "PTT up"; break;
            case INPUT_ENC_SHORT:
                last_event = "short press";
                s_beep_hz = 1500;
                s_beep_frames = 5;
                presses++;
                break;
            case INPUT_ENC_LONG:
                last_event = "long press";
                s_beep_hz = 600;
                s_beep_frames = 15;
                presses++;
                break;
            }
            ESP_LOGI(TAG, "event: %s", last_event);
        }
        if (tick % 20 == 0) mv = battery_mv();

        int enc = input_encoder_total();
        int peak = s_mic_peak;
        if (oled_ok) {
            oled_clear();
            oled_title("HARDWARE TEST", NULL);
            oled_printf(0, 17, 1, "ENC %d  SW %s", enc, input_enc_switch_held() ? "DOWN" : "up");
            oled_printf(0, 26, 1, "PTT %s", input_ptt_held() ? "HELD (tone)" : "up");
            oled_printf(0, 35, 1, "LAST %s", last_event);
            oled_printf(0, 44, 1, "BAT %d.%02dV %d%%", mv / 1000, (mv % 1000) / 10, battery_percent(mv));
            oled_text(0, 54, "MIC", 1);
            oled_bar(22, 53, 106, 10, peak, 8000);
            oled_flush();
        }
        uint32_t t = now_ms();
        if (t - last_log >= 1000) {
            last_log = t;
            printf("enc=%d sw=%d ptt=%d presses=%d mic_peak=%d bat=%dmV (%d%%)\n", enc, input_enc_switch_held(),
                   input_ptt_held(), presses, peak, mv, battery_percent(mv));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

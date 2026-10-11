// Mode 3: radio range test. Flash it on two units (bare Feathers work, the OLED is
// optional). Each sends 50 packets per second of 187 bytes, the same rate and size
// as voice, and reports what it hears from the other: packets per second, loss and
// RSSI. Press PTT to toggle this unit between normal and Long Range transmit.
// Walk apart and write down where loss passes 10%; that is your usable range.
#include <stdio.h>
#include <string.h>

#include "app_common.h"
#include "espnow_link.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "input.h"
#include "oled.h"
#include "packet.h"
#include "sdkconfig.h"

static const char *TAG = "radiotest";

#define TEST_PKT_BYTES 187
#define TEST_RATE_HZ   50

static MessageBufferHandle_t s_rx;
static volatile uint32_t s_tx_ok, s_tx_err;

// Updated by the rx task, read and reset once a second by the report loop.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_peer, s_rx_count, s_lost, s_last_seq;
static int32_t s_rssi_sum;
static int s_rssi_min = 0, s_rssi_max = -127;
static bool s_peer_lr;

static void tx_task(void *arg)
{
    uint8_t pkt[TEST_PKT_BYTES] = {0};
    pkt_hdr_t h = {.version = PKT_VERSION, .type = PKT_TYPE_TEST, .channel = 0, .sender = link_my_id()};
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        h.seq++;
        h.flags = link_long_range() ? PKT_FLAG_LR : 0;
        h.timestamp_us = (uint32_t)esp_timer_get_time();
        pkt_write_hdr(&h, pkt);
        if (link_send(pkt, sizeof(pkt)) == ESP_OK)
            s_tx_ok++;
        else
            s_tx_err++;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000 / TEST_RATE_HZ));
    }
}

static void rx_task(void *arg)
{
    uint8_t msg[LINK_RX_MSG_MAX];
    for (;;) {
        size_t n = xMessageBufferReceive(s_rx, msg, sizeof(msg), portMAX_DELAY);
        if (n < 1 + PKT_HDR_BYTES) continue;
        int rssi = (int8_t)msg[0];
        pkt_hdr_t h;
        if (pkt_read_hdr(msg + 1, n - 1, &h) != 0 || h.type != PKT_TYPE_TEST) continue;
        led_set(true);
        taskENTER_CRITICAL(&s_mux);
        if (h.sender != s_peer) {  // first packet, or a different unit
            s_peer = h.sender;
            s_last_seq = h.seq - 1;
        }
        int32_t gap = (int32_t)(h.seq - s_last_seq);
        if (gap > 1 && gap < 1000) s_lost += (uint32_t)(gap - 1);
        s_last_seq = h.seq;  // also resyncs if the other unit rebooted
        s_rx_count++;
        s_rssi_sum += rssi;
        if (rssi < s_rssi_min) s_rssi_min = rssi;
        if (rssi > s_rssi_max) s_rssi_max = rssi;
        s_peer_lr = (h.flags & PKT_FLAG_LR) != 0;
        taskEXIT_CRITICAL(&s_mux);
        led_set(false);
    }
}

void app_radiotest(void)
{
    bool oled_ok = oled_init(CONFIG_WT_OLED_ADDR) == ESP_OK;
    QueueHandle_t events = xQueueCreate(8, sizeof(input_event_t));
    bool have_input = input_init(events) == ESP_OK;
    s_rx = xMessageBufferCreate(16 * LINK_RX_MSG_MAX);
    ESP_ERROR_CHECK(link_init(CONFIG_WT_WIFI_CHANNEL, WT_START_LONG_RANGE, s_rx));
    xTaskCreatePinnedToCore(rx_task, "radio_rx", 4096, NULL, PRIO_RADIO_RX, NULL, CORE_NET);
    xTaskCreatePinnedToCore(tx_task, "radio_tx", 4096, NULL, PRIO_RADIO_TX, NULL, CORE_NET);
    ESP_LOGI(TAG, "Sending %d x %d byte packets per second. Press PTT to toggle Long Range.", TEST_RATE_HZ,
             TEST_PKT_BYTES);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        input_event_t ev;
        while (have_input && xQueueReceive(events, &ev, 0)) {
            if (ev == INPUT_PTT_DOWN) link_set_long_range(!link_long_range());
        }
        taskENTER_CRITICAL(&s_mux);
        uint32_t rx = s_rx_count, lost = s_lost, peer = s_peer;
        int avg = rx ? (int)(s_rssi_sum / (int32_t)rx) : 0;
        int mn = s_rssi_min, mx = s_rssi_max;
        bool peer_lr = s_peer_lr;
        s_rx_count = s_lost = 0;
        s_rssi_sum = 0;
        s_rssi_min = 0;
        s_rssi_max = -127;
        taskEXIT_CRITICAL(&s_mux);

        uint32_t total = rx + lost;
        int loss_pct = total ? (int)(lost * 100 / total) : 100;
        printf("me=%08lX tx=%s peer=%08lX(%s) rx=%lu/s lost=%lu loss=%d%% rssi avg=%d min=%d max=%d tx_ok=%lu "
               "tx_err=%lu dropped=%lu\n",
               (unsigned long)link_my_id(), link_long_range() ? "LR" : "NRM", (unsigned long)peer,
               peer_lr ? "LR" : "NRM", (unsigned long)rx, (unsigned long)lost, loss_pct, avg, mn, mx,
               (unsigned long)s_tx_ok, (unsigned long)s_tx_err, (unsigned long)link_rx_dropped());
        if (oled_ok) {
            oled_clear();
            oled_title("RANGE TEST", link_long_range() ? "TX LR" : "TX NRM");
            if (rx == 0) {
                oled_text(0, 24, "NO SIGNAL", 2);
            } else {
                oled_printf(0, 18, 2, "%d%% LOSS", loss_pct);
                oled_printf(0, 35, 1, "RX %lu/s  RSSI %d", (unsigned long)rx, avg);
                oled_bar(0, 45, 128, 8, avg + 100, 70);  // -100 dBm empty, -30 dBm full
            }
            oled_text(0, 56, "PTT: toggle LR", 1);
            oled_flush();
        }
    }
}

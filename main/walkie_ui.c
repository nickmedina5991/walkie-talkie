// Walkie talkie UI: the ui task handles input events and menus, the display task
// redraws the OLED at 10 Hz from the shared state. Only the display task touches
// the OLED framebuffer.
//
// Controls
//   PTT            hold to talk (busy tone if someone else has the channel)
//   Turn (home)    volume
//   Press (home)   open menu; turn to move, press to select
//   Long press     back / cancel
#include <stdio.h>
#include <string.h>

#include "app_common.h"
#include "battery.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "input.h"
#include "oled.h"
#include "sdkconfig.h"
#include "walkie.h"

static const char *TAG = "ui";

#define STUCK_PTT_MS  60000
#define VOLUME_SAVE_MS 2000

typedef enum { SCR_HOME, SCR_MENU, SCR_CHANNEL, SCR_MODE, SCR_ROSTER, SCR_STATS } screen_t;

static const char *const MENU_ITEMS[] = {"Channel", "Mode", "Roster", "Stats", "Back"};
#define MENU_COUNT (int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]))

static QueueHandle_t s_events;
static TimerHandle_t s_stuck_timer;
static volatile screen_t s_screen = SCR_HOME;
static volatile int s_menu_idx;
static volatile int s_edit_val;
static volatile bool s_ptt_locked;  // after a stuck-PTT cutoff or a lost tie break, wait for release
static uint32_t s_volume_changed_ms;
static bool s_volume_dirty;

static void stuck_ptt_cb(TimerHandle_t t)
{
    if (xEventGroupGetBits(g_ev) & EV_TX_ACTIVE) {
        wt_stop_tx();
        wt_busy_tone();
        s_ptt_locked = true;
        ESP_LOGW(TAG, "PTT held for 60 s, transmit cut off. Release and press again.");
    }
}

static void on_event(input_event_t ev)
{
    switch (ev) {
    case INPUT_PTT_DOWN:
        if (!s_ptt_locked && wt_start_tx()) xTimerReset(s_stuck_timer, 0);
        break;
    case INPUT_PTT_UP:
        s_ptt_locked = false;
        xTimerStop(s_stuck_timer, 0);
        if (xEventGroupGetBits(g_ev) & EV_TX_ACTIVE) wt_stop_tx();
        break;
    case INPUT_ENC_SHORT:
        switch (s_screen) {
        case SCR_HOME:
            s_menu_idx = 0;
            s_screen = SCR_MENU;
            break;
        case SCR_MENU:
            switch (s_menu_idx) {
            case 0:
                s_edit_val = g_settings.channel;
                s_screen = SCR_CHANNEL;
                break;
            case 1:
                s_edit_val = g_settings.long_range;
                s_screen = SCR_MODE;
                break;
            case 2: s_screen = SCR_ROSTER; break;
            case 3: s_screen = SCR_STATS; break;
            default: s_screen = SCR_HOME; break;
            }
            break;
        case SCR_CHANNEL:
            wt_set_channel((uint8_t)s_edit_val);
            wt_settings_save();
            s_screen = SCR_MENU;
            break;
        case SCR_MODE:
            wt_set_long_range(s_edit_val != 0);
            wt_settings_save();
            s_screen = SCR_MENU;
            break;
        default: s_screen = SCR_MENU; break;
        }
        break;
    case INPUT_ENC_LONG:
        s_screen = (s_screen == SCR_MENU || s_screen == SCR_HOME) ? SCR_HOME : SCR_MENU;
        break;
    }
}

static void on_turn(int d)
{
    switch (s_screen) {
    case SCR_HOME: {
        int v = (int)g_settings.volume + d;
        v = v < 0 ? 0 : (v > WT_VOLUME_MAX ? WT_VOLUME_MAX : v);
        wt_set_volume((uint8_t)v);
        s_volume_dirty = true;
        s_volume_changed_ms = now_ms();
        break;
    }
    case SCR_MENU: s_menu_idx = ((s_menu_idx + d) % MENU_COUNT + MENU_COUNT) % MENU_COUNT; break;
    case SCR_CHANNEL: s_edit_val = ((s_edit_val - 1 + d) % 8 + 8) % 8 + 1; break;
    case SCR_MODE: s_edit_val = !s_edit_val; break;
    default: break;
    }
}

static void ui_task(void *arg)
{
    for (;;) {
        input_event_t ev;
        if (xQueueReceive(s_events, &ev, pdMS_TO_TICKS(50)) == pdTRUE) on_event(ev);
        int d = input_encoder_delta();
        if (d) on_turn(d);

        if (xEventGroupGetBits(g_ev) & EV_TX_PREEMPTED) {
            xEventGroupClearBits(g_ev, EV_TX_PREEMPTED);
            xTimerStop(s_stuck_timer, 0);
            led_set(false);
            wt_busy_tone();
            s_ptt_locked = true;
            ESP_LOGI(TAG, "both pressed PTT, the other unit keeps the channel");
        }
        // Save volume once it stops changing, not on every click (flash wear).
        if (s_volume_dirty && now_ms() - s_volume_changed_ms > VOLUME_SAVE_MS) {
            s_volume_dirty = false;
            wt_settings_save();
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing

static void draw_header(void)
{
    EventBits_t b = xEventGroupGetBits(g_ev);
    oled_printf(0, 0, 1, "CH%u %s", g_settings.channel, g_settings.long_range ? "LR" : "NRM");
    int mv = g_stats.battery_mv;
    char bat[8];
    int pct = battery_percent(mv);
    snprintf(bat, sizeof(bat), "%s%d%%", (b & EV_LOW_BATT) ? "!" : "", pct);
    oled_text(OLED_W - (int)strlen(bat) * 6, 0, bat, 1);
    for (int x = 0; x < OLED_W; x++) oled_pixel(x, 9, true);
}

static void draw_home(void)
{
    EventBits_t b = xEventGroupGetBits(g_ev);
    draw_header();
    wt_peer_t peers[WT_MAX_PEERS];
    int n = wt_roster(peers, WT_MAX_PEERS);

    if (b & EV_TX_ACTIVE) {
        oled_text(0, 14, "TALKING", 2);
        oled_text(0, 36, "MIC", 1);
        oled_bar(22, 35, 106, 9, g_stats.mic_peak, 8000);
    } else if (b & EV_RX_ACTIVE) {
        char name[WT_NAME_LEN + 1];
        wt_peer_name(wt_talker_id(), name);
        oled_text(0, 14, "RX", 2);
        oled_printf(30, 18, 1, "%s %ddBm", name, g_stats.last_rssi);
        oled_printf(0, 36, 1, "PEERS %d", n);
    } else if (b & EV_CH_BUSY) {
        oled_text(0, 14, "BUSY", 2);
        oled_printf(0, 36, 1, "PEERS %d", n);
    } else {
        oled_text(0, 14, "READY", 2);
        oled_printf(0, 36, 1, "PEERS %d  %s", n, g_settings.name);
    }
    oled_text(0, 50, "VOL", 1);
    oled_bar(22, 48, 106, 11, g_settings.volume, WT_VOLUME_MAX);
}

static void draw_menu(void)
{
    oled_text(0, 0, "MENU", 1);
    for (int x = 0; x < OLED_W; x++) oled_pixel(x, 9, true);
    for (int i = 0; i < MENU_COUNT; i++) {
        int y = 12 + i * 10;
        oled_text(4, y + 1, MENU_ITEMS[i], 1);
        if (i == s_menu_idx) oled_invert_rect(0, y, OLED_W, 9);
    }
}

static void draw_channel(void)
{
    oled_text(0, 0, "CHANNEL", 1);
    oled_printf(52, 18, 3, "%d", s_edit_val);
    oled_text(0, 56, "turn: change press: save", 1);
}

static void draw_mode(void)
{
    oled_text(0, 0, "RADIO MODE", 1);
    oled_text(0, 20, s_edit_val ? "LONG RANGE" : "NORMAL", 2);
    oled_text(0, 42, s_edit_val ? "250 kbps, more range" : "1 Mbps, short range", 1);
    oled_text(0, 56, "all units must match", 1);
}

static void draw_roster(void)
{
    wt_peer_t peers[WT_MAX_PEERS];
    int n = wt_roster(peers, WT_MAX_PEERS);
    oled_printf(0, 0, 1, "ROSTER CH%u (%d)", g_settings.channel, n);
    for (int x = 0; x < OLED_W; x++) oled_pixel(x, 9, true);
    if (n == 0) oled_text(0, 24, "nobody in range", 1);
    for (int i = 0; i < n && i < 5; i++) {
        oled_printf(0, 12 + i * 10, 1, "%-6s %4ddBm %3u%%", peers[i].name, peers[i].rssi, peers[i].battery_pct);
    }
}

static void draw_stats(void)
{
    oled_text(0, 0, "STATS", 1);
    oled_printf(0, 10, 1, "RX %lu LOST %lu", (unsigned long)g_stats.rx_audio, (unsigned long)g_stats.rx_lost);
    oled_printf(0, 19, 1, "UNDER %lu CONC %lu", (unsigned long)g_stats.underruns, (unsigned long)g_stats.concealed);
    oled_printf(0, 28, 1, "LATE %lu AUTH %lu", (unsigned long)g_stats.late, (unsigned long)g_stats.rx_auth_fail);
    oled_printf(0, 37, 1, "TX %lu QFULL %lu", (unsigned long)g_stats.tx_pkts, (unsigned long)g_stats.tx_queue_full);
    oled_printf(0, 46, 1, "ENC %luus DEC %luus", (unsigned long)g_stats.encode_us_max,
                (unsigned long)g_stats.decode_us_max);
    oled_printf(0, 55, 1, "ID %08lX", (unsigned long)wt_my_id());
}

static void display_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        oled_clear();
        switch (s_screen) {
        case SCR_HOME: draw_home(); break;
        case SCR_MENU: draw_menu(); break;
        case SCR_CHANNEL: draw_channel(); break;
        case SCR_MODE: draw_mode(); break;
        case SCR_ROSTER: draw_roster(); break;
        case SCR_STATS: draw_stats(); break;
        }
        oled_flush();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(100));
    }
}

void wt_ui_start(void)
{
    s_events = xQueueCreate(8, sizeof(input_event_t));
    ESP_ERROR_CHECK(input_init(s_events));
    s_stuck_timer = xTimerCreate("stuck_ptt", pdMS_TO_TICKS(STUCK_PTT_MS), pdFALSE, NULL, stuck_ptt_cb);
    xTaskCreatePinnedToCore(ui_task, "ui", 4096, NULL, PRIO_UI, NULL, CORE_NET);
    if (oled_init(CONFIG_WT_OLED_ADDR) == ESP_OK) {
        xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, PRIO_DISPLAY, NULL, CORE_NET);
    } else {
        ESP_LOGW(TAG, "no OLED, running headless (serial log still works)");
    }
}

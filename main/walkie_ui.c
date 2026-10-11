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

// Layout: titles sit in the top 16 rows (yellow on two-color OLEDs), content in
// rows 16-63 (blue). Nothing crosses row 16. One text line is 7 pixels tall.

static void draw_header(void)
{
    EventBits_t b = xEventGroupGetBits(g_ev);
    char left[16], bat[8];
    snprintf(left, sizeof(left), "CH%u %s", g_settings.channel, g_settings.long_range ? "LR" : "NRM");
    snprintf(bat, sizeof(bat), "%s%d%%", (b & EV_LOW_BATT) ? "!" : "", battery_percent(g_stats.battery_mv));
    oled_title(left, bat);
}

static void draw_home(void)
{
    EventBits_t b = xEventGroupGetBits(g_ev);
    draw_header();
    wt_peer_t peers[WT_MAX_PEERS];
    int n = wt_roster(peers, WT_MAX_PEERS);

    // rows 19-32: big state word
    if (b & EV_TX_ACTIVE) {
        oled_text(0, 19, "TALKING", 2);
        oled_text(0, 37, "MIC", 1);
        oled_bar(22, 36, 106, 9, g_stats.mic_peak, 8000);
    } else if (b & EV_RX_ACTIVE) {
        char name[WT_NAME_LEN + 1];
        wt_peer_name(wt_talker_id(), name);
        oled_text(0, 19, "RX", 2);
        oled_printf(30, 23, 1, "%s %ddBm", name, g_stats.last_rssi);
        oled_printf(0, 37, 1, "PEERS %d", n);
    } else if (b & EV_CH_BUSY) {
        oled_text(0, 19, "BUSY", 2);
        oled_printf(0, 37, 1, "PEERS %d", n);
    } else {
        oled_text(0, 19, "READY", 2);
        oled_printf(0, 37, 1, "PEERS %d  %s", n, g_settings.name);
    }
    // rows 51-61: volume
    oled_text(0, 53, "VOL", 1);
    oled_bar(22, 51, 106, 11, g_settings.volume, WT_VOLUME_MAX);
}

static void draw_menu(void)
{
    oled_title("MENU", NULL);
    for (int i = 0; i < MENU_COUNT; i++) {  // 5 items x 9 rows = rows 18-62
        int y = 18 + i * 9;
        oled_text(4, y + 1, MENU_ITEMS[i], 1);
        if (i == s_menu_idx) oled_invert_rect(0, y, OLED_W, 9);
    }
}

static void draw_channel(void)
{
    oled_title("CHANNEL", NULL);
    char num[4];
    snprintf(num, sizeof(num), "%d", s_edit_val);
    oled_text((OLED_W - oled_text_width(num, 3)) / 2, 19, num, 3);  // rows 19-39
    oled_text(0, 45, "turn: change", 1);
    oled_text(0, 55, "press: save", 1);
}

static void draw_mode(void)
{
    oled_title("RADIO MODE", NULL);
    oled_text(0, 19, s_edit_val ? "LONG RANGE" : "NORMAL", 2);
    oled_text(0, 41, s_edit_val ? "250 kbps, ADPCM audio" : "1 Mbps, raw audio", 1);
    oled_text(0, 54, "all units must match", 1);
}

static void draw_roster(void)
{
    wt_peer_t peers[WT_MAX_PEERS];
    int n = wt_roster(peers, WT_MAX_PEERS);
    char title[24];
    snprintf(title, sizeof(title), "ROSTER CH%u (%d)", g_settings.channel, n);
    oled_title(title, NULL);
    if (n == 0) oled_text(0, 34, "nobody in range", 1);
    for (int i = 0; i < n && i < 5; i++) {  // rows 18-62
        oled_printf(0, 18 + i * 9, 1, "%-6s %4ddBm %3u%%", peers[i].name, peers[i].rssi, peers[i].battery_pct);
    }
}

static void draw_stats(void)
{
    char id[16];
    snprintf(id, sizeof(id), "ID %08lX", (unsigned long)wt_my_id());
    oled_title("STATS", id);
    oled_printf(0, 18, 1, "RX %lu LOST %lu", (unsigned long)g_stats.rx_audio, (unsigned long)g_stats.rx_lost);
    oled_printf(0, 27, 1, "UNDER %lu CONC %lu", (unsigned long)g_stats.underruns, (unsigned long)g_stats.concealed);
    oled_printf(0, 36, 1, "LATE %lu AUTH %lu", (unsigned long)g_stats.late, (unsigned long)g_stats.rx_auth_fail);
    oled_printf(0, 45, 1, "TX %lu QFULL %lu", (unsigned long)g_stats.tx_pkts, (unsigned long)g_stats.tx_queue_full);
    oled_printf(0, 54, 1, "ENC %luus DEC %luus", (unsigned long)g_stats.encode_us_max,
                (unsigned long)g_stats.decode_us_max);
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

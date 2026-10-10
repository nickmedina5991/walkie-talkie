// Shared state between the walkie talkie tasks (app_walkie.c) and the UI (walkie_ui.c).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

// Event group bits: system state that several tasks wait on or check.
#define EV_TX_ACTIVE    BIT0  // PTT held and we own the channel
#define EV_RX_ACTIVE    BIT1  // playing someone's audio
#define EV_CH_BUSY      BIT2  // someone else is talking on our channel
#define EV_MODE_LR      BIT3  // transmitting in Long Range mode
#define EV_LOW_BATT     BIT4
#define EV_TX_PREEMPTED BIT5  // we lost a simultaneous-press tie break (UI plays busy tone)

#define WT_NAME_LEN   6
#define WT_MAX_PEERS  8
#define WT_VOLUME_MAX 10

typedef struct {
    uint8_t channel;  // 1..8
    uint8_t volume;   // 0..10
    bool long_range;
    char name[WT_NAME_LEN + 1];
} wt_settings_t;

typedef struct {
    uint32_t id;
    char name[WT_NAME_LEN + 1];
    uint8_t battery_pct;
    int8_t rssi;
    bool talking;
    bool long_range;
    uint32_t last_ms;
} wt_peer_t;

typedef struct {
    // transmit side
    volatile uint32_t tx_frames, tx_pkts, tx_errors, tx_queue_full, capture_overruns, beacons_sent;
    // receive side
    volatile uint32_t rx_pkts, rx_audio, rx_beacons, rx_malformed, rx_auth_fail, rx_replay, rx_other_channel,
        rx_lost, rx_floor_ignored;
    // playback (the real-time scorecard)
    volatile uint32_t underruns, concealed, late, played;
    // timing, microseconds
    volatile uint32_t encode_us_max, decode_us_max;
    volatile int8_t last_rssi;
    volatile int battery_mv;
    volatile int mic_peak;
} wt_stats_t;

extern EventGroupHandle_t g_ev;
extern wt_stats_t g_stats;
extern wt_settings_t g_settings;  // read freely, change only through the wt_set_* calls

// Called by the UI task.
void wt_set_channel(uint8_t ch);
void wt_set_long_range(bool lr);
void wt_set_volume(uint8_t v);
void wt_settings_save(void);
void wt_tone(int freq_hz, int duration_ms);
void wt_busy_tone(void);
bool wt_start_tx(void);  // false if the channel is busy
void wt_stop_tx(void);
uint32_t wt_my_id(void);
uint32_t wt_talker_id(void);
int wt_roster(wt_peer_t *out, int max);  // copy of peers heard in the last 7 s
bool wt_peer_name(uint32_t id, char *out);

// Implemented in walkie_ui.c
void wt_ui_start(void);

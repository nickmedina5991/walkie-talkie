#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"

#if CONFIG_WT_LONG_RANGE
#define WT_START_LONG_RANGE true
#else
#define WT_START_LONG_RANGE false
#endif

// Core and priority plan from the design doc. Audio lives on core 1, away from
// the Wi-Fi stack (which ESP-IDF runs at priority 23 on core 0).
#define CORE_AUDIO 1
#define CORE_NET   0

#define PRIO_PLAYBACK 20
#define PRIO_CAPTURE  19
#define PRIO_ENCODE   18
#define PRIO_RADIO_RX 17
#define PRIO_RADIO_TX 16
#define PRIO_LINK_MGR 10
#define PRIO_UI       8
#define PRIO_DISPLAY  5
#define PRIO_MONITOR  2

void board_init(void);       // NVS, on-board LED, I2C power, timing pins
void led_set(bool on);
uint32_t now_ms(void);

void app_hwtest(void);
void app_loopback(void);
void app_radiotest(void);
void app_walkie(void);

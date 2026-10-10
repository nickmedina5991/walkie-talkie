// Timing markers for a logic analyzer or scope. Each audio task drives its pin
// high while it works on a frame, so the trace shows execution time, the 20 ms
// period and any jitter. Turn off in menuconfig > Walkie Talkie.
#pragma once

#include "board.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#if CONFIG_WT_TIMING_PINS
static inline void dbg_pins_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_DBG_CAPTURE) | (1ULL << PIN_DBG_PLAYBACK) | (1ULL << PIN_DBG_ENCODE),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
}
#define DBG_HI(pin) gpio_set_level((pin), 1)
#define DBG_LO(pin) gpio_set_level((pin), 0)
#else
static inline void dbg_pins_init(void) {}
#define DBG_HI(pin) ((void)0)
#define DBG_LO(pin) ((void)0)
#endif

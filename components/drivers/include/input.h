// Push to talk button, encoder push switch and encoder rotation.
//
// Button edges fire a GPIO interrupt. The ISR only restarts a 20 ms one-shot
// FreeRTOS software timer for that button (debounce). When the timer expires the
// timer task reads the settled level and posts an event to the queue you pass in.
// Rotation is counted by the PCNT hardware, so no interrupt per click.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    INPUT_PTT_DOWN,
    INPUT_PTT_UP,
    INPUT_ENC_SHORT,  // released before 600 ms
    INPUT_ENC_LONG,   // held 600 ms or more (reported on release)
} input_event_t;

esp_err_t input_init(QueueHandle_t event_queue);  // queue of input_event_t
bool input_ptt_held(void);
bool input_enc_switch_held(void);
// Detents turned since the last call (positive = clockwise if A/B are wired as documented).
int input_encoder_delta(void);
int input_encoder_total(void);

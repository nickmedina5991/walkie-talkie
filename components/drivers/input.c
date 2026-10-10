#include "input.h"

#include "board.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/timers.h"

static const char *TAG = "input";

#define DEBOUNCE_MS   20
#define LONG_PRESS_MS 600
#define PCNT_LIMIT    10000
#define COUNTS_PER_DETENT 4  // full quadrature decoding: 4 edges per click

typedef struct {
    gpio_num_t pin;
    TimerHandle_t timer;
    volatile bool pressed;  // debounced state
    TickType_t down_tick;
} button_t;

static button_t s_ptt = {.pin = PIN_PTT};
static button_t s_sw = {.pin = PIN_ENC_SW};
static QueueHandle_t s_queue;
static pcnt_unit_handle_t s_pcnt;
static int s_last_detents;

static void IRAM_ATTR button_isr(void *arg)
{
    button_t *b = (button_t *)arg;
    BaseType_t woken = pdFALSE;
    xTimerResetFromISR(b->timer, &woken);  // restart the debounce window
    portYIELD_FROM_ISR(woken);
}

static void debounce_done(TimerHandle_t t)
{
    button_t *b = (button_t *)pvTimerGetTimerID(t);
    bool pressed = gpio_get_level(b->pin) == 0;  // active low
    if (pressed == b->pressed) return;            // bounce that settled back
    b->pressed = pressed;
    input_event_t ev;
    if (b == &s_ptt) {
        ev = pressed ? INPUT_PTT_DOWN : INPUT_PTT_UP;
    } else {
        if (pressed) {
            b->down_tick = xTaskGetTickCount();
            return;
        }
        uint32_t held_ms = pdTICKS_TO_MS(xTaskGetTickCount() - b->down_tick);
        ev = held_ms >= LONG_PRESS_MS ? INPUT_ENC_LONG : INPUT_ENC_SHORT;
    }
    if (s_queue) xQueueSend(s_queue, &ev, 0);
}

static esp_err_t button_init(button_t *b, const char *name)
{
    b->timer = xTimerCreate(name, pdMS_TO_TICKS(DEBOUNCE_MS), pdFALSE, b, debounce_done);
    ESP_RETURN_ON_FALSE(b->timer, ESP_ERR_NO_MEM, TAG, "timer");
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << b->pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio");
    b->pressed = gpio_get_level(b->pin) == 0;
    return gpio_isr_handler_add(b->pin, button_isr, b);
}

static esp_err_t encoder_init(void)
{
    pcnt_unit_config_t unit_cfg = {
        .low_limit = -PCNT_LIMIT,
        .high_limit = PCNT_LIMIT,
        .flags.accum_count = true,  // keep counting past the limits instead of jumping to 0
    };
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_cfg, &s_pcnt), TAG, "pcnt unit");
    pcnt_glitch_filter_config_t filter = {.max_glitch_ns = 1000};
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(s_pcnt, &filter), TAG, "filter");

    // Standard two-channel quadrature setup (same as the ESP-IDF rotary encoder example).
    pcnt_chan_config_t a_cfg = {.edge_gpio_num = PIN_ENC_A, .level_gpio_num = PIN_ENC_B};
    pcnt_chan_config_t b_cfg = {.edge_gpio_num = PIN_ENC_B, .level_gpio_num = PIN_ENC_A};
    pcnt_channel_handle_t ch_a, ch_b;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_pcnt, &a_cfg, &ch_a), TAG, "chan a");
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_pcnt, &b_cfg, &ch_b), TAG, "chan b");
    pcnt_channel_set_edge_action(ch_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(ch_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(ch_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(ch_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    // The Adafruit encoder is a bare switch to GND, so it needs pull-ups.
    gpio_pullup_en(PIN_ENC_A);
    gpio_pullup_en(PIN_ENC_B);

    // Watch points at the limits are required for accum_count to work.
    pcnt_unit_add_watch_point(s_pcnt, PCNT_LIMIT);
    pcnt_unit_add_watch_point(s_pcnt, -PCNT_LIMIT);
    ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_pcnt), TAG, "enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(s_pcnt), TAG, "clear");
    return pcnt_unit_start(s_pcnt);
}

esp_err_t input_init(QueueHandle_t event_queue)
{
    s_queue = event_queue;
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;  // already installed is fine
    ESP_RETURN_ON_ERROR(button_init(&s_ptt, "ptt_db"), TAG, "ptt");
    ESP_RETURN_ON_ERROR(button_init(&s_sw, "sw_db"), TAG, "enc switch");
    ESP_RETURN_ON_ERROR(encoder_init(), TAG, "encoder");
    ESP_LOGI(TAG, "PTT on GPIO%d, encoder A/B/SW on GPIO%d/%d/%d", PIN_PTT, PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW);
    return ESP_OK;
}

bool input_ptt_held(void)
{
    return s_ptt.pressed;
}

bool input_enc_switch_held(void)
{
    return s_sw.pressed;
}

int input_encoder_total(void)
{
    int count = 0;
    pcnt_unit_get_count(s_pcnt, &count);
    // Round toward the nearest detent so a half-turned knob does not flicker.
    return (count >= 0 ? count + COUNTS_PER_DETENT / 2 : count - COUNTS_PER_DETENT / 2) / COUNTS_PER_DETENT;
}

int input_encoder_delta(void)
{
    int now = input_encoder_total();
    int d = now - s_last_detents;
    s_last_detents = now;
    return d;
}

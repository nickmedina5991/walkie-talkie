#include "app_common.h"

#include "board.h"
#include "dbg_pins.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "nvs_flash.h"

void board_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_LED) | (1ULL << PIN_I2C_POWER),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    gpio_set_level(PIN_I2C_POWER, 1);  // STEMMA QT / NeoPixel power on
    gpio_set_level(PIN_LED, 0);
    dbg_pins_init();
}

void led_set(bool on)
{
    gpio_set_level(PIN_LED, on ? 1 : 0);
}

uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

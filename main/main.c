// ESP32 walkie talkie. Pick the app mode in: idf.py menuconfig > Walkie Talkie.
#include "app_common.h"
#include "esp_log.h"
#include "sdkconfig.h"

void app_main(void)
{
    board_init();
#if CONFIG_WT_MODE_HWTEST
    ESP_LOGI("main", "mode: hardware test");
    app_hwtest();
#elif CONFIG_WT_MODE_LOOPBACK
    ESP_LOGI("main", "mode: audio loopback");
    app_loopback();
#elif CONFIG_WT_MODE_RADIOTEST
    ESP_LOGI("main", "mode: radio range test");
    app_radiotest();
#else
    ESP_LOGI("main", "mode: walkie talkie");
    app_walkie();
#endif
}

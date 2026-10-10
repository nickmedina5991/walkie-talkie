#include "battery.h"

#include "board.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "battery";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;

esp_err_t battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = VBAT_ADC_UNIT};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &s_adc), TAG, "adc unit");
    adc_oneshot_chan_cfg_t ch = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, VBAT_ADC_CHANNEL, &ch), TAG, "adc channel");
    adc_cali_line_fitting_config_t cali = {
        .unit_id = VBAT_ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "no ADC calibration in eFuse, readings will be rough");
        s_cali = NULL;
    }
    return ESP_OK;
}

int battery_mv(void)
{
    if (!s_adc) return -1;
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(s_adc, VBAT_ADC_CHANNEL, &raw) != ESP_OK) return -1;
        if (s_cali)
            adc_cali_raw_to_voltage(s_cali, raw, &mv);
        else
            mv = raw * 3100 / 4095;
        sum += mv;
    }
    return (sum / 8) * VBAT_DIVIDER;
}

int battery_percent(int mv)
{
    // Resting LiPo voltage to remaining charge, roughly. Under load it reads lower.
    static const struct {
        int mv, pct;
    } curve[] = {{4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 50},
                 {3700, 30},  {3600, 15}, {3500, 5},  {3300, 0}};
    if (mv <= 0) return 0;
    if (mv >= curve[0].mv) return 100;
    for (unsigned i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
        if (mv >= curve[i].mv) {
            int dv = curve[i - 1].mv - curve[i].mv;
            int dp = curve[i - 1].pct - curve[i].pct;
            return curve[i].pct + (mv - curve[i].mv) * dp / dv;
        }
    }
    return 0;
}

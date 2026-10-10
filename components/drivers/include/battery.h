// LiPo voltage through the Feather V2's built-in divider on GPIO35 (ADC1, safe with Wi-Fi).
#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t battery_init(void);
int battery_mv(void);              // averaged over 8 reads, -1 on error
int battery_percent(int mv);       // rough LiPo discharge curve, 0..100

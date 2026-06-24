#pragma once

#include "esp_err.h"

#define GAUGE_TEMP_OFFSET_F_DEFAULT  (-5)

esp_err_t gauge_temp_init(void);
esp_err_t gauge_temp_load(void);
esp_err_t gauge_temp_save(void);

int gauge_temp_get_offset_f(void);
void gauge_temp_set_offset_f(int offset_f);

int gauge_temp_c_to_f(float celsius);
float gauge_temp_c_to_f_float(float celsius);

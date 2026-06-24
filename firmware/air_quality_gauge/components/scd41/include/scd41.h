#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define SCD41_RETRY_MS  10000

typedef struct {
    int pin_sda;
    int pin_scl;
    int i2c_port;
    uint32_t clk_hz;
} scd41_config_t;

typedef struct {
    int co2_ppm;
    float temperature_c;
    float humidity_rh;
} scd41_reading_t;

esp_err_t scd41_init(const scd41_config_t *cfg);

bool scd41_is_ready(void);
bool scd41_is_missing(void);
bool scd41_is_manual(void);

esp_err_t scd41_rescan(void);
esp_err_t scd41_read(scd41_reading_t *out);

void scd41_get_last_reading(scd41_reading_t *out);

/* Manual ppm override when sensor absent (serial "ppm" command). */
void scd41_set_manual_ppm(int ppm);

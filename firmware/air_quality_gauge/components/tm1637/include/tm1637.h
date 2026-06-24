#pragma once

#include "esp_err.h"

typedef struct {
    int pin_clk;
    int pin_dio;
    int brightness; /* 0-7 */
} tm1637_config_t;

esp_err_t tm1637_init(const tm1637_config_t *cfg);
void tm1637_clear(void);

/* 0400 — always four digits, leading zeros. */
void tm1637_show_ppm(int ppm);

/* 72<F> — tens, ones, blank, F */
void tm1637_show_temp_f(int temp_f);

/* 45 H — tens, ones, blank, H */
void tm1637_show_humidity(int rh);

/*  ERR — sensor missing */
void tm1637_show_err(void);

void tm1637_show_raw(int value);

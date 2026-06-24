#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define CO2_GAUGE_PPM_MIN  400
#define CO2_GAUGE_PPM_MAX  2200

typedef struct {
    int pin_in1;
    int pin_in2;
    int pin_in3;
    int pin_in4;
    int step_delay_ms;
    int direction;
} co2_gauge_config_t;

esp_err_t co2_gauge_init(const co2_gauge_config_t *cfg);
esp_err_t co2_gauge_load_cal(void);
esp_err_t co2_gauge_save_cal(void);

void co2_gauge_set_target_ppm(int ppm);
void co2_gauge_sync_ppm(int ppm);
int co2_gauge_get_target_ppm(void);
int co2_gauge_get_display_ppm(void);
int co2_gauge_get_position(void);
int co2_gauge_get_step_min(void);
int co2_gauge_get_step_max(void);

void co2_gauge_show_cal(void);
void co2_gauge_print_cal(void);

bool co2_gauge_update_step(void);
int co2_gauge_get_step_delay_ms(void);
void co2_gauge_goto_ppm_now(int ppm);

/* Serial calibration — pauses auto needle motion while running. */
void co2_gauge_jog(int steps);
void co2_gauge_reverse(void);
void co2_gauge_set_speed(int ms);
void co2_gauge_zero_position(void);
void co2_gauge_cal_set_min(void);
void co2_gauge_cal_set_max(void);
void co2_gauge_home(void);
void co2_gauge_sweep(void);
void co2_gauge_test(void);
void co2_gauge_pins_test(void);

void co2_gauge_pause_auto(void);
void co2_gauge_resume_auto(void);

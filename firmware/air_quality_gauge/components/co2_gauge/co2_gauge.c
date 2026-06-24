#include "co2_gauge.h"
#include "gauge_cal_defaults.h"

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "co2_gauge";

#define PPM_SPAN       (CO2_GAUGE_PPM_MAX - CO2_GAUGE_PPM_MIN)

#define NVS_NS   "gauge"
#define NVS_MIN  "step_min"
#define NVS_MAX  "step_max"
#define NVS_POS  "step_pos"

static const uint8_t s_seq[4][4] = {
    { 1, 0, 0, 0 },
    { 0, 1, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, 0, 1 },
};

static gpio_num_t s_pins[4];
static int s_seq_idx;
static int s_direction = 1;
static int s_step_delay_ms = 12;
static int s_position;
static int s_step_min;
static int s_step_max;
static int s_target_ppm = CO2_GAUGE_PPM_MIN;
static int s_display_ppm = CO2_GAUGE_PPM_MIN;
static bool s_auto_motion = true;

static void step_delay(void)
{
    TickType_t ticks = pdMS_TO_TICKS(s_step_delay_ms);
    if (ticks < 1) {
        ticks = 1;
    }
    vTaskDelay(ticks);
}

static void pulse_step(int dir)
{
    s_seq_idx = (s_seq_idx + dir + 4) % 4;
    for (int p = 0; p < 4; p++) {
        gpio_set_level(s_pins[p], s_seq[s_seq_idx][p]);
    }
}

static void step_once(int dir)
{
    pulse_step(dir);
    step_delay();
}

static void step_n(int steps)
{
    if (steps == 0) {
        return;
    }
    int n = steps < 0 ? -steps : steps;
    int dir = steps < 0 ? -s_direction : s_direction;
    for (int i = 0; i < n; i++) {
        step_once(dir);
    }
    s_position += steps;
}

static int ppm_to_steps(int ppm)
{
    if (ppm <= CO2_GAUGE_PPM_MIN) {
        return s_step_min;
    }
    if (ppm >= CO2_GAUGE_PPM_MAX) {
        return s_step_max;
    }
    int span = s_step_max - s_step_min;
    return s_step_min + (int)((int64_t)(ppm - CO2_GAUGE_PPM_MIN) * span / PPM_SPAN);
}

static void move_toward(int target_steps)
{
    while (s_position != target_steps) {
        int delta = target_steps - s_position;
        int dir = delta > 0 ? s_direction : -s_direction;
        step_once(dir);
        s_position += (delta > 0) ? 1 : -1;
    }
}

esp_err_t co2_gauge_init(const co2_gauge_config_t *cfg)
{
    s_pins[0] = (gpio_num_t)cfg->pin_in1;
    s_pins[1] = (gpio_num_t)cfg->pin_in2;
    s_pins[2] = (gpio_num_t)cfg->pin_in3;
    s_pins[3] = (gpio_num_t)cfg->pin_in4;
    s_direction = cfg->direction != 0 ? cfg->direction : 1;
    s_step_delay_ms = cfg->step_delay_ms >= 10 ? cfg->step_delay_ms : 12;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << s_pins[0]) | (1ULL << s_pins[1]) |
                        (1ULL << s_pins[2]) | (1ULL << s_pins[3]),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    for (int i = 0; i < 4; i++) {
        gpio_set_drive_capability(s_pins[i], GPIO_DRIVE_CAP_3);
        gpio_set_level(s_pins[i], 0);
    }
    s_seq_idx = 0;
    s_step_min = GAUGE_CAL_STEP_MIN_DEFAULT;
    s_step_max = GAUGE_CAL_STEP_MAX_DEFAULT;
    s_position = 0;
    return ESP_OK;
}

static void apply_cal_defaults(void)
{
    s_step_min = GAUGE_CAL_STEP_MIN_DEFAULT;
    s_step_max = GAUGE_CAL_STEP_MAX_DEFAULT;
}

esp_err_t co2_gauge_load_cal(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        apply_cal_defaults();
        ESP_LOGW(TAG, "no NVS cal — using baked-in min=%d max=%d",
                 s_step_min, s_step_max);
        return err;
    }
    bool got_min = false;
    bool got_max = false;
    int32_t vmin = GAUGE_CAL_STEP_MIN_DEFAULT;
    int32_t vmax = GAUGE_CAL_STEP_MAX_DEFAULT;
    if (nvs_get_i32(h, NVS_MIN, &vmin) == ESP_OK) {
        s_step_min = vmin;
        got_min = true;
    }
    if (nvs_get_i32(h, NVS_MAX, &vmax) == ESP_OK) {
        s_step_max = vmax;
        got_max = true;
    }
    if (!got_min || !got_max) {
        if (!got_min) {
            s_step_min = GAUGE_CAL_STEP_MIN_DEFAULT;
        }
        if (!got_max) {
            s_step_max = GAUGE_CAL_STEP_MAX_DEFAULT;
        }
        ESP_LOGW(TAG, "partial NVS cal — filled gaps from defaults");
    }
    int32_t pos = 0;
    if (nvs_get_i32(h, NVS_POS, &pos) == ESP_OK) {
        s_position = pos;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "cal loaded: %d ppm -> %d | %d ppm -> %d  pos=%d",
             CO2_GAUGE_PPM_MIN, s_step_min, CO2_GAUGE_PPM_MAX, s_step_max,
             s_position);
    return ESP_OK;
}

static void save_position(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_i32(h, NVS_POS, s_position) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

esp_err_t co2_gauge_save_cal(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i32(h, NVS_MIN, s_step_min);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, NVS_MAX, s_step_max);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(h, NVS_POS, s_position);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

void co2_gauge_show_cal(void)
{
    ESP_LOGI(TAG, "position=%d  cal: %d ppm -> %d | %d ppm -> %d  span=%d",
             s_position, CO2_GAUGE_PPM_MIN, s_step_min,
             CO2_GAUGE_PPM_MAX, s_step_max, s_step_max - s_step_min);
}

void co2_gauge_print_cal(void)
{
    printf("position=%d  cal: %d ppm -> step %d | %d ppm -> step %d\r\n",
           s_position, CO2_GAUGE_PPM_MIN, s_step_min,
           CO2_GAUGE_PPM_MAX, s_step_max);
    printf("arc span=%d steps\r\n", s_step_max - s_step_min);
}

int co2_gauge_get_step_min(void)
{
    return s_step_min;
}

int co2_gauge_get_step_max(void)
{
    return s_step_max;
}

void co2_gauge_set_target_ppm(int ppm)
{
    if (ppm < CO2_GAUGE_PPM_MIN) {
        ppm = CO2_GAUGE_PPM_MIN;
    }
    if (ppm > CO2_GAUGE_PPM_MAX) {
        ppm = CO2_GAUGE_PPM_MAX;
    }
    s_target_ppm = ppm;
}

void co2_gauge_sync_ppm(int ppm)
{
    if (ppm < CO2_GAUGE_PPM_MIN) {
        ppm = CO2_GAUGE_PPM_MIN;
    }
    if (ppm > CO2_GAUGE_PPM_MAX) {
        ppm = CO2_GAUGE_PPM_MAX;
    }
    s_target_ppm = ppm;
    s_display_ppm = ppm;
}

int co2_gauge_get_target_ppm(void)
{
    return s_target_ppm;
}

int co2_gauge_get_display_ppm(void)
{
    return s_display_ppm;
}

int co2_gauge_get_position(void)
{
    return s_position;
}

void co2_gauge_goto_ppm_now(int ppm)
{
    bool was = s_auto_motion;
    s_auto_motion = false;
    int target = ppm_to_steps(ppm);
    move_toward(target);
    s_display_ppm = ppm;
    s_target_ppm = ppm;
    s_auto_motion = was;
}

bool co2_gauge_update_step(void)
{
    s_display_ppm += (s_target_ppm - s_display_ppm + 5) / 10;
    if (s_display_ppm < CO2_GAUGE_PPM_MIN) {
        s_display_ppm = CO2_GAUGE_PPM_MIN;
    }
    if (s_display_ppm > CO2_GAUGE_PPM_MAX) {
        s_display_ppm = CO2_GAUGE_PPM_MAX;
    }

    if (!s_auto_motion) {
        return false;
    }

    int target_steps = ppm_to_steps(s_display_ppm);
    if (s_position == target_steps) {
        return false;
    }
    int delta = target_steps - s_position;
    int dir = delta > 0 ? s_direction : -s_direction;
    pulse_step(dir);
    s_position += (delta > 0) ? 1 : -1;

    if (s_position == target_steps) {
        save_position();
    }
    return true;
}

int co2_gauge_get_step_delay_ms(void)
{
    return s_step_delay_ms;
}

void co2_gauge_jog(int steps)
{
    s_auto_motion = false;
    step_n(steps);
}

void co2_gauge_reverse(void)
{
    s_direction = -s_direction;
    printf("direction flipped (sign=%d)\r\n", s_direction);
}

void co2_gauge_set_speed(int ms)
{
    if (ms < 10) {
        ms = 10;
    }
    s_step_delay_ms = ms;
    printf("step delay %d ms\r\n", s_step_delay_ms);
}

void co2_gauge_zero_position(void)
{
    s_position = 0;
    printf("position counter = 0\r\n");
}

void co2_gauge_cal_set_min(void)
{
    s_step_min = s_position;
    printf("400 ppm = step %d\r\n", s_step_min);
}

void co2_gauge_cal_set_max(void)
{
    s_step_max = s_position;
    printf("2200 ppm = step %d\r\n", s_step_max);
}

static void sweep_fn(void)
{
    int saved = s_step_delay_ms;
    s_step_delay_ms = 12;
    move_toward(ppm_to_steps(CO2_GAUGE_PPM_MIN));
    s_display_ppm = CO2_GAUGE_PPM_MIN;
    s_target_ppm = CO2_GAUGE_PPM_MIN;
    vTaskDelay(pdMS_TO_TICKS(500));
    move_toward(ppm_to_steps(CO2_GAUGE_PPM_MAX));
    s_display_ppm = CO2_GAUGE_PPM_MAX;
    s_target_ppm = CO2_GAUGE_PPM_MAX;
    vTaskDelay(pdMS_TO_TICKS(1000));
    move_toward(ppm_to_steps(CO2_GAUGE_PPM_MIN));
    s_display_ppm = CO2_GAUGE_PPM_MIN;
    s_target_ppm = CO2_GAUGE_PPM_MIN;
    s_step_delay_ms = saved;
}

void co2_gauge_home(void)
{
    s_auto_motion = false;
    co2_gauge_goto_ppm_now(CO2_GAUGE_PPM_MIN);
}

void co2_gauge_sweep(void)
{
    s_auto_motion = false;
    sweep_fn();
}

void co2_gauge_test(void)
{
    s_auto_motion = false;
    printf("test: 64 steps, 30 ms/step\r\n");
    int saved = s_step_delay_ms;
    s_step_delay_ms = 30;
    step_n(64);
    s_step_delay_ms = saved;
    printf("test done, position=%d\r\n", s_position);
}

void co2_gauge_pins_test(void)
{
    s_auto_motion = false;
    printf("Each IN high for 1 s — watch ULN board LEDs\r\n");
    for (int i = 0; i < 4; i++) {
        for (int p = 0; p < 4; p++) {
            gpio_set_level(s_pins[p], 0);
        }
        gpio_set_level(s_pins[i], 1);
        printf("  IN%d GPIO %d ON\r\n", i + 1, (int)s_pins[i]);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    for (int p = 0; p < 4; p++) {
        gpio_set_level(s_pins[p], 0);
    }
    printf("pins done\r\n");
}

void co2_gauge_pause_auto(void)
{
    s_auto_motion = false;
}

void co2_gauge_resume_auto(void)
{
    s_auto_motion = true;
}

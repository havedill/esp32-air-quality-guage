/*
 * CO2 air quality gauge — stepper needle + TM1637 + SCD41.
 *
 * GPIO map:
 *   Stepper IN1-4 -> 32, 33, 26, 27
 *   TM1637 CLK/DIO -> 17, 18
 *   SCD41 SDA/SCL  -> 21, 22
 *
 * Display rotates: ppm (0400) / temp (72 F) / humidity (45 H).
 * Serial 115200: full calibration CLI (jog, cal, sweep, …) — type help.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "co2_gauge.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "gauge_cli.h"
#include "gauge_temp.h"
#include "nvs_flash.h"
#include "scd41.h"
#include "tm1637.h"

static const char *TAG = "gauge";

#define PIN_STEPPER_1  32
#define PIN_STEPPER_2  33
#define PIN_STEPPER_3  26
#define PIN_STEPPER_4  27
#define PIN_TM_CLK     17
#define PIN_TM_DIO     18
#define PIN_I2C_SDA    21
#define PIN_I2C_SCL    22

#define DISP_ROTATE_MS  4000
#define CO2_TARGET_DEADBAND_PPM  15

typedef enum {
    DISP_PPM = 0,
    DISP_TEMP,
    DISP_HUMID,
    DISP_COUNT,
} disp_mode_t;

static void gauge_task(void *arg)
{
    (void)arg;
    for (;;) {
        bool stepped = co2_gauge_update_step();
        int ms = stepped ? co2_gauge_get_step_delay_ms() : 100;
        TickType_t ticks = pdMS_TO_TICKS(ms);
        if (ticks < 1) {
            ticks = 1;
        }
        vTaskDelay(ticks);
    }
}

static void display_task(void *arg)
{
    (void)arg;
    disp_mode_t mode = DISP_PPM;
    TickType_t next_rotate = xTaskGetTickCount();

    for (;;) {
        if (scd41_is_missing()) {
            tm1637_show_err();
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        scd41_reading_t r;
        scd41_get_last_reading(&r);

        switch (mode) {
        case DISP_PPM:
            tm1637_show_ppm(r.co2_ppm);
            break;
        case DISP_TEMP:
            tm1637_show_temp_f(gauge_temp_c_to_f(r.temperature_c));
            break;
        case DISP_HUMID:
            tm1637_show_humidity((int)(r.humidity_rh + 0.5f));
            break;
        default:
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(250));

        if ((int32_t)(xTaskGetTickCount() - next_rotate) >= 0) {
            mode = (disp_mode_t)((mode + 1) % DISP_COUNT);
            next_rotate = xTaskGetTickCount() + pdMS_TO_TICKS(DISP_ROTATE_MS);
        }
    }
}

static void sensor_task(void *arg)
{
    (void)arg;
    scd41_reading_t r;

    for (;;) {
        if (scd41_is_missing()) {
            if (gauge_log_is_enabled()) {
                ESP_LOGW(TAG, "SCD41 missing — retrying");
            }
            if (scd41_rescan() == ESP_OK && gauge_log_is_enabled()) {
                ESP_LOGI(TAG, "SCD41 connected");
            }
            vTaskDelay(pdMS_TO_TICKS(SCD41_RETRY_MS));
            continue;
        }

        if (scd41_read(&r) == ESP_OK) {
            int target_delta = r.co2_ppm - co2_gauge_get_target_ppm();
            if (target_delta >= CO2_TARGET_DEADBAND_PPM ||
                target_delta <= -CO2_TARGET_DEADBAND_PPM) {
                co2_gauge_set_target_ppm(r.co2_ppm);
            }
            if (gauge_log_is_enabled()) {
                ESP_LOGI(TAG, "co2=%d ppm  T=%.1f F  RH=%.0f%%  needle=%d",
                         r.co2_ppm, gauge_temp_c_to_f_float(r.temperature_c),
                         r.humidity_rh, co2_gauge_get_display_ppm());
            }
        } else if (gauge_log_is_enabled()) {
            ESP_LOGW(TAG, "SCD41 read failed — will retry");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static bool read_line(char *buf, size_t sz)
{
    size_t len = 0;
    while (len + 1 < sz) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (len == 0) {
                continue;
            }
            buf[len] = '\0';
            fwrite("\r\n", 2, 1, stdout);
            fflush(stdout);
            return true;
        }
        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                fwrite("\b \b", 3, 1, stdout);
                fflush(stdout);
            }
            continue;
        }
        buf[len++] = c;
        fwrite(&c, 1, 1, stdout);
        fflush(stdout);
    }
    buf[len] = '\0';
    return true;
}

static void serial_task(void *arg)
{
    (void)arg;
    char line[96];

    vTaskDelay(pdMS_TO_TICKS(800));
    printf("\r\n=== air_quality_gauge (115200) ===\r\n");
    printf("Type 'help' — use 'log off' for a quiet prompt\r\n");
    gauge_cli_print_help();
    co2_gauge_print_cal();

    for (;;) {
        fputs("> ", stdout);
        fflush(stdout);
        if (!read_line(line, sizeof(line))) {
            continue;
        }
        gauge_cli_handle_line(line);
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    ESP_ERROR_CHECK(gauge_temp_init());

    co2_gauge_config_t gauge_cfg = {
        .pin_in1 = PIN_STEPPER_1,
        .pin_in2 = PIN_STEPPER_2,
        .pin_in3 = PIN_STEPPER_3,
        .pin_in4 = PIN_STEPPER_4,
        .step_delay_ms = 12,
        .direction = 1,
    };
    ESP_ERROR_CHECK(co2_gauge_init(&gauge_cfg));
    co2_gauge_load_cal();
    co2_gauge_show_cal();

    tm1637_config_t tm_cfg = {
        .pin_clk = PIN_TM_CLK,
        .pin_dio = PIN_TM_DIO,
        .brightness = 4,
    };
    ESP_ERROR_CHECK(tm1637_init(&tm_cfg));
    tm1637_show_raw(8888);
    vTaskDelay(pdMS_TO_TICKS(800));
    tm1637_clear();

    scd41_config_t scd_cfg = {
        .pin_sda = PIN_I2C_SDA,
        .pin_scl = PIN_I2C_SCL,
        .i2c_port = 0,
        .clk_hz = 100000,
    };
    ESP_ERROR_CHECK(scd41_init(&scd_cfg));

    if (scd41_is_ready()) {
        scd41_reading_t boot;
        if (scd41_read(&boot) == ESP_OK) {
            co2_gauge_sync_ppm(boot.co2_ppm);
            ESP_LOGI(TAG, "boot reading %d ppm", boot.co2_ppm);
        }
    } else {
        ESP_LOGW(TAG, "SCD41 not ready at boot — display shows ERR until found");
    }

    ESP_LOGI(TAG, "running — display rotates ppm / temp / humidity every %d s",
             DISP_ROTATE_MS / 1000);

    xTaskCreate(gauge_task, "gauge", 4096, NULL, 5, NULL);
    xTaskCreate(display_task, "display", 3072, NULL, 4, NULL);
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 4, NULL);
    xTaskCreate(serial_task, "serial", 6144, NULL, 3, NULL);
}

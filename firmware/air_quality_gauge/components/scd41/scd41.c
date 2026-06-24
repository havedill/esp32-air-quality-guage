#include "scd41.h"

#include <string.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "scd41";

#define SCD41_ADDR 0x62

#define CMD_START_PERIODIC  0x21B1
#define CMD_READ_MEAS       0xEC05
#define CMD_STOP_PERIODIC   0x3F86
#define CMD_GET_SERIAL      0x3682

static int s_port = I2C_NUM_0;
static bool s_hw_ok;
static bool s_started;
static bool s_manual;
static int s_manual_ppm = 800;
static float s_manual_temp_c = 22.0f;
static float s_manual_rh = 45.0f;
static scd41_reading_t s_last;

static uint8_t crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static esp_err_t write_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return i2c_master_write_to_device(s_port, SCD41_ADDR, buf, sizeof(buf),
                                      pdMS_TO_TICKS(100));
}

static esp_err_t read_bytes(uint8_t *buf, size_t len)
{
    return i2c_master_read_from_device(s_port, SCD41_ADDR, buf, len,
                                       pdMS_TO_TICKS(100));
}

static esp_err_t probe_sensor(void)
{
    esp_err_t err = write_cmd(CMD_STOP_PERIODIC);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    err = write_cmd(CMD_GET_SERIAL);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t raw[9];
    err = read_bytes(raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < 3; i++) {
        if (crc8(&raw[i * 3], 2) != raw[i * 3 + 2]) {
            return ESP_ERR_INVALID_CRC;
        }
    }
    return ESP_OK;
}

static esp_err_t start_periodic(void)
{
    if (probe_sensor() != ESP_OK) {
        return ESP_FAIL;
    }
    if (write_cmd(CMD_START_PERIODIC) != ESP_OK) {
        return ESP_FAIL;
    }
    s_hw_ok = true;
    s_started = true;
    s_manual = false;
    ESP_LOGI(TAG, "SCD41 started (5 s periodic)");
    vTaskDelay(pdMS_TO_TICKS(5000));
    return ESP_OK;
}

esp_err_t scd41_init(const scd41_config_t *cfg)
{
    s_port = cfg->i2c_port;
    s_hw_ok = false;
    s_started = false;
    s_manual = false;
    memset(&s_last, 0, sizeof(s_last));

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = (gpio_num_t)cfg->pin_sda,
        .scl_io_num = (gpio_num_t)cfg->pin_scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = cfg->clk_hz > 0 ? cfg->clk_hz : 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(s_port, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(s_port, I2C_MODE_MASTER, 0, 0, 0));

    if (start_periodic() != ESP_OK) {
        s_hw_ok = false;
        s_started = false;
        ESP_LOGW(TAG, "SCD41 not found — retry every %d s", SCD41_RETRY_MS / 1000);
        ESP_LOGW(TAG, "VCC=3V3 GND SDA=21 SCL=22");
        return ESP_OK;
    }
    return ESP_OK;
}

bool scd41_is_ready(void)
{
    return s_hw_ok && s_started;
}

bool scd41_is_manual(void)
{
    return s_manual;
}

bool scd41_is_missing(void)
{
    return !scd41_is_ready() && !s_manual;
}

esp_err_t scd41_rescan(void)
{
    if (scd41_is_ready()) {
        return ESP_OK;
    }
    if (start_periodic() != ESP_OK) {
        s_hw_ok = false;
        s_started = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void scd41_set_manual_ppm(int ppm)
{
    if (ppm < 400) {
        ppm = 400;
    }
    if (ppm > 5000) {
        ppm = 5000;
    }
    s_manual = true;
    s_manual_ppm = ppm;
}

void scd41_get_last_reading(scd41_reading_t *out)
{
    if (out != NULL) {
        *out = s_last;
    }
}

esp_err_t scd41_read(scd41_reading_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_manual) {
        out->co2_ppm = s_manual_ppm;
        out->temperature_c = s_manual_temp_c;
        out->humidity_rh = s_manual_rh;
        s_last = *out;
        return ESP_OK;
    }

    if (!s_hw_ok) {
        return ESP_ERR_NOT_FOUND;
    }

    if (write_cmd(CMD_READ_MEAS) != ESP_OK) {
        s_hw_ok = false;
        s_started = false;
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(30));

    uint8_t raw[9];
    if (read_bytes(raw, sizeof(raw)) != ESP_OK) {
        s_hw_ok = false;
        s_started = false;
        return ESP_FAIL;
    }
    for (int i = 0; i < 3; i++) {
        if (crc8(&raw[i * 3], 2) != raw[i * 3 + 2]) {
            ESP_LOGW(TAG, "CRC error on word %d", i);
            s_hw_ok = false;
            s_started = false;
            return ESP_ERR_INVALID_CRC;
        }
    }

    uint16_t co2 = ((uint16_t)raw[0] << 8) | raw[1];
    uint16_t t_raw = ((uint16_t)raw[3] << 8) | raw[4];
    uint16_t rh_raw = ((uint16_t)raw[6] << 8) | raw[7];

    out->co2_ppm = (int)co2;
    out->temperature_c = -45.0f + 175.0f * ((float)t_raw / 65535.0f);
    out->humidity_rh = 100.0f * ((float)rh_raw / 65535.0f);
    s_last = *out;
    return ESP_OK;
}

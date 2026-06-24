#include "gauge_temp.h"

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "gauge_temp";

#define NVS_NS       "gauge"
#define NVS_TEMP_OFF "temp_off_f"

static int s_offset_f = GAUGE_TEMP_OFFSET_F_DEFAULT;

esp_err_t gauge_temp_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    int32_t v;
    err = nvs_get_i32(h, NVS_TEMP_OFF, &v);
    nvs_close(h);
    if (err == ESP_OK) {
        s_offset_f = (int)v;
    }
    return err;
}

esp_err_t gauge_temp_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i32(h, NVS_TEMP_OFF, s_offset_f);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t gauge_temp_init(void)
{
    if (gauge_temp_load() != ESP_OK) {
        s_offset_f = GAUGE_TEMP_OFFSET_F_DEFAULT;
        ESP_LOGI(TAG, "temp offset %d F (default)", s_offset_f);
    } else {
        ESP_LOGI(TAG, "temp offset %d F (NVS)", s_offset_f);
    }
    return ESP_OK;
}

int gauge_temp_get_offset_f(void)
{
    return s_offset_f;
}

void gauge_temp_set_offset_f(int offset_f)
{
    s_offset_f = offset_f;
}

int gauge_temp_c_to_f(float celsius)
{
    return (int)(gauge_temp_c_to_f_float(celsius) + 0.5f);
}

float gauge_temp_c_to_f_float(float celsius)
{
    return celsius * 9.0f / 5.0f + 32.0f + (float)s_offset_f;
}

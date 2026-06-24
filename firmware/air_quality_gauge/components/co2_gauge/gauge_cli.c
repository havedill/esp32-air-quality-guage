#include "gauge_cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "co2_gauge.h"
#include "gauge_temp.h"
#include "esp_log.h"
#include "scd41.h"

static const char *TAG = "gauge_cli";

static bool s_log_enabled = true;

bool gauge_log_is_enabled(void)
{
    return s_log_enabled;
}

void gauge_log_set(bool enabled)
{
    s_log_enabled = enabled;
}

static void trim(char *s)
{
    char *p = s;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}

static int parse_jog_steps(const char *line)
{
    const char *p = line + 3;
    while (*p == ' ') {
        p++;
    }
    if (*p == '\0') {
        return 0;
    }
    return (int)strtol(p, NULL, 10);
}

void gauge_cli_print_help(void)
{
    printf("\r\nCommands:\r\n");
    printf("  help              command list\r\n");
    printf("  pos               needle position + cal\r\n");
    printf("  jog <steps>       relative move (+/-)\r\n");
    printf("  goto <ppm>        move to ppm (%d-%d)\r\n",
           CO2_GAUGE_PPM_MIN, CO2_GAUGE_PPM_MAX);
    printf("  cal min           set 400 ppm at current position\r\n");
    printf("  cal max           set 2200 ppm at current position\r\n");
    printf("  cal show | save | load\r\n");
    printf("  home              goto %d ppm\r\n", CO2_GAUGE_PPM_MIN);
    printf("  sweep             slow %d -> %d -> %d\r\n",
           CO2_GAUGE_PPM_MIN, CO2_GAUGE_PPM_MAX, CO2_GAUGE_PPM_MIN);
    printf("  reverse           flip motor direction\r\n");
    printf("  speed <ms>        step delay (min 10 ms)\r\n");
    printf("  zero              set position counter to 0\r\n");
    printf("  test / pins       hardware check\r\n");
    printf("  ppm <n>           bench override when sensor missing\r\n");
    printf("  sensor            show SCD41 connection status\r\n");
    printf("  sensor rescan     retry I2C after rewiring\r\n");
    printf("  log on|off        sensor readings on serial (now %s)\r\n",
           s_log_enabled ? "on" : "off");
    printf("  temp show         temperature offset (F)\r\n");
    printf("  temp offset <F>   adjust display/log temp (default %d F)\r\n",
           GAUGE_TEMP_OFFSET_F_DEFAULT);
    printf("  temp save | load  persist offset to NVS\r\n");
    printf("  follow on|off     resume/pause auto needle tracking\r\n\r\n");
}

void gauge_cli_handle_line(char *line)
{
    trim(line);
    if (line[0] == '\0') {
        return;
    }

    ESP_LOGI(TAG, "cmd: %s", line);

    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        gauge_cli_print_help();
        return;
    }
    if (strcmp(line, "test") == 0) {
        co2_gauge_test();
        return;
    }
    if (strcmp(line, "pins") == 0) {
        co2_gauge_pins_test();
        return;
    }
    if (strcmp(line, "pos") == 0) {
        co2_gauge_print_cal();
        printf("display=%d target=%d\r\n",
               co2_gauge_get_display_ppm(), co2_gauge_get_target_ppm());
        return;
    }
    if (strcmp(line, "reverse") == 0) {
        co2_gauge_reverse();
        return;
    }
    if (strcmp(line, "zero") == 0) {
        co2_gauge_zero_position();
        return;
    }
    if (strcmp(line, "home") == 0) {
        co2_gauge_home();
        return;
    }
    if (strcmp(line, "sweep") == 0) {
        co2_gauge_sweep();
        return;
    }
    if (strncmp(line, "jog", 3) == 0) {
        int steps = parse_jog_steps(line);
        if (steps == 0) {
            printf("usage: jog <steps>\r\n");
            return;
        }
        printf("jog %d ...\r\n", steps);
        co2_gauge_jog(steps);
        printf("position=%d\r\n", co2_gauge_get_position());
        return;
    }
    if (strncmp(line, "goto ", 5) == 0) {
        int ppm = (int)strtol(line + 5, NULL, 10);
        co2_gauge_goto_ppm_now(ppm);
        printf("position=%d\r\n", co2_gauge_get_position());
        return;
    }
    if (strncmp(line, "speed ", 6) == 0) {
        co2_gauge_set_speed((int)strtol(line + 6, NULL, 10));
        return;
    }
    if (strcmp(line, "cal min") == 0) {
        co2_gauge_cal_set_min();
        return;
    }
    if (strcmp(line, "cal max") == 0) {
        co2_gauge_cal_set_max();
        return;
    }
    if (strcmp(line, "cal show") == 0) {
        co2_gauge_print_cal();
        return;
    }
    if (strcmp(line, "cal save") == 0) {
        if (co2_gauge_save_cal() == ESP_OK) {
            printf("calibration saved to NVS\r\n");
            co2_gauge_resume_auto();
            printf("auto needle tracking on\r\n");
        } else {
            printf("save failed\r\n");
        }
        return;
    }
    if (strcmp(line, "cal load") == 0) {
        if (co2_gauge_load_cal() == ESP_OK) {
            printf("loaded from NVS\r\n");
            co2_gauge_print_cal();
        } else {
            printf("load failed\r\n");
        }
        return;
    }
    if (strncmp(line, "ppm ", 4) == 0) {
        int ppm = (int)strtol(line + 4, NULL, 10);
        if (scd41_is_ready()) {
            co2_gauge_set_target_ppm(ppm);
            printf("needle target %d ppm (sensor still live)\r\n", ppm);
        } else {
            scd41_set_manual_ppm(ppm);
            co2_gauge_set_target_ppm(ppm);
            printf("manual %d ppm (sensor missing)\r\n", ppm);
        }
        return;
    }
    if (strcmp(line, "sensor") == 0 || strcmp(line, "sensor status") == 0) {
        scd41_reading_t r;
        scd41_get_last_reading(&r);
        if (scd41_is_ready()) {
            printf("SCD41: connected\r\n");
            printf("  last: %d ppm  %.1f F  %.0f%%\r\n",
                   r.co2_ppm, gauge_temp_c_to_f_float(r.temperature_c),
                   r.humidity_rh);
        } else if (scd41_is_manual()) {
            printf("SCD41: missing (manual ppm %d)\r\n", r.co2_ppm);
        } else {
            printf("SCD41: missing — display shows ERR, retry every %d s\r\n",
                   SCD41_RETRY_MS / 1000);
            printf("  wire VCC=3V3 GND SDA=21 SCL=22\r\n");
        }
        return;
    }
    if (strcmp(line, "sensor rescan") == 0) {
        if (scd41_rescan() == ESP_OK) {
            printf("SCD41 found — live readings in ~5 s\r\n");
        } else {
            printf("SCD41 not found — check wiring\r\n");
        }
        return;
    }
    if (strcmp(line, "follow on") == 0) {
        co2_gauge_resume_auto();
        printf("auto needle tracking on\r\n");
        return;
    }
    if (strcmp(line, "follow off") == 0) {
        co2_gauge_pause_auto();
        printf("auto needle tracking off (cal mode)\r\n");
        return;
    }
    if (strcmp(line, "log") == 0 || strcmp(line, "log status") == 0) {
        printf("sensor logging %s\r\n", s_log_enabled ? "on" : "off");
        return;
    }
    if (strcmp(line, "log on") == 0) {
        gauge_log_set(true);
        printf("sensor logging on\r\n");
        return;
    }
    if (strcmp(line, "log off") == 0) {
        gauge_log_set(false);
        printf("sensor logging off\r\n");
        return;
    }
    if (strcmp(line, "temp show") == 0) {
        printf("temp offset %d F\r\n", gauge_temp_get_offset_f());
        return;
    }
    if (strncmp(line, "temp offset ", 12) == 0) {
        int off = (int)strtol(line + 12, NULL, 10);
        gauge_temp_set_offset_f(off);
        printf("temp offset %d F (temp save to persist)\r\n", off);
        return;
    }
    if (strcmp(line, "temp save") == 0) {
        if (gauge_temp_save() == ESP_OK) {
            printf("temp offset saved (%d F)\r\n", gauge_temp_get_offset_f());
        } else {
            printf("temp save failed\r\n");
        }
        return;
    }
    if (strcmp(line, "temp load") == 0) {
        if (gauge_temp_load() == ESP_OK) {
            printf("temp offset loaded (%d F)\r\n", gauge_temp_get_offset_f());
        } else {
            printf("no NVS offset — using %d F default\r\n",
                   GAUGE_TEMP_OFFSET_F_DEFAULT);
            gauge_temp_set_offset_f(GAUGE_TEMP_OFFSET_F_DEFAULT);
        }
        return;
    }

    printf("unknown command (try help)\r\n");
}

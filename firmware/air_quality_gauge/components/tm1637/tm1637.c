#include "tm1637.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"

#define TM1637_CMD_DATA    0x40
#define TM1637_CMD_DISPLAY 0x80
#define TM1637_CMD_ADDR    0xC0

#define SEG_BLANK   0x00
#define SEG_E       0x79
#define SEG_R       0x50
#define SEG_F       0x71
#define SEG_H       0x76

static gpio_num_t s_clk;
static gpio_num_t s_dio;
static int s_brightness = 7;

static const uint8_t s_digits[10] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
};

static void delay_us(void)
{
    esp_rom_delay_us(5);
}

static void dio_out(int level)
{
    gpio_set_direction(s_dio, GPIO_MODE_OUTPUT);
    gpio_set_level(s_dio, level);
}

static void start(void)
{
    dio_out(1);
    gpio_set_level(s_clk, 1);
    delay_us();
    dio_out(0);
    delay_us();
    gpio_set_level(s_clk, 0);
    delay_us();
}

static void stop(void)
{
    dio_out(0);
    delay_us();
    gpio_set_level(s_clk, 1);
    delay_us();
    dio_out(1);
    delay_us();
}

static void write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        gpio_set_level(s_clk, 0);
        delay_us();
        dio_out((data >> i) & 1);
        delay_us();
        gpio_set_level(s_clk, 1);
        delay_us();
    }
    gpio_set_level(s_clk, 0);
    delay_us();
    dio_out(1);
    gpio_set_level(s_clk, 1);
    delay_us();
    gpio_set_level(s_clk, 0);
    delay_us();
}

static void write_cmd(uint8_t cmd)
{
    start();
    write_byte(cmd);
    stop();
}

static void show_segments(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
    write_cmd(TM1637_CMD_DATA);
    start();
    write_byte(TM1637_CMD_ADDR);
    write_byte(d0);
    write_byte(d1);
    write_byte(d2);
    write_byte(d3);
    stop();
}

esp_err_t tm1637_init(const tm1637_config_t *cfg)
{
    s_clk = (gpio_num_t)cfg->pin_clk;
    s_dio = (gpio_num_t)cfg->pin_dio;
    s_brightness = cfg->brightness;
    if (s_brightness < 0) {
        s_brightness = 0;
    }
    if (s_brightness > 7) {
        s_brightness = 7;
    }

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << s_clk) | (1ULL << s_dio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(s_clk, 1);
    gpio_set_level(s_dio, 1);

    write_cmd(TM1637_CMD_DISPLAY | 0x08 | (uint8_t)s_brightness);
    tm1637_clear();
    return ESP_OK;
}

void tm1637_clear(void)
{
    show_segments(0, 0, 0, 0);
}

void tm1637_show_ppm(int ppm)
{
    if (ppm < 0) {
        ppm = 0;
    }
    if (ppm > 9999) {
        ppm = 9999;
    }
    show_segments(
        s_digits[(ppm / 1000) % 10],
        s_digits[(ppm / 100) % 10],
        s_digits[(ppm / 10) % 10],
        s_digits[ppm % 10]);
}

void tm1637_show_temp_f(int temp_f)
{
    if (temp_f < 0) {
        temp_f = 0;
    }
    if (temp_f > 199) {
        temp_f = 199;
    }
    show_segments(
        s_digits[(temp_f / 10) % 10],
        s_digits[temp_f % 10],
        SEG_BLANK,
        SEG_F);
}

void tm1637_show_humidity(int rh)
{
    if (rh < 0) {
        rh = 0;
    }
    if (rh > 99) {
        rh = 99;
    }
    show_segments(
        s_digits[(rh / 10) % 10],
        s_digits[rh % 10],
        SEG_BLANK,
        SEG_H);
}

void tm1637_show_err(void)
{
    show_segments(SEG_BLANK, SEG_E, SEG_R, SEG_R);
}

void tm1637_show_raw(int value)
{
    tm1637_show_ppm(value);
}

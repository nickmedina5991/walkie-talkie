#include "oled.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "font5x7.h"

static const char *TAG = "oled";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_present;
// 1 control byte + 8 pages x 128 columns. Byte = 8 vertical pixels, bit 0 on top.
static uint8_t s_buf[1 + OLED_W * OLED_H / 8];
static uint8_t *const s_fb = s_buf + 1;

static esp_err_t send_cmds(const uint8_t *cmds, size_t n)
{
    uint8_t tmp[32];
    if (n + 1 > sizeof(tmp)) return ESP_ERR_INVALID_SIZE;
    tmp[0] = 0x00;  // control byte: command stream
    memcpy(tmp + 1, cmds, n);
    return i2c_master_transmit(s_dev, tmp, n + 1, 50);
}

esp_err_t oled_init(uint8_t addr)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,  // most OLED boards also have their own pull-ups
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) return err;

    err = i2c_master_probe(s_bus, addr, 50);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no OLED answered at 0x%02X (try 0x3C or 0x3D, check SDA/SCL and power)", addr);
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) return err;

    static const uint8_t init_seq[] = {
        0xAE,        // display off
        0xD5, 0x80,  // clock divide
        0xA8, 0x3F,  // multiplex 64
        0xD3, 0x00,  // display offset 0
        0x40,        // start line 0
        0x8D, 0x14,  // charge pump on (internal VCC)
        0x20, 0x00,  // horizontal addressing mode
        0xA1,        // segment remap (column 127 = SEG0)
        0xC8,        // COM scan descending
        0xDA, 0x12,  // COM pins for 128x64
        0x81, 0xCF,  // contrast
        0xD9, 0xF1,  // pre-charge
        0xDB, 0x40,  // VCOMH deselect
        0xA4,        // resume to RAM content
        0xA6,        // normal (not inverted)
        0xAF,        // display on
    };
    err = send_cmds(init_seq, sizeof(init_seq));
    if (err != ESP_OK) return err;
    s_present = true;
    oled_clear();
    oled_flush();
    ESP_LOGI(TAG, "SSD1306 ready at 0x%02X", addr);
    return ESP_OK;
}

bool oled_present(void)
{
    return s_present;
}

void oled_clear(void)
{
    memset(s_fb, 0, OLED_W * OLED_H / 8);
}

void oled_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    uint8_t *b = &s_fb[(y >> 3) * OLED_W + x];
    if (on)
        *b |= (uint8_t)(1 << (y & 7));
    else
        *b &= (uint8_t)~(1 << (y & 7));
}

void oled_fill_rect(int x, int y, int w, int h, bool on)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) oled_pixel(i, j, on);
}

void oled_rect(int x, int y, int w, int h)
{
    for (int i = x; i < x + w; i++) {
        oled_pixel(i, y, true);
        oled_pixel(i, y + h - 1, true);
    }
    for (int j = y; j < y + h; j++) {
        oled_pixel(x, j, true);
        oled_pixel(x + w - 1, j, true);
    }
}

void oled_invert_rect(int x, int y, int w, int h)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (i < 0 || i >= OLED_W || j < 0 || j >= OLED_H) continue;
            s_fb[(j >> 3) * OLED_W + i] ^= (uint8_t)(1 << (j & 7));
        }
    }
}

static void draw_char(int x, int y, char c, int scale)
{
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    const uint8_t *g = font5x7[c - FONT_FIRST];
    for (int col = 0; col < FONT_W; col++) {
        for (int row = 0; row < FONT_H; row++) {
            if (g[col] & (1 << row)) {
                if (scale == 1)
                    oled_pixel(x + col, y + row, true);
                else
                    oled_fill_rect(x + col * scale, y + row * scale, scale, scale, true);
            }
        }
    }
}

int oled_text(int x, int y, const char *s, int scale)
{
    if (scale < 1) scale = 1;
    for (; *s; s++) {
        draw_char(x, y, *s, scale);
        x += (FONT_W + 1) * scale;
    }
    return x;
}

int oled_printf(int x, int y, int scale, const char *fmt, ...)
{
    char line[48];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    return oled_text(x, y, line, scale);
}

void oled_bar(int x, int y, int w, int h, int value, int max)
{
    if (max <= 0) return;
    if (value < 0) value = 0;
    if (value > max) value = max;
    oled_rect(x, y, w, h);
    int fill = (w - 4) * value / max;
    oled_fill_rect(x + 2, y + 2, fill, h - 4, true);
}

esp_err_t oled_flush(void)
{
    if (!s_present) return ESP_ERR_INVALID_STATE;
    static const uint8_t window[] = {0x21, 0, OLED_W - 1, 0x22, 0, 7};  // full column and page range
    esp_err_t err = send_cmds(window, sizeof(window));
    if (err != ESP_OK) return err;
    s_buf[0] = 0x40;  // control byte: data stream
    return i2c_master_transmit(s_dev, s_buf, sizeof(s_buf), 100);
}

void oled_set_power(bool on)
{
    if (!s_present) return;
    uint8_t c = on ? 0xAF : 0xAE;
    send_cmds(&c, 1);
}

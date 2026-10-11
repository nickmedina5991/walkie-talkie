// SSD1306 128x64 OLED over I2C with a RAM framebuffer.
// Draw into the framebuffer, then call oled_flush() to push it (about 25 ms at 400 kHz).
// Only one task should draw (the display task in walkie mode).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define OLED_W 128
#define OLED_H 64

// Two-color modules have a yellow band on rows 0-15 and blue on rows 16-63, with a
// small physical gap between them. Every screen keeps its title inside the top band
// and its content at or below OLED_BODY_Y so nothing straddles the color split.
#define OLED_TOP_H   16
#define OLED_TITLE_Y 4
#define OLED_BODY_Y  16

esp_err_t oled_init(uint8_t i2c_addr);
bool oled_present(void);
void oled_clear(void);
void oled_pixel(int x, int y, bool on);
void oled_fill_rect(int x, int y, int w, int h, bool on);
void oled_rect(int x, int y, int w, int h);
// Text with a 5x7 font. scale 1 = 6x8 pixel cells (21 chars per line), 2 = 12x16.
// Returns the x position after the last character.
int oled_text(int x, int y, const char *s, int scale);
int oled_printf(int x, int y, int scale, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
int oled_text_width(const char *s, int scale);
// Title in the top band: left text at the left edge, optional right text right aligned.
void oled_title(const char *left, const char *right);
// Horizontal bar, value 0..max.
void oled_bar(int x, int y, int w, int h, int value, int max);
void oled_invert_rect(int x, int y, int w, int h);
esp_err_t oled_flush(void);
void oled_set_power(bool on);

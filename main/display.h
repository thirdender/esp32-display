#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "generated/icons.h"

#define LCD_W 172
#define LCD_H 320

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

#define C_BG      rgb565(18, 22, 34)
#define C_HEADER  rgb565(24, 38, 62)
#define C_TEXT    rgb565(232, 238, 246)
#define C_DIM     rgb565(130, 146, 164)
#define C_ACCENT  rgb565(79, 195, 247)
#define C_LINE    rgb565(44, 60, 86)
#define C_HIGH    rgb565(255, 183, 77)
#define C_LOW     rgb565(100, 181, 246)
#define C_SUN     rgb565(255, 213, 79)
#define C_CLOUD   rgb565(176, 190, 197)
#define C_RAIN    rgb565(100, 181, 246)
#define C_SNOW    rgb565(225, 245, 254)
#define C_THUNDER rgb565(255, 241, 118)
#define C_FOG     rgb565(144, 164, 174)

void display_init(void);
void display_clear(uint16_t color);
void display_clear_bg(void);
void display_flush(void);
void display_set_backlight(uint8_t percent);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color);

int  display_text_width(const char *s, int scale);
void display_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale, bool fill_bg);
void display_text_center(int y, const char *s, uint16_t fg, int scale);
void display_big_text(int x, int y, const char *s, uint16_t fg);
void display_icon(int x, int y, const weather_icon_t *icon, int scale, uint16_t main_col, uint16_t detail_col);
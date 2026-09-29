#include "display.h"
#include "board_config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"

#include "generated/font_small.h"
#include "generated/font_big.h"
#include "generated/sky.h"

#define TAG "display"

static esp_lcd_panel_io_handle_t s_io = NULL;
static uint16_t *s_fb = NULL;

static inline uint16_t fix_color(uint16_t v);

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t chan = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PIN_LCD_BL,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan));
}

static void backlight_set(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (((1 << 13) - 1) * percent) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

void display_set_backlight(uint8_t percent)
{
    backlight_set(percent);
}

static void lcd_reset(void)
{
    gpio_config_t cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_LCD_RST,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
}

static void st7789_init(void)
{
    esp_lcd_panel_io_handle_t io = s_io;

    esp_lcd_panel_io_tx_param(io, 0x11, NULL, 0); /* sleep out */
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_lcd_panel_io_tx_param(io, 0x36, (uint8_t[]){0x40}, 1);  /* MADCTL portrait, MX (fix horizontal mirror) */
    esp_lcd_panel_io_tx_param(io, 0x3A, (uint8_t[]){0x55}, 1);  /* RGB565 */
    esp_lcd_panel_io_tx_param(io, 0xB0, (uint8_t[]){0x00, 0xE8}, 2); /* RAMCTRL, little endian */
    esp_lcd_panel_io_tx_param(io, 0xB2, (uint8_t[]){0x0C, 0x0C, 0x00, 0x33, 0x33}, 5);
    esp_lcd_panel_io_tx_param(io, 0xB7, (uint8_t[]){0x75}, 1);
    esp_lcd_panel_io_tx_param(io, 0xBB, (uint8_t[]){0x1A}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC0, (uint8_t[]){0x80}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC2, (uint8_t[]){0x01, 0xFF}, 2);
    esp_lcd_panel_io_tx_param(io, 0xC3, (uint8_t[]){0x13}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC4, (uint8_t[]){0x20}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC6, (uint8_t[]){0x0F}, 1);
    esp_lcd_panel_io_tx_param(io, 0xD0, (uint8_t[]){0xA4, 0xA1}, 2);
    esp_lcd_panel_io_tx_param(io, 0xE0, (uint8_t[]){0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38, 0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30}, 14);
    esp_lcd_panel_io_tx_param(io, 0xE1, (uint8_t[]){0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37, 0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x2E}, 14);
    esp_lcd_panel_io_tx_param(io, 0x20, NULL, 0); /* display inversion ON */
    esp_lcd_panel_io_tx_param(io, 0x29, NULL, 0); /* display ON */
}

static void st7789_fill_full(uint16_t color)
{
    static uint16_t rowbuf[240 * 40];
    color = fix_color(color); /* compensate panel invert + BGR like the flush does */
    for (int i = 0; i < 240 * 40; i++) {
        rowbuf[i] = color;
    }
    uint8_t caset[] = {0x00, 0x00, 0x00, 0xEF};
    uint8_t raset[] = {0x00, 0x00, 0x01, 0x3F};
    esp_lcd_panel_io_tx_param(s_io, 0x2A, caset, 4);
    esp_lcd_panel_io_tx_param(s_io, 0x2B, raset, 4);
    for (int y = 0; y < 320; y += 40) {
        esp_lcd_panel_io_tx_color(s_io, 0x2C, rowbuf, 240 * 40 * sizeof(uint16_t));
    }
}

void display_init(void)
{
    backlight_init();
    backlight_set(0);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .sclk_io_num = PIN_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_PCLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 2,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &s_io));

    lcd_reset();
    st7789_init();
    backlight_set(LCD_BL_PERCENT);

    s_fb = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_fb) {
        ESP_LOGE(TAG, "failed to allocate framebuffer");
        abort();
    }
    /* fill the whole ST7789 RAM so any visible area outside the 172px strip
     * shows a clean background instead of power-up garbage */
    st7789_fill_full(C_BG);
    display_clear(C_BG);
    display_flush();
}

void display_clear(uint16_t color)
{
    if (!s_fb) {
        return;
    }
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) {
        s_fb[i] = color;
    }
}

void display_clear_bg(void)
{
    if (!s_fb) {
        return;
    }
    memcpy(s_fb, sky_bg, sizeof(sky_bg));
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= LCD_H_RES || y >= LCD_V_RES || w <= 0 || h <= 0) {
        return;
    }
    if (x + w > LCD_H_RES) {
        w = LCD_H_RES - x;
    }
    if (y + h > LCD_V_RES) {
        h = LCD_V_RES - y;
    }
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *row = &s_fb[yy * LCD_H_RES];
        for (int xx = x; xx < x + w; xx++) {
            row[xx] = color;
        }
    }
}

void display_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color)
{
    if (r <= 0 || 2 * r >= w || 2 * r >= h) {
        display_fill_rect(x, y, w, h, color);
        return;
    }
    /* body */
    display_fill_rect(x + r, y, w - 2 * r, h, color);
    display_fill_rect(x, y + r, w, h - 2 * r, color);
    /* four rounded corners */
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                int xl = x + r + dx, yt = y + r + dy;
                if (xl >= 0 && xl < LCD_H_RES && yt >= 0 && yt < LCD_V_RES) {
                    s_fb[yt * LCD_H_RES + xl] = color;
                }
                xl = x + w - r - 1 + dx;
                if (xl >= 0 && xl < LCD_H_RES && yt >= 0 && yt < LCD_V_RES) {
                    s_fb[yt * LCD_H_RES + xl] = color;
                }
                int yb = y + h - r - 1 + dy;
                xl = x + r + dx;
                if (xl >= 0 && xl < LCD_H_RES && yb >= 0 && yb < LCD_V_RES) {
                    s_fb[yb * LCD_H_RES + xl] = color;
                }
                xl = x + w - r - 1 + dx;
                if (xl >= 0 && xl < LCD_H_RES && yb >= 0 && yb < LCD_V_RES) {
                    s_fb[yb * LCD_H_RES + xl] = color;
                }
            }
        }
    }
}

/* The panel inverts colors and swaps R/B (BGR). Compensate by pre-applying
 * the inverse (swap R/B, then invert) to each pixel before sending. */
static inline uint16_t fix_color(uint16_t v)
{
    uint16_t r = (v >> 11) & 0x1F;
    uint16_t g = (v >> 5) & 0x3F;
    uint16_t b = v & 0x1F;
    v = (uint16_t)((b << 11) | (g << 5) | r); /* swap R and B */
    return (uint16_t)(0xFFFF ^ v);            /* invert */
}

void display_flush(void)
{
    int x0 = LCD_GAP_X;
    int x1 = LCD_H_RES + LCD_GAP_X;
    int y0 = LCD_GAP_Y;
    int y1 = LCD_V_RES + LCD_GAP_Y;

    uint8_t caset[] = {
        (uint8_t)((x0 >> 8) & 0xFF), (uint8_t)(x0 & 0xFF),
        (uint8_t)(((x1 - 1) >> 8) & 0xFF), (uint8_t)((x1 - 1) & 0xFF),
    };
    uint8_t raset[] = {
        (uint8_t)((y0 >> 8) & 0xFF), (uint8_t)(y0 & 0xFF),
        (uint8_t)(((y1 - 1) >> 8) & 0xFF), (uint8_t)((y1 - 1) & 0xFF),
    };
    esp_lcd_panel_io_tx_param(s_io, 0x2A, caset, 4);
    esp_lcd_panel_io_tx_param(s_io, 0x2B, raset, 4);
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) {
        s_fb[i] = fix_color(s_fb[i]);
    }
    esp_lcd_panel_io_tx_color(s_io, 0x2C, s_fb, LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
}

static uint16_t utf8_decode(const char **sp)
{
    const uint8_t *s = (const uint8_t *)*sp;
    if (s[0] < 0x80) {
        *sp += 1;
        return s[0];
    }
    if ((s[0] & 0xE0) == 0xC0 && s[1]) {
        *sp += 2;
        return (uint16_t)(((s[0] & 0x1F) << 6) | (s[1] & 0x3F));
    }
    if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
        *sp += 3;
        return (uint16_t)(((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
    }
    *sp += 1;
    return '?';
}

static const glyph_t *glyph_lookup(const glyph_t *font, int count, uint16_t cp)
{
    for (int i = 0; i < count; i++) {
        if (font[i].cp == cp) {
            return &font[i];
        }
    }
    return NULL;
}

static int text_width_ex(const char *s, int scale, const glyph_t *font, int count)
{
    int w = 0;
    while (*s) {
        uint16_t cp = utf8_decode(&s);
        const glyph_t *g = glyph_lookup(font, count, cp);
        if (g) {
            w += g->advance * scale;
        }
    }
    return w;
}

static void draw_text_ex(int x, int y, const char *s, uint16_t fg, uint16_t bg,
                         int scale, bool fill_bg, const glyph_t *font, int count)
{
    int cx = x;
    while (*s) {
        uint16_t cp = utf8_decode(&s);
        const glyph_t *g = glyph_lookup(font, count, cp);
        if (!g) {
            g = glyph_lookup(font, count, '?');
        }
        if (g) {
            int gw = g->w * scale;
            int gh = g->h * scale;
            if (fill_bg) {
                display_fill_rect(cx, y, gw, gh, bg);
            }
            for (int r = 0; r < g->h; r++) {
                for (int c = 0; c < g->w; c++) {
                    if (g->data[r * g->rb + (c >> 3)] & (1 << (c & 7))) {
                        display_fill_rect(cx + c * scale, y + r * scale, scale, scale, fg);
                    }
                }
            }
            cx += g->advance * scale;
        }
    }
}

int display_text_width(const char *s, int scale)
{
    return text_width_ex(s, scale, font_small, FONT_SMALL_COUNT);
}

void display_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale, bool fill_bg)
{
    draw_text_ex(x, y, s, fg, bg, scale, fill_bg, font_small, FONT_SMALL_COUNT);
}

void display_text_center(int y, const char *s, uint16_t fg, int scale)
{
    int w = display_text_width(s, scale);
    int x = (LCD_W - w) / 2;
    if (x < 0) {
        x = 0;
    }
    draw_text_ex(x, y, s, fg, C_BG, scale, false, font_small, FONT_SMALL_COUNT);
}

void display_big_text(int x, int y, const char *s, uint16_t fg)
{
    draw_text_ex(x, y, s, fg, 0, 1, false, font_big, FONT_BIG_COUNT);
}

void display_icon(int x, int y, const weather_icon_t *icon, int scale, uint16_t main_col, uint16_t detail_col)
{
    for (int r = 0; r < ICON_SIZE; r++) {
        for (int c = 0; c < ICON_SIZE; c++) {
            int idx = r * ICON_ROWS + (c >> 3);
            int bit = 1 << (c & 7);
            if (icon->main && (icon->main[idx] & bit)) {
                display_fill_rect(x + c * scale, y + r * scale, scale, scale, main_col);
            } else if (icon->detail && (icon->detail[idx] & bit)) {
                display_fill_rect(x + c * scale, y + r * scale, scale, scale, detail_col);
            }
        }
    }
}
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_config.h"
#include "display.h"
#include "weather.h"
#include "led.h"
#include "screen.h"

#define TAG "main"

#define TEST_PATTERN 0

#if TEST_PATTERN
static void test_pattern(void)
{
    display_clear(C_BG);
    display_fill_rect(0, 0, 86, 160, 0xF800);   /* red */
    display_fill_rect(86, 0, 86, 160, 0x07E0);  /* green */
    display_fill_rect(0, 160, 86, 160, 0x001F); /* blue */
    display_fill_rect(86, 160, 86, 160, 0xAE77); /* mint */
    display_flush();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

#define REFRESH_OK_S   (30 * 60)
#define REFRESH_FAIL_S 60

#define C_DARK rgb565(18, 34, 66)

static void splash(const char *line1, const char *line2)
{
    display_clear_bg();
    display_text_center(120, line1, C_DARK, 2);
    if (line2) {
        display_text_center(160, line2, C_DARK, 1);
    }
    display_flush();
}

static int day_of_week(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    y -= m < 3;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static const char *dow_name(int dow)
{
    static const char *n[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return n[dow];
}

static uint16_t temp_color(float norm)
{
    /* interpolate cool (blue) -> warm (orange) by normalized temperature */
    uint8_t r = 100 + (uint8_t)((255 - 100) * norm);
    uint8_t g = 181 + (uint8_t)((183 - 181) * norm);
    uint8_t b = 246 - (uint8_t)((246 - 77) * norm);
    return rgb565(r, g, b);
}

static void render_graph(const weather_t *w, int day)
{
    const int x0 = 10, x1 = LCD_W - 10;
    const int y0 = 130, y1 = 270;
    const int gw = x1 - x0, gh = y1 - y0;

    float lo = 99.0f, hi = -99.0f;
    for (int h = 0; h < 24; h++) {
        float t = w->hourly[day][h];
        if (t < lo) lo = t;
        if (t > hi) hi = t;
    }
    if (hi - lo < 1.0f) hi = lo + 1.0f;

    /* baseline axis */
    display_fill_rect(x0, y1, gw, 1, C_LINE);

    const int nbars = 12;
    float bar_w = (float)gw / nbars;
    for (int i = 0; i < nbars; i++) {
        int h = i * 2; /* every 2nd hour */
        float norm = (w->hourly[day][h] - lo) / (hi - lo);
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        int bh = 1 + (int)(norm * (gh - 8));
        int bx = x0 + (int)(i * bar_w);
        int bw = (int)bar_w - 2;
        if (bw < 4) bw = 4;
        int br = bw / 2;
        if (br > 3) br = 3;
        display_fill_round_rect(bx, y1 - bh, bw, bh, br, temp_color(norm));
    }

    /* hour labels at their bar positions */
    static const char *labels[4] = {"0", "6", "12", "18"};
    static const int idx[4] = {0, 3, 6, 9};
    for (int i = 0; i < 4; i++) {
        int lx = x0 + (int)(idx[i] * bar_w);
        display_text(lx, y1 + 3, labels[i], C_DARK, C_BG, 1, false);
    }

    /* min/max labels */
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%d°", (int)(hi + 0.5f));
    display_text(x1 - display_text_width(tmp, 1), y0, tmp, C_DARK, C_BG, 1, false);
    snprintf(tmp, sizeof(tmp), "%d°", (int)(lo + 0.5f));
    display_text(x0, y1 - 12, tmp, C_DARK, C_BG, 1, false);
}

static void render(const weather_t *w, int day)
{
    char tmp[48];
    uint16_t mc, dc;
    const weather_icon_t *ic;
    int y, m, d;
    const char *date = day == 0 ? w->date0 : w->date1;
    const char *daylabel = day == 0 ? "TODAY" : "TOMORROW";
    int code = day == 0 ? w->now_code : w->code1;
    float maxt = day == 0 ? w->max0 : w->max1;
    float mint = day == 0 ? w->min0 : w->min1;

    display_clear_bg();

    /* header */
    display_fill_rect(0, 0, LCD_W, 17, C_HEADER);
    int lw = display_text_width(WEATHER_LOC, 1);
    display_text((LCD_W - lw) / 2, 3, WEATHER_LOC, C_TEXT, 0, 1, false);

    /* day header */
    sscanf(date, "%d-%d-%d", &y, &m, &d);
    snprintf(tmp, sizeof(tmp), "%s  %s %d", daylabel, dow_name(day_of_week(y, m, d)), d);
    display_text(4, 22, tmp, C_DARK, C_BG, 1, false);

    ic = weather_icon(code, &mc, &dc);
    display_icon(6, 36, ic, 2, mc, dc);

    snprintf(tmp, sizeof(tmp), "%d°", (int)((day == 0 ? w->now_temp : w->max1) + 0.5f));
    display_big_text(80, 38, tmp, C_DARK);

    snprintf(tmp, sizeof(tmp), "H %d°  L %d°", (int)(maxt + 0.5f), (int)(mint + 0.5f));
    display_text(82, 80, tmp, C_DARK, C_BG, 1, false);

    display_text_center(108, weather_desc(code), C_DARK, 1);

    /* temperature graph */
    if (w->hourly_valid) {
        render_graph(w, day);
    } else {
        display_text_center(200, "No hourly data", C_DARK, 1);
    }

    /* footer */
    snprintf(tmp, sizeof(tmp), "Updated %02d:%02d", w->now_hh, w->now_mm);
    display_text_center(300, tmp, C_DARK, 1);

    display_flush();
}

void app_main(void)
{
    display_init();
    led_init();
    screen_init();

#if TEST_PATTERN
    test_pattern();
#endif

    bool wifi_started = false;
    bool first = true;
    int64_t next_refresh = 0; /* refresh immediately on boot */

    for (;;) {
        screen_event_t ev = screen_tick();
        if (ev == SCREEN_WOKE) {
            next_refresh = 0;
        } else if (ev == SCREEN_SLEPT) {
            led_off();
        }

        if (screen_is_off()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (esp_timer_get_time() >= next_refresh) {
            if (!wifi_started) {
                splash("Weather Display", "Connecting to WiFi...");
                led_set(0, 0, 255);
                wifi_connect();
                wifi_started = true;
            } else if (!wifi_is_up()) {
                wifi_start_reconnect();
                if (!wifi_wait_connected(15)) {
                    splash("Weather Display", "No WiFi, retrying...");
                    led_set(255, 0, 0);
                    next_refresh = esp_timer_get_time() + (int64_t)REFRESH_FAIL_S * 1000000;
                    continue;
                }
            }

            splash(first ? "Weather Display" : "Refreshing...", "Fetching forecast...");
            led_set(255, 180, 0);

            weather_t w;
            if (weather_fetch(&w)) {
                render(&w, 0);
                led_set(0, 255, 0);
                next_refresh = esp_timer_get_time() + (int64_t)REFRESH_OK_S * 1000000;
            } else {
                splash("Weather Display", "Fetch failed, retrying...");
                led_set(255, 0, 0);
                next_refresh = esp_timer_get_time() + (int64_t)REFRESH_FAIL_S * 1000000;
            }
            screen_mark_active();
            first = false;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
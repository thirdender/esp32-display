#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "board_config.h"
#include "display.h"
#include "weather.h"
#include "led.h"

#define TAG "main"

#define TEST_PATTERN 0

#if TEST_PATTERN
static void test_pattern(void)
{
    const uint16_t colors[] = {rgb565(255, 0, 0), rgb565(0, 255, 0), rgb565(0, 0, 255),
                               rgb565(255, 255, 255)};
    for (int i = 0; i < 4; i++) {
        display_clear(colors[i]);
        display_flush();
        vTaskDelay(pdMS_TO_TICKS(8000));
    }
}
#endif

#define REFRESH_OK_S   (30 * 60)
#define REFRESH_FAIL_S 60

static void splash(const char *line1, const char *line2)
{
    display_clear(C_BG);
    display_text_center(120, line1, C_ACCENT, 2);
    if (line2) {
        display_text_center(160, line2, C_TEXT, 1);
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

static void render(const weather_t *w)
{
    char tmp[48];
    uint16_t mc, dc;
    const weather_icon_t *ic;
    int y, m, d;

    display_clear(C_BG);

    /* header */
    display_fill_rect(0, 0, LCD_W, 17, C_HEADER);
    int lw = display_text_width(WEATHER_LOC, 1);
    display_text((LCD_W - lw) / 2, 3, WEATHER_LOC, C_TEXT, 0, 1, false);

    /* TODAY */
    display_text(4, 22, "TODAY", C_ACCENT, C_BG, 1, true);

    ic = weather_icon(w->now_code, &mc, &dc);
    display_icon(6, 36, ic, 2, mc, dc);

    snprintf(tmp, sizeof(tmp), "%d°", (int)(w->now_temp + 0.5f));
    display_big_text(80, 38, tmp, C_TEXT);

    snprintf(tmp, sizeof(tmp), "H %d°  L %d°", (int)(w->max0 + 0.5f), (int)(w->min0 + 0.5f));
    display_text(82, 80, tmp, C_TEXT, C_BG, 1, true);

    display_text_center(108, weather_desc(w->now_code), C_DIM, 1);

    display_fill_rect(4, 138, LCD_W - 8, 1, C_LINE);

    /* TOMORROW */
    sscanf(w->date1, "%d-%d-%d", &y, &m, &d);
    snprintf(tmp, sizeof(tmp), "TOMORROW  %s %d", dow_name(day_of_week(y, m, d)), d);
    display_text(4, 148, tmp, C_ACCENT, C_BG, 1, true);

    ic = weather_icon(w->code1, &mc, &dc);
    display_icon(6, 172, ic, 1, mc, dc);

    snprintf(tmp, sizeof(tmp), "%d°", (int)(w->max1 + 0.5f));
    display_big_text(48, 166, tmp, C_TEXT);

    snprintf(tmp, sizeof(tmp), "H %d°  L %d°", (int)(w->max1 + 0.5f), (int)(w->min1 + 0.5f));
    display_text(50, 206, tmp, C_TEXT, C_BG, 1, true);

    display_text_center(224, weather_desc(w->code1), C_DIM, 1);

    /* footer */
    snprintf(tmp, sizeof(tmp), "Updated %02d:%02d", w->now_hh, w->now_mm);
    display_text_center(300, tmp, C_DIM, 1);

    display_flush();
}

void app_main(void)
{
    display_init();
    led_init();

#if TEST_PATTERN
    test_pattern();
#endif

    bool wifi_started = false;
    bool first = true;

    while (1) {
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
                vTaskDelay(pdMS_TO_TICKS(REFRESH_FAIL_S * 1000));
                continue;
            }
        }

        splash(first ? "Weather Display" : "Refreshing...", "Fetching forecast...");
        led_set(255, 180, 0);

        weather_t w;
        if (weather_fetch(&w)) {
            render(&w);
            led_set(0, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(REFRESH_OK_S * 1000));
        } else {
            splash("Weather Display", "Fetch failed, retrying...");
            led_set(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(REFRESH_FAIL_S * 1000));
        }
        first = false;
    }
}
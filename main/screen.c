#include "screen.h"
#include "board_config.h"
#include "display.h"

#include <stdint.h>
#include "esp_attr.h"
#include "esp_timer.h"
#include "driver/gpio.h"

/* Request flags (set from ISR/task, consumed by the main task in screen_tick). */
static volatile bool s_req_wake;
static volatile bool s_req_sleep;
static volatile bool s_req_toggle;

static bool s_off;
static int64_t s_last_active_us;
static int64_t s_last_toggle_us;

static void IRAM_ATTR button_isr(void *arg)
{
    s_req_toggle = true;
}

void screen_init(void)
{
    s_off = false;
    s_last_active_us = esp_timer_get_time();
    s_last_toggle_us = 0;
    display_set_backlight(LCD_BL_PERCENT);

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BUTTON, button_isr, NULL));
}

void screen_mark_active(void)
{
    s_last_active_us = esp_timer_get_time();
}

bool screen_is_off(void)
{
    return s_off;
}

void screen_request_wake(void)
{
    s_req_wake = true;
}

void screen_request_sleep(void)
{
    s_req_sleep = true;
}

void screen_request_toggle(void)
{
    s_req_toggle = true;
}

static void do_sleep(void)
{
    s_off = true;
    display_set_backlight(0);
}

static void do_wake(void)
{
    s_off = false;
    s_last_active_us = esp_timer_get_time();
    display_set_backlight(LCD_BL_PERCENT);
}

screen_event_t screen_tick(void)
{
    int64_t now = esp_timer_get_time();

    /* debounce: ignore requests that arrive within 150 ms of the last one */
    if (now - s_last_toggle_us < 150 * 1000) {
        return SCREEN_NONE;
    }

    screen_event_t ev = SCREEN_NONE;

    if (s_req_toggle) {
        s_req_toggle = false;
        s_last_toggle_us = now;
        if (s_off) {
            do_wake();
            ev = SCREEN_WOKE;
        } else {
            do_sleep();
            ev = SCREEN_SLEPT;
        }
    } else if (s_req_wake) {
        s_req_wake = false;
        s_last_toggle_us = now;
        if (s_off) {
            do_wake();
            ev = SCREEN_WOKE;
        }
    } else if (s_req_sleep) {
        s_req_sleep = false;
        s_last_toggle_us = now;
        if (!s_off) {
            do_sleep();
            ev = SCREEN_SLEPT;
        }
    }

    if (ev == SCREEN_NONE && !s_off &&
        now - s_last_active_us > (int64_t)SCREEN_IDLE_TIMEOUT_S * 1000000) {
        do_sleep();
        ev = SCREEN_SLEPT;
    }

    return ev;
}
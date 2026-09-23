#include "led.h"
#include "board_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_strip.h"

static led_strip_handle_t s_strip = NULL;
static SemaphoreHandle_t s_mux = NULL;

void led_init(void)
{
    led_strip_config_t cfg = {
        .strip_gpio_num = PIN_RGB_LED,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = { .with_dma = false },
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg, &rmt_cfg, &s_strip));
    s_mux = xSemaphoreCreateMutex();
    led_off();
}

void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) {
        return;
    }
    xSemaphoreTake(s_mux, portMAX_DELAY);
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
    xSemaphoreGive(s_mux);
}

void led_off(void)
{
    led_set(0, 0, 0);
}
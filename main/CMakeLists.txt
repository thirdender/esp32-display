idf_component_register(
    SRCS "main.c" "display.c" "weather.c" "led.c" "screen.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_driver_spi
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_rmt
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        nvs_flash
        esp_http_client
        json
        led_strip
)
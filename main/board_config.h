#pragma once

#define PIN_LCD_MOSI   6
#define PIN_LCD_SCLK   7
#define PIN_LCD_MISO   5
#define PIN_LCD_CS     14
#define PIN_LCD_DC     15
#define PIN_LCD_RST    21
#define PIN_LCD_BL     22
#define PIN_RGB_LED    8
#define PIN_BUTTON     9

#define LCD_HOST       SPI2_HOST
#define LCD_H_RES      172
#define LCD_V_RES      320
#define LCD_GAP_X      34
#define LCD_GAP_Y      0
#define LCD_PCLK_HZ    (20 * 1000 * 1000)
#define LCD_BL_PERCENT 80

#define WIFI_SSID      "Borg"
#define WIFI_PASS      "ancientquail852"
#define WIFI_TIMEOUT_S 25

#define WEATHER_LAT    "41.1961"
#define WEATHER_LON    "-75.9304"
#define WEATHER_LOC    "41.196N 75.930W"
#pragma once

#include <stdbool.h>
#include "generated/icons.h"

typedef struct {
    float now_temp;
    int   now_code;
    int   now_hh;
    int   now_mm;
    char  date0[11];
    char  date1[11];
    int   code0;
    int   code1;
    float max0;
    float min0;
    float max1;
    float min1;
} weather_t;

const weather_icon_t *weather_icon(int code, uint16_t *main_col, uint16_t *detail_col);
const char *weather_desc(int code);

bool wifi_connect(void);
bool wifi_is_up(void);
void wifi_start_reconnect(void);
bool wifi_wait_connected(int timeout_s);

bool weather_fetch(weather_t *w);
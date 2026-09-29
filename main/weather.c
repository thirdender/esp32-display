#include "weather.h"
#include "board_config.h"
#include "display.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "cJSON.h"

#define TAG "weather"

#define WIFI_BIT_CONNECTED BIT0
#define WIFI_BIT_FAILED    BIT1

static EventGroupHandle_t s_wifi_evt;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        EventBits_t was = xEventGroupGetBits(s_wifi_evt);
        xEventGroupClearBits(s_wifi_evt, WIFI_BIT_CONNECTED);
        if (was & WIFI_BIT_CONNECTED) {
            xEventGroupSetBits(s_wifi_evt, WIFI_BIT_FAILED); /* lost connection */
        } else {
            esp_wifi_connect(); /* initial connect retry */
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_evt, WIFI_BIT_CONNECTED);
        xEventGroupClearBits(s_wifi_evt, WIFI_BIT_FAILED);
    }
}

bool wifi_connect(void)
{
    s_wifi_evt = xEventGroupCreate();
    xEventGroupClearBits(s_wifi_evt, WIFI_BIT_CONNECTED | WIFI_BIT_FAILED);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    return wifi_wait_connected(WIFI_TIMEOUT_S);
}

bool wifi_is_up(void)
{
    return s_wifi_evt && (xEventGroupGetBits(s_wifi_evt) & WIFI_BIT_CONNECTED) != 0;
}

void wifi_start_reconnect(void)
{
    if (!s_wifi_evt) {
        return;
    }
    xEventGroupClearBits(s_wifi_evt, WIFI_BIT_FAILED);
    esp_wifi_connect();
}

bool wifi_wait_connected(int timeout_s)
{
    if (!s_wifi_evt) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_evt, WIFI_BIT_CONNECTED | WIFI_BIT_FAILED,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_s * 1000));
    if (bits & WIFI_BIT_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected");
        return true;
    }
    ESP_LOGW(TAG, "WiFi connect failed/timed out");
    return false;
}

static const char *API_URL =
    "http://api.open-meteo.com/v1/forecast"
    "?latitude=" WEATHER_LAT
    "&longitude=" WEATHER_LON
    "&current=temperature_2m,weather_code"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min"
    "&hourly=temperature_2m"
    "&timezone=auto&forecast_days=2&temperature_unit=celsius";

static bool http_get(const char *url, char *buf, size_t buflen)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 20000,
        .buffer_size = buflen,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGW(TAG, "http client init failed");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "http open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    esp_http_client_fetch_headers(client);

    size_t off = 0;
    int n;
    while (off < buflen - 1 &&
           (n = esp_http_client_read(client, buf + off, buflen - 1 - off)) > 0) {
        off += (size_t)n;
    }
    buf[off] = 0;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "http got %d bytes", (int)off);
    return off > 0;
}

bool weather_fetch(weather_t *w)
{
    static char buf[2048];
    if (!http_get(API_URL, buf, sizeof(buf))) {
        ESP_LOGW(TAG, "HTTP request failed");
        return false;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        return false;
    }

    bool ok = false;
    cJSON *cur = cJSON_GetObjectItem(root, "current");
    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (cur && daily) {
        cJSON *t = cJSON_GetObjectItem(cur, "temperature_2m");
        cJSON *c = cJSON_GetObjectItem(cur, "weather_code");
        cJSON *tm = cJSON_GetObjectItem(cur, "time");

        cJSON *times = cJSON_GetObjectItem(daily, "time");
        cJSON *codes = cJSON_GetObjectItem(daily, "weather_code");
        cJSON *mx = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON *mn = cJSON_GetObjectItem(daily, "temperature_2m_min");

        cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
        cJSON *htemps = hourly ? cJSON_GetObjectItem(hourly, "temperature_2m") : NULL;

        if (cJSON_IsNumber(t) && cJSON_IsNumber(c) && cJSON_IsString(tm) &&
            cJSON_IsArray(times) && cJSON_IsArray(codes) && cJSON_IsArray(mx) && cJSON_IsArray(mn) &&
            cJSON_GetArraySize(times) >= 2) {

            w->now_temp = (float)t->valuedouble;
            w->now_code = c->valueint;
            sscanf(tm->valuestring, "%*[^T]T%2d:%2d", &w->now_hh, &w->now_mm);

            cJSON *d0 = cJSON_GetArrayItem(times, 0);
            cJSON *d1 = cJSON_GetArrayItem(times, 1);
            strncpy(w->date0, d0->valuestring, sizeof(w->date0) - 1);
            w->date0[sizeof(w->date0) - 1] = 0;
            strncpy(w->date1, d1->valuestring, sizeof(w->date1) - 1);
            w->date1[sizeof(w->date1) - 1] = 0;

            w->code0 = cJSON_GetArrayItem(codes, 0)->valueint;
            w->code1 = cJSON_GetArrayItem(codes, 1)->valueint;
            w->max0 = (float)cJSON_GetArrayItem(mx, 0)->valuedouble;
            w->min0 = (float)cJSON_GetArrayItem(mn, 0)->valuedouble;
            w->max1 = (float)cJSON_GetArrayItem(mx, 1)->valuedouble;
            w->min1 = (float)cJSON_GetArrayItem(mn, 1)->valuedouble;

            w->hourly_valid = false;
            if (cJSON_IsArray(htemps) && cJSON_GetArraySize(htemps) >= 48) {
                for (int i = 0; i < 48; i++) {
                    w->hourly[i / 24][i % 24] =
                        (float)cJSON_GetArrayItem(htemps, i)->valuedouble;
                }
                w->hourly_valid = true;
            }
            ok = true;
        }
    }
    cJSON_Delete(root);

    if (ok) {
        ESP_LOGI(TAG, "now %.1f C code %d; today %dC/%dC; tomorrow %dC/%dC",
                 w->now_temp, w->now_code, (int)w->max0, (int)w->min0,
                 (int)w->max1, (int)w->min1);
    }
    return ok;
}

const weather_icon_t *weather_icon(int code, uint16_t *main_col, uint16_t *detail_col)
{
    if (code == 0) {
        *main_col = C_SUN; *detail_col = C_SUN;
        return &icon_sun;
    }
    if (code == 1 || code == 2) {
        *main_col = C_CLOUD; *detail_col = C_SUN;
        return &icon_partly;
    }
    if (code == 3) {
        *main_col = C_CLOUD; *detail_col = 0;
        return &icon_cloud;
    }
    if (code >= 45 && code <= 48) {
        *main_col = C_CLOUD; *detail_col = C_FOG;
        return &icon_fog;
    }
    if ((code >= 51 && code <= 57) || (code >= 61 && code <= 67) ||
        (code >= 80 && code <= 82)) {
        *main_col = C_CLOUD; *detail_col = C_RAIN;
        return &icon_rain;
    }
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
        *main_col = C_CLOUD; *detail_col = C_SNOW;
        return &icon_snow;
    }
    if (code >= 95 && code <= 99) {
        *main_col = C_CLOUD; *detail_col = C_THUNDER;
        return &icon_thunder;
    }
    *main_col = C_CLOUD; *detail_col = 0;
    return &icon_cloud;
}

const char *weather_desc(int code)
{
    switch (code) {
    case 0:  return "Clear sky";
    case 1:  return "Mostly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Dense drizzle";
    case 56:
    case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66:
    case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85:
    case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96:
    case 99: return "Storm + hail";
    default: return "Weather";
    }
}
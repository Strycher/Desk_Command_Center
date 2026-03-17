/**
 * Network HAL — P4 Implementation
 *
 * WiFi via ESP-Hosted (ESP32-C6 companion over SDIO).
 * HTTP via esp_http_client.
 * Standard esp_wifi API — ESP-Hosted is transparent at this layer.
 */

#if defined(CROWPANEL_P4)

#include "hal/network_hal.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char* TAG = "NET_P4";

// ---- WiFi state ----
static bool s_initialized = false;
static bool s_connected = false;
static char s_ip[20] = "";
static char s_ssid[33] = "";
static int8_t s_rssi = 0;
static char s_chipId[16] = "";
static esp_netif_t* s_netif = nullptr;
static EventGroupHandle_t s_wifiEvents = nullptr;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_DISCONNECTED_BIT BIT1

static void wifiEventHandler(void* arg, esp_event_base_t base,
                             int32_t id, void* data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi STA connected to AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_connected = false;
            s_ip[0] = '\0';
            s_rssi = 0;
            if (s_wifiEvents) {
                xEventGroupSetBits(s_wifiEvents, WIFI_DISCONNECTED_BIT);
                xEventGroupClearBits(s_wifiEvents, WIFI_CONNECTED_BIT);
            }
            ESP_LOGW(TAG, "WiFi disconnected");
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = (ip_event_got_ip_t*)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;

        /* Cache SSID + RSSI */
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            strncpy(s_ssid, (const char*)ap.ssid, sizeof(s_ssid) - 1);
            s_rssi = ap.rssi;
        }

        if (s_wifiEvents) {
            xEventGroupSetBits(s_wifiEvents, WIFI_CONNECTED_BIT);
            xEventGroupClearBits(s_wifiEvents, WIFI_DISCONNECTED_BIT);
        }
        ESP_LOGI(TAG, "Got IP: %s  RSSI: %d", s_ip, s_rssi);
    }
}

namespace hal {
namespace network {

void init()
{
    if (s_initialized) return;

    /* NVS (required by esp_wifi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Network stack + event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();

    /* WiFi driver (ESP-Hosted handles C6 SDIO transparently) */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, nullptr));

    /* STA mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifiEvents = xEventGroupCreate();

    /* Chip ID from MAC */
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_chipId, sizeof(s_chipId), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    s_initialized = true;
    ESP_LOGI(TAG, "Network init done, chipId=%s", s_chipId);
}

bool isConnected() { return s_connected; }
int8_t rssi()      { return s_rssi; }
const char* ipAddress() { return s_ip; }
const char* ssid()      { return s_ssid; }
const char* chipId()    { return s_chipId; }

// ---- HTTP ----

struct HttpCollector {
    char* buf;
    uint32_t len;
    uint32_t cap;
};

static esp_err_t httpEventCb(esp_http_client_event_t* evt)
{
    auto* col = (HttpCollector*)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && col) {
        uint32_t needed = col->len + evt->data_len + 1;
        if (needed > col->cap) {
            uint32_t newCap = needed * 2;
            char* newBuf = (char*)realloc(col->buf, newCap);
            if (!newBuf) return ESP_FAIL;
            col->buf = newBuf;
            col->cap = newCap;
        }
        memcpy(col->buf + col->len, evt->data, evt->data_len);
        col->len += evt->data_len;
        col->buf[col->len] = '\0';
    }
    return ESP_OK;
}

HttpResponse httpGet(const char* url, const char** headers, uint32_t timeoutMs)
{
    HttpResponse resp = {};
    resp.status = -1;

    HttpCollector col = {};
    col.cap = 4096;
    col.buf = (char*)malloc(col.cap);
    if (!col.buf) {
        snprintf(resp.error, sizeof(resp.error), "malloc failed");
        return resp;
    }
    col.buf[0] = '\0';

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.event_handler = httpEventCb;
    cfg.user_data = &col;
    cfg.timeout_ms = (int)timeoutMs;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        snprintf(resp.error, sizeof(resp.error), "client init failed");
        free(col.buf);
        return resp;
    }

    /* Add custom headers */
    if (headers) {
        for (const char** h = headers; *h; h++) {
            const char* colon = strchr(*h, ':');
            if (colon) {
                char key[64];
                size_t keyLen = colon - *h;
                if (keyLen >= sizeof(key)) keyLen = sizeof(key) - 1;
                memcpy(key, *h, keyLen);
                key[keyLen] = '\0';
                const char* val = colon + 1;
                while (*val == ' ') val++;
                esp_http_client_set_header(client, key, val);
            }
        }
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        resp.status = esp_http_client_get_status_code(client);
        resp.body = col.buf;
        resp.bodyLen = col.len;
    } else {
        snprintf(resp.error, sizeof(resp.error), "%s", esp_err_to_name(err));
        free(col.buf);
    }

    esp_http_client_cleanup(client);
    return resp;
}

HttpResponse httpPost(const char* url, const char* body,
                      const char* contentType,
                      const char** headers, uint32_t timeoutMs)
{
    HttpResponse resp = {};
    resp.status = -1;

    HttpCollector col = {};
    col.cap = 2048;
    col.buf = (char*)malloc(col.cap);
    if (!col.buf) {
        snprintf(resp.error, sizeof(resp.error), "malloc failed");
        return resp;
    }
    col.buf[0] = '\0';

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.event_handler = httpEventCb;
    cfg.user_data = &col;
    cfg.timeout_ms = (int)timeoutMs;
    cfg.method = HTTP_METHOD_POST;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        snprintf(resp.error, sizeof(resp.error), "client init failed");
        free(col.buf);
        return resp;
    }

    if (contentType) {
        esp_http_client_set_header(client, "Content-Type", contentType);
    }
    if (body) {
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    if (headers) {
        for (const char** h = headers; *h; h++) {
            const char* colon = strchr(*h, ':');
            if (colon) {
                char key[64];
                size_t keyLen = colon - *h;
                if (keyLen >= sizeof(key)) keyLen = sizeof(key) - 1;
                memcpy(key, *h, keyLen);
                key[keyLen] = '\0';
                const char* val = colon + 1;
                while (*val == ' ') val++;
                esp_http_client_set_header(client, key, val);
            }
        }
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        resp.status = esp_http_client_get_status_code(client);
        resp.body = col.buf;
        resp.bodyLen = col.len;
    } else {
        snprintf(resp.error, sizeof(resp.error), "%s", esp_err_to_name(err));
        free(col.buf);
    }

    esp_http_client_cleanup(client);
    return resp;
}

void httpResponseFree(HttpResponse* resp)
{
    if (resp && resp->body) {
        free(resp->body);
        resp->body = nullptr;
        resp->bodyLen = 0;
    }
}

}  // namespace network
}  // namespace hal

#endif  // CROWPANEL_P4

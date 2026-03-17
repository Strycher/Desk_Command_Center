/**
 * NVS Config Store — P4 Implementation
 * Uses ESP-IDF nvs_flash directly (no Arduino Preferences wrapper).
 * Same NVS namespace and key names as S3 for cross-platform compatibility.
 */

#if defined(CROWPANEL_P4)

#include "config_store.h"
#include "hal/platform_hal.h"

#include <cstring>
#include <cstdio>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "CFG_P4";
static const char* NVS_NAMESPACE = "dcc_cfg";
static nvs_handle_t s_nvs = 0;

/* Defaults */
static constexpr const char* DEFAULT_BRIDGE  = "192.168.50.24:8080";
static constexpr const char* DEFAULT_TZ      = "EST5EDT,M3.2.0,M11.1.0";
static constexpr uint8_t     DEFAULT_BRIGHT  = 0x10;
static constexpr bool        DEFAULT_24H     = false;
static constexpr uint16_t    DEFAULT_POLL    = 30;

/* NVS keys (max 15 chars) */
static const char* KEY_WIFI_COUNT  = "wifi_count";
static const char* KEY_BRIDGE      = "bridge_url";
static const char* KEY_TZ          = "timezone";
static const char* KEY_BRIGHT      = "brightness";
static const char* KEY_24H         = "clock_24h";
static const char* KEY_POLL        = "poll_sec";
static const char* KEY_DEV_NAME    = "dev_name";
static const char* KEY_DEV_KEY     = "dev_key";
static const char* KEY_HOME_SSID   = "home_ssid";

/* Helpers */
static uint8_t nvsGetU8(const char* key, uint8_t def) {
    uint8_t val = def;
    nvs_get_u8(s_nvs, key, &val);
    return val;
}

static uint16_t nvsGetU16(const char* key, uint16_t def) {
    uint16_t val = def;
    nvs_get_u16(s_nvs, key, &val);
    return val;
}

static void nvsGetStr(const char* key, char* buf, size_t bufLen) {
    buf[0] = '\0';
    size_t len = bufLen;
    nvs_get_str(s_nvs, key, buf, &len);
}

static void loadWifi(DeviceConfig& cfg) {
    cfg.wifi_count = nvsGetU8(KEY_WIFI_COUNT, 0);
    if (cfg.wifi_count > DCC_MAX_WIFI) cfg.wifi_count = DCC_MAX_WIFI;

    for (uint8_t i = 0; i < DCC_MAX_WIFI; i++) {
        char key_s[10], key_p[10], key_r[10];
        snprintf(key_s, sizeof(key_s), "wifi_s%d", i);
        snprintf(key_p, sizeof(key_p), "wifi_p%d", i);
        snprintf(key_r, sizeof(key_r), "wifi_r%d", i);

        if (i < cfg.wifi_count) {
            nvsGetStr(key_s, cfg.wifi[i].ssid, DCC_SSID_LEN);
            nvsGetStr(key_p, cfg.wifi[i].password, DCC_PASS_LEN);
            cfg.wifi[i].priority = nvsGetU8(key_r, i);
        } else {
            cfg.wifi[i].ssid[0] = '\0';
            cfg.wifi[i].password[0] = '\0';
            cfg.wifi[i].priority = i;
        }
    }
}

static void saveWifi(const DeviceConfig& cfg) {
    nvs_set_u8(s_nvs, KEY_WIFI_COUNT, cfg.wifi_count);

    for (uint8_t i = 0; i < cfg.wifi_count; i++) {
        char key_s[10], key_p[10], key_r[10];
        snprintf(key_s, sizeof(key_s), "wifi_s%d", i);
        snprintf(key_p, sizeof(key_p), "wifi_p%d", i);
        snprintf(key_r, sizeof(key_r), "wifi_r%d", i);

        nvs_set_str(s_nvs, key_s, cfg.wifi[i].ssid);
        nvs_set_str(s_nvs, key_p, cfg.wifi[i].password);
        nvs_set_u8(s_nvs, key_r, cfg.wifi[i].priority);
    }
}

void ConfigStore::init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "NVS namespace opened");
}

DeviceConfig ConfigStore::load() {
    DeviceConfig cfg = {};

    loadWifi(cfg);
    nvsGetStr(KEY_BRIDGE, cfg.bridge_url, DCC_URL_LEN);
    nvsGetStr(KEY_TZ, cfg.timezone, DCC_TZ_LEN);
    cfg.brightness        = nvsGetU8(KEY_BRIGHT, DEFAULT_BRIGHT);
    cfg.clock_24h         = nvsGetU8(KEY_24H, DEFAULT_24H ? 1 : 0) != 0;
    cfg.poll_interval_sec = nvsGetU16(KEY_POLL, DEFAULT_POLL);
    nvsGetStr(KEY_DEV_NAME, cfg.device_name, DCC_DEV_NAME_LEN);
    nvsGetStr(KEY_DEV_KEY, cfg.device_key, DCC_DEV_KEY_LEN);
    nvsGetStr(KEY_HOME_SSID, cfg.home_ssid, DCC_SSID_LEN);

    /* First-boot WiFi provisioning — seed home network if NVS is empty.
       Only 2.4 GHz — the ESP32-C6 companion doesn't support 5 GHz. */
    if (cfg.wifi_count == 0) {
        ESP_LOGW(TAG, "No WiFi networks — provisioning defaults");
        strncpy(cfg.wifi[0].ssid, "tsunami", DCC_SSID_LEN - 1);
        strncpy(cfg.wifi[0].password, "Ch33t0s!", DCC_PASS_LEN - 1);
        cfg.wifi[0].priority = 0;

        cfg.wifi_count = 1;
        strncpy(cfg.home_ssid, "tsunami", DCC_SSID_LEN - 1);

        /* Persist so this only runs once */
        saveWifi(cfg);
        nvs_set_str(s_nvs, KEY_HOME_SSID, cfg.home_ssid);
        nvs_commit(s_nvs);
    }

    /* Apply defaults for empty strings */
    if (cfg.bridge_url[0] == '\0') {
        strncpy(cfg.bridge_url, DEFAULT_BRIDGE, DCC_URL_LEN - 1);
    }
    if (cfg.timezone[0] == '\0') {
        strncpy(cfg.timezone, DEFAULT_TZ, DCC_TZ_LEN - 1);
    }

    ESP_LOGI(TAG, "loaded — bridge=%s tz=%s bright=0x%02X poll=%ds wifi=%d",
             cfg.bridge_url, cfg.timezone, cfg.brightness,
             cfg.poll_interval_sec, cfg.wifi_count);
    return cfg;
}

void ConfigStore::save(const DeviceConfig& cfg) {
    saveWifi(cfg);
    nvs_set_str(s_nvs, KEY_BRIDGE, cfg.bridge_url);
    nvs_set_str(s_nvs, KEY_TZ, cfg.timezone);
    nvs_set_u8(s_nvs, KEY_BRIGHT, cfg.brightness);
    nvs_set_u8(s_nvs, KEY_24H, cfg.clock_24h ? 1 : 0);
    nvs_set_u16(s_nvs, KEY_POLL, cfg.poll_interval_sec);
    nvs_set_str(s_nvs, KEY_DEV_NAME, cfg.device_name);
    nvs_set_str(s_nvs, KEY_DEV_KEY, cfg.device_key);
    nvs_set_str(s_nvs, KEY_HOME_SSID, cfg.home_ssid);
    nvs_commit(s_nvs);

    ESP_LOGI(TAG, "saved to NVS");
}

void ConfigStore::reset() {
    nvs_erase_all(s_nvs);
    nvs_commit(s_nvs);
    ESP_LOGW(TAG, "factory reset — NVS cleared");
}

#endif  // CROWPANEL_P4

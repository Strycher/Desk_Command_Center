/**
 * NVS Config Store — Implementation
 * Uses Arduino Preferences for typed key-value persistence.
 * NVS key names are max 15 chars.
 */

#include "config_store.h"
#include <Preferences.h>
#include "logger.h"

static Preferences prefs;
static const char* NVS_NAMESPACE = "dcc_cfg";

/* Defaults */
static constexpr const char* DEFAULT_BRIDGE  = "genx2k.dynu.net";
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

/* WiFi entries stored as wifi_s0..wifi_s4 (ssid), wifi_p0..wifi_p4 (pass), wifi_r0..wifi_r4 (priority) */
static void loadWifi(DeviceConfig& cfg) {
    cfg.wifi_count = prefs.getUChar(KEY_WIFI_COUNT, 0);
    if (cfg.wifi_count > DCC_MAX_WIFI) cfg.wifi_count = DCC_MAX_WIFI;

    for (uint8_t i = 0; i < DCC_MAX_WIFI; i++) {
        char key_s[10], key_p[10], key_r[10];
        snprintf(key_s, sizeof(key_s), "wifi_s%d", i);
        snprintf(key_p, sizeof(key_p), "wifi_p%d", i);
        snprintf(key_r, sizeof(key_r), "wifi_r%d", i);

        if (i < cfg.wifi_count) {
            prefs.getString(key_s, cfg.wifi[i].ssid, DCC_SSID_LEN);
            prefs.getString(key_p, cfg.wifi[i].password, DCC_PASS_LEN);
            cfg.wifi[i].priority = prefs.getUChar(key_r, i);
        } else {
            cfg.wifi[i].ssid[0] = '\0';
            cfg.wifi[i].password[0] = '\0';
            cfg.wifi[i].priority = i;
        }
    }
}

static void saveWifi(const DeviceConfig& cfg) {
    prefs.putUChar(KEY_WIFI_COUNT, cfg.wifi_count);

    for (uint8_t i = 0; i < cfg.wifi_count; i++) {
        char key_s[10], key_p[10], key_r[10];
        snprintf(key_s, sizeof(key_s), "wifi_s%d", i);
        snprintf(key_p, sizeof(key_p), "wifi_p%d", i);
        snprintf(key_r, sizeof(key_r), "wifi_r%d", i);

        prefs.putString(key_s, cfg.wifi[i].ssid);
        prefs.putString(key_p, cfg.wifi[i].password);
        prefs.putUChar(key_r, cfg.wifi[i].priority);
    }
}

void ConfigStore::init() {
    prefs.begin(NVS_NAMESPACE, false);
    LOG_INFO("CFG: NVS namespace opened");
}

DeviceConfig ConfigStore::load() {
    DeviceConfig cfg = {};

    loadWifi(cfg);
    prefs.getString(KEY_BRIDGE, cfg.bridge_url, DCC_URL_LEN);
    prefs.getString(KEY_TZ, cfg.timezone, DCC_TZ_LEN);
    cfg.brightness       = prefs.getUChar(KEY_BRIGHT, DEFAULT_BRIGHT);
    cfg.clock_24h        = prefs.getBool(KEY_24H, DEFAULT_24H);
    cfg.poll_interval_sec = prefs.getUShort(KEY_POLL, DEFAULT_POLL);

    /* Apply defaults for empty strings */
    if (cfg.bridge_url[0] == '\0') {
        strncpy(cfg.bridge_url, DEFAULT_BRIDGE, DCC_URL_LEN - 1);
    }
    if (cfg.timezone[0] == '\0') {
        strncpy(cfg.timezone, DEFAULT_TZ, DCC_TZ_LEN - 1);
    }

    LOG_INFO("CFG: loaded — bridge=%s tz=%s bright=0x%02X poll=%ds wifi=%d",
                  cfg.bridge_url, cfg.timezone, cfg.brightness,
                  cfg.poll_interval_sec, cfg.wifi_count);
    return cfg;
}

void ConfigStore::save(const DeviceConfig& cfg) {
    saveWifi(cfg);
    prefs.putString(KEY_BRIDGE, cfg.bridge_url);
    prefs.putString(KEY_TZ, cfg.timezone);
    prefs.putUChar(KEY_BRIGHT, cfg.brightness);
    prefs.putBool(KEY_24H, cfg.clock_24h);
    prefs.putUShort(KEY_POLL, cfg.poll_interval_sec);

    LOG_INFO("CFG: saved to NVS");
}

void ConfigStore::reset() {
    prefs.clear();
    LOG_WARN("CFG: factory reset — NVS cleared");
}

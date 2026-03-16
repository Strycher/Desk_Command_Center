/**
 * NVS Config Store — Persistent device settings
 * Uses Arduino Preferences (ESP-IDF NVS) for flash storage.
 */

#pragma once
#include <Arduino.h>

static constexpr uint8_t  DCC_MAX_WIFI       = 5;
static constexpr uint8_t  DCC_SSID_LEN       = 33;
static constexpr uint8_t  DCC_PASS_LEN       = 65;
static constexpr uint8_t  DCC_URL_LEN        = 128;
static constexpr uint8_t  DCC_TZ_LEN         = 48;
static constexpr uint8_t  DCC_DEV_NAME_LEN   = 32;
static constexpr uint8_t  DCC_DEV_KEY_LEN    = 48;

struct WifiEntry {
    char ssid[DCC_SSID_LEN];
    char password[DCC_PASS_LEN];
    uint8_t priority;           // 0 = highest
};

struct DeviceConfig {
    WifiEntry wifi[DCC_MAX_WIFI];
    uint8_t   wifi_count;
    char      bridge_url[DCC_URL_LEN];
    char      timezone[DCC_TZ_LEN];
    uint8_t   brightness;       // 0x05..0x10 (STC8H1K28 range)
    bool      clock_24h;
    uint16_t  poll_interval_sec;
    char      device_name[DCC_DEV_NAME_LEN];   // human-readable name ("dcc-7inch")
    char      device_key[DCC_DEV_KEY_LEN];     // pre-shared auth token
    char      home_ssid[DCC_SSID_LEN];         // home WiFi SSID (for WireGuard auto-detect)
};

namespace ConfigStore {
    void init();
    DeviceConfig load();
    void save(const DeviceConfig& cfg);
    void reset();
}

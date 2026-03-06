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
};

namespace ConfigStore {
    void init();
    DeviceConfig load();
    void save(const DeviceConfig& cfg);
    void reset();
}

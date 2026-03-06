/**
 * WiFi Manager — Multi-SSID with auto-reconnect and backoff.
 * Non-blocking: call check() from loop().
 */

#pragma once
#include <WiFi.h>
#include "config_store.h"

enum class WifiState : uint8_t {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    FAILED
};

namespace WifiManager {
    void init(const DeviceConfig& cfg);
    void check();
    WifiState state();
    int8_t rssi();
    String ip();
    String ssid();
}

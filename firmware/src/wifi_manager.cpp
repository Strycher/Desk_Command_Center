/**
 * WiFi Manager — Implementation
 * Tries SSIDs in priority order. Exponential backoff on disconnect.
 * Uses hal::network + hal::platform for portability across S3/P4.
 */

#include "wifi_manager.h"
#include "hal/network_hal.h"
#include "hal/platform_hal.h"
#include "logger.h"

static WifiState    _state = WifiState::DISCONNECTED;
static WifiEntry    _networks[DCC_MAX_WIFI];
static uint8_t      _count = 0;
static uint8_t      _tryIdx = 0;
static uint32_t     _lastAttemptMs = 0;
static uint32_t     _backoffMs = 1000;
/* 15s allows for ESP-Hosted SDIO latency on P4 (C6 companion) */
static constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t MAX_BACKOFF_MS     = 60000;

static void sortByPriority() {
    /* Simple insertion sort — max 5 entries */
    for (uint8_t i = 1; i < _count; i++) {
        WifiEntry tmp = _networks[i];
        int8_t j = i - 1;
        while (j >= 0 && _networks[j].priority > tmp.priority) {
            _networks[j + 1] = _networks[j];
            j--;
        }
        _networks[j + 1] = tmp;
    }
}

static void tryConnect() {
    if (_count == 0) {
        _state = WifiState::FAILED;
        LOG_WARN("WIFI: no networks configured");
        return;
    }

    _state = WifiState::CONNECTING;
    const char* ssid = _networks[_tryIdx].ssid;
    const char* pass = _networks[_tryIdx].password;

    LOG_INFO("WIFI: connecting to '%s' (%d/%d)...", ssid, _tryIdx + 1, _count);
    hal::network::connect(ssid, pass);
    _lastAttemptMs = hal::platform::uptimeMs();
}

void WifiManager::init(const DeviceConfig& cfg) {
    hal::network::init();

    _count = cfg.wifi_count;
    if (_count > DCC_MAX_WIFI) _count = DCC_MAX_WIFI;

    for (uint8_t i = 0; i < _count; i++) {
        _networks[i] = cfg.wifi[i];
    }
    sortByPriority();

    _tryIdx = 0;
    _backoffMs = 1000;

    if (_count > 0) {
        tryConnect();
    } else {
        _state = WifiState::FAILED;
        LOG_WARN("WIFI: no networks in config");
    }
}

void WifiManager::check() {
    if (_state == WifiState::CONNECTED) {
        if (!hal::network::isConnected()) {
            LOG_WARN("WIFI: connection lost");
            _state = WifiState::DISCONNECTED;
            _tryIdx = 0;
            _backoffMs = 1000;
        }
        return;
    }

    if (_state == WifiState::CONNECTING) {
        if (hal::network::isConnected()) {
            _state = WifiState::CONNECTED;
            _backoffMs = 1000;
            LOG_INFO("WIFI: connected — IP=%s RSSI=%d",
                     hal::network::ipAddress(), hal::network::rssi());
            return;
        }

        /* Timeout on current SSID */
        uint32_t now = hal::platform::uptimeMs();
        if (now - _lastAttemptMs > CONNECT_TIMEOUT_MS) {
            LOG_WARN("WIFI: timeout on '%s'", _networks[_tryIdx].ssid);
            hal::network::disconnect();
            _tryIdx++;

            if (_tryIdx >= _count) {
                /* Exhausted all SSIDs — backoff then retry */
                _tryIdx = 0;
                _state = WifiState::DISCONNECTED;
                _lastAttemptMs = now;
                LOG_WARN("WIFI: all SSIDs failed, backoff %lums", _backoffMs);
            } else {
                tryConnect();
            }
        }
        return;
    }

    /* DISCONNECTED or FAILED — wait for backoff then retry */
    uint32_t now = hal::platform::uptimeMs();
    if (_count > 0 && now - _lastAttemptMs > _backoffMs) {
        _backoffMs = (_backoffMs * 2 > MAX_BACKOFF_MS) ? MAX_BACKOFF_MS : _backoffMs * 2;
        tryConnect();
    }
}

WifiState WifiManager::state() { return _state; }

int8_t WifiManager::rssi() {
    return (_state == WifiState::CONNECTED) ? hal::network::rssi() : 0;
}

const char* WifiManager::ip() {
    return (_state == WifiState::CONNECTED) ? hal::network::ipAddress() : "--";
}

const char* WifiManager::ssid() {
    return (_state == WifiState::CONNECTED) ? hal::network::ssid() : "--";
}

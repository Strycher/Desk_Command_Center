/**
 * WiFi Manager — Implementation
 * Tries SSIDs in priority order. Exponential backoff on disconnect.
 */

#include "wifi_manager.h"

static WifiState    _state = WifiState::DISCONNECTED;
static WifiEntry    _networks[DCC_MAX_WIFI];
static uint8_t      _count = 0;
static uint8_t      _tryIdx = 0;
static uint32_t     _lastAttemptMs = 0;
static uint32_t     _backoffMs = 1000;
static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
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
        Serial.println("WIFI: no networks configured");
        return;
    }

    _state = WifiState::CONNECTING;
    const char* ssid = _networks[_tryIdx].ssid;
    const char* pass = _networks[_tryIdx].password;

    Serial.printf("WIFI: connecting to '%s' (%d/%d)...\n", ssid, _tryIdx + 1, _count);
    WiFi.begin(ssid, pass);
    _lastAttemptMs = millis();
}

void WifiManager::init(const DeviceConfig& cfg) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);

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
        Serial.println("WIFI: no networks in config");
    }
}

void WifiManager::check() {
    if (_state == WifiState::CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WIFI: connection lost");
            _state = WifiState::DISCONNECTED;
            _tryIdx = 0;
            _backoffMs = 1000;
        }
        return;
    }

    if (_state == WifiState::CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            _state = WifiState::CONNECTED;
            _backoffMs = 1000;
            Serial.printf("WIFI: connected — IP=%s RSSI=%d\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            return;
        }

        /* Timeout on current SSID */
        if (millis() - _lastAttemptMs > CONNECT_TIMEOUT_MS) {
            Serial.printf("WIFI: timeout on '%s'\n", _networks[_tryIdx].ssid);
            WiFi.disconnect();
            _tryIdx++;

            if (_tryIdx >= _count) {
                /* Exhausted all SSIDs — backoff then retry */
                _tryIdx = 0;
                _state = WifiState::DISCONNECTED;
                _lastAttemptMs = millis();
                Serial.printf("WIFI: all SSIDs failed, backoff %lums\n", _backoffMs);
            } else {
                tryConnect();
            }
        }
        return;
    }

    /* DISCONNECTED or FAILED — wait for backoff then retry */
    if (_count > 0 && millis() - _lastAttemptMs > _backoffMs) {
        _backoffMs = min(_backoffMs * 2, MAX_BACKOFF_MS);
        tryConnect();
    }
}

WifiState WifiManager::state() { return _state; }

int8_t WifiManager::rssi() {
    return (_state == WifiState::CONNECTED) ? WiFi.RSSI() : 0;
}

String WifiManager::ip() {
    return (_state == WifiState::CONNECTED) ? WiFi.localIP().toString() : String("--");
}

String WifiManager::ssid() {
    return (_state == WifiState::CONNECTED) ? WiFi.SSID() : String("--");
}

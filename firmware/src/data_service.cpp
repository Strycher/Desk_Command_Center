/**
 * Data Service — Implementation
 * Non-blocking HTTP GET with ArduinoJson parsing.
 */

#include "data_service.h"
#include <HTTPClient.h>
#include <WiFi.h>

struct SpiRamAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) override { return ps_malloc(size); }
    void deallocate(void* ptr) override { free(ptr); }
    void* reallocate(void* ptr, size_t new_size) override {
        return ps_realloc(ptr, new_size);
    }
};

static SpiRamAllocator psramAllocator;
static JsonDocument _doc(&psramAllocator);

static char         _url[256] = {};
static uint32_t     _intervalMs = 30000;
static uint32_t     _lastPollMs = 0;
static FetchState   _state = FetchState::IDLE;
static String       _lastError;
static uint32_t     _lastSuccessMs = 0;
static DataCallback _callback = nullptr;
static bool         _forcePoll = false;

static void doFetch() {
    if (WiFi.status() != WL_CONNECTED) {
        _state = FetchState::ERROR;
        _lastError = "WiFi not connected";
        return;
    }

    _state = FetchState::FETCHING;
    HTTPClient http;

    String url = String("http://") + _url + "/api/dashboard";
    Serial.printf("DATA: fetching %s\n", url.c_str());

    http.begin(url);
    http.setTimeout(10000);
    int code = http.GET();

    if (code == HTTP_CODE_OK) {
        String payload = http.getString();
        _doc.clear();
        DeserializationError err = deserializeJson(_doc, payload);

        if (err) {
            _state = FetchState::ERROR;
            _lastError = String("JSON: ") + err.c_str();
            Serial.printf("DATA: parse error — %s\n", err.c_str());
        } else {
            _state = FetchState::SUCCESS;
            _lastSuccessMs = millis();
            _lastError = "";
            Serial.printf("DATA: OK — %d bytes, %d keys\n",
                          payload.length(), _doc.size());
            if (_callback) _callback(_doc);
        }
    } else {
        _state = FetchState::ERROR;
        _lastError = String("HTTP ") + String(code);
        Serial.printf("DATA: HTTP error %d\n", code);
    }

    http.end();
}

void DataService::init(const char* bridgeUrl, uint16_t pollIntervalSec) {
    strncpy(_url, bridgeUrl, sizeof(_url) - 1);
    _intervalMs = (uint32_t)pollIntervalSec * 1000;
    _lastPollMs = 0;
    Serial.printf("DATA: init bridge=%s interval=%ds\n", _url, pollIntervalSec);
}

void DataService::poll() {
    uint32_t now = millis();
    bool due = (now - _lastPollMs >= _intervalMs) || _forcePoll;

    if (due && _state != FetchState::FETCHING) {
        _lastPollMs = now;
        _forcePoll = false;
        doFetch();
    }
}

void DataService::forcePoll() {
    _forcePoll = true;
}

FetchState DataService::state() { return _state; }

JsonDocument& DataService::data() { return _doc; }

uint32_t DataService::lastFetchMs() { return _lastSuccessMs; }

String DataService::lastError() { return _lastError; }

void DataService::onData(DataCallback cb) { _callback = cb; }

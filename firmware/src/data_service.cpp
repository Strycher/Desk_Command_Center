/**
 * Data Service — Background HTTP polling via FreeRTOS task.
 *
 * Network I/O (HTTP fetch + JSON parse) runs on Core 0 in a dedicated
 * task. The main loop on Core 1 checks a flag and fires the data
 * callback — all LVGL updates happen on Core 1, never blocking on
 * network timeouts.
 *
 * Synchronization:
 *   _dataMutex  — guards _doc (JsonDocument) during write/read
 *   _dataReady  — volatile flag, set by net task, cleared by main loop
 */

#include "data_service.h"
#include "ntp_time.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

/* --- PSRAM allocator for ArduinoJson --- */
struct SpiRamAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) override { return ps_malloc(size); }
    void deallocate(void* ptr) override { free(ptr); }
    void* reallocate(void* ptr, size_t new_size) override {
        return ps_realloc(ptr, new_size);
    }
};

static SpiRamAllocator psramAllocator;
static JsonDocument _doc(&psramAllocator);

/* --- Configuration --- */
static char         _url[256] = {};
static uint32_t     _intervalMs = 30000;

/* --- State (written by net task, read by main loop) --- */
static volatile FetchState _state = FetchState::IDLE;
static String       _lastError;
static uint32_t     _lastSuccessMs = 0;
static DataCallback _callback = nullptr;
static volatile bool _forcePoll = false;
static volatile bool _dataReady = false;

/* --- FreeRTOS primitives --- */
static SemaphoreHandle_t _dataMutex = nullptr;
static TaskHandle_t      _taskHandle = nullptr;

static constexpr uint32_t NET_TASK_STACK = 8192;
static constexpr UBaseType_t NET_TASK_PRIORITY = 1;
static constexpr BaseType_t NET_TASK_CORE = 0;
static constexpr TickType_t MUTEX_WAIT = pdMS_TO_TICKS(50);

/* --- HTTP timeout (covers connect + response, but not DNS) --- */
static constexpr int HTTP_TIMEOUT_MS = 8000;

/**
 * Perform one HTTP GET + JSON parse cycle.
 * Runs on Core 0 — allowed to block.
 */
static void doFetch() {
    if (WiFi.status() != WL_CONNECTED) {
        _state = FetchState::ERROR;
        _lastError = "WiFi not connected";
        return;
    }

    _state = FetchState::FETCHING;
    HTTPClient http;

    String url = String("http://") + _url + "/api/dashboard";
    Serial.printf("DATA: [Core %d] fetching %s\n", xPortGetCoreID(), url.c_str());

    http.begin(url);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    int code = http.GET();

    if (code == HTTP_CODE_OK) {
        String payload = http.getString();

        /* Lock mutex to write to shared _doc */
        if (xSemaphoreTake(_dataMutex, MUTEX_WAIT) == pdTRUE) {
            _doc.clear();
            DeserializationError err = deserializeJson(_doc, payload);

            if (err) {
                xSemaphoreGive(_dataMutex);
                _state = FetchState::ERROR;
                _lastError = String("JSON: ") + err.c_str();
                Serial.printf("DATA: parse error — %s\n", err.c_str());
            } else {
                _state = FetchState::SUCCESS;
                _lastSuccessMs = millis();
                _lastError = "";
                _dataReady = true;  /* Signal main loop */
                xSemaphoreGive(_dataMutex);
                Serial.printf("DATA: OK — %d bytes, %d keys\n",
                              payload.length(), _doc.size());
            }
        } else {
            /* Couldn't acquire mutex — skip this cycle */
            Serial.println("DATA: mutex timeout, skipping update");
        }
    } else {
        _state = FetchState::ERROR;
        _lastError = String("HTTP ") + String(code);
        Serial.printf("DATA: HTTP error %d\n", code);
    }

    http.end();
}

/**
 * Background network task — runs forever on Core 0.
 * Handles HTTP polling and periodic NTP re-sync.
 */
static void networkTask(void* param) {
    Serial.printf("DATA: network task started on Core %d\n", xPortGetCoreID());

    /* Initial delay — let WiFi connect before first fetch */
    vTaskDelay(pdMS_TO_TICKS(5000));

    for (;;) {
        /* Fetch bridge data */
        doFetch();

        /* NTP re-sync (non-critical, runs in background) */
        NtpTime::backgroundResync();

        /* Log heap for monitoring */
        Serial.printf("DATA: heap=%lu PSRAM=%lu\n",
                      ESP.getFreeHeap(), ESP.getFreePsram());

        /* Sleep until next poll (or wake early on forcePoll) */
        uint32_t sleepMs = _intervalMs;
        if (_forcePoll) {
            _forcePoll = false;
            sleepMs = 100;  /* Short delay to avoid tight loop */
        }
        vTaskDelay(pdMS_TO_TICKS(sleepMs));
    }
}

/* --- Public API --- */

void DataService::init(const char* bridgeUrl, uint16_t pollIntervalSec) {
    strncpy(_url, bridgeUrl, sizeof(_url) - 1);
    _intervalMs = (uint32_t)pollIntervalSec * 1000;
    _dataMutex = xSemaphoreCreateMutex();
    Serial.printf("DATA: init bridge=%s interval=%ds\n", _url, pollIntervalSec);
}

void DataService::startTask() {
    if (_taskHandle != nullptr) return;  /* Already running */

    xTaskCreatePinnedToCore(
        networkTask,
        "net",
        NET_TASK_STACK,
        nullptr,
        NET_TASK_PRIORITY,
        &_taskHandle,
        NET_TASK_CORE
    );
    Serial.println("DATA: background task launched on Core 0");
}

void DataService::checkReady() {
    if (!_dataReady) return;

    /* New data available — lock, fire callback, clear flag */
    if (xSemaphoreTake(_dataMutex, MUTEX_WAIT) == pdTRUE) {
        if (_callback) _callback(_doc);
        _dataReady = false;
        xSemaphoreGive(_dataMutex);
    }
    /* If mutex unavailable, try again next loop iteration (5ms) */
}

void DataService::forcePoll() {
    _forcePoll = true;
}

FetchState DataService::state() { return _state; }

JsonDocument& DataService::data() { return _doc; }

uint32_t DataService::lastFetchMs() { return _lastSuccessMs; }

String DataService::lastError() { return _lastError; }

void DataService::onData(DataCallback cb) { _callback = cb; }

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
#include "hal/network_hal.h"
#include "hal/platform_hal.h"
#include "logger.h"
#include "ntp_time.h"
#include "ha_command.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdio>
#include <esp_heap_caps.h>

/* --- PSRAM allocator for ArduinoJson --- */
struct SpiRamAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) override {
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    }
    void deallocate(void* ptr) override { free(ptr); }
    void* reallocate(void* ptr, size_t new_size) override {
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
    }
};

static SpiRamAllocator psramAllocator;
static JsonDocument _doc(&psramAllocator);

/* --- Configuration --- */
static char         _url[256] = {};
static uint32_t     _intervalMs = 30000;
static char         _chipId[13] = {};    // 12 hex chars + null
static char         _deviceKey[48] = {}; // pre-shared auth token

/* --- State (written by net task, read by main loop) --- */
static volatile FetchState _state = FetchState::IDLE;
static char         _lastError[64] = {};
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
static constexpr uint32_t HTTP_TIMEOUT_MS = 8000;

/**
 * Perform one HTTP GET + JSON parse cycle.
 * Runs on Core 0 — allowed to block.
 */
static void doFetch() {
    if (!hal::network::isConnected()) {
        _state = FetchState::ERROR;
        snprintf(_lastError, sizeof(_lastError), "WiFi not connected");
        return;
    }

    _state = FetchState::FETCHING;

    /* Build URL */
    char fullUrl[384];
    snprintf(fullUrl, sizeof(fullUrl), "http://%s/api/dashboard", _url);
    LOG_INFO("DATA: [Core %d] fetching %s", xPortGetCoreID(), fullUrl);

    /* Build headers */
    const char* hdrs[4] = {};
    char idHdr[64] = {};
    char keyHdr[96] = {};
    int hIdx = 0;

    if (_chipId[0] != '\0') {
        snprintf(idHdr, sizeof(idHdr), "X-Device-ID: %s", _chipId);
        hdrs[hIdx++] = idHdr;
    }
    if (_deviceKey[0] != '\0') {
        snprintf(keyHdr, sizeof(keyHdr), "X-Device-Key: %s", _deviceKey);
        hdrs[hIdx++] = keyHdr;
    }
    hdrs[hIdx] = nullptr;

    hal::network::HttpResponse resp = hal::network::httpGet(
        fullUrl, hIdx > 0 ? hdrs : nullptr, HTTP_TIMEOUT_MS);

    if (resp.status == 200 && resp.body) {
        /* Lock mutex to write to shared _doc */
        if (xSemaphoreTake(_dataMutex, MUTEX_WAIT) == pdTRUE) {
            _doc.clear();
            DeserializationError err = deserializeJson(_doc, resp.body, resp.bodyLen);

            if (err) {
                xSemaphoreGive(_dataMutex);
                _state = FetchState::ERROR;
                snprintf(_lastError, sizeof(_lastError), "JSON: %s", err.c_str());
                LOG_ERROR("DATA: parse error — %s", err.c_str());
            } else {
                _state = FetchState::SUCCESS;
                _lastSuccessMs = hal::platform::uptimeMs();
                _lastError[0] = '\0';
                _dataReady = true;  /* Signal main loop */
                xSemaphoreGive(_dataMutex);
                LOG_INFO("DATA: OK — %lu bytes, %d keys",
                         (unsigned long)resp.bodyLen, _doc.size());
            }
        } else {
            /* Couldn't acquire mutex — skip this cycle */
            LOG_WARN("DATA: mutex timeout, skipping update");
        }
    } else {
        _state = FetchState::ERROR;
        if (resp.error[0] != '\0') {
            snprintf(_lastError, sizeof(_lastError), "%s", resp.error);
        } else {
            snprintf(_lastError, sizeof(_lastError), "HTTP %d", resp.status);
        }
        LOG_ERROR("DATA: %s", _lastError);
    }

    hal::network::httpResponseFree(&resp);
}

/**
 * Background network task — runs forever on Core 0.
 * Handles HTTP polling and periodic NTP re-sync.
 */
static void networkTask(void* param) {
    LOG_INFO("DATA: network task started on Core %d", xPortGetCoreID());

    /* Initial delay — let WiFi connect before first fetch */
    vTaskDelay(pdMS_TO_TICKS(5000));

    for (;;) {
        doFetch();
        NtpTime::backgroundResync();

        /* Process any pending HA commands immediately */
        HACommand::processQueue();

        LOG_DEBUG("DATA: heap=%lu PSRAM=%lu",
                  hal::platform::heapFree(), hal::platform::psramFree());

        uint32_t sleepMs = _intervalMs;
        if (_forcePoll) {
            _forcePoll = false;
            sleepMs = 100;
        }

        /* During sleep, wake periodically to check for commands */
        uint32_t slept = 0;
        while (slept < sleepMs) {
            uint32_t chunk = (sleepMs - slept > 200) ? 200 : (sleepMs - slept);
            vTaskDelay(pdMS_TO_TICKS(chunk));
            slept += chunk;

            /* Check for commands between polls — provides <200ms latency */
            if (HACommand::processQueue()) {
                LOG_INFO("DATA: processed command during poll sleep");
            }
            if (_forcePoll) {
                _forcePoll = false;
                break;  /* exit sleep early for forced poll */
            }
        }
    }
}

/* --- Public API --- */

void DataService::init(const char* bridgeUrl, uint16_t pollIntervalSec,
                       const char* deviceKey) {
    strncpy(_url, bridgeUrl, sizeof(_url) - 1);
    _intervalMs = (uint32_t)pollIntervalSec * 1000;
    _dataMutex = xSemaphoreCreateMutex();

    /* Use chip ID from network HAL (populated during hal::network::init()) */
    strncpy(_chipId, hal::network::chipId(), sizeof(_chipId) - 1);

    if (deviceKey && deviceKey[0] != '\0') {
        strncpy(_deviceKey, deviceKey, sizeof(_deviceKey) - 1);
    }

    LOG_INFO("DATA: init bridge=%s interval=%ds chipId=%s",
             _url, pollIntervalSec, _chipId);
}

const char* DataService::chipId() { return _chipId; }

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
    LOG_INFO("DATA: background task launched on Core 0");
}

void DataService::checkReady() {
    /* Non-blocking mutex try — provides memory barrier for dual-core
       cache coherency. Reading _dataReady inside the critical section
       prevents stale-cache reads between Core 0 (writer) and Core 1. */
    if (xSemaphoreTake(_dataMutex, 0) == pdTRUE) {
        if (_dataReady && _callback) {
            _callback(_doc);
            _dataReady = false;
        }
        xSemaphoreGive(_dataMutex);
    }
}

void DataService::forcePoll() {
    _forcePoll = true;
}

FetchState DataService::state() { return _state; }

JsonDocument& DataService::data() { return _doc; }

uint32_t DataService::lastFetchMs() { return _lastSuccessMs; }

const char* DataService::lastError() { return _lastError; }

void DataService::onData(DataCallback cb) { _callback = cb; }

/**
 * HA Command — FreeRTOS queue + HTTP POST implementation.
 *
 * Core 1 (LVGL) enqueues commands via send().
 * Core 0 (network task) calls processQueue() to dequeue and
 * send HTTP POST to bridge /api/ha/command.
 * Core 1 (main loop) calls checkResult() to fire the callback.
 */

#include "ha_command.h"
#include "hal/network_hal.h"
#include "logger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdio>

/* ── Command structure (sent through queue) ── */
struct HACmd {
    char entityId[64];
    char service[32];
    char dataJson[128];  // optional JSON payload
};

/* ── Result structure (written by net task, read by main loop) ── */
struct HACmdResult {
    bool success;
    char entityId[64];
    char newState[32];
    char error[96];
};

/* ── State ── */
static char _bridgeUrl[256] = {};
static QueueHandle_t _cmdQueue = nullptr;
static SemaphoreHandle_t _resultMutex = nullptr;
static volatile bool _busy = false;
static volatile bool _resultReady = false;
static HACmdResult _result = {};
static HACommand::ResultCallback _callback = nullptr;

static constexpr uint32_t HTTP_CMD_TIMEOUT_MS = 5000;

/**
 * Extract a JSON string value for a given key from a raw JSON string.
 * Writes up to maxLen-1 chars into dest. Returns true if found.
 * Simple — no nested objects or escaped quotes.
 */
static bool extractJsonString(const char* json, const char* key,
                              char* dest, size_t maxLen) {
    /* Search for "key":"value" */
    char pattern[80];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char* start = strstr(json, pattern);
    if (!start) {
        /* Try with space: "key": "value" */
        snprintf(pattern, sizeof(pattern), "\"%s\": \"", key);
        start = strstr(json, pattern);
    }
    if (!start) return false;

    start += strlen(pattern);
    const char* end = strchr(start, '"');
    if (!end) return false;

    size_t len = end - start;
    if (len >= maxLen) len = maxLen - 1;
    memcpy(dest, start, len);
    dest[len] = '\0';
    return true;
}

void HACommand::init(const char* bridgeUrl) {
    strncpy(_bridgeUrl, bridgeUrl, sizeof(_bridgeUrl) - 1);
    _cmdQueue = xQueueCreate(1, sizeof(HACmd));
    _resultMutex = xSemaphoreCreateMutex();
    LOG_INFO("HACMD: init bridge=%s", _bridgeUrl);
}

bool HACommand::send(const char* entityId, const char* service,
                     const char* dataJson) {
    if (_busy) {
        LOG_WARN("HACMD: busy — command rejected");
        return false;
    }

    HACmd cmd = {};
    strncpy(cmd.entityId, entityId, sizeof(cmd.entityId) - 1);
    strncpy(cmd.service, service, sizeof(cmd.service) - 1);
    if (dataJson) {
        strncpy(cmd.dataJson, dataJson, sizeof(cmd.dataJson) - 1);
    }

    if (xQueueSend(_cmdQueue, &cmd, 0) != pdTRUE) {
        LOG_WARN("HACMD: queue full");
        return false;
    }

    _busy = true;
    LOG_INFO("HACMD: queued %s → %s", entityId, service);
    return true;
}

bool HACommand::processQueue() {
    HACmd cmd;
    if (xQueueReceive(_cmdQueue, &cmd, 0) != pdTRUE) {
        return false;  // no command pending
    }

    LOG_INFO("HACMD: [Core %d] processing %s → %s",
             xPortGetCoreID(), cmd.entityId, cmd.service);

    HACmdResult res = {};
    strncpy(res.entityId, cmd.entityId, sizeof(res.entityId) - 1);

    if (!hal::network::isConnected()) {
        res.success = false;
        strncpy(res.error, "WiFi not connected", sizeof(res.error) - 1);
    } else {
        char url[384];
        snprintf(url, sizeof(url), "http://%s/api/ha/command", _bridgeUrl);

        /* Build JSON body */
        char body[256];
        if (cmd.dataJson[0]) {
            snprintf(body, sizeof(body),
                     "{\"entity_id\":\"%s\",\"service\":\"%s\",\"data\":%s}",
                     cmd.entityId, cmd.service, cmd.dataJson);
        } else {
            snprintf(body, sizeof(body),
                     "{\"entity_id\":\"%s\",\"service\":\"%s\"}",
                     cmd.entityId, cmd.service);
        }

        hal::network::HttpResponse resp = hal::network::httpPost(
            url, body, "application/json", nullptr, HTTP_CMD_TIMEOUT_MS);

        if (resp.status == 200 && resp.body) {
            /* Quick parse — extract "success" and "state" from response */
            res.success = (strstr(resp.body, "\"success\":true") != nullptr)
                       || (strstr(resp.body, "\"success\": true") != nullptr);

            extractJsonString(resp.body, "state", res.newState,
                              sizeof(res.newState));

            if (!res.success) {
                if (!extractJsonString(resp.body, "error", res.error,
                                       sizeof(res.error))) {
                    strncpy(res.error, "Unknown error",
                            sizeof(res.error) - 1);
                }
            }
            LOG_INFO("HACMD: response code=%d success=%d state=%s",
                     resp.status, res.success, res.newState);
        } else {
            res.success = false;
            if (resp.error[0] != '\0') {
                snprintf(res.error, sizeof(res.error), "%s", resp.error);
            } else {
                snprintf(res.error, sizeof(res.error), "HTTP %d", resp.status);
            }
            LOG_ERROR("HACMD: %s", res.error);
        }
        hal::network::httpResponseFree(&resp);
    }

    /* Store result and signal main loop */
    if (xSemaphoreTake(_resultMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _result = res;
        _resultReady = true;
        xSemaphoreGive(_resultMutex);
    }

    return true;
}

void HACommand::checkResult() {
    if (!_resultReady) return;

    if (xSemaphoreTake(_resultMutex, 0) == pdTRUE) {
        if (_resultReady && _callback) {
            _callback(_result.success, _result.entityId,
                      _result.newState, _result.error);
            _resultReady = false;
            _busy = false;
        }
        xSemaphoreGive(_resultMutex);
    }
}

bool HACommand::isBusy() { return _busy; }

void HACommand::onResult(ResultCallback cb) { _callback = cb; }

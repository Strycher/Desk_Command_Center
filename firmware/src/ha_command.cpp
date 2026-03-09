/**
 * HA Command — FreeRTOS queue + HTTP POST implementation.
 *
 * Core 1 (LVGL) enqueues commands via send().
 * Core 0 (network task) calls processQueue() to dequeue and
 * send HTTP POST to bridge /api/ha/command.
 * Core 1 (main loop) calls checkResult() to fire the callback.
 */

#include "ha_command.h"
#include "logger.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

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

static constexpr int HTTP_CMD_TIMEOUT_MS = 5000;

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

    if (WiFi.status() != WL_CONNECTED) {
        res.success = false;
        strncpy(res.error, "WiFi not connected", sizeof(res.error) - 1);
    } else {
        HTTPClient http;
        String url = String("http://") + _bridgeUrl + "/api/ha/command";
        http.begin(url);
        http.setTimeout(HTTP_CMD_TIMEOUT_MS);
        http.setConnectTimeout(HTTP_CMD_TIMEOUT_MS);
        http.addHeader("Content-Type", "application/json");

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

        int code = http.POST(body);

        if (code == HTTP_CODE_OK) {
            String payload = http.getString();
            /* Quick parse — extract "success" and "state" from response */
            res.success = payload.indexOf("\"success\":true") >= 0
                       || payload.indexOf("\"success\": true") >= 0;

            /* Extract new state from "state":"<value>" */
            int stateIdx = payload.indexOf("\"state\":\"");
            if (stateIdx >= 0) {
                stateIdx += 9;  // skip past "state":"
                int endIdx = payload.indexOf("\"", stateIdx);
                if (endIdx > stateIdx) {
                    int len = endIdx - stateIdx;
                    if (len >= (int)sizeof(res.newState))
                        len = sizeof(res.newState) - 1;
                    payload.substring(stateIdx, endIdx).toCharArray(
                        res.newState, sizeof(res.newState));
                }
            }

            if (!res.success) {
                /* Extract error message */
                int errIdx = payload.indexOf("\"error\":\"");
                if (errIdx >= 0) {
                    errIdx += 9;
                    int endIdx = payload.indexOf("\"", errIdx);
                    if (endIdx > errIdx) {
                        payload.substring(errIdx, endIdx).toCharArray(
                            res.error, sizeof(res.error));
                    }
                } else {
                    strncpy(res.error, "Unknown error",
                            sizeof(res.error) - 1);
                }
            }
            LOG_INFO("HACMD: response code=%d success=%d state=%s",
                     code, res.success, res.newState);
        } else {
            res.success = false;
            snprintf(res.error, sizeof(res.error), "HTTP %d", code);
            LOG_ERROR("HACMD: HTTP error %d", code);
        }
        http.end();
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

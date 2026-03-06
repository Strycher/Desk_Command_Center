/**
 * Data Service — Background HTTP polling of bridge API.
 *
 * Network I/O runs on a FreeRTOS task pinned to Core 0.
 * The main loop on Core 1 never blocks on network calls.
 *
 * Usage:
 *   setup(): DataService::init(url, interval);
 *            DataService::onData(callback);
 *            DataService::startTask();
 *
 *   loop():  DataService::checkReady();  // fires callback if new data
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class FetchState : uint8_t { IDLE, FETCHING, SUCCESS, ERROR };

typedef void (*DataCallback)(JsonDocument& doc);

namespace DataService {
    void init(const char* bridgeUrl, uint16_t pollIntervalSec);

    /** Launch background FreeRTOS task on Core 0. Call once from setup(). */
    void startTask();

    /** Check if new data is ready. If so, fires the onData callback.
     *  Call from loop() — runs on Core 1, safe for LVGL updates. */
    void checkReady();

    /** Request an immediate poll (next cycle, not blocking). */
    void forcePoll();

    FetchState state();
    JsonDocument& data();
    uint32_t lastFetchMs();
    String lastError();
    void onData(DataCallback cb);
}

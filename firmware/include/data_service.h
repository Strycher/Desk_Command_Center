/**
 * Data Service — Async HTTP polling of bridge API.
 * Non-blocking: call poll() from loop().
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class FetchState : uint8_t { IDLE, FETCHING, SUCCESS, ERROR };

typedef void (*DataCallback)(JsonDocument& doc);

namespace DataService {
    void init(const char* bridgeUrl, uint16_t pollIntervalSec);
    void poll();
    void forcePoll();
    FetchState state();
    JsonDocument& data();
    uint32_t lastFetchMs();
    String lastError();
    void onData(DataCallback cb);
}

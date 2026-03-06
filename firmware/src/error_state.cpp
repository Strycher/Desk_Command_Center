/**
 * Error State Manager — Implementation
 * Tracks WiFi + bridge connectivity with auto-recovery backoff.
 */

#include "error_state.h"

static bool     s_wifiConnected    = false;
static bool     s_bridgeReachable  = false;
static bool     s_hasCachedData    = false;
static uint16_t s_failureCount     = 0;
static uint32_t s_lastSuccessMs    = 0;
static uint32_t s_lastRetryMs      = 0;
static char     s_lastError[64]    = "";

/* Exponential backoff: 5s, 10s, 20s, 40s, 60s max */
static constexpr uint32_t BASE_RETRY_MS = 5000;
static constexpr uint32_t MAX_RETRY_MS  = 60000;

void ErrorState::init() {
    s_wifiConnected   = false;
    s_bridgeReachable = false;
    s_hasCachedData   = false;
    s_failureCount    = 0;
    s_lastSuccessMs   = 0;
    s_lastRetryMs     = 0;
    s_lastError[0]    = '\0';
}

void ErrorState::setWifiConnected(bool connected) {
    s_wifiConnected = connected;
    if (!connected) {
        s_bridgeReachable = false;
    }
}

void ErrorState::setBridgeReachable(bool reachable) {
    s_bridgeReachable = reachable;
}

void ErrorState::recordSuccess() {
    s_lastSuccessMs   = millis();
    s_failureCount    = 0;
    s_bridgeReachable = true;
    s_lastError[0]    = '\0';
}

void ErrorState::recordFailure(const char* reason) {
    s_failureCount++;
    s_bridgeReachable = false;
    s_lastRetryMs     = millis();
    if (reason) {
        strncpy(s_lastError, reason, sizeof(s_lastError) - 1);
        s_lastError[sizeof(s_lastError) - 1] = '\0';
    }
}

ConnState ErrorState::state() {
    if (!s_wifiConnected) return ConnState::OFFLINE;
    if (!s_bridgeReachable) {
        return s_failureCount == 0 ? ConnState::CONNECTING : ConnState::DEGRADED;
    }
    return ConnState::CONNECTED;
}

const char* ErrorState::stateStr() {
    switch (state()) {
        case ConnState::CONNECTED:  return "Connected";
        case ConnState::CONNECTING: return "Connecting";
        case ConnState::DEGRADED:   return "Bridge unreachable";
        case ConnState::OFFLINE:    return "Offline";
    }
    return "?";
}

const char* ErrorState::lastError() {
    return s_lastError;
}

uint16_t ErrorState::failureCount() {
    return s_failureCount;
}

bool ErrorState::hasCachedData() {
    return s_hasCachedData;
}

void ErrorState::setCachedDataAvailable(bool available) {
    s_hasCachedData = available;
}

uint32_t ErrorState::secondsSinceLastSuccess() {
    if (s_lastSuccessMs == 0) return UINT32_MAX;
    return (millis() - s_lastSuccessMs) / 1000;
}

bool ErrorState::shouldRetry() {
    if (state() == ConnState::CONNECTED) return true;
    if (!s_wifiConnected) return false;

    uint32_t backoff = BASE_RETRY_MS;
    for (uint16_t i = 1; i < s_failureCount && i < 5; i++) {
        backoff *= 2;
    }
    if (backoff > MAX_RETRY_MS) backoff = MAX_RETRY_MS;

    return (millis() - s_lastRetryMs) >= backoff;
}

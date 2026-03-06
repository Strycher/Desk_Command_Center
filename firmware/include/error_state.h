/**
 * Error State Manager — Connection and data health tracking.
 * Manages WiFi/bridge connectivity state and offline mode.
 */

#pragma once
#include <Arduino.h>

enum class ConnState : uint8_t {
    CONNECTED,      // WiFi + bridge both reachable
    CONNECTING,     // WiFi connecting or bridge first attempt
    DEGRADED,       // WiFi OK but bridge unreachable
    OFFLINE         // No WiFi
};

namespace ErrorState {

    void init();

    /** Update WiFi connectivity state. */
    void setWifiConnected(bool connected);

    /** Update bridge reachability (call after each poll attempt). */
    void setBridgeReachable(bool reachable);

    /** Record a successful data fetch. */
    void recordSuccess();

    /** Record a failed data fetch with error info. */
    void recordFailure(const char* reason);

    /** Get current connection state. */
    ConnState state();

    /** Get human-readable state string. */
    const char* stateStr();

    /** Get last error reason (empty if none). */
    const char* lastError();

    /** Get consecutive failure count. */
    uint16_t failureCount();

    /** Whether we have any cached data to display. */
    bool hasCachedData();

    /** Mark that we have valid cached data available. */
    void setCachedDataAvailable(bool available);

    /** Seconds since last successful data fetch. */
    uint32_t secondsSinceLastSuccess();

    /** Whether auto-recovery should be attempted now. */
    bool shouldRetry();
}

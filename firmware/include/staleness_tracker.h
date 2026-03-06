/**
 * Staleness Tracker — Per-source freshness monitoring.
 * Tracks last_updated timestamps and classifies data freshness.
 */

#pragma once
#include <Arduino.h>

enum class Freshness : uint8_t { FRESH, AGING, STALE, UNKNOWN };

enum class DataSource : uint8_t {
    GOOGLE_CALENDAR,
    MICROSOFT_CALENDAR,
    WEATHER,
    UNFOCUSED_TASKS,
    MONDAY_TASKS,
    GITHUB,
    HOME_ASSISTANT,
    BEADS,
    CLAUDE,
    _COUNT
};

namespace StalenessTracker {

    /**
     * Initialize with default thresholds.
     * aging_ms: time after which data becomes "aging" (default 2 min)
     * stale_ms: time after which data becomes "stale" (default 5 min)
     */
    void init(uint32_t aging_ms = 120000, uint32_t stale_ms = 300000);

    /** Record that a source was just updated. */
    void markUpdated(DataSource source);

    /** Get freshness classification for a source. */
    Freshness getFreshness(DataSource source);

    /** Get milliseconds since last update for a source. */
    uint32_t getAge(DataSource source);

    /** Check if any source is stale. */
    bool anyStale();

    /** Check if all sources with data are fresh. */
    bool allFresh();

    /** Get overall freshness (worst of all tracked sources). */
    Freshness overallFreshness();

    /** Get display string for freshness state. */
    const char* freshnessStr(Freshness f);
}

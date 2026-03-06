/**
 * Staleness Tracker — Implementation
 * Uses millis() for timing. Sources start as UNKNOWN until first update.
 */

#include "staleness_tracker.h"

static uint32_t s_lastUpdated[static_cast<uint8_t>(DataSource::_COUNT)];
static bool     s_hasData[static_cast<uint8_t>(DataSource::_COUNT)];
static uint32_t s_agingMs;
static uint32_t s_staleMs;

void StalenessTracker::init(uint32_t aging_ms, uint32_t stale_ms) {
    s_agingMs = aging_ms;
    s_staleMs = stale_ms;
    for (uint8_t i = 0; i < static_cast<uint8_t>(DataSource::_COUNT); i++) {
        s_lastUpdated[i] = 0;
        s_hasData[i] = false;
    }
}

void StalenessTracker::markUpdated(DataSource source) {
    uint8_t idx = static_cast<uint8_t>(source);
    s_lastUpdated[idx] = millis();
    s_hasData[idx] = true;
}

Freshness StalenessTracker::getFreshness(DataSource source) {
    uint8_t idx = static_cast<uint8_t>(source);
    if (!s_hasData[idx]) return Freshness::UNKNOWN;

    uint32_t age = millis() - s_lastUpdated[idx];
    if (age >= s_staleMs) return Freshness::STALE;
    if (age >= s_agingMs) return Freshness::AGING;
    return Freshness::FRESH;
}

uint32_t StalenessTracker::getAge(DataSource source) {
    uint8_t idx = static_cast<uint8_t>(source);
    if (!s_hasData[idx]) return UINT32_MAX;
    return millis() - s_lastUpdated[idx];
}

bool StalenessTracker::anyStale() {
    for (uint8_t i = 0; i < static_cast<uint8_t>(DataSource::_COUNT); i++) {
        if (s_hasData[i] && getFreshness(static_cast<DataSource>(i)) == Freshness::STALE) {
            return true;
        }
    }
    return false;
}

bool StalenessTracker::allFresh() {
    for (uint8_t i = 0; i < static_cast<uint8_t>(DataSource::_COUNT); i++) {
        if (s_hasData[i] && getFreshness(static_cast<DataSource>(i)) != Freshness::FRESH) {
            return false;
        }
    }
    return true;
}

Freshness StalenessTracker::overallFreshness() {
    Freshness worst = Freshness::FRESH;
    bool anyData = false;
    for (uint8_t i = 0; i < static_cast<uint8_t>(DataSource::_COUNT); i++) {
        if (!s_hasData[i]) continue;
        anyData = true;
        Freshness f = getFreshness(static_cast<DataSource>(i));
        if (static_cast<uint8_t>(f) > static_cast<uint8_t>(worst)) {
            worst = f;
        }
    }
    return anyData ? worst : Freshness::UNKNOWN;
}

const char* StalenessTracker::freshnessStr(Freshness f) {
    switch (f) {
        case Freshness::FRESH:   return "Fresh";
        case Freshness::AGING:   return "Aging";
        case Freshness::STALE:   return "Stale";
        case Freshness::UNKNOWN: return "Unknown";
    }
    return "?";
}

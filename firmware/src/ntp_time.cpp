/**
 * NTP Time Sync — Implementation
 * Uses ESP32 configTzTime() / esp_sntp for SNTP + POSIX timezone.
 */

#include "ntp_time.h"
#include "hal/platform_hal.h"
#include <time.h>
#include <stdlib.h>
#include <cstring>
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "logger.h"

/* configTzTime() — available on Arduino ESP32 globally,
   on ESP-IDF we implement via esp_sntp API */
#if defined(CROWPANEL_P4)
#include "esp_sntp.h"
static void doConfigTzTime(const char* tz, const char* s1, const char* s2) {
    setenv("TZ", tz, 1);
    tzset();
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, s1);
    esp_sntp_setservername(1, s2);
    esp_sntp_init();
}
#else
/* Arduino ESP32 provides configTzTime() via esp32-hal.h */
#include <Arduino.h>
static inline void doConfigTzTime(const char* tz, const char* s1, const char* s2) {
    configTzTime(tz, s1, s2);
}
#endif

static bool         _synced = false;
static uint32_t     _lastSyncMs = 0;
static constexpr uint32_t RESYNC_INTERVAL_MS = 24UL * 60 * 60 * 1000; // 24 hours

static const char* NTP_SERVER1 = "pool.ntp.org";
static const char* NTP_SERVER2 = "time.nist.gov";

/**
 * Read current time via time()/localtime_r() directly.
 * Avoids ESP32 Arduino getLocalTime(0) race condition where
 * timeout=0 can fail if millis() ticks between two internal calls.
 */
static bool safeLocalTime(struct tm* out) {
    time_t now = time(nullptr);
    if (now < 1000000000) return false;  /* NTP not synced yet */
    localtime_r(&now, out);
    return true;
}

void NtpTime::init(const char* timezone) {
    doConfigTzTime(timezone, NTP_SERVER1, NTP_SERVER2);
    LOG_INFO("NTP: init tz=%s", timezone);
}

void NtpTime::check() {
    if (!_synced) {
        struct tm timeinfo;
        if (safeLocalTime(&timeinfo)) {
            _synced = true;
            _lastSyncMs = hal::platform::uptimeMs();
            LOG_INFO("NTP: first sync — %04d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        }
    }
}

void NtpTime::backgroundResync() {
    if (!_synced) return;
    if (hal::platform::uptimeMs() - _lastSyncMs < RESYNC_INTERVAL_MS) return;

    const char* tz = getenv("TZ");
    doConfigTzTime(tz ? tz : "UTC0", NTP_SERVER1, NTP_SERVER2);
    _lastSyncMs = hal::platform::uptimeMs();
    LOG_INFO("NTP: [Core %d] re-sync triggered", xPortGetCoreID());
}

bool NtpTime::isSynced() {
    return _synced;
}

/* Cached time strings — avoids repeated formatting and LVGL label
 * invalidation when the display text hasn't actually changed. */
static char _cachedTime[16] = "--:--";
static char _cachedDate[24] = "---";
static int  _cachedMin  = -1;  /* minute of last time format */
static int  _cachedMday = -1;  /* day of last date format */
static bool _cached24h  = false;

const char* NtpTime::timeStr(bool h24) {
    struct tm timeinfo;
    if (!safeLocalTime(&timeinfo)) return _cachedTime;

    /* Only reformat when minute or 24h mode changes */
    if (timeinfo.tm_min != _cachedMin || h24 != _cached24h) {
        _cachedMin = timeinfo.tm_min;
        _cached24h = h24;
        if (h24) {
            snprintf(_cachedTime, sizeof(_cachedTime), "%02d:%02d",
                     timeinfo.tm_hour, timeinfo.tm_min);
        } else {
            int h = timeinfo.tm_hour % 12;
            if (h == 0) h = 12;
            const char* ampm = (timeinfo.tm_hour < 12) ? "AM" : "PM";
            snprintf(_cachedTime, sizeof(_cachedTime), "%d:%02d %s",
                     h, timeinfo.tm_min, ampm);
        }
    }
    return _cachedTime;
}

const char* NtpTime::dateStr() {
    struct tm timeinfo;
    if (!safeLocalTime(&timeinfo)) return _cachedDate;

    /* Only reformat when day changes */
    if (timeinfo.tm_mday != _cachedMday) {
        _cachedMday = timeinfo.tm_mday;

        static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

        snprintf(_cachedDate, sizeof(_cachedDate), "%s, %s %d",
                 days[timeinfo.tm_wday], months[timeinfo.tm_mon], timeinfo.tm_mday);
    }
    return _cachedDate;
}

time_t NtpTime::now() {
    return time(nullptr);
}

void NtpTime::setTimezone(const char* tz) {
    setenv("TZ", tz, 1);
    tzset();
    LOG_INFO("NTP: timezone updated to %s", tz);
}

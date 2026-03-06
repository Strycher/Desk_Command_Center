/**
 * NTP Time Sync — Implementation
 * Uses ESP32 configTzTime() for SNTP + POSIX timezone.
 */

#include "ntp_time.h"
#include <time.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>

static bool         _synced = false;
static uint32_t     _lastSyncMs = 0;
static constexpr uint32_t RESYNC_INTERVAL_MS = 6UL * 60 * 60 * 1000; // 6 hours

static const char* NTP_SERVER1 = "pool.ntp.org";
static const char* NTP_SERVER2 = "time.nist.gov";

void NtpTime::init(const char* timezone) {
    configTzTime(timezone, NTP_SERVER1, NTP_SERVER2);
    Serial.printf("NTP: init tz=%s\n", timezone);
}

void NtpTime::check() {
    /* Main loop only — detect first successful sync.
     * getLocalTime with timeout=0 is non-blocking. */
    if (!_synced) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0)) {
            _synced = true;
            _lastSyncMs = millis();
            Serial.printf("NTP: first sync — %04d-%02d-%02d %02d:%02d:%02d\n",
                          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        }
    }
    /* Re-sync is handled by backgroundResync() on Core 0 */
}

void NtpTime::backgroundResync() {
    /* Called from network task on Core 0 — safe to block. */
    if (!_synced) return;
    if (millis() - _lastSyncMs < RESYNC_INTERVAL_MS) return;

    const char* tz = getenv("TZ");
    configTzTime(tz ? tz : "UTC0", NTP_SERVER1, NTP_SERVER2);
    _lastSyncMs = millis();
    Serial.printf("NTP: [Core %d] re-sync triggered\n", xPortGetCoreID());
}

bool NtpTime::isSynced() {
    return _synced;
}

String NtpTime::timeStr(bool h24) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) return String("--:--");

    char buf[16];
    if (h24) {
        snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    } else {
        int h = timeinfo.tm_hour % 12;
        if (h == 0) h = 12;
        const char* ampm = (timeinfo.tm_hour < 12) ? "AM" : "PM";
        snprintf(buf, sizeof(buf), "%d:%02d %s", h, timeinfo.tm_min, ampm);
    }
    return String(buf);
}

String NtpTime::dateStr() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) return String("---");

    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    char buf[24];
    snprintf(buf, sizeof(buf), "%s, %s %d",
             days[timeinfo.tm_wday], months[timeinfo.tm_mon], timeinfo.tm_mday);
    return String(buf);
}

time_t NtpTime::now() {
    return time(nullptr);
}

void NtpTime::setTimezone(const char* tz) {
    setenv("TZ", tz, 1);
    tzset();
    Serial.printf("NTP: timezone updated to %s\n", tz);
}

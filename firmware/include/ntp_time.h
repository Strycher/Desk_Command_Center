/**
 * NTP Time Sync — SNTP with POSIX timezone.
 * Call init() after WiFi connects. Re-syncs every 6 hours.
 */

#pragma once
#include <Arduino.h>

namespace NtpTime {
    void init(const char* timezone);
    void check();                       // Called from loop — handles re-sync
    bool isSynced();
    String timeStr(bool h24 = false);   // "10:42 AM" or "10:42"
    String dateStr();                   // "Wed, Mar 5"
    time_t now();
    void setTimezone(const char* tz);
}

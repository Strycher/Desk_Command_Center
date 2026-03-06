/**
 * NTP Time Sync — SNTP with POSIX timezone.
 * Call init() after WiFi connects.
 *
 * check() is safe for the main loop (non-blocking).
 * backgroundResync() is called from the network task on Core 0.
 */

#pragma once
#include <Arduino.h>

namespace NtpTime {
    void init(const char* timezone);
    void check();                       // Main loop — first-sync detection only
    bool isSynced();
    String timeStr(bool h24 = false);   // "10:42 AM" or "10:42"
    String dateStr();                   // "Wed, Mar 5"
    time_t now();
    void setTimezone(const char* tz);

    /** Called from network task (Core 0). Safe to block. */
    void backgroundResync();
}

/**
 * Time Helpers — multi-timezone formatting (issue #257).
 *
 * Used by the home screen to display Arizona / Pacific time alongside
 * the local (Eastern) clock without disturbing the process-wide TZ
 * setting that NtpTime owns.
 *
 * Single-thread use only (LVGL UI thread). setenv/tzset is not
 * thread-safe; callers must not invoke this concurrently with other
 * code that reads localtime.
 */

#pragma once
#include <stddef.h>
#include <time.h>

namespace TimeHelpers {
    /**
     * Format the current wall-clock time in the given POSIX TZ string,
     * as 12-hour "H:MM AM/PM" (matches NtpTime::timeStr style — no
     * leading zero on hour).
     *
     * Temporarily switches the process TZ, formats, then restores the
     * previous TZ. If the system clock is not yet synced (epoch ~1970),
     * the formatter still produces a value — caller is expected to
     * gate display via NtpTime::isSynced().
     *
     * @param posixTz POSIX TZ string, e.g. "MST7", "PST8PDT,M3.2.0,M11.1.0"
     * @param outBuf  caller-allocated buffer
     * @param bufLen  size of outBuf (recommend >= 12 bytes)
     */
    void formatNow12h(const char* posixTz, char* outBuf, size_t bufLen);

    /** POSIX TZ strings for the zones the home screen displays. */
    inline constexpr const char* TZ_ARIZONA = "MST7";
    inline constexpr const char* TZ_PACIFIC = "PST8PDT,M3.2.0,M11.1.0";
}

/**
 * Time Helpers implementation — see time_helpers.h for contract.
 */

#include "time_helpers.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void TimeHelpers::formatNow12h(const char* posixTz, char* outBuf, size_t bufLen)
{
    if (!outBuf || bufLen == 0 || !posixTz) return;

    /* Save current process TZ so we can restore it. */
    const char* prevTz = getenv("TZ");
    char savedTz[64] = {0};
    bool hadPrev = false;
    if (prevTz) {
        strncpy(savedTz, prevTz, sizeof(savedTz) - 1);
        hadPrev = true;
    }

    /* Switch TZ, format, restore. */
    setenv("TZ", posixTz, 1);
    tzset();

    time_t now = time(nullptr);
    struct tm tmInTz;
    localtime_r(&now, &tmInTz);

    int h = tmInTz.tm_hour % 12;
    if (h == 0) h = 12;
    const char* ampm = (tmInTz.tm_hour < 12) ? "AM" : "PM";
    snprintf(outBuf, bufLen, "%d:%02d %s", h, tmInTz.tm_min, ampm);

    if (hadPrev) {
        setenv("TZ", savedTz, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
}

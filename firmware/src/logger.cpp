/**
 * Structured Logger — Implementation
 *
 * Ring buffer: 4KB circular FIFO with portMUX spinlock.
 * SD logging:  512B buffer, 5s flush, 48h retention with grace mode.
 * Serial:      Direct Serial.print (ESP32 Arduino is thread-safe).
 *
 * Adapted from Field Compass (C:\Dev\Field_Compass).
 */

#include "logger.h"
#include "sd_manager.h"
#include "ntp_time.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>

/* ═══════════════════════════════════════════════════════════
 *  Ring Buffer — 4KB SRAM, spinlock-protected
 * ═══════════════════════════════════════════════════════════ */

static constexpr uint16_t RING_SIZE = 4096;
static char               s_ring[RING_SIZE];
static volatile uint16_t  s_ringHead = 0;
static volatile uint16_t  s_ringTail = 0;
static portMUX_TYPE       s_ringMux  = portMUX_INITIALIZER_UNLOCKED;

/* ═══════════════════════════════════════════════════════════
 *  SD Log Buffer — 512B SRAM, FreeRTOS mutex
 * ═══════════════════════════════════════════════════════════ */

static constexpr uint16_t SD_BUF_SIZE          = 512;
static constexpr uint32_t FLUSH_INTERVAL_MS    = 5000;      /* 5 seconds */
static constexpr uint32_t ROTATE_INTERVAL_MS   = 3600000;   /* 1 hour */
static constexpr uint32_t RETENTION_HOURS      = 48;
#define LOG_DIR "/logs"

static char              s_sdBuf[SD_BUF_SIZE];
static uint16_t          s_sdBufPos       = 0;
static bool              s_sdLogActive    = false;
static char              s_logFilename[48];    /* "/logs/serial_YYYYMMDD_HHMMSS.log" */
static File              s_logFile;            /* kept open for session */
static SemaphoreHandle_t s_sdMutex        = nullptr;
static uint32_t          s_lastFlushMs    = 0;
static uint32_t          s_lastRotationMs = 0;

/* Forward declarations (internal) */
static void sdFlushInternal();
static void sdRotate();

/* ═══════════════════════════════════════════════════════════
 *  Init / Lifecycle
 * ═══════════════════════════════════════════════════════════ */

void Logger::init() {
    s_ringHead = 0;
    s_ringTail = 0;
    s_sdBufPos = 0;
    s_sdLogActive = false;
    s_sdMutex = xSemaphoreCreateMutex();
}

void Logger::initSDLog() {
    if (!SDManager::isMounted()) {
        Serial.println("[LOG] SD not mounted — file logging disabled");
        return;
    }

    /* Ensure /logs directory exists */
    SDManager::mkdir(LOG_DIR);

    /* Build filename from NTP time (or uptime fallback) */
    struct tm ti;
    time_t now = time(nullptr);
    if (NtpTime::isSynced() && now >= 1000000000 && (localtime_r(&now, &ti), true)) {
        snprintf(s_logFilename, sizeof(s_logFilename),
                 LOG_DIR "/serial_%04d%02d%02d_%02d%02d%02d.log",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        unsigned long s = millis() / 1000;
        snprintf(s_logFilename, sizeof(s_logFilename),
                 LOG_DIR "/serial_boot_%lu.log", s);
    }

    /* Run smart rotation before opening new file */
    sdRotate();

    /* Open log file for append (kept open for session) */
    s_logFile = SDManager::openAppend(s_logFilename);
    if (!s_logFile) {
        Serial.printf("[LOG] Failed to open %s\n", s_logFilename);
        return;
    }

    s_sdLogActive = true;
    s_sdBufPos = 0;
    s_lastFlushMs = millis();
    s_lastRotationMs = millis();
    Serial.printf("[LOG] Logging to %s\n", s_logFilename);
}

void Logger::tick() {
    if (!s_sdLogActive) return;

    uint32_t now = millis();

    /* Flush SD buffer every 5 seconds */
    if (now - s_lastFlushMs >= FLUSH_INTERVAL_MS) {
        flushSD();
    }

    /* Check rotation every hour */
    if (now - s_lastRotationMs >= ROTATE_INTERVAL_MS) {
        sdRotate();
        s_lastRotationMs = now;
    }
}

/* ═══════════════════════════════════════════════════════════
 *  logPrintf — Triple-destination, thread-safe
 * ═══════════════════════════════════════════════════════════ */

void Logger::logPrintf(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* 1. Ring buffer (spinlock — safe from either core + ISR) */
    portENTER_CRITICAL(&s_ringMux);
    const char* p = buf;
    while (*p) {
        s_ring[s_ringHead] = *p++;
        s_ringHead = (s_ringHead + 1) % RING_SIZE;
        if (s_ringHead == s_ringTail) {
            s_ringTail = (s_ringTail + 1) % RING_SIZE;
        }
    }
    portEXIT_CRITICAL(&s_ringMux);

    /* 2. SD buffer (mutex — may block briefly, safe from either core) */
    if (s_sdLogActive && s_sdMutex) {
        if (xSemaphoreTake(s_sdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            const char* q = buf;
            while (*q) {
                s_sdBuf[s_sdBufPos++] = *q++;
                if (s_sdBufPos >= SD_BUF_SIZE) {
                    sdFlushInternal();
                }
            }
            xSemaphoreGive(s_sdMutex);
        }
        /* If mutex timeout, silently skip SD copy (don't block caller) */
    }

    /* 3. USB Serial (ESP32 Arduino Serial is thread-safe) */
    Serial.print(buf);
}

/* ═══════════════════════════════════════════════════════════
 *  Timestamp — per-core buffer (lock-free)
 * ═══════════════════════════════════════════════════════════ */

const char* Logger::timestamp() {
    /* Two buffers — one per core — so simultaneous LOG_* calls
       from Core 0 (DataService) and Core 1 (loop) don't collide. */
    static char tsBuf[2][16];
    int core = xPortGetCoreID();

    struct tm ti;
    time_t now = time(nullptr);
    if (NtpTime::isSynced() && now >= 1000000000 && (localtime_r(&now, &ti), true)) {
        snprintf(tsBuf[core], sizeof(tsBuf[core]), "[%02d:%02d:%02d] ",
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        unsigned long s = millis() / 1000;
        snprintf(tsBuf[core], sizeof(tsBuf[core]), "[%03lu:%02lu:%02lu] ",
                 s / 3600, (s % 3600) / 60, s % 60);
    }
    return tsBuf[core];
}

/* ═══════════════════════════════════════════════════════════
 *  Ring Buffer — Read access (for WebSerial)
 * ═══════════════════════════════════════════════════════════ */

uint16_t Logger::ringHead() {
    portENTER_CRITICAL(&s_ringMux);
    uint16_t h = s_ringHead;
    portEXIT_CRITICAL(&s_ringMux);
    return h;
}

String Logger::ringReadIncremental(uint16_t& readPos) {
    String out;
    out.reserve(1024);

    portENTER_CRITICAL(&s_ringMux);

    uint16_t head = s_ringHead;
    uint16_t tail = s_ringTail;

    /* If readPos fell behind tail (data was overwritten), jump to tail */
    uint16_t dist = (head >= readPos)
                        ? (head - readPos)
                        : (RING_SIZE - readPos + head);
    if (dist > RING_SIZE - 100) {
        readPos = tail;
    }

    /* Read available data (max ~1024 chars to avoid large responses) */
    uint16_t count = 0;
    while (readPos != head && count < 1024) {
        char c = s_ring[readPos];
        readPos = (readPos + 1) % RING_SIZE;
        if (c != '\r') {       /* strip carriage returns */
            out += c;
            count++;
        }
    }

    portEXIT_CRITICAL(&s_ringMux);
    return out;
}

/* ═══════════════════════════════════════════════════════════
 *  SD — Flush / Rotate
 * ═══════════════════════════════════════════════════════════ */

void Logger::flushSD() {
    if (!s_sdLogActive || !s_sdMutex) return;
    if (xSemaphoreTake(s_sdMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        sdFlushInternal();
        xSemaphoreGive(s_sdMutex);
    }
    s_lastFlushMs = millis();
}

/** Internal flush — must be called with s_sdMutex held. */
static void sdFlushInternal() {
    if (s_sdBufPos == 0) return;
    if (s_logFile) {
        size_t written = s_logFile.write((uint8_t*)s_sdBuf, s_sdBufPos);
        if (written != (size_t)s_sdBufPos) {
            /* Write failure — reopen and retry once */
            s_logFile.close();
            s_logFile = SDManager::openAppend(s_logFilename);
            if (s_logFile) {
                s_logFile.write((uint8_t*)s_sdBuf, s_sdBufPos);
            } else {
                s_sdLogActive = false;
                Serial.println("[LOG] SD write failed — logging disabled");
            }
        }
        s_logFile.flush();
    }
    s_sdBufPos = 0;
}

/* ── Rotation scratch space (file-static so captureless lambda can access) ── */
struct RotEntry {
    char name[48];
    time_t ts;
};
static RotEntry  s_rotEntries[32];
static int       s_rotCount    = 0;
static time_t    s_rotNewest   = 0;

/** listDir callback — collect log file timestamps. */
static void rotCollect(const char* name, size_t) {
    if (s_rotCount >= 32) return;
    int yr, mo, dy, hr, mn, sc;
    if (sscanf(name, "serial_%4d%2d%2d_%2d%2d%2d.log",
               &yr, &mo, &dy, &hr, &mn, &sc) == 6) {
        struct tm t = {};
        t.tm_year = yr - 1900;
        t.tm_mon  = mo - 1;
        t.tm_mday = dy;
        t.tm_hour = hr;
        t.tm_min  = mn;
        t.tm_sec  = sc;
        time_t logTime = mktime(&t);
        snprintf(s_rotEntries[s_rotCount].name,
                 sizeof(s_rotEntries[s_rotCount].name),
                 LOG_DIR "/%s", name);
        s_rotEntries[s_rotCount].ts = logTime;
        if (logTime > s_rotNewest) s_rotNewest = logTime;
        s_rotCount++;
    }
}

/**
 * Smart log rotation — adapted from Field Compass.
 * Normal mode: delete logs older than RETENTION_HOURS.
 * Grace mode:  if newest log is older than RETENTION_HOURS,
 *              keep everything (device was off a long time).
 */
static void sdRotate() {
    if (!SDManager::isMounted()) return;
    if (!SDManager::exists(LOG_DIR)) return;

    /* Collect log file timestamps */
    s_rotCount  = 0;
    s_rotNewest = 0;

    time_t now;
    time(&now);

    SDManager::listDir(LOG_DIR, rotCollect);

    if (s_rotCount == 0) return;

    /* Grace mode: if newest log is very old, keep all files */
    double hoursSinceNewest = (s_rotNewest > 0)
                                  ? difftime(now, s_rotNewest) / 3600.0
                                  : 0;
    if (hoursSinceNewest >= (double)RETENTION_HOURS) {
        Serial.printf("[LOG] Grace mode: last log %.0fh old, keeping all\n",
                      hoursSinceNewest);
        return;
    }

    /* Normal mode: delete files older than RETENTION_HOURS */
    time_t cutoff = now - ((time_t)RETENTION_HOURS * 3600);
    int deleted = 0;
    for (int i = 0; i < s_rotCount; i++) {
        if (s_rotEntries[i].ts < cutoff) {
            SD.remove(s_rotEntries[i].name);
            deleted++;
        }
    }
    if (deleted > 0) {
        Serial.printf("[LOG] Rotation: deleted %d files older than %luh\n",
                      deleted, (unsigned long)RETENTION_HOURS);
    }
}

/**
 * Structured Logger — Triple-destination logging for DCC.
 *
 * logPrintf → Ring buffer (web serial) + SD card + USB Serial
 * Adapted from Field Compass logging pattern for dual-core ESP32-S3.
 *
 * Thread safety:
 *   Ring buffer  — portMUX spinlock (ISR-safe, both cores)
 *   SD buffer    — FreeRTOS mutex (may block, both cores)
 *   timestamp()  — per-core static buffer (lock-free)
 */

#pragma once
#include <Arduino.h>

/* ── Log level constants ── */
#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

/* Default: INFO (override with -DLOG_LEVEL=N in platformio.ini) */
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

namespace Logger {

    /** Init ring buffer + spinlock. Call right after Serial.begin(). */
    void init();

    /** Open session log file on SD. Call after SDManager::init() + NTP. */
    void initSDLog();

    /** Periodic: flush SD buffer (5s) + rotation check (1h). Call from loop(). */
    void tick();

    /** Triple-destination log. Thread-safe across both cores. */
    void logPrintf(const char* fmt, ...);

    /** Timestamp prefix: "[HH:MM:SS] " (NTP) or "[HHH:MM:SS] " (uptime). */
    const char* timestamp();

    /** Force flush SD buffer (call before serving /logs). */
    void flushSD();

    /* ── Ring buffer access (for WebSerial) ── */

    /** Read new data since readPos. Updates readPos. Max 1024 chars. */
    String ringReadIncremental(uint16_t& readPos);

    /** Current ring head position (init a new web client's readPos). */
    uint16_t ringHead();
}

/* ── Severity macros (compile-time filtered) ── */

#if LOG_LEVEL >= LOG_LEVEL_ERROR
  #define LOG_ERROR(fmt, ...) \
      Logger::logPrintf("%sERROR " fmt "\n", Logger::timestamp(), ##__VA_ARGS__)
#else
  #define LOG_ERROR(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
  #define LOG_WARN(fmt, ...) \
      Logger::logPrintf("%sWARN  " fmt "\n", Logger::timestamp(), ##__VA_ARGS__)
#else
  #define LOG_WARN(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
  #define LOG_INFO(fmt, ...) \
      Logger::logPrintf("%sINFO  " fmt "\n", Logger::timestamp(), ##__VA_ARGS__)
#else
  #define LOG_INFO(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
  #define LOG_DEBUG(fmt, ...) \
      Logger::logPrintf("%sDEBUG " fmt "\n", Logger::timestamp(), ##__VA_ARGS__)
#else
  #define LOG_DEBUG(fmt, ...) ((void)0)
#endif

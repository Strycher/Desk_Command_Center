/**
 * SD Card Manager — Mount, detect, read/write.
 * Shares SPI bus with microphone — mutual exclusion required.
 * DIP switch: S1=1, S0=0 for SD mode.
 *
 * S3-only: P4 CrowPanel doesn't expose SD. Header is includable
 * on P4 but all functions are no-ops (init returns false).
 */

#pragma once
#include <stdint.h>
#include <stddef.h>

#if !defined(CROWPANEL_P4)
#include <Arduino.h>
#include <SD.h>
#endif

namespace SDManager {

    bool init();
    bool isMounted();
    uint32_t totalMB();
    uint32_t usedMB();
    bool appendLog(const char* filename, const char* text);
    bool writeFile(const char* path, const char* content);
    void unmount();

    bool mkdir(const char* path);
    bool exists(const char* path);
    void listDir(const char* path, void (*cb)(const char* name, size_t size));

#if !defined(CROWPANEL_P4)
    String readFile(const char* path);
    File openRead(const char* path);
    File openAppend(const char* path);
#endif
}

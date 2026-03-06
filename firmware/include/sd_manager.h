/**
 * SD Card Manager — Mount, detect, read/write.
 * Shares SPI bus with microphone — mutual exclusion required.
 * DIP switch: S1=1, S0=0 for SD mode.
 */

#pragma once
#include <Arduino.h>

namespace SDManager {

    /** Attempt to mount SD card. Returns true if successful. */
    bool init();

    /** Whether an SD card is currently mounted. */
    bool isMounted();

    /** Get total card size in MB. */
    uint32_t totalMB();

    /** Get used space in MB. */
    uint32_t usedMB();

    /** Append text to a log file. Creates file if needed. */
    bool appendLog(const char* filename, const char* text);

    /** Read entire file contents into a String. Max 4KB. */
    String readFile(const char* path);

    /** Write string to a file (overwrite). */
    bool writeFile(const char* path, const char* content);

    /** Unmount SD card safely. */
    void unmount();
}

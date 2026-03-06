/**
 * SD Card Manager — Implementation
 * SPI mode on GPIO 4 (CS), 5 (MOSI), 6 (MISO), 7 (CLK).
 * Note: pin assignment from CrowPanel schematic — verify with hardware.
 */

#include "sd_manager.h"
#include <SD.h>
#include <SPI.h>
#include "logger.h"

/* SD card SPI pins — CrowPanel Advance 5.0" */
static constexpr int8_t SD_CS   = 4;
static constexpr int8_t SD_MOSI = 5;
static constexpr int8_t SD_MISO = 6;
static constexpr int8_t SD_CLK  = 7;

static bool s_mounted = false;
static SPIClass s_spi(HSPI);

bool SDManager::init() {
    s_spi.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

    if (!SD.begin(SD_CS, s_spi, 4000000)) {
        LOG_WARN("SD: mount failed or no card");
        s_mounted = false;
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        LOG_WARN("SD: no card detected");
        s_mounted = false;
        return false;
    }

    const char* typeStr = "UNKNOWN";
    if (cardType == CARD_MMC) typeStr = "MMC";
    else if (cardType == CARD_SD) typeStr = "SD";
    else if (cardType == CARD_SDHC) typeStr = "SDHC";

    LOG_INFO("SD: %s card, %lu MB", typeStr,
             (unsigned long)(SD.totalBytes() / (1024 * 1024)));
    s_mounted = true;
    return true;
}

bool SDManager::isMounted() {
    return s_mounted;
}

uint32_t SDManager::totalMB() {
    if (!s_mounted) return 0;
    return (uint32_t)(SD.totalBytes() / (1024 * 1024));
}

uint32_t SDManager::usedMB() {
    if (!s_mounted) return 0;
    return (uint32_t)(SD.usedBytes() / (1024 * 1024));
}

bool SDManager::appendLog(const char* filename, const char* text) {
    if (!s_mounted) return false;

    File f = SD.open(filename, FILE_APPEND);
    if (!f) {
        LOG_ERROR("SD: failed to open %s for append", filename);
        return false;
    }
    f.println(text);
    f.close();
    return true;
}

String SDManager::readFile(const char* path) {
    if (!s_mounted) return "";

    File f = SD.open(path, FILE_READ);
    if (!f) return "";

    String content;
    size_t maxRead = 4096;
    while (f.available() && content.length() < maxRead) {
        content += (char)f.read();
    }
    f.close();
    return content;
}

bool SDManager::writeFile(const char* path, const char* content) {
    if (!s_mounted) return false;

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        LOG_ERROR("SD: failed to open %s for write", path);
        return false;
    }
    f.print(content);
    f.close();
    return true;
}

void SDManager::unmount() {
    if (s_mounted) {
        SD.end();
        s_mounted = false;
        LOG_INFO("SD: unmounted");
    }
}

/* --- Extensions for logger / web serial --- */

bool SDManager::mkdir(const char* path) {
    if (!s_mounted) return false;
    if (SD.exists(path)) return true;  /* already exists */
    return SD.mkdir(path);
}

bool SDManager::exists(const char* path) {
    if (!s_mounted) return false;
    return SD.exists(path);
}

File SDManager::openRead(const char* path) {
    if (!s_mounted) return File();
    return SD.open(path, FILE_READ);
}

File SDManager::openAppend(const char* path) {
    if (!s_mounted) return File();
    return SD.open(path, FILE_APPEND);
}

void SDManager::listDir(const char* path,
                        void (*cb)(const char* name, size_t size))
{
    if (!s_mounted || !cb) return;

    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            cb(entry.name(), entry.size());
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
}

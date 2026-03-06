/**
 * SD Card Manager — Implementation
 * SPI mode on GPIO 4 (CS), 5 (MOSI), 6 (MISO), 7 (CLK).
 * Note: pin assignment from CrowPanel schematic — verify with hardware.
 */

#include "sd_manager.h"
#include <SD.h>
#include <SPI.h>

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
        Serial.println("SD: mount failed or no card");
        s_mounted = false;
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("SD: no card detected");
        s_mounted = false;
        return false;
    }

    const char* typeStr = "UNKNOWN";
    if (cardType == CARD_MMC) typeStr = "MMC";
    else if (cardType == CARD_SD) typeStr = "SD";
    else if (cardType == CARD_SDHC) typeStr = "SDHC";

    Serial.printf("SD: %s card, %lu MB\n", typeStr,
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
        Serial.printf("SD: failed to open %s for append\n", filename);
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
        Serial.printf("SD: failed to open %s for write\n", path);
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
        Serial.println("SD: unmounted");
    }
}

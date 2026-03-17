/**
 * SD Manager — P4 Stub
 * CrowPanel 7" doesn't expose an SD card slot.
 * All functions return "not available" gracefully.
 */
#if defined(CROWPANEL_P4)

#include "sd_manager.h"

bool SDManager::init() { return false; }
bool SDManager::isMounted() { return false; }
uint32_t SDManager::totalMB() { return 0; }
uint32_t SDManager::usedMB() { return 0; }
bool SDManager::appendLog(const char*, const char*) { return false; }
bool SDManager::writeFile(const char*, const char*) { return false; }
void SDManager::unmount() {}
bool SDManager::mkdir(const char*) { return false; }
bool SDManager::exists(const char*) { return false; }
void SDManager::listDir(const char*, void (*)(const char*, size_t)) {}

#endif  // CROWPANEL_P4

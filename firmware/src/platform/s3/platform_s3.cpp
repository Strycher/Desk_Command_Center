/**
 * Platform HAL — S3 Implementation
 *
 * Thin wrappers around Arduino globals.
 */

#if !defined(CROWPANEL_P4)

#include "hal/platform_hal.h"
#include <Arduino.h>

namespace hal {
namespace platform {

void earlyInit()
{
    Serial.begin(115200);
}

uint32_t uptimeMs()
{
    return millis();
}

void delayMs(uint32_t ms)
{
    delay(ms);
}

uint32_t heapFree()
{
    return ESP.getFreeHeap();
}

uint32_t psramFree()
{
    return ESP.getFreePsram();
}

const char* chipModel()
{
    return "ESP32-S3";
}

void restart()
{
    ESP.restart();
}

}  // namespace platform
}  // namespace hal

#endif  // !CROWPANEL_P4

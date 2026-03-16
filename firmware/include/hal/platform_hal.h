/**
 * Platform HAL — System-level abstractions.
 *
 * Wraps Arduino/ESP-IDF differences for timing, heap queries, and
 * chip identification. Shared code should call these instead of
 * millis(), delay(), ESP.getFreeHeap(), etc.
 *
 * S3 implementation: thin wrappers around Arduino globals
 * P4 implementation: FreeRTOS / esp_system APIs
 */

#pragma once
#include <stdint.h>

namespace hal {
namespace platform {

/**
 * Early platform init — call before anything else.
 *
 * S3:  Serial.begin(115200)
 * P4:  no-op (ESP-IDF configures UART via menuconfig)
 */
void earlyInit();

/** Milliseconds since boot (wraps at ~49 days). */
uint32_t uptimeMs();

/** Blocking delay. Prefer FreeRTOS vTaskDelay in tasks. */
void delayMs(uint32_t ms);

/** Free internal SRAM heap (bytes). */
uint32_t heapFree();

/** Free PSRAM (bytes). Returns 0 if no PSRAM. */
uint32_t psramFree();

/** Human-readable chip model string (e.g., "ESP32-S3", "ESP32-P4"). */
const char* chipModel();

/** Software reset. Does not return. */
void restart();

}  // namespace platform
}  // namespace hal

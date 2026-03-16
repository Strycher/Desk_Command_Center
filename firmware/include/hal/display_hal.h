/**
 * Display HAL — Platform-independent display interface.
 *
 * S3 implementation: LovyanGFX RGB + manual LVGL setup + lv_timer_handler()
 * P4 implementation: MIPI-DSI EK79007 + esp_lvgl_port (managed LVGL task)
 *
 * The key lifecycle difference:
 *   S3:  caller calls tick() every loop iteration (drives lv_timer_handler)
 *   P4:  esp_lvgl_port runs its own FreeRTOS task; tick() is a no-op
 *
 * Thread safety:
 *   All LVGL widget manipulation MUST be wrapped in lock()/unlock().
 *   S3:  lock/unlock are no-ops (single-threaded LVGL on Core 1)
 *   P4:  lock/unlock map to lvgl_port_lock/unlock (LVGL runs on its own task)
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

namespace hal {
namespace display {

/**
 * Initialize display hardware, LVGL, and touch input.
 *
 * S3:  LovyanGFX lcd.begin() + manual LVGL draw buffer + driver registration
 * P4:  LDO power + MIPI-DSI bus + EK79007 panel + esp_lvgl_port
 *
 * After init(), LVGL is ready for widget creation (under lock).
 * Backlight starts OFF — call setBacklight() separately.
 */
bool init();

/**
 * LVGL tick — call from main loop.
 *
 * S3:  calls lv_timer_handler() to process LVGL timers and rendering
 * P4:  no-op (esp_lvgl_port has its own FreeRTOS task)
 */
void tick();

/**
 * Acquire LVGL mutex for safe widget manipulation.
 *
 * S3:  always returns true (no contention — LVGL runs on caller's thread)
 * P4:  blocks up to timeout_ms waiting for esp_lvgl_port mutex
 *
 * @param timeout_ms  Maximum wait (0 = try once, portMAX_DELAY = block forever)
 * @return true if lock acquired
 */
bool lock(uint32_t timeout_ms = 0);

/** Release LVGL mutex. Must be called after lock() returns true. */
void unlock();

/** Set backlight brightness (0 = off, 100 = max). */
void setBacklight(uint8_t percent);

/** Get current backlight brightness (0-100). */
uint8_t getBacklight();

/** Screen dimensions (compile-time constants exposed at runtime). */
uint16_t screenWidth();
uint16_t screenHeight();

}  // namespace display
}  // namespace hal

#pragma once
#include <stdint.h>
#include <stdbool.h>

namespace hal {
namespace display {

/** Initialize display hardware + LVGL. Returns true on success. */
bool init();

/** Set backlight brightness (0 = off, 100 = max). */
void setBacklight(uint8_t percent);

/** Screen dimensions. */
uint16_t screenWidth();
uint16_t screenHeight();

}  // namespace display
}  // namespace hal

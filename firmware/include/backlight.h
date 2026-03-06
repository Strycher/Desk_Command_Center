/**
 * Backlight Control — STC8H1K28 co-processor via I2C.
 * V1.1 board: continuous brightness 0x05 (off) to 0x10 (max).
 */

#pragma once
#include <Arduino.h>

namespace Backlight {
    void init();
    void setBrightness(uint8_t percent);  // 0-100% mapped to hardware range
    uint8_t getBrightness();              // Current percent
}

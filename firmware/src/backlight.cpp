/**
 * Backlight Control — Implementation
 * STC8H1K28 at I2C 0x30. Brightness range: 0x05 (off) to 0x10 (max).
 * Uses the same I2C bus as touch (GPIO 15/16), already initialized by LovyanGFX.
 */

#include "backlight.h"
#include <Wire.h>
#include "pins_config.h"
#include "logger.h"

static constexpr uint8_t BL_ADDR    = BACKLIGHT_I2C_ADDR;
static constexpr uint8_t BL_REG_MIN = 0x05;
static constexpr uint8_t BL_REG_MAX = 0x10;

static uint8_t _currentPercent = 100;

static uint8_t percentToReg(uint8_t percent) {
    if (percent > 100) percent = 100;
    /* Map 0-100% → 0x05-0x10 (11 steps) */
    return BL_REG_MIN + (uint8_t)((uint16_t)percent * (BL_REG_MAX - BL_REG_MIN) / 100);
}

static void writeReg(uint8_t val) {
    Wire.beginTransmission(BL_ADDR);
    Wire.write(val);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        LOG_ERROR("BL: I2C write error %d", err);
    }
}

void Backlight::init() {
    /* I2C bus already started by LovyanGFX touch driver */
    setBrightness(100);
    LOG_INFO("BL: initialized at max brightness");
}

void Backlight::setBrightness(uint8_t percent) {
    _currentPercent = (percent > 100) ? 100 : percent;
    writeReg(percentToReg(_currentPercent));
    LOG_INFO("BL: brightness %d%% (reg=0x%02X)",
             _currentPercent, percentToReg(_currentPercent));
}

uint8_t Backlight::getBrightness() {
    return _currentPercent;
}

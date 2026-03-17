/**
 * Backlight Control — Implementation (ESP32-S3 only)
 * STC8H1K28 at I2C 0x30. Brightness range: 0x05 (off) to 0x10 (max).
 * P4 uses direct PWM via display_driver_p4.cpp instead.
 */

#if !defined(CROWPANEL_P4)

#include "backlight.h"
#include "driver/i2c.h"
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
    /* Use ESP-IDF i2c driver directly — LovyanGFX owns I2C_NUM_0 for
       GT911 touch, and Arduino Wire silently fails on the shared bus. */
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BL_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(TOUCH_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        LOG_ERROR("BL: I2C write 0x%02X error %d (%s)", val, err, esp_err_to_name(err));
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

#endif /* !CROWPANEL_P4 */

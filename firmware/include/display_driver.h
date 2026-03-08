/**
 * CrowPanel Advance 5.0" — LovyanGFX Display Driver
 * ST7262 RGB interface + GT911 capacitive touch
 * Pin mapping from Elecrow factory firmware V1.1
 */

#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <driver/i2c.h>
#include "pins_config.h"

class LGFX : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB     _bus_instance;
    lgfx::Panel_RGB   _panel_instance;

    lgfx::Touch_GT911 _touch_instance;


    LGFX(void) {
        /* --- Panel --- */
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width  = LCD_H_RES;
            cfg.memory_height = LCD_V_RES;
            cfg.panel_width   = LCD_H_RES;
            cfg.panel_height  = LCD_V_RES;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel_instance.config(cfg);
        }

        /* PSRAM for frame buffer */
        {
            auto cfg = _panel_instance.config_detail();
            cfg.use_psram = 1;
            _panel_instance.config_detail(cfg);
        }

        /* --- RGB Bus (16-bit) --- */
        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;

            /* Blue */
            cfg.pin_d0  = GPIO_NUM_21;   // B0
            cfg.pin_d1  = GPIO_NUM_47;   // B1
            cfg.pin_d2  = GPIO_NUM_48;   // B2
            cfg.pin_d3  = GPIO_NUM_45;   // B3
            cfg.pin_d4  = GPIO_NUM_38;   // B4

            /* Green */
            cfg.pin_d5  = GPIO_NUM_9;    // G0
            cfg.pin_d6  = GPIO_NUM_10;   // G1
            cfg.pin_d7  = GPIO_NUM_11;   // G2
            cfg.pin_d8  = GPIO_NUM_12;   // G3
            cfg.pin_d9  = GPIO_NUM_13;   // G4
            cfg.pin_d10 = GPIO_NUM_14;   // G5

            /* Red */
            cfg.pin_d11 = GPIO_NUM_7;    // R0
            cfg.pin_d12 = GPIO_NUM_17;   // R1
            cfg.pin_d13 = GPIO_NUM_18;   // R2
            cfg.pin_d14 = GPIO_NUM_3;    // R3
            cfg.pin_d15 = GPIO_NUM_46;   // R4

            /* Sync */
            cfg.pin_henable = GPIO_NUM_42;
            cfg.pin_vsync   = GPIO_NUM_41;
            cfg.pin_hsync   = GPIO_NUM_40;
            cfg.pin_pclk    = GPIO_NUM_39;
            cfg.freq_write  = LCD_PIXEL_CLOCK_HZ;

            /* Timing */
            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch  = 50;   /* 43→50: extra margin for PSRAM bus contention */
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch  = 12;
            cfg.pclk_active_neg   = 1;
            cfg.de_idle_high      = 0;
            cfg.pclk_idle_high    = 0;

            _bus_instance.config(cfg);
        }
        _panel_instance.setBus(&_bus_instance);

        /* --- Touch (GT911) --- */
        {
            auto cfg = _touch_instance.config();
            cfg.x_min = 0;
            cfg.x_max = LCD_H_RES;
            cfg.y_min = 0;
            cfg.y_max = LCD_V_RES;
            cfg.pin_int  = PIN_TOUCH_INT;
            cfg.pin_rst  = PIN_TOUCH_RST;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port = TOUCH_I2C_PORT;
            cfg.pin_sda  = GPIO_NUM_15;
            cfg.pin_scl  = GPIO_NUM_16;
            cfg.freq     = TOUCH_I2C_FREQ;
            cfg.i2c_addr = TOUCH_I2C_ADDR;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};

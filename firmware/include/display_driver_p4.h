/**
 * CrowPanel 7.0" (ESP32-P4) — MIPI-DSI Display Driver
 * EK79007 panel controller, 1024x600, 2-lane MIPI-DSI
 * GT911 capacitive touch via I2C
 *
 * Uses ESP-IDF esp_lcd API (available through Arduino framework).
 */

#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_ldo_regulator.h>
#include <driver/ledc.h>
#include <Wire.h>

/* ── Pin definitions (CrowPanel 7" P4) ─────────────────────── */
#define P4_TOUCH_SDA       45
#define P4_TOUCH_SCL       46
#define P4_TOUCH_INT       42
#define P4_TOUCH_RST       40
#define P4_BACKLIGHT_PIN   31
#define P4_TOUCH_I2C_ADDR  0x5D

/* ── MIPI-DSI configuration ────────────────────────────────── */
#define P4_DSI_LANE_NUM        2
#define P4_DSI_LANE_BITRATE_MBPS  900
#define P4_DPI_CLK_MHZ        52
#define P4_LCD_H_RES          1024
#define P4_LCD_V_RES          600
#define P4_LCD_HSYNC          1
#define P4_LCD_HBP            160
#define P4_LCD_HFP            160
#define P4_LCD_VSYNC          1
#define P4_LCD_VBP            23
#define P4_LCD_VFP            12

/* ── LDO channels for MIPI PHY power ──────────────────────── */
#define P4_LDO_MIPI_PHY_CHAN  3   /* 2.5V for DPHY */

/* Forward declarations for LVGL integration */
namespace P4Display {

    /* Display panel handle — used by LVGL flush callback */
    extern esp_lcd_panel_handle_t panel;

    /**
     * Initialize MIPI-DSI display:
     *   1. Power LDOs (2.5V DPHY)
     *   2. Create MIPI-DSI bus (2 lanes @ 900 Mbps)
     *   3. Install EK79007 panel driver
     *   4. Init backlight PWM on GPIO31
     *   5. Init GT911 touch via I2C (Wire on GPIO 45/46)
     *
     * Returns true on success.
     */
    bool init();

    /**
     * LVGL flush callback — copies draw buffer to DSI panel.
     */
    void lvglFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p);

    /**
     * LVGL touch read callback — reads GT911 via I2C.
     */
    void lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data);

    /**
     * Set backlight brightness (0-100%).
     */
    void setBacklight(uint8_t percent);
}

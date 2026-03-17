/**
 * UI Scaling — Resolution-independent layout helpers.
 *
 * All UI code was originally written for 800×480 (S3 CrowPanel 5").
 * These helpers scale pixel values proportionally to the actual display
 * resolution, keeping the same visual layout on 1024×600 (P4 CrowPanel 7")
 * or any future resolution.
 *
 * Usage:
 *   lv_obj_set_size(card, SX(380), SY(180));
 *   lv_obj_set_pos(card, SX(10), SY(40));
 *   lv_obj_set_style_pad_all(card, SU(12), 0);  // uniform scale
 *
 * All three are constexpr-safe for use in static constants.
 *
 * NOTE: "SCL" is reserved by Arduino (I2C clock pin). We use SX/SY/SU.
 */

#pragma once
#include "pins_config.h"   /* LCD_H_RES, LCD_V_RES */

/* Reference resolution (S3 CrowPanel 5") — all hardcoded pixels
   in the original UI code target this resolution. */
static constexpr int16_t UI_REF_W = 800;
static constexpr int16_t UI_REF_H = 480;

/* Scale a horizontal pixel value (widths, x-positions, horizontal padding). */
static constexpr int16_t SX(int16_t v) {
    return (int16_t)((int32_t)v * LCD_H_RES / UI_REF_W);
}

/* Scale a vertical pixel value (heights, y-positions, vertical padding). */
static constexpr int16_t SY(int16_t v) {
    return (int16_t)((int32_t)v * LCD_V_RES / UI_REF_H);
}

/* Uniform scale — uses the smaller ratio so content never overflows.
   Good for square elements (radii, icons, padding, shadow, font offsets).
   Uses ternary instead of if-constexpr for C++14 compat (Arduino S3). */
static constexpr int16_t SU(int16_t v) {
    /* min(LCD_H_RES/800, LCD_V_RES/480) — compare cross-multiplied to avoid float */
    return ((int32_t)LCD_H_RES * UI_REF_H <= (int32_t)LCD_V_RES * UI_REF_W)
        ? (int16_t)((int32_t)v * LCD_H_RES / UI_REF_W)
        : (int16_t)((int32_t)v * LCD_V_RES / UI_REF_H);
}

/* Content area geometry — between status bar and nav bar. */
static constexpr int16_t UI_STATUS_BAR_H = SY(30);
static constexpr int16_t UI_NAV_BAR_H    = SY(50);
static constexpr int16_t UI_CONTENT_Y    = UI_STATUS_BAR_H;
static constexpr int16_t UI_CONTENT_H    = LCD_V_RES - UI_STATUS_BAR_H - UI_NAV_BAR_H;

/* Full-width content with standard padding. */
static constexpr int16_t UI_PAD          = SU(10);
static constexpr int16_t UI_CONTENT_W    = LCD_H_RES - 2 * UI_PAD;

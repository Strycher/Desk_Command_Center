/**
 * Weather Icon — Canvas-drawn weather indicators.
 * Uses LVGL canvas to draw simple weather symbols from OWM codes.
 *
 * Icon code format: "NNx" where NN=condition, x=d(ay)/n(ight)
 *   01=clear, 02=few clouds, 03=scattered, 04=broken/overcast,
 *   09=shower rain, 10=rain, 11=thunderstorm, 13=snow, 50=mist
 */

#include "ui/weather_icon.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

static constexpr int16_t ICON_SIZE = 48;
static lv_color_t _iconBuf[ICON_SIZE * ICON_SIZE];

/* --- Colors --- */
static const lv_color_t COL_SUN     = lv_color_hex(0xFFD93D);
static const lv_color_t COL_MOON    = lv_color_hex(0xC4C4E0);
static const lv_color_t COL_CLOUD   = lv_color_hex(0xB0B0C8);
static const lv_color_t COL_CLOUD_D = lv_color_hex(0x808098);
static const lv_color_t COL_RAIN    = lv_color_hex(0x5BA8FF);
static const lv_color_t COL_SNOW    = lv_color_hex(0xE8E8FF);
static const lv_color_t COL_BOLT    = lv_color_hex(0xFFE066);
static const lv_color_t COL_FOG     = lv_color_hex(0x9898B0);
static const lv_color_t COL_BG      = lv_color_hex(0x1a1a2e);

/* --- Drawing helpers --- */

static void fillCircle(lv_obj_t* c, int16_t cx, int16_t cy, int16_t r,
                        lv_color_t color) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.border_width = 0;
    int16_t x = cx - r;
    int16_t y = cy - r;
    int16_t d = r * 2;
    lv_canvas_draw_rect(c, x, y, d, d, &dsc);
}

static void drawLine(lv_obj_t* c, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                      lv_color_t color, int16_t width) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 1;
    dsc.round_end = 1;
    lv_point_t pts[2] = {{x1, y1}, {x2, y2}};
    lv_canvas_draw_line(c, pts, 2, &dsc);
}

static void fillRect(lv_obj_t* c, int16_t x, int16_t y, int16_t w, int16_t h,
                      lv_color_t color, int16_t radius) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = radius;
    dsc.border_width = 0;
    lv_canvas_draw_rect(c, x, y, w, h, &dsc);
}

/* --- Weather shapes --- */

static void drawSun(lv_obj_t* c, int16_t cx, int16_t cy, int16_t r) {
    /* Rays */
    for (int i = 0; i < 8; i++) {
        float angle = i * 3.14159f / 4.0f;
        int16_t ix = cx + (int16_t)((r + 2) * cosf(angle));
        int16_t iy = cy + (int16_t)((r + 2) * sinf(angle));
        int16_t ox = cx + (int16_t)((r + 6) * cosf(angle));
        int16_t oy = cy + (int16_t)((r + 6) * sinf(angle));
        drawLine(c, ix, iy, ox, oy, COL_SUN, 2);
    }
    /* Core */
    fillCircle(c, cx, cy, r, COL_SUN);
}

static void drawMoon(lv_obj_t* c, int16_t cx, int16_t cy, int16_t r) {
    /* Full circle then overlay a dark circle to create crescent */
    fillCircle(c, cx, cy, r, COL_MOON);
    fillCircle(c, cx + r / 2, cy - r / 3, r - 2, COL_BG);
}

static void drawCloud(lv_obj_t* c, int16_t x, int16_t y, lv_color_t col) {
    /* Cloud from overlapping circles + base rect */
    fillCircle(c, x + 10, y + 8, 8, col);
    fillCircle(c, x + 20, y + 4, 10, col);
    fillCircle(c, x + 30, y + 7, 7, col);
    fillRect(c, x + 4, y + 10, 32, 10, col, 4);
}

static void drawRainDrops(lv_obj_t* c, int16_t x, int16_t y) {
    drawLine(c, x + 10, y, x + 8,  y + 6, COL_RAIN, 2);
    drawLine(c, x + 18, y, x + 16, y + 6, COL_RAIN, 2);
    drawLine(c, x + 26, y, x + 24, y + 6, COL_RAIN, 2);
}

static void drawSnowDots(lv_obj_t* c, int16_t x, int16_t y) {
    fillCircle(c, x + 10, y + 2, 2, COL_SNOW);
    fillCircle(c, x + 20, y + 4, 2, COL_SNOW);
    fillCircle(c, x + 28, y + 2, 2, COL_SNOW);
    fillCircle(c, x + 14, y + 8, 2, COL_SNOW);
    fillCircle(c, x + 24, y + 9, 2, COL_SNOW);
}

static void drawLightning(lv_obj_t* c, int16_t x, int16_t y) {
    drawLine(c, x + 20, y,     x + 16, y + 6,  COL_BOLT, 2);
    drawLine(c, x + 16, y + 6, x + 22, y + 6,  COL_BOLT, 2);
    drawLine(c, x + 22, y + 6, x + 18, y + 12, COL_BOLT, 2);
}

static void drawFog(lv_obj_t* c, int16_t x, int16_t y) {
    for (int i = 0; i < 4; i++) {
        drawLine(c, x + 4, y + i * 6, x + 36, y + i * 6, COL_FOG, 2);
    }
}

/* --- Main draw dispatch --- */

static void drawIcon(lv_obj_t* c, int code, bool night) {
    lv_canvas_fill_bg(c, COL_BG, LV_OPA_COVER);

    switch (code) {
        case 1:  /* Clear */
            if (night) drawMoon(c, 24, 22, 12);
            else       drawSun(c, 24, 22, 10);
            break;

        case 2:  /* Few clouds */
            if (night) drawMoon(c, 16, 14, 8);
            else       drawSun(c, 16, 14, 7);
            drawCloud(c, 8, 20, COL_CLOUD);
            break;

        case 3:  /* Scattered clouds */
            drawCloud(c, 4, 8, COL_CLOUD_D);
            drawCloud(c, 8, 18, COL_CLOUD);
            break;

        case 4:  /* Broken/overcast */
            drawCloud(c, 2, 6, COL_CLOUD_D);
            drawCloud(c, 6, 16, COL_CLOUD);
            break;

        case 9:  /* Shower rain */
            drawCloud(c, 6, 8, COL_CLOUD);
            drawRainDrops(c, 6, 26);
            drawRainDrops(c, 2, 34);
            break;

        case 10: /* Rain */
            drawCloud(c, 6, 6, COL_CLOUD_D);
            drawCloud(c, 8, 12, COL_CLOUD);
            drawRainDrops(c, 6, 28);
            break;

        case 11: /* Thunderstorm */
            drawCloud(c, 6, 6, COL_CLOUD_D);
            drawCloud(c, 8, 12, COL_CLOUD);
            drawLightning(c, 4, 28);
            drawRainDrops(c, 6, 36);
            break;

        case 13: /* Snow */
            drawCloud(c, 6, 6, COL_CLOUD);
            drawSnowDots(c, 4, 26);
            break;

        case 50: /* Mist/fog */
            drawCloud(c, 6, 6, COL_CLOUD_D);
            drawFog(c, 4, 26);
            break;

        default: /* Unknown — show a question mark cloud */
            drawCloud(c, 8, 14, COL_CLOUD);
            break;
    }
}

/* --- Public API --- */

lv_obj_t* WeatherIcon::create(lv_obj_t* parent) {
    lv_obj_t* canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, _iconBuf, ICON_SIZE, ICON_SIZE,
                          LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(canvas, COL_BG, LV_OPA_COVER);
    return canvas;
}

void WeatherIcon::update(lv_obj_t* canvas, const char* owmCode) {
    if (!canvas || !owmCode || !owmCode[0]) return;

    int code = atoi(owmCode);
    bool night = (strlen(owmCode) >= 3 && owmCode[2] == 'n');

    drawIcon(canvas, code, night);
    lv_obj_invalidate(canvas);
}

/**
 * Weather Icon — Direct-buffer weather indicators.
 * Renders weather symbols directly to a pixel buffer, bypassing
 * LVGL's canvas draw pipeline which corrupts its internal memory
 * pool when mixing draw types (circles + lines + rects).
 *
 * Uses LVGL canvas only for hosting the buffer and invalidation.
 * All rasterization is done with simple algorithms (midpoint circle,
 * Bresenham line) writing directly to the lv_color_t buffer.
 *
 * Icon code format: "NNx" where NN=condition, x=d(ay)/n(ight)
 *   01=clear, 02=few clouds, 03=scattered, 04=broken/overcast,
 *   09=shower rain, 10=rain, 11=thunderstorm, 13=snow, 50=mist
 */

#include "ui/weather_icon.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <esp_heap_caps.h>
#include "logger.h"

static constexpr int16_t ICON_SIZE = 48;

/* Per-canvas buffer pointer — set before each draw pass.
 * Each canvas owns its own heap-allocated buffer (stored in user_data).
 * This replaces the former shared static buffer that caused corruption
 * when multiple screens (home + weather) both created WeatherIcon canvases. */
static lv_color_t* _curBuf = nullptr;

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

/* ================================================================
 * Direct buffer rendering — no LVGL draw calls
 * ================================================================ */

static inline void bufSetPixel(int16_t x, int16_t y, lv_color_t c) {
    if (x >= 0 && x < ICON_SIZE && y >= 0 && y < ICON_SIZE) {
        _curBuf[y * ICON_SIZE + x] = c;
    }
}

static void bufFillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                         lv_color_t c) {
    /* Clamp to bounds */
    int16_t x0 = (x < 0) ? 0 : x;
    int16_t y0 = (y < 0) ? 0 : y;
    int16_t x1 = (x + w > ICON_SIZE) ? ICON_SIZE : x + w;
    int16_t y1 = (y + h > ICON_SIZE) ? ICON_SIZE : y + h;

    for (int16_t row = y0; row < y1; row++) {
        lv_color_t* p = &_curBuf[row * ICON_SIZE + x0];
        for (int16_t col = x0; col < x1; col++) {
            *p++ = c;
        }
    }
}

static void bufClear(lv_color_t c) {
    for (int i = 0; i < ICON_SIZE * ICON_SIZE; i++) {
        _curBuf[i] = c;
    }
}

/* Filled circle — midpoint algorithm with horizontal scanline fill */
static void bufFillCircle(int16_t cx, int16_t cy, int16_t r, lv_color_t c) {
    if (r <= 0) return;

    /* Fill horizontal spans using midpoint circle to find edges */
    int16_t x = 0;
    int16_t y = r;
    int16_t d = 1 - r;

    /* Draw horizontal lines for each y-offset from center */
    auto hline = [&](int16_t lx, int16_t rx, int16_t ly) {
        if (ly < 0 || ly >= ICON_SIZE) return;
        if (lx < 0) lx = 0;
        if (rx >= ICON_SIZE) rx = ICON_SIZE - 1;
        lv_color_t* p = &_curBuf[ly * ICON_SIZE + lx];
        for (int16_t i = lx; i <= rx; i++) *p++ = c;
    };

    while (x <= y) {
        /* Each (x,y) gives 4 horizontal spans */
        hline(cx - x, cx + x, cy + y);
        hline(cx - x, cx + x, cy - y);
        hline(cx - y, cx + y, cy + x);
        hline(cx - y, cx + y, cy - x);

        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

/* Thick line — Bresenham with perpendicular thickness */
static void bufDrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         lv_color_t c, int16_t width) {
    int16_t dx = abs(x1 - x0);
    int16_t dy = abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;
    int16_t halfW = width / 2;

    for (;;) {
        /* Draw a small filled area around the pixel for line width */
        for (int16_t wy = -halfW; wy <= halfW; wy++) {
            for (int16_t wx = -halfW; wx <= halfW; wx++) {
                bufSetPixel(x0 + wx, y0 + wy, c);
            }
        }

        if (x0 == x1 && y0 == y1) break;

        int16_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* --- Weather shapes (same visual design, direct buffer ops) --- */

static void drawSun(int16_t cx, int16_t cy, int16_t r) {
    /* Rays */
    for (int i = 0; i < 8; i++) {
        float angle = i * 3.14159f / 4.0f;
        int16_t ix = cx + (int16_t)((r + 2) * cosf(angle));
        int16_t iy = cy + (int16_t)((r + 2) * sinf(angle));
        int16_t ox = cx + (int16_t)((r + 6) * cosf(angle));
        int16_t oy = cy + (int16_t)((r + 6) * sinf(angle));
        bufDrawLine(ix, iy, ox, oy, COL_SUN, 2);
    }
    /* Core */
    bufFillCircle(cx, cy, r, COL_SUN);
}

static void drawMoon(int16_t cx, int16_t cy, int16_t r) {
    /* Full circle then overlay a dark circle to create crescent */
    bufFillCircle(cx, cy, r, COL_MOON);
    bufFillCircle(cx + r / 2, cy - r / 3, r - 2, COL_BG);
}

static void drawCloud(int16_t x, int16_t y, lv_color_t col) {
    /* Cloud from overlapping circles + base rect */
    bufFillCircle(x + 10, y + 8, 8, col);
    bufFillCircle(x + 20, y + 4, 10, col);
    bufFillCircle(x + 30, y + 7, 7, col);
    bufFillRect(x + 4, y + 10, 32, 10, col);
}

static void drawRainDrops(int16_t x, int16_t y) {
    bufDrawLine(x + 10, y, x + 8,  y + 6, COL_RAIN, 2);
    bufDrawLine(x + 18, y, x + 16, y + 6, COL_RAIN, 2);
    bufDrawLine(x + 26, y, x + 24, y + 6, COL_RAIN, 2);
}

static void drawSnowDots(int16_t x, int16_t y) {
    bufFillCircle(x + 10, y + 2, 2, COL_SNOW);
    bufFillCircle(x + 20, y + 4, 2, COL_SNOW);
    bufFillCircle(x + 28, y + 2, 2, COL_SNOW);
    bufFillCircle(x + 14, y + 8, 2, COL_SNOW);
    bufFillCircle(x + 24, y + 9, 2, COL_SNOW);
}

static void drawLightning(int16_t x, int16_t y) {
    bufDrawLine(x + 20, y,     x + 16, y + 6,  COL_BOLT, 2);
    bufDrawLine(x + 16, y + 6, x + 22, y + 6,  COL_BOLT, 2);
    bufDrawLine(x + 22, y + 6, x + 18, y + 12, COL_BOLT, 2);
}

static void drawFog(int16_t x, int16_t y) {
    for (int i = 0; i < 4; i++) {
        bufDrawLine(x + 4, y + i * 6, x + 36, y + i * 6, COL_FOG, 2);
    }
}

/* --- Main draw dispatch --- */

static void drawIcon(int code, bool night) {
    bufClear(COL_BG);

    switch (code) {
        case 1:  /* Clear */
            if (night) drawMoon(24, 22, 12);
            else       drawSun(24, 22, 10);
            break;

        case 2:  /* Few clouds */
            if (night) drawMoon(16, 14, 8);
            else       drawSun(16, 14, 7);
            drawCloud(8, 20, COL_CLOUD);
            break;

        case 3:  /* Scattered clouds */
            drawCloud(4, 8, COL_CLOUD_D);
            drawCloud(8, 18, COL_CLOUD);
            break;

        case 4:  /* Broken/overcast */
            drawCloud(2, 6, COL_CLOUD_D);
            drawCloud(6, 16, COL_CLOUD);
            break;

        case 9:  /* Shower rain */
            drawCloud(6, 8, COL_CLOUD);
            drawRainDrops(6, 26);
            drawRainDrops(2, 34);
            break;

        case 10: /* Rain */
            drawCloud(6, 6, COL_CLOUD_D);
            drawCloud(8, 12, COL_CLOUD);
            drawRainDrops(6, 28);
            break;

        case 11: /* Thunderstorm */
            drawCloud(6, 6, COL_CLOUD_D);
            drawCloud(8, 12, COL_CLOUD);
            drawLightning(4, 28);
            drawRainDrops(6, 36);
            break;

        case 13: /* Snow */
            drawCloud(6, 6, COL_CLOUD);
            drawSnowDots(4, 26);
            break;

        case 50: /* Mist/fog */
            drawCloud(6, 6, COL_CLOUD_D);
            drawFog(4, 26);
            break;

        default: /* Unknown — show a question mark cloud */
            drawCloud(8, 14, COL_CLOUD);
            break;
    }
}

/* --- Public API --- */

lv_obj_t* WeatherIcon::create(lv_obj_t* parent) {
    /* Each canvas gets its own buffer so multiple icons can coexist
     * (e.g. home screen + weather screen).  Prefer PSRAM (8 MB free)
     * to keep SRAM headroom for LVGL heap and draw buffers. */
    const size_t bufBytes = ICON_SIZE * ICON_SIZE * sizeof(lv_color_t);
    lv_color_t* buf = (lv_color_t*)heap_caps_malloc(bufBytes,
                                                     MALLOC_CAP_SPIRAM);
    if (!buf) {
        buf = (lv_color_t*)malloc(bufBytes);   /* SRAM fallback */
    }
    if (!buf) {
        LOG_ERROR("WICON: failed to allocate %u B icon buffer", bufBytes);
        return nullptr;
    }

    lv_obj_t* canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, buf, ICON_SIZE, ICON_SIZE,
                          LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_user_data(canvas, buf);         /* stash for update() */

    _curBuf = buf;
    bufClear(COL_BG);
    _curBuf = nullptr;

    lv_obj_invalidate(canvas);
    return canvas;
}

void WeatherIcon::update(lv_obj_t* canvas, const char* owmCode) {
    if (!canvas || !owmCode || !owmCode[0]) return;

    /* Retrieve this canvas's own buffer */
    _curBuf = (lv_color_t*)lv_obj_get_user_data(canvas);
    if (!_curBuf) return;

    /* Parse OWM icon code: "NNd" or "NNn" */
    int code = atoi(owmCode);
    bool night = (strlen(owmCode) >= 3 && owmCode[2] == 'n');

    /* Render directly to this canvas's pixel buffer */
    drawIcon(code, night);

    _curBuf = nullptr;                         /* defensive: no stale ref */

    /* Tell LVGL the canvas content changed */
    lv_obj_invalidate(canvas);
}

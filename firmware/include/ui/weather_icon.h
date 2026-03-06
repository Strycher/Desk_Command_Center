/**
 * Weather Icon — Canvas-drawn weather indicators.
 * Draws simple, attractive weather icons from OWM icon codes.
 * No external assets — all generated from LVGL drawing primitives.
 */

#pragma once
#include <lvgl.h>

namespace WeatherIcon {
    /** Create a 48x48 canvas widget for weather icons. */
    lv_obj_t* create(lv_obj_t* parent);

    /** Redraw the icon for the given OpenWeatherMap icon code (e.g. "01d", "10n"). */
    void update(lv_obj_t* canvas, const char* owmCode);
}

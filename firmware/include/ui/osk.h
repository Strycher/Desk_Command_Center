/**
 * On-Screen Keyboard — LVGL keyboard wrapper.
 * Floats on lv_layer_top(), auto-shows on textarea focus.
 */

#pragma once
#include <lvgl.h>

namespace OSK {

    /** Initialize the keyboard (call once after LVGL init). */
    void init();

    /** Show keyboard and associate with a textarea. */
    void show(lv_obj_t* textarea);

    /** Hide keyboard. */
    void hide();

    /** Whether keyboard is currently visible. */
    bool isVisible();

    /** Get the keyboard object (for custom event handling). */
    lv_obj_t* keyboard();
}

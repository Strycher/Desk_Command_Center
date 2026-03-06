/**
 * On-Screen Keyboard — Implementation
 * Uses lv_keyboard on lv_layer_top() for global accessibility.
 * 800px wide, 200px tall — fits 10 keys at ~80px each.
 */

#include "ui/osk.h"

static lv_obj_t* s_kb      = nullptr;
static lv_obj_t* s_overlay  = nullptr;
static bool      s_visible  = false;

static void onKbEvent(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        OSK::hide();
    }
}

void OSK::init() {
    /* Semi-transparent overlay to dim background */
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_overlay, 800, 480);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    /* LVGL keyboard widget */
    s_kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_kb, 800, 200);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* Style to match theme */
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(0xE0E0FF), 0);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(0x252548),
                               LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(0xE0E0FF),
                                 LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(0x6C63FF),
                               LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_add_event_cb(s_kb, onKbEvent, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_kb, onKbEvent, LV_EVENT_CANCEL, nullptr);

    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
}

void OSK::show(lv_obj_t* textarea) {
    if (!s_kb || !textarea) return;
    lv_keyboard_set_textarea(s_kb, textarea);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_overlay);
    lv_obj_move_foreground(s_kb);
    s_visible = true;
}

void OSK::hide() {
    if (!s_kb) return;
    lv_keyboard_set_textarea(s_kb, nullptr);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
}

bool OSK::isVisible() {
    return s_visible;
}

lv_obj_t* OSK::keyboard() {
    return s_kb;
}

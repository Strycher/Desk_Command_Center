/**
 * Boot Splash Screen — Implementation
 */

#include "ui/splash_screen.h"

static const lv_color_t BG_COLOR    = lv_color_hex(0x0f0f23);
static const lv_color_t TITLE_COLOR = lv_color_hex(0xE0E0FF);
static const lv_color_t STATUS_COLOR = lv_color_hex(0x8888AA);
static const lv_color_t SPINNER_COLOR = lv_color_hex(0x6C63FF);

void SplashScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* Title */
    _lblTitle = lv_label_create(_screen);
    lv_label_set_text(_lblTitle, "Desktop Command Center");
    lv_obj_set_style_text_font(_lblTitle, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lblTitle, TITLE_COLOR, 0);
    lv_obj_align(_lblTitle, LV_ALIGN_CENTER, 0, -40);

    /* Spinner */
    _spinner = lv_spinner_create(_screen, 1000, 60);
    lv_obj_set_size(_spinner, 40, 40);
    lv_obj_align(_spinner, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_arc_color(_spinner, SPINNER_COLOR, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_spinner, lv_color_hex(0x2a2a4a), LV_PART_MAIN);

    /* Status text */
    _lblStatus = lv_label_create(_screen);
    lv_label_set_text(_lblStatus, "Initializing...");
    lv_obj_set_style_text_font(_lblStatus, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblStatus, STATUS_COLOR, 0);
    lv_obj_align(_lblStatus, LV_ALIGN_CENTER, 0, 60);
}

void SplashScreen::update(const DashboardData& data) {
    /* Splash doesn't use dashboard data */
}

void SplashScreen::updateStatus(const char* msg) {
    if (_lblStatus) {
        lv_label_set_text(_lblStatus, msg);
    }
}

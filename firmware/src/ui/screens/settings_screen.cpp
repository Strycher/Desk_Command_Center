/**
 * Settings Screen — Implementation
 * Content area: y=30..430. Scrollable settings groups.
 */

#include "ui/screens/settings_screen.h"
#include "ui/osk.h"
#include "backlight.h"
#include "logger.h"

static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG        = lv_color_hex(0x1a1a2e);
static const lv_color_t TEXT_PRIMARY   = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0xB0B0D0);
static const lv_color_t ACCENT         = lv_color_hex(0x6C63FF);
static const lv_color_t BTN_BG        = lv_color_hex(0x252540);

static constexpr int16_t CONTENT_Y = 30;
static constexpr int16_t PAD       = 10;

/* Common US/world timezones as POSIX TZ strings */
static const char* TZ_LABELS =
    "Eastern (US)\n"
    "Central (US)\n"
    "Mountain (US)\n"
    "Pacific (US)\n"
    "UTC\n"
    "London (GMT)\n"
    "Berlin (CET)\n"
    "Tokyo (JST)\n"
    "Sydney (AEST)";

static const char* TZ_VALUES[] = {
    "EST5EDT,M3.2.0,M11.1.0",
    "CST6CDT,M3.2.0,M11.1.0",
    "MST7MDT,M3.2.0,M11.1.0",
    "PST8PDT,M3.2.0,M11.1.0",
    "UTC0",
    "GMT0BST,M3.5.0/1,M10.5.0",
    "CET-1CEST,M3.5.0,M10.5.0/3",
    "JST-9",
    "AEST-10AEDT,M10.1.0,M4.1.0/3"
};
static constexpr uint8_t TZ_COUNT = 9;

static lv_obj_t* makeCard(lv_obj_t* parent, int16_t w, int16_t h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

/* --- Callbacks --- */

void SettingsScreen::onSaveURL(lv_event_t* e) {
    auto* self = (SettingsScreen*)lv_event_get_user_data(e);
    OSK::hide();
    const char* url = lv_textarea_get_text(self->_taBridgeURL);
    if (url && url[0] != '\0') {
        strncpy(self->_cfg.bridge_url, url, DCC_URL_LEN - 1);
        self->_cfg.bridge_url[DCC_URL_LEN - 1] = '\0';
        ConfigStore::save(self->_cfg);
        lv_label_set_text(self->_lblStatus, "Bridge URL saved");
    }
}

void SettingsScreen::onSaveTZ(lv_event_t* e) {
    auto* self = (SettingsScreen*)lv_event_get_user_data(e);
    uint16_t sel = lv_dropdown_get_selected(self->_ddTimezone);
    if (sel < TZ_COUNT) {
        strncpy(self->_cfg.timezone, TZ_VALUES[sel], DCC_TZ_LEN - 1);
        self->_cfg.timezone[DCC_TZ_LEN - 1] = '\0';
        ConfigStore::save(self->_cfg);
        lv_label_set_text(self->_lblStatus, "Timezone saved — reboot to apply");
    }
}

void SettingsScreen::onBrightnessChanged(lv_event_t* e) {
    auto* self = (SettingsScreen*)lv_event_get_user_data(e);
    int32_t val = lv_slider_get_value(self->_sliderBright);
    self->_cfg.brightness = (uint8_t)val;
    Backlight::setBrightness((uint8_t)val);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(self->_lblBrightVal, buf);
    /* NVS save deferred to onBrightnessRelease — saves block UI for ~100ms */
}

void SettingsScreen::onBrightnessRelease(lv_event_t* e) {
    auto* self = (SettingsScreen*)lv_event_get_user_data(e);
    ConfigStore::save(self->_cfg);
    lv_label_set_text(self->_lblStatus, "Brightness saved");
}

void SettingsScreen::on24hToggle(lv_event_t* e) {
    auto* self = (SettingsScreen*)lv_event_get_user_data(e);
    self->_cfg.clock_24h = lv_obj_has_state(self->_sw24h, LV_STATE_CHECKED);
    ConfigStore::save(self->_cfg);
    lv_label_set_text(self->_lblStatus, "Clock format saved");
}

void SettingsScreen::onSavePoll(lv_event_t* e) {
    auto* self = (SettingsScreen*)lv_event_get_user_data(e);
    OSK::hide();
    const char* txt = lv_textarea_get_text(self->_taPollSec);
    int val = atoi(txt);
    if (val >= 5 && val <= 3600) {
        self->_cfg.poll_interval_sec = (uint16_t)val;
        ConfigStore::save(self->_cfg);
        lv_label_set_text(self->_lblStatus, "Poll interval saved");
    } else {
        lv_label_set_text(self->_lblStatus, "Range: 5-3600 seconds");
    }
}

void SettingsScreen::onURLFocus(lv_event_t* e) {
    OSK::show(lv_event_get_target(e));
}

void SettingsScreen::onPollFocus(lv_event_t* e) {
    OSK::show(lv_event_get_target(e));
}

/* --- Screen creation --- */

void SettingsScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* Scrollable container */
    lv_obj_t* scroll = lv_obj_create(_screen);
    lv_obj_set_size(scroll, 780, 380);
    lv_obj_set_pos(scroll, PAD, CONTENT_Y + 5);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_set_style_pad_row(scroll, 8, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);

    /* Status label */
    _lblStatus = lv_label_create(_screen);
    lv_obj_set_style_text_font(_lblStatus, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblStatus, ACCENT, 0);
    lv_obj_align(_lblStatus, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_label_set_text(_lblStatus, "");

    /* === Bridge URL Card === */
    lv_obj_t* urlCard = makeCard(scroll, 760, 90);

    lv_obj_t* urlHdr = lv_label_create(urlCard);
    lv_label_set_text(urlHdr, "Bridge URL");
    lv_obj_set_style_text_font(urlHdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(urlHdr, TEXT_PRIMARY, 0);
    lv_obj_align(urlHdr, LV_ALIGN_TOP_LEFT, 0, 0);

    _taBridgeURL = lv_textarea_create(urlCard);
    lv_obj_set_size(_taBridgeURL, 600, 36);
    lv_obj_align(_taBridgeURL, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_textarea_set_max_length(_taBridgeURL, DCC_URL_LEN - 1);
    lv_textarea_set_one_line(_taBridgeURL, true);
    lv_textarea_set_placeholder_text(_taBridgeURL, "http://bridge:8000");
    lv_obj_set_style_bg_color(_taBridgeURL, BTN_BG, 0);
    lv_obj_set_style_text_color(_taBridgeURL, TEXT_PRIMARY, 0);
    lv_obj_add_event_cb(_taBridgeURL, onURLFocus, LV_EVENT_FOCUSED, this);

    _btnSaveURL = lv_btn_create(urlCard);
    lv_obj_set_size(_btnSaveURL, 100, 34);
    lv_obj_align(_btnSaveURL, LV_ALIGN_TOP_RIGHT, 0, 26);
    lv_obj_set_style_bg_color(_btnSaveURL, ACCENT, 0);
    lv_obj_set_style_radius(_btnSaveURL, 8, 0);
    lv_obj_t* lblSaveURL = lv_label_create(_btnSaveURL);
    lv_label_set_text(lblSaveURL, "Save");
    lv_obj_set_style_text_color(lblSaveURL, TEXT_PRIMARY, 0);
    lv_obj_center(lblSaveURL);
    lv_obj_add_event_cb(_btnSaveURL, onSaveURL, LV_EVENT_CLICKED, this);

    /* === Timezone Card === */
    lv_obj_t* tzCard = makeCard(scroll, 760, 90);

    lv_obj_t* tzHdr = lv_label_create(tzCard);
    lv_label_set_text(tzHdr, "Timezone");
    lv_obj_set_style_text_font(tzHdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tzHdr, TEXT_PRIMARY, 0);
    lv_obj_align(tzHdr, LV_ALIGN_TOP_LEFT, 0, 0);

    _ddTimezone = lv_dropdown_create(tzCard);
    lv_obj_set_size(_ddTimezone, 300, 36);
    lv_obj_align(_ddTimezone, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_dropdown_set_options(_ddTimezone, TZ_LABELS);
    lv_obj_set_style_bg_color(_ddTimezone, BTN_BG, 0);
    lv_obj_set_style_text_color(_ddTimezone, TEXT_PRIMARY, 0);

    _btnSaveTZ = lv_btn_create(tzCard);
    lv_obj_set_size(_btnSaveTZ, 100, 34);
    lv_obj_align(_btnSaveTZ, LV_ALIGN_TOP_RIGHT, 0, 26);
    lv_obj_set_style_bg_color(_btnSaveTZ, ACCENT, 0);
    lv_obj_set_style_radius(_btnSaveTZ, 8, 0);
    lv_obj_t* lblSaveTZ = lv_label_create(_btnSaveTZ);
    lv_label_set_text(lblSaveTZ, "Save");
    lv_obj_set_style_text_color(lblSaveTZ, TEXT_PRIMARY, 0);
    lv_obj_center(lblSaveTZ);
    lv_obj_add_event_cb(_btnSaveTZ, onSaveTZ, LV_EVENT_CLICKED, this);

    /* === Display Card === */
    lv_obj_t* dispCard = makeCard(scroll, 760, 110);

    lv_obj_t* dispHdr = lv_label_create(dispCard);
    lv_label_set_text(dispHdr, "Display");
    lv_obj_set_style_text_font(dispHdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dispHdr, TEXT_PRIMARY, 0);
    lv_obj_align(dispHdr, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* dispHint = lv_label_create(dispCard);
    lv_label_set_text(dispHint, "(auto-saved)");
    lv_obj_set_style_text_font(dispHint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dispHint, TEXT_SECONDARY, 0);
    lv_obj_align_to(dispHint, dispHdr, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, 0);

    /* Brightness slider */
    lv_obj_t* lblBright = lv_label_create(dispCard);
    lv_label_set_text(lblBright, "Brightness:");
    lv_obj_set_style_text_font(lblBright, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblBright, TEXT_SECONDARY, 0);
    lv_obj_align(lblBright, LV_ALIGN_TOP_LEFT, 0, 28);

    _sliderBright = lv_slider_create(dispCard);
    lv_obj_set_size(_sliderBright, 300, 12);
    lv_obj_align(_sliderBright, LV_ALIGN_TOP_LEFT, 100, 32);
    lv_slider_set_range(_sliderBright, 10, 100);
    lv_obj_set_style_bg_color(_sliderBright, BTN_BG, 0);
    lv_obj_set_style_bg_color(_sliderBright, ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_sliderBright, TEXT_PRIMARY, LV_PART_KNOB);
    lv_obj_add_event_cb(_sliderBright, onBrightnessChanged,
                        LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(_sliderBright, onBrightnessRelease,
                        LV_EVENT_RELEASED, this);

    _lblBrightVal = lv_label_create(dispCard);
    lv_obj_set_style_text_font(_lblBrightVal, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblBrightVal, TEXT_PRIMARY, 0);
    lv_obj_align(_lblBrightVal, LV_ALIGN_TOP_LEFT, 420, 28);
    lv_label_set_text(_lblBrightVal, "--");

    /* 24h toggle */
    lv_obj_t* lbl24h = lv_label_create(dispCard);
    lv_label_set_text(lbl24h, "24-hour clock:");
    lv_obj_set_style_text_font(lbl24h, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl24h, TEXT_SECONDARY, 0);
    lv_obj_align(lbl24h, LV_ALIGN_TOP_LEFT, 0, 60);

    _sw24h = lv_switch_create(dispCard);
    lv_obj_align(_sw24h, LV_ALIGN_TOP_LEFT, 130, 56);
    lv_obj_set_style_bg_color(_sw24h, BTN_BG, 0);
    lv_obj_set_style_bg_color(_sw24h, ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(_sw24h, on24hToggle, LV_EVENT_VALUE_CHANGED, this);

    /* === Poll Interval Card === */
    lv_obj_t* pollCard = makeCard(scroll, 760, 90);

    lv_obj_t* pollHdr = lv_label_create(pollCard);
    lv_label_set_text(pollHdr, "Poll Interval");
    lv_obj_set_style_text_font(pollHdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pollHdr, TEXT_PRIMARY, 0);
    lv_obj_align(pollHdr, LV_ALIGN_TOP_LEFT, 0, 0);

    _taPollSec = lv_textarea_create(pollCard);
    lv_obj_set_size(_taPollSec, 120, 36);
    lv_obj_align(_taPollSec, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_textarea_set_max_length(_taPollSec, 4);
    lv_textarea_set_one_line(_taPollSec, true);
    lv_textarea_set_accepted_chars(_taPollSec, "0123456789");
    lv_textarea_set_placeholder_text(_taPollSec, "30");
    lv_obj_set_style_bg_color(_taPollSec, BTN_BG, 0);
    lv_obj_set_style_text_color(_taPollSec, TEXT_PRIMARY, 0);
    lv_obj_add_event_cb(_taPollSec, onPollFocus, LV_EVENT_FOCUSED, this);

    lv_obj_t* lblSec = lv_label_create(pollCard);
    lv_label_set_text(lblSec, "seconds (5-3600)");
    lv_obj_set_style_text_font(lblSec, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblSec, TEXT_SECONDARY, 0);
    lv_obj_align(lblSec, LV_ALIGN_TOP_LEFT, 130, 34);

    _btnSavePoll = lv_btn_create(pollCard);
    lv_obj_set_size(_btnSavePoll, 100, 34);
    lv_obj_align(_btnSavePoll, LV_ALIGN_TOP_RIGHT, 0, 26);
    lv_obj_set_style_bg_color(_btnSavePoll, ACCENT, 0);
    lv_obj_set_style_radius(_btnSavePoll, 8, 0);
    lv_obj_t* lblSavePoll = lv_label_create(_btnSavePoll);
    lv_label_set_text(lblSavePoll, "Save");
    lv_obj_set_style_text_color(lblSavePoll, TEXT_PRIMARY, 0);
    lv_obj_center(lblSavePoll);
    lv_obj_add_event_cb(_btnSavePoll, onSavePoll, LV_EVENT_CLICKED, this);

    LOG_INFO("SETTINGS: screen created");
}

void SettingsScreen::loadSettings() {
    _cfg = ConfigStore::load();

    /* Bridge URL */
    lv_textarea_set_text(_taBridgeURL, _cfg.bridge_url);

    /* Timezone — find matching entry */
    for (uint8_t i = 0; i < TZ_COUNT; i++) {
        if (strcmp(_cfg.timezone, TZ_VALUES[i]) == 0) {
            lv_dropdown_set_selected(_ddTimezone, i);
            break;
        }
    }

    /* Brightness */
    lv_slider_set_value(_sliderBright, _cfg.brightness, LV_ANIM_OFF);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", _cfg.brightness);
    lv_label_set_text(_lblBrightVal, buf);

    /* 24h clock */
    if (_cfg.clock_24h) {
        lv_obj_add_state(_sw24h, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_sw24h, LV_STATE_CHECKED);
    }

    /* Poll interval */
    char pollBuf[8];
    snprintf(pollBuf, sizeof(pollBuf), "%d", _cfg.poll_interval_sec);
    lv_textarea_set_text(_taPollSec, pollBuf);
}

void SettingsScreen::update(const DashboardData& data) {
    /* Settings screen doesn't need dashboard data updates */
}

void SettingsScreen::onShow() {
    loadSettings();
    lv_label_set_text(_lblStatus, "");
}

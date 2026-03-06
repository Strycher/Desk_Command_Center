/**
 * Calendar Screen — Implementation
 * Content area: y=30..430, scrollable event list with color-coded cards.
 */

#include "ui/screens/calendar_screen.h"

static const lv_color_t BG_COLOR     = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG      = lv_color_hex(0x1a1a2e);
static const lv_color_t CARD_HL      = lv_color_hex(0x252548);
static const lv_color_t TEXT_PRIMARY  = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0x8888AA);
static const lv_color_t ACCENT       = lv_color_hex(0x6C63FF);
static const lv_color_t BTN_BG       = lv_color_hex(0x252540);

static constexpr int16_t CONTENT_Y = 30;
static constexpr int16_t NAV_HEIGHT = 50;

static const char* DAY_LABELS[] = {"Today", "Tomorrow", "In 2 days", "In 3 days"};

void CalendarScreen::onPrev(lv_event_t* e) {
    auto* self = (CalendarScreen*)lv_event_get_user_data(e);
    if (self->_dayOffset > 0) {
        self->_dayOffset--;
        self->rebuildEventList();
    }
}

void CalendarScreen::onNext(lv_event_t* e) {
    auto* self = (CalendarScreen*)lv_event_get_user_data(e);
    if (self->_dayOffset < 3) {
        self->_dayOffset++;
        self->rebuildEventList();
    }
}

void CalendarScreen::onToday(lv_event_t* e) {
    auto* self = (CalendarScreen*)lv_event_get_user_data(e);
    self->_dayOffset = 0;
    self->rebuildEventList();
}

void CalendarScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* Day navigation bar */
    lv_obj_t* navBar = lv_obj_create(_screen);
    lv_obj_set_size(navBar, 780, 40);
    lv_obj_set_pos(navBar, 10, CONTENT_Y + 5);
    lv_obj_set_style_bg_opa(navBar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(navBar, 0, 0);
    lv_obj_set_style_pad_all(navBar, 0, 0);
    lv_obj_clear_flag(navBar, LV_OBJ_FLAG_SCROLLABLE);

    _btnPrev = lv_btn_create(navBar);
    lv_obj_set_size(_btnPrev, 80, 34);
    lv_obj_align(_btnPrev, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(_btnPrev, BTN_BG, 0);
    lv_obj_set_style_radius(_btnPrev, 8, 0);
    lv_obj_t* lblPrev = lv_label_create(_btnPrev);
    lv_label_set_text(lblPrev, LV_SYMBOL_LEFT " Prev");
    lv_obj_set_style_text_color(lblPrev, TEXT_PRIMARY, 0);
    lv_obj_center(lblPrev);
    lv_obj_add_event_cb(_btnPrev, onPrev, LV_EVENT_CLICKED, this);

    _lblDayTitle = lv_label_create(navBar);
    lv_obj_set_style_text_font(_lblDayTitle, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblDayTitle, TEXT_PRIMARY, 0);
    lv_obj_align(_lblDayTitle, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(_lblDayTitle, "Today");

    _btnToday = lv_btn_create(navBar);
    lv_obj_set_size(_btnToday, 70, 34);
    lv_obj_align(_btnToday, LV_ALIGN_RIGHT_MID, -90, 0);
    lv_obj_set_style_bg_color(_btnToday, ACCENT, 0);
    lv_obj_set_style_radius(_btnToday, 8, 0);
    lv_obj_t* lblToday = lv_label_create(_btnToday);
    lv_label_set_text(lblToday, "Today");
    lv_obj_set_style_text_color(lblToday, TEXT_PRIMARY, 0);
    lv_obj_center(lblToday);
    lv_obj_add_event_cb(_btnToday, onToday, LV_EVENT_CLICKED, this);

    _btnNext = lv_btn_create(navBar);
    lv_obj_set_size(_btnNext, 80, 34);
    lv_obj_align(_btnNext, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(_btnNext, BTN_BG, 0);
    lv_obj_set_style_radius(_btnNext, 8, 0);
    lv_obj_t* lblNext = lv_label_create(_btnNext);
    lv_label_set_text(lblNext, "Next " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(lblNext, TEXT_PRIMARY, 0);
    lv_obj_center(lblNext);
    lv_obj_add_event_cb(_btnNext, onNext, LV_EVENT_CLICKED, this);

    /* Scrollable event list */
    _eventList = lv_obj_create(_screen);
    lv_obj_set_size(_eventList, 780, 340);
    lv_obj_set_pos(_eventList, 10, CONTENT_Y + 50);
    lv_obj_set_style_bg_opa(_eventList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_eventList, 0, 0);
    lv_obj_set_style_pad_all(_eventList, 0, 0);
    lv_obj_set_style_pad_row(_eventList, 8, 0);
    lv_obj_set_flex_flow(_eventList, LV_FLEX_FLOW_COLUMN);

    /* Empty state */
    _lblEmpty = lv_label_create(_eventList);
    lv_obj_set_style_text_font(_lblEmpty, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lblEmpty, TEXT_SECONDARY, 0);
    lv_label_set_text(_lblEmpty, "No events");
    lv_obj_set_width(_lblEmpty, 780);
    lv_obj_set_style_text_align(_lblEmpty, LV_TEXT_ALIGN_CENTER, 0);

    Serial.println("CAL: screen created");
}

void CalendarScreen::addEventCard(const char* title, const char* time,
                                   const char* location, uint32_t color,
                                   bool isCurrent) {
    lv_obj_t* card = lv_obj_create(_eventList);
    lv_obj_set_size(card, 760, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(card, 60, 0);
    lv_obj_set_style_bg_color(card, isCurrent ? CARD_HL : CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Color stripe */
    lv_obj_t* stripe = lv_obj_create(card);
    lv_obj_set_size(stripe, 4, lv_pct(100));
    lv_obj_align(stripe, LV_ALIGN_LEFT_MID, -6, 0);
    lv_obj_set_style_bg_color(stripe, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(stripe, 2, 0);
    lv_obj_set_style_border_width(stripe, 0, 0);

    /* Time */
    lv_obj_t* lblTime = lv_label_create(card);
    lv_obj_set_style_text_font(lblTime, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblTime, TEXT_SECONDARY, 0);
    lv_obj_align(lblTime, LV_ALIGN_TOP_LEFT, 6, 0);
    lv_label_set_text(lblTime, time);

    /* Title */
    lv_obj_t* lblTitle = lv_label_create(card);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblTitle, TEXT_PRIMARY, 0);
    lv_obj_align(lblTitle, LV_ALIGN_TOP_LEFT, 6, 20);
    lv_obj_set_width(lblTitle, 700);
    lv_label_set_long_mode(lblTitle, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lblTitle, title);

    /* Location (if present) */
    if (location && location[0] != '\0') {
        lv_obj_t* lblLoc = lv_label_create(card);
        lv_obj_set_style_text_font(lblLoc, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lblLoc, TEXT_SECONDARY, 0);
        lv_obj_align(lblLoc, LV_ALIGN_TOP_LEFT, 6, 42);
        lv_obj_set_width(lblLoc, 700);
        lv_label_set_long_mode(lblLoc, LV_LABEL_LONG_DOT);
        lv_label_set_text(lblLoc, location);
    }
}

void CalendarScreen::rebuildEventList() {
    if (!_eventList || !_lastData) return;

    /* Clear existing children */
    lv_obj_clean(_eventList);

    /* Update day title */
    if (_dayOffset < 4) {
        lv_label_set_text(_lblDayTitle, DAY_LABELS[_dayOffset]);
    } else {
        lv_label_set_text_fmt(_lblDayTitle, "In %d days", _dayOffset);
    }

    /* Merge Google + Microsoft events */
    /* For v1: show all events (no date filtering on device — bridge sends today's events) */
    uint8_t count = 0;

    if (_lastData->google_calendar.status == SourceStatus::OK) {
        for (uint8_t i = 0; i < _lastData->google_calendar_count; i++) {
            const CalendarEvent& ev = _lastData->google_calendar.data[i];
            char timeBuf[48];
            snprintf(timeBuf, sizeof(timeBuf), "%s - %s",
                     ev.start_time, ev.end_time);
            addEventCard(ev.title, timeBuf, ev.location, ev.color, count == 0);
            count++;
        }
    }

    if (_lastData->microsoft_calendar.status == SourceStatus::OK) {
        for (uint8_t i = 0; i < _lastData->microsoft_calendar_count; i++) {
            const CalendarEvent& ev = _lastData->microsoft_calendar.data[i];
            char timeBuf[48];
            snprintf(timeBuf, sizeof(timeBuf), "%s - %s",
                     ev.start_time, ev.end_time);
            addEventCard(ev.title, timeBuf, ev.location, ev.color, false);
            count++;
        }
    }

    if (count == 0) {
        _lblEmpty = lv_label_create(_eventList);
        lv_obj_set_style_text_font(_lblEmpty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_lblEmpty, TEXT_SECONDARY, 0);
        lv_label_set_text(_lblEmpty, "No events for this day");
        lv_obj_set_width(_lblEmpty, 760);
        lv_obj_set_style_text_align(_lblEmpty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(_lblEmpty, 60, 0);
    }

    /* Source legend at bottom */
    lv_obj_t* legend = lv_obj_create(_eventList);
    lv_obj_set_size(legend, 760, 30);
    lv_obj_set_style_bg_opa(legend, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(legend, 0, 0);
    lv_obj_set_style_pad_all(legend, 0, 0);
    lv_obj_clear_flag(legend, LV_OBJ_FLAG_SCROLLABLE);

    /* Google indicator */
    lv_obj_t* gDot = lv_obj_create(legend);
    lv_obj_set_size(gDot, 8, 8);
    lv_obj_set_style_bg_color(gDot, lv_color_hex(0x4285F4), 0);
    lv_obj_set_style_bg_opa(gDot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(gDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(gDot, 0, 0);
    lv_obj_align(gDot, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* gLbl = lv_label_create(legend);
    lv_label_set_text(gLbl, "Google");
    lv_obj_set_style_text_font(gLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gLbl, TEXT_SECONDARY, 0);
    lv_obj_align(gLbl, LV_ALIGN_LEFT_MID, 14, 0);

    /* Microsoft indicator */
    lv_obj_t* mDot = lv_obj_create(legend);
    lv_obj_set_size(mDot, 8, 8);
    lv_obj_set_style_bg_color(mDot, lv_color_hex(0x00A4EF), 0);
    lv_obj_set_style_bg_opa(mDot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(mDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(mDot, 0, 0);
    lv_obj_align(mDot, LV_ALIGN_LEFT_MID, 80, 0);

    lv_obj_t* mLbl = lv_label_create(legend);
    lv_label_set_text(mLbl, "Microsoft");
    lv_obj_set_style_text_font(mLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mLbl, TEXT_SECONDARY, 0);
    lv_obj_align(mLbl, LV_ALIGN_LEFT_MID, 94, 0);
}

void CalendarScreen::update(const DashboardData& data) {
    _lastData = &data;
    rebuildEventList();
}

void CalendarScreen::onShow() {
    _dayOffset = 0;
    if (_lastData) rebuildEventList();
}

/**
 * Home Screen — Implementation
 * Layout: Clock top-left, Appointment top-right,
 *         Weather bottom-left, Tasks bottom-right.
 * Content area: y=30..430 (400px between status bar and nav bar).
 */

#include "ui/home_screen.h"
#include "ui/weather_icon.h"
#include "ntp_time.h"
#include "config_store.h"
#include "logger.h"

static const lv_color_t BG_COLOR     = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG      = lv_color_hex(0x1a1a2e);
static const lv_color_t TEXT_PRIMARY  = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0xB0B0D0);
static const lv_color_t ACCENT       = lv_color_hex(0x6C63FF);

static constexpr int16_t CONTENT_Y   = 30;
static constexpr int16_t CONTENT_H   = 400;
static constexpr int16_t PAD         = 10;

/* --- Card helper --- */
static lv_obj_t* makeCard(lv_obj_t* parent, int16_t x, int16_t y,
                           int16_t w, int16_t h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

/* ========== CLOCK ========== */

void HomeScreen::createClock(lv_obj_t* parent) {
    lv_obj_t* area = makeCard(parent, PAD, CONTENT_Y + PAD, 380, 180);

    _lblClock = lv_label_create(area);
    lv_obj_set_style_text_font(_lblClock, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(_lblClock, TEXT_PRIMARY, 0);
    lv_obj_align(_lblClock, LV_ALIGN_TOP_LEFT, 0, 10);
    lv_label_set_text(_lblClock, "--:--");

    _lblDate = lv_label_create(area);
    lv_obj_set_style_text_font(_lblDate, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblDate, TEXT_SECONDARY, 0);
    lv_obj_align(_lblDate, LV_ALIGN_TOP_LEFT, 0, 75);
    lv_label_set_text(_lblDate, "---");
}

void HomeScreen::updateClock() {
    if (!_lblClock) return;
    String t = NtpTime::timeStr(false);
    lv_label_set_text(_lblClock, t.c_str());
    String d = NtpTime::dateStr();
    lv_label_set_text(_lblDate, d.c_str());
}

void HomeScreen::clockTimerCb(lv_timer_t* timer) {
    auto* self = (HomeScreen*)timer->user_data;
    self->updateClock();
}

/* ========== NEXT APPOINTMENT ========== */

void HomeScreen::createApptCard(lv_obj_t* parent) {
    _cardAppt = makeCard(parent, 400, CONTENT_Y + PAD, 390, 180);

    lv_obj_t* header = lv_label_create(_cardAppt);
    lv_label_set_text(header, LV_SYMBOL_BELL " Today");
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(header, TEXT_SECONDARY, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Up to 3 compact event rows: "HH:MM  Event title..." */
    for (uint8_t i = 0; i < MAX_APPT_ROWS; i++) {
        int16_t rowY = 24 + i * 46;

        _lblApptTimes[i] = lv_label_create(_cardAppt);
        lv_obj_set_style_text_font(_lblApptTimes[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_lblApptTimes[i], TEXT_SECONDARY, 0);
        lv_obj_align(_lblApptTimes[i], LV_ALIGN_TOP_LEFT, 0, rowY);
        lv_label_set_text(_lblApptTimes[i], "");

        _lblApptTitles[i] = lv_label_create(_cardAppt);
        lv_obj_set_style_text_font(_lblApptTitles[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_lblApptTitles[i], TEXT_PRIMARY, 0);
        lv_obj_align(_lblApptTitles[i], LV_ALIGN_TOP_LEFT, 0, rowY + 20);
        lv_obj_set_width(_lblApptTitles[i], 340);
        lv_label_set_long_mode(_lblApptTitles[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(_lblApptTitles[i], "");
    }

    _lblApptMore = lv_label_create(_cardAppt);
    lv_obj_set_style_text_font(_lblApptMore, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lblApptMore, ACCENT, 0);
    lv_obj_align(_lblApptMore, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_label_set_text(_lblApptMore, "No upcoming events");
}

/* Extract HH:MM from ISO 8601 "2026-03-06T15:00:00-05:00" → "15:00" */
static void fmtTime(const char* iso, char* out, size_t outLen) {
    const char* t = strchr(iso, 'T');
    if (!t) { out[0] = '\0'; return; }
    int hh = 0, mm = 0;
    sscanf(t + 1, "%d:%d", &hh, &mm);
    snprintf(out, outLen, "%02d:%02d", hh, mm);
}

void HomeScreen::updateAppt(const DashboardData& data) {
    if (!_cardAppt) return;

    /* Gather all events from Google + Microsoft into a flat list */
    const CalendarEvent* events[MAX_EVENTS * 2];
    uint8_t total = 0;

    if (data.google_calendar.status == SourceStatus::OK) {
        for (uint8_t i = 0; i < data.google_calendar_count && total < MAX_EVENTS; i++) {
            events[total++] = &data.google_calendar.data[i];
        }
    }
    if (data.microsoft_calendar.status == SourceStatus::OK) {
        for (uint8_t i = 0; i < data.microsoft_calendar_count && total < MAX_EVENTS; i++) {
            events[total++] = &data.microsoft_calendar.data[i];
        }
    }

    /* Show up to MAX_APPT_ROWS events in compact format */
    uint8_t show = (total > MAX_APPT_ROWS) ? MAX_APPT_ROWS : total;
    for (uint8_t i = 0; i < MAX_APPT_ROWS; i++) {
        if (i < show) {
            const CalendarEvent& ev = *events[i];
            char startFmt[16], endFmt[16], timeBuf[40];
            fmtTime(ev.start_time, startFmt, sizeof(startFmt));
            fmtTime(ev.end_time, endFmt, sizeof(endFmt));
            snprintf(timeBuf, sizeof(timeBuf), "%s - %s", startFmt, endFmt);
            lv_label_set_text(_lblApptTimes[i], timeBuf);
            lv_label_set_text(_lblApptTitles[i], ev.title);
        } else {
            lv_label_set_text(_lblApptTimes[i], "");
            lv_label_set_text(_lblApptTitles[i], "");
        }
    }

    /* Footer: "+N more" or empty */
    if (total > MAX_APPT_ROWS) {
        char moreBuf[32];
        snprintf(moreBuf, sizeof(moreBuf), "+%d more today", total - MAX_APPT_ROWS);
        lv_label_set_text(_lblApptMore, moreBuf);
    } else if (total == 0) {
        lv_label_set_text(_lblApptMore, "No upcoming events");
    } else {
        lv_label_set_text(_lblApptMore, "");
    }
}

/* ========== WEATHER ========== */

void HomeScreen::createWeatherCard(lv_obj_t* parent) {
    _cardWeather = makeCard(parent, PAD, CONTENT_Y + 200, 380, 190);

    lv_obj_t* header = lv_label_create(_cardWeather);
    lv_label_set_text(header, "Weather");
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(header, TEXT_SECONDARY, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);

    _weatherCanvas = WeatherIcon::create(_cardWeather);
    lv_obj_align(_weatherCanvas, LV_ALIGN_TOP_LEFT, 0, 24);

    _lblTemp = lv_label_create(_cardWeather);
    lv_obj_set_style_text_font(_lblTemp, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(_lblTemp, TEXT_PRIMARY, 0);
    lv_obj_align(_lblTemp, LV_ALIGN_TOP_LEFT, 58, 24);
    lv_label_set_text(_lblTemp, "--" "\xC2\xB0");

    _lblHighLow = lv_label_create(_cardWeather);
    lv_obj_set_style_text_font(_lblHighLow, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblHighLow, TEXT_SECONDARY, 0);
    lv_obj_align(_lblHighLow, LV_ALIGN_TOP_LEFT, 58, 68);
    lv_label_set_text(_lblHighLow, "H: --  L: --");

    _lblCondition = lv_label_create(_cardWeather);
    lv_obj_set_style_text_font(_lblCondition, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblCondition, TEXT_PRIMARY, 0);
    lv_obj_align(_lblCondition, LV_ALIGN_TOP_LEFT, 0, 96);
    lv_label_set_text(_lblCondition, "No data");
}

void HomeScreen::updateWeather(const DashboardData& data) {
    if (!_lblTemp) return;

    if (data.weather.status == SourceStatus::OK) {
        const WeatherData& w = data.weather.data;
        char tempBuf[16];
        snprintf(tempBuf, sizeof(tempBuf), "%.0f\xC2\xB0", w.temp_current);
        lv_label_set_text(_lblTemp, tempBuf);

        char hlBuf[32];
        snprintf(hlBuf, sizeof(hlBuf), "H: %.0f\xC2\xB0  L: %.0f\xC2\xB0",
                 w.temp_high, w.temp_low);
        lv_label_set_text(_lblHighLow, hlBuf);
        lv_label_set_text(_lblCondition, w.condition);
        WeatherIcon::update(_weatherCanvas, w.icon);
    } else {
        lv_label_set_text(_lblTemp, "--\xC2\xB0");
        lv_label_set_text(_lblHighLow, "H: --  L: --");
        lv_label_set_text(_lblCondition,
                          data.weather.status == SourceStatus::ERROR ? "Error" : "No data");
        WeatherIcon::update(_weatherCanvas, "");
    }
}

/* ========== TASKS SUMMARY ========== */

void HomeScreen::createTasksCard(lv_obj_t* parent) {
    _cardTasks = makeCard(parent, 400, CONTENT_Y + 200, 390, 190);

    lv_obj_t* header = lv_label_create(_cardTasks);
    lv_label_set_text(header, "Tasks");
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(header, TEXT_SECONDARY, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);

    for (uint8_t i = 0; i < 5; i++) {
        _lblTaskItems[i] = lv_label_create(_cardTasks);
        lv_obj_set_style_text_font(_lblTaskItems[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(_lblTaskItems[i], TEXT_PRIMARY, 0);
        lv_obj_align(_lblTaskItems[i], LV_ALIGN_TOP_LEFT, 0, 24 + i * 28);
        lv_obj_set_width(_lblTaskItems[i], 360);
        lv_label_set_long_mode(_lblTaskItems[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(_lblTaskItems[i], "");
    }
    _taskItemCount = 0;
}

void HomeScreen::updateTasks(const DashboardData& data) {
    if (!_cardTasks) return;

    /* Merge Unfocused + Monday tasks, show top 5 */
    struct MergedTask {
        const char* title;
        const char* due;
        uint8_t priority;
        const char* source;
    };

    MergedTask merged[10];
    uint8_t count = 0;

    if (data.unfocused_tasks.status == SourceStatus::OK) {
        for (uint8_t i = 0; i < data.unfocused_tasks_count && count < 10; i++) {
            merged[count++] = {
                data.unfocused_tasks.data[i].title,
                data.unfocused_tasks.data[i].due_date,
                data.unfocused_tasks.data[i].priority,
                "UF"
            };
        }
    }
    if (data.monday_tasks.status == SourceStatus::OK) {
        for (uint8_t i = 0; i < data.monday_tasks_count && count < 10; i++) {
            merged[count++] = {
                data.monday_tasks.data[i].title,
                data.monday_tasks.data[i].due_date,
                data.monday_tasks.data[i].priority,
                "MO"
            };
        }
    }

    /* Sort by priority (simple bubble) */
    for (uint8_t i = 0; i < count; i++) {
        for (uint8_t j = i + 1; j < count; j++) {
            if (merged[j].priority < merged[i].priority) {
                MergedTask tmp = merged[i];
                merged[i] = merged[j];
                merged[j] = tmp;
            }
        }
    }

    uint8_t show = (count > 5) ? 5 : count;
    for (uint8_t i = 0; i < 5; i++) {
        if (i < show) {
            char buf[128];
            const char* prio = "";
            if (merged[i].priority == 1) prio = "! ";
            else if (merged[i].priority == 2) prio = "- ";
            snprintf(buf, sizeof(buf), "%s[%s] %s",
                     prio, merged[i].source, merged[i].title);
            lv_label_set_text(_lblTaskItems[i], buf);
        } else {
            lv_label_set_text(_lblTaskItems[i], "");
        }
    }

    if (count == 0) {
        lv_label_set_text(_lblTaskItems[0], "No task sources connected");
        lv_obj_set_style_text_color(_lblTaskItems[0], TEXT_SECONDARY, 0);
    }
}

/* ========== SCREEN LIFECYCLE ========== */

void HomeScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    createClock(_screen);
    createApptCard(_screen);
    createWeatherCard(_screen);
    createTasksCard(_screen);

    /* Clock updates every second */
    _clockTimer = lv_timer_create(clockTimerCb, 1000, this);
    lv_timer_pause(_clockTimer);

    LOG_INFO("HOME: screen created");
}

void HomeScreen::update(const DashboardData& data) {
    updateAppt(data);
    updateWeather(data);
    updateTasks(data);
}

void HomeScreen::onShow() {
    if (_clockTimer) lv_timer_resume(_clockTimer);
    updateClock();
}

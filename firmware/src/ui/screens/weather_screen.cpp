/**
 * Weather Screen — Implementation
 * Content area: y=30..430.
 * Top: current conditions card. Middle: hourly/daily toggle. Bottom: forecast.
 */

#include "ui/screens/weather_screen.h"
#include "ui/weather_icon.h"
#include "logger.h"

static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG        = lv_color_hex(0x1a1a2e);
static const lv_color_t TEXT_PRIMARY   = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0xB0B0D0);
static const lv_color_t ACCENT         = lv_color_hex(0x6C63FF);
static const lv_color_t HOURLY_BG     = lv_color_hex(0x252548);
static const lv_color_t TOGGLE_ACTIVE  = lv_color_hex(0x6C63FF);
static const lv_color_t TOGGLE_INACTIVE = lv_color_hex(0x2a2a4a);
static const lv_color_t RAIN_BLUE      = lv_color_hex(0x5BA8FF);

static constexpr int16_t CONTENT_Y = 30;
static constexpr int16_t PAD       = 10;

/* Map OWM icon code to short description for hourly tiles */
static const char* owmToLabel(const char* code) {
    if (!code || !code[0]) return "?";
    int num = atoi(code);
    switch (num) {
        case  1: return "Clear";
        case  2: return "Ptly";
        case  3: return "Cloudy";
        case  4: return "Ovcst";
        case  9: return "Shwrs";
        case 10: return "Rain";
        case 11: return "Storm";
        case 13: return "Snow";
        case 50: return "Fog";
        default: return "?";
    }
}

/* Helper: make a styled card */
static lv_obj_t* makeCard(lv_obj_t* parent, int16_t x, int16_t y,
                           int16_t w, int16_t h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

/* Helper: make a toggle button */
static lv_obj_t* makeToggleBtn(lv_obj_t* parent, const char* text,
                                int16_t x, int16_t y, bool active) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 90, 30);
    lv_obj_set_style_bg_color(btn, active ? TOGGLE_ACTIVE : TOGGLE_INACTIVE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, TEXT_PRIMARY, 0);
    lv_obj_center(lbl);

    return btn;
}

void WeatherScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* === Current conditions card (top, full width) === */
    lv_obj_t* currentCard = makeCard(_screen, PAD, CONTENT_Y + PAD, 780, 150);

    /* Weather icon canvas (48x48, drawn from OWM codes) */
    _weatherIcon = WeatherIcon::create(currentCard);
    lv_obj_align(_weatherIcon, LV_ALIGN_TOP_LEFT, 0, 12);

    _lblTemp = lv_label_create(currentCard);
    lv_obj_set_style_text_font(_lblTemp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(_lblTemp, TEXT_PRIMARY, 0);
    lv_obj_align(_lblTemp, LV_ALIGN_TOP_LEFT, 58, 10);
    lv_label_set_text(_lblTemp, "--\xC2\xB0");

    _lblCondition = lv_label_create(currentCard);
    lv_obj_set_style_text_font(_lblCondition, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblCondition, TEXT_SECONDARY, 0);
    lv_obj_align(_lblCondition, LV_ALIGN_TOP_LEFT, 58, 68);
    lv_label_set_text(_lblCondition, "No data");

    /* Detail stats on the right side */
    _lblHighLow = lv_label_create(currentCard);
    lv_obj_set_style_text_font(_lblHighLow, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblHighLow, TEXT_PRIMARY, 0);
    lv_obj_align(_lblHighLow, LV_ALIGN_TOP_RIGHT, 0, 10);
    lv_label_set_text(_lblHighLow, "H: --\xC2\xB0  L: --\xC2\xB0");

    _lblHumidity = lv_label_create(currentCard);
    lv_obj_set_style_text_font(_lblHumidity, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblHumidity, TEXT_SECONDARY, 0);
    lv_obj_align(_lblHumidity, LV_ALIGN_TOP_RIGHT, 0, 40);
    lv_label_set_text(_lblHumidity, "Humidity: --%");

    _lblPrecip = lv_label_create(currentCard);
    lv_obj_set_style_text_font(_lblPrecip, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblPrecip, TEXT_SECONDARY, 0);
    lv_obj_align(_lblPrecip, LV_ALIGN_TOP_RIGHT, 0, 70);
    lv_label_set_text(_lblPrecip, "Precip: --%");

    /* === Hourly / Daily toggle buttons === */
    _btnHourly = makeToggleBtn(_screen, "Hourly", PAD + 4, CONTENT_Y + 168, true);
    lv_obj_set_user_data(_btnHourly, this);
    lv_obj_add_event_cb(_btnHourly, onToggleCb, LV_EVENT_CLICKED, nullptr);

    _btnDaily = makeToggleBtn(_screen, "Daily", PAD + 100, CONTENT_Y + 168, false);
    lv_obj_set_user_data(_btnDaily, this);
    lv_obj_add_event_cb(_btnDaily, onToggleCb, LV_EVENT_CLICKED, nullptr);

    /* === Forecast scroll row (shared by hourly and daily) === */
    _forecastRow = lv_obj_create(_screen);
    lv_obj_set_size(_forecastRow, 780, 190);
    lv_obj_set_pos(_forecastRow, PAD, CONTENT_Y + 205);
    lv_obj_set_style_bg_opa(_forecastRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_forecastRow, 0, 0);
    lv_obj_set_style_pad_all(_forecastRow, 0, 0);
    lv_obj_set_style_pad_column(_forecastRow, 8, 0);
    lv_obj_set_flex_flow(_forecastRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(_forecastRow, LV_DIR_HOR);

    LOG_INFO("WEATHER: screen created");
}

void WeatherScreen::onToggleCb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    auto* self = (WeatherScreen*)lv_obj_get_user_data(btn);
    if (!self) return;

    if (btn == self->_btnHourly && self->_viewMode != VIEW_HOURLY) {
        self->setViewMode(VIEW_HOURLY);
    } else if (btn == self->_btnDaily && self->_viewMode != VIEW_DAILY) {
        self->setViewMode(VIEW_DAILY);
    }
}

void WeatherScreen::setViewMode(ViewMode mode) {
    _viewMode = mode;

    /* Update toggle button colors */
    lv_obj_set_style_bg_color(_btnHourly,
        mode == VIEW_HOURLY ? TOGGLE_ACTIVE : TOGGLE_INACTIVE, 0);
    lv_obj_set_style_bg_color(_btnDaily,
        mode == VIEW_DAILY ? TOGGLE_ACTIVE : TOGGLE_INACTIVE, 0);

    rebuildForecast();
}

void WeatherScreen::rebuildForecast() {
    if (!_hasCachedData) return;
    if (_viewMode == VIEW_DAILY) {
        rebuildDaily(_cachedWeather);
    } else {
        rebuildHourly(_cachedWeather);
    }
}

void WeatherScreen::rebuildHourly(const WeatherData& w) {
    lv_obj_clean(_forecastRow);

    if (w.hourly_count == 0) {
        lv_obj_t* lbl = lv_label_create(_forecastRow);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, "No hourly data available");
        return;
    }

    for (uint8_t i = 0; i < w.hourly_count && i < MAX_HOURLY; i++) {
        const HourlyForecast& h = w.hourly[i];

        lv_obj_t* tile = lv_obj_create(_forecastRow);
        lv_obj_set_size(tile, 90, 170);
        lv_obj_set_style_bg_color(tile, HOURLY_BG, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tile, 10, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 6, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        /* Time label */
        lv_obj_t* lblTime = lv_label_create(tile);
        lv_obj_set_style_text_font(lblTime, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lblTime, TEXT_PRIMARY, 0);
        lv_obj_align(lblTime, LV_ALIGN_TOP_MID, 0, 0);
        lv_label_set_text(lblTime, h.time);

        /* Weather description */
        lv_obj_t* lblIcon = lv_label_create(tile);
        lv_obj_set_style_text_font(lblIcon, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lblIcon, TEXT_SECONDARY, 0);
        lv_obj_align(lblIcon, LV_ALIGN_TOP_MID, 0, 28);
        lv_label_set_text(lblIcon, owmToLabel(h.icon));

        /* Temperature */
        lv_obj_t* lblTemp = lv_label_create(tile);
        lv_obj_set_style_text_font(lblTemp, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lblTemp, TEXT_PRIMARY, 0);
        lv_obj_align(lblTemp, LV_ALIGN_TOP_MID, 0, 58);
        char tempBuf[16];
        snprintf(tempBuf, sizeof(tempBuf), "%.0f\xC2\xB0", h.temp);
        lv_label_set_text(lblTemp, tempBuf);

        /* Precipitation chance */
        if (h.precip_chance > 0.0f) {
            lv_obj_t* lblPrecip = lv_label_create(tile);
            lv_obj_set_style_text_font(lblPrecip, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(lblPrecip, RAIN_BLUE, 0);
            lv_obj_align(lblPrecip, LV_ALIGN_TOP_MID, 0, 90);
            char precipBuf[16];
            snprintf(precipBuf, sizeof(precipBuf), "%.0f%%", h.precip_chance);
            lv_label_set_text(lblPrecip, precipBuf);
        }
    }
}

void WeatherScreen::rebuildDaily(const WeatherData& w) {
    lv_obj_clean(_forecastRow);

    if (w.daily_count == 0) {
        lv_obj_t* lbl = lv_label_create(_forecastRow);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, "No daily data available");
        return;
    }

    for (uint8_t i = 0; i < w.daily_count && i < MAX_DAILY; i++) {
        const DailyForecast& d = w.daily[i];

        lv_obj_t* tile = lv_obj_create(_forecastRow);
        lv_obj_set_size(tile, 140, 170);
        lv_obj_set_style_bg_color(tile, HOURLY_BG, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tile, 10, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 8, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        /* Day name (Mon, Tue, ...) */
        lv_obj_t* lblDay = lv_label_create(tile);
        lv_obj_set_style_text_font(lblDay, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lblDay, TEXT_PRIMARY, 0);
        lv_obj_align(lblDay, LV_ALIGN_TOP_MID, 0, 0);
        lv_label_set_text(lblDay, d.day);

        /* Condition from icon code */
        lv_obj_t* lblCond = lv_label_create(tile);
        lv_obj_set_style_text_font(lblCond, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lblCond, TEXT_SECONDARY, 0);
        lv_obj_align(lblCond, LV_ALIGN_TOP_MID, 0, 28);
        lv_label_set_text(lblCond, owmToLabel(d.icon));

        /* High / Low temps */
        lv_obj_t* lblHL = lv_label_create(tile);
        lv_obj_set_style_text_font(lblHL, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lblHL, TEXT_PRIMARY, 0);
        lv_obj_align(lblHL, LV_ALIGN_TOP_MID, 0, 62);
        char hlBuf[24];
        snprintf(hlBuf, sizeof(hlBuf), "%.0f\xC2\xB0/%.0f\xC2\xB0",
                 d.temp_high, d.temp_low);
        lv_label_set_text(lblHL, hlBuf);

        /* Precipitation chance */
        lv_obj_t* lblRain = lv_label_create(tile);
        lv_obj_set_style_text_font(lblRain, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lblRain,
            d.precip_chance > 30.0f ? RAIN_BLUE : TEXT_SECONDARY, 0);
        lv_obj_align(lblRain, LV_ALIGN_TOP_MID, 0, 94);
        char rainBuf[16];
        snprintf(rainBuf, sizeof(rainBuf), "%.0f%%", d.precip_chance);
        lv_label_set_text(lblRain, rainBuf);
    }
}

void WeatherScreen::update(const DashboardData& data) {
    _lastData = &data;
    _dirty = true;

    /* Label updates are safe offscreen — they don't trigger layout recalc */
    if (data.weather.status == SourceStatus::OK) {
        const WeatherData& w = data.weather.data;

        /* Cache for toggle rebuilds */
        _cachedWeather = w;
        _hasCachedData = true;

        char tempBuf[16];
        snprintf(tempBuf, sizeof(tempBuf), "%.0f\xC2\xB0", w.temp_current);
        lv_label_set_text(_lblTemp, tempBuf);

        lv_label_set_text(_lblCondition, w.condition);

        /* Update weather icon canvas from OWM code */
        WeatherIcon::update(_weatherIcon, w.icon);

        char hlBuf[32];
        snprintf(hlBuf, sizeof(hlBuf), "H: %.0f\xC2\xB0  L: %.0f\xC2\xB0",
                 w.temp_high, w.temp_low);
        lv_label_set_text(_lblHighLow, hlBuf);

        char humBuf[24];
        snprintf(humBuf, sizeof(humBuf), "Humidity: %d%%", w.humidity);
        lv_label_set_text(_lblHumidity, humBuf);

        char precipBuf[24];
        snprintf(precipBuf, sizeof(precipBuf), "Precip: %.0f%%",
                 w.precip_chance);
        lv_label_set_text(_lblPrecip, precipBuf);

        /* Rebuild forecast tiles only if we're the currently-visible screen.
           Offscreen rebuilds create dirty LVGL layout trees that hang Core 1
           when the screen becomes visible via lv_scr_load_anim(). */
        if (lv_scr_act() == _screen) {
            rebuildForecast();
            _dirty = false;
        }
    } else {
        _hasCachedData = false;
        lv_label_set_text(_lblTemp, "--\xC2\xB0");
        lv_label_set_text(_lblCondition,
                          data.weather.status == SourceStatus::ERROR
                              ? "Connection error" : "No data");
        lv_label_set_text(_lblHighLow, "H: --\xC2\xB0  L: --\xC2\xB0");
        lv_label_set_text(_lblHumidity, "Humidity: --%");
        lv_label_set_text(_lblPrecip, "Precip: --%");
        if (lv_scr_act() == _screen) {
            lv_obj_clean(_forecastRow);
            _dirty = false;
        }
    }
}

void WeatherScreen::onShow() {
    if (!_lastData) return;
    if (_lastData->weather.status == SourceStatus::OK && _hasCachedData) {
        rebuildForecast();
    } else {
        lv_obj_clean(_forecastRow);
    }
    _dirty = false;
}

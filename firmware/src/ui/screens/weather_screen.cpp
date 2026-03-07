/**
 * Weather Screen — Implementation
 * Content area: y=30..430.
 * Top: current conditions card. Middle: detail stats. Bottom: hourly scroll.
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

    /* === Hourly forecast header === */
    lv_obj_t* lblHourlyHeader = lv_label_create(_screen);
    lv_obj_set_style_text_font(lblHourlyHeader, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lblHourlyHeader, TEXT_SECONDARY, 0);
    lv_obj_set_pos(lblHourlyHeader, PAD + 4, CONTENT_Y + 170);
    lv_label_set_text(lblHourlyHeader, "Hourly Forecast");

    /* === Horizontal scrollable hourly row === */
    _hourlyRow = lv_obj_create(_screen);
    lv_obj_set_size(_hourlyRow, 780, 190);
    lv_obj_set_pos(_hourlyRow, PAD, CONTENT_Y + 195);
    lv_obj_set_style_bg_opa(_hourlyRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_hourlyRow, 0, 0);
    lv_obj_set_style_pad_all(_hourlyRow, 0, 0);
    lv_obj_set_style_pad_column(_hourlyRow, 8, 0);
    lv_obj_set_flex_flow(_hourlyRow, LV_FLEX_FLOW_ROW);
    /* Allow horizontal scroll only */
    lv_obj_set_scroll_dir(_hourlyRow, LV_DIR_HOR);

    LOG_INFO("WEATHER: screen created");
}

void WeatherScreen::rebuildHourly(const WeatherData& w) {
    lv_obj_clean(_hourlyRow);

    if (w.hourly_count == 0) {
        lv_obj_t* lbl = lv_label_create(_hourlyRow);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, "No hourly data available");
        return;
    }

    for (uint8_t i = 0; i < w.hourly_count && i < MAX_HOURLY; i++) {
        const HourlyForecast& h = w.hourly[i];

        lv_obj_t* tile = lv_obj_create(_hourlyRow);
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

        /* Weather description (mapped from OWM code) */
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
            lv_obj_set_style_text_color(lblPrecip, ACCENT, 0);
            lv_obj_align(lblPrecip, LV_ALIGN_TOP_MID, 0, 90);
            char precipBuf[16];
            snprintf(precipBuf, sizeof(precipBuf), "%.0f%%", h.precip_chance);
            lv_label_set_text(lblPrecip, precipBuf);
        }
    }
}

void WeatherScreen::update(const DashboardData& data) {
    _lastData = &data;
    _dirty = true;

    /* Label updates are safe offscreen — they don't trigger layout recalc */
    if (data.weather.status == SourceStatus::OK) {
        const WeatherData& w = data.weather.data;

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

        /* Rebuild hourly tiles only if we're the currently-visible screen.
           Offscreen rebuilds create dirty LVGL layout trees that hang Core 1
           when the screen becomes visible via lv_scr_load_anim(). */
        if (lv_scr_act() == _screen) {
            rebuildHourly(w);
            _dirty = false;
        }
    } else {
        lv_label_set_text(_lblTemp, "--\xC2\xB0");
        lv_label_set_text(_lblCondition,
                          data.weather.status == SourceStatus::ERROR
                              ? "Connection error" : "No data");
        lv_label_set_text(_lblHighLow, "H: --\xC2\xB0  L: --\xC2\xB0");
        lv_label_set_text(_lblHumidity, "Humidity: --%");
        lv_label_set_text(_lblPrecip, "Precip: --%");
        if (lv_scr_act() == _screen) {
            lv_obj_clean(_hourlyRow);
            _dirty = false;
        }
    }
}

void WeatherScreen::onShow() {
    if (!_lastData) return;
    if (_lastData->weather.status == SourceStatus::OK) {
        rebuildHourly(_lastData->weather.data);
    } else {
        lv_obj_clean(_hourlyRow);
    }
    _dirty = false;
}

/**
 * Weather Screen — Current conditions, details, and hourly/daily forecast.
 * Large current temp + icon, detail row, toggle between hourly scroll
 * and daily forecast cards.
 */

#pragma once
#include "ui/base_screen.h"

class WeatherScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;
    void onShow() override;

private:
    enum ViewMode : uint8_t { VIEW_HOURLY, VIEW_DAILY };

    /* Current conditions */
    lv_obj_t* _weatherIcon  = nullptr;
    lv_obj_t* _lblTemp      = nullptr;
    lv_obj_t* _lblCondition = nullptr;

    /* Detail row */
    lv_obj_t* _lblHighLow   = nullptr;
    lv_obj_t* _lblHumidity  = nullptr;
    lv_obj_t* _lblPrecip    = nullptr;

    /* Hourly / Daily toggle */
    lv_obj_t* _btnHourly    = nullptr;
    lv_obj_t* _btnDaily     = nullptr;
    ViewMode  _viewMode     = VIEW_HOURLY;

    /* Forecast container (shared between hourly and daily) */
    lv_obj_t* _forecastRow  = nullptr;

    /* Cached data for rebuilding after toggle */
    WeatherData _cachedWeather = {};
    bool _hasCachedData = false;

    /* Cached data for deferred rebuild */
    const DashboardData* _lastData = nullptr;
    bool _dirty = false;

    void rebuildHourly(const WeatherData& w);
    void rebuildDaily(const WeatherData& w);
    void rebuildForecast();
    void setViewMode(ViewMode mode);

    static void onToggleCb(lv_event_t* e);
};

/**
 * Weather Screen — Current conditions, details, and hourly forecast.
 * Large current temp + icon, detail row, horizontal hourly scroll.
 */

#pragma once
#include "ui/base_screen.h"

class WeatherScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;
    void onShow() override;

private:
    /* Current conditions */
    lv_obj_t* _weatherIcon  = nullptr;   /* WeatherIcon canvas */
    lv_obj_t* _lblTemp      = nullptr;
    lv_obj_t* _lblCondition = nullptr;

    /* Detail row */
    lv_obj_t* _lblHighLow   = nullptr;
    lv_obj_t* _lblHumidity  = nullptr;
    lv_obj_t* _lblPrecip    = nullptr;

    /* Hourly forecast container */
    lv_obj_t* _hourlyRow    = nullptr;

    /* Cached data for deferred rebuild */
    const DashboardData* _lastData = nullptr;
    bool _dirty = false;

    void rebuildHourly(const WeatherData& w);
};

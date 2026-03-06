/**
 * Home Screen — Clock, next appointment, weather, tasks summary.
 * Content area: 800 x 400 (between 30px status bar and 50px nav bar).
 */

#pragma once
#include "ui/base_screen.h"

class HomeScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;
    void onShow() override;

private:
    /* Clock widget */
    lv_obj_t* _lblClock    = nullptr;
    lv_obj_t* _lblDate     = nullptr;
    lv_timer_t* _clockTimer = nullptr;

    /* Next appointment card */
    lv_obj_t* _cardAppt     = nullptr;
    lv_obj_t* _lblApptTitle = nullptr;
    lv_obj_t* _lblApptTime  = nullptr;
    lv_obj_t* _lblApptMore  = nullptr;
    lv_obj_t* _apptStripe   = nullptr;

    /* Weather summary card */
    lv_obj_t* _cardWeather  = nullptr;
    lv_obj_t* _lblWeatherIcon = nullptr;
    lv_obj_t* _lblTemp      = nullptr;
    lv_obj_t* _lblHighLow   = nullptr;
    lv_obj_t* _lblCondition = nullptr;

    /* Tasks summary card */
    lv_obj_t* _cardTasks    = nullptr;
    lv_obj_t* _lblTaskItems[5] = {};
    uint8_t   _taskItemCount = 0;

    void createClock(lv_obj_t* parent);
    void createApptCard(lv_obj_t* parent);
    void createWeatherCard(lv_obj_t* parent);
    void createTasksCard(lv_obj_t* parent);

    void updateClock();
    void updateAppt(const DashboardData& data);
    void updateWeather(const DashboardData& data);
    void updateTasks(const DashboardData& data);

    static void clockTimerCb(lv_timer_t* timer);
};

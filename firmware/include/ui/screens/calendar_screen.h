/**
 * Calendar Screen — Day view with event cards.
 * Prev/Next/Today navigation, scrollable event list.
 */

#pragma once
#include "ui/base_screen.h"

class CalendarScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;
    void onShow() override;

private:
    lv_obj_t* _lblDayTitle  = nullptr;
    lv_obj_t* _btnPrev      = nullptr;
    lv_obj_t* _btnNext      = nullptr;
    lv_obj_t* _btnToday     = nullptr;
    lv_obj_t* _eventList    = nullptr;
    lv_obj_t* _lblEmpty     = nullptr;

    const DashboardData* _lastData = nullptr;
    bool _dirty = false;
    int8_t _dayOffset = 0;  // 0 = today, +1 = tomorrow, etc.

    void rebuildEventList();
    void addEventCard(const char* title, const char* time,
                      const char* location, uint32_t color, bool isCurrent);

    static void onPrev(lv_event_t* e);
    static void onNext(lv_event_t* e);
    static void onToday(lv_event_t* e);
};

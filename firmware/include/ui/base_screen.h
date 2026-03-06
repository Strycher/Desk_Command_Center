/**
 * BaseScreen — Abstract base for all UI screens.
 * Screens are created once at boot and never destroyed.
 */

#pragma once
#include <lvgl.h>
#include "dashboard_data.h"

class BaseScreen {
public:
    virtual ~BaseScreen() = default;
    virtual void create(lv_obj_t* parent) = 0;
    virtual void update(const DashboardData& data) = 0;
    virtual void onShow() {}
    virtual void onHide() {}
    lv_obj_t* screen() { return _screen; }

protected:
    lv_obj_t* _screen = nullptr;
};

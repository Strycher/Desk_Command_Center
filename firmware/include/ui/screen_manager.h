/**
 * Screen Manager — Create-once lifecycle for all UI screens.
 * All screens registered at boot. No dynamic allocation after init.
 */

#pragma once
#include <lvgl.h>
#include "ui/base_screen.h"

enum class ScreenId : uint8_t {
    HOME,
    CALENDAR,
    TASKS,
    WEATHER,
    DEVOPS,
    HA,
    CLAUDE,
    SETTINGS,
    DIAGNOSTICS,
    _COUNT
};

namespace ScreenManager {
    void init();
    void registerScreen(ScreenId id, BaseScreen* screen);
    void show(ScreenId id, lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_NONE,
              uint32_t anim_ms = 0, bool auto_del = false);
    void showHome();
    void updateAll(const DashboardData& data);
    ScreenId currentId();
}

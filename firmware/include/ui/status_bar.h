/**
 * Status Bar — Persistent top bar with time, date, WiFi, sync status.
 * Created on LVGL top layer, updated every second.
 */

#pragma once
#include <lvgl.h>

namespace StatusBar {
    void create();
    void update();  // Called every second via LVGL timer
}

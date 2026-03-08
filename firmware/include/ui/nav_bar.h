/**
 * Navigation Bar — Persistent 8-icon bottom bar.
 * Created on LVGL top layer, persists across screen transitions.
 */

#pragma once
#include <lvgl.h>
#include "ui/screen_manager.h"

namespace NavBar {
    void create();
    void setActive(ScreenId id);
}

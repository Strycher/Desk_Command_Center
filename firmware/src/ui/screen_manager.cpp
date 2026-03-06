/**
 * Screen Manager — Implementation
 */

#include "ui/screen_manager.h"

static BaseScreen* screens[static_cast<uint8_t>(ScreenId::_COUNT)] = {};
static ScreenId    current = ScreenId::HOME;

void ScreenManager::init() {
    Serial.println("SCR: screen manager init");
}

void ScreenManager::registerScreen(ScreenId id, BaseScreen* screen) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= static_cast<uint8_t>(ScreenId::_COUNT)) return;

    screens[idx] = screen;
    screen->create(nullptr);
    /* Disable scrolling on all screens — content is fixed layout */
    if (screen->screen()) {
        lv_obj_clear_flag(screen->screen(), LV_OBJ_FLAG_SCROLLABLE);
    }
    Serial.printf("SCR: registered screen %d\n", idx);
}

void ScreenManager::show(ScreenId id, lv_scr_load_anim_t anim,
                          uint32_t anim_ms, bool auto_del) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= static_cast<uint8_t>(ScreenId::_COUNT) || !screens[idx]) return;

    /* Hide current (only if it's a registered screen) */
    uint8_t cur_idx = static_cast<uint8_t>(current);
    if (cur_idx < static_cast<uint8_t>(ScreenId::_COUNT) && screens[cur_idx] && id != current) {
        screens[cur_idx]->onHide();
    }

    /* Show new */
    lv_obj_t* target = screens[idx]->screen();
    if (target) {
        if (anim == LV_SCR_LOAD_ANIM_NONE || anim_ms == 0) {
            lv_scr_load(target);
        } else {
            lv_scr_load_anim(target, anim, anim_ms, 0, auto_del);
        }
    }
    current = id;
    screens[idx]->onShow();

    Serial.printf("SCR: show screen %d\n", idx);
}

void ScreenManager::showHome() {
    show(ScreenId::HOME, LV_SCR_LOAD_ANIM_FADE_ON, 200, false);
}

void ScreenManager::updateAll(const DashboardData& data) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(ScreenId::_COUNT); i++) {
        if (screens[i]) {
            screens[i]->update(data);
        }
    }
}

ScreenId ScreenManager::currentId() {
    return current;
}

/**
 * Settings Screen — Bridge URL, Timezone, Display settings.
 * Single scrollable screen with grouped setting cards.
 */

#pragma once
#include "ui/base_screen.h"
#include "config_store.h"

class SettingsScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;
    void onShow() override;

private:
    /* Bridge URL */
    lv_obj_t* _taBridgeURL = nullptr;
    lv_obj_t* _btnSaveURL  = nullptr;

    /* Timezone */
    lv_obj_t* _ddTimezone  = nullptr;
    lv_obj_t* _btnSaveTZ   = nullptr;

    /* Display */
    lv_obj_t* _sliderBright = nullptr;
    lv_obj_t* _lblBrightVal = nullptr;
    lv_obj_t* _sw24h        = nullptr;

    /* Poll interval */
    lv_obj_t* _taPollSec    = nullptr;
    lv_obj_t* _btnSavePoll  = nullptr;

    /* Status */
    lv_obj_t* _lblStatus   = nullptr;

    DeviceConfig _cfg;

    void loadSettings();

    static void onSaveURL(lv_event_t* e);
    static void onSaveTZ(lv_event_t* e);
    static void onBrightnessChanged(lv_event_t* e);
    static void onBrightnessRelease(lv_event_t* e);
    static void on24hToggle(lv_event_t* e);
    static void onSavePoll(lv_event_t* e);
    static void onURLFocus(lv_event_t* e);
    static void onPollFocus(lv_event_t* e);
};

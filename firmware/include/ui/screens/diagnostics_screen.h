/**
 * Diagnostics Screen — System health and debug info.
 * FW version, heap/PSRAM, WiFi, per-source sync status.
 */

#pragma once
#include "ui/base_screen.h"

class DiagnosticsScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;
    void onShow() override;

private:
    /* System info */
    lv_obj_t* _lblVersion   = nullptr;
    lv_obj_t* _lblHeap      = nullptr;
    lv_obj_t* _lblPSRAM     = nullptr;
    lv_obj_t* _lblUptime    = nullptr;

    /* WiFi info */
    lv_obj_t* _lblWifiSSID  = nullptr;
    lv_obj_t* _lblWifiIP    = nullptr;
    lv_obj_t* _lblWifiRSSI  = nullptr;

    /* Data sources */
    lv_obj_t* _lblLastFetch = nullptr;
    lv_obj_t* _lblFetchErr  = nullptr;

    lv_timer_t* _refreshTimer = nullptr;

    void refreshSystemInfo();

    static void onRefreshTimer(lv_timer_t* timer);
};

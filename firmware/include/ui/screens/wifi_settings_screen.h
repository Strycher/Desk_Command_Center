/**
 * WiFi Settings Screen — Manage saved networks.
 * List, add, delete, connect/disconnect. Up to 5 saved networks.
 */

#pragma once
#include "ui/base_screen.h"
#include "config_store.h"

class WifiSettingsScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;
    void onShow() override;

private:
    lv_obj_t* _networkList = nullptr;
    lv_obj_t* _btnAdd      = nullptr;
    lv_obj_t* _lblStatus   = nullptr;

    /* Add network dialog */
    lv_obj_t* _dialogBg    = nullptr;
    lv_obj_t* _taSSID      = nullptr;
    lv_obj_t* _taPass      = nullptr;
    lv_obj_t* _btnSave     = nullptr;
    lv_obj_t* _btnCancel   = nullptr;

    DeviceConfig _cfg;

    void rebuildNetworkList();
    void showAddDialog();
    void hideAddDialog();
    void saveNewNetwork();
    void deleteNetwork(uint8_t index);

    static void onAdd(lv_event_t* e);
    static void onSave(lv_event_t* e);
    static void onCancel(lv_event_t* e);
    static void onDelete(lv_event_t* e);
    static void onSSIDFocus(lv_event_t* e);
    static void onPassFocus(lv_event_t* e);
};

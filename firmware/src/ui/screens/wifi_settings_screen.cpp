/**
 * WiFi Settings Screen — Implementation
 * Content area: y=30..430. Network list + add dialog with OSK.
 */

#include "ui/screens/wifi_settings_screen.h"
#include "ui/osk.h"
#include "wifi_manager.h"
#include "logger.h"

static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG        = lv_color_hex(0x1a1a2e);
static const lv_color_t DIALOG_BG      = lv_color_hex(0x252548);
static const lv_color_t TEXT_PRIMARY   = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0x8888AA);
static const lv_color_t ACCENT         = lv_color_hex(0x6C63FF);
static const lv_color_t DANGER         = lv_color_hex(0xFF4444);
static const lv_color_t BTN_BG        = lv_color_hex(0x252540);
static const lv_color_t CONNECTED_CLR = lv_color_hex(0x44BB44);

static constexpr int16_t CONTENT_Y = 30;
static constexpr int16_t PAD       = 10;

/* --- Callbacks --- */

void WifiSettingsScreen::onAdd(lv_event_t* e) {
    auto* self = (WifiSettingsScreen*)lv_event_get_user_data(e);
    self->showAddDialog();
}

void WifiSettingsScreen::onSave(lv_event_t* e) {
    auto* self = (WifiSettingsScreen*)lv_event_get_user_data(e);
    OSK::hide();
    self->saveNewNetwork();
}

void WifiSettingsScreen::onCancel(lv_event_t* e) {
    auto* self = (WifiSettingsScreen*)lv_event_get_user_data(e);
    OSK::hide();
    self->hideAddDialog();
}

void WifiSettingsScreen::onDelete(lv_event_t* e) {
    auto* self = (WifiSettingsScreen*)lv_event_get_user_data(e);
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    /* User data encodes: high byte = 0xFF marker, low byte = index */
    /* For simplicity, we store the index in the button's user data */
    lv_obj_t* btn = lv_event_get_target(e);
    idx = (uint8_t)(uintptr_t)btn->user_data;
    self->deleteNetwork(idx);
}

void WifiSettingsScreen::onSSIDFocus(lv_event_t* e) {
    lv_obj_t* ta = lv_event_get_target(e);
    OSK::show(ta);
}

void WifiSettingsScreen::onPassFocus(lv_event_t* e) {
    lv_obj_t* ta = lv_event_get_target(e);
    OSK::show(ta);
}

/* --- Screen creation --- */

void WifiSettingsScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* Header */
    lv_obj_t* header = lv_label_create(_screen);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(header, TEXT_SECONDARY, 0);
    lv_obj_set_pos(header, PAD + 4, CONTENT_Y + 8);
    lv_label_set_text(header, LV_SYMBOL_WIFI " WiFi Networks");

    /* Status label */
    _lblStatus = lv_label_create(_screen);
    lv_obj_set_style_text_font(_lblStatus, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblStatus, TEXT_SECONDARY, 0);
    lv_obj_align(_lblStatus, LV_ALIGN_TOP_RIGHT, -PAD, CONTENT_Y + 10);
    lv_label_set_text(_lblStatus, "");

    /* Add button */
    _btnAdd = lv_btn_create(_screen);
    lv_obj_set_size(_btnAdd, 120, 34);
    lv_obj_set_pos(_btnAdd, 670, CONTENT_Y + 4);
    lv_obj_set_style_bg_color(_btnAdd, ACCENT, 0);
    lv_obj_set_style_radius(_btnAdd, 8, 0);
    lv_obj_t* lblAdd = lv_label_create(_btnAdd);
    lv_label_set_text(lblAdd, LV_SYMBOL_PLUS " Add");
    lv_obj_set_style_text_color(lblAdd, TEXT_PRIMARY, 0);
    lv_obj_center(lblAdd);
    lv_obj_add_event_cb(_btnAdd, onAdd, LV_EVENT_CLICKED, this);

    /* Network list */
    _networkList = lv_obj_create(_screen);
    lv_obj_set_size(_networkList, 780, 340);
    lv_obj_set_pos(_networkList, PAD, CONTENT_Y + 45);
    lv_obj_set_style_bg_opa(_networkList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_networkList, 0, 0);
    lv_obj_set_style_pad_all(_networkList, 0, 0);
    lv_obj_set_style_pad_row(_networkList, 6, 0);
    lv_obj_set_flex_flow(_networkList, LV_FLEX_FLOW_COLUMN);

    /* Add dialog (hidden initially) */
    _dialogBg = lv_obj_create(_screen);
    lv_obj_set_size(_dialogBg, 400, 220);
    lv_obj_center(_dialogBg);
    lv_obj_set_style_bg_color(_dialogBg, DIALOG_BG, 0);
    lv_obj_set_style_bg_opa(_dialogBg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_dialogBg, 12, 0);
    lv_obj_set_style_border_width(_dialogBg, 1, 0);
    lv_obj_set_style_border_color(_dialogBg, ACCENT, 0);
    lv_obj_set_style_pad_all(_dialogBg, 16, 0);
    lv_obj_clear_flag(_dialogBg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_dialogBg, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* dlgTitle = lv_label_create(_dialogBg);
    lv_label_set_text(dlgTitle, "Add WiFi Network");
    lv_obj_set_style_text_font(dlgTitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dlgTitle, TEXT_PRIMARY, 0);
    lv_obj_align(dlgTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    /* SSID textarea */
    lv_obj_t* lblSSID = lv_label_create(_dialogBg);
    lv_label_set_text(lblSSID, "SSID:");
    lv_obj_set_style_text_font(lblSSID, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblSSID, TEXT_SECONDARY, 0);
    lv_obj_align(lblSSID, LV_ALIGN_TOP_LEFT, 0, 30);

    _taSSID = lv_textarea_create(_dialogBg);
    lv_obj_set_size(_taSSID, 360, 36);
    lv_obj_align(_taSSID, LV_ALIGN_TOP_LEFT, 0, 48);
    lv_textarea_set_max_length(_taSSID, DCC_SSID_LEN - 1);
    lv_textarea_set_one_line(_taSSID, true);
    lv_textarea_set_placeholder_text(_taSSID, "Network name");
    lv_obj_set_style_bg_color(_taSSID, CARD_BG, 0);
    lv_obj_set_style_text_color(_taSSID, TEXT_PRIMARY, 0);
    lv_obj_add_event_cb(_taSSID, onSSIDFocus, LV_EVENT_FOCUSED, this);

    /* Password textarea */
    lv_obj_t* lblPass = lv_label_create(_dialogBg);
    lv_label_set_text(lblPass, "Password:");
    lv_obj_set_style_text_font(lblPass, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblPass, TEXT_SECONDARY, 0);
    lv_obj_align(lblPass, LV_ALIGN_TOP_LEFT, 0, 90);

    _taPass = lv_textarea_create(_dialogBg);
    lv_obj_set_size(_taPass, 360, 36);
    lv_obj_align(_taPass, LV_ALIGN_TOP_LEFT, 0, 108);
    lv_textarea_set_max_length(_taPass, DCC_PASS_LEN - 1);
    lv_textarea_set_one_line(_taPass, true);
    lv_textarea_set_placeholder_text(_taPass, "Password (optional)");
    lv_textarea_set_password_mode(_taPass, true);
    lv_obj_set_style_bg_color(_taPass, CARD_BG, 0);
    lv_obj_set_style_text_color(_taPass, TEXT_PRIMARY, 0);
    lv_obj_add_event_cb(_taPass, onPassFocus, LV_EVENT_FOCUSED, this);

    /* Save / Cancel buttons */
    _btnSave = lv_btn_create(_dialogBg);
    lv_obj_set_size(_btnSave, 100, 34);
    lv_obj_align(_btnSave, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(_btnSave, ACCENT, 0);
    lv_obj_set_style_radius(_btnSave, 8, 0);
    lv_obj_t* lblSave = lv_label_create(_btnSave);
    lv_label_set_text(lblSave, "Save");
    lv_obj_set_style_text_color(lblSave, TEXT_PRIMARY, 0);
    lv_obj_center(lblSave);
    lv_obj_add_event_cb(_btnSave, onSave, LV_EVENT_CLICKED, this);

    _btnCancel = lv_btn_create(_dialogBg);
    lv_obj_set_size(_btnCancel, 100, 34);
    lv_obj_align(_btnCancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_btnCancel, BTN_BG, 0);
    lv_obj_set_style_radius(_btnCancel, 8, 0);
    lv_obj_t* lblCancel = lv_label_create(_btnCancel);
    lv_label_set_text(lblCancel, "Cancel");
    lv_obj_set_style_text_color(lblCancel, TEXT_PRIMARY, 0);
    lv_obj_center(lblCancel);
    lv_obj_add_event_cb(_btnCancel, onCancel, LV_EVENT_CLICKED, this);

    LOG_INFO("WIFI_SET: screen created");
}

/* --- Network list --- */

void WifiSettingsScreen::rebuildNetworkList() {
    lv_obj_clean(_networkList);

    String currentSSID = WifiManager::ssid();
    bool isConnected = (WifiManager::state() == WifiState::CONNECTED);

    if (_cfg.wifi_count == 0) {
        lv_obj_t* lbl = lv_label_create(_networkList);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, "No saved networks. Tap + to add one.");
        lv_obj_set_width(lbl, 760);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(lbl, 40, 0);
        return;
    }

    for (uint8_t i = 0; i < _cfg.wifi_count; i++) {
        lv_obj_t* card = lv_obj_create(_networkList);
        lv_obj_set_size(card, 760, 56);
        lv_obj_set_style_bg_color(card, CARD_BG, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 10, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        /* WiFi icon + SSID */
        lv_obj_t* lblIcon = lv_label_create(card);
        lv_obj_set_style_text_font(lblIcon, &lv_font_montserrat_16, 0);
        lv_obj_align(lblIcon, LV_ALIGN_LEFT_MID, 0, 0);
        lv_label_set_text(lblIcon, LV_SYMBOL_WIFI);

        bool active = isConnected &&
                      strcmp(_cfg.wifi[i].ssid, currentSSID.c_str()) == 0;
        lv_obj_set_style_text_color(lblIcon,
            active ? CONNECTED_CLR : TEXT_SECONDARY, 0);

        lv_obj_t* lblSSID = lv_label_create(card);
        lv_obj_set_style_text_font(lblSSID, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lblSSID, TEXT_PRIMARY, 0);
        lv_obj_align(lblSSID, LV_ALIGN_LEFT_MID, 28, 0);
        lv_obj_set_width(lblSSID, 500);
        lv_label_set_long_mode(lblSSID, LV_LABEL_LONG_DOT);
        lv_label_set_text(lblSSID, _cfg.wifi[i].ssid);

        /* Priority badge */
        lv_obj_t* lblPrio = lv_label_create(card);
        lv_obj_set_style_text_font(lblPrio, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lblPrio, TEXT_SECONDARY, 0);
        lv_obj_align(lblPrio, LV_ALIGN_RIGHT_MID, -80, 0);
        char prioBuf[16];
        snprintf(prioBuf, sizeof(prioBuf), "P%d", _cfg.wifi[i].priority);
        lv_label_set_text(lblPrio, prioBuf);

        /* Status text */
        if (active) {
            lv_obj_t* lblConn = lv_label_create(card);
            lv_obj_set_style_text_font(lblConn, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lblConn, CONNECTED_CLR, 0);
            lv_obj_align(lblConn, LV_ALIGN_RIGHT_MID, -120, 0);
            lv_label_set_text(lblConn, "Connected");
        }

        /* Delete button */
        lv_obj_t* btnDel = lv_btn_create(card);
        lv_obj_set_size(btnDel, 60, 30);
        lv_obj_align(btnDel, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(btnDel, DANGER, 0);
        lv_obj_set_style_radius(btnDel, 6, 0);
        btnDel->user_data = (void*)(uintptr_t)i;
        lv_obj_t* lblDel = lv_label_create(btnDel);
        lv_label_set_text(lblDel, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(lblDel, TEXT_PRIMARY, 0);
        lv_obj_center(lblDel);
        lv_obj_add_event_cb(btnDel, onDelete, LV_EVENT_CLICKED, this);
    }
}

/* --- Dialog --- */

void WifiSettingsScreen::showAddDialog() {
    if (_cfg.wifi_count >= DCC_MAX_WIFI) {
        lv_label_set_text(_lblStatus, "Max 5 networks reached");
        return;
    }
    lv_textarea_set_text(_taSSID, "");
    lv_textarea_set_text(_taPass, "");
    lv_obj_clear_flag(_dialogBg, LV_OBJ_FLAG_HIDDEN);
}

void WifiSettingsScreen::hideAddDialog() {
    lv_obj_add_flag(_dialogBg, LV_OBJ_FLAG_HIDDEN);
}

void WifiSettingsScreen::saveNewNetwork() {
    const char* ssid = lv_textarea_get_text(_taSSID);
    const char* pass = lv_textarea_get_text(_taPass);

    if (!ssid || ssid[0] == '\0') {
        lv_label_set_text(_lblStatus, "SSID required");
        return;
    }

    uint8_t idx = _cfg.wifi_count;
    strncpy(_cfg.wifi[idx].ssid, ssid, DCC_SSID_LEN - 1);
    _cfg.wifi[idx].ssid[DCC_SSID_LEN - 1] = '\0';
    strncpy(_cfg.wifi[idx].password, pass, DCC_PASS_LEN - 1);
    _cfg.wifi[idx].password[DCC_PASS_LEN - 1] = '\0';
    _cfg.wifi[idx].priority = idx;  // lowest priority for new entries
    _cfg.wifi_count++;

    ConfigStore::save(_cfg);
    hideAddDialog();
    rebuildNetworkList();
    lv_label_set_text(_lblStatus, "Network added");
}

void WifiSettingsScreen::deleteNetwork(uint8_t index) {
    if (index >= _cfg.wifi_count) return;

    /* Shift remaining entries down */
    for (uint8_t i = index; i < _cfg.wifi_count - 1; i++) {
        _cfg.wifi[i] = _cfg.wifi[i + 1];
    }
    _cfg.wifi_count--;

    ConfigStore::save(_cfg);
    rebuildNetworkList();
    lv_label_set_text(_lblStatus, "Network removed");
}

/* --- Lifecycle --- */

void WifiSettingsScreen::update(const DashboardData& data) {
    /* Update connection status display */
    if (WifiManager::state() == WifiState::CONNECTED) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s (%ddBm)",
                 WifiManager::ssid().c_str(), WifiManager::rssi());
        lv_label_set_text(_lblStatus, buf);
    } else {
        lv_label_set_text(_lblStatus, "Disconnected");
    }
}

void WifiSettingsScreen::onShow() {
    _cfg = ConfigStore::load();
    hideAddDialog();
    rebuildNetworkList();
}

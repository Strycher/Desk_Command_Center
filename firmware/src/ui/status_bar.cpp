/**
 * Status Bar — Implementation
 * 800 x 30px top bar on LVGL top layer.
 */

#include "ui/status_bar.h"
#include "ntp_time.h"
#include "wifi_manager.h"
#include "data_service.h"
#include "config_store.h"
#include "logger.h"

static const lv_color_t BAR_BG       = lv_color_hex(0x1a1a2e);
static const lv_color_t TEXT_COLOR    = lv_color_hex(0xCCCCDD);
static const lv_color_t SYNC_OK      = lv_color_hex(0x4CAF50);
static const lv_color_t SYNC_STALE   = lv_color_hex(0xFFC107);
static const lv_color_t SYNC_ERROR   = lv_color_hex(0xF44336);

static constexpr int16_t BAR_HEIGHT = 30;
static constexpr uint32_t STALE_THRESHOLD_MS = 120000; // 2 min

static lv_obj_t* bar       = nullptr;
static lv_obj_t* lblTime   = nullptr;
static lv_obj_t* lblDate   = nullptr;
static lv_obj_t* lblWifi   = nullptr;
static lv_obj_t* lblSync   = nullptr;

static bool _clock24h = false;

static void timerCb(lv_timer_t* timer) {
    StatusBar::update();
}

static const char* wifiIcon(int8_t rssi) {
    if (rssi == 0)   return LV_SYMBOL_CLOSE;     // disconnected
    if (rssi > -50)  return LV_SYMBOL_WIFI;       // excellent
    if (rssi > -70)  return LV_SYMBOL_WIFI;       // good
    return LV_SYMBOL_WARNING;                      // weak
}

static lv_color_t syncColor() {
    FetchState fs = DataService::state();
    if (fs == FetchState::ERROR) return SYNC_ERROR;
    if (fs == FetchState::SUCCESS) {
        uint32_t age = millis() - DataService::lastFetchMs();
        return (age > STALE_THRESHOLD_MS) ? SYNC_STALE : SYNC_OK;
    }
    return SYNC_STALE; // IDLE or FETCHING
}

void StatusBar::create() {
    lv_obj_t* layer = lv_layer_top();

    bar = lv_obj_create(layer);
    lv_obj_set_size(bar, 800, BAR_HEIGHT);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, BAR_BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 10, 0);
    lv_obj_set_style_pad_ver(bar, 2, 0);

    /* Time — left */
    lblTime = lv_label_create(bar);
    lv_obj_set_style_text_font(lblTime, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblTime, TEXT_COLOR, 0);
    lv_obj_align(lblTime, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_text(lblTime, "--:--");

    /* Date — center */
    lblDate = lv_label_create(bar);
    lv_obj_set_style_text_font(lblDate, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblDate, TEXT_COLOR, 0);
    lv_obj_align(lblDate, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lblDate, "---");

    /* WiFi icon — right */
    lblWifi = lv_label_create(bar);
    lv_obj_set_style_text_font(lblWifi, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblWifi, TEXT_COLOR, 0);
    lv_obj_align(lblWifi, LV_ALIGN_RIGHT_MID, -30, 0);
    lv_label_set_text(lblWifi, LV_SYMBOL_WIFI);

    /* Sync indicator — far right */
    lblSync = lv_label_create(bar);
    lv_obj_set_style_text_font(lblSync, &lv_font_montserrat_14, 0);
    lv_obj_align(lblSync, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_label_set_text(lblSync, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(lblSync, SYNC_STALE, 0);

    /* Update every 1 second */
    lv_timer_create(timerCb, 1000, nullptr);

    LOG_INFO("SB: status bar created");
}

void StatusBar::update() {
    if (!lblTime) return;

    /* Time */
    String t = NtpTime::timeStr(_clock24h);
    lv_label_set_text(lblTime, t.c_str());

    /* Date */
    String d = NtpTime::dateStr();
    lv_label_set_text(lblDate, d.c_str());

    /* WiFi */
    int8_t rssi = WifiManager::rssi();
    lv_label_set_text(lblWifi, wifiIcon(rssi));
    lv_color_t wifiColor = (WifiManager::state() == WifiState::CONNECTED)
                           ? SYNC_OK : SYNC_ERROR;
    lv_obj_set_style_text_color(lblWifi, wifiColor, 0);

    /* Sync */
    lv_obj_set_style_text_color(lblSync, syncColor(), 0);
}

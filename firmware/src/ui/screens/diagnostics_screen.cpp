/**
 * Diagnostics Screen — Implementation
 * Content area: y=30..430. Auto-refresh every 2 seconds.
 */

#include "ui/screens/diagnostics_screen.h"
#include "wifi_manager.h"
#include "data_service.h"
#include "logger.h"

static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG        = lv_color_hex(0x1a1a2e);
static const lv_color_t TEXT_PRIMARY   = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0xB0B0D0);
static const lv_color_t ACCENT         = lv_color_hex(0x6C63FF);
static const lv_color_t GOOD           = lv_color_hex(0x44BB44);
static const lv_color_t WARN           = lv_color_hex(0xFFAA00);
static const lv_color_t BAD            = lv_color_hex(0xFF4444);

static constexpr int16_t CONTENT_Y = 30;
static constexpr int16_t PAD       = 10;

#define FW_VERSION "0.2.0-dev"

static lv_obj_t* makeCard(lv_obj_t* parent, int16_t x, int16_t y,
                           int16_t w, int16_t h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t* addRow(lv_obj_t* parent, const char* label, int16_t y) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y);
    lv_label_set_text(lbl, label);

    lv_obj_t* val = lv_label_create(parent);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(val, TEXT_PRIMARY, 0);
    lv_obj_align(val, LV_ALIGN_TOP_LEFT, 140, y);
    lv_label_set_text(val, "--");
    return val;
}

void DiagnosticsScreen::onRefreshTimer(lv_timer_t* timer) {
    auto* self = (DiagnosticsScreen*)timer->user_data;
    self->refreshSystemInfo();
}

void DiagnosticsScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* Header */
    lv_obj_t* header = lv_label_create(_screen);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(header, TEXT_SECONDARY, 0);
    lv_obj_set_pos(header, PAD + 4, CONTENT_Y + 8);
    lv_label_set_text(header, LV_SYMBOL_SETTINGS " Diagnostics");

    /* === System Card (left) === */
    lv_obj_t* sysCard = makeCard(_screen, PAD, CONTENT_Y + 35, 380, 180);

    lv_obj_t* sysHdr = lv_label_create(sysCard);
    lv_label_set_text(sysHdr, "System");
    lv_obj_set_style_text_font(sysHdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sysHdr, TEXT_PRIMARY, 0);
    lv_obj_align(sysHdr, LV_ALIGN_TOP_LEFT, 0, 0);

    _lblVersion = addRow(sysCard, "FW Version:", 28);
    _lblHeap    = addRow(sysCard, "Free Heap:", 52);
    _lblPSRAM   = addRow(sysCard, "Free PSRAM:", 76);
    _lblUptime  = addRow(sysCard, "Uptime:", 100);

    /* === WiFi Card (right) === */
    lv_obj_t* wifiCard = makeCard(_screen, 400, CONTENT_Y + 35, 390, 180);

    lv_obj_t* wifiHdr = lv_label_create(wifiCard);
    lv_label_set_text(wifiHdr, "WiFi");
    lv_obj_set_style_text_font(wifiHdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifiHdr, TEXT_PRIMARY, 0);
    lv_obj_align(wifiHdr, LV_ALIGN_TOP_LEFT, 0, 0);

    _lblWifiSSID = addRow(wifiCard, "SSID:", 28);
    _lblWifiIP   = addRow(wifiCard, "IP:", 52);
    _lblWifiRSSI = addRow(wifiCard, "RSSI:", 76);

    /* === Data Service Card (bottom) === */
    lv_obj_t* dataCard = makeCard(_screen, PAD, CONTENT_Y + 230, 780, 120);

    lv_obj_t* dataHdr = lv_label_create(dataCard);
    lv_label_set_text(dataHdr, "Bridge Data Service");
    lv_obj_set_style_text_font(dataHdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dataHdr, TEXT_PRIMARY, 0);
    lv_obj_align(dataHdr, LV_ALIGN_TOP_LEFT, 0, 0);

    _lblLastFetch = addRow(dataCard, "Last Fetch:", 28);
    _lblFetchErr  = addRow(dataCard, "Last Error:", 52);

    /* Auto-refresh timer (2s, paused until shown) */
    _refreshTimer = lv_timer_create(onRefreshTimer, 2000, this);
    lv_timer_pause(_refreshTimer);

    LOG_INFO("DIAG: screen created");
}

void DiagnosticsScreen::refreshSystemInfo() {
    /* Version */
    lv_label_set_text(_lblVersion, FW_VERSION);

    /* Heap */
    char buf[64];
    uint32_t freeHeap = ESP.getFreeHeap();
    snprintf(buf, sizeof(buf), "%lu KB", freeHeap / 1024);
    lv_label_set_text(_lblHeap, buf);
    lv_obj_set_style_text_color(_lblHeap,
        freeHeap < 30000 ? BAD : (freeHeap < 60000 ? WARN : GOOD), 0);

    /* PSRAM */
    uint32_t freePSRAM = ESP.getFreePsram();
    snprintf(buf, sizeof(buf), "%lu KB", freePSRAM / 1024);
    lv_label_set_text(_lblPSRAM, buf);

    /* Uptime */
    uint32_t sec = millis() / 1000;
    uint32_t hr = sec / 3600;
    uint32_t mn = (sec % 3600) / 60;
    uint32_t sc = sec % 60;
    snprintf(buf, sizeof(buf), "%luh %02lum %02lus", hr, mn, sc);
    lv_label_set_text(_lblUptime, buf);

    /* WiFi */
    if (WifiManager::state() == WifiState::CONNECTED) {
        lv_label_set_text(_lblWifiSSID, WifiManager::ssid().c_str());
        lv_label_set_text(_lblWifiIP, WifiManager::ip().c_str());
        snprintf(buf, sizeof(buf), "%d dBm", WifiManager::rssi());
        lv_label_set_text(_lblWifiRSSI, buf);
        lv_obj_set_style_text_color(_lblWifiRSSI,
            WifiManager::rssi() > -60 ? GOOD : (WifiManager::rssi() > -75 ? WARN : BAD), 0);
    } else {
        lv_label_set_text(_lblWifiSSID, "Disconnected");
        lv_label_set_text(_lblWifiIP, "--");
        lv_label_set_text(_lblWifiRSSI, "--");
    }

    /* Data service */
    uint32_t lastMs = DataService::lastFetchMs();
    if (lastMs > 0) {
        uint32_t ago = (millis() - lastMs) / 1000;
        snprintf(buf, sizeof(buf), "%lus ago", ago);
        lv_label_set_text(_lblLastFetch, buf);
    } else {
        lv_label_set_text(_lblLastFetch, "Never");
    }

    String err = DataService::lastError();
    if (err.length() > 0) {
        lv_label_set_text(_lblFetchErr, err.c_str());
        lv_obj_set_style_text_color(_lblFetchErr, BAD, 0);
    } else {
        lv_label_set_text(_lblFetchErr, "None");
        lv_obj_set_style_text_color(_lblFetchErr, GOOD, 0);
    }
}

void DiagnosticsScreen::update(const DashboardData& data) {
    /* Diagnostics doesn't use dashboard data directly */
}

void DiagnosticsScreen::onShow() {
    refreshSystemInfo();
    if (_refreshTimer) lv_timer_resume(_refreshTimer);
}

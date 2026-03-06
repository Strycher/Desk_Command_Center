/**
 * Desk Command Center — Main Entry Point
 * CrowPanel Advance 5.0" (ESP32-S3-WROOM-1-N16R8)
 *
 * Boot sequence: Config → Display → LVGL → Splash → Backlight →
 *                WiFi → NTP → DataService → UI screens → Home
 */

#include <Arduino.h>
#include <lvgl.h>
#include "display_driver.h"
#include "pins_config.h"
#include "config_store.h"
#include "wifi_manager.h"
#include "backlight.h"
#include "ntp_time.h"
#include "data_service.h"
#include "dashboard_data.h"
#include "staleness_tracker.h"
#include "error_state.h"

/* UI */
#include "ui/splash_screen.h"
#include "ui/status_bar.h"
#include "ui/nav_bar.h"
#include "ui/osk.h"
#include "ui/screen_manager.h"
#include "ui/home_screen.h"
#include "ui/screens/calendar_screen.h"
#include "ui/screens/tasks_screen.h"
#include "ui/screens/weather_screen.h"
#include "ui/screens/devops_screen.h"
#include "ui/screens/ha_screen.h"
#include "ui/screens/claude_screen.h"
#include "ui/screens/wifi_settings_screen.h"
#include "ui/screens/settings_screen.h"
#include "ui/screens/diagnostics_screen.h"

static LGFX lcd;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

/* LVGL draw buffer — 1/10 screen in SRAM (avoids PSRAM bus contention
   with the RGB DMA peripheral that reads the frame buffer from PSRAM) */
static lv_color_t disp_draw_buf[SCREEN_WIDTH * SCREEN_HEIGHT / 10];

/* Screens — allocated once, never destroyed */
static SplashScreen         splash;
static HomeScreen           homeScreen;
static CalendarScreen       calendarScreen;
static TasksScreen          tasksScreen;
static WeatherScreen        weatherScreen;
static DevOpsScreen         devopsScreen;
static HAScreen             haScreen;
static ClaudeScreen         claudeScreen;
static WifiSettingsScreen   wifiSettingsScreen;
static SettingsScreen       settingsScreen;
static DiagnosticsScreen    diagnosticsScreen;

/* Dashboard data — parsed from bridge JSON */
static DashboardData dashData;

/* --- LVGL display flush callback (sync copy to RGB frame buffer) --- */
static void lvglFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    lcd.pushImage(area->x1, area->y1, w, h,
                  (lgfx::rgb565_t*)&color_p->full);
    lv_disp_flush_ready(drv);
}


/* --- LVGL touch read callback --- */
static uint32_t _touchPolls = 0;
static uint32_t _touchHits  = 0;

static void lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    _touchPolls++;
    uint16_t x, y;
    if (lcd.getTouch(&x, &y)) {
        _touchHits++;
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
        /* Log first 5 touches + every 50th after that */
        if (_touchHits <= 5 || _touchHits % 50 == 0) {
            Serial.printf("TOUCH: hit #%lu at (%d,%d) [polls=%lu]\n",
                          _touchHits, x, y, _touchPolls);
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    /* Report first poll + every ~5 seconds (500 polls at ~33/sec) */
    if (_touchPolls == 1 || _touchPolls % 500 == 0) {
        Serial.printf("TOUCH: polls=%lu hits=%lu\n", _touchPolls, _touchHits);
    }
}

/* --- Data callback: bridge JSON → DashboardData → all screens --- */
static void onBridgeData(JsonDocument& doc) {
    DashboardParser::parse(doc, dashData);
    dashData.last_updated_ms = millis();

    /* Mark all sources that have data as updated */
    if (dashData.google_calendar.status == SourceStatus::OK)
        StalenessTracker::markUpdated(DataSource::GOOGLE_CALENDAR);
    if (dashData.microsoft_calendar.status == SourceStatus::OK)
        StalenessTracker::markUpdated(DataSource::MICROSOFT_CALENDAR);
    if (dashData.weather.status == SourceStatus::OK)
        StalenessTracker::markUpdated(DataSource::WEATHER);
    if (dashData.unfocused_tasks.status == SourceStatus::OK)
        StalenessTracker::markUpdated(DataSource::UNFOCUSED_TASKS);
    if (dashData.monday_tasks.status == SourceStatus::OK)
        StalenessTracker::markUpdated(DataSource::MONDAY_TASKS);
    if (dashData.github.status == SourceStatus::OK)
        StalenessTracker::markUpdated(DataSource::GITHUB);
    if (dashData.home_assistant.status == SourceStatus::OK)
        StalenessTracker::markUpdated(DataSource::HOME_ASSISTANT);
    if (dashData.beads.status == SourceStatus::OK)
        StalenessTracker::markUpdated(DataSource::BEADS);
    if (dashData.claude.status == SourceStatus::OK)
        StalenessTracker::markUpdated(DataSource::CLAUDE);

    /* Update error state */
    ErrorState::recordSuccess();
    ErrorState::setCachedDataAvailable(true);

    /* Push data to all screens */
    ScreenManager::updateAll(dashData);

    Serial.printf("DCC: data updated, heap=%lu\n", ESP.getFreeHeap());
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Desk Command Center ===");

    /* Init config store */
    ConfigStore::init();
    DeviceConfig cfg = ConfigStore::load();

    /* GT911 touch reset — pull RST low for 120ms then release.
       Must happen BEFORE lcd.begin() which runs Touch_GT911::init().
       Factory V1.1 firmware uses GPIO 1 for GT911 reset.
       Verified via I2C scan: GT911 appears at 0x5D after this reset. */
    pinMode(PIN_TOUCH_RST_GPIO, OUTPUT);
    digitalWrite(PIN_TOUCH_RST_GPIO, LOW);
    delay(120);
    pinMode(PIN_TOUCH_RST_GPIO, INPUT);
    delay(300);  /* GT911 post-reset init time */
    Serial.println("TOUCH: GT911 reset done (GPIO 1, 300ms post-reset)");

    /* Init display + touch (PIN_TOUCH_RST=-1 → LovyanGFX skips its own
       1ms reset which is too short for GT911; our manual 120ms reset
       already brought the GT911 online at 0x5D). */
    lcd.begin();
    lcd.setColorDepth(16);
    lcd.fillScreen(TFT_BLACK);


    /* Init LVGL */
    lv_init();

    /* Single SRAM draw buffer, 1/10 screen size — matches Elecrow factory
       approach. SRAM buffer avoids bus contention with RGB DMA on PSRAM. */
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, nullptr,
                          SCREEN_WIDTH * SCREEN_HEIGHT / 10);

    /* Display driver — standard partial updates via pushImageDMA */
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = SCREEN_WIDTH;
    disp_drv.ver_res  = SCREEN_HEIGHT;
    disp_drv.flush_cb = lvglFlush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* Touch input driver */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvglTouchRead;
    lv_indev_drv_register(&indev_drv);

    Serial.printf("DCC: LVGL ready (%dx%d, SRAM buf %lu KB, pushImageDMA)\n",
                  SCREEN_WIDTH, SCREEN_HEIGHT,
                  sizeof(disp_draw_buf) / 1024);

    /* Show splash screen immediately */
    splash.create(nullptr);
    lv_scr_load(splash.screen());
    splash.updateStatus("Initializing...");
    lv_timer_handler();

    /* Init backlight */
    Backlight::init();
    Backlight::setBrightness(cfg.brightness);

    /* Init subsystems */
    splash.updateStatus("Connecting to WiFi...");
    lv_timer_handler();
    WifiManager::init(cfg);
    ErrorState::init();
    ErrorState::setWifiConnected(WifiManager::state() == WifiState::CONNECTED);

    splash.updateStatus("Syncing time...");
    lv_timer_handler();
    NtpTime::init(cfg.timezone);

    splash.updateStatus("Starting data service...");
    lv_timer_handler();
    StalenessTracker::init();
    DataService::init(cfg.bridge_url, cfg.poll_interval_sec);
    DataService::onData(onBridgeData);
    DataService::startTask();  /* Launch background network task on Core 0 */

    /* Register all screens */
    splash.updateStatus("Building UI...");
    lv_timer_handler();
    ScreenManager::init();
    ScreenManager::registerScreen(ScreenId::HOME,        &homeScreen);
    ScreenManager::registerScreen(ScreenId::CALENDAR,    &calendarScreen);
    ScreenManager::registerScreen(ScreenId::TASKS,       &tasksScreen);
    ScreenManager::registerScreen(ScreenId::WEATHER,     &weatherScreen);
    ScreenManager::registerScreen(ScreenId::DEVOPS,      &devopsScreen);
    ScreenManager::registerScreen(ScreenId::HA,          &haScreen);
    ScreenManager::registerScreen(ScreenId::CLAUDE,      &claudeScreen);
    ScreenManager::registerScreen(ScreenId::SETTINGS,    &settingsScreen);
    ScreenManager::registerScreen(ScreenId::DIAGNOSTICS, &diagnosticsScreen);

    /* Create persistent UI layers (status bar + nav bar + OSK) */
    StatusBar::create();
    NavBar::create();
    OSK::init();

    /* Transition from splash to home */
    splash.updateStatus("Ready!");
    lv_timer_handler();
    delay(500);

    ScreenManager::show(ScreenId::HOME);

    Serial.printf("DCC: boot complete, heap=%lu, PSRAM=%lu\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
}

void loop() {
    lv_timer_handler();
    WifiManager::check();
    NtpTime::check();

    /* Update error state from WiFi */
    ErrorState::setWifiConnected(WifiManager::state() == WifiState::CONNECTED);

    /* Check for new data from background network task (non-blocking) */
    DataService::checkReady();

    delay(5);
}

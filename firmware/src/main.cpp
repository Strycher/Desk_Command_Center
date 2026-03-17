/**
 * Desk Command Center — Main Entry Point (S3 / Arduino)
 * CrowPanel Advance 5.0" (ESP32-S3-WROOM-1-N16R8)
 *
 * Boot sequence: Serial → Logger → Config → Display (HAL) → Splash →
 *                Backlight → WiFi → NTP → SD → Logger SD → DataService →
 *                WebSerial → UI screens → Home
 *
 * P4 uses a separate entry point: platform/p4/main_p4.cpp
 */

#include <Arduino.h>
#include <lvgl.h>
#include "hal/display_hal.h"
#include "hal/platform_hal.h"
#include "logger.h"
#include "config_store.h"
#include "wifi_manager.h"
#include "ntp_time.h"
#include "sd_manager.h"
#include "web_serial.h"
#include "data_service.h"
#include "ha_command.h"
#include "ui/ha_control_modal.h"
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

/* Screens — allocated once, never destroyed */
static SplashScreen         splash;
static HomeScreen           homeScreen;
static CalendarScreen       calendarScreen;
static TasksScreen          tasksScreen;
static WeatherScreen        weatherScreen;
static DevOpsScreen         devopsScreen;
HAScreen                    haScreen;
static ClaudeScreen         claudeScreen;
static WifiSettingsScreen   wifiSettingsScreen;
static SettingsScreen       settingsScreen;
static DiagnosticsScreen    diagnosticsScreen;

/* Dashboard data — parsed from bridge JSON */
static DashboardData dashData;

/* --- Data callback: bridge JSON → DashboardData → all screens --- */
static void onBridgeData(JsonDocument& doc) {
    DashboardParser::parse(doc, dashData);
    dashData.last_updated_ms = hal::platform::uptimeMs();

    /* Mark all sources that have data as updated */
    if (dashData.google_calendar.status == SourceStatus::OK ||
        dashData.google_calendar.status == SourceStatus::STALE)
        StalenessTracker::markUpdated(DataSource::GOOGLE_CALENDAR);
    if (dashData.microsoft_calendar.status == SourceStatus::OK ||
        dashData.microsoft_calendar.status == SourceStatus::STALE)
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

    LOG_INFO("DCC: data updated, heap=%lu", hal::platform::heapFree());
}

void setup() {
    hal::platform::earlyInit();
    Logger::init();  /* Ring buffer + spinlock — before any LOG_* calls */
    LOG_INFO("=== Desk Command Center (%s) ===", hal::platform::chipModel());

    /* Init config store */
    ConfigStore::init();
    DeviceConfig cfg = ConfigStore::load();

    /* Init display hardware + LVGL + touch (platform-specific via HAL) */
    if (!hal::display::init()) {
        LOG_ERROR("Display init FAILED — halting");
        while (true) hal::platform::delayMs(1000);
    }

    /* Show splash screen immediately */
    splash.create(nullptr);
    lv_scr_load(splash.screen());
    splash.updateStatus("Initializing...");
    hal::display::tick();

    /* Backlight on */
    hal::display::setBacklight(cfg.brightness);

    /* Init subsystems */
    splash.updateStatus("Connecting to WiFi...");
    hal::display::tick();
    WifiManager::init(cfg);
    ErrorState::init();
    ErrorState::setWifiConnected(WifiManager::state() == WifiState::CONNECTED);

    splash.updateStatus("Syncing time...");
    hal::display::tick();
    NtpTime::init(cfg.timezone);

    /* Init SD card + open session log file */
    SDManager::init();
    Logger::initSDLog();

    splash.updateStatus("Starting data service...");
    hal::display::tick();
    StalenessTracker::init();
    DataService::init(cfg.bridge_url, cfg.poll_interval_sec, cfg.device_key);
    HACommand::init(cfg.bridge_url);
    HACommand::onResult([](bool success, const char* entityId,
                           const char* newState, const char* error) {
        HAControlModal::onCommandResult(success, newState, error);
    });
    DataService::onData(onBridgeData);
    DataService::startTask();  /* Launch background network task on Core 0 */

    /* Start web serial monitor (needs WiFi) */
    WebSerial::init();

    /* Register all screens */
    splash.updateStatus("Building UI...");
    hal::display::tick();
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
    hal::display::tick();
    hal::platform::delayMs(500);

    ScreenManager::show(ScreenId::HOME);

    LOG_INFO("DCC: boot complete, heap=%lu, PSRAM=%lu",
             hal::platform::heapFree(), hal::platform::psramFree());
}

void loop() {
    hal::display::tick();
    WifiManager::check();
    NtpTime::check();

    /* Update error state from WiFi */
    ErrorState::setWifiConnected(WifiManager::state() == WifiState::CONNECTED);

    /* Check for new data from background network task (non-blocking) */
    DataService::checkReady();
    HACommand::checkResult();

    /* Logger: flush SD buffer + rotation check */
    Logger::tick();

    /* Web serial monitor: start when WiFi connects, then process HTTP */
    WebSerial::init();  /* no-op if already started or no WiFi */
    WebSerial::handleClient();

    hal::platform::delayMs(5);
}

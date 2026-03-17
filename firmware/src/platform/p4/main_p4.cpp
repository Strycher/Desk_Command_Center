/**
 * Desk Command Center — P4 Entry Point (ESP-IDF app_main)
 * CrowPanel Advance 7.0" (ESP32-P4 + ESP32-C6 companion)
 *
 * Boot sequence: Platform → Logger → Config → Display (HAL) → Splash →
 *                Backlight → WiFi → NTP → DataService → UI screens → Home
 *
 * Unlike S3 (Arduino setup/loop), the P4 uses ESP-IDF's app_main()
 * with an explicit main loop task. LVGL runs in its own task via
 * esp_lvgl_port — all LVGL calls must be guarded with lock/unlock.
 */
#if defined(CROWPANEL_P4)

#include "hal/display_hal.h"
#include "hal/platform_hal.h"
#include "hal/network_hal.h"
#include "logger.h"
#include "config_store.h"
#include "wifi_manager.h"
#include "ntp_time.h"
#include "sd_manager.h"
#include "web_serial.h"
#include "data_service.h"
#include "ha_command.h"
#include "dashboard_data.h"
#include "staleness_tracker.h"
#include "error_state.h"
#include "ui/ha_control_modal.h"

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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

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

    /* Push data to all screens (must hold LVGL lock) */
    if (hal::display::lock(100)) {
        ScreenManager::updateAll(dashData);
        hal::display::unlock();
    }

    LOG_INFO("DCC: data updated, heap=%lu", hal::platform::heapFree());
}

extern "C" void app_main(void)
{
    hal::platform::earlyInit();
    Logger::init();
    LOG_INFO("=== Desk Command Center (%s, 7\") ===", hal::platform::chipModel());

    /* Init config store */
    ConfigStore::init();
    DeviceConfig cfg = ConfigStore::load();

    /* Init display hardware + LVGL + touch (platform-specific via HAL) */
    if (!hal::display::init()) {
        LOG_ERROR("Display init FAILED — halting");
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    /* Show splash screen (must hold LVGL lock on P4) */
    if (hal::display::lock(1000)) {
        splash.create(nullptr);
        lv_scr_load(splash.screen());
        splash.updateStatus("Initializing...");
        hal::display::unlock();
    }

    /* Backlight on */
    hal::display::setBacklight(cfg.brightness);

    /* Init subsystems */
    if (hal::display::lock(100)) {
        splash.updateStatus("Connecting to WiFi...");
        hal::display::unlock();
    }
    WifiManager::init(cfg);
    ErrorState::init();
    ErrorState::setWifiConnected(WifiManager::state() == WifiState::CONNECTED);

    if (hal::display::lock(100)) {
        splash.updateStatus("Syncing time...");
        hal::display::unlock();
    }
    NtpTime::init(cfg.timezone);

    /* SD card — P4 stub returns false, logger gracefully disables SD logging */
    SDManager::init();
    Logger::initSDLog();

    if (hal::display::lock(100)) {
        splash.updateStatus("Starting data service...");
        hal::display::unlock();
    }
    StalenessTracker::init();
    DataService::init(cfg.bridge_url, cfg.poll_interval_sec, cfg.device_key);
    HACommand::init(cfg.bridge_url);
    HACommand::onResult([](bool success, const char* entityId,
                           const char* newState, const char* error) {
        HAControlModal::onCommandResult(success, newState, error);
    });
    DataService::onData(onBridgeData);
    DataService::startTask();

    /* Web serial — P4 stub is no-op */
    WebSerial::init();

    /* Register all screens (must hold LVGL lock) */
    if (hal::display::lock(1000)) {
        splash.updateStatus("Building UI...");

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

        /* Create persistent UI layers */
        StatusBar::create();
        NavBar::create();
        OSK::init();

        /* Transition from splash to home */
        splash.updateStatus("Ready!");
        hal::display::unlock();
    }

    hal::platform::delayMs(500);

    if (hal::display::lock(100)) {
        ScreenManager::show(ScreenId::HOME);
        hal::display::unlock();
    }

    LOG_INFO("DCC: boot complete, heap=%lu, PSRAM=%lu",
             hal::platform::heapFree(), hal::platform::psramFree());

    /* Main loop — equivalent to S3's loop() */
    for (;;) {
        WifiManager::check();
        NtpTime::check();

        /* Update error state from WiFi */
        ErrorState::setWifiConnected(WifiManager::state() == WifiState::CONNECTED);

        /* Check for new data from background network task */
        DataService::checkReady();
        HACommand::checkResult();

        /* Logger: flush SD buffer + rotation check */
        Logger::tick();

        /* Web serial (P4: no-op) */
        WebSerial::init();
        WebSerial::handleClient();

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

#endif  // CROWPANEL_P4

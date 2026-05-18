# Main component — P4 ESP-IDF build
# Compiles P4 platform files + shared modules.
# S3 uses Arduino framework separately (platformio.ini).

set(P4_PLATFORM_SRCS
    "platform/p4/main_p4.cpp"
    "platform/p4/display_p4.cpp"
    "platform/p4/platform_p4.cpp"
    "platform/p4/network_p4.cpp"
    "platform/p4/config_store_p4.cpp"
    "platform/p4/sd_manager_p4.cpp"
    "platform/p4/web_serial_p4.cpp"
)

set(SHARED_SRCS
    "wifi_manager.cpp"
    "data_service.cpp"
    "ha_command.cpp"
    "ntp_time.cpp"
    "time_helpers.cpp"
    "logger.cpp"
    "error_state.cpp"
    "staleness_tracker.cpp"
    "dashboard_data.cpp"
)

# UI sources — LVGL screens and widgets
set(UI_SRCS
    "ui/splash_screen.cpp"
    "ui/home_screen.cpp"
    "ui/status_bar.cpp"
    "ui/nav_bar.cpp"
    "ui/osk.cpp"
    "ui/screen_manager.cpp"
    "ui/ha_control_modal.cpp"
    "ui/weather_icon.cpp"
    "ui/screens/calendar_screen.cpp"
    "ui/screens/claude_screen.cpp"
    "ui/screens/devops_screen.cpp"
    "ui/screens/diagnostics_screen.cpp"
    "ui/screens/ha_screen.cpp"
    "ui/screens/settings_screen.cpp"
    "ui/screens/tasks_screen.cpp"
    "ui/screens/weather_screen.cpp"
    "ui/screens/wifi_settings_screen.cpp"
)

idf_component_register(
    SRCS ${P4_PLATFORM_SRCS} ${SHARED_SRCS} ${UI_SRCS}
    INCLUDE_DIRS "../include"
)

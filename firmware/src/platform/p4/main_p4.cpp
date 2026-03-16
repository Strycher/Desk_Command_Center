/**
 * P4 Entry Point — ESP-IDF app_main().
 *
 * Minimal display test: initializes MIPI-DSI display via HAL,
 * turns on backlight, shows "Hello DCC" via LVGL.
 */
#if defined(CROWPANEL_P4)

#include "hal/display_hal.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "main_p4";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== Desk Command Center (P4) ===");

    // Initialize display + LVGL
    if (!hal::display::init()) {
        ESP_LOGE(TAG, "Display init FAILED — halting");
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // Turn on backlight
    hal::display::setBacklight(100);
    ESP_LOGI(TAG, "Backlight ON");

    // Show test label
    if (lvgl_port_lock(0)) {
        lv_obj_t* scr = lv_scr_act();
        lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

        lv_obj_t* label = lv_label_create(scr);
        lv_label_set_text(label, "Hello DCC");

        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_text_font(&style, &lv_font_montserrat_42);
        lv_style_set_text_color(&style, lv_color_black());
        lv_obj_add_style(label, &style, LV_PART_MAIN);

        lv_obj_center(label);
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Display test running. Screen: %dx%d",
             hal::display::screenWidth(), hal::display::screenHeight());

    // esp_lvgl_port manages the LVGL task — nothing to do in main
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#endif  // CROWPANEL_P4

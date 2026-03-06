/**
 * Desk Command Center — Main Entry Point
 * CrowPanel Advance 5.0" (ESP32-S3)
 *
 * Task 1.1: Compiles clean with display + LVGL initialized.
 * No UI yet — that comes in tasks 1.2–1.5.
 */

#include <Arduino.h>
#include <lvgl.h>
#include "display_driver.h"
#include "config_store.h"

static LGFX lcd;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

/* Double buffer in PSRAM — 800 * 40 lines * 2 bytes * 2 buffers = ~125 KB */
static constexpr uint32_t BUF_LINES = 40;
static lv_color_t* buf1 = nullptr;
static lv_color_t* buf2 = nullptr;

/* --- LVGL display flush callback --- */
static void lvglFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.writePixels((uint16_t*)color_p, w * h);
    lcd.endWrite();
    lv_disp_flush_ready(drv);
}

/* --- LVGL touch read callback --- */
static void lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    uint16_t x, y;
    if (lcd.getTouch(&x, &y)) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("DCC: starting...");

    /* Init config store */
    ConfigStore::init();
    DeviceConfig cfg = ConfigStore::load();

    /* Init display */
    lcd.begin();
    lcd.setColorDepth(16);
    lcd.fillScreen(TFT_BLACK);

    /* Init LVGL */
    lv_init();

    /* Allocate draw buffers in PSRAM */
    buf1 = (lv_color_t*)ps_malloc(SCREEN_WIDTH * BUF_LINES * sizeof(lv_color_t));
    buf2 = (lv_color_t*)ps_malloc(SCREEN_WIDTH * BUF_LINES * sizeof(lv_color_t));
    if (!buf1 || !buf2) {
        Serial.println("ERROR: PSRAM buffer allocation failed");
        return;
    }
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCREEN_WIDTH * BUF_LINES);

    /* Display driver */
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

    Serial.printf("DCC: LVGL ready (%dx%d, %lu KB PSRAM buffers)\n",
                  SCREEN_WIDTH, SCREEN_HEIGHT,
                  (SCREEN_WIDTH * BUF_LINES * sizeof(lv_color_t) * 2) / 1024);
    Serial.printf("DCC: Free heap: %lu, PSRAM: %lu\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
}

void loop() {
    lv_timer_handler();
    delay(5);
}

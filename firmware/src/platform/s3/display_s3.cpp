/**
 * Display HAL — S3 Implementation (CrowPanel 5.0")
 *
 * LovyanGFX RGB display + GT911 touch + STC8H1K28 I2C backlight.
 * Manual LVGL setup with draw buffer in SRAM.
 *
 * LVGL runs on the caller's thread (Core 1 loop). tick() drives
 * lv_timer_handler(). lock()/unlock() are no-ops.
 */

#if !defined(CROWPANEL_P4)

#include "hal/display_hal.h"

#include <Arduino.h>
#include <lvgl.h>
#include "driver/i2c.h"
#include "display_driver.h"
#include "pins_config.h"
#include "logger.h"

// ---- LovyanGFX display instance ----
static LGFX s_lcd;

// ---- LVGL draw buffer (SRAM — avoids PSRAM bus contention with RGB DMA) ----
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_disp_buf[SCREEN_WIDTH * SCREEN_HEIGHT / 10];

static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

// ---- Backlight via STC8H1K28 I2C ----
static constexpr uint8_t BL_ADDR    = BACKLIGHT_I2C_ADDR;
static constexpr uint8_t BL_REG_MIN = 0x05;
static constexpr uint8_t BL_REG_MAX = 0x10;
static uint8_t s_brightness = 0;

static uint8_t percentToReg(uint8_t percent) {
    if (percent > 100) percent = 100;
    return BL_REG_MIN + (uint8_t)((uint16_t)percent * (BL_REG_MAX - BL_REG_MIN) / 100);
}

static void backlightWrite(uint8_t val) {
    /* Use ESP-IDF i2c driver directly — LovyanGFX owns I2C_NUM_0 for
       GT911 touch, and Arduino Wire silently fails on the shared bus
       (endTransmission returns 0 but no bytes are actually sent). */
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BL_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(TOUCH_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        LOG_ERROR("BL: I2C write 0x%02X error %d (%s)", val, err, esp_err_to_name(err));
    }
}

// ---- Touch stats ----
static uint32_t s_touchPolls = 0;
static uint32_t s_touchHits  = 0;

// ---- LVGL flush callback ----
static void lvglFlush(lv_disp_drv_t* drv, const lv_area_t* area,
                      lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    s_lcd.pushImage(area->x1, area->y1, w, h,
                    (lgfx::rgb565_t*)&color_p->full);
    lv_disp_flush_ready(drv);
}

// ---- LVGL touch callback ----
static void lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    s_touchPolls++;
    uint16_t x, y;
    if (s_lcd.getTouch(&x, &y)) {
        s_touchHits++;
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
        if (s_touchHits <= 5 || s_touchHits % 50 == 0) {
            LOG_DEBUG("TOUCH: hit #%lu at (%d,%d) [polls=%lu]",
                      s_touchHits, x, y, s_touchPolls);
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    if (s_touchPolls == 1 || s_touchPolls % 500 == 0) {
        LOG_DEBUG("TOUCH: polls=%lu hits=%lu", s_touchPolls, s_touchHits);
    }
}

// ---- HAL implementation ----

namespace hal {
namespace display {

bool init()
{
    /* GT911 touch reset — pull RST low for 120ms then release.
       Must happen BEFORE lcd.begin() which runs Touch_GT911::init().
       Factory V1.1 firmware uses GPIO 1 for GT911 reset.
       Verified via I2C scan: GT911 appears at 0x5D after this reset. */
    pinMode(PIN_TOUCH_RST_GPIO, OUTPUT);
    digitalWrite(PIN_TOUCH_RST_GPIO, LOW);
    delay(120);
    pinMode(PIN_TOUCH_RST_GPIO, INPUT);
    delay(300);  /* GT911 post-reset init time */
    LOG_INFO("TOUCH: GT911 reset done (GPIO 1, 300ms post-reset)");

    /* Init display + touch */
    s_lcd.begin();
    s_lcd.setColorDepth(16);
    s_lcd.fillScreen(TFT_BLACK);

    /* Init LVGL */
    lv_init();

    /* Single SRAM draw buffer, 1/10 screen size */
    lv_disp_draw_buf_init(&s_draw_buf, s_disp_buf, nullptr,
                          SCREEN_WIDTH * SCREEN_HEIGHT / 10);

    /* Display driver */
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = SCREEN_WIDTH;
    s_disp_drv.ver_res  = SCREEN_HEIGHT;
    s_disp_drv.flush_cb = lvglFlush;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    /* Touch input driver */
    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = lvglTouchRead;
    lv_indev_drv_register(&s_indev_drv);

    LOG_INFO("DISPLAY: LVGL ready (%dx%d, SRAM buf %lu KB)",
             SCREEN_WIDTH, SCREEN_HEIGHT,
             sizeof(s_disp_buf) / 1024);

    /* Wake backlight chip — STC8H1K28 requires an initial write
       before it responds to brightness commands. */
    backlightWrite(BL_REG_MAX);
    s_brightness = 100;

    return true;
}

void tick()
{
    lv_timer_handler();
}

bool lock(uint32_t timeout_ms)
{
    /* S3: LVGL runs on caller's thread — no contention */
    (void)timeout_ms;
    return true;
}

void unlock()
{
    /* S3: no-op */
}

void setBacklight(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_brightness = percent;
    backlightWrite(percentToReg(percent));
    LOG_INFO("BL: brightness %d%% (reg=0x%02X)", percent, percentToReg(percent));
}

uint8_t getBacklight()
{
    return s_brightness;
}

uint16_t screenWidth()  { return SCREEN_WIDTH; }
uint16_t screenHeight() { return SCREEN_HEIGHT; }

}  // namespace display
}  // namespace hal

#endif  // !CROWPANEL_P4

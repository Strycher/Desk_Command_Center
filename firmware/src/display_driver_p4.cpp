/**
 * CrowPanel 7.0" (ESP32-P4) — MIPI-DSI Display Driver Implementation
 *
 * Initializes EK79007 panel over 2-lane MIPI-DSI with DPI output.
 * Touch: GT911 via I2C on GPIO 45/46.
 * Backlight: PWM on GPIO 31 @ 30 kHz.
 */

#if defined(CROWPANEL_P4)

#include "display_driver_p4.h"
#include <lvgl.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_commands.h>
#include <esp_ldo_regulator.h>
#include <driver/ledc.h>
#include <Wire.h>
#include "logger.h"

namespace P4Display {

esp_lcd_panel_handle_t panel = nullptr;
static esp_lcd_dsi_bus_handle_t dsi_bus = nullptr;
static esp_lcd_panel_io_handle_t io_handle = nullptr;

/* ── Touch state ─────────────────────────────────────────── */
static uint32_t _touchPolls = 0;
static uint32_t _touchHits  = 0;

/* ── GT911 touch read via I2C ────────────────────────────── */
static bool gt911Read(uint16_t* x, uint16_t* y) {
    /* Read touch status register (0x814E) */
    Wire.beginTransmission(P4_TOUCH_I2C_ADDR);
    Wire.write(0x81);
    Wire.write(0x4E);
    if (Wire.endTransmission() != 0) return false;

    Wire.requestFrom((uint8_t)P4_TOUCH_I2C_ADDR, (uint8_t)1);
    if (!Wire.available()) return false;
    uint8_t status = Wire.read();

    uint8_t touchCount = status & 0x0F;
    bool bufferReady   = (status & 0x80) != 0;

    if (!bufferReady || touchCount == 0) {
        /* Clear status register */
        Wire.beginTransmission(P4_TOUCH_I2C_ADDR);
        Wire.write(0x81);
        Wire.write(0x4E);
        Wire.write(0x00);
        Wire.endTransmission();
        return false;
    }

    /* Read first touch point (0x8150-0x8153: x_lo, x_hi, y_lo, y_hi) */
    Wire.beginTransmission(P4_TOUCH_I2C_ADDR);
    Wire.write(0x81);
    Wire.write(0x50);
    if (Wire.endTransmission() != 0) return false;

    Wire.requestFrom((uint8_t)P4_TOUCH_I2C_ADDR, (uint8_t)4);
    if (Wire.available() < 4) return false;

    uint8_t x_lo = Wire.read();
    uint8_t x_hi = Wire.read();
    uint8_t y_lo = Wire.read();
    uint8_t y_hi = Wire.read();

    *x = (uint16_t)x_lo | ((uint16_t)x_hi << 8);
    *y = (uint16_t)y_lo | ((uint16_t)y_hi << 8);

    /* Clear status register */
    Wire.beginTransmission(P4_TOUCH_I2C_ADDR);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write(0x00);
    Wire.endTransmission();

    return true;
}

/* ── Backlight PWM ───────────────────────────────────────── */
static void initBacklight() {
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_cfg.duty_resolution = LEDC_TIMER_10_BIT;
    timer_cfg.timer_num  = LEDC_TIMER_0;
    timer_cfg.freq_hz    = 30000;  /* 30 kHz */
    timer_cfg.clk_cfg    = LEDC_AUTO_CLK;
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel    = LEDC_CHANNEL_0;
    ch_cfg.timer_sel  = LEDC_TIMER_0;
    ch_cfg.gpio_num   = P4_BACKLIGHT_PIN;
    ch_cfg.duty       = 1023;  /* Max brightness */
    ch_cfg.hpoint     = 0;
    ledc_channel_config(&ch_cfg);

    LOG_INFO("P4 BL: PWM init on GPIO%d @ 30kHz", P4_BACKLIGHT_PIN);
}

/* ── MIPI-DSI display init ───────────────────────────────── */
bool init() {
    LOG_INFO("P4 DSI: initializing MIPI-DSI display");

    /* 1. Power LDO for MIPI DPHY (2.5V) */
    esp_ldo_channel_handle_t ldo_mipi = nullptr;
    esp_ldo_channel_config_t ldo_cfg = {};
    ldo_cfg.chan_id = P4_LDO_MIPI_PHY_CHAN;
    ldo_cfg.voltage_mv = 2500;
    esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi);
    if (err != ESP_OK) {
        LOG_ERROR("P4 DSI: LDO init failed: %d", err);
        return false;
    }
    LOG_INFO("P4 DSI: DPHY LDO enabled (2.5V, chan %d)", P4_LDO_MIPI_PHY_CHAN);

    /* 2. Create MIPI-DSI bus */
    esp_lcd_dsi_bus_config_t bus_cfg = {};
    bus_cfg.bus_id = 0;
    bus_cfg.num_data_lanes = P4_DSI_LANE_NUM;
    bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_cfg.lane_bit_rate_mbps = P4_DSI_LANE_BITRATE_MBPS;
    err = esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus);
    if (err != ESP_OK) {
        LOG_ERROR("P4 DSI: bus create failed: %d", err);
        return false;
    }
    LOG_INFO("P4 DSI: bus created (%d lanes @ %d Mbps)",
             P4_DSI_LANE_NUM, P4_DSI_LANE_BITRATE_MBPS);

    /* 3. Create DBI IO for sending init commands to EK79007 */
    esp_lcd_dbi_io_config_t dbi_cfg = {};
    dbi_cfg.virtual_channel = 0;
    dbi_cfg.lcd_cmd_bits = 8;
    dbi_cfg.lcd_param_bits = 8;
    err = esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &io_handle);
    if (err != ESP_OK) {
        LOG_ERROR("P4 DSI: panel IO create failed: %d", err);
        return false;
    }

    /* 4. Send MIPI DCS init commands to EK79007 (sleep out + display on) */
    esp_lcd_panel_io_tx_param(io_handle, LCD_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    esp_lcd_panel_io_tx_param(io_handle, LCD_CMD_DISPON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    LOG_INFO("P4 DSI: EK79007 init commands sent (sleep-out + display-on)");

    /* 5. Create DPI panel with timing config */
    esp_lcd_dpi_panel_config_t dpi_cfg = {};
    dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_cfg.dpi_clock_freq_mhz = P4_DPI_CLK_MHZ;
    dpi_cfg.virtual_channel = 0;
    dpi_cfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi_cfg.video_timing.h_size = P4_LCD_H_RES;
    dpi_cfg.video_timing.v_size = P4_LCD_V_RES;
    dpi_cfg.video_timing.hsync_pulse_width = P4_LCD_HSYNC;
    dpi_cfg.video_timing.hsync_back_porch  = P4_LCD_HBP;
    dpi_cfg.video_timing.hsync_front_porch = P4_LCD_HFP;
    dpi_cfg.video_timing.vsync_pulse_width = P4_LCD_VSYNC;
    dpi_cfg.video_timing.vsync_back_porch  = P4_LCD_VBP;
    dpi_cfg.video_timing.vsync_front_porch = P4_LCD_VFP;

    err = esp_lcd_new_panel_dpi(dsi_bus, &dpi_cfg, &panel);
    if (err != ESP_OK) {
        LOG_ERROR("P4 DSI: DPI panel create failed: %d", err);
        return false;
    }
    LOG_INFO("P4 DSI: DPI panel initialized (%dx%d @ %d MHz)",
             P4_LCD_H_RES, P4_LCD_V_RES, P4_DPI_CLK_MHZ);

    /* 6. Init backlight PWM */
    initBacklight();

    /* 7. Init GT911 touch via I2C */
    pinMode(P4_TOUCH_RST, OUTPUT);
    digitalWrite(P4_TOUCH_RST, LOW);
    delay(120);
    pinMode(P4_TOUCH_RST, INPUT);
    delay(300);  /* GT911 post-reset init time */

    Wire.begin(P4_TOUCH_SDA, P4_TOUCH_SCL, 400000);
    LOG_INFO("P4 TOUCH: GT911 init (SDA=%d, SCL=%d, INT=%d, RST=%d)",
             P4_TOUCH_SDA, P4_TOUCH_SCL, P4_TOUCH_INT, P4_TOUCH_RST);

    return true;
}

/* ── LVGL flush callback ─────────────────────────────────── */
void lvglFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    if (panel) {
        esp_lcd_panel_draw_bitmap(panel,
            area->x1, area->y1,
            area->x2 + 1, area->y2 + 1,
            color_p);
    }
    lv_disp_flush_ready(drv);
}

/* ── LVGL touch read callback ────────────────────────────── */
void lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    _touchPolls++;
    uint16_t x, y;
    if (gt911Read(&x, &y)) {
        _touchHits++;
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
        if (_touchHits <= 5 || _touchHits % 50 == 0) {
            LOG_DEBUG("P4 TOUCH: hit #%lu at (%d,%d) [polls=%lu]",
                      _touchHits, x, y, _touchPolls);
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    if (_touchPolls == 1 || _touchPolls % 500 == 0) {
        LOG_DEBUG("P4 TOUCH: polls=%lu hits=%lu", _touchPolls, _touchHits);
    }
}

/* ── Backlight control ───────────────────────────────────── */
void setBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)percent * 1023 / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    LOG_INFO("P4 BL: brightness %d%% (duty=%lu)", percent, duty);
}

}  /* namespace P4Display */

#endif /* CROWPANEL_P4 */

/**
 * P4 Display Driver — MIPI-DSI + EK79007 + LVGL via esp_lvgl_port.
 *
 * Based on Elecrow CrowPanel 7" BSP (Lesson07-Turn_on_the_screen).
 * Uses esp_lcd_ek79007 component for panel init, esp_lvgl_port for
 * managed LVGL integration (task, locking, DMA2D).
 */
#if defined(CROWPANEL_P4)

#include "hal/display_hal.h"
#include "pins_config.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_ek79007.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_touch_gt911.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

static const char* TAG = "display_p4";

// ----- Hardware constants -----
#define LCD_H_RES          1024
#define LCD_V_RES          600
#define LCD_BITS_PER_PIXEL 16
#define LCD_GPIO_BACKLIGHT 31
#define BACKLIGHT_PWM_HZ   30000

// ----- Handles -----
static esp_lcd_panel_handle_t    s_panel     = NULL;
static esp_lcd_dsi_bus_handle_t  s_dsi_bus   = NULL;
static esp_lcd_panel_io_handle_t s_dbi_io    = NULL;
static esp_ldo_channel_handle_t  s_ldo3      = NULL;
static esp_ldo_channel_handle_t  s_ldo4      = NULL;
static lv_display_t*             s_lvgl_disp = NULL;
static esp_lcd_touch_handle_t    s_touch     = NULL;
static lv_indev_t*               s_touch_indev = NULL;

// ---- Backlight (LEDC PWM) ----

static esp_err_t backlight_init()
{
    const gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << LCD_GPIO_BACKLIGHT),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&gpio_cfg);
    if (err != ESP_OK) return err;

    const ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_11_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = BACKLIGHT_PWM_HZ,
        .clk_cfg         = LEDC_USE_PLL_DIV_CLK,
    };
    err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) return err;

    const ledc_channel_config_t ch_cfg = {
        .gpio_num   = LCD_GPIO_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    };
    return ledc_channel_config(&ch_cfg);
}

// ---- MIPI-DSI Display ----

static esp_err_t display_port_init()
{
    esp_err_t err;

    // DSI bus
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id            = 0,
        .num_data_lanes    = 2,
        .phy_clk_src       = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 900,
    };
    err = esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus);
    if (err != ESP_OK) return err;

    // DBI IO (for sending init commands to EK79007)
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    err = esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &s_dbi_io);
    if (err != ESP_OK) return err;

    // DPI panel config
    lcd_color_rgb_pixel_format_t px_fmt = LCD_COLOR_PIXEL_FORMAT_RGB565;
    if (LCD_BITS_PER_PIXEL == 24)
        px_fmt = LCD_COLOR_PIXEL_FORMAT_RGB888;
    else if (LCD_BITS_PER_PIXEL == 18)
        px_fmt = LCD_COLOR_PIXEL_FORMAT_RGB666;

    const esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel   = 0,
        .dpi_clk_src       = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 51,
        .pixel_format      = px_fmt,
        .num_fbs           = 1,
        .video_timing = {
            .h_size             = LCD_H_RES,
            .v_size             = LCD_V_RES,
            .hsync_pulse_width  = 70,
            .hsync_back_porch   = 160,
            .hsync_front_porch  = 160,
            .vsync_pulse_width  = 10,
            .vsync_back_porch   = 23,
            .vsync_front_porch  = 12,
        },
        .flags = {
            .use_dma2d = true,
        },
    };

    // EK79007 panel via vendor driver
    ek79007_vendor_config_t vendor_cfg = {
        .mipi_config = {
            .dsi_bus    = s_dsi_bus,
            .dpi_config = &dpi_cfg,
        },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num  = -1,
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel  = LCD_BITS_PER_PIXEL,
        .vendor_config   = &vendor_cfg,
    };
    err = esp_lcd_new_panel_ek79007(s_dbi_io, &panel_cfg, &s_panel);
    if (err != ESP_OK) return err;

    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) return err;

    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

// ---- LVGL integration via esp_lvgl_port ----

static esp_err_t lvgl_init()
{
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority    = configMAX_PRIORITIES - 4,
        .task_stack       = 8192 * 2,
        .task_affinity    = -1,
        .task_max_sleep_ms = 10,
        .timer_period_ms  = 5,
    };
    esp_err_t err = lvgl_port_init(&lvgl_cfg);
    if (err != ESP_OK) return err;

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle      = s_dbi_io,
        .panel_handle   = s_panel,
        .control_handle = s_panel,
        .buffer_size    = LCD_H_RES * LCD_V_RES * (LCD_BITS_PER_PIXEL / 8),
        .double_buffer  = true,
        .hres           = LCD_H_RES,
        .vres           = LCD_V_RES,
        .monochrome     = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma    = false,
            .buff_spiram = true,
            .sw_rotate   = true,
            .full_refresh = false,
            .direct_mode  = false,
        },
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = false,
        },
    };
    s_lvgl_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (s_lvgl_disp == NULL) return ESP_FAIL;

    return ESP_OK;
}

// ---- GT911 Touch ----

static esp_err_t touch_init()
{
    /* I2C master bus for GT911 */
    i2c_master_bus_handle_t i2c_bus = NULL;
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port   = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)P4_TOUCH_SDA,
        .scl_io_num = (gpio_num_t)P4_TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* I2C panel IO for GT911 */
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = P4_TOUCH_I2C_FREQ;
    err = esp_lcd_new_panel_io_i2c(i2c_bus, &io_cfg, &tp_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch I2C IO failed: %s", esp_err_to_name(err));
        return err;
    }

    /* GT911 touch config */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max          = LCD_H_RES,
        .y_max          = LCD_V_RES,
        .rst_gpio_num   = (gpio_num_t)P4_TOUCH_RST,
        .int_gpio_num   = (gpio_num_t)P4_TOUCH_INT,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };

    /* Try primary address (0x5D), then backup (0x14) */
    err = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GT911 @ 0x5D failed, trying 0x14...");
        io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        esp_lcd_panel_io_handle_t tp_io2 = NULL;
        err = esp_lcd_new_panel_io_i2c(i2c_bus, &io_cfg, &tp_io2);
        if (err == ESP_OK) {
            err = esp_lcd_touch_new_i2c_gt911(tp_io2, &tp_cfg, &s_touch);
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GT911 touch init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Register with LVGL via esp_lvgl_port */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = s_lvgl_disp,
        .handle = s_touch,
    };
    s_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (s_touch_indev == NULL) {
        ESP_LOGE(TAG, "LVGL touch registration failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "GT911 touch initialized (SDA=%d SCL=%d INT=%d RST=%d)",
             P4_TOUCH_SDA, P4_TOUCH_SCL, P4_TOUCH_INT, P4_TOUCH_RST);
    return ESP_OK;
}

// ---- HAL interface implementation ----

namespace hal {
namespace display {

bool init()
{
    esp_err_t err;

    // Power: LDO3 = 2500mV (MIPI DPHY), LDO4 = 3300mV (panel logic)
    esp_ldo_channel_config_t ldo3_cfg = {
        .chan_id    = 3,
        .voltage_mv = 2500,
    };
    err = esp_ldo_acquire_channel(&ldo3_cfg, &s_ldo3);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LDO3 init failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_ldo_channel_config_t ldo4_cfg = {
        .chan_id    = 4,
        .voltage_mv = 3300,
    };
    err = esp_ldo_acquire_channel(&ldo4_cfg, &s_ldo4);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LDO4 init failed: %s", esp_err_to_name(err));
        return false;
    }

    // Backlight PWM (starts off)
    err = backlight_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Backlight init failed: %s", esp_err_to_name(err));
        return false;
    }

    // MIPI-DSI display
    err = display_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display port init failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "MIPI-DSI display initialized");

    // LVGL with esp_lvgl_port (managed task + DMA2D)
    err = lvgl_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL init failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "LVGL initialized (%dx%d)", LCD_H_RES, LCD_V_RES);

    // GT911 capacitive touch
    err = touch_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed — display works but no touch input");
        /* Non-fatal: display still usable for demo/debug */
    }

    return true;
}

void tick()
{
    /* P4: esp_lvgl_port runs its own FreeRTOS task — nothing to do here */
}

bool lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void unlock()
{
    lvgl_port_unlock();
}

static uint8_t s_brightness = 0;

void setBacklight(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_brightness = percent;
    uint32_t duty = (percent == 0) ? 0 : ((percent * 18) + 200);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

uint8_t getBacklight()
{
    return s_brightness;
}

uint16_t screenWidth()  { return LCD_H_RES; }
uint16_t screenHeight() { return LCD_V_RES; }

}  // namespace display
}  // namespace hal

#endif  // CROWPANEL_P4

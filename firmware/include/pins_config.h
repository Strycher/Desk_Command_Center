/**
 * CrowPanel Advance 5.0" — Pin Definitions
 * Source: Elecrow factory firmware V1.1
 */

#pragma once

/* --- Display (ST7262 RGB interface) --- */
#define LCD_H_RES  800
#define LCD_V_RES  480

/* RGB565 data bus */
#define PIN_LCD_B0   21
#define PIN_LCD_B1   47
#define PIN_LCD_B2   48
#define PIN_LCD_B3   45
#define PIN_LCD_B4   38

#define PIN_LCD_G0    9
#define PIN_LCD_G1   10
#define PIN_LCD_G2   11
#define PIN_LCD_G3   12
#define PIN_LCD_G4   13
#define PIN_LCD_G5   14

#define PIN_LCD_R0    7
#define PIN_LCD_R1   17
#define PIN_LCD_R2   18
#define PIN_LCD_R3    3
#define PIN_LCD_R4   46

/* Sync signals */
#define PIN_LCD_HSYNC  40
#define PIN_LCD_VSYNC  41
#define PIN_LCD_PCLK   39
#define PIN_LCD_DE     42   /* Data enable (HENABLE) */

/* Timing */
#define LCD_PIXEL_CLOCK_HZ  (15 * 1000 * 1000)

/* --- Touch (GT911 capacitive, I2C) --- */
#define PIN_TOUCH_SDA  15
#define PIN_TOUCH_SCL  16
#define PIN_TOUCH_INT  -1   /* not connected to ESP32 GPIO */
#define PIN_TOUCH_RST  -1   /* LovyanGFX must NOT reset GT911 (its 1ms pulse is too short) */
#define PIN_TOUCH_RST_GPIO 1  /* GPIO 1 — manual 120ms reset in setup() before lcd.begin() */
#define TOUCH_I2C_ADDR 0x5D
#define TOUCH_I2C_PORT I2C_NUM_0
#define TOUCH_I2C_FREQ 400000

/* --- Backlight (STC8H1K28 co-processor, I2C) --- */
#define BACKLIGHT_I2C_ADDR  0x30
/* Commands: write single byte 0-249 for brightness */

/* --- I2C external port --- */
#define PIN_I2C_SDA  15   /* shared with touch */
#define PIN_I2C_SCL  16   /* shared with touch */

/* --- UART --- */
/* UART0: USB-C (programming/debug) — default Serial  */
/* UART1: External connector — Serial1               */

/* --- SD Card (mutual exclusion with microphone) --- */
/* SD uses SPI bus — pins TBD from schematic */

/* --- Speaker / Audio --- */
/* Driven via STC8H1K28 co-processor at I2C 0x30 */

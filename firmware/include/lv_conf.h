/**
 * LVGL Configuration for CrowPanel Advance 5.0"
 * 800x480 IPS, 16-bit color, double-buffered in PSRAM
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* === Color === */
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0

/* === Memory === */
#define LV_MEM_CUSTOM      0
#define LV_MEM_SIZE         (64U * 1024U)  /* 64 KB for LVGL internal heap */
#define LV_MEM_ADR          0
#define LV_MEM_BUF_MAX_NUM  16

/* === Display === */
#define LV_DISP_DEF_REFR_PERIOD  33  /* ~30 FPS */
#define LV_INDEV_DEF_READ_PERIOD 33
#define LV_DPI_DEF               160

/* === Drawing === */
#define LV_DRAW_COMPLEX    1
#define LV_SHADOW_CACHE_SIZE  0
#define LV_CIRCLE_CACHE_SIZE  4

/* === GPU === */
#define LV_USE_GPU_STM32_DMA2D  0
#define LV_USE_GPU_ARM2D        0

/* === Logging === */
#ifdef DEBUG
#define LV_USE_LOG        1
#define LV_LOG_LEVEL      LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF     1
#else
#define LV_USE_LOG        0
#endif

/* === Asserts === */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* === Font === */
#define LV_FONT_MONTSERRAT_14    1
#define LV_FONT_MONTSERRAT_16    1
#define LV_FONT_MONTSERRAT_20    1
#define LV_FONT_MONTSERRAT_24    1
#define LV_FONT_MONTSERRAT_28    1
#define LV_FONT_MONTSERRAT_36    1
#define LV_FONT_MONTSERRAT_48    1
#define LV_FONT_DEFAULT          &lv_font_montserrat_20

/* === Text === */
#define LV_TXT_ENC            LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS    " ,.;:-_"

/* === Widgets === */
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     0  /* heavy on memory */
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      1

/* === Extra widgets === */
#define LV_USE_CALENDAR   1
#define LV_USE_CHART      1
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     1
#define LV_USE_KEYBOARD   1
#define LV_USE_LED        1
#define LV_USE_LIST       1
#define LV_USE_MENU       0
#define LV_USE_METER      1
#define LV_USE_MSGBOX     1
#define LV_USE_SPAN       1
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    1
#define LV_USE_TABVIEW    1
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

/* === Themes === */
#define LV_USE_THEME_DEFAULT  1
#define LV_THEME_DEFAULT_DARK 1  /* dark theme for desk display */
#define LV_USE_THEME_BASIC    1

/* === Layouts === */
#define LV_USE_FLEX  1
#define LV_USE_GRID  1

/* === Animations === */
#define LV_USE_ANIMATION       1
#define LV_USE_ANIMATION_TIMELINE 1

/* === File system === */
#define LV_USE_FS_STDIO    0
#define LV_USE_FS_POSIX    0
#define LV_USE_FS_FATFS    0

/* === Snapshot === */
#define LV_USE_SNAPSHOT    0

#endif /* LV_CONF_H */

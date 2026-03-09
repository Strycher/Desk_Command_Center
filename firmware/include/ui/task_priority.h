/**
 * Task Priority Helpers — per-source sort rank and color mapping.
 * Each source platform defines its own priority vocabulary.
 * Add new sources by adding a new rank/color function pair.
 */

#pragma once
#include <lvgl.h>
#include <string.h>

/* ── Colors ── */
static const lv_color_t PRIO_CRITICAL = lv_color_hex(0xFF1744);  // bright red
static const lv_color_t PRIO_HIGH     = lv_color_hex(0xFF4444);  // red
static const lv_color_t PRIO_MED      = lv_color_hex(0xFFAA00);  // orange
static const lv_color_t PRIO_LOW      = lv_color_hex(0x44AA44);  // green
static const lv_color_t PRIO_NONE     = lv_color_hex(0x555577);  // gray

/* ── Unfocused ── */

/** Sort rank for Unfocused priorities. Lower = higher priority. */
static inline uint8_t unfocusedPrioRank(const char* p) {
    if (!p || p[0] == '\0')            return 99;
    if (strcmp(p, "Critical") == 0)    return 0;
    if (strcmp(p, "High") == 0)        return 1;
    if (strcmp(p, "Medium") == 0)      return 2;
    if (strcmp(p, "Low") == 0)         return 3;
    return 99;
}

/** Color for Unfocused priorities. */
static inline lv_color_t unfocusedPrioColor(const char* p) {
    if (!p || p[0] == '\0')            return PRIO_NONE;
    if (strcmp(p, "Critical") == 0)    return PRIO_CRITICAL;
    if (strcmp(p, "High") == 0)        return PRIO_HIGH;
    if (strcmp(p, "Medium") == 0)      return PRIO_MED;
    if (strcmp(p, "Low") == 0)         return PRIO_LOW;
    return PRIO_NONE;
}

/* ── Monday.com (stub — implement when adapter lands) ── */

static inline uint8_t mondayPrioRank(const char* p) {
    (void)p;
    return 99;
}

static inline lv_color_t mondayPrioColor(const char* p) {
    (void)p;
    return PRIO_NONE;
}

/* ── Generic dispatcher by source ── */

static inline uint8_t taskPrioRank(const char* priority, const char* source) {
    if (strcmp(source, "unfocused") == 0 || strcmp(source, "UF") == 0)
        return unfocusedPrioRank(priority);
    if (strcmp(source, "monday") == 0 || strcmp(source, "MO") == 0)
        return mondayPrioRank(priority);
    return 99;
}

static inline lv_color_t taskPrioColor(const char* priority, const char* source) {
    if (strcmp(source, "unfocused") == 0 || strcmp(source, "UF") == 0)
        return unfocusedPrioColor(priority);
    if (strcmp(source, "monday") == 0 || strcmp(source, "MO") == 0)
        return mondayPrioColor(priority);
    return PRIO_NONE;
}

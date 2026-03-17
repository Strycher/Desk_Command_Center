/**
 * Home Assistant Screen — HA-style tile widget cards.
 * Grid layout with domain-specific card renderers.
 * Tiles arranged in row-wrap flex (4 standard / 2 wide per row).
 *
 * Two rendering modes:
 *   Label mode:  device-grouped cards — multi-entity devices get
 *                consolidated wide tiles; single-entity devices use
 *                domain-specific renderers.
 *   Domain mode: legacy domain-sorted groups (climate, lights, etc.)
 */

#include "ui/screens/ha_screen.h"
#include "ui/ha_control_modal.h"
#include "ui/ui_scale.h"
#include <cstring>
#include "logger.h"

/* ── Colors ─────────────────────────────────────────── */
static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t TILE_BG        = lv_color_hex(0x1c1c36);
static const lv_color_t TILE_BG_ON     = lv_color_hex(0x2a2a50);
static const lv_color_t TOGGLE_ACTIVE  = lv_color_hex(0x6C63FF);
static const lv_color_t TOGGLE_INACTIVE = lv_color_hex(0x2a2a40);
static const lv_color_t TEXT_PRIMARY   = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0x9898B8);
static const lv_color_t TEXT_DIM       = lv_color_hex(0x606078);
static const lv_color_t ACCENT         = lv_color_hex(0x6C63FF);
static const lv_color_t STATE_ON       = lv_color_hex(0x44BB44);
static const lv_color_t STATE_OFF      = lv_color_hex(0x555566);
static const lv_color_t CLIMATE_HEAT   = lv_color_hex(0xFF6633);
static const lv_color_t CLIMATE_COOL   = lv_color_hex(0x3399FF);
static const lv_color_t CLIMATE_IDLE   = lv_color_hex(0x888899);
static const lv_color_t LIGHT_ON       = lv_color_hex(0xFFB84D);
static const lv_color_t MEDIA_ACCENT   = lv_color_hex(0x00BCD4);
static const lv_color_t SENSOR_ACCENT  = lv_color_hex(0x5C6BC0);
static const lv_color_t PERSON_ACCENT  = lv_color_hex(0x66BB6A);
static const lv_color_t DEVICE_ACCENT  = lv_color_hex(0x7E57C2);

/* ── Layout constants (scaled via ui_scale.h) ──────── */
/* Use UI_CONTENT_Y, UI_PAD, UI_CONTENT_W from ui_scale.h */
static const int16_t CARD_STD    = SX(250);   /* 3 per row */
static const int16_t CARD_WIDE   = SX(380);   /* 2 per row */
static const int16_t CARD_H      = SY(80);    /* standard card height */
static const int16_t CARD_H_TALL = SY(100);   /* climate / info-dense cards */
static const int16_t CARD_H_MULTI = SY(90);   /* multi-entity device cards */
static const int16_t TILE_GAP    = SU(6);
static const int16_t TILE_R      = SU(10);
/* Legacy aliases for domain-mode renderers */
static const int16_t TILE_W      = CARD_STD;
static const int16_t TILE_H      = CARD_H;
static const int16_t TILE_WIDE   = CARD_WIDE;
static const int16_t TILE_FULL   = UI_CONTENT_W - 2 * UI_PAD;
static const int16_t TILE_TALL   = CARD_H_TALL;

/* ── Domain helpers ─────────────────────────────────── */
static const char* domainLabel(const char* d) {
    if (strcmp(d, "climate") == 0)       return "Climate";
    if (strcmp(d, "light") == 0)         return "Lights";
    if (strcmp(d, "switch") == 0)        return "Switches";
    if (strcmp(d, "media_player") == 0)  return "Media";
    if (strcmp(d, "sensor") == 0)        return "Sensors";
    if (strcmp(d, "binary_sensor") == 0) return "Sensors";
    if (strcmp(d, "person") == 0)        return "People";
    if (strcmp(d, "device_tracker") == 0) return "Trackers";
    if (strcmp(d, "cover") == 0)         return "Covers";
    if (strcmp(d, "fan") == 0)           return "Fans";
    if (strcmp(d, "lock") == 0)          return "Security";
    return d;
}

static const char* domainIcon(const char* d) {
    if (strcmp(d, "climate") == 0)       return LV_SYMBOL_CHARGE;
    if (strcmp(d, "light") == 0)         return LV_SYMBOL_EYE_OPEN;
    if (strcmp(d, "switch") == 0)        return LV_SYMBOL_POWER;
    if (strcmp(d, "sensor") == 0)        return LV_SYMBOL_GPS;
    if (strcmp(d, "binary_sensor") == 0) return LV_SYMBOL_GPS;
    if (strcmp(d, "media_player") == 0)  return LV_SYMBOL_AUDIO;
    if (strcmp(d, "person") == 0)        return LV_SYMBOL_HOME;
    if (strcmp(d, "device_tracker") == 0) return LV_SYMBOL_HOME;
    if (strcmp(d, "cover") == 0)         return LV_SYMBOL_UP;
    if (strcmp(d, "fan") == 0)           return LV_SYMBOL_REFRESH;
    if (strcmp(d, "lock") == 0)          return LV_SYMBOL_CLOSE;
    return LV_SYMBOL_HOME;
}

static lv_color_t domainAccent(const char* d) {
    if (strcmp(d, "climate") == 0)       return CLIMATE_HEAT;
    if (strcmp(d, "light") == 0)         return LIGHT_ON;
    if (strcmp(d, "switch") == 0)        return STATE_ON;
    if (strcmp(d, "media_player") == 0)  return MEDIA_ACCENT;
    if (strcmp(d, "sensor") == 0)        return SENSOR_ACCENT;
    if (strcmp(d, "binary_sensor") == 0) return SENSOR_ACCENT;
    if (strcmp(d, "person") == 0)        return PERSON_ACCENT;
    if (strcmp(d, "device_tracker") == 0) return PERSON_ACCENT;
    return ACCENT;
}

/* Sort priority: lower = rendered first.
 * "network" is a virtual section for router/printer devices
 * whose primary domain is binary_sensor/sensor. */
static uint8_t domainOrder(const char* d) {
    if (strcmp(d, "climate") == 0)       return 0;
    if (strcmp(d, "light") == 0)         return 1;
    if (strcmp(d, "switch") == 0)        return 2;
    if (strcmp(d, "media_player") == 0)  return 3;
    if (strcmp(d, "sensor") == 0)        return 4;
    if (strcmp(d, "binary_sensor") == 0) return 5;
    if (strcmp(d, "person") == 0)        return 6;
    if (strcmp(d, "device_tracker") == 0) return 7;
    return 8;
}

/* Map a device's primary domain to a visual section key.
 * This collapses binary_sensor into sensor, and device_tracker
 * with GPS source_type into "person" (handled at a higher level). */
static const char* sectionKey(const char* domain) {
    if (strcmp(domain, "binary_sensor") == 0) return "sensor";
    return domain;
}

/* Should this device use a wide card? */
static bool needsWideCard(const char* sectionDomain, uint8_t entityCount) {
    if (strcmp(sectionDomain, "climate") == 0) return true;
    if (entityCount >= 3) return true;
    return false;
}

/* ── Controllability check ──────────────────────────── */
static bool isControllable(const char* domain) {
    return strcmp(domain, "sensor") != 0 &&
           strcmp(domain, "binary_sensor") != 0 &&
           strcmp(domain, "device_tracker") != 0 &&
           strcmp(domain, "person") != 0;
}

/* ── Tile base helper ───────────────────────────────── */
static lv_obj_t* makeTile(lv_obj_t* parent, int16_t w, int16_t h) {
    lv_obj_t* t = lv_obj_create(parent);
    lv_obj_set_size(t, w, h);
    lv_obj_set_style_bg_color(t, TILE_BG, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, TILE_R, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_pad_all(t, SU(8), 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    return t;
}

static const char* entityName(const HAEntity& e) {
    return e.friendly_name[0] ? e.friendly_name : e.entity_id;
}

/* Strip device name prefix from entity friendly_name to avoid redundancy.
 * "RT-AX82U Primary Router Download speed" → "Download speed"
 * Returns pointer into a static buffer. */
static const char* shortEntityName(const HAEntity& e, const char* deviceName) {
    static char buf[64];
    const char* fname = entityName(e);
    size_t devLen = strlen(deviceName);
    if (devLen > 0 && strncmp(fname, deviceName, devLen) == 0) {
        const char* rest = fname + devLen;
        while (*rest == ' ') rest++;  /* trim leading space */
        if (*rest) {
            strncpy(buf, rest, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            return buf;
        }
    }
    strncpy(buf, fname, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

/* Format a sensor state value: if it's a float with many decimals,
 * truncate to 2 decimal places. "927.123456" → "927.12"
 * Non-numeric states pass through unchanged. */
static void formatSensorValue(char* out, size_t outLen, const char* state) {
    /* Check if state looks like a float with a decimal point */
    const char* dot = strchr(state, '.');
    if (dot) {
        /* Count digits after dot */
        int fracLen = strlen(dot + 1);
        if (fracLen > 2) {
            /* Parse and reformat to 2 decimal places */
            double val = strtod(state, nullptr);
            snprintf(out, outLen, "%.2f", val);
            return;
        }
    }
    strncpy(out, state, outLen - 1);
    out[outLen - 1] = '\0';
}

static void capitalizeFirst(char* buf) {
    if (buf[0] >= 'a' && buf[0] <= 'z') buf[0] -= 32;
}

/* ── Click handler — extern access to haScreen in main.cpp ── */
extern HAScreen haScreen;

void HAScreen::onCardClick(lv_event_t* e) {
    uintptr_t entityIdx = (uintptr_t)lv_event_get_user_data(e);

    if (!haScreen._lastData) return;
    const HAData& ha = haScreen._lastData->home_assistant.data;
    if (entityIdx >= ha.entity_count) return;

    const HAEntity& entity = ha.entities[entityIdx];

    /* Find device name for this entity */
    const char* devName = entity.friendly_name;
    for (uint8_t d = 0; d < ha.device_count; d++) {
        if (ha.devices[d].entity_start <= entityIdx &&
            entityIdx < ha.devices[d].entity_start + ha.devices[d].entity_count) {
            devName = ha.devices[d].device_name;
            break;
        }
    }

    HAControlModal::show(entity, devName);
}

void HAScreen::onToggleClick(lv_event_t* e) {
    bool wantRoom = (bool)(uintptr_t)lv_event_get_user_data(e);
    if (haScreen._groupByRoom == wantRoom) return;  // already in this mode

    haScreen._groupByRoom = wantRoom;
    haScreen.updateToggleStyle();

    if (haScreen._lastData &&
        haScreen._lastData->home_assistant.status == SourceStatus::OK) {
        haScreen.rebuildEntityList(haScreen._lastData->home_assistant.data);
    }
}

void HAScreen::updateToggleStyle() {
    if (!_btnRoom || !_btnCategory) return;

    lv_obj_set_style_bg_color(_btnRoom,
        _groupByRoom ? TOGGLE_ACTIVE : TOGGLE_INACTIVE, 0);
    lv_obj_set_style_bg_color(_btnCategory,
        _groupByRoom ? TOGGLE_INACTIVE : TOGGLE_ACTIVE, 0);
}

/* ── Screen create ──────────────────────────────────── */
static lv_obj_t* makeToggleBtn(lv_obj_t* parent, const char* text,
                                lv_coord_t x, bool active) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, SX(90), SY(24));
    lv_obj_set_pos(btn, x, SY(3));
    lv_obj_set_style_bg_color(btn, active ? TOGGLE_ACTIVE : TOGGLE_INACTIVE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, SU(6), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, TEXT_PRIMARY, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);

    return btn;
}

/* Toggle row sits below the status bar */
static const int16_t TOGGLE_Y      = UI_CONTENT_Y + SY(2);   /* just below status bar */
static const int16_t ENTITY_LIST_Y = UI_CONTENT_Y + SY(30);  /* below toggle row */
static const int16_t ENTITY_LIST_H = UI_CONTENT_H - SY(30);  /* remaining content height */

void HAScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* Toggle buttons: [Room] [Type] at top-right, below status bar */
    _btnRoom = makeToggleBtn(_screen, LV_SYMBOL_HOME " Room",
                              SX(586), _groupByRoom);
    lv_obj_set_pos(_btnRoom, SX(586), TOGGLE_Y);
    lv_obj_add_event_cb(_btnRoom, onToggleClick, LV_EVENT_CLICKED,
                        (void*)(uintptr_t)1);

    _btnCategory = makeToggleBtn(_screen, LV_SYMBOL_LIST " Type",
                                  SX(682), !_groupByRoom);
    lv_obj_set_pos(_btnCategory, SX(682), TOGGLE_Y);
    lv_obj_add_event_cb(_btnCategory, onToggleClick, LV_EVENT_CLICKED,
                        (void*)(uintptr_t)0);

    /* Scrollable tile container — below toggle row */
    _entityList = lv_obj_create(_screen);
    lv_obj_set_size(_entityList, UI_CONTENT_W, ENTITY_LIST_H);
    lv_obj_set_pos(_entityList, UI_PAD, ENTITY_LIST_Y);
    lv_obj_set_style_bg_opa(_entityList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_entityList, 0, 0);
    lv_obj_set_style_pad_all(_entityList, 0, 0);
    lv_obj_set_style_pad_row(_entityList, SU(6), 0);
    lv_obj_set_flex_flow(_entityList, LV_FLEX_FLOW_COLUMN);

    LOG_INFO("HA: screen created");
}

/* ── Climate Card (wide tile with thermostat) ───────── */
void HAScreen::addClimateCard(lv_obj_t* parent, const HAEntity& e) {
    bool heating = (strcmp(e.extra.climate.hvac_action, "heating") == 0);
    bool cooling = (strcmp(e.extra.climate.hvac_action, "cooling") == 0);
    lv_color_t accent = heating ? CLIMATE_HEAT
                       : cooling ? CLIMATE_COOL
                       : CLIMATE_IDLE;

    lv_obj_t* tile = makeTile(parent, TILE_WIDE, TILE_TALL);

    /* Icon */
    lv_obj_t* icon = lv_label_create(tile);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, accent, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(icon, LV_SYMBOL_CHARGE);

    /* Name */
    lv_obj_t* name = lv_label_create(tile);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, TEXT_SECONDARY, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, SX(28), SY(2));
    lv_obj_set_width(name, SX(200));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, entityName(e));

    /* Current temp — large, right side */
    lv_obj_t* cur = lv_label_create(tile);
    lv_obj_set_style_text_font(cur, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(cur, TEXT_PRIMARY, 0);
    lv_obj_align(cur, LV_ALIGN_TOP_RIGHT, 0, SY(-4));
    char buf[16];
    if (e.extra.climate.current_temp > 0)
        snprintf(buf, sizeof(buf), "%.0fF", e.extra.climate.current_temp);
    else
        snprintf(buf, sizeof(buf), "%s", e.state);
    lv_label_set_text(cur, buf);

    /* HVAC action — colored dot + text, bottom left */
    lv_obj_t* dot = lv_obj_create(tile);
    lv_obj_set_size(dot, SU(10), SU(10));
    lv_obj_set_style_bg_color(dot, accent, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_LEFT, 0, SY(-2));

    lv_obj_t* action = lv_label_create(tile);
    lv_obj_set_style_text_font(action, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(action, accent, 0);
    lv_obj_align(action, LV_ALIGN_BOTTOM_LEFT, SX(16), 0);
    const char* hvac = e.extra.climate.hvac_action;
    if (hvac[0]) {
        char abuf[16];
        strncpy(abuf, hvac, sizeof(abuf) - 1);
        abuf[sizeof(abuf) - 1] = '\0';
        capitalizeFirst(abuf);
        lv_label_set_text(action, abuf);
    } else {
        lv_label_set_text(action, e.state);
    }

    /* Target temp + preset — bottom right */
    if (e.extra.climate.target_temp > 0) {
        lv_obj_t* target = lv_label_create(tile);
        lv_obj_set_style_text_font(target, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(target, TEXT_DIM, 0);
        lv_obj_align(target, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        char tbuf[32];
        if (e.extra.climate.preset_mode[0])
            snprintf(tbuf, sizeof(tbuf), "> %.0fF  %s",
                     e.extra.climate.target_temp, e.extra.climate.preset_mode);
        else
            snprintf(tbuf, sizeof(tbuf), "> %.0fF",
                     e.extra.climate.target_temp);
        lv_label_set_text(target, tbuf);
    }
}

/* ── Sensor Tile ───────────────────────────────────── */
void HAScreen::addSensorRow(lv_obj_t* parent, const HAEntity& e, int16_t w) {
    if (w <= 0) w = CARD_STD;
    lv_obj_t* tile = makeTile(parent, w, CARD_H);

    /* Icon */
    lv_obj_t* icon = lv_label_create(tile);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(icon, SENSOR_ACCENT, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(icon, LV_SYMBOL_GPS);

    /* Name — truncated */
    lv_obj_t* name = lv_label_create(tile);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name, TEXT_SECONDARY, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, SX(22), SY(2));
    lv_obj_set_width(name, w - SX(40));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, entityName(e));

    /* Value + unit — prominent, with decimal truncation */
    lv_obj_t* val = lv_label_create(tile);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(val, TEXT_PRIMARY, 0);
    lv_obj_align(val, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    char buf[48];
    char fmtVal[32];
    formatSensorValue(fmtVal, sizeof(fmtVal), e.state);
    if (strcmp(e.domain, "sensor") == 0 && e.extra.sensor.unit[0])
        snprintf(buf, sizeof(buf), "%s %s", fmtVal, e.extra.sensor.unit);
    else
        snprintf(buf, sizeof(buf), "%s", fmtVal);
    lv_label_set_text(val, buf);
}

/* ── Light / Switch / Fan Card ──────────────────────── */
void HAScreen::addLightSwitchRow(lv_obj_t* parent, const HAEntity& e,
                                  int16_t w, const char* displayName) {
    if (w <= 0) w = CARD_STD;
    bool isOn = (strcmp(e.state, "on") == 0);
    bool isUnavail = (strcmp(e.state, "unavailable") == 0);
    lv_color_t stateClr = isOn ? domainAccent(e.domain)
                         : isUnavail ? lv_color_hex(0x996600) : STATE_OFF;

    lv_obj_t* tile = makeTile(parent, w, CARD_H);
    if (isOn) lv_obj_set_style_bg_color(tile, TILE_BG_ON, 0);

    /* Icon — colored by state */
    lv_obj_t* icon = lv_label_create(tile);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, stateClr, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(icon, domainIcon(e.domain));

    /* Name */
    lv_obj_t* name = lv_label_create(tile);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, TEXT_SECONDARY, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, SX(26), SY(2));
    lv_obj_set_width(name, w - SX(44));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, displayName ? displayName : entityName(e));

    /* State label */
    lv_obj_t* st = lv_label_create(tile);
    lv_obj_set_style_text_font(st, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(st, isOn ? stateClr : TEXT_DIM, 0);
    lv_obj_align(st, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    if (isUnavail)
        lv_label_set_text(st, LV_SYMBOL_WARNING " Unavail.");
    else
        lv_label_set_text(st, isOn ? LV_SYMBOL_OK " On" : LV_SYMBOL_CLOSE " Off");
}

/* ── Media Card ────────────────────────────────────── */
void HAScreen::addMediaCard(lv_obj_t* parent, const HAEntity& e, int16_t w) {
    if (w <= 0) w = CARD_STD;
    bool playing = (strcmp(e.state, "playing") == 0);
    lv_obj_t* tile = makeTile(parent, w, CARD_H);

    /* Icon */
    lv_obj_t* icon = lv_label_create(tile);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, playing ? MEDIA_ACCENT : TEXT_DIM, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(icon, LV_SYMBOL_AUDIO);

    /* Name */
    lv_obj_t* name = lv_label_create(tile);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, TEXT_PRIMARY, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, SX(28), SY(2));
    lv_obj_set_width(name, w - SX(120));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, entityName(e));

    /* State — top right, capitalized */
    lv_obj_t* stLbl = lv_label_create(tile);
    lv_obj_set_style_text_font(stLbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(stLbl, playing ? MEDIA_ACCENT : TEXT_DIM, 0);
    lv_obj_align(stLbl, LV_ALIGN_TOP_RIGHT, 0, SY(2));
    char stBuf[32];
    strncpy(stBuf, e.state, sizeof(stBuf) - 1);
    stBuf[sizeof(stBuf) - 1] = '\0';
    capitalizeFirst(stBuf);
    lv_label_set_text(stLbl, stBuf);

    /* Now playing — bottom row */
    if (e.extra.media.media_title[0] || e.extra.media.app_name[0]) {
        lv_obj_t* info = lv_label_create(tile);
        lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(info, MEDIA_ACCENT, 0);
        lv_obj_align(info, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_width(info, w - SX(24));
        lv_label_set_long_mode(info, LV_LABEL_LONG_DOT);
        char buf[80];
        if (e.extra.media.app_name[0] && e.extra.media.media_title[0])
            snprintf(buf, sizeof(buf), "%s - %s",
                     e.extra.media.app_name, e.extra.media.media_title);
        else if (e.extra.media.media_title[0])
            snprintf(buf, sizeof(buf), "%s", e.extra.media.media_title);
        else
            snprintf(buf, sizeof(buf), "%s", e.extra.media.app_name);
        lv_label_set_text(info, buf);
    }
}

/* ── Generic Tile (person, lock, cover, device_tracker, etc.) ── */
void HAScreen::addGenericRow(lv_obj_t* parent, const HAEntity& e, int16_t w) {
    if (w <= 0) w = CARD_STD;
    bool isPositive = (strcmp(e.state, "on") == 0 ||
                       strcmp(e.state, "home") == 0 ||
                       strcmp(e.state, "locked") == 0);
    lv_color_t stateClr = isPositive ? domainAccent(e.domain) : TEXT_DIM;

    lv_obj_t* tile = makeTile(parent, w, CARD_H);

    /* Icon */
    lv_obj_t* icon = lv_label_create(tile);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, stateClr, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(icon, domainIcon(e.domain));

    /* Name */
    lv_obj_t* name = lv_label_create(tile);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, TEXT_SECONDARY, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, SX(26), SY(2));
    lv_obj_set_width(name, w - SX(44));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, entityName(e));

    /* State — capitalized */
    lv_obj_t* stLbl = lv_label_create(tile);
    lv_obj_set_style_text_font(stLbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(stLbl, stateClr, 0);
    lv_obj_align(stLbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    char buf[32];
    strncpy(buf, e.state, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    capitalizeFirst(buf);
    lv_label_set_text(stLbl, buf);
}

/* ── Person Card (standard: name + location + battery) ── */
void HAScreen::addPersonCard(lv_obj_t* parent, const HADeviceGroup& grp,
                              const HAEntity* entities) {
    lv_obj_t* tile = makeTile(parent, CARD_STD, CARD_H);

    /* Extract person name: strip phone model suffix if present */
    char pname[32];
    const char* src = grp.device_name;
    /* Try to use just the first word (person's name) */
    const char* space = strchr(src, ' ');
    if (space && (strstr(src, "Samsung") || strstr(src, "Galaxy") ||
                  strstr(src, "iPhone") || strstr(src, "Pixel"))) {
        size_t len = space - src;
        if (len >= sizeof(pname)) len = sizeof(pname) - 1;
        memcpy(pname, src, len);
        pname[len] = '\0';
    } else {
        strncpy(pname, src, sizeof(pname) - 1);
        pname[sizeof(pname) - 1] = '\0';
    }
    /* Strip trailing 's if present (e.g. "Mari's") */
    size_t plen = strlen(pname);
    if (plen >= 2 && pname[plen - 2] == '\'' && pname[plen - 1] == 's') {
        pname[plen - 2] = '\0';
    }

    /* Icon */
    lv_obj_t* icon = lv_label_create(tile);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, PERSON_ACCENT, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(icon, LV_SYMBOL_HOME);

    /* Person name */
    lv_obj_t* name = lv_label_create(tile);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, TEXT_PRIMARY, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, SX(26), SY(2));
    lv_label_set_text(name, pname);

    /* Find tracker and battery sensor */
    const char* location = nullptr;
    int battery = -1;
    for (uint8_t i = 0; i < grp.entity_count; i++) {
        const HAEntity& ent = entities[grp.entity_start + i];
        if (strcmp(ent.domain, "device_tracker") == 0) {
            location = ent.state;
        } else if (strcmp(ent.domain, "sensor") == 0 &&
                   strcmp(ent.extra.sensor.device_class, "battery") == 0) {
            battery = atoi(ent.state);
        }
    }

    /* Location — with home icon */
    if (location) {
        bool home = (strcmp(location, "home") == 0);
        lv_obj_t* loc = lv_label_create(tile);
        lv_obj_set_style_text_font(loc, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(loc, home ? PERSON_ACCENT : TEXT_DIM, 0);
        lv_obj_align(loc, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        char lbuf[32];
        char locCap[16];
        strncpy(locCap, location, sizeof(locCap) - 1);
        locCap[sizeof(locCap) - 1] = '\0';
        capitalizeFirst(locCap);
        snprintf(lbuf, sizeof(lbuf), "%s %s",
                 home ? LV_SYMBOL_HOME : LV_SYMBOL_GPS, locCap);
        lv_label_set_text(loc, lbuf);
    }

    /* Battery — bottom right */
    if (battery >= 0) {
        lv_obj_t* bat = lv_label_create(tile);
        lv_obj_set_style_text_font(bat, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(bat, battery > 20 ? TEXT_SECONDARY
                                         : lv_color_hex(0xFF4444), 0);
        lv_obj_align(bat, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        char bbuf[16];
        snprintf(bbuf, sizeof(bbuf), "%s %d%%", LV_SYMBOL_CHARGE, battery);
        lv_label_set_text(bat, bbuf);
    }
}

/* ── Multi-Entity Device Card (wide: device name + entity grid) ── */
void HAScreen::addMultiEntityCard(lv_obj_t* parent, const HADeviceGroup& grp,
                                   const HAEntity* entities, int16_t w) {
    int16_t h = CARD_H_MULTI;
    if (grp.entity_count > 4) h = SY(110);

    lv_obj_t* tile = makeTile(parent, w, h);

    /* Domain icon */
    lv_obj_t* icon = lv_label_create(tile);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_color_t accent = domainAccent(grp.domain);
    lv_obj_set_style_text_color(icon, accent, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(icon, domainIcon(grp.domain));

    /* Device name as title */
    lv_obj_t* name = lv_label_create(tile);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, TEXT_PRIMARY, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, SX(26), SY(2));
    lv_obj_set_width(name, w - SX(44));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, grp.device_name);

    /* Entity states — compact flex row */
    lv_obj_t* row = lv_obj_create(tile);
    lv_obj_set_size(row, w - SX(24), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, SX(12), 0);
    lv_obj_set_style_pad_row(row, SY(2), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, SX(4), SY(24));

    for (uint8_t i = 0; i < grp.entity_count; i++) {
        const HAEntity& ent = entities[grp.entity_start + i];

        /* device_tracker entities are kept — useful for routers,
           network devices, and other non-person trackers */

        const char* sname = shortEntityName(ent, grp.device_name);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

        bool isOn = (strcmp(ent.state, "on") == 0 ||
                     strcmp(ent.state, "home") == 0);
        bool isOff = (strcmp(ent.state, "off") == 0);
        bool isUnavail = (strcmp(ent.state, "unavailable") == 0);
        lv_color_t clr = isOn ? STATE_ON
                        : isUnavail ? lv_color_hex(0x996600)
                        : isOff ? TEXT_DIM
                        : TEXT_PRIMARY;
        lv_obj_set_style_text_color(lbl, clr, 0);

        char buf[64];
        const char* symb = isOn ? LV_SYMBOL_OK : isUnavail ? LV_SYMBOL_WARNING
                          : LV_SYMBOL_CLOSE;
        if (strcmp(ent.domain, "sensor") == 0 && ent.extra.sensor.unit[0]) {
            char fv[32];
            formatSensorValue(fv, sizeof(fv), ent.state);
            snprintf(buf, sizeof(buf), "%s: %s %s", sname, fv,
                     ent.extra.sensor.unit);
        } else if (strcmp(ent.domain, "binary_sensor") == 0 ||
                   strcmp(ent.domain, "switch") == 0 ||
                   strcmp(ent.domain, "light") == 0) {
            snprintf(buf, sizeof(buf), "%s: %s", sname, symb);
        } else {
            snprintf(buf, sizeof(buf), "%s: %s", sname, ent.state);
        }
        lv_label_set_text(lbl, buf);
    }
}

/* ═══════════════════════════════════════════════════════
 *  Room Mode: Area-Sectioned Card Grid
 * ═══════════════════════════════════════════════════════ */

void HAScreen::addRoomHeader(const char* roomName, uint8_t count) {
    lv_obj_t* hdr = lv_obj_create(_entityList);
    lv_obj_set_size(hdr, UI_CONTENT_W, SY(24));
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblIcon = lv_label_create(hdr);
    lv_obj_set_style_text_font(lblIcon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblIcon, ACCENT, 0);
    lv_obj_align(lblIcon, LV_ALIGN_LEFT_MID, SX(4), SY(-2));
    lv_label_set_text(lblIcon, LV_SYMBOL_HOME);

    lv_obj_t* lblName = lv_label_create(hdr);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblName, TEXT_SECONDARY, 0);
    lv_obj_align(lblName, LV_ALIGN_LEFT_MID, SX(26), SY(-2));
    char buf[48];
    snprintf(buf, sizeof(buf), "%s (%d)", roomName, count);
    lv_label_set_text(lblName, buf);
}

void HAScreen::rebuildByRoom(const HAData& ha) {
    LOG_INFO("HA: rebuild room mode — %d devices", ha.device_count);

    /* ── Discover unique room names ── */
    static constexpr uint8_t MAX_ROOMS = 16;
    char rooms[MAX_ROOMS][32];
    uint8_t roomCount = 0;

    for (uint8_t d = 0; d < ha.device_count; d++) {
        const char* area = ha.devices[d].area_name;
        if (area[0] == '\0') continue;  // skip unassigned — goes to "Other"

        bool found = false;
        for (uint8_t r = 0; r < roomCount; r++) {
            if (strcmp(rooms[r], area) == 0) { found = true; break; }
        }
        if (!found && roomCount < MAX_ROOMS) {
            strncpy(rooms[roomCount], area, 31);
            rooms[roomCount][31] = '\0';
            roomCount++;
        }
    }

    /* Sort rooms alphabetically */
    for (uint8_t i = 0; i < roomCount; i++) {
        for (uint8_t j = i + 1; j < roomCount; j++) {
            if (strcasecmp(rooms[j], rooms[i]) < 0) {
                char tmp[32];
                memcpy(tmp, rooms[i], 32);
                memcpy(rooms[i], rooms[j], 32);
                memcpy(rooms[j], tmp, 32);
            }
        }
    }

    /* ── Render each room section ── */
    for (uint8_t r = 0; r < roomCount; r++) {
        /* Collect device indices for this room */
        uint8_t devIdx[MAX_HA_DEVICES];
        uint8_t devCount = 0;
        for (uint8_t d = 0; d < ha.device_count; d++) {
            if (strcmp(ha.devices[d].area_name, rooms[r]) == 0) {
                devIdx[devCount++] = d;
            }
        }
        if (devCount == 0) continue;

        /* Room header */
        addRoomHeader(rooms[r], devCount);

        /* Card grid */
        lv_obj_t* grid = makeSectionGrid();

        for (uint8_t i = 0; i < devCount; i++) {
            const HADeviceGroup& grp = ha.devices[devIdx[i]];
            const char* sec = sectionKey(grp.domain);
            bool wide = needsWideCard(sec, grp.entity_count);
            addDeviceCardToGrid(grid, grp, ha.entities, wide);
        }
    }

    /* ── "Other" section: devices with no area + standalone entities ── */
    uint8_t noAreaIdx[MAX_HA_DEVICES];
    uint8_t noAreaCount = 0;
    for (uint8_t d = 0; d < ha.device_count; d++) {
        if (ha.devices[d].area_name[0] == '\0') {
            noAreaIdx[noAreaCount++] = d;
        }
    }
    uint8_t standaloneCount = ha.entity_count - ha.standalone_start;

    if (noAreaCount > 0 || standaloneCount > 0) {
        addRoomHeader("Other", noAreaCount + standaloneCount);
        lv_obj_t* grid = makeSectionGrid();

        for (uint8_t i = 0; i < noAreaCount; i++) {
            const HADeviceGroup& grp = ha.devices[noAreaIdx[i]];
            const char* sec = sectionKey(grp.domain);
            bool wide = needsWideCard(sec, grp.entity_count);
            addDeviceCardToGrid(grid, grp, ha.entities, wide);
        }

        /* Standalone entities */
        for (uint8_t i = ha.standalone_start; i < ha.entity_count; i++) {
            const HAEntity& ent = ha.entities[i];
            if (strcmp(ent.domain, "climate") == 0)
                addClimateCard(grid, ent);
            else if (strcmp(ent.domain, "sensor") == 0 ||
                     strcmp(ent.domain, "binary_sensor") == 0)
                addSensorRow(grid, ent, CARD_STD);
            else if (strcmp(ent.domain, "light") == 0 ||
                     strcmp(ent.domain, "switch") == 0)
                addLightSwitchRow(grid, ent, CARD_STD);
            else if (strcmp(ent.domain, "media_player") == 0)
                addMediaCard(grid, ent, CARD_STD);
            else
                addGenericRow(grid, ent, CARD_STD);
        }
    }
}

/* ═══════════════════════════════════════════════════════
 *  Category Mode: Domain-Sectioned Card Grid
 * ═══════════════════════════════════════════════════════ */

/* ── Section Header: domain icon + label + count + divider ── */
void HAScreen::addSectionHeader(const char* domain, uint8_t count) {
    lv_color_t accent = domainAccent(domain);

    lv_obj_t* hdr = lv_obj_create(_entityList);
    lv_obj_set_size(hdr, UI_CONTENT_W, SY(24));
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblIcon = lv_label_create(hdr);
    lv_obj_set_style_text_font(lblIcon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblIcon, accent, 0);
    lv_obj_align(lblIcon, LV_ALIGN_LEFT_MID, SX(4), SY(-2));
    lv_label_set_text(lblIcon, domainIcon(domain));

    lv_obj_t* lblName = lv_label_create(hdr);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblName, TEXT_SECONDARY, 0);
    lv_obj_align(lblName, LV_ALIGN_LEFT_MID, SX(26), SY(-2));
    char buf[48];
    snprintf(buf, sizeof(buf), "%s (%d)", domainLabel(domain), count);
    lv_label_set_text(lblName, buf);
}

/* ── Section Grid: row-wrap flex container for cards ── */
lv_obj_t* HAScreen::makeSectionGrid() {
    lv_obj_t* grid = lv_obj_create(_entityList);
    lv_obj_set_size(grid, UI_CONTENT_W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_column(grid, TILE_GAP, 0);
    lv_obj_set_style_pad_row(grid, TILE_GAP, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    return grid;
}

/* Render a device into a grid — dispatches to correct card type */
void HAScreen::addDeviceCardToGrid(lv_obj_t* grid, const HADeviceGroup& grp,
                                    const HAEntity* entities, bool wide) {
    int16_t w = wide ? CARD_WIDE : CARD_STD;
    const char* dom = grp.domain;

    /* Capture child count before renderer adds a tile */
    uint32_t childBefore = lv_obj_get_child_cnt(grid);

    /* Person cards get special rendering */
    if (strcmp(dom, "device_tracker") == 0) {
        addPersonCard(grid, grp, entities);
    }
    /* Multi-entity devices → multi-entity card */
    else if (grp.entity_count > 1) {
        /* Climate devices with sensors → climate card (first climate entity) */
        if (strcmp(dom, "climate") == 0) {
            addClimateCard(grid, entities[grp.entity_start]);
        } else {
            addMultiEntityCard(grid, grp, entities, w);
        }
    }
    /* Single-entity devices → domain-specific card */
    else {
        const HAEntity& ent = entities[grp.entity_start];
        if (strcmp(ent.domain, "climate") == 0)
            addClimateCard(grid, ent);
        else if (strcmp(ent.domain, "sensor") == 0 ||
                 strcmp(ent.domain, "binary_sensor") == 0)
            addSensorRow(grid, ent, w);
        else if (strcmp(ent.domain, "light") == 0 ||
                 strcmp(ent.domain, "switch") == 0 ||
                 strcmp(ent.domain, "fan") == 0)
            addLightSwitchRow(grid, ent, w);
        else if (strcmp(ent.domain, "media_player") == 0)
            addMediaCard(grid, ent, w);
        else
            addGenericRow(grid, ent, w);
    }

    /* Attach click handler to the tile just added (if controllable) */
    const char* primaryDomain = grp.domain;
    if (grp.entity_count == 1) primaryDomain = entities[grp.entity_start].domain;

    uint32_t childAfter = lv_obj_get_child_cnt(grid);
    if (childAfter > childBefore && isControllable(primaryDomain)) {
        lv_obj_t* tile = lv_obj_get_child(grid, childAfter - 1);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, onCardClick, LV_EVENT_CLICKED,
                            (void*)(uintptr_t)grp.entity_start);
    }
}

void HAScreen::rebuildDeviceGrouped(const HAData& ha) {
    LOG_INFO("HA: rebuild label mode — %d devices, %d standalone, %d total",
             ha.device_count,
             ha.entity_count - ha.standalone_start,
             ha.entity_count);

    /* ── Discover unique domain sections from devices ── */
    char sections[10][16];
    uint8_t sectionCount = 0;

    for (uint8_t d = 0; d < ha.device_count; d++) {
        const char* sec = sectionKey(ha.devices[d].domain);
        bool found = false;
        for (uint8_t s = 0; s < sectionCount; s++) {
            if (strcmp(sections[s], sec) == 0) { found = true; break; }
        }
        if (!found && sectionCount < 10) {
            strncpy(sections[sectionCount], sec, 15);
            sections[sectionCount][15] = '\0';
            sectionCount++;
        }
    }

    /* Sort sections by domain priority */
    for (uint8_t i = 0; i < sectionCount; i++) {
        for (uint8_t j = i + 1; j < sectionCount; j++) {
            if (domainOrder(sections[j]) < domainOrder(sections[i])) {
                char tmp[16];
                memcpy(tmp, sections[i], 16);
                memcpy(sections[i], sections[j], 16);
                memcpy(sections[j], tmp, 16);
            }
        }
    }

    /* ── Render each domain section ── */
    for (uint8_t s = 0; s < sectionCount; s++) {
        const char* sec = sections[s];

        /* Collect device indices for this section */
        uint8_t devIdx[MAX_HA_DEVICES];
        uint8_t devCount = 0;
        for (uint8_t d = 0; d < ha.device_count; d++) {
            if (strcmp(sectionKey(ha.devices[d].domain), sec) == 0) {
                devIdx[devCount++] = d;
            }
        }
        if (devCount == 0) continue;

        /* Section header */
        addSectionHeader(sec, devCount);

        /* Card grid */
        lv_obj_t* grid = makeSectionGrid();

        /* Render each device as a card */
        for (uint8_t i = 0; i < devCount; i++) {
            const HADeviceGroup& grp = ha.devices[devIdx[i]];
            bool wide = needsWideCard(sec, grp.entity_count);
            addDeviceCardToGrid(grid, grp, ha.entities, wide);
        }
    }

    /* ── Standalone entities (no device) ── */
    uint8_t standaloneCount = ha.entity_count - ha.standalone_start;
    if (standaloneCount > 0) {
        addSectionHeader("sensor", standaloneCount);
        lv_obj_t* grid = makeSectionGrid();
        for (uint8_t i = ha.standalone_start; i < ha.entity_count; i++) {
            const HAEntity& ent = ha.entities[i];
            if (strcmp(ent.domain, "climate") == 0)
                addClimateCard(grid, ent);
            else if (strcmp(ent.domain, "sensor") == 0 ||
                     strcmp(ent.domain, "binary_sensor") == 0)
                addSensorRow(grid, ent, CARD_STD);
            else if (strcmp(ent.domain, "light") == 0 ||
                     strcmp(ent.domain, "switch") == 0)
                addLightSwitchRow(grid, ent, CARD_STD);
            else if (strcmp(ent.domain, "media_player") == 0)
                addMediaCard(grid, ent, CARD_STD);
            else
                addGenericRow(grid, ent, CARD_STD);
        }
    }
}

/* ═══════════════════════════════════════════════════════
 *  Domain Mode: Legacy Grouped-by-Domain Rendering
 * ═══════════════════════════════════════════════════════ */

void HAScreen::addDomainGroup(const char* domain, const HAEntity* entities,
                               const uint8_t* indices, uint8_t count) {
    lv_color_t accent = domainAccent(domain);

    /* Section header */
    lv_obj_t* hdr = lv_obj_create(_entityList);
    lv_obj_set_size(hdr, TILE_FULL, SY(26));
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblIcon = lv_label_create(hdr);
    lv_obj_set_style_text_font(lblIcon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblIcon, accent, 0);
    lv_obj_align(lblIcon, LV_ALIGN_LEFT_MID, SX(4), 0);
    lv_label_set_text(lblIcon, domainIcon(domain));

    lv_obj_t* lblName = lv_label_create(hdr);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblName, TEXT_SECONDARY, 0);
    lv_obj_align(lblName, LV_ALIGN_LEFT_MID, SX(26), 0);
    char hdrBuf[48];
    snprintf(hdrBuf, sizeof(hdrBuf), "%s (%d)", domainLabel(domain), count);
    lv_label_set_text(lblName, hdrBuf);

    /* Tile grid — row-wrap flex for tile layout */
    lv_obj_t* grid = lv_obj_create(_entityList);
    lv_obj_set_size(grid, TILE_FULL, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_column(grid, TILE_GAP, 0);
    lv_obj_set_style_pad_row(grid, TILE_GAP, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    /* Dispatch to domain-specific tile renderers */
    for (uint8_t i = 0; i < count; i++) {
        const HAEntity& ent = entities[indices[i]];
        if (strcmp(ent.domain, "climate") == 0)
            addClimateCard(grid, ent);
        else if (strcmp(ent.domain, "sensor") == 0 ||
                 strcmp(ent.domain, "binary_sensor") == 0)
            addSensorRow(grid, ent, CARD_STD);
        else if (strcmp(ent.domain, "light") == 0 ||
                 strcmp(ent.domain, "switch") == 0 ||
                 strcmp(ent.domain, "fan") == 0)
            addLightSwitchRow(grid, ent, CARD_STD);
        else if (strcmp(ent.domain, "media_player") == 0)
            addMediaCard(grid, ent, CARD_STD);
        else
            addGenericRow(grid, ent, CARD_STD);
    }
}

void HAScreen::rebuildDomainGrouped(const HAData& ha) {
    /* Discover unique domains */
    char domains[16][16];
    uint8_t domainCount = 0;
    for (uint8_t i = 0; i < ha.entity_count; i++) {
        bool found = false;
        for (uint8_t d = 0; d < domainCount; d++) {
            if (strcmp(domains[d], ha.entities[i].domain) == 0) {
                found = true;
                break;
            }
        }
        if (!found && domainCount < 16) {
            strncpy(domains[domainCount], ha.entities[i].domain, 15);
            domains[domainCount][15] = '\0';
            domainCount++;
        }
    }

    /* Sort by domain priority (climate first, sensors last) */
    for (uint8_t i = 0; i < domainCount; i++) {
        for (uint8_t j = i + 1; j < domainCount; j++) {
            if (domainOrder(domains[j]) < domainOrder(domains[i])) {
                char tmp[16];
                memcpy(tmp, domains[i], 16);
                memcpy(domains[i], domains[j], 16);
                memcpy(domains[j], tmp, 16);
            }
        }
    }

    LOG_INFO("HA: %d domains (sorted)", domainCount);

    /* Build domain groups with tile grids */
    for (uint8_t d = 0; d < domainCount; d++) {
        uint8_t indices[MAX_HA_ENTITIES];
        uint8_t count = 0;
        for (uint8_t i = 0; i < ha.entity_count; i++) {
            if (strcmp(ha.entities[i].domain, domains[d]) == 0)
                indices[count++] = i;
        }
        addDomainGroup(domains[d], ha.entities, indices, count);
    }
}

/* ═══════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════ */

void HAScreen::rebuildEntityList(const HAData& ha) {
    LOG_INFO("HA: rebuild — entity_count=%d label_mode=%d",
             ha.entity_count, ha.label_mode ? 1 : 0);

    /* Preserve scroll position across rebuilds so the user doesn't
     * lose their place when data refreshes every 30s. */
    lv_coord_t scrollY = lv_obj_get_scroll_y(_entityList);

    lv_obj_clean(_entityList);

    if (ha.entity_count == 0) {
        lv_obj_t* lbl = lv_label_create(_entityList);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, "No Home Assistant entities");
        lv_obj_set_width(lbl, TILE_FULL);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(lbl, SY(60), 0);
        return;
    }

    if (ha.label_mode && _groupByRoom) {
        rebuildByRoom(ha);
    } else if (ha.label_mode) {
        rebuildDeviceGrouped(ha);
    } else {
        rebuildDomainGrouped(ha);
    }

    /* Restore scroll — clamp to new content height to avoid overscroll */
    if (scrollY > 0) {
        lv_obj_update_layout(_entityList);  /* force layout calc first */
        lv_coord_t maxScroll = lv_obj_get_scroll_bottom(_entityList);
        if (scrollY > maxScroll) scrollY = maxScroll;
        lv_obj_scroll_to_y(_entityList, scrollY, LV_ANIM_OFF);
    }
}

/* ── onShow ─────────────────────────────────────────── */
void HAScreen::onShow() {
    LOG_INFO("HA: onShow() _lastData=%p dirty=%d", _lastData, _dirty);
    if (!_lastData) {
        LOG_WARN("HA: onShow() — no data yet");
        return;
    }
    LOG_INFO("HA: onShow() status=%d entities=%d",
             (int)_lastData->home_assistant.status,
             _lastData->home_assistant.data.entity_count);

    if (_lastData->home_assistant.status == SourceStatus::OK) {
        rebuildEntityList(_lastData->home_assistant.data);
    } else {
        lv_obj_clean(_entityList);
        lv_obj_t* lbl = lv_label_create(_entityList);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl,
            _lastData->home_assistant.status == SourceStatus::ERROR
                ? "Home Assistant: connection error"
                : "No Home Assistant data");
        lv_obj_set_width(lbl, TILE_FULL);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(lbl, SY(60), 0);
    }
    _dirty = false;
}

/* ── update ─────────────────────────────────────────── */
void HAScreen::update(const DashboardData& data) {
    _lastData = &data;
    _dirty = true;
    LOG_INFO("HA: update() status=%d entities=%d active=%d",
             (int)data.home_assistant.status,
             data.home_assistant.data.entity_count,
             (lv_scr_act() == _screen) ? 1 : 0);

    /* Rebuild only if currently visible — avoids stale LVGL layout trees */
    if (lv_scr_act() == _screen) {
        if (data.home_assistant.status == SourceStatus::OK) {
            rebuildEntityList(data.home_assistant.data);
        } else {
            lv_obj_clean(_entityList);
            lv_obj_t* lbl = lv_label_create(_entityList);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
            lv_label_set_text(lbl,
                data.home_assistant.status == SourceStatus::ERROR
                    ? "Home Assistant: connection error"
                    : "No Home Assistant data");
            lv_obj_set_width(lbl, TILE_FULL);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_pad_top(lbl, SY(60), 0);
        }
        _dirty = false;
    }
}

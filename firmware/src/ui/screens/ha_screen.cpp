/**
 * Home Assistant Screen — HA-style tile widget cards.
 * Grid layout with domain-specific card renderers.
 * Tiles arranged in row-wrap flex (4 standard / 2 wide per row).
 */

#include "ui/screens/ha_screen.h"
#include <cstring>
#include "logger.h"

/* ── Colors ─────────────────────────────────────────── */
static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t TILE_BG        = lv_color_hex(0x1c1c36);
static const lv_color_t TILE_BG_ON     = lv_color_hex(0x2a2a50);
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

/* ── Layout constants ───────────────────────────────── */
static constexpr int16_t CONTENT_Y   = 30;
static constexpr int16_t PAD         = 10;
static constexpr int16_t TILE_W      = 180;
static constexpr int16_t TILE_H      = 70;
static constexpr int16_t TILE_WIDE   = 366;   /* 2 x TILE_W + gap */
static constexpr int16_t TILE_TALL   = 90;
static constexpr int16_t TILE_GAP    = 6;
static constexpr int16_t TILE_R      = 10;

/* ── Domain helpers ─────────────────────────────────── */
static const char* domainLabel(const char* d) {
    if (strcmp(d, "climate") == 0)       return "Climate";
    if (strcmp(d, "light") == 0)         return "Lights";
    if (strcmp(d, "switch") == 0)        return "Switches";
    if (strcmp(d, "media_player") == 0)  return "Media";
    if (strcmp(d, "sensor") == 0)        return "Sensors";
    if (strcmp(d, "binary_sensor") == 0) return "Sensors";
    if (strcmp(d, "person") == 0)        return "People";
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
    return ACCENT;
}

/* Sort priority: lower = rendered first */
static uint8_t domainOrder(const char* d) {
    if (strcmp(d, "climate") == 0)       return 0;
    if (strcmp(d, "light") == 0)         return 1;
    if (strcmp(d, "switch") == 0)        return 2;
    if (strcmp(d, "media_player") == 0)  return 3;
    if (strcmp(d, "sensor") == 0)        return 4;
    if (strcmp(d, "binary_sensor") == 0) return 5;
    if (strcmp(d, "person") == 0)        return 6;
    return 7;
}

/* ── Tile base helper ───────────────────────────────── */
static lv_obj_t* makeTile(lv_obj_t* parent, int16_t w, int16_t h) {
    lv_obj_t* t = lv_obj_create(parent);
    lv_obj_set_size(t, w, h);
    lv_obj_set_style_bg_color(t, TILE_BG, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, TILE_R, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_pad_all(t, 8, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    return t;
}

static const char* entityName(const HAEntity& e) {
    return e.friendly_name[0] ? e.friendly_name : e.entity_id;
}

static void capitalizeFirst(char* buf) {
    if (buf[0] >= 'a' && buf[0] <= 'z') buf[0] -= 32;
}

/* ── Screen create ──────────────────────────────────── */
void HAScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* Scrollable tile container — full content area */
    _entityList = lv_obj_create(_screen);
    lv_obj_set_size(_entityList, 780, 392);
    lv_obj_set_pos(_entityList, PAD, CONTENT_Y);
    lv_obj_set_style_bg_opa(_entityList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_entityList, 0, 0);
    lv_obj_set_style_pad_all(_entityList, 0, 0);
    lv_obj_set_style_pad_row(_entityList, 6, 0);
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
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 28, 2);
    lv_obj_set_width(name, 200);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, entityName(e));

    /* Current temp — large, right side */
    lv_obj_t* cur = lv_label_create(tile);
    lv_obj_set_style_text_font(cur, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(cur, TEXT_PRIMARY, 0);
    lv_obj_align(cur, LV_ALIGN_TOP_RIGHT, 0, -4);
    char buf[16];
    if (e.extra.climate.current_temp > 0)
        snprintf(buf, sizeof(buf), "%.0fF", e.extra.climate.current_temp);
    else
        snprintf(buf, sizeof(buf), "%s", e.state);
    lv_label_set_text(cur, buf);

    /* HVAC action — colored dot + text, bottom left */
    lv_obj_t* dot = lv_obj_create(tile);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_bg_color(dot, accent, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_LEFT, 0, -2);

    lv_obj_t* action = lv_label_create(tile);
    lv_obj_set_style_text_font(action, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(action, accent, 0);
    lv_obj_align(action, LV_ALIGN_BOTTOM_LEFT, 16, 0);
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

/* ── Sensor Tile (standard size) ────────────────────── */
void HAScreen::addSensorRow(lv_obj_t* parent, const HAEntity& e) {
    lv_obj_t* tile = makeTile(parent, TILE_W, TILE_H);

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
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 22, 2);
    lv_obj_set_width(name, TILE_W - 40);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, entityName(e));

    /* Value + unit — prominent */
    lv_obj_t* val = lv_label_create(tile);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(val, TEXT_PRIMARY, 0);
    lv_obj_align(val, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    char buf[48];
    if (strcmp(e.domain, "sensor") == 0 && e.extra.sensor.unit[0])
        snprintf(buf, sizeof(buf), "%s %s", e.state, e.extra.sensor.unit);
    else
        snprintf(buf, sizeof(buf), "%s", e.state);
    lv_label_set_text(val, buf);
}

/* ── Light / Switch / Fan Tile (standard size) ──────── */
void HAScreen::addLightSwitchRow(lv_obj_t* parent, const HAEntity& e) {
    bool isOn = (strcmp(e.state, "on") == 0);
    lv_color_t stateClr = isOn ? domainAccent(e.domain) : STATE_OFF;

    lv_obj_t* tile = makeTile(parent, TILE_W, TILE_H);
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
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 26, 2);
    lv_obj_set_width(name, TILE_W - 44);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, entityName(e));

    /* State label — "On" / "Off" */
    lv_obj_t* st = lv_label_create(tile);
    lv_obj_set_style_text_font(st, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(st, isOn ? stateClr : TEXT_DIM, 0);
    lv_obj_align(st, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_label_set_text(st, isOn ? "On" : "Off");
}

/* ── Media Card (wide tile) ─────────────────────────── */
void HAScreen::addMediaCard(lv_obj_t* parent, const HAEntity& e) {
    bool playing = (strcmp(e.state, "playing") == 0);
    lv_obj_t* tile = makeTile(parent, TILE_WIDE, 76);

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
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 28, 2);
    lv_obj_set_width(name, 230);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, entityName(e));

    /* State — top right, capitalized */
    lv_obj_t* stLbl = lv_label_create(tile);
    lv_obj_set_style_text_font(stLbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(stLbl, playing ? MEDIA_ACCENT : TEXT_DIM, 0);
    lv_obj_align(stLbl, LV_ALIGN_TOP_RIGHT, 0, 2);
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
        lv_obj_set_width(info, TILE_WIDE - 24);
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

/* ── Generic Tile (person, lock, cover, etc.) ───────── */
void HAScreen::addGenericRow(lv_obj_t* parent, const HAEntity& e) {
    bool isPositive = (strcmp(e.state, "on") == 0 ||
                       strcmp(e.state, "home") == 0 ||
                       strcmp(e.state, "locked") == 0);
    lv_color_t stateClr = isPositive ? domainAccent(e.domain) : TEXT_DIM;

    lv_obj_t* tile = makeTile(parent, TILE_W, TILE_H);

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
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 26, 2);
    lv_obj_set_width(name, TILE_W - 44);
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

/* ── Domain Group: header + tile grid ───────────────── */
void HAScreen::addDomainGroup(const char* domain, const HAEntity* entities,
                               const uint8_t* indices, uint8_t count) {
    lv_color_t accent = domainAccent(domain);

    /* Section header */
    lv_obj_t* hdr = lv_obj_create(_entityList);
    lv_obj_set_size(hdr, 760, 26);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblIcon = lv_label_create(hdr);
    lv_obj_set_style_text_font(lblIcon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblIcon, accent, 0);
    lv_obj_align(lblIcon, LV_ALIGN_LEFT_MID, 4, 0);
    lv_label_set_text(lblIcon, domainIcon(domain));

    lv_obj_t* lblName = lv_label_create(hdr);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblName, TEXT_SECONDARY, 0);
    lv_obj_align(lblName, LV_ALIGN_LEFT_MID, 26, 0);
    char hdrBuf[48];
    snprintf(hdrBuf, sizeof(hdrBuf), "%s (%d)", domainLabel(domain), count);
    lv_label_set_text(lblName, hdrBuf);

    /* Tile grid — row-wrap flex for tile layout */
    lv_obj_t* grid = lv_obj_create(_entityList);
    lv_obj_set_size(grid, 760, LV_SIZE_CONTENT);
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
        if (strcmp(domain, "climate") == 0)
            addClimateCard(grid, ent);
        else if (strcmp(domain, "sensor") == 0 ||
                 strcmp(domain, "binary_sensor") == 0)
            addSensorRow(grid, ent);
        else if (strcmp(domain, "light") == 0 ||
                 strcmp(domain, "switch") == 0 ||
                 strcmp(domain, "fan") == 0)
            addLightSwitchRow(grid, ent);
        else if (strcmp(domain, "media_player") == 0)
            addMediaCard(grid, ent);
        else
            addGenericRow(grid, ent);
    }
}

/* ── Rebuild entity list with sorted domains ────────── */
void HAScreen::rebuildEntityList(const HAData& ha) {
    LOG_INFO("HA: rebuild — entity_count=%d", ha.entity_count);
    lv_obj_clean(_entityList);

    if (ha.entity_count == 0) {
        lv_obj_t* lbl = lv_label_create(_entityList);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, "No Home Assistant entities");
        lv_obj_set_width(lbl, 760);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(lbl, 60, 0);
        return;
    }

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
        lv_obj_set_width(lbl, 760);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(lbl, 60, 0);
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
            lv_obj_set_width(lbl, 760);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_pad_top(lbl, 60, 0);
        }
        _dirty = false;
    }
}

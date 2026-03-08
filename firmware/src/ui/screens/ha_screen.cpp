/**
 * Home Assistant Screen — Implementation
 * Content area: y=30..430.
 * Domain-specific widget cards: climate thermostat, sensors, lights, media.
 */

#include "ui/screens/ha_screen.h"
#include <cstring>
#include "logger.h"

static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG        = lv_color_hex(0x1a1a2e);
static const lv_color_t GROUP_BG       = lv_color_hex(0x252548);
static const lv_color_t TEXT_PRIMARY   = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0xB0B0D0);
static const lv_color_t TEXT_DIM       = lv_color_hex(0x888899);
static const lv_color_t ACCENT         = lv_color_hex(0x6C63FF);
static const lv_color_t STATE_ON       = lv_color_hex(0x44BB44);
static const lv_color_t STATE_OFF      = lv_color_hex(0x666688);
static const lv_color_t CLIMATE_HEAT   = lv_color_hex(0xFF6633);
static const lv_color_t CLIMATE_COOL   = lv_color_hex(0x3399FF);
static const lv_color_t CLIMATE_IDLE   = lv_color_hex(0x888899);

static constexpr int16_t CONTENT_Y = 30;
static constexpr int16_t PAD       = 10;

/* Domain display names */
static const char* domainLabel(const char* domain) {
    if (strcmp(domain, "climate") == 0)       return "Climate";
    if (strcmp(domain, "light") == 0)         return "Lights";
    if (strcmp(domain, "lock") == 0)          return "Security";
    if (strcmp(domain, "binary_sensor") == 0) return "Sensors";
    if (strcmp(domain, "sensor") == 0)        return "Sensors";
    if (strcmp(domain, "switch") == 0)        return "Switches";
    if (strcmp(domain, "cover") == 0)         return "Covers";
    if (strcmp(domain, "fan") == 0)           return "Fans";
    if (strcmp(domain, "media_player") == 0)  return "Media";
    if (strcmp(domain, "person") == 0)        return "People";
    return domain;
}

/* Domain icon (LVGL symbol) */
static const char* domainIcon(const char* domain) {
    if (strcmp(domain, "climate") == 0)       return LV_SYMBOL_CHARGE;
    if (strcmp(domain, "light") == 0)         return LV_SYMBOL_EYE_OPEN;
    if (strcmp(domain, "lock") == 0)          return LV_SYMBOL_CLOSE;
    if (strcmp(domain, "sensor") == 0)        return LV_SYMBOL_GPS;
    if (strcmp(domain, "binary_sensor") == 0) return LV_SYMBOL_GPS;
    if (strcmp(domain, "switch") == 0)        return LV_SYMBOL_POWER;
    if (strcmp(domain, "cover") == 0)         return LV_SYMBOL_UP;
    if (strcmp(domain, "fan") == 0)           return LV_SYMBOL_REFRESH;
    if (strcmp(domain, "media_player") == 0)  return LV_SYMBOL_AUDIO;
    if (strcmp(domain, "person") == 0)        return LV_SYMBOL_GPS;
    return LV_SYMBOL_HOME;
}

void HAScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* Header */
    lv_obj_t* header = lv_label_create(_screen);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(header, TEXT_SECONDARY, 0);
    lv_obj_set_pos(header, PAD + 4, CONTENT_Y + 8);
    lv_label_set_text(header, LV_SYMBOL_CHARGE " Home Assistant");

    /* Scrollable entity list */
    _entityList = lv_obj_create(_screen);
    lv_obj_set_size(_entityList, 780, 360);
    lv_obj_set_pos(_entityList, PAD, CONTENT_Y + 32);
    lv_obj_set_style_bg_opa(_entityList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_entityList, 0, 0);
    lv_obj_set_style_pad_all(_entityList, 0, 0);
    lv_obj_set_style_pad_row(_entityList, 8, 0);
    lv_obj_set_flex_flow(_entityList, LV_FLEX_FLOW_COLUMN);

    LOG_INFO("HA: screen created");
}

/* ─── Climate Card: name, HVAC action, current/target temps ─── */
void HAScreen::addClimateCard(lv_obj_t* parent, const HAEntity& entity) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 720, 56);
    lv_obj_set_style_bg_color(card, GROUP_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Name */
    lv_obj_t* lblName = lv_label_create(card);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblName, TEXT_PRIMARY, 0);
    lv_obj_align(lblName, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_width(lblName, 300);
    lv_label_set_long_mode(lblName, LV_LABEL_LONG_DOT);
    lv_label_set_text(lblName, entity.friendly_name[0]
                                   ? entity.friendly_name
                                   : entity.entity_id);

    /* HVAC action indicator — colored dot */
    bool heating = (strcmp(entity.extra.climate.hvac_action, "heating") == 0);
    bool cooling = (strcmp(entity.extra.climate.hvac_action, "cooling") == 0);
    lv_color_t actionColor = heating ? CLIMATE_HEAT
                           : cooling ? CLIMATE_COOL
                           : CLIMATE_IDLE;

    lv_obj_t* dot = lv_obj_create(card);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_bg_color(dot, actionColor, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 310, 4);

    /* HVAC action text */
    lv_obj_t* lblAction = lv_label_create(card);
    lv_obj_set_style_text_font(lblAction, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblAction, actionColor, 0);
    lv_obj_align(lblAction, LV_ALIGN_TOP_LEFT, 324, 2);
    const char* actionStr = entity.extra.climate.hvac_action;
    if (actionStr[0]) {
        char buf[16];
        strncpy(buf, actionStr, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (buf[0] >= 'a' && buf[0] <= 'z') buf[0] -= 32;
        lv_label_set_text(lblAction, buf);
    } else {
        lv_label_set_text(lblAction, entity.state);
    }

    /* Current temp — large */
    lv_obj_t* lblCur = lv_label_create(card);
    lv_obj_set_style_text_font(lblCur, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lblCur, TEXT_PRIMARY, 0);
    lv_obj_align(lblCur, LV_ALIGN_TOP_RIGHT, 0, -2);
    char tempBuf[16];
    if (entity.extra.climate.current_temp > 0) {
        snprintf(tempBuf, sizeof(tempBuf), "%.0f\xC2\xB0",
                 entity.extra.climate.current_temp);
    } else {
        snprintf(tempBuf, sizeof(tempBuf), "%s", entity.state);
    }
    lv_label_set_text(lblCur, tempBuf);

    /* Target temp — smaller, below current */
    if (entity.extra.climate.target_temp > 0) {
        lv_obj_t* lblTarget = lv_label_create(card);
        lv_obj_set_style_text_font(lblTarget, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lblTarget, TEXT_DIM, 0);
        lv_obj_align(lblTarget, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        char tgtBuf[24];
        snprintf(tgtBuf, sizeof(tgtBuf), "Target: %.0f\xC2\xB0",
                 entity.extra.climate.target_temp);
        lv_label_set_text(lblTarget, tgtBuf);
    }

    /* Preset mode — bottom left */
    if (entity.extra.climate.preset_mode[0]) {
        lv_obj_t* lblPreset = lv_label_create(card);
        lv_obj_set_style_text_font(lblPreset, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lblPreset, TEXT_DIM, 0);
        lv_obj_align(lblPreset, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_label_set_text(lblPreset, entity.extra.climate.preset_mode);
    }
}

/* ─── Sensor Row: name + value with unit ─── */
void HAScreen::addSensorRow(lv_obj_t* parent, const HAEntity& entity) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 720, 28);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Entity name */
    lv_obj_t* lblName = lv_label_create(row);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblName, TEXT_PRIMARY, 0);
    lv_obj_align(lblName, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_width(lblName, 500);
    lv_label_set_long_mode(lblName, LV_LABEL_LONG_DOT);
    lv_label_set_text(lblName, entity.friendly_name[0]
                                   ? entity.friendly_name
                                   : entity.entity_id);

    /* Value + unit */
    lv_obj_t* lblValue = lv_label_create(row);
    lv_obj_set_style_text_font(lblValue, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblValue, ACCENT, 0);
    lv_obj_align(lblValue, LV_ALIGN_RIGHT_MID, 0, 0);
    char valBuf[48];
    if (entity.extra.sensor.unit[0]) {
        snprintf(valBuf, sizeof(valBuf), "%s %s",
                 entity.state, entity.extra.sensor.unit);
    } else {
        snprintf(valBuf, sizeof(valBuf), "%s", entity.state);
    }
    lv_label_set_text(lblValue, valBuf);
}

/* ─── Light / Switch / Fan Row: colored dot + name + ON/OFF ─── */
void HAScreen::addLightSwitchRow(lv_obj_t* parent, const HAEntity& entity) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 720, 30);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* On/off indicator dot */
    bool isOn = (strcmp(entity.state, "on") == 0);
    lv_obj_t* dot = lv_obj_create(row);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_bg_color(dot, isOn ? STATE_ON : STATE_OFF, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);

    /* Entity name */
    lv_obj_t* lblName = lv_label_create(row);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblName, TEXT_PRIMARY, 0);
    lv_obj_align(lblName, LV_ALIGN_LEFT_MID, 18, 0);
    lv_obj_set_width(lblName, 480);
    lv_label_set_long_mode(lblName, LV_LABEL_LONG_DOT);
    lv_label_set_text(lblName, entity.friendly_name[0]
                                   ? entity.friendly_name
                                   : entity.entity_id);

    /* State label */
    lv_obj_t* lblState = lv_label_create(row);
    lv_obj_set_style_text_font(lblState, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblState, isOn ? STATE_ON : STATE_OFF, 0);
    lv_obj_align(lblState, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_label_set_text(lblState, isOn ? "ON" : "OFF");
}

/* ─── Media Card: name, state, now playing ─── */
void HAScreen::addMediaCard(lv_obj_t* parent, const HAEntity& entity) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 720, 46);
    lv_obj_set_style_bg_color(card, GROUP_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    bool isPlaying = (strcmp(entity.state, "playing") == 0);

    /* Name */
    lv_obj_t* lblName = lv_label_create(card);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblName, TEXT_PRIMARY, 0);
    lv_obj_align(lblName, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_width(lblName, 400);
    lv_label_set_long_mode(lblName, LV_LABEL_LONG_DOT);
    lv_label_set_text(lblName, entity.friendly_name[0]
                                   ? entity.friendly_name
                                   : entity.entity_id);

    /* State — top right */
    lv_obj_t* lblState = lv_label_create(card);
    lv_obj_set_style_text_font(lblState, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblState, isPlaying ? STATE_ON : TEXT_DIM, 0);
    lv_obj_align(lblState, LV_ALIGN_TOP_RIGHT, 0, 2);
    lv_label_set_text(lblState, entity.state);

    /* Media title — bottom row */
    if (entity.extra.media.media_title[0]) {
        lv_obj_t* lblTitle = lv_label_create(card);
        lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lblTitle, ACCENT, 0);
        lv_obj_align(lblTitle, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_width(lblTitle, 500);
        lv_label_set_long_mode(lblTitle, LV_LABEL_LONG_DOT);
        char buf[80];
        if (entity.extra.media.app_name[0]) {
            snprintf(buf, sizeof(buf), "%s \xE2\x80\xA2 %s",
                     entity.extra.media.app_name, entity.extra.media.media_title);
        } else {
            snprintf(buf, sizeof(buf), "%s", entity.extra.media.media_title);
        }
        lv_label_set_text(lblTitle, buf);
    } else if (entity.extra.media.app_name[0]) {
        lv_obj_t* lblApp = lv_label_create(card);
        lv_obj_set_style_text_font(lblApp, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lblApp, TEXT_DIM, 0);
        lv_obj_align(lblApp, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_label_set_text(lblApp, entity.extra.media.app_name);
    }
}

/* ─── Generic Row: lock, cover, person, etc. ─── */
void HAScreen::addGenericRow(lv_obj_t* parent, const HAEntity& entity) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 720, 28);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Entity name */
    lv_obj_t* lblName = lv_label_create(row);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblName, TEXT_PRIMARY, 0);
    lv_obj_align(lblName, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_width(lblName, 500);
    lv_label_set_long_mode(lblName, LV_LABEL_LONG_DOT);
    lv_label_set_text(lblName, entity.friendly_name[0]
                                   ? entity.friendly_name
                                   : entity.entity_id);

    /* State value */
    lv_obj_t* lblState = lv_label_create(row);
    lv_obj_set_style_text_font(lblState, &lv_font_montserrat_14, 0);
    lv_obj_align(lblState, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_label_set_text(lblState, entity.state);

    /* Color based on state */
    bool isOn = (strcmp(entity.state, "on") == 0 ||
                 strcmp(entity.state, "home") == 0 ||
                 strcmp(entity.state, "locked") == 0);
    bool isOff = (strcmp(entity.state, "off") == 0 ||
                  strcmp(entity.state, "away") == 0 ||
                  strcmp(entity.state, "unlocked") == 0);
    if (isOn)
        lv_obj_set_style_text_color(lblState, STATE_ON, 0);
    else if (isOff)
        lv_obj_set_style_text_color(lblState, STATE_OFF, 0);
    else
        lv_obj_set_style_text_color(lblState, TEXT_SECONDARY, 0);
}

/* ─── Domain Group Container ─── */
void HAScreen::addDomainGroup(const char* domain, const HAEntity* entities,
                               const uint8_t* indices, uint8_t count) {
    lv_obj_t* group = lv_obj_create(_entityList);
    lv_obj_set_size(group, 760, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(group, CARD_BG, 0);
    lv_obj_set_style_bg_opa(group, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(group, 10, 0);
    lv_obj_set_style_border_width(group, 0, 0);
    lv_obj_set_style_pad_all(group, 10, 0);
    lv_obj_set_style_pad_row(group, 4, 0);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);

    /* Group header */
    lv_obj_t* hdr = lv_obj_create(group);
    lv_obj_set_size(hdr, 740, 28);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblIcon = lv_label_create(hdr);
    lv_obj_set_style_text_font(lblIcon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblIcon, ACCENT, 0);
    lv_obj_align(lblIcon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_text(lblIcon, domainIcon(domain));

    lv_obj_t* lblDomain = lv_label_create(hdr);
    lv_obj_set_style_text_font(lblDomain, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblDomain, TEXT_PRIMARY, 0);
    lv_obj_align(lblDomain, LV_ALIGN_LEFT_MID, 24, 0);
    char hdrBuf[48];
    snprintf(hdrBuf, sizeof(hdrBuf), "%s (%d)", domainLabel(domain), count);
    lv_label_set_text(lblDomain, hdrBuf);

    /* Entity cards — dispatched by domain */
    for (uint8_t i = 0; i < count; i++) {
        const HAEntity& ent = entities[indices[i]];
        if (strcmp(domain, "climate") == 0) {
            addClimateCard(group, ent);
        } else if (strcmp(domain, "sensor") == 0 ||
                   strcmp(domain, "binary_sensor") == 0) {
            addSensorRow(group, ent);
        } else if (strcmp(domain, "light") == 0 ||
                   strcmp(domain, "switch") == 0 ||
                   strcmp(domain, "fan") == 0) {
            addLightSwitchRow(group, ent);
        } else if (strcmp(domain, "media_player") == 0) {
            addMediaCard(group, ent);
        } else {
            addGenericRow(group, ent);
        }
    }
}

void HAScreen::rebuildEntityList(const HAData& ha) {
    lv_obj_clean(_entityList);

    if (ha.entity_count == 0) {
        lv_obj_t* lbl = lv_label_create(_entityList);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, "No Home Assistant entities");
        lv_obj_set_width(lbl, 760);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(lbl, 60, 0);
        return;
    }

    /* Discover unique domains */
    char domains[MAX_HA_ENTITIES][16];
    uint8_t domainCount = 0;

    for (uint8_t i = 0; i < ha.entity_count; i++) {
        bool found = false;
        for (uint8_t d = 0; d < domainCount; d++) {
            if (strcmp(domains[d], ha.entities[i].domain) == 0) {
                found = true;
                break;
            }
        }
        if (!found && domainCount < MAX_HA_ENTITIES) {
            strncpy(domains[domainCount], ha.entities[i].domain, 15);
            domains[domainCount][15] = '\0';
            domainCount++;
        }
    }

    /* For each domain, collect indices and create group */
    for (uint8_t d = 0; d < domainCount; d++) {
        uint8_t indices[MAX_HA_ENTITIES];
        uint8_t count = 0;
        for (uint8_t i = 0; i < ha.entity_count; i++) {
            if (strcmp(ha.entities[i].domain, domains[d]) == 0) {
                indices[count++] = i;
            }
        }
        addDomainGroup(domains[d], ha.entities, indices, count);
    }
}

void HAScreen::onShow() {
    if (!_lastData) return;
    if (_lastData->home_assistant.status == SourceStatus::OK) {
        rebuildEntityList(_lastData->home_assistant.data);
    } else {
        lv_obj_clean(_entityList);
        lv_obj_t* lbl = lv_label_create(_entityList);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
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

void HAScreen::update(const DashboardData& data) {
    _lastData = &data;
    _dirty = true;
    /* Rebuild only if we're the currently-visible screen.
       Offscreen rebuilds create dirty LVGL layout trees that hang Core 1
       when the screen becomes visible via lv_scr_load_anim(). */
    if (lv_scr_act() == _screen) {
        if (data.home_assistant.status == SourceStatus::OK) {
            rebuildEntityList(data.home_assistant.data);
        } else {
            lv_obj_clean(_entityList);
            lv_obj_t* lbl = lv_label_create(_entityList);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
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

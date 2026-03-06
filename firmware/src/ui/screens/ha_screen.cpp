/**
 * Home Assistant Screen — Implementation
 * Content area: y=30..430.
 * Scrollable entity list grouped by domain with state display.
 */

#include "ui/screens/ha_screen.h"
#include <cstring>
#include "logger.h"

static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG        = lv_color_hex(0x1a1a2e);
static const lv_color_t GROUP_BG       = lv_color_hex(0x252548);
static const lv_color_t TEXT_PRIMARY   = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0x8888AA);
static const lv_color_t ACCENT         = lv_color_hex(0x6C63FF);
static const lv_color_t STATE_ON       = lv_color_hex(0x44BB44);
static const lv_color_t STATE_OFF      = lv_color_hex(0x666688);

static constexpr int16_t CONTENT_Y = 30;
static constexpr int16_t PAD       = 10;

/* Domain display names */
static const char* domainLabel(const char* domain) {
    if (strcmp(domain, "climate") == 0)     return "Climate";
    if (strcmp(domain, "light") == 0)       return "Lights";
    if (strcmp(domain, "lock") == 0)        return "Security";
    if (strcmp(domain, "binary_sensor") == 0) return "Sensors";
    if (strcmp(domain, "sensor") == 0)      return "Sensors";
    if (strcmp(domain, "switch") == 0)      return "Switches";
    if (strcmp(domain, "cover") == 0)       return "Covers";
    if (strcmp(domain, "fan") == 0)         return "Fans";
    if (strcmp(domain, "media_player") == 0) return "Media";
    return domain;
}

/* Domain icon (LVGL symbol) */
static const char* domainIcon(const char* domain) {
    if (strcmp(domain, "climate") == 0)     return LV_SYMBOL_CHARGE;
    if (strcmp(domain, "light") == 0)       return LV_SYMBOL_EYE_OPEN;
    if (strcmp(domain, "lock") == 0)        return LV_SYMBOL_CLOSE;
    if (strcmp(domain, "sensor") == 0)      return LV_SYMBOL_GPS;
    if (strcmp(domain, "binary_sensor") == 0) return LV_SYMBOL_GPS;
    if (strcmp(domain, "switch") == 0)      return LV_SYMBOL_POWER;
    if (strcmp(domain, "cover") == 0)       return LV_SYMBOL_UP;
    if (strcmp(domain, "fan") == 0)         return LV_SYMBOL_REFRESH;
    if (strcmp(domain, "media_player") == 0) return LV_SYMBOL_AUDIO;
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
    lv_label_set_text(header, LV_SYMBOL_HOME " Home Assistant");

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

void HAScreen::addEntityRow(lv_obj_t* parent, const HAEntity& entity) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 720, 30);
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
                 strcmp(entity.state, "locked") == 0 ||
                 strcmp(entity.state, "heating") == 0 ||
                 strcmp(entity.state, "cooling") == 0);
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

    /* Entity rows */
    for (uint8_t i = 0; i < count; i++) {
        addEntityRow(group, entities[indices[i]]);
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

void HAScreen::update(const DashboardData& data) {
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
}

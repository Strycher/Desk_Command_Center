/**
 * Claude Activity Screen — Implementation
 * Content area: y=30..430.
 * Agent status card + Beads task summary.
 */

#include "ui/screens/claude_screen.h"
#include <cstring>
#include "logger.h"

static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG        = lv_color_hex(0x1a1a2e);
static const lv_color_t TEXT_PRIMARY   = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0x8888AA);
static const lv_color_t ACCENT         = lv_color_hex(0x6C63FF);
static const lv_color_t STATUS_ACTIVE  = lv_color_hex(0x44BB44);
static const lv_color_t STATUS_IDLE    = lv_color_hex(0xFFAA00);
static const lv_color_t STATUS_OFF     = lv_color_hex(0xFF4444);

static constexpr int16_t CONTENT_Y = 30;
static constexpr int16_t PAD       = 10;

static lv_obj_t* makeCard(lv_obj_t* parent, int16_t x, int16_t y,
                           int16_t w, int16_t h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

void ClaudeScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* Header */
    lv_obj_t* header = lv_label_create(_screen);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(header, TEXT_SECONDARY, 0);
    lv_obj_set_pos(header, PAD + 4, CONTENT_Y + 8);
    lv_label_set_text(header, LV_SYMBOL_CHARGE " Claude Agent");

    /* === Agent Status Card === */
    lv_obj_t* agentCard = makeCard(_screen, PAD, CONTENT_Y + 35, 780, 160);

    /* Status dot */
    _statusDot = lv_obj_create(agentCard);
    lv_obj_set_size(_statusDot, 16, 16);
    lv_obj_align(_statusDot, LV_ALIGN_TOP_LEFT, 0, 4);
    lv_obj_set_style_bg_color(_statusDot, STATUS_OFF, 0);
    lv_obj_set_style_bg_opa(_statusDot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_statusDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_statusDot, 0, 0);

    /* Status text */
    _lblStatus = lv_label_create(agentCard);
    lv_obj_set_style_text_font(_lblStatus, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblStatus, TEXT_PRIMARY, 0);
    lv_obj_align(_lblStatus, LV_ALIGN_TOP_LEFT, 26, 0);
    lv_label_set_text(_lblStatus, "Offline");

    /* Current task */
    lv_obj_t* taskHdr = lv_label_create(agentCard);
    lv_label_set_text(taskHdr, "Current Task:");
    lv_obj_set_style_text_font(taskHdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(taskHdr, TEXT_SECONDARY, 0);
    lv_obj_align(taskHdr, LV_ALIGN_TOP_LEFT, 0, 40);

    _lblTask = lv_label_create(agentCard);
    lv_obj_set_style_text_font(_lblTask, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lblTask, TEXT_PRIMARY, 0);
    lv_obj_align(_lblTask, LV_ALIGN_TOP_LEFT, 0, 62);
    lv_obj_set_width(_lblTask, 740);
    lv_label_set_long_mode(_lblTask, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_lblTask, "No active task");

    /* === Beads Summary Card === */
    lv_obj_t* beadsCard = makeCard(_screen, PAD, CONTENT_Y + 210, 780, 120);

    lv_obj_t* beadsHdr = lv_label_create(beadsCard);
    lv_label_set_text(beadsHdr, "Beads Task Tracker");
    lv_obj_set_style_text_font(beadsHdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(beadsHdr, TEXT_SECONDARY, 0);
    lv_obj_align(beadsHdr, LV_ALIGN_TOP_LEFT, 0, 0);

    _lblBeadsOpen = lv_label_create(beadsCard);
    lv_obj_set_style_text_font(_lblBeadsOpen, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblBeadsOpen, TEXT_PRIMARY, 0);
    lv_obj_align(_lblBeadsOpen, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_label_set_text(_lblBeadsOpen, "Open: --");

    _lblBeadsIP = lv_label_create(beadsCard);
    lv_obj_set_style_text_font(_lblBeadsIP, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblBeadsIP, TEXT_PRIMARY, 0);
    lv_obj_align(_lblBeadsIP, LV_ALIGN_TOP_LEFT, 250, 30);
    lv_label_set_text(_lblBeadsIP, "In Progress: --");

    _lblBeadsBlk = lv_label_create(beadsCard);
    lv_obj_set_style_text_font(_lblBeadsBlk, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblBeadsBlk, STATUS_OFF, 0);
    lv_obj_align(_lblBeadsBlk, LV_ALIGN_TOP_LEFT, 540, 30);
    lv_label_set_text(_lblBeadsBlk, "Blocked: --");

    LOG_INFO("CLAUDE: screen created");
}

void ClaudeScreen::update(const DashboardData& data) {
    /* Agent status */
    if (data.claude.status == SourceStatus::OK) {
        const ClaudeData& c = data.claude.data;

        lv_color_t dotColor = STATUS_OFF;
        if (strcmp(c.status, "active") == 0)      dotColor = STATUS_ACTIVE;
        else if (strcmp(c.status, "idle") == 0)    dotColor = STATUS_IDLE;

        lv_obj_set_style_bg_color(_statusDot, dotColor, 0);

        char statusBuf[32];
        /* Capitalize first letter */
        snprintf(statusBuf, sizeof(statusBuf), "%s", c.status);
        if (statusBuf[0] >= 'a' && statusBuf[0] <= 'z') statusBuf[0] -= 32;
        lv_label_set_text(_lblStatus, statusBuf);

        if (c.current_task[0] != '\0') {
            lv_label_set_text(_lblTask, c.current_task);
        } else {
            lv_label_set_text(_lblTask, "No active task");
        }
    } else {
        lv_obj_set_style_bg_color(_statusDot, STATUS_OFF, 0);
        lv_label_set_text(_lblStatus, "Offline");
        lv_label_set_text(_lblTask, "No data from bridge");
    }

    /* Beads summary */
    if (data.beads.status == SourceStatus::OK) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Open: %d", data.beads.data.open_count);
        lv_label_set_text(_lblBeadsOpen, buf);
        snprintf(buf, sizeof(buf), "In Progress: %d",
                 data.beads.data.in_progress_count);
        lv_label_set_text(_lblBeadsIP, buf);
        snprintf(buf, sizeof(buf), "Blocked: %d",
                 data.beads.data.blocked_count);
        lv_label_set_text(_lblBeadsBlk, buf);
        lv_obj_set_style_text_color(_lblBeadsBlk,
            data.beads.data.blocked_count > 0 ? STATUS_OFF : TEXT_PRIMARY, 0);
    } else {
        lv_label_set_text(_lblBeadsOpen, "Open: --");
        lv_label_set_text(_lblBeadsIP, "In Progress: --");
        lv_label_set_text(_lblBeadsBlk, "Blocked: --");
    }
}

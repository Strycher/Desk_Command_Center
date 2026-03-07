/**
 * DevOps Screen — Implementation
 * Content area: y=30..430.
 * Left: scrollable repo cards. Right: Beads + agent status.
 */

#include "ui/screens/devops_screen.h"
#include <cstring>
#include "logger.h"

static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG        = lv_color_hex(0x1a1a2e);
static const lv_color_t TEXT_PRIMARY   = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0xB0B0D0);
static const lv_color_t ACCENT         = lv_color_hex(0x6C63FF);
static const lv_color_t CI_PASS       = lv_color_hex(0x44BB44);
static const lv_color_t CI_FAIL       = lv_color_hex(0xFF4444);
static const lv_color_t CI_PEND       = lv_color_hex(0xFFAA00);

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
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

void DevOpsScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* === Left: scrollable repo list === */
    lv_obj_t* repoHeader = lv_label_create(_screen);
    lv_obj_set_style_text_font(repoHeader, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(repoHeader, TEXT_SECONDARY, 0);
    lv_obj_set_pos(repoHeader, PAD + 4, CONTENT_Y + 8);
    lv_label_set_text(repoHeader, "Repositories");

    _repoList = lv_obj_create(_screen);
    lv_obj_set_size(_repoList, 480, 360);
    lv_obj_set_pos(_repoList, PAD, CONTENT_Y + 32);
    lv_obj_set_style_bg_opa(_repoList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_repoList, 0, 0);
    lv_obj_set_style_pad_all(_repoList, 0, 0);
    lv_obj_set_style_pad_row(_repoList, 6, 0);
    lv_obj_set_flex_flow(_repoList, LV_FLEX_FLOW_COLUMN);

    /* === Right: Beads summary card === */
    lv_obj_t* beadsCard = makeCard(_screen, 500, CONTENT_Y + PAD, 290, 130);

    lv_obj_t* beadsHeader = lv_label_create(beadsCard);
    lv_label_set_text(beadsHeader, "Beads Tasks");
    lv_obj_set_style_text_font(beadsHeader, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(beadsHeader, TEXT_SECONDARY, 0);
    lv_obj_align(beadsHeader, LV_ALIGN_TOP_LEFT, 0, 0);

    _lblBeadsOpen = lv_label_create(beadsCard);
    lv_obj_set_style_text_font(_lblBeadsOpen, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lblBeadsOpen, TEXT_PRIMARY, 0);
    lv_obj_align(_lblBeadsOpen, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_label_set_text(_lblBeadsOpen, "Open: --");

    _lblBeadsIP = lv_label_create(beadsCard);
    lv_obj_set_style_text_font(_lblBeadsIP, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lblBeadsIP, TEXT_PRIMARY, 0);
    lv_obj_align(_lblBeadsIP, LV_ALIGN_TOP_LEFT, 0, 52);
    lv_label_set_text(_lblBeadsIP, "In Progress: --");

    _lblBeadsBlk = lv_label_create(beadsCard);
    lv_obj_set_style_text_font(_lblBeadsBlk, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lblBeadsBlk, CI_FAIL, 0);
    lv_obj_align(_lblBeadsBlk, LV_ALIGN_TOP_LEFT, 0, 78);
    lv_label_set_text(_lblBeadsBlk, "Blocked: --");

    /* === Right: Agent status card === */
    lv_obj_t* agentCard = makeCard(_screen, 500, CONTENT_Y + 150, 290, 120);

    lv_obj_t* agentHeader = lv_label_create(agentCard);
    lv_label_set_text(agentHeader, "Claude Agent");
    lv_obj_set_style_text_font(agentHeader, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(agentHeader, TEXT_SECONDARY, 0);
    lv_obj_align(agentHeader, LV_ALIGN_TOP_LEFT, 0, 0);

    _lblAgentStatus = lv_label_create(agentCard);
    lv_obj_set_style_text_font(_lblAgentStatus, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lblAgentStatus, TEXT_PRIMARY, 0);
    lv_obj_align(_lblAgentStatus, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_label_set_text(_lblAgentStatus, "Status: --");

    _lblAgentTask = lv_label_create(agentCard);
    lv_obj_set_style_text_font(_lblAgentTask, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblAgentTask, TEXT_SECONDARY, 0);
    lv_obj_align(_lblAgentTask, LV_ALIGN_TOP_LEFT, 0, 52);
    lv_obj_set_width(_lblAgentTask, 260);
    lv_label_set_long_mode(_lblAgentTask, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_lblAgentTask, "");

    LOG_INFO("DEVOPS: screen created");
}

void DevOpsScreen::addRepoCard(const RepoStatus& repo) {
    lv_obj_t* card = lv_obj_create(_repoList);
    lv_obj_set_size(card, 460, 70);
    lv_obj_set_style_bg_color(card, CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Repo name */
    lv_obj_t* lblName = lv_label_create(card);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblName, TEXT_PRIMARY, 0);
    lv_obj_align(lblName, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_width(lblName, 300);
    lv_label_set_long_mode(lblName, LV_LABEL_LONG_DOT);
    lv_label_set_text(lblName, repo.name);

    /* CI badge */
    lv_color_t ciColor = CI_PEND;
    if (strcmp(repo.ci_status, "passing") == 0) ciColor = CI_PASS;
    else if (strcmp(repo.ci_status, "failing") == 0) ciColor = CI_FAIL;

    lv_obj_t* ciBadge = lv_obj_create(card);
    lv_obj_set_size(ciBadge, 10, 10);
    lv_obj_align(ciBadge, LV_ALIGN_TOP_RIGHT, -60, 4);
    lv_obj_set_style_bg_color(ciBadge, ciColor, 0);
    lv_obj_set_style_bg_opa(ciBadge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ciBadge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ciBadge, 0, 0);

    lv_obj_t* lblCI = lv_label_create(card);
    lv_obj_set_style_text_font(lblCI, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblCI, ciColor, 0);
    lv_obj_align(lblCI, LV_ALIGN_TOP_RIGHT, 0, 2);
    lv_label_set_text(lblCI, repo.ci_status);

    /* PRs and Issues */
    char statsBuf[64];
    snprintf(statsBuf, sizeof(statsBuf), "PRs: %d  Issues: %d",
             repo.open_prs, repo.open_issues);
    lv_obj_t* lblStats = lv_label_create(card);
    lv_obj_set_style_text_font(lblStats, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblStats, TEXT_SECONDARY, 0);
    lv_obj_align(lblStats, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_label_set_text(lblStats, statsBuf);
}

void DevOpsScreen::rebuildRepoList(const GitHubData& gh) {
    lv_obj_clean(_repoList);

    if (gh.repo_count == 0) {
        lv_obj_t* lbl = lv_label_create(_repoList);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, "No repositories configured");
        lv_obj_set_width(lbl, 460);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(lbl, 40, 0);
        return;
    }

    for (uint8_t i = 0; i < gh.repo_count && i < MAX_REPOS; i++) {
        addRepoCard(gh.repos[i]);
    }
}

void DevOpsScreen::onShow() {
    if (!_lastData) return;
    /* Rebuild repo list from stored data when screen becomes visible */
    if (_lastData->github.status == SourceStatus::OK) {
        rebuildRepoList(_lastData->github.data);
    } else {
        lv_obj_clean(_repoList);
        lv_obj_t* lbl = lv_label_create(_repoList);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, _lastData->github.status == SourceStatus::ERROR
                               ? "GitHub: connection error" : "No GitHub data");
    }
    _dirty = false;
}

void DevOpsScreen::update(const DashboardData& data) {
    _lastData = &data;
    _dirty = true;

    /* GitHub repos — rebuild only if we're the currently-visible screen.
       Offscreen rebuilds create dirty LVGL layout trees that hang Core 1
       when the screen becomes visible via lv_scr_load_anim(). */
    if (lv_scr_act() == _screen) {
        if (data.github.status == SourceStatus::OK) {
            rebuildRepoList(data.github.data);
        } else {
            lv_obj_clean(_repoList);
            lv_obj_t* lbl = lv_label_create(_repoList);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
            lv_label_set_text(lbl, data.github.status == SourceStatus::ERROR
                                   ? "GitHub: connection error" : "No GitHub data");
        }
        _dirty = false;
    }

    /* Beads summary — safe in-place label updates, fine offscreen */
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
            data.beads.data.blocked_count > 0 ? CI_FAIL : TEXT_PRIMARY, 0);
    } else {
        lv_label_set_text(_lblBeadsOpen, "Open: --");
        lv_label_set_text(_lblBeadsIP, "In Progress: --");
        lv_label_set_text(_lblBeadsBlk, "Blocked: --");
    }

    /* Claude agent */
    if (data.claude.status == SourceStatus::OK) {
        char statusBuf[32];
        snprintf(statusBuf, sizeof(statusBuf), "Status: %s",
                 data.claude.data.status);
        lv_label_set_text(_lblAgentStatus, statusBuf);

        lv_color_t statusColor = TEXT_SECONDARY;
        if (strcmp(data.claude.data.status, "active") == 0)
            statusColor = CI_PASS;
        else if (strcmp(data.claude.data.status, "idle") == 0)
            statusColor = CI_PEND;
        lv_obj_set_style_text_color(_lblAgentStatus, statusColor, 0);

        lv_label_set_text(_lblAgentTask, data.claude.data.current_task);
    } else {
        lv_label_set_text(_lblAgentStatus, "Status: --");
        lv_obj_set_style_text_color(_lblAgentStatus, TEXT_SECONDARY, 0);
        lv_label_set_text(_lblAgentTask, "");
    }
}

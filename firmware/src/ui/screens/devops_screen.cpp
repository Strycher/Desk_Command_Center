/**
 * DevOps Screen — Implementation
 * Content area: y=30..430.
 * Left: scrollable repo cards. Right: Beads + agent status.
 */

#include "ui/screens/devops_screen.h"
#include "ui/ui_scale.h"
#include <cstring>
#include "logger.h"

static DevOpsScreen* s_instance = nullptr;

static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG        = lv_color_hex(0x1a1a2e);
static const lv_color_t TEXT_PRIMARY   = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0xB0B0D0);
static const lv_color_t ACCENT         = lv_color_hex(0x6C63FF);
static const lv_color_t CI_PASS       = lv_color_hex(0x44BB44);
static const lv_color_t CI_FAIL       = lv_color_hex(0xFF4444);
static const lv_color_t CI_PEND       = lv_color_hex(0xFFAA00);

static const lv_color_t HEALTH_GREEN  = lv_color_hex(0x44BB44);
static const lv_color_t HEALTH_YELLOW = lv_color_hex(0xFFAA00);
static const lv_color_t HEALTH_RED    = lv_color_hex(0xFF4444);

static lv_color_t healthColor(uint8_t health) {
    switch (health) {
        case 0: return HEALTH_GREEN;
        case 1: return HEALTH_YELLOW;
        default: return HEALTH_RED;
    }
}

/* Use UI_CONTENT_Y, UI_PAD, UI_CONTENT_W from ui_scale.h */

static lv_obj_t* makeCard(lv_obj_t* parent, int16_t x, int16_t y,
                           int16_t w, int16_t h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, SU(12), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, SU(12), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

void DevOpsScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    s_instance = this;
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* === Left: scrollable repo list === */
    lv_obj_t* repoHeader = lv_label_create(_screen);
    lv_obj_set_style_text_font(repoHeader, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(repoHeader, TEXT_SECONDARY, 0);
    lv_obj_set_pos(repoHeader, UI_PAD + SX(4), UI_CONTENT_Y + SY(8));
    lv_label_set_text(repoHeader, "Repositories");

    _repoList = lv_obj_create(_screen);
    lv_obj_set_size(_repoList, SX(480), SY(360));
    lv_obj_set_pos(_repoList, UI_PAD, UI_CONTENT_Y + SY(32));
    lv_obj_set_style_bg_opa(_repoList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_repoList, 0, 0);
    lv_obj_set_style_pad_all(_repoList, 0, 0);
    lv_obj_set_style_pad_row(_repoList, SU(6), 0);
    lv_obj_set_flex_flow(_repoList, LV_FLEX_FLOW_COLUMN);

    /* === Right: Beads per-project card === */
    lv_obj_t* beadsCard = makeCard(_screen, SX(500), UI_CONTENT_Y + UI_PAD,
                                      SX(290), SY(180));

    lv_obj_t* beadsHeader = lv_label_create(beadsCard);
    lv_label_set_text(beadsHeader, "Beads Projects");
    lv_obj_set_style_text_font(beadsHeader, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(beadsHeader, TEXT_SECONDARY, 0);
    lv_obj_align(beadsHeader, LV_ALIGN_TOP_LEFT, 0, 0);

    _beadsContainer = lv_obj_create(beadsCard);
    lv_obj_set_size(_beadsContainer, SX(266), SY(146));
    lv_obj_align(_beadsContainer, LV_ALIGN_TOP_LEFT, 0, SY(22));
    lv_obj_set_style_bg_opa(_beadsContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_beadsContainer, 0, 0);
    lv_obj_set_style_pad_all(_beadsContainer, 0, 0);
    lv_obj_set_style_pad_row(_beadsContainer, SU(4), 0);
    lv_obj_set_flex_flow(_beadsContainer, LV_FLEX_FLOW_COLUMN);

    /* === Right: Agent status card === */
    lv_obj_t* agentCard = makeCard(_screen, SX(500), UI_CONTENT_Y + SY(200),
                                      SX(290), SY(120));

    lv_obj_t* agentHeader = lv_label_create(agentCard);
    lv_label_set_text(agentHeader, "Claude Agent");
    lv_obj_set_style_text_font(agentHeader, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(agentHeader, TEXT_SECONDARY, 0);
    lv_obj_align(agentHeader, LV_ALIGN_TOP_LEFT, 0, 0);

    _lblAgentStatus = lv_label_create(agentCard);
    lv_obj_set_style_text_font(_lblAgentStatus, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lblAgentStatus, TEXT_PRIMARY, 0);
    lv_obj_align(_lblAgentStatus, LV_ALIGN_TOP_LEFT, 0, SY(26));
    lv_label_set_text(_lblAgentStatus, "Status: --");

    _lblAgentTask = lv_label_create(agentCard);
    lv_obj_set_style_text_font(_lblAgentTask, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblAgentTask, TEXT_SECONDARY, 0);
    lv_obj_align(_lblAgentTask, LV_ALIGN_TOP_LEFT, 0, SY(52));
    lv_obj_set_width(_lblAgentTask, SX(260));
    lv_label_set_long_mode(_lblAgentTask, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_lblAgentTask, "");

    LOG_INFO("DEVOPS: screen created");
}

void DevOpsScreen::addRepoCard(const RepoStatus& repo) {
    lv_obj_t* card = lv_obj_create(_repoList);
    lv_obj_set_size(card, SX(460), SY(70));
    lv_obj_set_style_bg_color(card, CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, SU(10), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, SU(10), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Health dot — left edge */
    lv_obj_t* healthDot = lv_obj_create(card);
    lv_obj_set_size(healthDot, SU(10), SU(10));
    lv_obj_align(healthDot, LV_ALIGN_TOP_LEFT, 0, SU(4));
    lv_obj_set_style_bg_color(healthDot, healthColor(repo.health), 0);
    lv_obj_set_style_bg_opa(healthDot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(healthDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(healthDot, 0, 0);

    /* Repo name — offset right for health dot */
    lv_obj_t* lblName = lv_label_create(card);
    lv_obj_set_style_text_font(lblName, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblName, TEXT_PRIMARY, 0);
    lv_obj_align(lblName, LV_ALIGN_TOP_LEFT, SX(18), 0);
    lv_obj_set_width(lblName, SX(280));
    lv_label_set_long_mode(lblName, LV_LABEL_LONG_DOT);
    lv_label_set_text(lblName, repo.name);

    /* CI badge + text — top right */
    lv_color_t ciColor = CI_PEND;
    if (strcmp(repo.ci_status, "passing") == 0) ciColor = CI_PASS;
    else if (strcmp(repo.ci_status, "failing") == 0) ciColor = CI_FAIL;

    lv_obj_t* ciBadge = lv_obj_create(card);
    lv_obj_set_size(ciBadge, SU(10), SU(10));
    lv_obj_align(ciBadge, LV_ALIGN_TOP_RIGHT, SX(-60), SU(4));
    lv_obj_set_style_bg_color(ciBadge, ciColor, 0);
    lv_obj_set_style_bg_opa(ciBadge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ciBadge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ciBadge, 0, 0);

    lv_obj_t* lblCI = lv_label_create(card);
    lv_obj_set_style_text_font(lblCI, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblCI, ciColor, 0);
    lv_obj_align(lblCI, LV_ALIGN_TOP_RIGHT, 0, SY(2));
    lv_label_set_text(lblCI, repo.ci_status);

    /* PRs: "X ready / Y draft" with age-based coloring */
    char prBuf[48];
    uint8_t ready = (repo.draft_prs <= repo.open_prs)
        ? repo.open_prs - repo.draft_prs : 0;
    if (repo.draft_prs > 0) {
        snprintf(prBuf, sizeof(prBuf), "PRs: %d ready / %d draft", ready, repo.draft_prs);
    } else if (repo.open_prs > 0) {
        snprintf(prBuf, sizeof(prBuf), "PRs: %d ready", ready);
    } else {
        snprintf(prBuf, sizeof(prBuf), "PRs: 0");
    }

    /* Color by PR age. 255 = NTP not synced, treat as unknown (no color). */
    lv_color_t prColor = TEXT_SECONDARY;
    if (repo.oldest_pr_days != 255) {
        if (repo.oldest_pr_days > 7) prColor = HEALTH_RED;
        else if (repo.oldest_pr_days > 3) prColor = HEALTH_YELLOW;
    }

    lv_obj_t* lblPRs = lv_label_create(card);
    lv_obj_set_style_text_font(lblPRs, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblPRs, prColor, 0);
    lv_obj_align(lblPRs, LV_ALIGN_TOP_LEFT, SX(18), SY(26));
    lv_label_set_text(lblPRs, prBuf);

    /* Issues count — right side of bottom row */
    char issBuf[32];
    snprintf(issBuf, sizeof(issBuf), "Issues: %d", repo.open_issues);
    lv_obj_t* lblIss = lv_label_create(card);
    lv_obj_set_style_text_font(lblIss, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblIss, TEXT_SECONDARY, 0);
    lv_obj_align(lblIss, LV_ALIGN_TOP_RIGHT, 0, SY(26));
    lv_label_set_text(lblIss, issBuf);

    /* Tap to show CI detail — child index = repo array index */
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, repoCardClickCb, LV_EVENT_CLICKED, nullptr);
}

void DevOpsScreen::rebuildRepoList(const GitHubData& gh) {
    lv_obj_clean(_repoList);

    if (gh.repo_count == 0) {
        lv_obj_t* lbl = lv_label_create(_repoList);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, "No repositories configured");
        lv_obj_set_width(lbl, SX(460));
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(lbl, SY(40), 0);
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
    /* Close CI popup on data update to avoid stale data references */
    closeCIPopup();
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

    /* Beads — per-project rows */
    if (data.beads.status == SourceStatus::OK) {
        lv_obj_clean(_beadsContainer);
        const BeadsData& bd = data.beads.data;

        if (bd.project_count == 0) {
            /* Fallback to aggregate if no per-project data */
            char buf[48];
            snprintf(buf, sizeof(buf), "Open: %d  IP: %d  Blk: %d",
                     bd.open_count, bd.in_progress_count, bd.blocked_count);
            lv_obj_t* lbl = lv_label_create(_beadsContainer);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl, TEXT_PRIMARY, 0);
            lv_label_set_text(lbl, buf);
        } else {
            for (uint8_t i = 0; i < bd.project_count; i++) {
                const BeadsProject& proj = bd.projects[i];
                lv_obj_t* row = lv_obj_create(_beadsContainer);
                lv_obj_set_size(row, SX(266), SY(28));
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_pad_all(row, 0, 0);
                lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

                /* Project name */
                lv_obj_t* lblName = lv_label_create(row);
                lv_obj_set_style_text_font(lblName, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(lblName, TEXT_PRIMARY, 0);
                lv_obj_align(lblName, LV_ALIGN_LEFT_MID, 0, 0);
                lv_label_set_text(lblName, proj.name);

                /* Counts: open / ip / blocked */
                char buf[48];
                snprintf(buf, sizeof(buf), "%d / %d / %d",
                         proj.open, proj.in_progress, proj.blocked);
                lv_obj_t* lblCounts = lv_label_create(row);
                lv_obj_set_style_text_font(lblCounts, &lv_font_montserrat_14, 0);
                lv_obj_align(lblCounts, LV_ALIGN_RIGHT_MID, 0, 0);

                /* Color: red if blocked > open (structurally stuck) */
                bool stuck = proj.blocked > proj.open && proj.blocked > 0;
                lv_obj_set_style_text_color(lblCounts,
                    stuck ? CI_FAIL : TEXT_SECONDARY, 0);
                lv_label_set_text(lblCounts, buf);
            }
        }
    } else {
        lv_obj_clean(_beadsContainer);
        lv_obj_t* lbl = lv_label_create(_beadsContainer);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, "No beads data");
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

void DevOpsScreen::onHide() {
    closeCIPopup();
}

void DevOpsScreen::repoCardClickCb(lv_event_t* e) {
    if (!s_instance || !s_instance->_lastData) return;
    if (s_instance->_lastData->github.status != SourceStatus::OK) return;
    lv_obj_t* card = lv_event_get_target(e);
    uint32_t idx = lv_obj_get_index(card);
    if (idx >= s_instance->_lastData->github.data.repo_count) return;
    s_instance->showCIDetail(s_instance->_lastData->github.data.repos[idx]);
}

void DevOpsScreen::showCIDetail(const RepoStatus& repo) {
    closeCIPopup();  // close any existing

    _ciPopup = lv_obj_create(_screen);
    lv_obj_set_size(_ciPopup, SX(400), SY(280));
    lv_obj_center(_ciPopup);
    lv_obj_set_style_bg_color(_ciPopup, lv_color_hex(0x16162e), 0);
    lv_obj_set_style_bg_opa(_ciPopup, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_ciPopup, SU(16), 0);
    lv_obj_set_style_border_color(_ciPopup, ACCENT, 0);
    lv_obj_set_style_border_width(_ciPopup, 2, 0);
    lv_obj_set_style_pad_all(_ciPopup, SU(16), 0);
    lv_obj_set_style_shadow_width(_ciPopup, SU(20), 0);
    lv_obj_set_style_shadow_opa(_ciPopup, LV_OPA_50, 0);

    /* Title */
    lv_obj_t* title = lv_label_create(_ciPopup);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, TEXT_PRIMARY, 0);
    lv_obj_set_width(title, SX(340));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(title, repo.name);

    /* CI workflow rows */
    lv_obj_t* list = lv_obj_create(_ciPopup);
    lv_obj_set_size(list, SX(368), SY(180));
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, SY(30));
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, SU(6), 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    if (repo.ci_run_count == 0) {
        lv_obj_t* lbl = lv_label_create(list);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);
        lv_label_set_text(lbl, "No CI workflow runs");
    } else {
        for (uint8_t i = 0; i < repo.ci_run_count; i++) {
            const CIRun& run = repo.ci_runs[i];

            lv_obj_t* row = lv_obj_create(list);
            lv_obj_set_size(row, SX(368), SY(32));
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            /* Status dot */
            lv_color_t runColor = CI_PEND;
            if (strcmp(run.conclusion, "success") == 0) runColor = CI_PASS;
            else if (strcmp(run.conclusion, "failure") == 0) runColor = CI_FAIL;

            lv_obj_t* dot = lv_obj_create(row);
            lv_obj_set_size(dot, SU(8), SU(8));
            lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_style_bg_color(dot, runColor, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(dot, 0, 0);

            /* Workflow name */
            lv_obj_t* lblWf = lv_label_create(row);
            lv_obj_set_style_text_font(lblWf, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lblWf, TEXT_PRIMARY, 0);
            lv_obj_align(lblWf, LV_ALIGN_LEFT_MID, SX(16), 0);
            lv_obj_set_width(lblWf, SX(200));
            lv_label_set_long_mode(lblWf, LV_LABEL_LONG_DOT);
            lv_label_set_text(lblWf, run.workflow);

            /* Branch */
            lv_obj_t* lblBr = lv_label_create(row);
            lv_obj_set_style_text_font(lblBr, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lblBr, TEXT_SECONDARY, 0);
            lv_obj_align(lblBr, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_set_width(lblBr, SX(120));
            lv_label_set_long_mode(lblBr, LV_LABEL_LONG_DOT);
            lv_label_set_text(lblBr, run.branch);
        }
    }

    /* Close button — "X" top-right */
    lv_obj_t* closeBtn = lv_btn_create(_ciPopup);
    lv_obj_set_size(closeBtn, SU(28), SU(28));
    lv_obj_align(closeBtn, LV_ALIGN_TOP_RIGHT, SU(4), SU(-4));
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x333355), 0);
    lv_obj_set_style_radius(closeBtn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(closeBtn, 0, 0);
    lv_obj_t* lblX = lv_label_create(closeBtn);
    lv_label_set_text(lblX, LV_SYMBOL_CLOSE);
    lv_obj_center(lblX);
    lv_obj_add_event_cb(closeBtn, ciPopupCloseCb, LV_EVENT_CLICKED, nullptr);
}

void DevOpsScreen::ciPopupCloseCb(lv_event_t* e) {
    if (s_instance) s_instance->closeCIPopup();
}

void DevOpsScreen::closeCIPopup() {
    if (_ciPopup) {
        lv_obj_del(_ciPopup);
        _ciPopup = nullptr;
    }
}

/**
 * HA Control Modal — Implementation.
 *
 * Modal structure:
 *   backdrop (full-screen, semi-transparent, click-to-close)
 *   +-- panel (400x280, centered)
 *       +-- header (domain icon + device name + X button)
 *       +-- body (domain-specific controls)
 *       +-- overlay (spinner + "Sending..." — shown during command)
 */

#include "ui/ha_control_modal.h"
#include "ha_command.h"
#include "data_service.h"
#include "logger.h"
#include <cstring>

/* -- Colors -- */
static const lv_color_t MODAL_BG      = lv_color_hex(0x1c1c36);
static const lv_color_t BACKDROP_CLR   = lv_color_hex(0x000000);
static const lv_color_t TEXT_PRI       = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SEC       = lv_color_hex(0x9898B8);
static const lv_color_t BTN_BG        = lv_color_hex(0x33335a);
static const lv_color_t BTN_ACTIVE    = lv_color_hex(0x6C63FF);
static const lv_color_t STATE_ON_CLR  = lv_color_hex(0x44BB44);
static const lv_color_t STATE_OFF_CLR = lv_color_hex(0x555566);
static const lv_color_t ERROR_CLR     = lv_color_hex(0xF44336);
static const lv_color_t HEAT_CLR      = lv_color_hex(0xFF6633);
static const lv_color_t COOL_CLR      = lv_color_hex(0x3399FF);

/* -- Layout -- */
static constexpr int16_t PANEL_W = 400;
static constexpr int16_t PANEL_H = 280;
static constexpr int16_t HDR_H   = 40;

/* -- State -- */
static lv_obj_t* _backdrop  = nullptr;
static lv_obj_t* _panel     = nullptr;
static lv_obj_t* _overlay   = nullptr;
static lv_obj_t* _lblState  = nullptr;
static lv_obj_t* _lblToast  = nullptr;
static lv_timer_t* _toastTimer = nullptr;

/* Current entity info (copied for callbacks) */
static char _entityId[64]   = {};
static char _domain[16]     = {};
static char _currentState[32] = {};

/* Climate mode buttons (for re-highlighting on result) */
static constexpr uint8_t CLIMATE_MODE_COUNT = 3;
static const char* _climateModes[] = {"heat", "cool", "off"};
static lv_obj_t* _modeBtns[CLIMATE_MODE_COUNT] = {};

/* -- Forward declarations -- */
static void buildToggleBody(lv_obj_t* body);
static void buildClimateBody(lv_obj_t* body, const HAEntity& entity);
static void buildMediaBody(lv_obj_t* body, const HAEntity& entity);
static void buildCoverBody(lv_obj_t* body);
static void showOverlay();
static void hideOverlay();

/* -- Callbacks -- */
static void onBackdropClick(lv_event_t* e) {
    HAControlModal::close();
}

static void onCloseClick(lv_event_t* e) {
    HAControlModal::close();
}

static void sendCommand(const char* service, const char* dataJson = nullptr) {
    if (HACommand::isBusy()) {
        LOG_WARN("MODAL: command busy");
        return;
    }
    showOverlay();
    HACommand::send(_entityId, service, dataJson);
}

static void onToggleClick(lv_event_t* e) {
    bool isOn = (strcmp(_currentState, "on") == 0 ||
                 strcmp(_currentState, "home") == 0);

    if (strcmp(_domain, "lock") == 0) {
        sendCommand(strcmp(_currentState, "locked") == 0 ? "unlock" : "lock");
    } else {
        sendCommand(isOn ? "turn_off" : "turn_on");
    }
}

static void onToastTimer(lv_timer_t* timer) {
    if (_lblToast) {
        lv_obj_add_flag(_lblToast, LV_OBJ_FLAG_HIDDEN);
    }
    _toastTimer = nullptr;
}

/* -- Climate callbacks -- */
static float _targetTemp = 0;
static lv_obj_t* _lblTarget = nullptr;

static void onTempUp(lv_event_t* e) {
    _targetTemp += 1.0f;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0""F", _targetTemp);
    if (_lblTarget) lv_label_set_text(_lblTarget, buf);
    char data[48];
    snprintf(data, sizeof(data), "{\"temperature\":%.0f}", _targetTemp);
    sendCommand("set_temperature", data);
}

static void onTempDown(lv_event_t* e) {
    _targetTemp -= 1.0f;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0""F", _targetTemp);
    if (_lblTarget) lv_label_set_text(_lblTarget, buf);
    char data[48];
    snprintf(data, sizeof(data), "{\"temperature\":%.0f}", _targetTemp);
    sendCommand("set_temperature", data);
}

static void onHvacMode(lv_event_t* e) {
    const char* mode = (const char*)lv_event_get_user_data(e);
    char data[48];
    snprintf(data, sizeof(data), "{\"hvac_mode\":\"%s\"}", mode);
    sendCommand("set_hvac_mode", data);
}

/* -- Media callbacks -- */
static void onPlayPause(lv_event_t* e) {
    sendCommand("media_play_pause");
}

static void onVolUp(lv_event_t* e) {
    sendCommand("volume_up");
}

static void onVolDown(lv_event_t* e) {
    sendCommand("volume_down");
}

/* -- Cover callbacks -- */
static void onCoverOpen(lv_event_t* e) { sendCommand("open_cover"); }
static void onCoverClose(lv_event_t* e) { sendCommand("close_cover"); }
static void onCoverStop(lv_event_t* e) { sendCommand("stop_cover"); }

/* -- Domain icon (same as ha_screen.cpp) -- */
static const char* modalDomainIcon(const char* d) {
    if (strcmp(d, "climate") == 0)       return LV_SYMBOL_CHARGE;
    if (strcmp(d, "light") == 0)         return LV_SYMBOL_EYE_OPEN;
    if (strcmp(d, "switch") == 0)        return LV_SYMBOL_POWER;
    if (strcmp(d, "media_player") == 0)  return LV_SYMBOL_AUDIO;
    if (strcmp(d, "cover") == 0)         return LV_SYMBOL_UP;
    if (strcmp(d, "fan") == 0)           return LV_SYMBOL_REFRESH;
    if (strcmp(d, "lock") == 0)          return LV_SYMBOL_CLOSE;
    return LV_SYMBOL_HOME;
}

/* ============================================
 *  Public API
 * ============================================ */

void HAControlModal::show(const HAEntity& entity, const char* deviceName) {
    if (_backdrop) close();  // close any existing modal

    /* Cache entity info for callbacks */
    strncpy(_entityId, entity.entity_id, sizeof(_entityId) - 1);
    strncpy(_domain, entity.domain, sizeof(_domain) - 1);
    strncpy(_currentState, entity.state, sizeof(_currentState) - 1);

    lv_obj_t* layer = lv_layer_top();

    /* -- Backdrop -- */
    _backdrop = lv_obj_create(layer);
    lv_obj_set_size(_backdrop, 800, 480);
    lv_obj_set_pos(_backdrop, 0, 0);
    lv_obj_set_style_bg_color(_backdrop, BACKDROP_CLR, 0);
    lv_obj_set_style_bg_opa(_backdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_backdrop, 0, 0);
    lv_obj_set_style_radius(_backdrop, 0, 0);
    lv_obj_clear_flag(_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_backdrop, onBackdropClick, LV_EVENT_CLICKED, nullptr);

    /* -- Panel -- */
    _panel = lv_obj_create(_backdrop);
    lv_obj_set_size(_panel, PANEL_W, PANEL_H);
    lv_obj_center(_panel);
    lv_obj_set_style_bg_color(_panel, MODAL_BG, 0);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_panel, 12, 0);
    lv_obj_set_style_border_width(_panel, 1, 0);
    lv_obj_set_style_border_color(_panel, lv_color_hex(0x33335a), 0);
    lv_obj_set_style_pad_all(_panel, 12, 0);
    lv_obj_clear_flag(_panel, LV_OBJ_FLAG_SCROLLABLE);
    /* Stop clicks on panel from propagating to backdrop */
    lv_obj_add_flag(_panel, LV_OBJ_FLAG_CLICKABLE);

    /* -- Header: icon + name + close -- */
    lv_obj_t* icon = lv_label_create(_panel);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, BTN_ACTIVE, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(icon, modalDomainIcon(entity.domain));

    lv_obj_t* title = lv_label_create(_panel);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, TEXT_PRI, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 28, 2);
    lv_obj_set_width(title, PANEL_W - 80);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text(title, deviceName);

    lv_obj_t* btnClose = lv_btn_create(_panel);
    lv_obj_set_size(btnClose, 32, 32);
    lv_obj_align(btnClose, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_set_style_bg_opa(btnClose, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btnClose, 0, 0);
    lv_obj_t* lblX = lv_label_create(btnClose);
    lv_label_set_text(lblX, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(lblX, TEXT_SEC, 0);
    lv_obj_center(lblX);
    lv_obj_add_event_cb(btnClose, onCloseClick, LV_EVENT_CLICKED, nullptr);

    /* -- Body: domain-specific controls -- */
    lv_obj_t* body = lv_obj_create(_panel);
    lv_obj_set_size(body, PANEL_W - 24, PANEL_H - HDR_H - 40);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, HDR_H);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    if (strcmp(entity.domain, "climate") == 0) {
        buildClimateBody(body, entity);
    } else if (strcmp(entity.domain, "media_player") == 0) {
        buildMediaBody(body, entity);
    } else if (strcmp(entity.domain, "cover") == 0) {
        buildCoverBody(body);
    } else {
        /* light, switch, fan, lock -- toggle */
        buildToggleBody(body);
    }

    /* -- Toast label (hidden initially) -- */
    _lblToast = lv_label_create(_panel);
    lv_obj_set_style_text_font(_lblToast, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblToast, STATE_ON_CLR, 0);
    lv_obj_align(_lblToast, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(_lblToast, LV_OBJ_FLAG_HIDDEN);

    /* -- Overlay (hidden initially) -- */
    _overlay = lv_obj_create(_panel);
    lv_obj_set_size(_overlay, PANEL_W - 24, PANEL_H - HDR_H - 40);
    lv_obj_align(_overlay, LV_ALIGN_TOP_LEFT, 0, HDR_H);
    lv_obj_set_style_bg_color(_overlay, MODAL_BG, 0);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(_overlay, 0, 0);
    lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* spinner = lv_spinner_create(_overlay, 1000, 60);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t* lblSending = lv_label_create(_overlay);
    lv_obj_set_style_text_font(lblSending, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblSending, TEXT_SEC, 0);
    lv_obj_align(lblSending, LV_ALIGN_CENTER, 0, 24);
    lv_label_set_text(lblSending, "Sending...");

    LOG_INFO("MODAL: opened for %s (%s) state=%s",
             deviceName, entity.entity_id, entity.state);
}

void HAControlModal::close() {
    if (_toastTimer) {
        lv_timer_del(_toastTimer);
        _toastTimer = nullptr;
    }
    if (_backdrop) {
        lv_obj_del_async(_backdrop);
        _backdrop = nullptr;
        _panel = nullptr;
        _overlay = nullptr;
        _lblState = nullptr;
        _lblToast = nullptr;
        _lblTarget = nullptr;
        for (uint8_t i = 0; i < CLIMATE_MODE_COUNT; i++) _modeBtns[i] = nullptr;
    }
    LOG_INFO("MODAL: closed");
}

bool HAControlModal::isOpen() {
    return _backdrop != nullptr;
}

void HAControlModal::onCommandResult(bool success, const char* newState,
                                      const char* error) {
    if (!_backdrop) return;  // modal already closed

    hideOverlay();

    if (success && newState && newState[0]) {
        /* Update stored state */
        strncpy(_currentState, newState, sizeof(_currentState) - 1);

        /* Update state label (domain-aware) */
        if (_lblState) {
            char buf[48];
            if (strcmp(_domain, "climate") == 0) {
                bool isHeat = (strcmp(newState, "heat") == 0 ||
                               strcmp(newState, "heat_cool") == 0);
                bool isCool = (strcmp(newState, "cool") == 0);
                bool isOff  = (strcmp(newState, "off") == 0);
                snprintf(buf, sizeof(buf), "Mode: %s", newState);
                lv_label_set_text(_lblState, buf);
                lv_obj_set_style_text_color(_lblState,
                    isHeat ? HEAT_CLR : isCool ? COOL_CLR
                    : isOff ? STATE_OFF_CLR : TEXT_PRI, 0);
            } else if (strcmp(_domain, "lock") == 0) {
                bool locked = (strcmp(newState, "locked") == 0);
                snprintf(buf, sizeof(buf), "State: %s %s",
                         locked ? LV_SYMBOL_CLOSE : LV_SYMBOL_OK,
                         locked ? "Locked" : "Unlocked");
                lv_label_set_text(_lblState, buf);
                lv_obj_set_style_text_color(_lblState,
                    locked ? STATE_OFF_CLR : STATE_ON_CLR, 0);
            } else {
                bool isOn = (strcmp(newState, "on") == 0 ||
                             strcmp(newState, "home") == 0);
                snprintf(buf, sizeof(buf), "State: %s %s",
                         isOn ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE,
                         isOn ? "On" : "Off");
                lv_label_set_text(_lblState, buf);
                lv_obj_set_style_text_color(_lblState,
                    isOn ? STATE_ON_CLR : STATE_OFF_CLR, 0);
            }
        }

        /* Re-highlight climate mode buttons */
        if (strcmp(_domain, "climate") == 0) {
            for (uint8_t i = 0; i < CLIMATE_MODE_COUNT; i++) {
                if (_modeBtns[i]) {
                    bool active = (strcmp(newState, _climateModes[i]) == 0);
                    lv_obj_set_style_bg_color(_modeBtns[i],
                        active ? BTN_ACTIVE : BTN_BG, 0);
                }
            }
        }

        /* Show success toast */
        if (_lblToast) {
            lv_obj_set_style_text_color(_lblToast, STATE_ON_CLR, 0);
            lv_label_set_text(_lblToast, LV_SYMBOL_OK " Updated");
            lv_obj_clear_flag(_lblToast, LV_OBJ_FLAG_HIDDEN);
            _toastTimer = lv_timer_create(onToastTimer, 2000, nullptr);
            lv_timer_set_repeat_count(_toastTimer, 1);
        }

        /* Force early poll to update the main card grid */
        DataService::forcePoll();
    } else {
        /* Show error toast */
        if (_lblToast) {
            lv_obj_set_style_text_color(_lblToast, ERROR_CLR, 0);
            char msg[96];
            snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " %s",
                     (error && error[0]) ? error : "Command failed");
            lv_label_set_text(_lblToast, msg);
            lv_obj_clear_flag(_lblToast, LV_OBJ_FLAG_HIDDEN);
            _toastTimer = lv_timer_create(onToastTimer, 4000, nullptr);
            lv_timer_set_repeat_count(_toastTimer, 1);
        }
    }
}

/* ============================================
 *  Domain-Specific Control Builders
 * ============================================ */

static void showOverlay() {
    if (_overlay) lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void hideOverlay() {
    if (_overlay) lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* -- Toggle (light, switch, fan, lock) -- */
static void buildToggleBody(lv_obj_t* body) {
    bool isOn = (strcmp(_currentState, "on") == 0 ||
                 strcmp(_currentState, "home") == 0);
    bool isLock = (strcmp(_domain, "lock") == 0);

    /* State label */
    _lblState = lv_label_create(body);
    lv_obj_set_style_text_font(_lblState, &lv_font_montserrat_20, 0);
    lv_obj_align(_lblState, LV_ALIGN_TOP_MID, 0, 20);

    char stateBuf[48];
    if (isLock) {
        bool locked = (strcmp(_currentState, "locked") == 0);
        snprintf(stateBuf, sizeof(stateBuf), "State: %s %s",
                 locked ? LV_SYMBOL_CLOSE : LV_SYMBOL_OK,
                 locked ? "Locked" : "Unlocked");
        lv_obj_set_style_text_color(_lblState,
                                    locked ? STATE_OFF_CLR : STATE_ON_CLR, 0);
    } else {
        snprintf(stateBuf, sizeof(stateBuf), "State: %s %s",
                 isOn ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE,
                 isOn ? "On" : "Off");
        lv_obj_set_style_text_color(_lblState,
                                    isOn ? STATE_ON_CLR : STATE_OFF_CLR, 0);
    }
    lv_label_set_text(_lblState, stateBuf);

    /* Toggle button */
    lv_obj_t* btn = lv_btn_create(body);
    lv_obj_set_size(btn, 180, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(btn, BTN_BG, 0);
    lv_obj_set_style_bg_color(btn, BTN_ACTIVE, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, TEXT_PRI, 0);
    lv_obj_center(lbl);

    if (isLock) {
        lv_label_set_text(lbl,
            strcmp(_currentState, "locked") == 0 ? "Unlock" : "Lock");
    } else {
        lv_label_set_text(lbl, isOn ? "Turn Off" : "Turn On");
    }

    lv_obj_add_event_cb(btn, onToggleClick, LV_EVENT_CLICKED, nullptr);
}

/* -- Climate -- */
static void buildClimateBody(lv_obj_t* body, const HAEntity& entity) {
    _targetTemp = entity.extra.climate.target_temp;

    /* Current temp */
    lv_obj_t* lblCurrent = lv_label_create(body);
    lv_obj_set_style_text_font(lblCurrent, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblCurrent, TEXT_SEC, 0);
    lv_obj_align(lblCurrent, LV_ALIGN_TOP_LEFT, 0, 4);
    char cbuf[32];
    snprintf(cbuf, sizeof(cbuf), "Current: %.0f\xC2\xB0""F",
             entity.extra.climate.current_temp);
    lv_label_set_text(lblCurrent, cbuf);

    /* Target temp: [ - ] value [ + ] */
    lv_obj_t* btnDown = lv_btn_create(body);
    lv_obj_set_size(btnDown, 50, 40);
    lv_obj_align(btnDown, LV_ALIGN_TOP_LEFT, 20, 40);
    lv_obj_set_style_bg_color(btnDown, BTN_BG, 0);
    lv_obj_set_style_radius(btnDown, 8, 0);
    lv_obj_t* lblD = lv_label_create(btnDown);
    lv_label_set_text(lblD, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(lblD, TEXT_PRI, 0);
    lv_obj_center(lblD);
    lv_obj_add_event_cb(btnDown, onTempDown, LV_EVENT_CLICKED, nullptr);

    _lblTarget = lv_label_create(body);
    lv_obj_set_style_text_font(_lblTarget, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lblTarget, TEXT_PRI, 0);
    lv_obj_align(_lblTarget, LV_ALIGN_TOP_MID, 0, 44);
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%.0f\xC2\xB0""F", _targetTemp);
    lv_label_set_text(_lblTarget, tbuf);

    lv_obj_t* btnUp = lv_btn_create(body);
    lv_obj_set_size(btnUp, 50, 40);
    lv_obj_align(btnUp, LV_ALIGN_TOP_RIGHT, -20, 40);
    lv_obj_set_style_bg_color(btnUp, BTN_BG, 0);
    lv_obj_set_style_radius(btnUp, 8, 0);
    lv_obj_t* lblU = lv_label_create(btnUp);
    lv_label_set_text(lblU, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(lblU, TEXT_PRI, 0);
    lv_obj_center(lblU);
    lv_obj_add_event_cb(btnUp, onTempUp, LV_EVENT_CLICKED, nullptr);

    /* Mode state label (updated by onCommandResult) */
    _lblState = lv_label_create(body);
    lv_obj_set_style_text_font(_lblState, &lv_font_montserrat_14, 0);
    lv_obj_align(_lblState, LV_ALIGN_TOP_RIGHT, 0, 4);
    {
        bool isHeat = (strcmp(entity.state, "heat") == 0 ||
                       strcmp(entity.state, "heat_cool") == 0);
        bool isCool = (strcmp(entity.state, "cool") == 0);
        bool isOff  = (strcmp(entity.state, "off") == 0);
        char mbuf[32];
        snprintf(mbuf, sizeof(mbuf), "Mode: %s", entity.state);
        lv_label_set_text(_lblState, mbuf);
        lv_obj_set_style_text_color(_lblState,
            isHeat ? HEAT_CLR : isCool ? COOL_CLR
            : isOff ? STATE_OFF_CLR : TEXT_PRI, 0);
    }

    /* HVAC mode buttons */
    static const char* modeLabels[] = {"Heat", "Cool", "Off"};
    int16_t btnW = 90;
    int16_t startX = (PANEL_W - 24 - btnW * 3 - 16) / 2;

    lv_obj_t* lblMode = lv_label_create(body);
    lv_obj_set_style_text_font(lblMode, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblMode, TEXT_SEC, 0);
    lv_obj_align(lblMode, LV_ALIGN_TOP_LEFT, 0, 92);
    lv_label_set_text(lblMode, "Mode:");

    for (int i = 0; i < CLIMATE_MODE_COUNT; i++) {
        lv_obj_t* btn = lv_btn_create(body);
        lv_obj_set_size(btn, btnW, 34);
        lv_obj_set_pos(btn, startX + i * (btnW + 8), 110);

        bool active = (strcmp(entity.state, _climateModes[i]) == 0);
        lv_obj_set_style_bg_color(btn, active ? BTN_ACTIVE : BTN_BG, 0);
        lv_obj_set_style_radius(btn, 6, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, modeLabels[i]);
        lv_obj_set_style_text_color(lbl, TEXT_PRI, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, onHvacMode, LV_EVENT_CLICKED,
                            (void*)_climateModes[i]);
        _modeBtns[i] = btn;
    }

    /* Preset buttons (if preset_mode is available) */
    if (entity.extra.climate.preset_mode[0]) {
        static const char* presets[] = {"home", "away"};
        static const char* presetLabels[] = {"Home", "Away"};

        lv_obj_t* lblPreset = lv_label_create(body);
        lv_obj_set_style_text_font(lblPreset, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lblPreset, TEXT_SEC, 0);
        lv_obj_align(lblPreset, LV_ALIGN_TOP_LEFT, 0, 150);
        lv_label_set_text(lblPreset, "Preset:");

        for (int i = 0; i < 2; i++) {
            lv_obj_t* btn = lv_btn_create(body);
            lv_obj_set_size(btn, 100, 34);
            lv_obj_set_pos(btn, startX + i * 108, 168);

            bool active = (strcmp(entity.extra.climate.preset_mode,
                                 presets[i]) == 0);
            lv_obj_set_style_bg_color(btn, active ? BTN_ACTIVE : BTN_BG, 0);
            lv_obj_set_style_radius(btn, 6, 0);

            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, presetLabels[i]);
            lv_obj_set_style_text_color(lbl, TEXT_PRI, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl);

            char data[64];
            snprintf(data, sizeof(data),
                     "{\"preset_mode\":\"%s\"}", presets[i]);
            /* Can't pass stack string to event -- use static array */
            static char presetData[2][64];
            strncpy(presetData[i], data, sizeof(presetData[i]) - 1);
            /* Use a separate callback for presets */
        }
    }
}

/* -- Media -- */
static void buildMediaBody(lv_obj_t* body, const HAEntity& entity) {
    /* Now Playing info */
    if (entity.extra.media.media_title[0]) {
        lv_obj_t* lblTitle = lv_label_create(body);
        lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lblTitle, TEXT_PRI, 0);
        lv_obj_align(lblTitle, LV_ALIGN_TOP_LEFT, 0, 4);
        lv_obj_set_width(lblTitle, PANEL_W - 48);
        lv_label_set_long_mode(lblTitle, LV_LABEL_LONG_DOT);
        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "Now Playing: %s",
                 entity.extra.media.media_title);
        lv_label_set_text(lblTitle, tbuf);
    }

    if (entity.extra.media.app_name[0]) {
        lv_obj_t* lblApp = lv_label_create(body);
        lv_obj_set_style_text_font(lblApp, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lblApp, TEXT_SEC, 0);
        lv_obj_align(lblApp, LV_ALIGN_TOP_LEFT, 0, 28);
        char abuf[32];
        snprintf(abuf, sizeof(abuf), "App: %s",
                 entity.extra.media.app_name);
        lv_label_set_text(lblApp, abuf);
    }

    /* State label */
    _lblState = lv_label_create(body);
    lv_obj_set_style_text_font(_lblState, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblState, TEXT_SEC, 0);
    lv_obj_align(_lblState, LV_ALIGN_TOP_LEFT, 0, 52);
    char sbuf[32];
    snprintf(sbuf, sizeof(sbuf), "State: %s", _currentState);
    lv_label_set_text(_lblState, sbuf);

    /* Play/Pause button */
    lv_obj_t* btnPlay = lv_btn_create(body);
    lv_obj_set_size(btnPlay, 160, 44);
    lv_obj_align(btnPlay, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(btnPlay, BTN_BG, 0);
    lv_obj_set_style_radius(btnPlay, 8, 0);
    lv_obj_t* lblPlay = lv_label_create(btnPlay);
    lv_obj_set_style_text_font(lblPlay, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblPlay, TEXT_PRI, 0);
    lv_obj_center(lblPlay);
    lv_label_set_text(lblPlay, LV_SYMBOL_PLAY " / " LV_SYMBOL_PAUSE);
    lv_obj_add_event_cb(btnPlay, onPlayPause, LV_EVENT_CLICKED, nullptr);

    /* Volume buttons */
    lv_obj_t* btnVolDown = lv_btn_create(body);
    lv_obj_set_size(btnVolDown, 100, 40);
    lv_obj_align(btnVolDown, LV_ALIGN_BOTTOM_LEFT, 30, 0);
    lv_obj_set_style_bg_color(btnVolDown, BTN_BG, 0);
    lv_obj_set_style_radius(btnVolDown, 8, 0);
    lv_obj_t* lblVD = lv_label_create(btnVolDown);
    lv_label_set_text(lblVD, LV_SYMBOL_VOLUME_MID " -");
    lv_obj_set_style_text_color(lblVD, TEXT_PRI, 0);
    lv_obj_center(lblVD);
    lv_obj_add_event_cb(btnVolDown, onVolDown, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btnVolUp = lv_btn_create(body);
    lv_obj_set_size(btnVolUp, 100, 40);
    lv_obj_align(btnVolUp, LV_ALIGN_BOTTOM_RIGHT, -30, 0);
    lv_obj_set_style_bg_color(btnVolUp, BTN_BG, 0);
    lv_obj_set_style_radius(btnVolUp, 8, 0);
    lv_obj_t* lblVU = lv_label_create(btnVolUp);
    lv_label_set_text(lblVU, LV_SYMBOL_VOLUME_MAX " +");
    lv_obj_set_style_text_color(lblVU, TEXT_PRI, 0);
    lv_obj_center(lblVU);
    lv_obj_add_event_cb(btnVolUp, onVolUp, LV_EVENT_CLICKED, nullptr);
}

/* -- Cover -- */
static void buildCoverBody(lv_obj_t* body) {
    _lblState = lv_label_create(body);
    lv_obj_set_style_text_font(_lblState, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblState, TEXT_PRI, 0);
    lv_obj_align(_lblState, LV_ALIGN_TOP_MID, 0, 20);
    char buf[32];
    snprintf(buf, sizeof(buf), "State: %s", _currentState);
    lv_label_set_text(_lblState, buf);

    int16_t btnW = 100;
    int16_t gap = 12;
    int16_t startX = (PANEL_W - 24 - btnW * 3 - gap * 2) / 2;

    const char* labels[] = {"Open", "Stop", "Close"};
    lv_event_cb_t cbs[] = {onCoverOpen, onCoverStop, onCoverClose};

    for (int i = 0; i < 3; i++) {
        lv_obj_t* btn = lv_btn_create(body);
        lv_obj_set_size(btn, btnW, 44);
        lv_obj_set_pos(btn, startX + i * (btnW + gap), 80);
        lv_obj_set_style_bg_color(btn, BTN_BG, 0);
        lv_obj_set_style_radius(btn, 8, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, TEXT_PRI, 0);
        lv_obj_center(lbl);
        lv_label_set_text(lbl, labels[i]);

        lv_obj_add_event_cb(btn, cbs[i], LV_EVENT_CLICKED, nullptr);
    }
}

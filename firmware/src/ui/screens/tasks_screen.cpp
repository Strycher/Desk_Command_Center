/**
 * Tasks Screen — Implementation
 * Content area: y=30..430, tabbed task list with priority-sorted cards.
 */

#include "ui/screens/tasks_screen.h"
#include "logger.h"

static const lv_color_t BG_COLOR       = lv_color_hex(0x0f0f23);
static const lv_color_t CARD_BG        = lv_color_hex(0x1a1a2e);
static const lv_color_t TEXT_PRIMARY   = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SECONDARY = lv_color_hex(0x8888AA);
static const lv_color_t ACCENT         = lv_color_hex(0x6C63FF);
static const lv_color_t BTN_BG        = lv_color_hex(0x252540);
static const lv_color_t PRIO_HIGH     = lv_color_hex(0xFF4444);
static const lv_color_t PRIO_MED      = lv_color_hex(0xFFAA00);
static const lv_color_t PRIO_LOW      = lv_color_hex(0x44AA44);
static const lv_color_t PRIO_NONE     = lv_color_hex(0x555577);
static const lv_color_t COMPLETED_CLR = lv_color_hex(0x336633);

static constexpr int16_t CONTENT_Y = 30;

/* --- Tab button callbacks --- */

void TasksScreen::onTabAll(lv_event_t* e) {
    auto* self = (TasksScreen*)lv_event_get_user_data(e);
    self->setActiveTab(TabFilter::ALL);
}

void TasksScreen::onTabUnfocused(lv_event_t* e) {
    auto* self = (TasksScreen*)lv_event_get_user_data(e);
    self->setActiveTab(TabFilter::UNFOCUSED);
}

void TasksScreen::onTabMonday(lv_event_t* e) {
    auto* self = (TasksScreen*)lv_event_get_user_data(e);
    self->setActiveTab(TabFilter::MONDAY);
}

void TasksScreen::setActiveTab(TabFilter tab) {
    _activeTab = tab;
    styleTabBtn(_btnAll, tab == TabFilter::ALL);
    styleTabBtn(_btnUnfocused, tab == TabFilter::UNFOCUSED);
    styleTabBtn(_btnMonday, tab == TabFilter::MONDAY);
    rebuildTaskList();
}

void TasksScreen::styleTabBtn(lv_obj_t* btn, bool active) {
    lv_obj_set_style_bg_color(btn, active ? ACCENT : BTN_BG, 0);
}

/* --- Screen creation --- */

void TasksScreen::create(lv_obj_t* parent) {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    /* Tab bar */
    lv_obj_t* tabBar = lv_obj_create(_screen);
    lv_obj_set_size(tabBar, 780, 40);
    lv_obj_set_pos(tabBar, 10, CONTENT_Y + 5);
    lv_obj_set_style_bg_opa(tabBar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tabBar, 0, 0);
    lv_obj_set_style_pad_all(tabBar, 0, 0);
    lv_obj_clear_flag(tabBar, LV_OBJ_FLAG_SCROLLABLE);

    _btnAll = lv_btn_create(tabBar);
    lv_obj_set_size(_btnAll, 90, 34);
    lv_obj_align(_btnAll, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(_btnAll, ACCENT, 0);
    lv_obj_set_style_radius(_btnAll, 8, 0);
    lv_obj_t* lblAll = lv_label_create(_btnAll);
    lv_label_set_text(lblAll, "All");
    lv_obj_set_style_text_color(lblAll, TEXT_PRIMARY, 0);
    lv_obj_center(lblAll);
    lv_obj_add_event_cb(_btnAll, onTabAll, LV_EVENT_CLICKED, this);

    _btnUnfocused = lv_btn_create(tabBar);
    lv_obj_set_size(_btnUnfocused, 120, 34);
    lv_obj_align(_btnUnfocused, LV_ALIGN_LEFT_MID, 100, 0);
    lv_obj_set_style_bg_color(_btnUnfocused, BTN_BG, 0);
    lv_obj_set_style_radius(_btnUnfocused, 8, 0);
    lv_obj_t* lblUF = lv_label_create(_btnUnfocused);
    lv_label_set_text(lblUF, "Unfocused");
    lv_obj_set_style_text_color(lblUF, TEXT_PRIMARY, 0);
    lv_obj_center(lblUF);
    lv_obj_add_event_cb(_btnUnfocused, onTabUnfocused, LV_EVENT_CLICKED, this);

    _btnMonday = lv_btn_create(tabBar);
    lv_obj_set_size(_btnMonday, 120, 34);
    lv_obj_align(_btnMonday, LV_ALIGN_LEFT_MID, 230, 0);
    lv_obj_set_style_bg_color(_btnMonday, BTN_BG, 0);
    lv_obj_set_style_radius(_btnMonday, 8, 0);
    lv_obj_t* lblMO = lv_label_create(_btnMonday);
    lv_label_set_text(lblMO, "Monday");
    lv_obj_set_style_text_color(lblMO, TEXT_PRIMARY, 0);
    lv_obj_center(lblMO);
    lv_obj_add_event_cb(_btnMonday, onTabMonday, LV_EVENT_CLICKED, this);

    /* Scrollable task list */
    _taskList = lv_obj_create(_screen);
    lv_obj_set_size(_taskList, 780, 340);
    lv_obj_set_pos(_taskList, 10, CONTENT_Y + 50);
    lv_obj_set_style_bg_opa(_taskList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_taskList, 0, 0);
    lv_obj_set_style_pad_all(_taskList, 0, 0);
    lv_obj_set_style_pad_row(_taskList, 6, 0);
    lv_obj_set_flex_flow(_taskList, LV_FLEX_FLOW_COLUMN);

    /* Initial empty state */
    _lblEmpty = lv_label_create(_taskList);
    lv_obj_set_style_text_font(_lblEmpty, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lblEmpty, TEXT_SECONDARY, 0);
    lv_label_set_text(_lblEmpty, "No tasks");
    lv_obj_set_width(_lblEmpty, 780);
    lv_obj_set_style_text_align(_lblEmpty, LV_TEXT_ALIGN_CENTER, 0);

    LOG_INFO("TASKS: screen created");
}

/* --- Task card --- */

void TasksScreen::addTaskCard(const char* title, const char* due,
                               uint8_t priority, const char* source,
                               bool completed) {
    lv_obj_t* card = lv_obj_create(_taskList);
    lv_obj_set_size(card, 760, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(card, 50, 0);
    lv_obj_set_style_bg_color(card, completed ? COMPLETED_CLR : CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Priority stripe */
    lv_color_t stripeColor;
    switch (priority) {
        case 1:  stripeColor = PRIO_HIGH; break;
        case 2:  stripeColor = PRIO_MED;  break;
        case 3:  stripeColor = PRIO_LOW;  break;
        default: stripeColor = PRIO_NONE; break;
    }

    lv_obj_t* stripe = lv_obj_create(card);
    lv_obj_set_size(stripe, 4, lv_pct(100));
    lv_obj_align(stripe, LV_ALIGN_LEFT_MID, -6, 0);
    lv_obj_set_style_bg_color(stripe, stripeColor, 0);
    lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(stripe, 2, 0);
    lv_obj_set_style_border_width(stripe, 0, 0);

    /* Title */
    lv_obj_t* lblTitle = lv_label_create(card);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblTitle, TEXT_PRIMARY, 0);
    lv_obj_align(lblTitle, LV_ALIGN_TOP_LEFT, 6, 0);
    lv_obj_set_width(lblTitle, 580);
    lv_label_set_long_mode(lblTitle, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lblTitle, title);

    if (completed) {
        lv_obj_set_style_text_decor(lblTitle, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        lv_obj_set_style_text_opa(lblTitle, LV_OPA_60, 0);
    }

    /* Source badge */
    lv_obj_t* lblSource = lv_label_create(card);
    lv_obj_set_style_text_font(lblSource, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblSource, TEXT_SECONDARY, 0);
    lv_obj_align(lblSource, LV_ALIGN_TOP_RIGHT, -6, 0);
    lv_label_set_text(lblSource, source);

    /* Due date (if present) */
    if (due && due[0] != '\0') {
        lv_obj_t* lblDue = lv_label_create(card);
        lv_obj_set_style_text_font(lblDue, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lblDue, TEXT_SECONDARY, 0);
        lv_obj_align(lblDue, LV_ALIGN_TOP_LEFT, 6, 22);
        char dueBuf[48];
        snprintf(dueBuf, sizeof(dueBuf), "Due: %s", due);
        lv_label_set_text(lblDue, dueBuf);
    }
}

/* --- Rebuild list based on active tab --- */

void TasksScreen::rebuildTaskList() {
    if (!_taskList || !_lastData) return;

    lv_obj_clean(_taskList);

    /* Collect tasks matching the active filter */
    struct MergedTask {
        const char* title;
        const char* due;
        uint8_t priority;
        const char* source;
        bool completed;
    };

    MergedTask merged[MAX_TASKS * 2];
    uint8_t count = 0;

    bool showUF = (_activeTab == TabFilter::ALL ||
                   _activeTab == TabFilter::UNFOCUSED);
    bool showMO = (_activeTab == TabFilter::ALL ||
                   _activeTab == TabFilter::MONDAY);

    if (showUF && _lastData->unfocused_tasks.status == SourceStatus::OK) {
        for (uint8_t i = 0; i < _lastData->unfocused_tasks_count &&
             count < MAX_TASKS * 2; i++) {
            const TaskItem& t = _lastData->unfocused_tasks.data[i];
            merged[count++] = {t.title, t.due_date, t.priority,
                               "Unfocused", t.completed};
        }
    }

    if (showMO && _lastData->monday_tasks.status == SourceStatus::OK) {
        for (uint8_t i = 0; i < _lastData->monday_tasks_count &&
             count < MAX_TASKS * 2; i++) {
            const TaskItem& t = _lastData->monday_tasks.data[i];
            merged[count++] = {t.title, t.due_date, t.priority,
                               "Monday", t.completed};
        }
    }

    /* Sort: incomplete first, then by priority (1=high first) */
    for (uint8_t i = 0; i < count; i++) {
        for (uint8_t j = i + 1; j < count; j++) {
            bool swap = false;
            if (merged[i].completed && !merged[j].completed) {
                swap = true;
            } else if (merged[i].completed == merged[j].completed) {
                uint8_t pi = merged[i].priority == 0 ? 99 : merged[i].priority;
                uint8_t pj = merged[j].priority == 0 ? 99 : merged[j].priority;
                if (pj < pi) swap = true;
            }
            if (swap) {
                MergedTask tmp = merged[i];
                merged[i] = merged[j];
                merged[j] = tmp;
            }
        }
    }

    for (uint8_t i = 0; i < count; i++) {
        addTaskCard(merged[i].title, merged[i].due, merged[i].priority,
                    merged[i].source, merged[i].completed);
    }

    if (count == 0) {
        _lblEmpty = lv_label_create(_taskList);
        lv_obj_set_style_text_font(_lblEmpty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_lblEmpty, TEXT_SECONDARY, 0);
        lv_obj_set_width(_lblEmpty, 760);
        lv_obj_set_style_text_align(_lblEmpty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(_lblEmpty, 60, 0);

        const char* msg = "No tasks";
        if (_activeTab == TabFilter::UNFOCUSED) {
            if (_lastData->unfocused_tasks.status == SourceStatus::ERROR)
                msg = "Unfocused: connection error";
            else
                msg = "No Unfocused tasks";
        } else if (_activeTab == TabFilter::MONDAY) {
            if (_lastData->monday_tasks.status == SourceStatus::ERROR)
                msg = "Monday.com: connection error";
            else
                msg = "No Monday.com tasks";
        }
        lv_label_set_text(_lblEmpty, msg);
    }
}

void TasksScreen::update(const DashboardData& data) {
    _lastData = &data;
    rebuildTaskList();
}

void TasksScreen::onShow() {
    _activeTab = TabFilter::ALL;
    if (_btnAll) styleTabBtn(_btnAll, true);
    if (_btnUnfocused) styleTabBtn(_btnUnfocused, false);
    if (_btnMonday) styleTabBtn(_btnMonday, false);
    if (_lastData) rebuildTaskList();
}

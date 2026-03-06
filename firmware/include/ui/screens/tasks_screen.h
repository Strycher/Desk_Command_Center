/**
 * Tasks Screen — Unfocused + Monday.com task list with source tabs.
 * Tab filtering, priority-sorted cards, empty state per source.
 */

#pragma once
#include "ui/base_screen.h"

class TasksScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;
    void onShow() override;

private:
    enum class TabFilter : uint8_t { ALL, UNFOCUSED, MONDAY };

    lv_obj_t* _btnAll       = nullptr;
    lv_obj_t* _btnUnfocused = nullptr;
    lv_obj_t* _btnMonday    = nullptr;
    lv_obj_t* _taskList     = nullptr;
    lv_obj_t* _lblEmpty     = nullptr;

    const DashboardData* _lastData = nullptr;
    TabFilter _activeTab = TabFilter::ALL;

    void rebuildTaskList();
    void addTaskCard(const char* title, const char* due,
                     uint8_t priority, const char* source, bool completed);
    void setActiveTab(TabFilter tab);
    void styleTabBtn(lv_obj_t* btn, bool active);

    static void onTabAll(lv_event_t* e);
    static void onTabUnfocused(lv_event_t* e);
    static void onTabMonday(lv_event_t* e);
};

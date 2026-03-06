/**
 * Claude Activity Screen — Agent session status display.
 * Shows agent status, current task, and last activity time.
 */

#pragma once
#include "ui/base_screen.h"

class ClaudeScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;

private:
    lv_obj_t* _lblStatus    = nullptr;
    lv_obj_t* _statusDot    = nullptr;
    lv_obj_t* _lblTask      = nullptr;
    lv_obj_t* _lblBeadsOpen = nullptr;
    lv_obj_t* _lblBeadsIP   = nullptr;
    lv_obj_t* _lblBeadsBlk  = nullptr;
};

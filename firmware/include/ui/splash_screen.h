/**
 * Boot Splash Screen — Shown immediately at startup.
 * Dark background, project name, animated spinner, status text.
 */

#pragma once
#include "ui/base_screen.h"

class SplashScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;
    void updateStatus(const char* msg);

private:
    lv_obj_t* _lblTitle  = nullptr;
    lv_obj_t* _spinner   = nullptr;
    lv_obj_t* _lblStatus = nullptr;
};

/**
 * Home Assistant Screen — Entity groups by domain.
 * Dynamic domain grouping from bridge entity data.
 */

#pragma once
#include "ui/base_screen.h"

class HAScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;

private:
    lv_obj_t* _entityList = nullptr;

    void rebuildEntityList(const HAData& ha);
    void addDomainGroup(const char* domain, const HAEntity* entities,
                        const uint8_t* indices, uint8_t count);
    void addEntityRow(lv_obj_t* parent, const HAEntity& entity);
};

/**
 * Home Assistant Screen — Domain-specific widget cards.
 * Climate thermostat, sensor values, light/switch toggles, media cards.
 * Supports device-grouped rendering (label mode) and domain grouping (legacy).
 */

#pragma once
#include "ui/base_screen.h"

class HAScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;
    void onShow() override;

private:
    lv_obj_t* _entityList = nullptr;
    const DashboardData* _lastData = nullptr;
    bool _dirty = false;

    void rebuildEntityList(const HAData& ha);

    /* Label mode: device-grouped rendering */
    void rebuildDeviceGrouped(const HAData& ha);
    void addDeviceCard(lv_obj_t* parent, const HADeviceGroup& grp,
                       const HAEntity* entities);
    void addSingleEntityTile(lv_obj_t* parent, const HAEntity& entity);

    /* Domain mode: legacy grouped-by-domain rendering */
    void rebuildDomainGrouped(const HAData& ha);
    void addDomainGroup(const char* domain, const HAEntity* entities,
                        const uint8_t* indices, uint8_t count);

    /* Domain-specific card renderers (shared by both modes) */
    void addClimateCard(lv_obj_t* parent, const HAEntity& entity);
    void addSensorRow(lv_obj_t* parent, const HAEntity& entity);
    void addLightSwitchRow(lv_obj_t* parent, const HAEntity& entity);
    void addMediaCard(lv_obj_t* parent, const HAEntity& entity);
    void addGenericRow(lv_obj_t* parent, const HAEntity& entity);
};

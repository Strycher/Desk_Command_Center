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

    static void onCardClick(lv_event_t* e);

    void rebuildEntityList(const HAData& ha);

    /* Label mode: domain-sectioned card grid */
    void rebuildDeviceGrouped(const HAData& ha);
    void addSectionHeader(const char* domain, uint8_t count);
    lv_obj_t* makeSectionGrid();
    void addDeviceCardToGrid(lv_obj_t* grid, const HADeviceGroup& grp,
                             const HAEntity* entities, bool wide);
    void addMultiEntityCard(lv_obj_t* parent, const HADeviceGroup& grp,
                            const HAEntity* entities, int16_t w);

    /* Domain mode: legacy grouped-by-domain rendering */
    void rebuildDomainGrouped(const HAData& ha);
    void addDomainGroup(const char* domain, const HAEntity* entities,
                        const uint8_t* indices, uint8_t count);

    /* Domain-specific card renderers (shared by both modes) */
    void addClimateCard(lv_obj_t* parent, const HAEntity& entity);
    void addSensorRow(lv_obj_t* parent, const HAEntity& entity,
                      int16_t w = 0);
    void addLightSwitchRow(lv_obj_t* parent, const HAEntity& entity,
                           int16_t w = 0, const char* displayName = nullptr);
    void addMediaCard(lv_obj_t* parent, const HAEntity& entity,
                      int16_t w = 0);
    void addPersonCard(lv_obj_t* parent, const HADeviceGroup& grp,
                       const HAEntity* entities);
    void addGenericRow(lv_obj_t* parent, const HAEntity& entity,
                       int16_t w = 0);
};

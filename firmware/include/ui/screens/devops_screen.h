/**
 * DevOps Screen — GitHub repos + Citadel/agent status.
 * Scrollable repo cards with CI badges, agent activity summary.
 */

#pragma once
#include "ui/base_screen.h"

class DevOpsScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;
    void onShow() override;
    void onHide() override;

private:
    lv_obj_t* _repoList     = nullptr;
    const DashboardData* _lastData = nullptr;
    bool _dirty = false;

    /* Citadel — per-project rows */
    lv_obj_t* _beadsContainer = nullptr;  // flex column, rebuilt on update

    /* Agent status */
    lv_obj_t* _lblAgentStatus = nullptr;
    lv_obj_t* _lblAgentTask   = nullptr;

    /* CI detail popup */
    lv_obj_t* _ciPopup = nullptr;

    void rebuildRepoList(const GitHubData& gh);
    void addRepoCard(const RepoStatus& repo);

    void showCIDetail(const RepoStatus& repo);
    void closeCIPopup();
    static void ciPopupCloseCb(lv_event_t* e);
    static void repoCardClickCb(lv_event_t* e);
};

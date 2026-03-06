/**
 * DevOps Screen — GitHub repos + Beads/agent status.
 * Scrollable repo cards with CI badges, agent activity summary.
 */

#pragma once
#include "ui/base_screen.h"

class DevOpsScreen : public BaseScreen {
public:
    void create(lv_obj_t* parent) override;
    void update(const DashboardData& data) override;

private:
    lv_obj_t* _repoList     = nullptr;

    /* Beads summary */
    lv_obj_t* _lblBeadsOpen = nullptr;
    lv_obj_t* _lblBeadsIP   = nullptr;
    lv_obj_t* _lblBeadsBlk  = nullptr;

    /* Agent status */
    lv_obj_t* _lblAgentStatus = nullptr;
    lv_obj_t* _lblAgentTask   = nullptr;

    void rebuildRepoList(const GitHubData& gh);
    void addRepoCard(const RepoStatus& repo);
};

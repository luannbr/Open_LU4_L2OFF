#pragma once

#include "Shared/src/StatusWnd.h"

// l2npcstatuswnd.h, no fields beyond CStatusWnd in the target.
class L2NPCStatusWnd final : public CStatusWnd {
public:
    L2NPCStatusWnd();

    void DisplayCommon();
    void OnRefresh() override;

    static L2NPCStatusWnd& Inst();
};

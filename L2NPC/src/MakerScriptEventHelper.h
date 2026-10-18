#pragma once

#include "Shared/src/IOCPMessage.h"

namespace l2npc {
class NpcMakerEx;
}


// GlobalObject.cpp's concrete maker SCRIPT_EVENT completion. Unlike the NPC
// helper, the target carries and keeps the live NpcMakerEx object itself.
class MakerScriptEventHelper final : public CIOCPMessage {
public:
    MakerScriptEventHelper(l2npc::NpcMakerEx* maker, int eventType, int argument2, int argument3);

    void OnReceive(int transferred) override;

private:
    l2npc::NpcMakerEx* m_pMaker = nullptr;
    int m_eventType = 0;
    int m_arg2 = 0;
    int m_arg3 = 0;
};

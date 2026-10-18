#include "L2NPC/src/CScriptEventHelperClass.h"

#include "L2NPC/src/NPC.h"
#include "L2NPC/src/NPCEventFactory.h"
#include "L2NPC/src/NpcEvent.h"
#include "Shared/src/Log.h"


CScriptEventHelperClass::CScriptEventHelperClass(int npcWorldId, int eventType, int argument2, int argument3) noexcept :
    m_npcWorldId(npcWorldId), m_eventType(eventType), m_arg2(argument2), m_arg3(argument3) {}

void CScriptEventHelperClass::OnReceive(int) {
    L2_GUARD_FUNCTION();
    CNPC* const npc = CNPC::GetObjectW(m_npcWorldId);
    if (!npc) {
        return;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateScriptEvent(npc, m_eventType, m_arg2, m_arg3);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CreateScriptEvent failed",
        //     L"GlobalObject.cpp",
        //     0x1cb)
        // Log.Add(LOG_ERROR,
        //     "[NPC AI] Cannot create delayed script event %d for NPC %u (arguments %d, %d). AI callback skipped.",
        //     m_eventType, static_cast<unsigned int>(m_npcWorldId), m_arg2, m_arg3)
        L2_LOG_ERROR(
            Log, "[NPC AI] Cannot create delayed script event %d for NPC %u (arguments %d, %d); AI callback skipped.",
            m_eventType, static_cast<unsigned int>(m_npcWorldId), m_arg2, m_arg3
        );
        return;
    }

    npc->HandleEvent(event);
    L2_RELEASE(event, ObjectRefType::CreateDelete);
}

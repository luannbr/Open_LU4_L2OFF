#include "L2NPC/src/MakerScriptEventHelper.h"

#include "L2NPC/src/MakerEventFactory.h"
#include "L2NPC/src/NpcEvent.h"
#include "L2NPC/src/NpcMakerEx.h"


MakerScriptEventHelper::MakerScriptEventHelper(l2npc::NpcMakerEx* maker, int eventType, int argument2, int argument3) :
    m_pMaker(maker), m_eventType(eventType), m_arg2(argument2), m_arg3(argument3) {
    L2_ADD_REF(m_pMaker, ObjectRefType::IocpMessageRetain);
}

void MakerScriptEventHelper::OnReceive(int) {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateScriptEvent(m_pMaker, m_eventType, m_arg2, m_arg3);
    if (event) {
        m_pMaker->HandleEvent(event);
        L2_RELEASE(event, ObjectRefType::CreateDelete);
    }

    // Shipping has no destructor fallback for this keep. If Post fails,
    // CIOCPMessage is destroyed at IOCPMessage.cpp:0x18 while this ORT4
    // reference remains leaked. Only an accepted OnReceive balances it.
    L2_RELEASE(m_pMaker, ObjectRefType::IocpMessageRetain);
}

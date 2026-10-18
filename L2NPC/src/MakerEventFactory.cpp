#include "MakerEventFactory.h"

#include "L2Core/src/inc/Guard.h"
#include "NPC.h"
#include "NpcEvent.h"
#include "NpcMaker.h"
#include "NpcMakerEx.h"
#include "NpcSpawnDefineEx.h"
#include "Shared/src/TemplateHelper.h"


MakerEvent* MakerEventFactory::CreateStartEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateNpcDeletedEvent(
    l2npc::NpcMakerEx* maker, NpcSpawnDefineEx* definition, CNPC* npc, int argument
) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 1;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    L2_ADD_REF(definition, ObjectRefType::EventRetain);
    event->deleted_def = definition;
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->deleted_npc = npc;
    event->reply = argument;
    event->died = npc->m_bDied ? 1 : 0;
    return event;
}

MakerEvent* MakerEventFactory::CreateAllNpcDeletedEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 2;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateScriptEvent(l2npc::NpcMakerEx* maker, int argument1, int argument2, int argument3) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 3;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    event->script_event_arg1 = argument1;
    event->script_event_arg2 = argument2;
    event->script_event_arg3 = argument3;
    return event;
}

MakerEvent* MakerEventFactory::CreateDbNpcInfoEvent(
    l2npc::NpcMakerEx* maker, NpcSpawnDefineEx* definition, l2npc::DBNpcInfo& record
) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 4;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    event->record0 = &record;
    L2_ADD_REF(definition, ObjectRefType::EventRetain);
    event->loaded_def = definition;
    return event;
}

MakerEvent* MakerEventFactory::CreateTimerEventEx(l2npc::NpcMakerEx* maker, int timerId) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 5;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    event->timer_id = timerId;
    return event;
}

MakerEvent* MakerEventFactory::CreateNpcCreatedEvent(l2npc::NpcMakerEx* maker, NpcSpawnDefineEx* definition, CNPC* npc) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 6;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    L2_ADD_REF(definition, ObjectRefType::EventRetain);
    event->created_def = definition;
    L2_ADD_REF(npc, ObjectRefType::EventRetain);
    event->created_npc = npc;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnNpcPosEvent(l2npc::NpcMakerEx* maker, int enabled) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 7;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    event->enabled = enabled;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnDoorEvent(l2npc::NpcMakerEx* maker, int enabled) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 8;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    event->enabled = enabled;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnStartTeamBattleAgitFinalEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x0e;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnStartSiegeEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 9;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnEndSiegeEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x0a;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnProclaimSiegeEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x0b;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnCancelSiegeEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x0d;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnDestructCTRLTowerEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x0c;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnFieldCycleChangedEvent(l2npc::NpcMakerEx* maker, int eventId, int state) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x0f;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    event->event_id = eventId;
    event->state = state;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnFortressEvent(
    l2npc::NpcMakerEx* maker,
    int residenceId,
    int eventId,
    int reply,
    int argument0,
    int argument1,
    int argument2,
    char16_t* text
) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x10;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    event->residence_id = residenceId;
    event->event_id = eventId;
    event->reply = reply;
    event->i0 = argument0;
    event->i1 = argument1;
    event->i2 = argument2;
    event->s0 = text;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnInstantZoneEvent(
    l2npc::NpcMakerEx* maker,
    int inzoneTypeId,
    int level,
    int inzoneId,
    int eventId,
    int reply,
    int argument0,
    int actionId,
    int scriptArgument1,
    int scriptArgument2,
    int scriptArgument3
) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x11;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    event->inzone_type_id = inzoneTypeId;
    event->level = level;
    event->inzone_id = inzoneId;
    event->event_id = eventId;
    event->reply = reply;
    event->i0 = argument0;
    event->action_id = actionId;
    event->script_event_arg1 = scriptArgument1;
    event->script_event_arg2 = scriptArgument2;
    event->script_event_arg3 = scriptArgument3;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnOlympiadFieldStepChangedEvent(
    l2npc::NpcMakerEx* maker, int eventId, int, int state, int inzoneTypeId, int inzoneId
) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x12;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    event->event_id = eventId;
    event->state = state;
    event->inzone_type_id = inzoneTypeId;
    event->level = 0;
    event->inzone_id = inzoneId;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnExtendedStartSiegeEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x13;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnDestructSupplyEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x14;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateOnDeclareDominionEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x15;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateStartChangeTimeEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x16;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateEndChangeTimeEvent(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return nullptr;
    }

    auto* event = new MakerEvent();
    event->m_nEventId = 0x17;
    L2_ADD_REF(maker, ObjectRefType::EventRetain);
    event->myself = maker;
    return event;
}

MakerEvent* MakerEventFactory::CreateDebugMakerEvent(l2npc::NpcMakerEx& maker, int argument, CCreature& creature) {
    L2_GUARD_FUNCTION();
    auto* event = new MakerEvent();
    event->m_nEventId = 0x18;
    L2_ADD_REF(&(maker), ObjectRefType::EventRetain);
    event->myself = &maker;
    event->reply = argument;
    event->creature = creature.m_data;
    return event;
}

MakerEvent* MakerEventFactory::CreateDynamicQuestEvent(
    l2npc::NpcMakerEx& maker, DynamicQuestType::NPCJobType jobType, char success
) {
    L2_GUARD_FUNCTION();
    auto* event = new MakerEvent();
    event->m_nEventId = 0x19;
    L2_ADD_REF(&(maker), ObjectRefType::EventRetain);
    event->myself = &maker;
    event->reply = static_cast<int>(jobType);
    event->success = static_cast<signed char>(success);
    return event;
}

MakerEvent* MakerEventFactory::CreateDynamicQuestInfoEvent(l2npc::NpcMakerEx& maker, int questId, int eventId) {
    L2_GUARD_FUNCTION();
    auto* event = new MakerEvent();
    event->m_nEventId = 0x1a;
    L2_ADD_REF(&(maker), ObjectRefType::EventRetain);
    event->myself = &maker;
    event->quest_id = questId;
    event->event_id = eventId;
    return event;
}

MakerEvent* MakerEventFactory::CreatePCDieEvent(l2npc::NpcMakerEx& maker, CCreature& creature) {
    L2_GUARD_FUNCTION();
    auto* event = new MakerEvent();
    event->m_nEventId = 0x1b;
    L2_ADD_REF(&(maker), ObjectRefType::EventRetain);
    event->myself = &maker;
    event->creature = creature.m_data;
    return event;
}

MakerEvent* MakerEventFactory::CreateEventCampaignEvent(
    l2npc::NpcMakerEx& maker, EventCampaignType::NPCJobType jobType, char success
) {
    L2_GUARD_FUNCTION();
    auto* event = new MakerEvent();
    event->m_nEventId = 0x1d;
    L2_ADD_REF(&(maker), ObjectRefType::EventRetain);
    event->myself = &maker;
    event->reply = static_cast<int>(jobType);
    event->success = static_cast<signed char>(success);
    return event;
}

MakerEvent* MakerEventFactory::Create_ON_HUNTER_GUILD_BLESS_EVENT(l2npc::NpcMakerEx& maker, bool enabled) {
    L2_GUARD_FUNCTION();
    auto* event = new MakerEvent();
    event->m_nEventId = 0x1e;
    L2_ADD_REF(&(maker), ObjectRefType::EventRetain);
    event->myself = &maker;
    event->reply = static_cast<unsigned char>(enabled);
    return event;
}

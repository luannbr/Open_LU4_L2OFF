#pragma once

namespace l2npc {
class DBNpcInfo;
class NpcMakerEx;
} // namespace l2npc

namespace DynamicQuestType {
enum NPCJobType : int;
}

namespace EventCampaignType {
enum NPCJobType : int;
}


class CCreature;
class CNPC;
struct MakerEvent;
class NpcSpawnDefineEx;

// Recovered from MakerEventFactory.h. The 30 named factories below are the
// complete contiguous production body range at 0x00465438..0x004670b4.
class MakerEventFactory {
public:
    static MakerEvent* CreateStartEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateNpcDeletedEvent(
        l2npc::NpcMakerEx* maker, NpcSpawnDefineEx* definition, CNPC* npc, int argument
    );
    static MakerEvent* CreateAllNpcDeletedEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateScriptEvent(l2npc::NpcMakerEx* maker, int argument1, int argument2, int argument3);
    static MakerEvent* CreateDbNpcInfoEvent(
        l2npc::NpcMakerEx* maker, NpcSpawnDefineEx* definition, l2npc::DBNpcInfo& record
    );
    static MakerEvent* CreateTimerEventEx(l2npc::NpcMakerEx* maker, int timerId);
    static MakerEvent* CreateNpcCreatedEvent(l2npc::NpcMakerEx* maker, NpcSpawnDefineEx* definition, CNPC* npc);
    static MakerEvent* CreateOnNpcPosEvent(l2npc::NpcMakerEx* maker, int enabled);
    static MakerEvent* CreateOnDoorEvent(l2npc::NpcMakerEx* maker, int enabled);
    static MakerEvent* CreateOnStartTeamBattleAgitFinalEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateOnStartSiegeEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateOnEndSiegeEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateOnProclaimSiegeEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateOnCancelSiegeEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateOnDestructCTRLTowerEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateOnFieldCycleChangedEvent(l2npc::NpcMakerEx* maker, int eventId, int state);
    static MakerEvent* CreateOnFortressEvent(
        l2npc::NpcMakerEx* maker,
        int residenceId,
        int eventId,
        int reply,
        int argument0,
        int argument1,
        int argument2,
        char16_t* text
    );
    static MakerEvent* CreateOnInstantZoneEvent(
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
    );
    static MakerEvent* CreateOnOlympiadFieldStepChangedEvent(
        l2npc::NpcMakerEx* maker, int eventId, int unused, int state, int inzoneTypeId, int inzoneId
    );
    static MakerEvent* CreateOnExtendedStartSiegeEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateOnDestructSupplyEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateOnDeclareDominionEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateStartChangeTimeEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateEndChangeTimeEvent(l2npc::NpcMakerEx* maker);
    static MakerEvent* CreateDebugMakerEvent(l2npc::NpcMakerEx& maker, int argument, CCreature& creature);
    static MakerEvent* CreateDynamicQuestEvent(l2npc::NpcMakerEx& maker, DynamicQuestType::NPCJobType jobType, char success);
    static MakerEvent* CreateDynamicQuestInfoEvent(l2npc::NpcMakerEx& maker, int questId, int eventId);
    static MakerEvent* CreatePCDieEvent(l2npc::NpcMakerEx& maker, CCreature& creature);
    static MakerEvent* CreateEventCampaignEvent(
        l2npc::NpcMakerEx& maker, EventCampaignType::NPCJobType jobType, char success
    );
    static MakerEvent* Create_ON_HUNTER_GUILD_BLESS_EVENT(l2npc::NpcMakerEx& maker, bool enabled);
};

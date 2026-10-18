#pragma once

#include "L2NPC/src/AdminRewardSystem.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOCs.h"
#include "Shared/src/ScriptAccessible.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Systems.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

namespace l2npc {
class NpcMakerEx;
}


class CodeInfoList;
class CSharedCreatureData;
class CSharedGeneralObjectData;
class CSharedPartyData;
class Position;
class RoomInfoList;
class CNPC;

namespace layout {

inline constexpr std::size_t kGlobalObjectSize = 0x1b0;

namespace globalObject {
inline constexpr std::size_t m_adminRewardSystem = 0x180;
} // namespace globalObject

} // namespace layout

// Exact spelling (including the historical "Stauts" typo) and values from
// the target PDB.
enum AreaDoorStautsType : std::int32_t {
    ADST_AREA_SETEVENT = 1,
    ADST_DOOR_SETEVENT = 2,
    ADST_MAX = 3,
    ADST_DOOR_REGISTER = -2,
    ADST_AREA_REGISTER = -1,
};

// Process-wide script service object recovered as GlobalObject in the PDB.
class GlobalObject : public ScriptAccessibleEx {
public:
    struct AreaStatusEventInfo {
        AreaStatusEventInfo() = default;
        AreaStatusEventInfo(int id, int onOff, int inzoneId, char16_t* name) :
            m_nId(id), m_bOnOff(onOff), m_nInZoneId(inzoneId), m_sName(name) {}

        int m_nId = 0;
        int m_bOnOff = 0;
        int m_nInZoneId = 0;
        char16_t* m_sName = nullptr;
    };

    struct DoorStatusEventInfo {
        DoorStatusEventInfo() = default;
        DoorStatusEventInfo(int id, int onOff, int inzoneId, char16_t* name) :
            m_nId(id), m_bOnOff(onOff), m_nInZoneId(inzoneId), m_sName(name) {}

        int m_nId = 0;
        int m_bOnOff = 0;
        int m_nInZoneId = 0;
        char16_t* m_sName = nullptr;
    };

    using EventTopRankerIdMap = std::map<int, int>;
    using RoomInfoListMap = std::map<std::u16string, RoomInfoList*>;
    using vAreaStatusType = std::vector<AreaStatusEventInfo>;
    using vDoorStatusType = std::vector<DoorStatusEventInfo>;

    GlobalObject();
    ~GlobalObject() override;

    void CollectFunc(ScriptTypeInfo* typeInfo) override;
    void CollectVar(ScriptTypeInfo* typeInfo) override;
    bool IsGlobalObjectInScript() const override;
    GlobalObject* CastGlobalObjectInScript() override;
    ScriptAccessibleEx* GetScriptAccessibleEx() const override;

    // GlobalObject.cpp::RegistCheckPCDie appends directly to this exact
    // observer vector. CServerSocket::UserObjectEvent iterates the same
    // member, preserving duplicate registrations and their order.
    void RegistCheckPCDie(l2npc::NpcMakerEx* maker);
    std::vector<l2npc::NpcMakerEx*> m_CheckPCDieMakerList;
    // Current executable fields at +0x28, +0x40 and +0x60. The adjacent
    // PDB supplies their original names and TimeValue element type. The
    // constructor at 0x004477d0 confirms their current initialization.
    int mEventValue[5];
    std::vector<TimeValue> mJackpotTimes;
    bool mJackpotTimeOver;

protected:
    EventTopRankerIdMap mEventTopRankerIds;
    int mServerTime;

public:
    // GlobalObject PDB +0x90/+0xb8. RoomInfoList pointers have the same
    // process-lifetime ownership as the target map. The map does not delete
    // them during GlobalObject teardown.
    bool m_HitSchduledEvent;
    CIOCriticalSection mRoomInfoLock;
    RoomInfoListMap mRoomInfoListMap;
    // Exact target members. The 2018 executable places these at +0x8c,
    // +0xd8, and +0x100 respectively. Target `long` is always 32-bit.
    // GetEventScheduledTime writes the public map directly.
    CIOCriticalSection m_EventLock;
    std::map<int, std::vector<std::int32_t>> m_EventScheduledTimeMap;
    CYieldLock m_AreaStatusLock;
    vAreaStatusType m_vAreaStatus;
    CYieldLock m_DoorStatusLock;
    vDoorStatusType m_vDoorStatus;
    AdminRewardSystem m_adminRewardSystem;

    int GetEventValue(int index);
    bool SetEventValue(int index, int value);
    void PrintVarInt(int, int, int, int, int, int, int, int);
    int GetDynamicQuestGoalInfo(int, int);
    int GetDynamicQuestLivePoint(int);
    int Superpoint_GetMoveType(const char16_t* superPointName, int npcClassId);
    void AddScriptLog(int level, char16_t* text);
    int GetDateTime(int timestamp, int field);
    int GetItemCollectable(CSharedCreatureData* creature);
    int HaveMemo(CSharedCreatureData* creature, int questId);
    int GetMemoState(CSharedCreatureData* creature, int questId);
    int GetMemoStateEx(CSharedCreatureData* creature, int questId, int slot);
    int HaveNRMemo(CSharedCreatureData* creature, int questId);
    int GetNRMemoState(CSharedCreatureData* creature, int questId);
    int GetNRMemoStateEx(CSharedCreatureData* creature, int questId, int slot);
    std::int64_t OwnItemCount(CSharedCreatureData* creature, int classId);
    std::int64_t OwnItemCount2(CSharedCreatureData* creature, int classId);
    int OwnItemEnchantCount(CSharedCreatureData* creature, int classId);
    int IsVariationItem(CSharedCreatureData* creature, int classId);
    int IsEnchantedItem(CSharedCreatureData* creature, int classId);
    bool IsParticipantInDynamicContent(CSharedCreatureData* creature);
    int GetElapsedTimeFromLastLogout(CSharedCreatureData* creature);
    int GetSex(CSharedCreatureData* creature);
    bool IsWeakenedDeathPenalty(CSharedCreatureData* creature);
    bool IsCreateDate(CSharedCreatureData* creature);
    bool CanGetBirthdayGift(CSharedCreatureData* creature);
    void SaveGetBirthdayGiftTime(CSharedCreatureData* creature);
    int GetBirthdayGiftCount(CSharedCreatureData* creature);
    bool CanGet5YearGift(CSharedCreatureData* creature);
    bool Is5Yeardate(CSharedCreatureData* creature);
    void SaveGet5YearGiftTimeCount(CSharedCreatureData* creature);
    int Get5YearGiftCount(CSharedCreatureData* creature);
    bool GetDailyQuestFlag(CSharedCreatureData* creature, int questId);
    void SetDailyQuestFlag(CSharedCreatureData* creature, int questId);
    bool GetWeeklyQuestFlag(CSharedCreatureData* creature, int questId);
    void SetWeeklyQuestFlag(CSharedCreatureData* creature, int questId);
    void Quest_GiveQuest(CSharedCreatureData* creature, int questId);
    void Quest_CompleteQuest(CSharedCreatureData* creature, int questId);
    void Quest_StartJournal(CSharedCreatureData* creature, int questId, int journalId);
    void Quest_CompleteJournal(CSharedCreatureData* creature, int questId, int journalId);
    CNPC* GetNPCFromID(int objectId);
    void Announce(char16_t* text);
    void AnnounceFStr(
        int messageId, char16_t* string1, char16_t* string2, char16_t* string3, char16_t* string4, char16_t* string5
    );
    void SendUIEvent(
        CSharedCreatureData* creature,
        int argument1,
        int argument2,
        int argument3,
        char16_t* string1,
        char16_t* string2,
        char16_t* string3,
        char16_t* string4,
        char16_t* string5,
        char16_t* string6
    );
    void SendUIEventFStr(
        CSharedCreatureData* creature,
        int argument1,
        int argument2,
        int argument3,
        char16_t* label1,
        char16_t* label2,
        char16_t* label3,
        char16_t* label4,
        char16_t* label5,
        int messageId,
        char16_t* string1,
        char16_t* string2,
        char16_t* string3,
        char16_t* string4,
        char16_t* string5
    );
    void BroadcastUIEventFStr(
        CSharedCreatureData* creature,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        char16_t* label1,
        char16_t* label2,
        char16_t* label3,
        char16_t* label4,
        char16_t* label5,
        int messageId,
        char16_t* string1,
        char16_t* string2,
        char16_t* string3,
        char16_t* string4,
        char16_t* string5
    );
    void BroadcastUIEventFStrInTerritory(
        int territoryId,
        char16_t* territoryName,
        int argument1,
        int argument2,
        int argument3,
        char16_t* label1,
        char16_t* label2,
        char16_t* label3,
        char16_t* label4,
        char16_t* label5,
        int messageId,
        char16_t* string1,
        char16_t* string2,
        char16_t* string3,
        char16_t* string4,
        char16_t* string5
    );
    void SendUSMEvent(CSharedCreatureData* creature, int eventId, int argument);
    void BroadcastUSMEvent(CSharedCreatureData* creature, int eventId, int argument1, int argument2);
    void ShowOnScreenMsg(
        CSharedCreatureData* creature,
        int messageId,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        int argument6,
        int argument7,
        int argument8
    );
    void ShowOnScreenMsgStr(
        CSharedCreatureData* creature,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        int argument6,
        int argument7,
        int argument8,
        char16_t* message
    );
    void ShowOnScreenMsgFStr(
        CSharedCreatureData* creature,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        int argument6,
        int argument7,
        int argument8,
        int argument9,
        const char16_t* string1,
        const char16_t* string2,
        const char16_t* string3,
        const char16_t* string4,
        const char16_t* string5
    );
    void BroadcastOnScreenMsg(
        CSharedCreatureData* creature,
        int range,
        int messageId,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        int argument6,
        int argument7,
        int argument8
    );
    void BroadcastOnScreenMsgStr(
        CSharedCreatureData* creature,
        int range,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        int argument6,
        int argument7,
        int argument8,
        char16_t* message
    );
    void BroadcastOnScreenMsgFStr(
        CSharedCreatureData* creature,
        int range,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        int argument6,
        int argument7,
        int argument8,
        int argument9,
        const char16_t* string1,
        const char16_t* string2,
        const char16_t* string3,
        const char16_t* string4,
        const char16_t* string5
    );
    void ShowMsgInTerritory(int territoryId, char16_t* territoryName, int messageId);
    void ShowMsgInTerritory2(int territoryId, char16_t* territoryName, char16_t* message);
    void ShowFStrMsgInTerritory2(
        int territoryId,
        char16_t* territoryName,
        int messageId,
        char16_t* string1,
        char16_t* string2,
        char16_t* string3,
        char16_t* string4,
        char16_t* string5
    );
    void InstantTeleportEx(CSharedCreatureData* target, int x, int y, int z, int heading);
    int IsInWater(CSharedCreatureData* creature);
    int IsNull(void* value);
    void SetAsNull(void* value);
    CSharedCreatureData* GetNullCreature();
    l2npc::NpcMakerEx* GetNullNpcMaker();
    std::int64_t FloatToInt(float value);
    float DistFromTo(CSharedCreatureData* from, CSharedCreatureData* to);
    int Skill_IsMagic(std::int64_t skillNameId);
    int Skill_IsHate(std::int64_t skillNameId);
    int Skill_GetAbnormalLevel(std::int64_t skillNameId);
    void SetSkillAll(CSharedCreatureData* creature);
    void SetSkillAllByEtcClass(CSharedCreatureData* creature, int etcClassId);
    int GetAbnormalLevel(CSharedCreatureData* creature, int abnormalType);
    int GetAbnormalMagicLevel(CSharedCreatureData* creature, int abnormalType);
    int GetIndexFromCreature(CSharedCreatureData* creature);
    std::int64_t StrToIntW(char16_t* value);
    float GetCos(float value);
    float GetSin(float value);
    float GetSqrt(float value);
    CodeInfoList* AllocCodeInfoList();
    int GetL2Time(int fieldType);
    int GetTimeOfDay();
    char16_t* IntToStr(std::int64_t value);
    char16_t* IntToFStr(std::int64_t value);
    char16_t* GetTimeAttackRecordInfo(int roomNo, int recordType, int fieldType, int memberIndex);
    int IsWinnerOfTimeAttackEvent(CSharedCreatureData* creature, int roomNo);
    int GetTimeAttackRewardFlag(CSharedCreatureData* creature, int roomNo);
    int GetStep_FieldCycle(int cycleId);
    int GetPoint_FieldCycle(int cycleId);
    bool AddPoint_FieldCycle(int cycleId, int side, int point, CSharedCreatureData* actor);
    bool AddPointWithoutActor_FieldCycle(int cycleId, int side, int point);
    bool SetStep_FieldCycle(int cycleId, int side, int step, CSharedCreatureData* actor);
    bool SetStepWithoutActor_FieldCycle(int cycleId, int side, int step);
    bool SetPoint_FieldCycle(int cycleId, int point);
    bool SetPointLock_FieldCycle(int cycleId, int lockFlag, int lockValue);
    int GetElapsedTimeAfterStepChanged_FieldCycle(int cycleId);
    int GetElapsedTimeAfterMissionStarted_FieldCycle(int cycleId);
    int GetMissionStatus_FieldCycle(int cycleId);
    bool SetResult_FieldCycle(int cycleId, int side, int result, CSharedCreatureData* actor);
    void BroadcastOnScreenMsgFStr_FieldCycle(
        int value1,
        int value2,
        int value3,
        int value4,
        int value5,
        int value6,
        int value7,
        int value8,
        int value9,
        int value10,
        const char16_t* string1,
        const char16_t* string2,
        const char16_t* string3,
        const char16_t* string4,
        const char16_t* string5
    );
    void BroadcastFieldEvent(CSharedCreatureData* creature, int eventId, int argument);
    char16_t* GetSubpledgeMasterName(CSharedCreatureData* creature, int pledgeType);
    int GetOwnResidenceType(CSharedCreatureData* creature);
    int Castle_GetDomainFortressContractStatus(int castleId);
    bool IsValidFortressId(int fortressId);
    int Fortress_GetOwnerPledgeId(int residenceId);
    int Fortress_GetParentCastleId(int fortressId);
    int Fortress_GetFacilityLevel(int fortressId, int facilityType);
    int Fortress_GetContractStatus(int fortressId);
    int Fortress_GetRentCost(int fortressId);
    int Fortress_GetNextRewardRemainTime(int fortressId);
    int Fortress_GetAvailableOwnMinutes(int fortressId);
    void SetDominionState(int dominionId, int state);
    int GetDominionState(int dominionId);
    int GetDominionWarState(int residenceId);
    int IsInstantAgitOwnerPledge(int pledgeId);
    void LoadEventScheduledTime(int eventId);
    bool IsEventScheduledTime(int eventId);
    int GetDBSavingMap(int key);
    void RegisterDBSavingMap(int key, int value);
    int UnregisterDBSavingMap(int key);
    void LoadDBSavingMap(CSharedCreatureData* target, int key);
    int IsSameString(char16_t* left, char16_t* right);
    void Area_SetOnOff(char16_t* areaName, int onOff);
    void Area_SetOnOffEx(char16_t* areaName, int onOff, int inzoneId);
    void Area_SetBannedTerritoryOnOff(char16_t* areaName, int onOff, int territoryId, int inzoneId);
    void Castle_GateOpenClose2(char16_t* doorName, int openClose);
    void Castle_GateOpenCloseEx(char16_t* doorName, int openClose, int inzoneId);
    int GetAreaStatusEx(char16_t* name, int inzoneId);
    int GetDoorStatusEx(char16_t* name, int inzoneId);
    bool Area_RegisterStatus(CSharedCreatureData* target, char16_t* name, int inzoneId);
    bool Door_RegisterStatus(CSharedCreatureData* target, char16_t* name, int inzoneId);
    bool Area_UnRegisterStatus(CSharedCreatureData* target, char16_t* name, int inzoneId);
    bool Door_UnRegisterStatus(CSharedCreatureData* target, char16_t* name, int inzoneId);
    void RegisterEventStatus(AreaDoorStautsType type, char16_t* name, int inzoneId, int status);
    // In this target revision the matching NS_GET_JACKPOT_TIME callback is
    // DummyPacket, so no inbound packet repopulates the vector.
    void LoadJackpotTime(int requestId);
    int IsJackpotTime(int unused);
    // PDB: NpcMakerEx* GlobalObject::GetNpcMaker(const wchar_t*).
    // The target wchar_t is 16-bit. Char16_t keeps that ABI width on
    // non-Windows hosts.
    l2npc::NpcMakerEx* GetNpcMaker(const char16_t* name);
    l2npc::NpcMakerEx* InstantZone_GetNpcMaker(int inzoneId, const char16_t* name);
    Position* GetTerritoryNpcPos(int inzoneId, const char16_t* makerName, int spawnDefinitionIndex);
    CSharedCreatureData* GetCreatureFromID(int id);
    CSharedCreatureData* GetCreatureFromIndex(int index);
    CSharedPartyData* GetParty(CSharedCreatureData* creature);
    CSharedPartyData* GetPartyFromID(int partyId);
    CSharedPartyData* GetPartyFromEventRoom(int roomNumber, int partType);
    int Party_GetCount(CSharedCreatureData* creature);
    CSharedCreatureData* Party_GetCreature(CSharedCreatureData* creature, int memberIndex);
    CSharedCreatureData* Party_GetLeader(CSharedCreatureData* creature);
    int DropItem3(CSharedCreatureData* creature, int itemClassId, std::int64_t count);
    void ChairGroup_AssignParty(CSharedCreatureData* creature, int chairGroupId);
    void ChairGroup_ClearParty(int chairGroupId1, int chairGroupId2, int chairGroupId3, int chairGroupId4);
    int AddPartyToEventRoom(int roomNumber, int partType, int partySid);
    int ClearEventRoom(int roomNumber, int partType, int parameter);
    char16_t* GetRank_RimKamaroka(int oneBasedRank, int fieldType);
    void SetPoint_RimKamaroka(CSharedCreatureData* creature, int type, int point);
    CSharedGeneralObjectData* GetMPCC(CSharedCreatureData* creature);
    int GetSSQPart(CSharedCreatureData* creature);
    void GotoServerWithPos(CSharedCreatureData* creature, int worldId, int x, int y, int z);
    void GotoServer(CSharedCreatureData* creature, int worldId);
    void ReturnToOriginServer(CSharedCreatureData* creature);
    bool IsRaidUser(CSharedCreatureData* creature);
    void IncreaseAP(CSharedCreatureData* creature, int amount);
    void ShowAPListWnd(CSharedCreatureData* creature);
    bool IsDormantUser(CSharedCreatureData* creature);
    bool IsNewbieUser(CSharedCreatureData* creature);
    bool IsDormantParty(CSharedPartyData* party);
    bool IsNewbieParty(CSharedPartyData* party);
    bool IsDormantOrNewbieParty(CSharedPartyData* party);
    int GetMasterIDFriendNPC(CSharedCreatureData* creature);
    bool SetMasterFriendNPC(CSharedCreatureData* master, CSharedCreatureData* npc);
    void AddPCSocial(int sharedCreatureIndex, int socialAction);
    void SendDominionScriptEvent(int eventId, int firstArgument, int secondArgument);
    void InzoneDoorBreakable(char16_t* doorName, int inzoneId);
    void SetNpcParam(CSharedCreatureData* creature, int parameterType, float value);
    void AddLog(int selector, CSharedCreatureData* creature, int argument);
    void AddLogEx(int selector, CSharedCreatureData* creature, std::int64_t argument1, std::int64_t argument2);
    void AddLogExWithoutCreature(int selector, int argument1, int argument2);
    void ShowCursedWeaponStatus(CSharedCreatureData* creature, int weaponId);
    void SetShillienMoon(int state);
    void SunRise(CSharedCreatureData* creature, int mode);
    void SetXMasEventState(int eventId, int state);
    void SetYearCatchEventState(int state);
    int StartBuffEvent(int eventType, int firstArgument, int secondArgument, int thirdArgument);
    int SetEventState(
        int value1,
        int value2,
        int value3,
        int value4,
        int value5,
        int value6,
        int value7,
        char16_t* string1,
        char16_t* string2
    );
    void AddDynamicQuestPoint(
        CSharedCreatureData* creature, int questId, int firstArgument, int secondArgument, int pointType, int thirdArgument
    );
    void AddEventCampaignPoint(
        CSharedCreatureData* creature,
        int campaignId,
        int firstArgument,
        int pointType,
        int secondArgument,
        int thirdArgument
    );
    int IsEventServer();
    int GetDynamicQuestProgress(int index);
    // GlobalObject::IsPreviousOwner reads SharedCastleInfo[residenceId]
    // and compares both the pledge database id and origin world id.
    bool IsPreviousOwner(CSharedCreatureData* creature, int residenceId);
    int GetPlayingUserAverage();
    int GetEventElapsedTime();
    int SetEventDebugInfo(int index, int value);
    int GetAgathionCount();
    int GetEventGoal();
    int GetOlympiadGameRule();
    int GetOlympiadRank(CSharedCreatureData* creature);
    void AddOlympiadPoint(CSharedCreatureData* creature, int point);
    void ResetOlympiadPoint(CSharedCreatureData* creature, int point);
    int GetLegendaryWorldID() const;
    const char16_t* GetWinnerPledgeNameLastSeason();
    const char16_t* GetWinnerPlayerNameLastSeason();
    void SetWinnerLastSeasonGotPrize();
    int GetPledgeGameTopRankerPointThisSeason();
    bool IsMemberOfWinnersPledgeLastSeason(CSharedCreatureData* creature);
    bool IsWinnerLastSeason(CSharedCreatureData* creature);
    bool IsWinnerLastSeasonAndDidNotGetPrize(CSharedCreatureData* creature);
    int CreateRoomInfoList(char16_t* name, int size);
    RoomInfoList* GetRoomInfoList(char16_t* name);

    int Rand(int upperExclusive);
    int RandOrPreset(int upperExclusive);
    Position* GetRandomPosInPos(int x, int y, int z, bool flying, int minimumDistance, int maximumDistance);
    Position* GetRandomPosInCreature(CSharedCreatureData* creature, int minimumDistance, int maximumDistance);
    Position* GetRandomPosInTerritory(char16_t* territoryName, bool floating, char16_t* npcName);
    void SendScriptEvent(CSharedCreatureData* target, int argument1, int argument2);
    void SendScriptEventEx(CSharedCreatureData* target, int argument1, int argument2, int argument3);
    void SendMakerScriptEvent(l2npc::NpcMakerEx* maker, int argument1, int argument2, int argument3);

    void SetInZoneFlag(int inzoneId, int flagId, int value);
    int GetInZoneFlag(int inzoneId, int flagId);
    bool GetDoorStatus(const char16_t* doorName);
};


// L2NPC.pdb: S_GDATA32 `gGlobalObj`, type GlobalObject.
// The executable constructs this single process object through
// `gGlobalObj$initializer$`. All recovered call sites use its fixed address
// (0x019ae840 in the current image).
extern GlobalObject gGlobalObj;

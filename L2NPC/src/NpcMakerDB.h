#pragma once

#include "Shared/src/MemorySub.h"

#include "NpcMaker.h"
#include "NpcMakerEx.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TerritoryInfo.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace l2npc {

using NpcPosEventName = std::u16string;
using NpcPosEventNameList = std::vector<NpcPosEventName>;


struct InstantZoneKey {
    int typeId = 0;
    int clusterId = 0;

    friend bool operator<(const InstantZoneKey& left, const InstantZoneKey& right) {
        return std::make_pair(left.typeId, left.clusterId) < std::make_pair(right.typeId, right.clusterId);
    }
};


// Exact NpcMakerDB.h record used by m_DomainTerritories. The pointer follows
// the 32-bit domain id at the naturally aligned +0x8 offset, giving the
// 0x10-byte stride observed in the shipping image.
struct DomainStructType {
    int m_nDomainID = 0;
    CTerritory* m_pTerritory = nullptr;
};


// NpcMakerDB.h owns this exact timer base and both concrete controller
// classes. Shipping L2NPC uses a 0x20-byte xvector ABI. The supplied donor
// PDB describes an older, larger build. Portable members use std::vector.
class CNpcPosEvent : public CIOObject {
public:
    enum {
        TOTAL_TICK = 720000,
        DEFAULT_TICK = 900,
    };

    CNpcPosEvent();
    ~CNpcPosEvent() override;

    void StartTimer();
    int GetNpcPosIndex() const;

protected:
    int mNpcPosIndex = 0;
    int mTotalMakerSize = 0;
    int mTick = 0;
    int mStartTick = 0;
};

class NpcPosEventController : public CNpcPosEvent {
public:
    void AddNpcPosEvent(std::vector<CNpcMaker*>& npcMakers, int enabled);
    void TimerExpired(int timerId) override;

private:
    struct NpcPosData {
        NpcPosData(std::vector<CNpcMaker*>& makerList, int enabled);

        int mMakerIndex = 0;
        int mIsEnabled = 0;
        std::vector<CNpcMaker*> mMakerList;
    };

    std::vector<NpcPosData> m_NpcPosDatas;
};

class NpcPosExEventController : public CNpcPosEvent {
public:
    void AddNpcPosExEvent(std::vector<NpcMakerEx*>& npcMakers, int enabled);
    void TimerExpired(int timerId) override;

private:
    struct NpcPosDataEx {
        NpcPosDataEx(std::vector<NpcMakerEx*>& makerList, int enabled);

        int mMakerIndex = 0;
        int mIsEnabled = 0;
        std::vector<NpcMakerEx*> mMakerList;
    };

    // This is a distinct private field in the shipping class. It shadows
    // CNpcPosEvent::mNpcPosIndex and is the index used by the Ex callback.
    int mNpcPosIndex = 0;
    std::vector<NpcPosDataEx> m_NpcPosDataExs;
};


// Ownership and registration state recovered from CNpcMakerDB
// (NpcMakerDB.cpp). The target inherits CIOObject privately and exposes its
// singleton operations publicly.
class CNpcMakerDB : private CIOObject {
public:
    CNpcMakerDB();
    ~CNpcMakerDB() override;

    L2_DECLARE_OBJECT_TYPE(CNpcMakerDB)

    static CNpcMakerDB& Inst();
    bool Load(const wchar_t* fileName);

    // Exact nested record used both by the (type, cluster) template database
    // and by the concrete in-zone-id instance database.
    struct NpcMakerExEventInfo {
        int nEventId = 0;
        int nEnable = 0;
        NpcMakerEx* pMakerEx = nullptr;
    };

    // Exact nested CNpcMakerDB record from NpcMakerDB.h.
    struct LoadingProcessFlag {
        bool bIsFinished = false;
    };

    // Source aliases recorded in NpcMakerDB.pdb. Their old PDB extents are
    // stale, while the current executable fixes every xstd owner below at
    // 0x20 bytes.
    using SiegeEventKeyType = std::pair<int, SiegeDefendType>;
    using SiegeReleatedMakerMapType = std::map<SiegeEventKeyType, std::vector<CNpcMaker*>>;
    using OlympiadFieldEventMakerMapType = std::map<int, NpcMakerEx*>;
    using NpcMakerExEnableType = std::pair<NpcMakerEx*, int>;
    using NpcMakerExEnableListType = std::vector<NpcMakerExEnableType>;
    using FortressEventKeyType = std::pair<int, int>;
    using FortressEventMakerExMapType = std::map<FortressEventKeyType, NpcMakerExEnableListType>;
    using NpcMakerExEventInfoListType = std::vector<NpcMakerExEventInfo>;
    using InstantZoneTypeKey = std::pair<int, int>;
    using InzoneTypeMakerExMapType = std::map<InstantZoneTypeKey, NpcMakerExEventInfoListType>;
    using InstantZoneEventMakerExMapType = std::map<int, NpcMakerExEventInfoListType>;
    // Windows wchar_t is represented by char16_t at the portable ABI
    // boundary. The target stores these script-literal pointers unchanged.
    using DoorNameListType = std::vector<const char16_t*>;
    using InstantZoneDoorEventMakerExDBMapType = std::map<NpcMakerEx*, DoorNameListType>;
    using InstantZoneDoorEventMakerExMapType =
        std::map<int, std::map<std::u16string, std::vector<NpcMakerEx*>, runtime::CStringLess<std::u16string>>>;
    using InzoneFlagKey = std::pair<int, int>;
    using InzoneFlagMap = std::map<InzoneFlagKey, int>;

    // NpcMakerDB.cpp::CheckMutex probes the process-wide protection object
    // published by L2Server during shared-world initialization. The method
    // is kept on the recovered owner class instead of hiding the startup
    // contract in the portable host.
    bool CheckMutex();

    // Exact CNpcMakerDB methods and owner field. GlobalObject merely forwards
    // its two script natives to this process-wide database in the target.
    void AddSiegeRelatedCount();
    bool IsSiegeRelatedCompleted() const;
    int GetInZoneMapFlag(int inzoneId, int flagId);
    void SetInZoneMapFlag(int inzoneId, int flagId, int value);
    void RegisterNpcPosEvent(const char16_t* eventName, CNpcMaker* maker);
    int RegisterNpcPosEvent(const char16_t* eventName, NpcMakerEx* maker);
    int RegisterSiegeEventEx(int eventId, NpcMakerEx* maker);
    int RegisterAgitSiegeEventEx(int eventId, NpcMakerEx* maker);
    void RegisterSiegeEvent(CNpcMaker* maker);
    void RegisterAgitAttackEvent(int agitId, CNpcMaker* maker);
    void RegisterAgitDefendEvent(int agitId, CNpcMaker* maker);
    void RegisterAgitTB_BattleRoyalEvent(int agitId, CNpcMaker* maker);
    void RegisterAgitTB_FinalEvent(int agitId, CNpcMaker* maker);
    void RegisterDoorEvent(const char16_t* doorName, CNpcMaker* maker);
    int RegisterDoorEvent(const char16_t* doorName, int inzoneType, NpcMakerEx* maker);
    void RegisterFieldCycleEvent(unsigned int eventId, NpcMakerEx* maker);
    void RegisterOlympiadFieldEvent(int eventId, NpcMakerEx* maker);
    void RegisterPledgeGameFieldEvent(int eventId, NpcMakerEx* maker);
    void RegisterFortressEvent(int residenceId, int eventId, int spawn, NpcMakerEx* maker);
    void RegisterInstantZoneEvent(int inzoneType, int clusterId, int eventId, int spawn, NpcMakerEx* maker);
    void RegisterHunterGuildBless(NpcMakerEx* maker);
    void RegisterInstantZoneNotifySubscriber(InstantZoneKey key, NpcMakerEx* maker);
    void UnregisterInstantZoneNotifySubscriber(InstantZoneKey key, NpcMakerEx* maker);
    void NotifyToInstantZoneMakerSubscriber(
        InstantZoneKey key, std::uint32_t sourceInzoneId, int eventId, int argument1, int argument2, int argument3
    );
    void DoorEvent(const char16_t* doorName, int enabled, int inzoneId);
    void AgitEvent(int agitId, std::u16string agitName, int enabled, int variant, bool databaseRespawn);
    void SiegeEvent(int residenceId, int event, bool databaseRespawn);
    void RegisterNpcMakerWorkingSiegeEvent(int eventId, std::vector<CNpcMaker*>& makers);
    void ClearNpcMakerWorkingSiegeEvent(int eventId);
    void TB_StartBatleRoyal(
        int eventId, const char16_t* eventName, const std::map<int, std::pair<int, int>>& teams, bool databaseRespawn
    );
    void TB_StartFinal(
        int eventId, const char16_t* eventName, const std::map<int, std::pair<int, int>>& teams, bool databaseRespawn
    );
    void TB_EndBattle(int eventId, const char16_t* eventName);
    bool GetDoorStatus(const char16_t* doorName);
    void RegisterAsOlympiadOperator(int npcSid);
    void RegisterAsPledgeGameOperator(int npcSid);
    void RegisterAsFieldCycleManager(int npcSid);
    void RegisterAsWorldChatOperator(int npcSid);
    CNPC* GetOlympiadOperatorBase();
    CNPC* GetPledgeGameOperatorBase();
    CNPC* GetWorldChatOperatorBase() const;
    NpcMakerEx* GetInstantZoneMakerEx(int inzoneId, const char16_t* makerName, bool checkName);
    void OlympiadFieldEvent(int zeroBasedFieldIndex, int matchType, int state);
    void FieldCycleStepExpired(int cycleId, int eventId, int step);
    void FortressEvent(int residenceId, int eventId, int argument0, int argument1, int argument2, char16_t* text);
    void InstantZoneEvent(
        int inzoneTypeId, int clusterId, int inzoneId, int eventId, int argument0, const std::uint8_t* packetTail
    );
    void OlympiadInZoneMakerCreate(int inzoneTypeId, int inzoneId);
    void PledgeGameInZoneMakerCreate(int inzoneTypeId, int inzoneId);
    void OnNpcPosEvent(char16_t* eventName, bool enabled);
    void OnNpcPosesEvent(const NpcPosEventNameList& despawn, const NpcPosEventNameList& spawn);
    void FieldCycleEvent(int fieldId, int step);
    void OnHunterGuildBless(bool enabled);

    void CreateAllNPC();
    void FastCreateAllNPC(int threadId);
    void WaitForOtherNPCCreator(bool exPhase);
    void WaitForL2ServerPreparation();
    void TimerExpired(int timerId) override;
    void OnMakerFinished();
    void InitDynamicQuestMaker();
    void InitEventCampaignMaker();
    void AddNpcMaker(CNpcMaker* maker);
    CNpcMaker* GetNpcMaker(int index);
    int GetNpcMakerSize();
    bool AddNpcMakerEx(NpcMakerEx* maker);
    NpcMakerEx* FindNpcMakerEx(const std::u16string& name) const;
    CTerritory* GetTerritory(const std::u16string& name) const;
    std::map<std::u16string, CTerritory*, runtime::CStringLess<std::u16string>>* GetTerritoryMap();
    void InsertTerritory(CTerritory* territory);
    void AddDomainTerritory(int domainId, CTerritory* territory);
    int FindDomain(const FVector& position);
    bool ProcessOneNpcMaker();
    bool ProcessOneNpcMaker(int index);
    bool ProcessOneNpcMakerEx();
    bool ProcessOneNpcMakerEx(int index);

private:
    bool IsSpawnPossible(CNpcMaker* maker);
    bool IsSpawnPossible(NpcMakerEx* maker);
    void _CreateInstantZoneMakerEx(int inzoneId, NpcMakerExEventInfoListType& templates);
    void _DeleteInstantZoneMakerEx(int inzoneId);
    void _SetInstantZoneMapFlag(int inzoneId, const std::uint8_t* packet);
    void RegisterInstantZoneDoorEventMakerEx(int inzoneId, NpcMakerEx* source, NpcMakerEx* instance);

    // Declaration order and historical offsets below follow current
    // L2NPC.exe 0x004f6a24/0x004f5cfc. The source container names come from
    // the PDB. Portable members own standard maps and vectors directly.
    // their host offsets need not match those shipping offsets.

    // +0x018, +0x038: extended-maker name index and observer list.
    std::map<std::u16string, NpcMakerEx*, runtime::CStringLess<std::u16string>> mNpcMakerExMap;
    std::vector<NpcMakerEx*> mNpcMakerExList;

    // +0x058: registry populated by parser_spawn before maker registration.
    std::map<std::u16string, CTerritory*, runtime::CStringLess<std::u16string>> m_TerritoryMap;

    // +0x078: exact raw observer registry from NpcMakerDB.h.
    std::vector<CNpcMaker*> m_NpcMakers;

    // +0x098, +0x0b8: regular siege subscriptions and active makers.
    SiegeReleatedMakerMapType m_NpcMakersReceivingSiegeEvent;
    std::multimap<int, CNpcMaker*> m_NpcMakersWorkingSiegeEventMap;

    // +0x0d8, immediately after the working-maker map. Callbacks are made
    // only after this lock has been released.
    CIOCriticalSection m_NpcMakersWorkingSiegeEventMapSect;

    // +0x100 .. +0x218: agit locks and subscriber maps.
    CIOCriticalSection m_AgitAttackEventSect;
    CIOCriticalSection m_AgitDefendEventSect;
    std::multimap<int, CNpcMaker*> m_NpcMakerReceivingAgitAttackEvent;
    std::multimap<int, CNpcMaker*> m_NpcMakerReceivingAgitDefendEvent;
    CIOCriticalSection m_AgitTBBattleRoyalEventSect;
    CIOCriticalSection m_AgitTBFinalEventSect;
    std::multimap<std::pair<int, int>, CNpcMaker*> m_NpcMakerReceivingAgitTB_BattleRoyalEvent;
    std::multimap<std::pair<int, int>, CNpcMaker*> m_NpcMakerReceivingAgitTB_FinalEvent;

    // +0x220.
    std::vector<DomainStructType> m_DomainTerritories;

    // +0x240 .. +0x254. PDB type 0x1F8A is `volatile long` for all six
    // fields. The current EXE combines plain volatile MOVs with explicit
    // Interlocked operations. Portable/RawAtomic keeps that distinction
    // while keeping MSVC's 32-bit-long layout on LP64 hosts.
    volatile std::int32_t m_nMakerProcessed;
    volatile std::int32_t m_nSiegeRelatedProcessed;
    volatile std::int32_t m_nTotalSiegeRelated;
    volatile std::int32_t m_nMakerDoneCount;
    volatile std::int32_t m_nMakerExDoneCount;
    volatile std::int32_t m_MakerFinished;

    // +0x258. Receiver registration and live receiver traversal use the same
    // recursive CYieldLock.
    CYieldLock m_ReceiverLock;

    // +0x268 .. +0x2e7.
    std::map<std::u16string, std::vector<CNpcMaker*>, runtime::CStringLess<std::u16string>> m_NpcMakerReceivingDoorEvent;
    std::map<std::u16string, std::vector<CNpcMaker*>, runtime::CStringLess<std::u16string>> m_NpcMakerReceivingNpcPosEvent;
    std::map<std::u16string, std::vector<NpcMakerEx*>, runtime::CStringLess<std::u16string>> mNpcMakerExReceivingDoorEvent;
    std::map<std::u16string, std::vector<NpcMakerEx*>, runtime::CStringLess<std::u16string>> mNpcMakerExReceivingNpcPosEvent;

    // +0x2e8 .. +0x397.
    CIOCriticalSection m_ExSiegeEventSect;
    CIOCriticalSection m_ExAgitSiegeEventSect;
    std::multimap<int, NpcMakerEx*> mNpcMakerExSiegeEvent;
    std::multimap<int, NpcMakerEx*> mNpcMakerExAgitSiegeEvent;
    std::multimap<int, NpcMakerEx*> mNpcMakerExFieldCycleEvent;

    // +0x398. RegisterAsFieldCycleManager appends without deduplication.
    std::vector<int> m_npcFieldCycleManager;

    // +0x3b8, +0x3d8.
    OlympiadFieldEventMakerMapType m_npcMakerExOlympiadFieldEvent;
    std::vector<int> m_npcOlympiadManager;

    // +0x3f8, +0x418, +0x438.
    std::map<int, NpcMakerEx*> m_npcMakerExPledgeGameFieldEvent;
    std::vector<int> m_npcPledgeGameManager;
    std::vector<int> m_npcWorldChatManager;

    // +0x458.
    FortressEventMakerExMapType m_NpcMakerExReceivingFortressEvent;

    // +0x478 .. +0x4c7.
    CYieldLock m_InZoneMakerLock;
    InzoneTypeMakerExMapType m_NpcMakerExReceivingInZoneEventDB;
    InstantZoneEventMakerExMapType m_NpcMakerExReceivingInZoneEvent;

    // +0x4c8 .. +0x517. The source keeps wchar_t* script-literal pointers.
    // char16_t keeps the Windows code-unit width on every host.
    CYieldLock m_InZoneDoorLock;
    InstantZoneDoorEventMakerExDBMapType m_NpcMakerExReceivingInZoneDoorEventDB;
    InstantZoneDoorEventMakerExMapType m_NpcMakerExReceivingInZoneDoorEvent;

    // +0x518. SiegeEvent keeps this outer critical
    // section across the regular-maker path and every NpcMakerEx callback.
    CIOCriticalSection m_NpcMakerSiegeEventSect;

    // +0x540. Protected with m_InZoneMakerLock together with the in-zone
    // maker registries.
    InzoneFlagMap m_InzoneMapFlag;

    // +0x560. Set only after every regular and extended maker has passed
    // startup processing, matching CNpcMakerDB::OnMakerFinished.
public:
    LoadingProcessFlag m_stLoadingProcess;

private:
    // +0x568, +0x588: fields added after the PDB build.
    std::map<InstantZoneKey, std::set<NpcMakerEx*>> m_mapInstantZoneNotify;

    // Append-only. Duplicates produce duplicate callbacks.
    // Check-PC-die belongs to GlobalObject::m_CheckPCDieMakerList.
    std::vector<NpcMakerEx*> m_npcHunterGuildBlessMakers;
};


extern CNpcMakerDB g_NpcMakerDB;

} // namespace l2npc

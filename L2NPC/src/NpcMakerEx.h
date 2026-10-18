#pragma once

#include "Shared/src/MemorySub.h"

#include "ParamProperty.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "ScriptTypeInfo.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Obj.h"
#include "Shared/src/ScriptAccessible.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TerritoryInfo.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace DynamicQuestType {
enum NPCJobType : int;
}

namespace EventCampaignType {
enum NPCJobType : int;
}


class CCreature;
class CReloadableNPCEventHandler;
class CSharedCreatureData;
class GlobalObject;
class CNPC;
struct MakerEvent;
class NpcSpawnDefineEx;

namespace layout {

// The exact 2018 image uses the newer four-word xvector and allocates both
// ItemIndexList and its state-free IntList subclass as 0x40-byte objects.
// The adjacent 2015 PDB record is 0x38 bytes because its xvector has only
// three pointer-sized words.
inline constexpr std::size_t kItemIndexListSize = 0x40;
inline constexpr std::size_t kIntListSize = kItemIndexListSize;

} // namespace layout

// NpcMakerEx.h lines 14-48 in the matching source image. Its ordinary data
// operations are compiled by NPC.cpp. The script registration methods below
// are inline members of this header. Current L2NPC.exe fixes the newer vector
// extent and the offsets kept here.
class ItemIndexList : public ScriptAccessibleEx {
public:
    ItemIndexList();
    ~ItemIndexList() override;

    ItemIndexList(const ItemIndexList&) = delete;
    ItemIndexList& operator=(const ItemIndexList&) = delete;
    ItemIndexList(ItemIndexList&&) = delete;
    ItemIndexList& operator=(ItemIndexList&&) = delete;

    void CollectFunc(ScriptTypeInfo* typeInfo) override {
        ScriptTypeInfo& intType = GetBuiltinScriptType(ScriptTypeId::Int);
        typeInfo->AddFunction(L"GetSize", &ItemIndexList::GetSize, &intType, {});
        typeInfo->AddFunction(L"GetItemIndex", &ItemIndexList::GetItemIndexByArrayIndex, &intType, {&intType});
    }

    ScriptAccessibleEx* GetScriptAccessibleEx() const override { return const_cast<ItemIndexList*>(this); }

    void AddItem(std::int64_t value);
    void RemoveItem(std::int64_t value);
    void Clear();
    int GetItemIndexByArrayIndex(int index);
    int GetItemIndexWithMaxValue();
    int GetItemIndexWithMinValue();
    int GetSize();

private:
    int m_nMaxSize;
    std::vector<std::int64_t> m_ItemIndexList;
    CYieldLock m_lock;
};

// NpcMakerEx.h lines 50-68. The subclass adds no state and only replaces the
// inline native-function table.
class IntList : public ItemIndexList {
public:
    void CollectFunc(ScriptTypeInfo* typeInfo) override {
        ScriptTypeInfo& voidType = GetBuiltinScriptType(ScriptTypeId::Void);
        ScriptTypeInfo& intType = GetBuiltinScriptType(ScriptTypeId::Int);
        ScriptTypeInfo& int64Type = GetBuiltinScriptType(ScriptTypeId::Int64);
        typeInfo->AddFunction(L"Add", &ItemIndexList::AddItem, &voidType, {&int64Type});
        typeInfo->AddFunction(L"Remove", &ItemIndexList::RemoveItem, &voidType, {&int64Type});
        typeInfo->AddFunction(L"Clear", &ItemIndexList::Clear, &voidType, {});
        typeInfo->AddFunction(L"Get", &ItemIndexList::GetItemIndexByArrayIndex, &intType, {&intType});
        typeInfo->AddFunction(L"GetMax", &ItemIndexList::GetItemIndexWithMaxValue, &intType, {});
        typeInfo->AddFunction(L"GetMin", &ItemIndexList::GetItemIndexWithMinValue, &intType, {});
        typeInfo->AddFunction(L"GetSize", &ItemIndexList::GetSize, &intType, {});
        typeInfo->AddFunction(L"GetItemIndex", &ItemIndexList::GetItemIndexByArrayIndex, &intType, {&intType});
    }
};


// npcmakerex.h:97-149 in the matching PDB. This is a distinct script class,
// not an alias for NPC.h's AtomicValue, although both have the same physical
// layout and machine-level operations.
class AtomicValueMaker : public ScriptAccessibleEx {
public:
    AtomicValueMaker() = default;
    ~AtomicValueMaker() override = default;

    int Increment(int amount) { return runtime::raw_atomic::AddI32(&mValue, static_cast<std::int32_t>(amount)); }

    int Decrement(int amount) {
        const std::uint32_t bits = 0U - static_cast<std::uint32_t>(amount);
        std::int32_t delta = 0;
        std::memcpy(&delta, &bits, sizeof(delta));
        return runtime::raw_atomic::AddI32(&mValue, delta);
    }

    int Exchange(int value) { return runtime::raw_atomic::ExchangeI32(&mValue, static_cast<std::int32_t>(value)); }

    int CompareExchange(int exchange, int comparand) {
        return runtime::raw_atomic::CompareExchangeI32(
            &mValue, static_cast<std::int32_t>(exchange), static_cast<std::int32_t>(comparand)
        );
    }

    int GetValue() { return runtime::raw_atomic::LoadI32Relaxed(&mValue); }

    void CollectFunc(ScriptTypeInfo* typeInfo) override {
        // npcmakerex.h:130-136. Current executable 0x004fc0d4.
        ScriptTypeInfo& intType = GetBuiltinScriptType(ScriptTypeId::Int);
        typeInfo->AddFunction(L"Increment", &AtomicValueMaker::Increment, &intType, {&intType});
        typeInfo->AddFunction(L"Decrement", &AtomicValueMaker::Decrement, &intType, {&intType});
        typeInfo->AddFunction(L"Exchange", &AtomicValueMaker::Exchange, &intType, {&intType});
        typeInfo->AddFunction(L"GetValue", &AtomicValueMaker::GetValue, &intType, {});
        typeInfo->AddFunction(L"CompareExchange", &AtomicValueMaker::CompareExchange, &intType, {&intType, &intType});
    }

    void CollectVar(ScriptTypeInfo* typeInfo) override {
        // npcmakerex.h:141-143. MValue is the sole field at +0x08.
        DeclareScriptVariable(typeInfo, L"value", 0x08, mValue);
    }

    ScriptAccessibleEx* GetScriptAccessibleEx() const override { return const_cast<AtomicValueMaker*>(this); }

    volatile std::int32_t mValue = 0;
};


namespace l2npc {

class NpcMakerEx;

// Original NpcMakerEx.h type. The PDB exposes all three payload fields and
// NpcMakerEx's respawn queue reads them directly.
class NpcRespawnInfo final : public ::MemoryObject {
public:
    NpcRespawnInfo(std::uint32_t availableTime, int spawnCount, NpcSpawnDefineEx* spawnDefine);
    ~NpcRespawnInfo() override;

    L2_DECLARE_MEMORY_ALLOCATION(NpcRespawnInfo)

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool operator<(const NpcRespawnInfo& other) const;

    std::uint32_t m_nAvailableTime;
    int m_nSpawnCount;
    NpcSpawnDefineEx* m_pSpawnDefine;
};

// NpcMakerEx.cpp:129-162. The target timer owns this CIOObject for 30 seconds
// while it holds one ORT_VERIFIER_RETAIN reference to the deleting maker.
class CNpcMakerExReleaseVerifier final : public CIOObject {
public:
    CNpcMakerExReleaseVerifier(NpcMakerEx* maker);

    L2_DECLARE_MEMORY_ALLOCATION(CNpcMakerExReleaseVerifier)

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void TimerExpired(int timerId) override;

private:
    NpcMakerEx* m_pNpcMakerEx = nullptr;
};


// Script-driven maker recovered as NpcMakerEx in NpcMakerEx.cpp. It is kept
// separate from CNpcMaker because the target uses different startup, event
// and spawn-definition lifecycles for the two classes.
class NpcMakerEx : public CIOObject, public ScriptAccessibleEx {
public:
    class __async_arg_AsyncTimerExpired;

    NpcMakerEx();
    NpcMakerEx(const NpcMakerEx& other);
    ~NpcMakerEx() override;

    L2_DECLARE_MEMORY_ALLOCATION(NpcMakerEx)

    NpcMakerEx* Copy() {
        // PDB type 0x25D0 declares Copy inline. The current EXE's inlined
        // call site performs a nullable allocation followed by this copy
        // constructor, so keep the target's allocation-failure result.
        return new NpcMakerEx(*this);
    }

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    void CollectFunc(ScriptTypeInfo* typeInfo) override;
    void CollectVar(ScriptTypeInfo* typeInfo) override;
    bool IsNPCMakerExInScript() const override;
    NpcMakerEx* CastNPCMakerExInScript() override;
    ScriptAccessibleEx* GetScriptAccessibleEx() const override;

    void SetName(const char16_t* value);
    const char16_t* GetName() const noexcept;
    void SetMaximumNPC(int maximumNpc);
    int GetMaximumNPC() const noexcept;
    int GetDefineCount() const noexcept;
    int GetTotalNPC() const noexcept;
    void SetEnabled(int value);
    int GetEnabled() const noexcept;
    void SetDomainID(int domainId);
    int GetDomainID() const noexcept;
    void SetFlying(bool flying);
    bool IsFlying() const noexcept;
    bool IsDeleting() const noexcept;
    int GetInZoneDeleteCount() const noexcept;
    int IncreaseInZoneDelete();
    unsigned int GetInZoneID() const noexcept;
    unsigned int GetInZoneClassID() const noexcept;
    unsigned int GetInZoneClusterID() const noexcept;
    void SetInZoneID(unsigned int inzoneId);
    void SetInZoneType(unsigned int classId, unsigned int clusterId);
    int CreateMakerKey();
    bool RegisterNpc(CNPC* npc);
    void UnregisterNpc(CNPC* npc);
    CSharedCreatureData* GetNpcFromKey(int key) const;
    TerritoryInfo* GetTerritoryInfo() noexcept;
    const TerritoryInfo* GetTerritoryInfo() const noexcept;
    void AddBannedTerritory(CTerritory* territory);
    void AddTerritory(CTerritory* territory);
    void SetAI(const char16_t* className);
    bool SetParameter(const char16_t* name, const char16_t* value);
    void SetAIParam(const CAIParameter& parameter);
    bool InitializeRandomPos();

    void AddTimerEx(int timerId, int milliseconds);
    void __async_AsyncTimerExpired(CExecutionArgumentPad* argument);
    void AddSpawnDefineEx(NpcSpawnDefineEx* spawnDefine);
    NpcSpawnDefineEx* GetSpawnDefine(int index) const;
    NpcSpawnDefineEx* GetSpawnDefineByNick(const char16_t* nickname) const;
    void RegisterRespawn(int delaySeconds, int spawnCount, NpcSpawnDefineEx* spawnDefine);
    void ResetRespawn();
    void DoRespawn();
    int AtomicIncreaseTotal(NpcSpawnDefineEx* spawnDefine, int amount, int updateSpawnDefine);
    int AtomicDecreaseTotal(NpcSpawnDefineEx* spawnDefine, int amount, int updateSpawnDefine);
    void TimerExpired(int timerId) override;
    void Delete();
    int HandleEvent(MakerEvent* event);
    void ReportMakerClassHierarchy(int builderObjectId);
    void AgitEvent(int enabled, bool databaseRespawn);
    void FieldCycleEvent(int eventId, int state);
    void OlympiadFieldEvent(int fieldId, int step, int state);
    void OnFortressEvent(
        int residenceId, int eventId, int reply, int argument0, int argument1, int argument2, char16_t* text
    );
    void OnInstantZoneEvent(
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
    void OnDoorEvent(int enabled);
    void OnStartSiegeEvent();
    void OnEndSiegeEvent();
    void OnProclaimSiegeEvent();
    void OnDestructCTRLTowerEvent();
    void OnCancelSiegeEvent();
    void OnExtendedStartSiegeEvent();
    void OnDestructSupplyEvent();
    void OnDeclareDominionEvent();
    void OnStartChangeTimeEvent();
    void OnEndChangeTimeEvent();
    void OnStartTeamBattleAgitFinalEvent();
    void OnDynamicQuestEvent(DynamicQuestType::NPCJobType jobType, char success);
    void OnDynamicQuestInfo(int questId, int eventId);
    void OnPCDieEvent(CCreature& creature);
    void OnEventCampaignEvent(EventCampaignType::NPCJobType jobType, char success);
    void OnNpcPosEvent(int enabled);
    void OnHunterGuildBless(bool enabled);
    int RegisterNpcPosEvent(const char16_t* eventName);
    int RegisterSiegeEventEx(int eventId);
    int RegisterAgitSiegeEventEx(int eventId);
    int RegisterDoorEvent(const char16_t* doorName, int inzoneType);
    void RegisterFieldCycleEventEx(int eventId);
    void RegisterOlympiadFieldEventEx();
    void RegisterOlympiadZoneEventEx(int inzoneType);
    int Residence_GetCastleType(int residenceId) const;
    void RegisterPledgeGameFieldEventEx();
    void RegisterPledgeGameZoneEventEx(int inzoneType);
    void RegistCheckPCDie();
    void RegisterHunterGuildBless();
    void RegisterFortressEventEx(int residenceId, int eventId, int spawn);
    void RegisterInstantZoneEventEx(int inzoneType, int clusterId, int eventId, int spawn);
    void SetDefineExInZoneID(unsigned int inzoneId);
    void RegisterInstantZoneNotifySubscriber();
    void UnregisterInstantZoneNotifySubscriber();
    void NotifyToInstantZoneMaker(int eventId, int argument1, int argument2, int argument3);

    // The adjacent PDB supplies the original declarations and names. Current
    // L2NPC.exe constructors/destructors fix every offset below and explain
    // the complete growth from the donor's 0x1510 to the shipping 0x1580.
    ::RefHelpers m_RefHelpers;
    bool m_bDeleting;
    std::uint8_t m_nameAlignment;
    std::array<char16_t, 49> mName;
    int mDefineCount;
    volatile std::int32_t mNpcCount;
    int mMaximumNPC;
    std::int64_t i_quest0;
    std::int64_t i_quest1;
    std::int64_t i_quest2;
    std::int64_t i_quest3;
    std::int64_t i_quest4;
    std::int64_t i_quest5;
    std::int64_t i_quest6;
    std::int64_t i_quest7;
    std::int64_t i_quest8;
    std::int64_t i_quest9;
    int i_ai0;
    int i_ai1;
    int i_ai2;
    int i_ai3;
    int i_ai4;
    int i_ai5;
    int i_ai6;
    int i_ai7;
    int i_ai8;
    int i_ai9;
    CSharedCreatureData* c_ai0;
    CSharedCreatureData* c_ai1;
    CSharedCreatureData* c_ai2;
    CSharedCreatureData* c_ai3;
    CSharedCreatureData* c_ai4;
    IntList mIntList;
    IntList* m_IntList;
    int enabled;
    int mDomainID;
    AtomicValueMaker mAvAI0;
    AtomicValueMaker* av_ai0;
    AtomicValueMaker mAvAI1;
    AtomicValueMaker* av_ai1;
    AtomicValueMaker mAvQuest0;
    AtomicValueMaker* av_quest0;
    AtomicValueMaker mAvQuest1;
    AtomicValueMaker* av_quest1;
    AtomicValueMaker mAvQuest2;
    AtomicValueMaker mAvQuest3;
    AtomicValueMaker mAvQuest4;
    AtomicValueMaker mAvQuest5;
    AtomicValueMaker mAvAI2;
    AtomicValueMaker mAvAI3;
    AtomicValueMaker mAvAI4;
    AtomicValueMaker mAvAI5;
    AtomicValueMaker* av_quest2;
    AtomicValueMaker* av_quest3;
    AtomicValueMaker* av_quest4;
    AtomicValueMaker* av_quest5;
    AtomicValueMaker* av_ai2;
    AtomicValueMaker* av_ai3;
    AtomicValueMaker* av_ai4;
    AtomicValueMaker* av_ai5;
    int mMaxTotalOfSpawnDefineEx;
    bool m_bFlying;
    std::array<std::uint8_t, 3> m_flyingAlignment;
    // PDB type 0x285f is `volatile long`, not std::atomic<long>. The
    // Interlocked operations are supplied at the individual call sites.
    volatile std::int32_t m_nInZoneDelete;
    std::uint32_t m_serializerAlignment;
    CExecutionSerializer<NpcMakerEx, 32> Serializer;
    CIOCriticalSection mLock;
    CYieldLock mNpcCountLock;
    bool m_bSendIncreaseMakerExFailed;
    bool m_bSendDecreaseMakerExFailed;
    std::array<std::uint8_t, 2> m_makerKeyAlignment;
    volatile std::int32_t mMakerKeyFactory;
    std::map<int, int> mKeyToNpcMap;
    std::u16string mAI;
    TerritoryInfo mTerritoryInfo;
    CReloadableNPCEventHandler* mEventHandler;
    CAIParameter mAIParam;
    std::vector<NpcSpawnDefineEx*> mSpawnDefineList;
    std::map<std::u16string, NpcSpawnDefineEx*> mNpcSpawnDefineMap;
    struct InzoneTypeKey {
        unsigned int classID;
        unsigned int clusterID;
    };
    struct lessRespawn {
        bool operator()(NpcRespawnInfo* left, NpcRespawnInfo* right) const;
    };
    InzoneTypeKey m_inzoneType;
    unsigned int m_nInZoneID;
    int m_nInzoneDBId;
    runtime::PriorityQueue<NpcRespawnInfo*, std::vector<NpcRespawnInfo*>, lessRespawn> mRespawnQueue;
    CYieldLock m_RespawnQueueLock;
    bool m_bNpcMakerExDebugOn;

private:
    friend class CNpcMakerExReleaseVerifier;
    friend struct MakerEvent;
    friend class GlobalObject;
};


} // namespace l2npc

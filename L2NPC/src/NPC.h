#pragma once

#include "Shared/src/MemorySub.h"

#include "Creature.h"
#include "DesirePQ.h"
#include "HateInfo.h"
#include "L2Core/src/inc/Guard.h"
#include "NpcMakerEx.h"
#include "ParamProperty.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "ReloadableNPCEventHandler.h"
#include "ScriptTypeInfo.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace l2npc {

class CNpcSpawnDefine;

} // namespace l2npc


class BaseSpawnDefine;
class CFormationInst;
class CSharedCreatureData;
class CSharedItemData;
class CSharedPartyData;
class CSharedPledgeData;
class CNPC;
struct CNPCEvent;
class CNPCEventHandler;
enum NpcAttackMoveType : int;

// NPC.h in the matching source image. npc.cpp owns the single process-global
// instance. TimeHour in serversocket.cpp replaces all three fields directly.
enum L2TimeFieldType : int {
    L2F_HOUR = 0,
    L2F_MIN = 1,
    L2F_IS_NIGHT = 2,
};

struct L2TimeInfo {
    int mHour = 13;
    int mMin = 0;
    int mIsNight = 0;
};


extern L2TimeInfo g_L2TimeInfo;

// npc.h:2287-2443 in the matching source image. The current executable
// confirms the later 0x20-byte xvector ABI and hence these 0x30/0x38-byte
// layouts and script-visible offsets.
class CodeInfo : public ScriptAccessibleEx {
public:
    CodeInfo() { Init(); }
    ~CodeInfo() override = default;

    void Init() {
        mTargetSet.clear();
        mCurrentPos = 0;
        mCode = 0;
    }

    int GetCode() const { return mCode; }

    void SetCode(int value) { mCode = value; }

    void SetTarget(int objectId) {
        for (int targetId : mTargetSet) {
            if (targetId == objectId) {
                return;
            }
        }
        mTargetSet.push_back(objectId);
    }

    CSharedCreatureData* Next();
    CSharedCreatureData* RandomSelectOne();

    void CollectFunc(ScriptTypeInfo* typeInfo) override {
        ScriptTypeInfo& creatureType = GetBuiltinScriptType(ScriptTypeId::Creature);
        typeInfo->AddFunction(L"Next", &CodeInfo::Next, &creatureType, {});
        typeInfo->AddFunction(L"RandomSelectOne", &CodeInfo::RandomSelectOne, &creatureType, {});
    }

    void CollectVar(ScriptTypeInfo* typeInfo) override {
        DeclareScriptVariable(typeInfo, L"code", static_cast<int>(offsetof(CodeInfo, mCode)), mCode);
        typeInfo->SetCompiledOffset(L"code", 0x2c);
    }

    ScriptAccessibleEx* GetScriptAccessibleEx() const override { return const_cast<CodeInfo*>(this); }

private:
    std::vector<int> mTargetSet;
    int mCurrentPos = 0;
    int mCode = 0;
};


class CodeInfoList : public ScriptAccessibleEx {
public:
    CodeInfoList() { Init(); }
    ~CodeInfoList() override = default;

    void Init() {
        // Target pool reuse keeps the script variable mCode.
        mCodeInfoList.clear();
        mCurrentPos = 0;
    }

    void SetInfo(int code, CSharedCreatureData* creature) {
        L2_GUARD_FUNCTION();
        if (!creature) {
            return;
        }

        for (CodeInfo* entry : mCodeInfoList) {
            if (entry->GetCode() == code) {
                entry->SetTarget(creature->m_nId);
                return;
            }
        }

        CodeInfo* const entry = Singleton<ThreadLocalStorage<TempObjectPool<CodeInfo, 32>, 8>>::inst()->Alloc();
        if (!entry) {
            return;
        }

        entry->SetCode(code);
        entry->SetTarget(creature->m_nId);
        mCodeInfoList.push_back(entry);
    }

    CodeInfo* Next() {
        if (mCurrentPos < 0 || static_cast<std::size_t>(mCurrentPos) >= mCodeInfoList.size()) {
            return nullptr;
        }
        return runtime::CheckedVectorAt(mCodeInfoList, static_cast<std::size_t>(mCurrentPos++));
    }

    CodeInfo* RandomSelectOne() {
        if (mCodeInfoList.empty()) {
            return nullptr;
        }
        return runtime::CheckedVectorAt(mCodeInfoList, static_cast<std::size_t>(LegacyCrtRand()) % mCodeInfoList.size());
    }

    void CollectFunc(ScriptTypeInfo* typeInfo) override {
        ScriptTypeInfo& voidType = GetBuiltinScriptType(ScriptTypeId::Void);
        ScriptTypeInfo& intType = GetBuiltinScriptType(ScriptTypeId::Int);
        ScriptTypeInfo& creatureType = GetBuiltinScriptType(ScriptTypeId::Creature);
        ScriptTypeInfo& codeInfoType = GetBuiltinScriptType(ScriptTypeId::CodeInfo);

        typeInfo->AddFunction(L"SetInfo", &CodeInfoList::SetInfo, &voidType, {&intType, &creatureType});
        typeInfo->AddFunction(L"Next", &CodeInfoList::Next, &codeInfoType, {});
        typeInfo->AddFunction(L"RandomSelectOne", &CodeInfoList::RandomSelectOne, &codeInfoType, {});
    }

    void CollectVar(ScriptTypeInfo* typeInfo) override { DeclareScriptVariable(typeInfo, L"code", 0x08, mCode); }

    ScriptAccessibleEx* GetScriptAccessibleEx() const override { return const_cast<CodeInfoList*>(this); }

private:
    // Current L2NPC.exe 0x00442308 does not initialize this
    // script-visible field. TempObjectPool::Alloc calls Init(), which also
    // keeps it while resetting the list cursor and contents.
    int mCode;
    std::vector<CodeInfo*> mCodeInfoList;
    int mCurrentPos = 0;
};


// npc.h:2451-2503. The current executable has the same 0x10-byte object
// and lock-prefixed operations. Target long remains 32-bit on every host.
class AtomicValue : public ScriptAccessibleEx {
public:
    AtomicValue() = default;
    ~AtomicValue() override = default;

    int Increment(int amount) {
        // Current L2NPC.exe 0x00488908.
        return runtime::raw_atomic::AddI32(&mValue, static_cast<std::int32_t>(amount));
    }

    int Decrement(int amount) {
        // Current L2NPC.exe 0x004f7ab4 uses xadd with the two's-complement
        // negation of amount, including the INT_MIN wraparound case.
        const std::uint32_t bits = 0U - static_cast<std::uint32_t>(amount);
        std::int32_t delta = 0;
        std::memcpy(&delta, &bits, sizeof(delta));
        return runtime::raw_atomic::AddI32(&mValue, delta);
    }

    int Exchange(int value) {
        // Current L2NPC.exe 0x004f7ac0.
        return runtime::raw_atomic::ExchangeI32(&mValue, static_cast<std::int32_t>(value));
    }

    int CompareExchange(int exchange, int comparand) {
        // Current L2NPC.exe 0x00488914 returns the observed old value.
        return runtime::raw_atomic::CompareExchangeI32(
            &mValue, static_cast<std::int32_t>(exchange), static_cast<std::int32_t>(comparand)
        );
    }

    int GetValue() {
        // Current L2NPC.exe 0x00488920 is an aligned volatile dword read.
        return runtime::raw_atomic::LoadI32Relaxed(&mValue);
    }

    void CollectFunc(ScriptTypeInfo* typeInfo) override {
        ScriptTypeInfo& intType = GetBuiltinScriptType(ScriptTypeId::Int);
        typeInfo->AddFunction(L"Increment", &AtomicValue::Increment, &intType, {&intType});
        typeInfo->AddFunction(L"Decrement", &AtomicValue::Decrement, &intType, {&intType});
        typeInfo->AddFunction(L"Exchange", &AtomicValue::Exchange, &intType, {&intType});
        typeInfo->AddFunction(L"GetValue", &AtomicValue::GetValue, &intType, {});
        typeInfo->AddFunction(L"CompareExchange", &AtomicValue::CompareExchange, &intType, {&intType, &intType});
    }

    void CollectVar(ScriptTypeInfo* typeInfo) override { DeclareScriptVariable(typeInfo, L"value", 0x08, mValue); }

    ScriptAccessibleEx* GetScriptAccessibleEx() const override { return const_cast<AtomicValue*>(this); }

    volatile std::int32_t mValue = 0;
};


// npc.h:2511-2581. The current executable confirms every
// script-visible offset and the complete 0x50-byte record.
class RoomInfo : public ScriptAccessibleEx {
public:
    RoomInfo() = default;
    ~RoomInfo() override = default;

    int GetMemberID(int index) {
        // Current L2NPC.exe 0x00488970 has no bounds check.
        return mMemberList[static_cast<std::size_t>(index)];
    }

    void Clear();
    void SetParty(int taggedPartyId);
    int IsInRoom(CSharedCreatureData* creature);
    int PartyChanged();

    void CollectFunc(ScriptTypeInfo* typeInfo) override {
        ScriptTypeInfo& voidType = GetBuiltinScriptType(ScriptTypeId::Void);
        ScriptTypeInfo& intType = GetBuiltinScriptType(ScriptTypeId::Int);
        ScriptTypeInfo& creatureType = GetBuiltinScriptType(ScriptTypeId::Creature);
        typeInfo->AddFunction(L"GetMemberID", &RoomInfo::GetMemberID, &intType, {&intType});
        typeInfo->AddFunction(L"SetParty", &RoomInfo::SetParty, &voidType, {&intType});
        typeInfo->AddFunction(L"Clear", &RoomInfo::Clear, &voidType, {});
        typeInfo->AddFunction(L"IsInRoom", &RoomInfo::IsInRoom, &intType, {&creatureType});
        typeInfo->AddFunction(L"PartyChanged", &RoomInfo::PartyChanged, &intType, {});
    }

    void CollectVar(ScriptTypeInfo* typeInfo) override {
        DeclareScriptVariable(typeInfo, L"index", 0x08, mIndex);
        DeclareScriptVariable(typeInfo, L"member_count", 0x0c, mMemberCount);
        DeclareScriptVariable(typeInfo, L"status", 0x48, status);
        DeclareScriptVariable(typeInfo, L"time", 0x2c, mTime);
        DeclareScriptVariable(typeInfo, L"party_id", 0x30, mPartyID);
    }

    ScriptAccessibleEx* GetScriptAccessibleEx() const override { return const_cast<RoomInfo*>(this); }

    int mIndex = 0;
    int mMemberCount = 0;
    std::array<int, 7> mMemberList{};
    int mTime = 0;
    int mPartyID = 0;
    AtomicValue mStatus;
    AtomicValue* status = &mStatus;
};


// npc.h:2584-2634. RoomInfo records are one contiguous target array.
class RoomInfoList : public ScriptAccessibleEx {
public:
    // ScriptTypeInfo_T<RoomInfoList*>::Init at current 0x00516a88 uses the
    // source-visible default construction path for its kept registration
    // object: empty id, zero size and a null record array. The two-argument
    // constructor below remains the production room-list allocator.
    RoomInfoList() = default;
    RoomInfoList(char16_t* name, int size);

    ~RoomInfoList() override { delete[] mInfos; }

    RoomInfo* GetRoomInfo(int index) {
        if (index < 0 || index >= mSize) {
            return nullptr;
        }
        return mInfos + index;
    }

    void CollectFunc(ScriptTypeInfo* typeInfo) override {
        ScriptTypeInfo& roomInfoType = GetBuiltinScriptType(ScriptTypeId::RoomInfo);
        ScriptTypeInfo& intType = GetBuiltinScriptType(ScriptTypeId::Int);
        typeInfo->AddFunction(L"GetRoomInfo", &RoomInfoList::GetRoomInfo, &roomInfoType, {&intType});
    }

    void CollectVar(ScriptTypeInfo* typeInfo) override { DeclareScriptVariable(typeInfo, L"size", 0x08, mSize); }

    ScriptAccessibleEx* GetScriptAccessibleEx() const override { return const_cast<RoomInfoList*>(this); }

    int mSize = 0;
    RoomInfo* mInfos = nullptr;
    std::u16string mId;
};


namespace layout {

namespace position {
inline constexpr std::size_t x = 0x08;
inline constexpr std::size_t y = 0x0c;
inline constexpr std::size_t z = 0x10;
inline constexpr std::size_t size = 0x18;
} // namespace position

namespace positionList {
inline constexpr std::size_t count = 0x08;
inline constexpr std::size_t positions = 0x10;
inline constexpr std::size_t capacity = 5;
inline constexpr std::size_t size = 0x88;
} // namespace positionList

} // namespace layout

// npc.h:2637-2674. Position has only the shipping default construction,
// reset and script-variable surface. No convenience constructors are exposed.
class Position : public ScriptAccessibleEx {
public:
    Position() { Init(); }
    ~Position() override = default;

    void Init() {
        m_X = 0;
        m_Y = 0;
        m_Z = 0;
    }

    void CollectVar(ScriptTypeInfo* typeInfo) override {
        DeclareScriptVariable(typeInfo, L"x", 0x08, m_X);
        DeclareScriptVariable(typeInfo, L"y", 0x0c, m_Y);
        DeclareScriptVariable(typeInfo, L"z", 0x10, m_Z);
    }

    ScriptAccessibleEx* GetScriptAccessibleEx() const override { return const_cast<Position*>(this); }

    int m_X = 0;
    int m_Y = 0;
    int m_Z = 0;
};


// npc.h:2680-2727. The target owns five Position records inline and silently
// ignores additions after MAX_POSITION.
class PositionList : public ScriptAccessibleEx {
public:
    enum {
        MAX_POSITION = 5,
    };

    PositionList() { Init(); }
    ~PositionList() override = default;

    void Init() { mSize = 0; }

    Position* GetPosition(int index) {
        if (index < 0 || index >= mSize || index >= MAX_POSITION) {
            return nullptr;
        }
        return &mPositions[static_cast<std::size_t>(index)];
    }

    void AddPosition(const FVector& position) {
        if (mSize >= MAX_POSITION) {
            return;
        }
        Position& destination = mPositions[static_cast<std::size_t>(mSize)];
        destination.m_X = ConvertCoordinate(position.x);
        destination.m_Y = ConvertCoordinate(position.y);
        destination.m_Z = ConvertCoordinate(position.z);
        ++mSize;
    }

    void CollectFunc(ScriptTypeInfo* typeInfo) override {
        ScriptTypeInfo& positionType = GetBuiltinScriptType(ScriptTypeId::Position);
        ScriptTypeInfo& intType = GetBuiltinScriptType(ScriptTypeId::Int);
        typeInfo->AddFunction(L"GetPosition", &PositionList::GetPosition, &positionType, {&intType});
    }

    void CollectVar(ScriptTypeInfo* typeInfo) override { DeclareScriptVariable(typeInfo, L"size", 0x08, mSize); }

    ScriptAccessibleEx* GetScriptAccessibleEx() const override { return const_cast<PositionList*>(this); }

private:
    static int ConvertCoordinate(double value) {
        constexpr double minimum = static_cast<double>(std::numeric_limits<std::int32_t>::min());
        constexpr double maximumExclusive = static_cast<double>(std::numeric_limits<std::int32_t>::max()) + 1.0;
        if (!(value >= minimum && value < maximumExclusive)) {
            return std::numeric_limits<std::int32_t>::min();
        }
        return static_cast<int>(value);
    }

    int mSize = 0;
    std::array<Position, MAX_POSITION> mPositions{};
};


// NPC.h / NPC.cpp in the matching source image. Current L2NPC.exe keeps this
// exact 0x28-byte snapshot at CNPC +0x1a70. CDBNPC uses IsChanged before it
// persists an NPC's integer HP/MP and FVector position.
class OldNpcInfo {
public:
    explicit OldNpcInfo(CNPC& npc);

    bool IsChanged();

    CNPC& m_Npc;
    int m_nHP;
    int m_nMP;
    FVector m_vPos;
};


enum eMiniGameAgitFunc {
    MGF_REGISTER_PLEDGE = 0,
    MGF_UNREGISTER_PLEDGE = 1,
    MGF_GET_STATUS = 2,
    MGF_JOIN_GAME = 3,
    MGF_SET_WINNER = 4,
    MGF_GET_UNRETURNED_POINT = 5,
};

enum eOlympiadFunc {
    OF_ADD_OLYMPIAD_TEAM = 0,
    OF_ADD_OLYMPIAD_CLASSLESS = 1,
    OF_ADD_OLYMPIAD_CLASS = 2,
    OF_REMOVE_OLYMPIAD = 3,
    OF_ESCAPE_OLYMPIAD = 4,
    OF_OBSERVE_OLYMPIAD = 5,
    OF_ADD_OLYMPIAD_POINT = 6,
    OF_MAX = 7,
};

void OlympiadFuncHelper(eOlympiadFunc function, int sourceIndex, int targetIndex, int argument);

bool CheckAttackable(
    double attackRange,
    double sourceRadius,
    double targetRadius,
    double distanceSquaredXY,
    double sourceHeight,
    double targetHeight,
    int sourceZ,
    int targetZ
);

// PDB S_GDATA32 `unsigned g_LookNeighborTick`. GeneralEventPacket(-3)
// updates it and both CNPC::LookNeighbor methods read it directly.
extern std::uint32_t g_LookNeighborTick;

// Exact PDB global `int g_NPCClassID[12][18]`. CNPC::HandleEvent writes
// the first two columns for the current logical thread before entering the
// reloadable handler. Call-stack diagnostics read those same cells.
extern int g_NPCClassID[12][18];

// Current L2NPC.exe 0x00da9e60. The current image post-dates the available
// PDB. WndProc and every CNPC timer path share this one atomic counter.
extern std::atomic<std::int32_t> g_nTimerCount;
// NPC.cpp process-global. Every CNPC::SetAI attempt increments it before
// publishing the requested name or resolving a class.
extern std::atomic<std::int32_t> g_nNPCSpawnCount;

// Current L2NPC.exe NPC.cpp one-shot timer owners. Their field order and
// object sizes are visible directly in the machine code.
class CShowTutorialHTMLTimer : public CIOObject {
public:
    CShowTutorialHTMLTimer(int npcIndex, int toIndex, int delaySeconds, const char16_t* htmlFile, const char16_t* soundFile);

    void TimerExpired(int id) override;

private:
    int _npcindex = 0;
    int _toindex = 0;
    std::u16string sHTMLFile;
    std::u16string sSoundFile;
};


class CShowTutorialMarkTimer : public CIOObject {
public:
    CShowTutorialMarkTimer(int npcIndex, int toIndex, int type, int id, int delaySeconds);

    void TimerExpired(int timerId) override;

private:
    int _npcindex = 0;
    int _toindex = 0;
    int _id = 0;
    int _type = 0;
};


// Recovered CNPC runtime entity used by the AI path.
class CNPC : public CCreature, public ScriptAccessibleEx {
public:
    class __async_arg_AsyncTimerExpired;

    // NPC.cpp:6823-6862. TimerData is the exact eight-byte payload stored
    // below the per-script-id TimerSetting.
    struct TimerData {
        int m_MilliSec = 0;
        int m_StartMilliSec = 0;

        static std::atomic<std::int64_t> s_TimerUId;
    };

    struct TimerSetting {
        bool m_bTimerBlock = false;
        std::map<std::int64_t, TimerData*> m_TimerDataDB;

        void _Clear();
        void _Report(int id, std::u16string& report) const;
    };

    CNPC();
    ~CNPC() override;

    L2_DECLARE_MEMORY_ALLOCATION(CNPC)

    static CNPC* GetObjectW(int objectId);
    bool IsPrivate();
    void SetSpawnDefine(BaseSpawnDefine* definition);
    BaseSpawnDefine* SpawnDefine() const { return m_pSpawnDefine; }
    void SetRespawn(bool respawn);
    bool GetRespawn();
    int GetTimerDelay() const;
    int GetDesireSize();
    void SetOption(int index, int value);
    int GetOption(int index);
    int GetCurrentQuestID();
    void SetSpawnedPos(const FVector* position);
    FVector GetSpawnedPos() const;
    void SetFormationInst(CFormationInst* formation);
    bool Set(std::uint32_t npcObjectId, std::uint32_t sharedIndex);
    bool SetAI(const char16_t* aiName);
    int SetParameter(const char16_t* name, const char16_t* value);
    int SetParameter(std::vector<FVector> wayPoints, std::vector<int> wayPointDelays);
    int SetWayPoints(const char16_t* name, std::vector<FVector> wayPoints);
    int Summon_SetOption(int index, int value);
    void BroadcastScriptEventEx(int argument0, int argument1, int argument2, int radius);
    void BroadcastScriptEvent(int argument0, int argument1, int radius);
    void ChangeStopType(int stopType, int argument);
    int Say2(char16_t* text);
    int DebugSay(char16_t* text);
    void SayInt(std::int64_t value);
    void SayFloat(float value);
    int Shout(char16_t* text);
    void ShoutEx(char16_t* text, int value);
    int SayFStr(
        int messageId,
        const char16_t* parameter0,
        const char16_t* parameter1,
        const char16_t* parameter2,
        const char16_t* parameter3,
        const char16_t* parameter4
    );
    int SayFStrExtension(
        int messageId,
        const char16_t* parameter0,
        const char16_t* parameter1,
        const char16_t* parameter2,
        const char16_t* parameter3,
        const char16_t* parameter4,
        int argument0,
        int argument1,
        int argument2,
        int argument3,
        int argument4
    );
    int ShoutFStr(
        int messageId,
        const char16_t* parameter0,
        const char16_t* parameter1,
        const char16_t* parameter2,
        const char16_t* parameter3,
        const char16_t* parameter4
    );
    int ShoutFStrExtension(
        int messageId,
        const char16_t* parameter0,
        const char16_t* parameter1,
        const char16_t* parameter2,
        const char16_t* parameter3,
        const char16_t* parameter4,
        int argument0,
        int argument1,
        int argument2,
        int argument3,
        int argument4
    );
    void ShoutFStrEx(
        int messageId,
        const char16_t* parameter0,
        const char16_t* parameter1,
        const char16_t* parameter2,
        const char16_t* parameter3,
        const char16_t* parameter4,
        int argument
    );
    void ShoutFStrExExtension(
        int messageId,
        const char16_t* parameter0,
        const char16_t* parameter1,
        const char16_t* parameter2,
        const char16_t* parameter3,
        const char16_t* parameter4,
        int argument0,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5
    );
    void SetTimerPeriodByTick(int period);
    void SetTimerPeriod(int period);
    void SetPrivateID(int privateId);
    int _EquipItem(int itemClassId);
    int GetInventoryInfo(CSharedCreatureData* creature, int selector);
    void SetAbilityItemDrop(int ability);
    bool IsWeaponEquippedInHand(CSharedCreatureData* creature);
    bool IsArmorEquippedInHand(CSharedCreatureData* creature);
    int SetWeightPoint(CSharedCreatureData* creature, int weightPoint);
    std::int64_t OwnItemCountEx(CSharedCreatureData* creature, int itemClassId, int enchant);
    CSharedItemData* GetItemData(CSharedCreatureData* creature, int itemClassId);
    int UseItem(int itemIndex);
    int UseCategoryItem(int categoryId);
    int GiveItem(CSharedCreatureData* creature, int itemClassId, std::int64_t count, bool normalItem);
    int GiveItem1(CSharedCreatureData* creature, int itemClassId, std::int64_t count);
    int GiveQuestItem(CSharedCreatureData* creature, int itemClassId, std::int64_t count);
    int GiveItemEx(CSharedCreatureData* creature, int itemClassId, int argument, std::int64_t count);
    int DeleteItem(CSharedCreatureData* creature, int itemClassId, std::int64_t count, bool normalItem, bool itemFlag);
    int DeleteItem1(CSharedCreatureData* creature, int itemClassId, std::int64_t count);
    int DeleteQuestItem(CSharedCreatureData* creature, int itemClassId, std::int64_t count);
    int DeleteItem2(CSharedCreatureData* creature, int itemClassId, std::int64_t count);
    int DeleteItemEx(CSharedCreatureData* creature, int itemClassId, int argument, std::int64_t count);
    int DropItem1(CSharedCreatureData* creature, int itemClassId, std::int64_t count);
    int DropItem2(CSharedCreatureData* creature, int itemClassId, std::int64_t count, int contributorId);
    void CallToChangeClass(CSharedCreatureData* creature, int classId, int argument);
    void ChangeToAwakenedClass(CSharedCreatureData* creature, int classId);
    void CheckWeightToChangeSubclass(CSharedCreatureData* creature, int argument);
    void RideWyvern(CSharedCreatureData* creature, int wyvernClassId);
    void RideForEvent(CSharedCreatureData* creature, int argument0, int argument1, int argument2);
    void SummonAirShip(CSharedCreatureData* creature, int airportId, int platformId);
    void BuyAirShip(CSharedCreatureData* creature, int typeId);
    void CreatePet(CSharedCreatureData* creature, int collarClassId, int petNpcClassId, int petLevel);
    void EvolvePet(
        CSharedCreatureData* creature,
        int originalPetCollarDatabaseId,
        int originalPetNpcClassId,
        int petCollarClassId,
        int petNpcClassId,
        int petLevel
    );
    void DestroyPet(CSharedCreatureData* creature, int petCollarDatabaseId, int reason);
    void EvolvePetWithSameExp(CSharedCreatureData* creature, int petNpcClassId);
    int GetRidingType(CSharedCreatureData* creature);
    CSharedCreatureData* GetMasterUser();
    int GetPathfindFailCount();
    void SetCurrentQuestID(int questId);
    void SetCalledPledgeId(int pledgeId);
    bool SetAIParameter(CSharedCreatureData* creature, int type, int value);
    int GetAIParameter(CSharedCreatureData* creature, int type);
    void SetDieEvent(CSharedCreatureData* creature, int enabled, int distance);
    void SetRestartEvent(CSharedCreatureData* creature, int enabled, int distance);
    void ChangeNPCState(CSharedCreatureData* creature, int state);
    void SetDoorHpLevel(char16_t* doorName, int level);
    void GetDoorHpLevel(CSharedCreatureData* creature, char16_t* doorName);
    void SetStaticMeshStatus(CSharedCreatureData* creature, char16_t* meshName, int targetable, int meshIndex);
    void SetNobless(CSharedCreatureData* creature, int noblessType);
    void SetHero(CSharedCreatureData* creature);
    void SetLegendary(CSharedCreatureData* creature);
    int ClassChange(CSharedCreatureData* creature, int classId, int argument);
    int ClassChangeEx(CSharedCreatureData* creature, int classId, int argument);
    bool SetSkill(CSharedCreatureData* creature, std::int64_t skillNameId);
    void DeleteAcquireSkills(CSharedCreatureData* creature, int acquireType);
    void ChangeClassAndSetSkill(CSharedCreatureData* creature, int classId);
    void IncrementParam(CSharedCreatureData* creature, int parameterType, std::int64_t delta);
    float PointDistFromMe(float x, float y, float z);
    int IsCreatureID(int objectId);
    float DistFromMe(CSharedCreatureData* target);
    int GetWayPointDelay(AiWaypointDelays* delays, int index);
    char16_t* SubStr(char16_t* text, int start, int count);
    int GetAngleFromTarget(CSharedCreatureData* target);
    Position* GetRotatedPosition(int angleOffset, int distance);
    PositionList* GetCloestPositionList(AiWaypoints* waypoints, int x, int y, int z, int count);
    int IsInCategory(int categoryId, int objectId);
    void RegisterTeleporterType(int type, int argument);
    void ShowMultisell(int multisellId, CSharedCreatureData* creature);
    void ShowQuestInfoList(CSharedCreatureData* creature);
    void FHTML_SetFileName(CFHTML* html, char16_t* fileName);
    void FHTML_SetInt(CFHTML* html, char16_t* key, std::int64_t value);
    void FHTML_SetFloat(CFHTML* html, char16_t* key, float value);
    void FHTML_SetStr(CFHTML* html, char16_t* key, char16_t* value);
    void FHTML_SetFStr(
        CFHTML* html,
        char16_t* key,
        int fstringId,
        char16_t* parameter0,
        char16_t* parameter1,
        char16_t* parameter2,
        char16_t* parameter3,
        char16_t* parameter4
    );
    void ShowFHTML(CSharedCreatureData* creature, CFHTML* html);
    void ShowQuestFHTML(CSharedCreatureData* creature, CFHTML* html, int questId);
    void RegisterSiege(CSharedCreatureData* talker);
    void RegisterSiegeDefender(CSharedCreatureData* talker);
    void UnregisterSiege(CSharedCreatureData* talker);
    void CheckSiege(CSharedCreatureData* talker);
    void ViewSiegeList(CSharedCreatureData* talker, char16_t* html);
    void ApproveBattle(CSharedCreatureData* talker, char16_t* html);
    void AgitAuction(CSharedCreatureData* talker, int argument);
    void OpenSiegeInfo(CSharedCreatureData* talker);
    void UseSoulShot(int argument);
    void UseSpiritShot(int argument0, int argument1, int argument2);
    int SetEventHandler(CReloadableNPCEventHandler* handler);
    int GetTimeHour();
    void MG_RegisterPledge(CSharedCreatureData* creature, int argument);
    void MG_UnregisterPledge(CSharedCreatureData* creature);
    void MG_GetStatus(CSharedCreatureData* creature);
    void MG_JoinGame(CSharedCreatureData* creature);
    void MG_SetWinner(CSharedCreatureData* creature);
    void MG_GetUnreturnedPoint(CSharedCreatureData* creature);
    CSharedPledgeData* GetPledge(CSharedCreatureData* creature);
    CSharedPledgeData* GetPledgeByIndex(int index);
    char16_t* PledgeName(int index);
    char16_t* Castle_GetPledgeName();
    char16_t* Castle_GetOwnerName();
    char16_t* Fortress_GetOwnerPledgeName();
    char16_t* Fortress_GetOwnerPledgeMasterName();
    int Residence_GetId();
    int Residence_GetCastleType();
    int Residence_GetCastleTypeByID(int residenceId);
    int Residence_GetCampaignResult();
    int Residence_GetTaxRate();
    int Residence_GetTaxRateCurrent();
    int Residence_GetBuyTaxRateCurrent();
    int Residence_GetChildTaxRate();
    std::int64_t Residence_GetTaxIncome();
    std::int64_t Residence_GetTaxIncomeReserved();
    int GetSSQRoundNumber();
    int GetSSQStatus();
    int GetSSQWinner();
    int GetSSQPrevWinner();
    int GetSSQSealOwner(int sealNumber);
    int GetSSQTotalPoint(int side);
    int GetSSQMemberCount(int side);
    int GetSSQSealSelectionCount(int side, int sealNumber);
    void AddOlympiad(CSharedCreatureData* talker);
    void AddClassFreeOlympiad(CSharedCreatureData* talker);
    void AddTeamOlympiad(CSharedCreatureData* talker);
    int IsMainClass(CSharedCreatureData* talker);
    void RemoveOlympiad(CSharedCreatureData* talker);
    void EscapeOlympiad(CSharedCreatureData* talker);
    void ObserveOlympiad(CSharedCreatureData* joiner, int fieldId);
    int GetOlympiadWaitingCount();
    int GetClassFreeOlympiadWaitingCount();
    int GetTeamOlympiadWaitingCount();
    int GetStatusForOlympiadField(int oneBasedFieldId);
    int GetRuleForOlympiadField(int oneBasedFieldId);
    int GetOlympiadSeason();
    int GetOlympiadStep();
    int GetOlympiadPlayerCount();
    int GetRankByOlympiadRankOrder(int classType, int oneBasedOrder);
    char16_t* GetNameByOlympiadRankOrder(int classType, int oneBasedOrder);
    int GetPointByOlympiadRankOrder(int classType, int oneBasedOrder);
    char16_t* GetPlayer1ForOlympiadField(int oneBasedFieldId);
    char16_t* GetPlayer2ForOlympiadField(int oneBasedFieldId);
    int GetOlympiadPoint(CSharedCreatureData* creature);
    int GetPreviousOlympiadPoint(CSharedCreatureData* creature);
    int GetOlympiadTradePoint(CSharedCreatureData* creature);
    void DeletePreviousOlympiadPoint(CSharedCreatureData* talker, int point);
    void DeleteOlympiadTradePoint(CSharedCreatureData* talker, int point);
    void AddOlympiadTradePoint(CSharedCreatureData* talker, int point);
    CSharedCreatureData* FindNeighborHero(int radius);
    int CanUsePCCafePoint(CSharedCreatureData* talker);
    int GetPCCafePoint(CSharedCreatureData* talker);
    int UpdatePCCafePoint(CSharedCreatureData* talker, int delta);
    void UpdatePCCafePoint2(CSharedCreatureData* talker, int delta);
    int GiveItemByPCCafePoint(CSharedCreatureData* talker, int point, int itemClassId, int itemCount, std::int64_t argument);
    int GetPCCafePointEventState();
    int GetPCCafeCouponEventState();
    int IsEventDropTime();
    int IsPCCafeBonusTime();
    bool IsFishingChampionship();
    int GiveFishingPrize(CSharedCreatureData* creature);
    int ShowHtmlFishingRanking(CSharedCreatureData* creature);
    int GetFishingRanking(CSharedCreatureData* creature);
    int GetKamarokaRound();
    void ShowKamarokaRankingList(CSharedCreatureData* creature, int roomNo);
    void ShowKamarokaUserRankInfo(CSharedCreatureData* creature);
    void AddKamarokaNewRecord(int recordType, CSharedPartyData* party);
    void SetKamarokaReward(int reward, CSharedCreatureData* creature);
    void GetKamarokaCheckReward(int reward, CSharedCreatureData* creature);
    void GetEventRankerInfo(int eventId, CSharedCreatureData* creature, int value);
    void DecreaseEventTopRankerReward(int eventId, int rewardId, CSharedCreatureData* creature);
    void CanGiveEventData(CSharedCreatureData* creature, int value);
    void GiveEventItem(
        CSharedCreatureData* creature,
        int value1,
        std::int64_t value2,
        int value3,
        std::int64_t value4,
        int value5,
        int value6,
        int eventType,
        int value8
    );
    int IsShowingNameTag();
    void ChangeStatus(int status);
    void RegisterDominion(int residenceId, CSharedCreatureData* creature);
    void DeclareLord(int residenceId, CSharedCreatureData* creature);
    bool IsDominionOfLord(int residenceId);
    int GetDominionSiegeID(CSharedCreatureData* creature);
    void CancelPledgeDominion(int residenceId, CSharedCreatureData* creature);
    void CancleUserDominion(int residenceId, CSharedCreatureData* creature);
    void GetPledgeRegistrySize(int residenceId, CSharedCreatureData* creature);
    void GetUserRegistrySize(int residenceId, CSharedCreatureData* creature);
    int IsHostileInDominionSiege(CSharedCreatureData* creature);
    void BlockUpsetManagerEnter(int argument);
    void BlockUpset(int argument0, int argument1, CSharedCreatureData* creature, int argument3);
    void BlockUpsetUserEnter(int argument, CSharedCreatureData* creature);
    void BlockUpsetRegisterMe(int argument);
    void BlockUpsetChangeAmount(int argument0, int argument1);
    void BlockUpsetChangeColor(int argument0, int argument1);
    void CleftManagerEnter();
    int GetCleftState();
    void CleftUserEnter(CSharedCreatureData* creature);
    void CleftCenterDestroyed(int type, CSharedCreatureData* creature, int value);
    bool IsCleftUser(CSharedCreatureData* creature);
    int GetTimeOfSSQ(int timeType);
    std::int64_t GetDepositedSSQItemCount(CSharedCreatureData* creature, int side, int itemType);
    int GetSealSelectionNo(CSharedCreatureData* creature);
    int IsJoinableToDawn(CSharedCreatureData* creature);
    int GetTicketBuyCount(CSharedCreatureData* creature);
    std::int64_t Manor_GetSeedIncome();
    int IsManorSettingTime();
    int GetMaxSellableCount(int manorId, int seedOrder);
    int GetCurrentSeedSellCountSet(int manorId, int seedOrder);
    int GetCurrentSeedRemainCount(int manorId, int seedOrder);
    int GetCurrentSeedPrice(int manorId, int seedOrder);
    int GetNextSeedSellCountSet(int manorId, int seedOrder);
    int GetNextSeedPrice(int manorId, int seedOrder);
    int GetSeedDefaultPrice(int manorId, int seedOrder);
    int GetCropDefaultPrice(int manorId, int cropOrder);
    int GetProcurementRate(int manorId, int cropOrder);
    int GetProcurementCount(int manorId, int cropOrder);
    int GetProcurementType(int manorId, int cropOrder);
    int GetRemainProcureCropCount(int manorId, int cropOrder);
    int GetNextProcurementRate(int manorId, int cropOrder);
    int GetNextProcurementCount(int manorId, int cropOrder);
    int GetNextProcurementType(int manorId, int cropOrder);
    int GetSeedClassidByOrderNum(int manorId, int seedOrder);
    int GetCropClassidByOrderNum(int manorId, int cropOrder);
    void SetSeedSellProperty(
        CSharedCreatureData* creature, int manorId, int seedClassId, std::int64_t count, std::int64_t price
    );
    void SetCropProcureProperty(
        CSharedCreatureData* creature,
        int manorId,
        int cropClassId,
        std::int64_t count,
        int procurementType,
        std::int64_t price
    );
    void ShowSellSeedList(CSharedCreatureData* creature, int manorId);
    void ShowProcureCropList(CSharedCreatureData* creature, int manorId);
    void ShowSeedInfo(CSharedCreatureData* creature, int manorId, int seedOrder);
    void ShowCropInfo(CSharedCreatureData* creature, int manorId, int cropOrder);
    void ShowManorDefaultInfo(CSharedCreatureData* creature);
    void ShowSeedSetting(CSharedCreatureData* creature, int manorId);
    void ShowCropSetting(CSharedCreatureData* creature, int manorId);
    void ShowProcureCropDetail(CSharedCreatureData* creature, int cropOrder);
    int GetTimeAttackFee(int roomNo);
    void Residence_SetTaxRate(int taxRate);
    void Residence_SetVaultEmpty(int value);
    void Residence_SetChildTaxRateEach(int residenceId, int taxRate);
    void Residence_SetChildTaxRate(int taxRate);
    void Residence_VaultTakeOutMoney(CSharedCreatureData* creature, std::int64_t amount);
    void Residence_VaultSaveMoney(CSharedCreatureData* creature, std::int64_t amount);
    void Residence_ResetCastle(CSharedCreatureData* creature);
    void SetCompeteAgitOwner(CSharedCreatureData* creature, int state);
    int Castle_GetPledgeWorldId();
    int Castle_GetPledgeId();
    int Castle_IsUnderSiege();
    int Castle_IsUnderSiege2(int residenceId);
    int IsMyLord(CSharedCreatureData* creature);
    int Castle_GetLifeControlLevel();
    int Castle_GetHPRegen();
    int Castle_GetMPRegen();
    int Castle_GetRawSiegeTime();
    int Castle_GetRawSystemTime();
    char16_t* PrepareSiegeTime(int addFollowingDay, int addHalfDay, int hour, int minute);
    char16_t* Castle_GetSiegeTime();
    void Castle_GateOpenClose(int action);
    void Castle_BanishOthers();
    void Castle_SetCastleType(int castleType);
    int Castle_IsGateOpen();
    void Castle_SetSiegeTime(int addFollowingDay, int addHalfDay, int hour, int minute);
    void Castle_GetRelatedFortressList(CSharedCreatureData* creature);
    void Castle_SetHPRegen(int level);
    void Castle_SetMPRegen(int level);
    int Agit_GetTeleportLevel();
    int Agit_GetCostFailDay();
    int Agit_GetDecoLevel(int type);
    int Agit_GetDecoId(int type);
    int Agit_GetDecoExpire(int type);
    int Agit_GetDecoDay(int type, int level);
    int Agit_GetDecoFee(int type, int level);
    void Agit_SetDeco(CSharedCreatureData* creature, int type, int level);
    void Agit_ResetDeco(CSharedCreatureData* creature, int type);
    void Agit_SetMaster(CSharedCreatureData* creature);
    void Agit_StartObserver(CSharedCreatureData* creature, int x, int y, int z, int yaw, int pitch, int residenceId);
    void Agit_OpenDecoNpc(CSharedCreatureData* creature);
    void AuctionAgit_GetAgitCostInfo(CSharedCreatureData* creature);
    void InstantAgit_Apply(CSharedCreatureData* creature, int argument);
    void InstantAgit_AddDeco(int type, int level);
    void InstantAgit_Return(CSharedCreatureData* creature);
    void InstantAgit_Enter(CSharedCreatureData* creature);
    void InstantAgit_Leave(CSharedCreatureData* creature);
    void InstantAgit_Expel(CSharedCreatureData* creature);
    int InstantAgit_GetApplyCount(int instantAgitId, int selector);
    int InstantAgit_GetApplyFirstDate(int instantAgitId, int selector);
    int InstantAgit_GetApplyLastDate(int instantAgitId, int selector);
    int InstantAgit_GetDecoData(CSharedCreatureData* creature, int selector, int field);
    void InstantAgit_EnterManager();
    void Agit_SetTeleportLevel(int level);
    int GetAgitDeposit(int residenceId);
    int Fortress_GetState(int residenceId);
    int Fortress_GetSiegeStatus(int residenceId);
    bool Fortress_IsInBoundary(int residenceId);
    int Fortress_GetOwnerRewardCycleCount(int residenceId);
    int Fortress_GetCastleTreasureLevel(int residenceId);
    void Fortress_PledgeRegister(int pledgeId, int worldId, int fortressId);
    void Fortress_PledgeUnregister(int pledgeId, int worldId, int fortressId);
    void Fortress_BarrackCaptured(int pledgeId, int fortressId, int barrackType);
    void Fortress_ProtectedNpcDied(int pledgeId, int fortressId);
    void Fortress_ContractCastle(int pledgeId, int worldId, int fortressId, int castleId);
    void Fortress_OwnerRewardTaken(int pledgeId, int worldId, int fortressId, int rewardCycle, std::int64_t reward);
    void Fortress_CastleTreasureTaken(int pledgeId, int worldId, int fortressId);
    void Fortress_SetFacility(CSharedCreatureData* creature, int facilityType, int facilityLevel);
    void Fortress_ResetFacility(CSharedCreatureData* creature);
    int IsLordOfCastle(CSharedCreatureData* creature);
    int Pledge_GetCount(CSharedCreatureData* creature);
    int OwnPledgeNameValue(CSharedCreatureData* creature);
    int HasSubPledge(CSharedCreatureData* creature, int pledgeType);
    int HasAcademy(CSharedCreatureData* creature);
    int SetSubPledgeMaster(CSharedCreatureData* creature, int pledgeType, char16_t* masterName);
    int GetPledgeMemberCount(CSharedCreatureData* creature);
    int GetPledgeCastleSiegeDefenceCount(CSharedCreatureData* creature);
    int GetPledgeSkillLevel(CSharedCreatureData* creature);
    int IsWaitingDismiss(CSharedCreatureData* creature);
    int Castle_GetPledgeState(CSharedCreatureData* creature);
    int Castle_GetPledgeReserveState(CSharedCreatureData* creature);
    int Fortress_GetPledgeSiegeState(CSharedCreatureData* creature);
    int HavePledgePower(CSharedCreatureData* creature, int power);
    CSharedCreatureData* Pledge_GetCreature(CSharedCreatureData* creature, int memberIndex);
    CSharedCreatureData* Pledge_GetLeader(CSharedCreatureData* creature);
    void PledgeLevelUp(CSharedCreatureData* creature, int level);
    CSharedCreatureData* MPCC_GetMaster(int mpccId);
    int MPCC_GetPartyCount(int mpccId);
    int MPCC_GetPartyID(int mpccId, int partyIndex);
    int MPCC_GetMemberCount(int mpccId);
    int MPCC_GetMPCCId(CSharedCreatureData* creature);
    void MPCC_SetMasterPartyRouting(int mpccId, CSharedCreatureData* creature, int routing);
    CSharedCreatureData* GetLeaderOfParty(CSharedPartyData* party);
    CSharedCreatureData* GetMemberOfParty(CSharedPartyData* party, int memberIndex);
    int GetQuestItemProb(CSharedCreatureData* creature, CSharedPartyData* party, int questLevel);
    CSharedCreatureData* Party_GetMember(int partyId, int memberIndex);
    int IsAllPartyMemberActive(CSharedPartyData* party);
    int IsMemberOfParty(CSharedCreatureData* creature, CSharedPartyData* party);
    int GetPledgeMoney(CSharedCreatureData* creature);
    void GetPledgeRaidRewardRate();
    bool SetPledgeRaidPoint(CSharedPledgeData* pledge, std::int64_t point);
    int GetPledgeRaidCurrentStep();
    int GetPledgeRaidCurrentSeason();
    int GetRankOrTierPrevSeasonPledgeRaid(int pledgeDatabaseId);
    int GetHuntNPCCondition();
    void ShowPledgeRaidInfo(CSharedCreatureData* creature, int type);
    void PledgeMissionCheckAndAddCount(CSharedCreatureData* creature, int missionId, int count, int value, int extra);
    void GiveItemByCastleSiegeDefence(
        CSharedCreatureData* creature, int itemClassId, int reason, int argument, std::int64_t count
    );
    int HandleEvent(CNPCEvent* event);
    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    bool Delete() override;
    void Died();
    void OnCreate();
    void OnOlympiadGamePrepared(int fieldId, int matchType);
    void RegisterAsOlympiadOperator();
    void RegisterAsPledgeGameOperator();
    void RegisterAsWorldChatOperator();
    void RegisterAsFieldCycleManager();
    int RegisterToEventListener(int eventType);
    int RegisterAsAirportManager(int airportId, int platformId, int skipSharedPlatformCheck);
    void GetOnAirShip(CSharedCreatureData* target);
    int IsOccupiedPlatform(int sharedPlatformIndex);
    int CanLotto();
    int Lotto_GetState();
    int Lotto_GetRoundNumber();
    int Lotto_GetTicketPrice();
    int Lotto_GetAccumulatedReward();
    char16_t* Lotto_GetChooseTime();
    char16_t* Lotto_GetChosenNumber();
    void Lotto_MakeFinalRewardFHTML(CFHTML* html);
    void Lotto_ShowBuyingPage(CSharedCreatureData* target, int numberFlag, CFHTML* html);
    void Lotto_BuyTicket(CSharedCreatureData* target, int numberFlag, int price);
    void Lotto_ShowPrevRewardPage(CSharedCreatureData* target, int page);
    void Lotto_ShowCurRewardPage(CSharedCreatureData* target, int page);
    void Lotto_GiveReward(CSharedCreatureData* target, int itemDatabaseId);
    void FieldCycleStepExpired(int cycleId, int eventId, int step);
    bool IsEventPeriod(char16_t* eventName);
    void PrivateCreated(int npcObjectId);
    void CreatePrivates(char16_t* value);
    void CreateOnePrivate(int classId, const char16_t* aiName, int privateWeightPoint, int respawnTime);
    void CreateOneAnother(int classId, const char16_t* aiName, int x, int y, int z);
    int CreateOnePrivateEx(
        int classId,
        const char16_t* aiName,
        int privateWeightPoint,
        int respawnTime,
        int x,
        int y,
        int z,
        int direction,
        int p1,
        int p2,
        int p3
    );
    int CreateOnePrivateInzoneEx(
        int classId,
        const char16_t* aiName,
        int privateWeightPoint,
        int respawnTime,
        int x,
        int y,
        int z,
        int direction,
        int p1,
        int p2,
        int p3,
        int inzoneUid
    );
    void ShowPage(CSharedCreatureData* target, char16_t* page);
    void ShowQuestPage(CSharedCreatureData* target, char16_t* page, int questId);
    void ShowSetSiegeTime(
        CSharedCreatureData* target, char16_t* announcingPage, char16_t* siegePage, char16_t* scheduledPage
    );
    void ShowSkillList(CSharedCreatureData* target, char16_t* byePage);
    void ShowEtcSkillList(CSharedCreatureData* target, int listType, char16_t* byePage);
    void ShowEnchantSkillList(CSharedCreatureData* target, int listType);
    void ShowEnchantSkillListDrawer(CSharedCreatureData* target, std::int64_t skillNameId, int listType);
    void ShowGrowEtcSkillMessage(CSharedCreatureData* target, std::int64_t skillNameId, int messageType, char16_t* byePage);
    void ShowGrowSkillMessage(CSharedCreatureData* target, std::int64_t skillNameId, char16_t* byePage);
    void ShowGrowSkillMessage2(
        CSharedCreatureData* target, std::int64_t skillNameId, int argument1, int argument2, char16_t* byePage
    );
    void ShowEnchantSkillMessage(CSharedCreatureData* target, std::int64_t skillNameId, int listType);
    void ShowChoicePage(CSharedCreatureData* to, int nOption);
    void SellPreview(
        CSharedCreatureData* viewer,
        BuySellList* sellList,
        char16_t* ignoredPage,
        char16_t* title,
        char16_t* requiredPage,
        char16_t* emptyListPage
    );
    void ShowBuySell(CSharedCreatureData* viewer, BuySellList* sellList, BuySellList* buyList, float priceAdjustment);
    void SoundEffect(CSharedCreatureData* target, char16_t* soundFile);
    void VoiceEffect(CSharedCreatureData* target, char16_t* voiceFile, int argument);
    void VoiceNPCEffect(CSharedCreatureData* target, char16_t* voiceFile, int argument);
    void BroadcastVoiceNPCEffect(
        CSharedCreatureData* target, char16_t* voiceFile, int argument0, int argument1, int argument2
    );
    void EffectMusic(CSharedCreatureData* target, int radius, char16_t* musicFile);
    void PlayScene(CSharedCreatureData* target, int sceneIndex);
    void StartScenePlayer(CSharedCreatureData* target, int sceneIndex, int argument);
    void StartScenePlayerToParty(CSharedCreatureData* target, int sceneIndex, int argument);
    void StartScenePlayerAround(
        CSharedCreatureData* target, int sceneIndex, int argument0, int argument1, int argument2, int argument3
    );
    void ChangeUserDir(CSharedCreatureData* target, int direction);
    void RequestTotalSkillEnchantCount(CSharedCreatureData* target);
    void GiveRewardBySkillEnchant(CSharedCreatureData* target, int itemClassId);
    void ShowItemChangeAttr(CSharedCreatureData* target, int ignored);
    void FindRandomUser(int argument0, int argument1, int argument2, int argument3);
    void GetPlayingUserCount();
    void ChangeUserTalkTarget(CSharedCreatureData* target);
    void CreateOneDroidNPC(
        int classId, const char16_t* aiName, int privateWeightPoint, int respawnTime, int droidMasterCharSid
    );
    int CreateOneDroidNPCEx(
        int classId,
        const char16_t* aiName,
        int privateWeightPoint,
        int respawnTime,
        int x,
        int y,
        int z,
        int direction,
        int p1,
        int p2,
        int p3,
        int droidMasterCharSid
    );
    int CreateOneDroidNPCInzoneEx(
        int classId,
        const char16_t* aiName,
        int privateWeightPoint,
        int respawnTime,
        int x,
        int y,
        int z,
        int direction,
        int p1,
        int p2,
        int p3,
        int inzoneUid,
        int droidMasterCharSid
    );
    void CreateOnePrivateNearUser(
        CSharedCreatureData* talker, int classId, const char16_t* aiName, int x, int y, int z, int direction
    );
    CSharedCreatureData* Maker_FindNpcByKey(int key);
    int Maker_GetNpcCount();
    void InstantZone_SendMakerEvent(int inzoneTypeId, int clusterId, int inzoneId, int eventId, int argument0);
    bool CheckTerritory(const FVector& position);
    int InMyTerritory(CSharedCreatureData* creature);
    int InSameTerritory(int npcObjectId, CSharedCreatureData* creature);
    int IsInThisTerritory(char16_t* name);
    bool AssembleTerritoryInfo(
        char* buffer,
        unsigned int& capacity,
        unsigned int& used,
        bool initializeBounds,
        int* west,
        int* north,
        int* bottom,
        int* east,
        int* south,
        int* top
    );
    void InstantTeleport(CSharedCreatureData* target, int x, int y, int z);
    void InstantTeleportWithItem(CSharedCreatureData* target, int x, int y, int z, int itemClassId, std::int64_t itemCount);
    void InstantTeleportMPCC(
        CSharedCreatureData* target,
        int x,
        int y,
        int z,
        int range,
        int minimumDistance,
        int maximumDistance,
        int itemClassId,
        std::int64_t itemCount
    );
    void TeleportToUser(CSharedCreatureData* source, CSharedCreatureData* destination);
    void TeleportTo(CSharedCreatureData* source, CSharedCreatureData* destination);
    int TeleportParty(int partyId, int x, int y, int z, int distance, int argument);
    void ShowTelPosListPage(CSharedCreatureData* viewer, TelPosList* list);
    void TeleportFStr(
        CSharedCreatureData* viewer,
        TelPosList* list,
        char16_t* heading0,
        char16_t* heading1,
        char16_t* heading2,
        char16_t* heading3,
        int priceType,
        int noblesse,
        char16_t* footer0,
        char16_t* footer1,
        char16_t* footer2,
        char16_t* footer3,
        char16_t* footer4
    );
    void NoblessTeleport(
        CSharedCreatureData* viewer,
        TelPosList* list,
        char16_t* heading0,
        char16_t* heading1,
        char16_t* heading2,
        char16_t* heading3,
        int argument,
        char16_t* footer
    );
    void InstantRandomTeleportInMyTerritory();
    void InstantTeleportInMyTerritory(int x, int y, int z, int heading);
    void InstantTeleportInMyTerritoryWithCondition(int x, int y, int z, int heading, int condition, unsigned int flags);
    int dDoNothing();
    int dEffectAction(CSharedCreatureData* target, std::int64_t action, int delay);
    int dEffectAction2(CSharedCreatureData* target, std::int64_t action, int delay, int argument);
    int dMoveToWayPoint(std::vector<FVector>* waypoints, std::vector<int>* waypointDelays, int waypointIndex);
    int dMoveTo(const FVector& destination);
    int dMoveToTarget(int targetObjectId, std::int64_t stopDistance);
    int dMoveAround(const int range, const std::int64_t contextValue);
    int dFlee(CSharedCreatureData* target);
    int dFleeEx(int targetObjectId, std::int64_t fleeDistance);
    int dPetDefault(CDesire* desire);
    FVector GetFollowPosition(CNPC* follower, int distance);
    int dFollow(CSharedCreatureData* source, int targetObjectId, int followType, int followData1, int followData2);
    int dGetItem(CSharedItemData* item);
    int dPAttack(CSharedCreatureData* target, std::int64_t contextValue, int attackMoveType, bool force);
    int dStaticObjectAttack(CSharedStaticObjectData* target, std::int64_t contextValue, int attackMoveType, bool force);
    int dUseSkill(unsigned int targetObjectId, std::int64_t skillNameId, CDesire& desire);
    void ResetDesirePQ();
    void CancelTopDesireSkill();
    void ReportOrt(const char* callerFile, int callerLine) override;
    void CollectFunc(ScriptTypeInfo* typeInfo) override;
    void CollectVar(ScriptTypeInfo* typeInfo) override;
    bool IsNPCInScript() const override;
    CNPC* CastNPCInScript() override;
    ScriptAccessibleEx* GetScriptAccessibleEx() const override;
    bool IsNPC() const override;
    CNPC* CastNPC() override;
    int IsTargetable() const override;
    CSharedStaticObjectData* GetStaticObjectFromIndex(int index);
    CSharedStaticObjectData* GetStaticObjectFromID(int id);
    void RegisterGlobalMap(int region, int value);
    int UnregisterGlobalMap(int region);
    int GetGlobalMap(int region);
    int IsStaticObjectID(int id);
    float StaticObjectDistFromMe(CSharedStaticObjectData* target);
    void SeeCreature(CCreature* creature) override;
    void SeeCreatureData(CSharedCreatureData* creature);
    void Spelled(CSharedCreatureData* speller, int skillId, int skillLevel, int skillSubLevel) override;
    void Attacked(CSharedCreatureData* attacker, int damage, int argument, std::int64_t context, bool propagate, int tail);
    void MakeAttackEvent(CSharedCreatureData* attacker, float damage, int propagate);
    void PartyAttacked(CSharedCreatureData* attacker, CSharedCreatureData* privateNpc, int damage);
    void PartySpelled(
        CSharedCreatureData* speller, CSharedCreatureData* privateNpc, int skillId, int skillLevel, int skillSubLevel
    );
    void RealDamaged(CSharedCreatureData* attacker, std::int64_t damage);
    void ClanAttacked(
        CSharedCreatureData* attacker, CSharedCreatureData* victim, int weaponClassId, std::int64_t skillNameId, int damage
    );
    void ClanSpelled(
        CSharedCreatureData* speller, CSharedCreatureData* victim, int skillId, int skillLevel, int skillSubLevel
    );
    void ClanDied(CSharedCreatureData* victim);
    void StaticObjectClanAttacked(
        CSharedCreatureData* attacker,
        CSharedStaticObjectData* victim,
        int weaponClassId,
        std::int64_t skillNameId,
        int damage
    );
    void StaticObjectClanSpelled(
        CSharedCreatureData* speller, CSharedStaticObjectData* victim, int skillId, int skillLevel, int skillSubLevel
    );
    void AbnormalStatusChanged(int skillId, int skillLevel, int skillSubLevel, int spellerObjectId);
    void OnTeleportToUserRes(int creatureObjectId, int argument, int success);
    void OnTrapStepIn(int creatureObjectId, int creatureCount);
    void OnTrapStepOut(int creatureObjectId, int creatureCount);
    void OnTrapActivated(int argument);
    void OnTrapDefused(int argument);
    void OnTrapDetected(int argument);
    void SeeSpell(CSharedCreatureData* speller, CSharedCreatureData* target, std::int64_t skillNameId, int hate);
    bool GiveRewardItem(CSharedCreatureData* talker, int itemClassId);
    bool GiveRewardItem2(CSharedCreatureData* talker, int itemClassId, std::int64_t count, int rewardNpcExistTimeHour);
    bool ResetInzoneBinding(CSharedCreatureData* talker);
    bool ResetInzoneBinding2(CSharedCreatureData* talker, int instantZoneId);
    bool IsRewardItemEnabled() const;
    bool IsResetInzoneBindingEnabled() const;
    int GetRewardItemCount() const;
    int GetRewardNpcExistTimeHour() const;
    int GetRewardNpcType();
    // Physical CNPC image recovered from the current executable. The donor
    // PDB supplies the original member names. Target constructors/accesses
    // supply the post-PDB offsets and the additional AtomicValue/xvector
    // fields. Parser/loader lifetimes remain with their target owners.
    RefHelpers m_RefHelpers; // +0x0030

private:
    friend class CDesirePQ;
    friend class NpcSpawnDefineEx;
    friend class l2npc::CNpcSpawnDefine;

    int pstate; // +0x04b0
    int temper; // +0x04b4
    std::int64_t i_quest0; // +0x04b8
    std::int64_t i_quest1;
    std::int64_t i_quest2;
    std::int64_t i_quest3;
    std::int64_t i_quest4;
    std::int64_t i_quest5;
    std::int64_t i_quest6;
    std::int64_t i_quest7;
    std::int64_t i_quest8;
    std::int64_t i_quest9;
    std::int64_t i_ai0; // +0x0508
    std::int64_t i_ai1;
    std::int64_t i_ai2;
    std::int64_t i_ai3;
    std::int64_t i_ai4;
    std::int64_t i_ai5;
    std::int64_t i_ai6;
    std::int64_t i_ai7;
    std::int64_t i_ai8;
    std::int64_t i_ai9;
    int m_nAttackTick; // +0x0558
    int m_nSuperPointSize; // +0x055c
    CSharedCreatureData* c_quest0; // +0x0560
    CSharedCreatureData* c_quest1;
    CSharedCreatureData* c_quest2;
    CSharedCreatureData* c_quest3;
    CSharedCreatureData* c_quest4;
    CSharedCreatureData* c_ai0; // +0x0588
    CSharedCreatureData* c_ai1;
    CSharedCreatureData* c_ai2;
    CSharedCreatureData* c_ai3;
    CSharedCreatureData* c_ai4;
    AtomicValue mAvQuest0; // +0x05b0
    AtomicValue mAvQuest1;
    AtomicValue mAvAI0;
    AtomicValue mAvAI1;
    AtomicValue* av_quest0; // +0x05f0
    AtomicValue* av_quest1;
    AtomicValue* av_ai0;
    AtomicValue* av_ai1;
    AtomicValue mAvQuest2; // +0x0610
    AtomicValue mAvQuest3;
    AtomicValue mAvQuest4;
    AtomicValue mAvQuest5;
    AtomicValue mAvAI2;
    AtomicValue mAvAI3;
    AtomicValue mAvAI4;
    AtomicValue mAvAI5;
    AtomicValue* av_quest2; // +0x0690
    AtomicValue* av_quest3;
    AtomicValue* av_quest4;
    AtomicValue* av_quest5;
    AtomicValue* av_ai2;
    AtomicValue* av_ai3;
    AtomicValue* av_ai4;
    AtomicValue* av_ai5;
    IntList mIntList; // +0x06d0
    IntList* m_IntList; // +0x0710
    int m_WorldTrapState; // +0x0718

public:
    CSharedCreatureData* sm; // +0x0720
    CSharedCreatureData* master;
    CSharedCreatureData* boss;
    CSharedCreatureData* top_desire_target;
    std::int64_t top_skill_name_id; // +0x0740
    std::uint32_t m_nSpawnTick; // +0x0748
    int m_nMakerID;
    int __x;
    int __y;
    int __z;
    FVector prev_pos; // +0x0760
    bool bInMyTerritory;
    FVector m_vMoveAroundTo; // +0x0780
    FVector m_vMoveAroundStartPoint; // +0x0798
    PhysicalState m_PrevPState; // +0x07b0
    int m_SitOnStop;
    FVector m_vTeleportPosOnLost; // +0x07b8
    int m_MoveSuper_PointTargetIndex;
    int m_MoveSuper_WaitedTime;
    CYieldLock m_TimerLock; // +0x07d8
    std::map<int, TimerSetting*> m_TimerSettingDB; // +0x07e8
    std::uint32_t m_TimerCount;
    std::uint32_t m_TimerOverflowTick;
    bool m_bPrivate;
    std::uint32_t dwLastAttackedTick; // +0x0814
    int mSummonOption[5]; // +0x0818
    std::int32_t m_nSpawnTime;
    bool m_bBetrayOwnMaster;
    std::uint32_t m_nAirShipServerID;
    bool m_bDied;

private:
    CExecutionSerializer<CNPC, 32> Serializer; // +0x0840
    BaseSpawnDefine* m_pSpawnDefine; // +0x1380
    bool m_bRespawn;
    volatile std::int32_t m_nOutOfTerritoryCounter;
    volatile std::int32_t m_nAbsOutOfTerritoryCounter;
    int m_nDecayingCounter;
    int m_nWeaponClassID;
    int m_ChasingTargetServerID;
    int m_nCorpseCounter;
    FVector m_vLastAttackPos; // +0x13a8
    bool m_bMovingWhenAttack;
    int m_nPathFindFailCount;
    FVector m_vSpawnedPos; // +0x13c8
    CYieldLock m_DesirePQSect; // +0x13e0
    CDesirePQ m_DesirePQ; // +0x13f0
    std::u16string m_sAI; // +0x1500
    CReloadableNPCEventHandler* m_pEventHandler;
    CAIParameter m_AIParam; // +0x1530
    int m_bMove;
    std::uint32_t m_DecayLastTick;
    int m_nTimerDelay;
    volatile std::int32_t m_nTimerTick;
    volatile std::int32_t m_nTimerPeriod;
    CYieldLock m_PrivatesSect; // +0x169c
    std::set<int> m_sPrivates; // +0x16b0
    std::u16string mDBName; // +0x16d0
    bool m_bBoss;
    CIOCriticalSection m_FollowSlotSect; // +0x1700
    int m_nFollowSlot[5][24]; // +0x1728
    bool m_bItemDisappeared;
    CIOCriticalSection m_SkillSect; // +0x1910
    std::map<int, int> m_mapSkillFiredTick; // +0x1938
    CYieldLock m_HateChangeLock; // +0x1958
    HateInfoList* m_pHateInfoList;
    HateInfoList m_lstHateInfoList[4]; // +0x1970
    std::uint32_t m_dwLastLookNeighborTick; // +0x1a50
    volatile std::int32_t m_nNpcDespawnCount;
    volatile std::int32_t m_nEnterWorldCount;
    int m_nDBSaveTick;
    int m_nTryReturnCount;
    std::int32_t m_nLastReturnTick;
    CFormationInst* m_pFormationInst;

public:
    OldNpcInfo m_OldNpcInfo; // +0x1a70
    bool m_bFixMoveType;
    std::vector<int> m_vShuffleNumber; // +0x1aa0
    bool m_bDesireFreeze;
    int m_nBuilerIdForDebug;
    int m_nCanActState;
    int m_nNpcStage;
    volatile std::int32_t m_nAi;

    HateInfoList* GetCurrentHateInfoList();
    bool AddTimer(std::uint32_t milliseconds, int timerId);
    bool AddTimerEx(int timerId, int milliseconds);
    int AtomicAddTimerEx(CSharedCreatureData* creature, int timerId, int milliseconds);
    void BlockTimer(int timerId);
    bool UnblockTimer(int timerId);
    bool CheckAndClearTimerEvent(int timerId, std::int64_t uid);
    bool DoTimerEvent(int timerId);
    void CheckTimerCount();
    void __async_AsyncTimerExpired(CExecutionArgumentPad* argument);
    void AsyncTimerExpired1(int timerId, std::int64_t uid);
    void TimerExpired(int timerId) override;
    void TimerExpired2(int timerId, std::int64_t uid) override;

    void ReportTimerInfoToBuilder(int builderObjectId);
    void ReportDesire();
    void ReportDesireToBuilder(int builderObjectId, PhysicalState physicalState);
    void PartyDied(CNPC* died);
    l2npc::NpcMakerEx* GetMyMaker();
    void GiveContribution(CSharedCreatureData* creature, int contribution);
    void AddLogByNpc(int logType, CSharedCreatureData* creature, std::int64_t argument1, std::int64_t argument2);
    void AddLogByNpc2(
        int logType,
        CSharedCreatureData* creature,
        char16_t* text1,
        char16_t* text2,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        int argument6
    );
    int GetNPCLog(CSharedCreatureData* creature, int questId, int logType);
    int GetNPCLogByID(CSharedCreatureData* creature, int questId, int logType, int npcId);
    void DeleteNPCLog(CSharedCreatureData* creature, int questId, int logType);
    void DeleteNPCLogByID(CSharedCreatureData* creature, int questId, int logType, int npcId);
    int GetNRMemoCount(CSharedCreatureData* creature);
    bool HasSubJobClass(CSharedCreatureData* creature);
    int GetSubJobCount(CSharedCreatureData* creature);
    bool IsDualClass(CSharedCreatureData* creature);
    int GetMainClassID(CSharedCreatureData* creature);
    int GetMainClassLevel(CSharedCreatureData* creature);
    int GetDualClassID(CSharedCreatureData* creature);
    int AddFactionPoint(CSharedCreatureData* creature, int factionType, std::int64_t factionPoint, int reason);
    int SetFactionPoint(CSharedCreatureData* creature, int factionType, std::int64_t factionPoint, int reason);
    std::int64_t GetFactionPoint(CSharedCreatureData* creature, int factionType);
    int GetFactionLevel(CSharedCreatureData* creature, int factionType);
    void IncreaseNPCLog(CSharedCreatureData* creature, int questId, int logType, int maximum);
    void IncreaseNPCLogByID(CSharedCreatureData* creature, int questId, int logType, int npcId, int maximum);
    void ReportAIClassHierarchy(int builderObjectId);
    std::u16string GetDBName() const { return mDBName; }
    bool EnterWorld();
    void Save();
    void SaveDie();
    void SaveDie(char16_t* killerMasterName, int reason);
    void SetDBValue(CSharedCreatureData* creature, int value);
    void AddMoveAroundDesire(int method, float value);
    void AddMoveAroundLimitedDesire(int method, float value, int range);
    void AddMoveAroundDesireImpl(int method, float value, int range, int context);
    void AddDoNothingDesire(int seconds, float value);
    int IsNullCreature(CSharedCreatureData* creature);
    int IsNullHateInfo(HateInfo* hateInfo);
    int IsNullParty(CSharedPartyData* party);
    int IsNullString(char16_t* text);
    void AddChaseDesire(CSharedCreatureData* target, float value);
    void AddFleeDesireEx(CSharedCreatureData* target, int method, float value);
    void AddFollowDesire(CSharedCreatureData* target, float value);
    void AddFollowDesire2(CSharedCreatureData* target, float value, int followType, int followData1, int followData2);
    void AddFollowDesire3(
        CSharedCreatureData* source,
        CSharedCreatureData* target,
        float value,
        int followType,
        int followData1,
        int followData2
    );
    void AddDecayingDesire(float value);
    void AddMoveToWayPointDesire(
        std::vector<FVector>* waypoints, std::vector<int>* waypointDelays, int waypointIndex, float value
    );
    void AddPetDefaultDesire_Follow(float value);
    void _AddPetDefaultDesire_Follow(float value);
    void AddMoveFreewayDesire(int id, int context, float value);
    int AddMoveToDesire(int x, int y, int z, float value);
    void AddMoveToTargetDesire(int targetObjectId, int contextValue, int flags, float value);
    void AddMoveSuperPointDesire(const char16_t* name, int method, float value, int direction);
    void AddMoveFormationDesire(
        const char16_t* name, int id, int context, int uniqueId, int formationId, int formationIndex, int flags, float value
    );
    int dMoveSuperPoint(CDesire& desire);
    int dMoveFreeway(CDesire* desire);
    int dMoveFormation(CDesire* desire);
    void AddGetItemDesire(CSharedItemData* item, float value);
    void AddGetItemDesireEx(int itemIndex, float value);
    void RemoveDesire(PhysicalState physicalState);
    int GetLifeTime();
    int GetTick();
    int GetCurrentTick();
    int GetDirection(CSharedCreatureData* creature);
    int GetDirectionToTarget(CSharedCreatureData* creature);
    int GetMyDirection();
    int IsSpoiled();
    float HPRatio(CSharedCreatureData* creature);
    int GetMemoCount(CSharedCreatureData* creature);
    int SetMemo(CSharedCreatureData* creature, int questId);
    int SetMemoState(CSharedCreatureData* creature, int questId, int state);
    int SetMemoStateEx(CSharedCreatureData* creature, int questId, int slot, int state);
    int RemoveMemo(CSharedCreatureData* creature, int questId);
    void SetJournal(CSharedCreatureData* creature, int questId, int value);
    void SetFlagJournal(CSharedCreatureData* creature, int questId, int flag);
    void ResetFlagJournal(CSharedCreatureData* creature, int questId, int flag);
    int SetNRMemo(CSharedCreatureData* creature, int questId);
    int SetNRMemoState(CSharedCreatureData* creature, int questId, int state);
    int SetNRMemoStateEx(CSharedCreatureData* creature, int questId, int slot, int state);
    int RemoveNRMemo(CSharedCreatureData* creature, int questId);
    void SetNRJournal(CSharedCreatureData* creature, int questId, int value);
    void SetNRFlagJournal(CSharedCreatureData* creature, int questId, int flag);
    void ResetNRFlagJournal(CSharedCreatureData* creature, int questId, int flag);
    int GetOneTimeQuestFlag(CSharedCreatureData* creature, int questId);
    void SetOneTimeQuestFlag(CSharedCreatureData* creature, int questId, int value);
    int IsNewbie(CSharedCreatureData* creature);
    int IsOlympiadRegistered(CSharedCreatureData* creature);
    int IsAcademyMember(CSharedCreatureData* creature);
    int HasAcademyMaster(CSharedCreatureData* creature);
    int HasAcademyMember(CSharedCreatureData* creature);
    CSharedCreatureData* GetAcademyMaster(CSharedCreatureData* creature);
    CSharedCreatureData* GetAcademyMember(CSharedCreatureData* creature);
    int IsPCCafeUser(CSharedCreatureData* creature);
    int IsInCombatMode(CSharedCreatureData* creature);
    bool IsAttackable(CSharedCreatureData* creature);
    int CanAttack(CSharedCreatureData* creature);
    bool CanAttackEx(double targetCollisionRadius, const FVector& targetPosition, NpcAttackMoveType attackMoveType);
    float GetOverhitBonus(CSharedCreatureData* creature);
    void Dispel(CSharedCreatureData* creature, int selector);
    void Suicide(int argument);
    int UseSkill(CSharedCreatureData* creature, std::int64_t skillNameId);
    void SetTicketBuyCount(CSharedCreatureData* creature, int count);
    void CastBuffForQuestReward(CSharedCreatureData* creature, std::int64_t skillNameId);
    void CastBuffForAgitManager(CSharedCreatureData* creature, std::int64_t skillNameId);
    int IsMyBossAlive();
    int DepositSSQItem(CSharedCreatureData* creature, int side, int seal, std::int64_t firstCount, std::int64_t secondCount);
    int AddSSQMember(
        CSharedCreatureData* creature, int side, int seal, int position, std::int64_t initialPoint, int joinType
    );
    int DepositSSQItemEx(
        CSharedCreatureData* creature,
        int side,
        std::int64_t firstCount,
        std::int64_t secondCount,
        std::int64_t thirdCount,
        std::int64_t point
    );
    int DeleteDepositedSSQItem(CSharedCreatureData* creature, int side, int itemIndex, std::int64_t count);
    int AddTimeAttackRecord(int roomNumber, int partType, int partyServerId, int point, int eventTime, int elapsedTime);
    int AddTimeAttackFee(int roomNumber, int fee, int ignored);
    int GiveTimeAttackReward(CSharedCreatureData* creature, int roomNumber, int itemClassId, int count);
    void EventManagerEnter();
    void YearCatchEventManagerEnter();
    void ObserveEventMatch(CSharedCreatureData* creature, int eventId);
    void InsertEventRanking(int eventId, CSharedCreatureData* creature, int score);
    void CheckCursedUser(CSharedCreatureData* creature);
    int GetEvolutionId();
    void SetEvolutionId(int evolutionId);
    int InstantZone_GetId();
    int InstantZone_GetTypeId();
    void InstantZone_Enter(CSharedCreatureData* target, int instantZoneTypeId, int argument);
    void InstantZone_Leave(CSharedCreatureData* target, int x, int y, int z);
    void InstantZone_Leave_Ex(CSharedCreatureData* target, int x, int y, int z);
    void InstantZone_Finish_Ex(CSharedCreatureData* target, int result);
    void InstantZone_Finish(int result);
    void InstantZone_MarkRestriction();
    void InstantZone_AddExtraDuration(int duration);
    void InstantZone_GetDuration();
    void GetAllUserForInZone();
    void PlayAnimation(int animationId, int argument);
    void SetWorldTrapVisibleByClassId(int worldTrapClassId, int visible);
    void ActivateWorldTrapByClassId(int worldTrapClassId, int argument);
    void DefuseWorldTrapByClassId(int worldTrapClassId, int argument);
    void SetVisible(int visible);
    void UnequipWeapon();
    void SetEnchantOfWeapon(int enchant);
    void LookItem(int itemClassId, std::int64_t count, int enchant);
    int IsStackableItem(CSharedItemData* item);
    int IsStackableItemEx(int itemIndex);
    void OpenHennaItemListForEquip(CSharedCreatureData* target);
    void OpenHennaListForUnquip(CSharedCreatureData* target);
    void ShowPremiumItemList(CSharedCreatureData* target);
    int UpdatePledgeNameValue(CSharedCreatureData* target, int value);
    int CreateAcademy(CSharedCreatureData* target, char16_t* academyName);
    int CreateSubPledge(CSharedCreatureData* target, int subPledgeType, int subPledgeId, char16_t* subPledgeName);
    void ShowChangePledgeNameUI(CSharedCreatureData* target);
    void ChangeNickName(CSharedCreatureData* target, char16_t* name);
    void ChangeFStrNickName(CSharedCreatureData* target, int messageId, char16_t* name);
    void ChangeMasterName(CSharedCreatureData* target, char16_t* name);
    void ChangeFStrMasterName(CSharedCreatureData* target, int messageId, char16_t* name);
    int GetPVPPoint(CSharedCreatureData* target);
    int GiveItemByPVPPoint(CSharedCreatureData* target, int itemClassId, int reason, int argument, std::int64_t count);
    void UpdatePVPPoint(CSharedCreatureData* target, int delta, int type);
    void CreatePVPMatch(int matchType);
    void RegisterPVPMatch(CSharedPartyData* party, CSharedCreatureData* target);
    void UnregisterPVPMatch(CSharedPartyData* party, CSharedCreatureData* target);
    void StartPVPMatch(CSharedPartyData* blueTeamParty, CSharedPartyData* redTeamParty);
    void EndPVPMatch();
    void CheckRegisterParty(CSharedPartyData* party);
    void CheckRegisterParty2(CSharedPartyData* blueTeamParty, CSharedPartyData* redTeamParty);
    void RegisterResurrectionTower(int team, int towerObjectId);
    void RegisterUserResurrectionTower(unsigned int userDatabaseId);
    void CheckRegisterUserPVPMatch(CSharedCreatureData* target);
    void IsUserPVPMatching(CSharedCreatureData* target);
    void RegisterUserPVPMatch(CSharedCreatureData* target);
    void UnregisterUserPVPMatch(CSharedCreatureData* target);
    void AddKillPointUserPVPMatch(CSharedCreatureData* target, int point);
    void GetRankUserPVPMatch(CSharedCreatureData* target);
    int CreateBingoBoard(CSharedCreatureData* target, int size);
    int GetBingoBoardSize(CSharedCreatureData* target);
    int GetNumberFromBingoBoard(CSharedCreatureData* target, int index);
    int SelectBingoNumber(CSharedCreatureData* target, int number);
    int GetBingoSelectCount(CSharedCreatureData* target);
    int IsSelectedBingoNumber(CSharedCreatureData* target, int number);
    int GetMatchedBingoLineCount(CSharedCreatureData* target);
    void ClearBingoBoard(CSharedCreatureData* target);
    void ShowCreatePledge(CSharedCreatureData* target);
    void ShowVariationMakeWindow(CSharedCreatureData* target);
    void ShowVariationCancelWindow(CSharedCreatureData* target);
    void ShowBaseAttributeCancelWindow(CSharedCreatureData* target);
    void ShowCommission(CSharedCreatureData* target);
    void ShowUpgradeSystem(CSharedCreatureData* target);
    void ShowStatPage(CSharedCreatureData* target);
    void ShowCardCollect(CSharedCreatureData* target);
    void ShowEnsoulWindow(CSharedCreatureData* target);
    void ShowEnsoulExtractionWindow(CSharedCreatureData* target);
    void ShowBeautyShop(CSharedCreatureData* target, int shopType);
    void ShowCardUpdownGame(CSharedCreatureData* target, int argument);
    void ShowChannelingEffect(CSharedCreatureData* target, int skillId, int duration);
    void SetHTMLCookie(CSharedCreatureData* target, int pageGroup, int page);
    int GetHTMLCookie(CSharedCreatureData* target, int pageGroup, int page);
    void SetCookie(CSharedCreatureData* target, char16_t* key, int value);
    int GetCookie(CSharedCreatureData* target, char16_t* key);
    void SetAttackable(CSharedCreatureData* target, bool attackable);
    void ChangeDir(CSharedCreatureData* target, int direction, int argument);
    void ChangeMoveType2Ex(MoveType moveType, bool force, unsigned int reason);
    void ChangeMoveTypeEx(MoveType moveType, unsigned int reason);
    void ChangeMoveType(MoveType moveType);
    void ChangeMoveType2(MoveType moveType, bool force);
    void SendMovePacket(const FVector& destination);
    bool GetTopDesire(CDesire& desire);
    void ChangePetMoveMode(int mode, FVector position);
    void TogglePetMoveMode();
    void ShowSystemMessage(CSharedCreatureData* target, int messageId);
    void ShowSystemMessageStr(CSharedCreatureData* target, char16_t* text);
    void ShowSystemMessageFStr(
        CSharedCreatureData* target,
        int messageId,
        const char16_t* parameter0,
        const char16_t* parameter1,
        const char16_t* parameter2,
        const char16_t* parameter3,
        const char16_t* parameter4
    );
    void BroadcastSystemMessageW(CSharedCreatureData* target, int radius, int messageId);
    void BroadcastSystemMessageStr(CSharedCreatureData* target, int radius, char16_t* text);
    void BroadcastSystemMessageFStr(
        CSharedCreatureData* target,
        int radius,
        int messageId,
        char16_t* parameter0,
        char16_t* parameter1,
        char16_t* parameter2,
        char16_t* parameter3,
        char16_t* parameter4
    );
    void ShowSysMsgToParty2(
        CSharedPartyData* party, int routingValue, int messageId, int argument0, int argument1, int argument2, int argument3
    );
    void Whisper(CSharedCreatureData* target, char16_t* text);
    void WhisperFStr(
        CSharedCreatureData* target,
        int messageId,
        const char16_t* parameter0,
        const char16_t* parameter1,
        const char16_t* parameter2,
        const char16_t* parameter3,
        const char16_t* parameter4
    );
    void ShowTutorialHTML(CSharedCreatureData* target, char16_t* htmlFile);
    void ShowNewTutorialHTML(CSharedCreatureData* target, char16_t* htmlFile);
    void ShowTutorialHTML2(CSharedCreatureData* target, char16_t* htmlFile, int delaySeconds, char16_t* soundFile);
    void ShowTutorialMark(CSharedCreatureData* target, int id, int type);
    void ShowTutorialMark2(CSharedCreatureData* target, int id, int type, int delaySeconds);
    void ShowQuestMark(CSharedCreatureData* target, int questId);
    void CloseTutorialHTML(CSharedCreatureData* target);
    void EnableTutorialEvent(CSharedCreatureData* target, int eventId);
    void ShowRadar(CSharedCreatureData* target, int x, int y, int z, int type);
    void DeleteRadar(CSharedCreatureData* target, int x, int y, int z, int type);
    void DeleteAllRadar(CSharedCreatureData* target, int type);
    void ShowDirection(CSharedCreatureData* target, char16_t* directionFile, int argument0, int argument1, int argument2);
    void EarthQuakeByNPC(
        CSharedCreatureData* target, int argument0, int argument1, int argument2, int argument3, int argument4, int argument5
    );
    void EarthQuakeToParty(int partyId, int intensity, int duration, int radius);
    void SpecialCamera(
        CSharedCreatureData* target,
        int argument0,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        int argument6,
        int argument7,
        int argument8,
        int argument9,
        int argument10
    );
    void SpecialCameraEx(
        CSharedCreatureData* source,
        CSharedCreatureData* target,
        int argument0,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        int argument6,
        int argument7,
        int argument8
    );
    void SpecialCamera3(
        CSharedCreatureData* target,
        int argument0,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        int argument6,
        int argument7,
        int argument8,
        int argument9,
        int argument10
    );
    void SpecialCameraZLimit(
        CSharedCreatureData* target,
        int argument0,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        int argument6,
        int argument7,
        int argument8,
        int argument9,
        int argument10,
        int argument11,
        int argument12
    );
    void ChangeZoneInfo(CSharedCreatureData* target, int argument0, int argument1);
    void ChangeFogInfo(CSharedCreatureData* target, int argument0, int argument1);
    void ChangeScreenEffectInfo(CSharedCreatureData* target, int argument0, int argument1);
    void SetControlTowerLevel(char16_t* npcName, int level);
    void GetControlTowerLevel(CSharedCreatureData* target, char16_t* npcName);
    void TB_RegisterPledge(CSharedCreatureData* target);
    void TB_RegisterMember(CSharedCreatureData* target);
    void TB_GetNpcType(CSharedCreatureData* target);
    void TB_SetNpcType(CSharedCreatureData* target, int npcType);
    void TB_GetPledgeRegisterStatus(CSharedCreatureData* target, int pledgeId);
    void TB_GetBattleRoyalPledgeList(CSharedCreatureData* target);
    void TB_CheckMemberRegisterStatus(int memberId, CSharedCreatureData* target);
    void GetSubJobList(CSharedCreatureData* target, int argument0, int argument1);
    void CreateSubJob(CSharedCreatureData* target, int occupationId);
    void ChangeSubJob(CSharedCreatureData* target, int occupationId);
    void RenewSubJob(CSharedCreatureData* target, int argument0, int argument1, int dualClass);
    void CreateDualJob(CSharedCreatureData* target, int occupationId);
    bool SetSkillFired(std::int64_t skillNameId, int targetObjectId, int successNotification);
    void SpellSuccessed(CSharedCreatureData* target, std::int64_t skillNameId);
    void UseSkillStarted(std::uint32_t targetObjectId, std::int64_t skillNameId);
    void UseSkillFinished(std::uint32_t targetObjectId, std::int64_t skillNameId, bool well);
    void SendAreaStatusEvent(int inzoneId, char16_t* name, bool status);
    void SendDoorStatusEvent(int inzoneId, char16_t* name, bool status);
    bool OnPacketDesireManipulation(int actorObjectId, int actorSharedIndex, int argument, double value);
    bool OnPacketTransferHate(int ignored, int sourceUserObjectId, int destinationUserObjectId);
    void ReturnStartPoint() const;
    void SetDroidNPC(int npcClassId);
    void ClearDroidNPC();
    void ShowMultisellForEventMaker(CSharedCreatureData* talker);
    void ShowNewTutorialID(CSharedCreatureData* talker, int tutorialId);
    void SendAgitWarPacket(CSharedCreatureData* talker);
    void TeleportToInstantZone(CSharedCreatureData* source, CSharedCreatureData* destination, int instantZoneId);
    int GiveItemViaPledgeGame(CSharedCreatureData* talker, int argument);
    void GiveItemOnAccount(
        CSharedCreatureData* talker, int value1, std::int64_t value2, int value3, std::int64_t value4, int value5, int value6
    );
    void GiveItemOnAccount2(
        CSharedCreatureData* talker,
        int value1,
        std::int64_t value2,
        int value3,
        std::int64_t value4,
        int value5,
        int value6,
        std::int64_t value7,
        int value8,
        int value9
    );
    void GiveItemOnAccountForEvent(
        CSharedCreatureData* talker,
        int value1,
        std::int64_t value2,
        int value3,
        std::int64_t value4,
        int value5,
        int value6,
        char16_t* eventName
    );
    void DeleteAcquireSkill(CSharedCreatureData* target, int acquireType);
    void AddWorldChatBonusPoint(CSharedCreatureData* talker, int pointType, int point);
    void AddAndShowMonsterArenaPoint(CSharedCreatureData* talker, int inzoneTypeId, int stageProgress, int currentPoint);
    void ClearMonsterArenaPoint(CSharedCreatureData* talker, int inzoneTypeId);
    int AcceptUserQuest(CSharedCreatureData* talker, int questId);
    void InstantTeleportInzone(CSharedCreatureData* talker, int targetX, int targetY, int targetZ, int instantZoneId);
    void BroadcastScriptEventCond(int eventId, int argument1, int argument2, int radius);
    void AddEventLogByNpc(
        int selector,
        CSharedCreatureData* talker,
        char16_t* text0,
        char16_t* text1,
        int argument0,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5
    );
    void CastBuffForEventMaker(CSharedCreatureData* target);
    void CastBuffToRandomUser(std::int64_t skillNameId);
    bool AddAttackDesire2(CSharedCreatureData* target, int attackMoveType, float value);
    bool IsLockedItem(CSharedCreatureData* talker, int itemClassId);
    int UpdatePledgeNameValueV2(CSharedCreatureData* talker, int delta);
    void WorldRaid_Start();
    void WorldRaid_Stop();
    void WorldRaid_BroadcastEvent(int argument0, int argument1, int argument2);
    void SetTargetUserLevel(CSharedCreatureData* target, int level);
    int GetRaidPoint(CSharedCreatureData* talker);
    void UpdateRaidPoint(CSharedCreatureData* talker, int delta);
    bool IsOlympiadApplicant(CSharedCreatureData* talker);
    int GetHeroClass(CSharedCreatureData* talker);
    int GetUserInkEnergy(CSharedCreatureData* talker);
    void UpdateUserInkEnergy(CSharedCreatureData* talker, int argument0, int argument1);
    void StartDynamicQuest(CSharedCreatureData* talker, int questId);
    int IsPremiumUser(CSharedCreatureData* talker);
    int CheckAndSetOneTimeUserFlag(int flag, CSharedCreatureData* talker);
    bool IsUserWithPrivateStore(CSharedCreatureData* talker);
    void ShowLuckyGame(CSharedCreatureData* talker, int argument);
    void SetTrainingState(CSharedCreatureData* talker, int state);
    int GetTrainingState(CSharedCreatureData* talker);
    std::int64_t GetUserTraining(CSharedCreatureData* talker, int selector);
    void RequestGetGreatEventContributor();
    int GetAcquiredAbilityPoint(CSharedCreatureData* talker);
    int GetMachineWorldID();
    void SetMyNPCStage(int stage);
    int GetMyNPCStage();
    void SetBossNPCStage(int increment, int threshold, bool incrementBossStage);
    int GetBossNPCStage();
    int IsBoss();
    int GetNation();
    void SetVolatileVal(int value);
    int GetVolatileVal();
    int AddVolatileVal(int value);
    std::int64_t MinValue(std::int64_t first, std::int64_t second);
    bool TimerExpiredForSummon();
    void AddChoice(int code, int msgIndex);
    int GetSkill_ID(std::int64_t skillNameId);
    int GetSkill_Level(std::int64_t skillNameId);
    int GetSkill_SubLevel(std::int64_t skillNameId);
    std::int64_t MakeSkillNameID(int skillId, int level, int subLevel);
    std::int64_t ChangeSkill_ID(std::int64_t skillNameId, int delta);
    std::int64_t ChangeSkill_Level(std::int64_t skillNameId, int delta);
    std::int64_t ChangeSkill_SubLevel(std::int64_t skillNameId, int delta);
    int Skill_GetTargetType(std::int64_t skillNameId);
    int Skill_GetConsumeHP(std::int64_t skillNameId);
    int Skill_GetConsumeMP(std::int64_t skillNameId);
    int Skill_GetEffectPoint(std::int64_t skillNameId);
    int Skill_GetAbnormalType(std::int64_t skillNameId);
    bool Skill_HaveAttribute(std::int64_t skillNameId, int attribute);
    int Skill_InHitCoolTime(std::int64_t skillNameId);
    int Skill_InReuseDelay(std::int64_t skillNameId);
    void LogSkillError(int sourceLine, std::int64_t skillNameId);
    void IsToggleSkillOnOff(CSharedCreatureData* creature, std::int64_t skillNameId);
    void SRandNumbers(int maximumNumber, int requestedCount, int copies);
    int SRandGetNumbers(int index);
    bool CanAct();
    void LookNeighbor(int argument);
    void LookNeighbor1(int argument);
    bool AddAttackDesireEx(int targetObjectId, int attackMoveType, int force, float value);
    bool AddAttackDesire(CSharedCreatureData* target, int attackMoveType, float value);
    CSharedCreatureData* GetLastAttacker();
    CCreature* GetMaster();
    CSharedCreatureData* GetPet(CSharedCreatureData* owner);
    CSharedCreatureData* GetSummonByIndex(CSharedCreatureData* owner, int index);
    int HaveSummonOrPet(CSharedCreatureData* owner);
    bool FixMoveType(bool fixed);
    bool AddAllSummonAttackDesire(CSharedCreatureData* target, int attackMoveType, bool force, float value);
    bool AddUseSkillDesire(
        CSharedCreatureData* target, std::int64_t skillNameId, int skillTargetType, int attackMoveType, float value
    );
    bool AddUseOneTimeSkillDesire(
        CSharedCreatureData* target, std::int64_t skillNameId, int skillTargetType, int attackMoveType, float value
    );
    bool AddUseSkillDesireEx(
        int targetObjectId,
        std::int64_t skillNameId,
        int skillTargetType,
        int attackMoveType,
        int force,
        float value,
        int flags
    );
    bool AddUseSkillDesireEx2(
        int targetObjectId,
        std::int64_t skillNameId,
        int skillTargetType,
        int attackMoveType,
        int force,
        float value,
        int flags,
        int originId,
        int actionEvent
    );
    void AddEffectActionDesire(CSharedCreatureData* target, int action, int deadline, float value);
    void AddEffectActionDesire2(CSharedCreatureData* target, int action, int deadline, float value, int range);
    void RemoveAttackDesire(int targetObjectId);
    void RemoveAttackDesireOnly(int targetObjectId);
    void RemoveAttackDesireOnTop();
    bool RemoveAbsoluteDesire();
    void RandomizeAttackDesire();
    float GetTopDesireValue(PhysicalState state);
    void SetTopDesireTime(int seconds);
    void AddFleeDesire(CSharedCreatureData* target, float value);
    void SetMaxHateListSize(int size);
    void AddHateInfo(
        CSharedCreatureData* creature, std::int64_t hate, std::int64_t temp0, int forceAdd, std::int64_t accumulate
    );
    std::int64_t GetAvgHateValue();
    int GetHateInfoCount();
    HateInfo* GetHateInfoByIndex(int index);
    HateInfo* GetHateInfoByCreature(CSharedCreatureData* creature);
    HateInfo* GetClosestHateInfo();
    HateInfo* GetMaxHateInfo(int selector);
    HateInfo* GetMinHateInfo(int selector);
    HateInfo* GetNthHateInfo(int selector, int index, bool fromMinimum);
    void RemoveHateInfoByCreature(CSharedCreatureData* creature);
    bool TransferHateInfoByCreature(CSharedCreatureData* source, CSharedCreatureData* destination);
    void RemoveAllHateInfoIF(int condition, int value = 0);
    void SetHateInfoListIndex(int index);
    void RemoveAllDesire();
    void RemoveAllAttackDesire();
    void StopMove();
    void SetTeleportPosOnLost(int x, int y, int z);
    FVector GetTeleportPosOnLost();
    void RenewSpawnedPos(int x, int y, int z);
    int SetSuperPointSize(int value);
    void Despawn();
};


void MiniGameFuncHelper(eMiniGameAgitFunc function, CNPC* npc, CSharedCreatureData* creature, int argument);

// Recovered from l2npc/NPC.cpp. The field order mirrors the packet parameter
// consumed by NPCServer::Send("cdddpSddddddddddddd", ...).
class CreateOnePrivatePacketHelper {
public:
    struct PrivatePacketParam {
        int m_BossId = 0;
        int m_ClassId = 0;
        int m_PrivateId = 0;
        BaseSpawnDefine* m_pSpawnDefine = nullptr;
        const char16_t* m_AiName = nullptr;
        int m_WeightPoint = 0;
        int m_RespawnTime = 0;
        FVector m_Pos;
        int m_Dir = 0;
        int m_P1 = 0;
        int m_P2 = 0;
        int m_P3 = 0;
        int m_Fixed = 1;
        int m_MakerId = 0;
        int m_InzoneId = 0;
        int m_nDroidMasterCharSID = 0;
    };

    static void Send(const PrivatePacketParam& param);
};


class CPartyPrivateCreator : public CIOObject {
public:
    CPartyPrivateCreator(
        CNPC* boss, int classId, const char16_t* aiName, int privateWeightPoint, int respawnTime, int droidMasterCharSid
    );
    ~CPartyPrivateCreator() override;

    void TimerExpired(int timerId) override;

private:
    [[maybe_unused]] int m_nBossIndex = 0;
    int m_nClassID = 0;
    int m_nPrivateWeightPoint = 0;
    int m_nRespawnTime = 0;
    std::uint32_t m_nBossID = 0;
    std::u16string m_sAI;
    FVector m_vBossPos;
    BaseSpawnDefine* m_pSpawnDefine = nullptr;
    int m_EventID = -1;
    int m_PrivateID = 0;
    int m_nDroidMasterCharSID = 0;
};

class CPartyPrivateCreatorEx : public CIOObject {
public:
    CPartyPrivateCreatorEx(
        CNPC* boss,
        int classId,
        const char16_t* aiName,
        int privateWeightPoint,
        int respawnTime,
        int x,
        int y,
        int z,
        int direction,
        int p1,
        int p2,
        int p3,
        int makerId,
        int droidMasterCharSid
    );
    ~CPartyPrivateCreatorEx() override;

    void TimerExpired(int timerId) override;

private:
    int m_nBossID = 0;
    int m_nClassID = 0;
    int m_nPrivateWeightPoint = 0;
    int m_nRespawnTime = 0;
    int m_maker_id = 0;
    std::u16string m_sAI;
    int m_x = 0;
    int m_y = 0;
    int m_z = 0;
    int m_dir = 0;
    int m_p1 = 0;
    int m_p2 = 0;
    int m_p3 = 0;
    BaseSpawnDefine* m_pSpawnDefine = nullptr;
    int m_EventID = -1;
    int m_PrivateID = 0;
    int m_nDroidMasterCharSID = 0;
};

class CPartyPrivateCreatorInzoneEx : public CIOObject {
public:
    CPartyPrivateCreatorInzoneEx(
        CNPC* boss,
        int classId,
        const char16_t* aiName,
        int privateWeightPoint,
        int respawnTime,
        int x,
        int y,
        int z,
        int direction,
        int p1,
        int p2,
        int p3,
        int makerId,
        int inzoneUid,
        int droidMasterCharSid
    );
    ~CPartyPrivateCreatorInzoneEx() override;

    void TimerExpired(int timerId) override;

private:
    int m_nBossID = 0;
    int m_nClassID = 0;
    int m_nPrivateWeightPoint = 0;
    int m_nRespawnTime = 0;
    int m_maker_id = 0;
    std::u16string m_sAI;
    int m_x = 0;
    int m_y = 0;
    int m_z = 0;
    int m_dir = 0;
    int m_p1 = 0;
    int m_p2 = 0;
    int m_p3 = 0;
    BaseSpawnDefine* m_pSpawnDefine = nullptr;
    int m_EventID = -1;
    int m_PrivateID = 0;
    int m_InzoneUID = 0;
    std::uint32_t m_nDroidMasterCharSID = 0;
};

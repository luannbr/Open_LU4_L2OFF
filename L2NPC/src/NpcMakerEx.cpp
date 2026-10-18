#include "NpcMakerEx.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/TemplateHelper.h"

#include "AIManager.h"
#include "GlobalObject.h"
#include "MakerEventFactory.h"
#include "NPC.h"
#include "NPCServer.h"
#include "NpcEvent.h"
#include "NpcMakerDB.h"
#include "NpcSpawnDefineEx.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Runtime.h"
#include "ScriptTypeInfo.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/ResidenceData.h"
#include "Shared/src/SharedType.h"
#include "TextRuntime.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>
#include <string_view>

namespace l2npc {
namespace {


std::size_t RefTypeIndex(::ObjectRefType type) { return static_cast<std::size_t>(type); }

void AddScriptVariable(ScriptTypeInfo& typeInfo, const wchar_t* name, int offset, ScriptTypeId typeId) {
    auto* const variable = new ScriptVarInfo;
    variable->SetScriptVarName(name);
    variable->Offset(offset);
    variable->Type(&GetBuiltinScriptType(typeId));
    typeInfo.AddVarInfo(variable);
}

} // namespace

class NpcMakerEx::__async_arg_AsyncTimerExpired final : public CExecutionArgument {
public:
    explicit __async_arg_AsyncTimerExpired(int timerId) : id(timerId) {}

    void CopyToPad(CExecutionArgumentPad* destination) const override {
        std::memcpy(destination->Buffer, static_cast<const void*>(this), sizeof(*this));
    }

    int id = 0;
};


namespace {

// CExecutionSerializer stores an eight-byte MSVC member-function pointer in
// the Windows target. Some host ABIs use a wider representation, so this
// transport-only adapter enters the original NpcMakerEx member method.
void NpcMakerExAsyncTimerExpiredAdapter(NpcMakerEx* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncTimerExpired(argument);
}

} // namespace

void NpcMakerEx::__async_AsyncTimerExpired(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    int timerId = 0;
    std::memcpy(&timerId, argument->Buffer + 8, sizeof(timerId));
    if (timerId < 1) {
        L2_RELEASE(this, ::ObjectRefType::Anonymous);
        return;
    }

    MakerEvent* const event = MakerEventFactory::CreateTimerEventEx(this, timerId);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] "
        //     L"MakeEventFactory::CreateTimerEventEx failed.",
        //     L"NpcMakerEx.cpp",
        //     0x3b0)
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Cannot create timer event %d for maker '%s'. Skipping event delivery.",
        //     timerId, Utf16ToUtf8(GetName()).c_str())
        L2_LOG_ERROR(
            Log, "[NPC maker] Cannot create timer event %d for maker '%s'; skipping event delivery.", timerId,
            Utf16ToUtf8(GetName()).c_str()
        );
        L2_RELEASE(this, ::ObjectRefType::Anonymous);
        return;
    }

    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    L2_RELEASE(this, ::ObjectRefType::Anonymous);
}

NpcMakerEx::NpcMakerEx() : m_bSendIncreaseMakerExFailed(false), m_bSendDecreaseMakerExFailed(false) {
    L2_GUARD_FUNCTION();
    m_bDeleting = false;
    mEventHandler = nullptr;
    mDefineCount = 0;
    runtime::raw_atomic::StoreI32Relaxed(&mNpcCount, 0);
    mMaximumNPC = 0;
    i_quest0 = 0;
    i_quest1 = 0;
    i_quest2 = 0;
    i_quest3 = 0;
    i_quest4 = 0;
    i_quest5 = 0;
    i_quest6 = 0;
    i_quest7 = 0;
    i_quest8 = 0;
    i_quest9 = 0;
    i_ai0 = 0;
    i_ai1 = 0;
    i_ai2 = 0;
    i_ai3 = 0;
    i_ai4 = 0;
    i_ai5 = 0;
    i_ai6 = 0;
    i_ai7 = 0;
    i_ai8 = 0;
    i_ai9 = 0;
    c_ai0 = nullptr;
    c_ai1 = nullptr;
    c_ai2 = nullptr;
    c_ai3 = nullptr;
    c_ai4 = nullptr;
    m_IntList = &mIntList;
    enabled = 0;
    mDomainID = 0;
    runtime::raw_atomic::StoreI32Relaxed(&mMakerKeyFactory, 0);
    m_inzoneType.classID = 0;
    m_inzoneType.clusterID = 0;
    m_nInZoneID = 0;
    runtime::raw_atomic::StoreI32Relaxed(&m_nInZoneDelete, 0);
    mMaxTotalOfSpawnDefineEx = 0;
    std::memset(mName.data(), 0, sizeof(mName));
    m_bFlying = false;
    m_bNpcMakerExDebugOn = false;
    av_ai0 = &mAvAI0;
    av_ai1 = &mAvAI1;
    av_quest0 = &mAvQuest0;
    av_quest1 = &mAvQuest1;
    av_quest2 = &mAvQuest2;
    av_quest3 = &mAvQuest3;
    av_quest4 = &mAvQuest4;
    av_quest5 = &mAvQuest5;
    av_ai2 = &mAvAI2;
    av_ai3 = &mAvAI3;
    av_ai4 = &mAvAI4;
    av_ai5 = &mAvAI5;
}

NpcMakerEx::~NpcMakerEx() {
    // Current L2NPC.exe 0x004fc674 snapshots the pointer difference into a
    // signed 32-bit count before entering the loop, then reloads the vector
    // base for every indexed element. In particular, a wrapped/non-positive
    // count skips the releases instead of following a live end iterator.
    const int definitionCount = static_cast<int>(mSpawnDefineList.size());
    for (int index = 0; index < definitionCount; ++index) {
        NpcSpawnDefineEx* const definition = runtime::CheckedVectorAt(mSpawnDefineList, static_cast<std::size_t>(index));
        if (definition) {
            L2_RELEASE(definition, ::ObjectRefType::CreateDelete);
        }
    }
}

void NpcMakerEx::CollectFunc(ScriptTypeInfo* typeInfo) {
    ScriptTypeInfo& type = *typeInfo;
    ScriptTypeInfo& voidType = GetBuiltinScriptType(ScriptTypeId::Void);
    ScriptTypeInfo& intType = GetBuiltinScriptType(ScriptTypeId::Int);
    ScriptTypeInfo& stringType = GetBuiltinScriptType(ScriptTypeId::String);
    ScriptTypeInfo& spawnDefineType = GetBuiltinScriptType(ScriptTypeId::NpcSpawnDefineEx);

    // Current L2NPC.exe 0x004fdcfc. AddFuncInfo assigns the vector index
    // before insertion, so this source order is the native ABI order 0..24.
    type.AddFunction(L"GetSpawnDefine", &NpcMakerEx::GetSpawnDefine, &spawnDefineType, {&intType});
    type.AddFunction(L"GetSpawnDefineByNick", &NpcMakerEx::GetSpawnDefineByNick, &spawnDefineType, {&stringType});
    type.AddFunction(L"AddTimerEx", &NpcMakerEx::AddTimerEx, &voidType, {&intType, &intType});
    type.AddFunction(L"RegisterNpcPosEvent", &NpcMakerEx::RegisterNpcPosEvent, &intType, {&stringType});
    type.AddFunction(L"RegisterDoorEvent", &NpcMakerEx::RegisterDoorEvent, &intType, {&stringType, &intType});
    type.AddFunction(L"RegisterSiegeEventEx", &NpcMakerEx::RegisterSiegeEventEx, &intType, {&intType});
    type.AddFunction(L"RegisterAgitSiegeEventEx", &NpcMakerEx::RegisterAgitSiegeEventEx, &intType, {&intType});
    type.AddFunction(L"RegisterRespawn", &NpcMakerEx::RegisterRespawn, &voidType, {&intType, &intType, &spawnDefineType});
    type.AddFunction(L"DoRespawn", &NpcMakerEx::DoRespawn, &voidType, {});
    type.AddFunction(L"ResetRespawn", &NpcMakerEx::ResetRespawn, &voidType, {});
    type.AddFunction(
        L"AtomicIncreaseTotal", &NpcMakerEx::AtomicIncreaseTotal, &intType, {&spawnDefineType, &intType, &intType}
    );
    type.AddFunction(L"RegisterFieldCycleEventEx", &NpcMakerEx::RegisterFieldCycleEventEx, &voidType, {&intType});
    type.AddFunction(
        L"RegisterFortressEventEx", &NpcMakerEx::RegisterFortressEventEx, &voidType, {&intType, &intType, &intType}
    );
    type.AddFunction(
        L"RegisterInstantZoneEventEx", &NpcMakerEx::RegisterInstantZoneEventEx, &voidType,
        {&intType, &intType, &intType, &intType}
    );
    type.AddFunction(L"RegisterOlympiadFieldEventEx", &NpcMakerEx::RegisterOlympiadFieldEventEx, &voidType, {});
    type.AddFunction(L"RegisterOlympiadZoneEventEx", &NpcMakerEx::RegisterOlympiadZoneEventEx, &voidType, {&intType});
    type.AddFunction(L"GetInZoneID", &NpcMakerEx::GetInZoneID, &intType, {});
    type.AddFunction(L"RegistCheckPCDie", &NpcMakerEx::RegistCheckPCDie, &voidType, {});
    type.AddFunction(L"Residence_GetCastleType", &NpcMakerEx::Residence_GetCastleType, &intType, {&intType});
    type.AddFunction(L"RegisterPledgeGameFieldEventEx", &NpcMakerEx::RegisterPledgeGameFieldEventEx, &voidType, {});
    type.AddFunction(L"RegisterPledgeGameZoneEventEx", &NpcMakerEx::RegisterPledgeGameZoneEventEx, &voidType, {&intType});
    type.AddFunction(
        L"RegisterInstantZoneNotifySubscriber", &NpcMakerEx::RegisterInstantZoneNotifySubscriber, &voidType, {}
    );
    type.AddFunction(
        L"UnregisterInstantZoneNotifySubscriber", &NpcMakerEx::UnregisterInstantZoneNotifySubscriber, &voidType, {}
    );
    type.AddFunction(
        L"NotifyToInstantZoneMaker", &NpcMakerEx::NotifyToInstantZoneMaker, &voidType,
        {&intType, &intType, &intType, &intType}
    );
    type.AddFunction(L"RegisterHunterGuildBless", &NpcMakerEx::RegisterHunterGuildBless, &voidType, {});
}

void NpcMakerEx::CollectVar(ScriptTypeInfo* typeInfo) {
    ScriptTypeInfo& type = *typeInfo;

    // Current L2NPC.exe 0x004fceb0. Offsets are relative to the complete
    // target object even though this virtual is reached through the
    // ScriptAccessibleEx secondary base at +0x18. Native STL storage changes
    // host member positions, so script bindings follow the actual fields.
    AddScriptVariable(
        type, L"name", static_cast<int>(offsetof(NpcMakerEx, mName)), // shipping 0x4a2
        ScriptTypeId::String
    );
    AddScriptVariable(
        type, L"def_count", static_cast<int>(offsetof(NpcMakerEx, mDefineCount)), // shipping 0x504
        ScriptTypeId::Int
    );
    AddScriptVariable(
        type, L"maximum_npc", static_cast<int>(offsetof(NpcMakerEx, mMaximumNPC)), // shipping 0x50c
        ScriptTypeId::Int
    );
    AddScriptVariable(
        type, L"npc_count", static_cast<int>(offsetof(NpcMakerEx, mNpcCount)), // shipping 0x508
        ScriptTypeId::Int
    );
    AddScriptVariable(
        type, L"inzone_type_id",
        static_cast<int>(offsetof(NpcMakerEx, m_inzoneType) + offsetof(InzoneTypeKey, classID)), // shipping 0x1530
        ScriptTypeId::Int
    );
    AddScriptVariable(
        type, L"inzone_cluster_id",
        static_cast<int>(offsetof(NpcMakerEx, m_inzoneType) + offsetof(InzoneTypeKey, clusterID)), // shipping 0x1534
        ScriptTypeId::Int
    );
    AddScriptVariable(
        type, L"inzone_id", static_cast<int>(offsetof(NpcMakerEx, m_nInZoneID)), // shipping 0x1538
        ScriptTypeId::Int
    );

    constexpr std::array<const wchar_t*, 10> questNames{{
        L"i_quest0",
        L"i_quest1",
        L"i_quest2",
        L"i_quest3",
        L"i_quest4",
        L"i_quest5",
        L"i_quest6",
        L"i_quest7",
        L"i_quest8",
        L"i_quest9",
    }};
    for (std::size_t index = 0; index < questNames.size(); ++index) {
        AddScriptVariable(
            type, questNames[index],
            static_cast<int>(offsetof(NpcMakerEx, i_quest0) + index * sizeof(i_quest0)) /* shipping 0x510 + index * 8 */,
            ScriptTypeId::Int64
        );
    }

    constexpr std::array<const wchar_t*, 10> aiNames{{
        L"i_ai0",
        L"i_ai1",
        L"i_ai2",
        L"i_ai3",
        L"i_ai4",
        L"i_ai5",
        L"i_ai6",
        L"i_ai7",
        L"i_ai8",
        L"i_ai9",
    }};
    for (std::size_t index = 0; index < aiNames.size(); ++index) {
        AddScriptVariable(
            type, aiNames[index],
            static_cast<int>(offsetof(NpcMakerEx, i_ai0) + index * sizeof(i_ai0)) /* shipping 0x560 + index * 4 */,
            ScriptTypeId::Int
        );
    }

    AddScriptVariable(
        type, L"enabled", static_cast<int>(offsetof(NpcMakerEx, enabled)), // shipping 0x5f8
        ScriptTypeId::Int
    );
    AddScriptVariable(
        type, L"int_list", static_cast<int>(offsetof(NpcMakerEx, m_IntList)), // shipping 0x5f0
        ScriptTypeId::IntList
    );

    constexpr std::array<const wchar_t*, 5> creatureNames{{
        L"c_ai0",
        L"c_ai1",
        L"c_ai2",
        L"c_ai3",
        L"c_ai4",
    }};
    for (std::size_t index = 0; index < creatureNames.size(); ++index) {
        AddScriptVariable(
            type, creatureNames[index],
            static_cast<int>(offsetof(NpcMakerEx, c_ai0) + index * sizeof(c_ai0)) /* shipping 0x588 + index * 8 */,
            ScriptTypeId::Creature
        );
    }

    constexpr std::array<std::pair<const wchar_t*, int>, 12> atomicVariables{{
        {L"av_ai0", static_cast<int>(offsetof(NpcMakerEx, av_ai0))} /* shipping 0x610 */,
        {L"av_ai1", static_cast<int>(offsetof(NpcMakerEx, av_ai1))} /* shipping 0x628 */,
        {L"av_ai2", static_cast<int>(offsetof(NpcMakerEx, av_ai2))} /* shipping 0x700 */,
        {L"av_ai3", static_cast<int>(offsetof(NpcMakerEx, av_ai3))} /* shipping 0x708 */,
        {L"av_ai4", static_cast<int>(offsetof(NpcMakerEx, av_ai4))} /* shipping 0x710 */,
        {L"av_ai5", static_cast<int>(offsetof(NpcMakerEx, av_ai5))} /* shipping 0x718 */,
        {L"av_quest0", static_cast<int>(offsetof(NpcMakerEx, av_quest0))} /* shipping 0x640 */,
        {L"av_quest1", static_cast<int>(offsetof(NpcMakerEx, av_quest1))} /* shipping 0x658 */,
        {L"av_quest2", static_cast<int>(offsetof(NpcMakerEx, av_quest2))} /* shipping 0x6e0 */,
        {L"av_quest3", static_cast<int>(offsetof(NpcMakerEx, av_quest3))} /* shipping 0x6e8 */,
        {L"av_quest4", static_cast<int>(offsetof(NpcMakerEx, av_quest4))} /* shipping 0x6f0 */,
        {L"av_quest5", static_cast<int>(offsetof(NpcMakerEx, av_quest5))} /* shipping 0x6f8 */,
    }};
    for (const auto& entry : atomicVariables) {
        AddScriptVariable(type, entry.first, entry.second, ScriptTypeId::AtomicValueMaker);
    }
    const std::pair<const wchar_t*, int> compiledFields[] = {
        {L"name", 0x4a2},
        {L"def_count", 0x504},
        {L"maximum_npc", 0x50c},
        {L"npc_count", 0x508},
        {L"inzone_type_id", 0x1530},
        {L"inzone_cluster_id", 0x1534},
        {L"inzone_id", 0x1538},
        {L"enabled", 0x5f8},
        {L"int_list", 0x5f0},
        {L"i_quest0", 0x510},
        {L"i_quest1", 0x518},
        {L"i_quest2", 0x520},
        {L"i_quest3", 0x528},
        {L"i_quest4", 0x530},
        {L"i_quest5", 0x538},
        {L"i_quest6", 0x540},
        {L"i_quest7", 0x548},
        {L"i_quest8", 0x550},
        {L"i_quest9", 0x558},
        {L"i_ai0", 0x560},
        {L"i_ai1", 0x564},
        {L"i_ai2", 0x568},
        {L"i_ai3", 0x56c},
        {L"i_ai4", 0x570},
        {L"i_ai5", 0x574},
        {L"i_ai6", 0x578},
        {L"i_ai7", 0x57c},
        {L"i_ai8", 0x580},
        {L"i_ai9", 0x584},
        {L"c_ai0", 0x588},
        {L"c_ai1", 0x590},
        {L"c_ai2", 0x598},
        {L"c_ai3", 0x5a0},
        {L"c_ai4", 0x5a8},
        {L"av_ai0", 0x610},
        {L"av_ai1", 0x628},
        {L"av_ai2", 0x700},
        {L"av_ai3", 0x708},
        {L"av_ai4", 0x710},
        {L"av_ai5", 0x718},
        {L"av_quest0", 0x640},
        {L"av_quest1", 0x658},
        {L"av_quest2", 0x6e0},
        {L"av_quest3", 0x6e8},
        {L"av_quest4", 0x6f0},
        {L"av_quest5", 0x6f8},
    };
    for (const auto& [name, offset] : compiledFields) {
        type.SetCompiledOffset(name, offset);
    }
}

bool NpcMakerEx::IsNPCMakerExInScript() const { return true; }

NpcMakerEx* NpcMakerEx::CastNPCMakerExInScript() { return this; }

ScriptAccessibleEx* NpcMakerEx::GetScriptAccessibleEx() const { return const_cast<NpcMakerEx*>(this); }

void NpcMakerEx::SetName(const char16_t* value) {
    // wcscpy_s clears the destination on a null source before invoking the
    // target's invalid-parameter path.
    if (!value) {
        mName[0] = u'\0';
        myInvalidParameterHandler(L"(L\"Invalid string pointer\" && 0)", L"wcscpy_s", nullptr, 0, 0);
        return;
    }
    const std::size_t length = std::char_traits<char16_t>::length(value);
    if (length >= mName.size()) {
        mName[0] = u'\0';
        myInvalidParameterHandler(L"(L\"Buffer is too small\" && 0)", L"wcscpy_s", nullptr, 0, 0);
        return;
    }
    std::copy_n(value, length + 1, mName.begin());
}

const char16_t* NpcMakerEx::GetName() const noexcept { return mName.data(); }

void NpcMakerEx::SetMaximumNPC(int maximumNpc) { mMaximumNPC = maximumNpc; }

int NpcMakerEx::GetMaximumNPC() const noexcept { return mMaximumNPC; }

int NpcMakerEx::GetDefineCount() const noexcept { return mDefineCount; }

int NpcMakerEx::GetTotalNPC() const noexcept { return runtime::raw_atomic::LoadI32Relaxed(&mNpcCount); }

void NpcMakerEx::SetEnabled(int value) { enabled = value; }

int NpcMakerEx::GetEnabled() const noexcept { return enabled; }

void NpcMakerEx::SetDomainID(int domainId) { mDomainID = domainId; }

int NpcMakerEx::GetDomainID() const noexcept { return mDomainID; }

void NpcMakerEx::SetFlying(bool flying) { m_bFlying = flying; }

bool NpcMakerEx::IsFlying() const noexcept { return m_bFlying; }

bool NpcMakerEx::IsDeleting() const noexcept { return m_bDeleting; }

int NpcMakerEx::GetInZoneDeleteCount() const noexcept { return runtime::raw_atomic::LoadI32Relaxed(&m_nInZoneDelete); }

int NpcMakerEx::IncreaseInZoneDelete() { return runtime::raw_atomic::IncrementI32(&m_nInZoneDelete); }

unsigned int NpcMakerEx::GetInZoneID() const noexcept { return m_nInZoneID; }

unsigned int NpcMakerEx::GetInZoneClassID() const noexcept { return m_inzoneType.classID; }

unsigned int NpcMakerEx::GetInZoneClusterID() const noexcept { return m_inzoneType.clusterID; }

void NpcMakerEx::SetInZoneID(unsigned int inzoneId) { m_nInZoneID = inzoneId; }

void NpcMakerEx::SetInZoneType(unsigned int classId, unsigned int clusterId) {
    m_inzoneType.classID = classId;
    m_inzoneType.clusterID = clusterId;
}

int NpcMakerEx::CreateMakerKey() { return runtime::raw_atomic::IncrementI32(&mMakerKeyFactory); }

bool NpcMakerEx::RegisterNpc(CNPC* npc) {
    L2_GUARD_FUNCTION();
    if (!npc) {
        return false;
    }
    if (GetInZoneID() != 0 && runtime::raw_atomic::LoadI32Relaxed(&m_nInZoneDelete) != 0) {
        return false;
    }
    if (npc->m_nMakerID > 0) {
        {
            L2_GUARD(mLock);
            mKeyToNpcMap[npc->m_nMakerID] = static_cast<int>(npc->Id());
        }
    }
    return true;
}

void NpcMakerEx::UnregisterNpc(CNPC* npc) {
    L2_GUARD_FUNCTION();
    if (npc->m_nMakerID <= 0) {
        return;
    }
    {
        L2_GUARD(mLock);
        mKeyToNpcMap.erase(npc->m_nMakerID);
    }
}

CSharedCreatureData* NpcMakerEx::GetNpcFromKey(int key) const {
    L2_GUARD_FUNCTION();
    if (key < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RegisterMakerObject failed. "
        //     L"maker_id <=0",
        //     L"NpcMakerEx.cpp",
        //     0x344)
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Cannot look up NPC key %d for maker '%s': keys must be positive. Returning no NPC.",
        //     key, Utf16ToUtf8(GetName()).c_str())
        L2_LOG_ERROR(
            Log, "[NPC maker] Cannot look up NPC key %d for maker '%s': keys must be positive; returning no NPC.", key,
            Utf16ToUtf8(GetName()).c_str()
        );
        return nullptr;
    }

    int objectId = 0;
    auto& lock = const_cast<CIOCriticalSection&>(mLock);
    {
        L2_GUARD(lock);
        const auto found = mKeyToNpcMap.find(key);
        if (found != mKeyToNpcMap.end()) {
            objectId = found->second;
        }
    }
    CNPC* const npc = CNPC::GetObjectW(objectId);
    return npc ? npc->m_data : nullptr;
}

TerritoryInfo* NpcMakerEx::GetTerritoryInfo() noexcept { return &mTerritoryInfo; }

const TerritoryInfo* NpcMakerEx::GetTerritoryInfo() const noexcept { return &mTerritoryInfo; }

void NpcMakerEx::AddBannedTerritory(CTerritory* territory) {
    L2_GUARD_FUNCTION();
    mTerritoryInfo.AddBannedTerritory(territory);
}

void NpcMakerEx::AddTerritory(CTerritory* territory) {
    L2_GUARD_FUNCTION();
    mTerritoryInfo.AddTerritory(territory);
}

void NpcMakerEx::SetAI(const char16_t* className) {
    L2_GUARD_FUNCTION();
    mAI = std::u16string(className);

    CReloadableNPCEventHandler* const handler = CNPCAIManager::FindClass(1, className);
    if (!handler) {
        const std::string classNameWide = Utf16ToUtf8(className);
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot find AI class[%s]",
        //     classNameWide.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] AI class '%s' is missing for maker '%s'. Detaching inherited parameters and leaving the previous
        //     handler unchanged.", WideToUtf8(classNameWide).c_str(), Utf16ToUtf8(GetName()).c_str())
        L2_LOG_ERROR(
            Log,
            "[NPC maker] AI class '%s' is missing for maker '%s'; detaching inherited parameters and leaving the previous "
            "handler unchanged.",
            classNameWide.c_str(), Utf16ToUtf8(GetName()).c_str()
        );
        // Current EXE 0x004fb9cc clears only the first qword of
        // mAIParam. It leaves an earlier handler pointer.
        mAIParam.m_pSuper = nullptr;
        return;
    }

    mEventHandler = handler;
    mAIParam.m_pSuper = handler->GetParameter();
}

bool NpcMakerEx::SetParameter(const char16_t* name, const char16_t* valueText) {
    L2_GUARD_FUNCTION();
    if (!mEventHandler) {
        const std::string makerName = Utf16ToUtf8(GetName());
        const std::string parameterName = Utf16ToUtf8(name);
        // Log.Add(
        //     LOG_ERROR,
        //     L"Maker[%s] doesn't have ai handler. "
        //     L"cannot set the ai parameter[%s]",
        //     makerName.c_str(),
        //     parameterName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Cannot set parameter '%s' on maker '%s': no AI handler is bound.",
        //     WideToUtf8(parameterName).c_str(), WideToUtf8(makerName).c_str())
        L2_LOG_ERROR(
            Log, "[NPC maker] Cannot set parameter '%s' on maker '%s': no AI handler is bound.",
            parameterName.c_str(), makerName.c_str()
        );
        return false;
    }

    AIConstantData value = mEventHandler->GetParameterData(name);
    if (!value.type) {
        const std::string makerName = Utf16ToUtf8(GetName());
        const std::string parameterName = Utf16ToUtf8(name);
        // Log.Add(
        //     LOG_ERROR,
        //     L"Maker[%s] doesn't have ai handler. "
        //     L"cannot set the ai parameter[%s]",
        //     makerName.c_str(),
        //     parameterName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Cannot set parameter '%s' on maker '%s': the parameter has no declared type.",
        //     WideToUtf8(parameterName).c_str(), WideToUtf8(makerName).c_str())
        L2_LOG_ERROR(
            Log, "[NPC maker] Cannot set parameter '%s' on maker '%s': the parameter has no declared type.",
            parameterName.c_str(), makerName.c_str()
        );
        return false;
    }

    if (value.type == &GetBuiltinScriptType(ScriptTypeId::Int)) {
        const std::wstring valueWide = Utf16ToWide(valueText);
        value.data.intData = std::wcstoll(valueWide.c_str(), nullptr, 10);
    } else if (value.type == &GetBuiltinScriptType(ScriptTypeId::Float)) {
        const std::wstring valueWide = Utf16ToWide(valueText);
        value.data.floatData = static_cast<float>(std::wcstod(valueWide.c_str(), nullptr));
    } else if (value.type == &GetBuiltinScriptType(ScriptTypeId::String)) {
        const std::size_t length = std::char_traits<char16_t>::length(valueText);
        char16_t* const copy = static_cast<char16_t*>(std::calloc(length + 1, sizeof(char16_t)));
        if (copy) {
            std::memcpy(copy, valueText, (length + 1) * sizeof(char16_t));
        }
        // The target's UTF-16 duplication helper returns this raw allocation
        // directly. AIConstantData is a trivial pointer carrier and neither
        // the map nor CAIParameter owns or deep-copies it.
        value.data.stringData = copy;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Unknown type.",
        //     L"NpcMakerEx.cpp",
        //     0x14b)
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Cannot set parameter '%s' on maker '%s': type '%s' is unsupported. Keeping its previous value.",
        //     Utf16ToUtf8(name).c_str(), Utf16ToUtf8(GetName()).c_str(),
        //     WideToUtf8(value.type->GetScriptTypeName()).c_str())
        L2_LOG_ERROR(
            Log,
            "[NPC maker] Cannot set parameter '%s' on maker '%s': type '%s' is unsupported; keeping its previous value.",
            Utf16ToUtf8(name).c_str(), Utf16ToUtf8(GetName()).c_str(), WideToUtf8(value.type->GetScriptTypeName()).c_str()
        );
        return false;
    }
    mAIParam.m_mapData.insert_or_assign(std::u16string(name), value);

    // The shipping epilogue masks the low byte of the guard result, so the
    // declared bool remains false even after a successful assignment.
    return false;
}

void NpcMakerEx::SetAIParam(const CAIParameter& parameter) {
    L2_GUARD_FUNCTION();
    mAIParam.Copy(parameter);
}

bool NpcMakerEx::InitializeRandomPos() { return mTerritoryInfo.InitializeRandomPos(m_bFlying, mMaxTotalOfSpawnDefineEx); }

::MemoryObject* NpcMakerEx::AddRef(const char* callerFile, int callerLine, ::ObjectRefType type) {
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);

    ::RefHelperStruct& record = m_RefHelpers.m_RefData[RefTypeIndex(type)];
    record.m_nRefCount.fetch_add(1, std::memory_order_seq_cst);
    record.m_pAddCaller = callerFile;
    record.m_nAddLine = callerLine;
    return this;
}

std::int32_t NpcMakerEx::Release(const char* callerFile, int callerLine, ::ObjectRefType type, bool) {
    ::RefHelperStruct& record = m_RefHelpers.m_RefData[RefTypeIndex(type)];
    const std::int32_t subtypeRemaining = record.m_nRefCount.fetch_sub(1, std::memory_order_seq_cst) - 1;
    if (subtypeRemaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]  sub-type[%s] "
        //     L"sub-reference count[%d] is below zero!",
        //     callerFile,
        //     callerLine,
        //     ::ObjectRefTypeName(type),
        //     subtypeRemaining)
        // Log.Add(LOG_ERROR,
        //     "[references] NpcMakerEx object %d: release at %s:%d made %s count negative (%d). Observed count now %d, last
        //     retain %s:%d, previous release %s:%d.", MemoryObjectID(), callerFile ? callerFile : "(unknown)", callerLine,
        //     WideToUtf8(::ObjectRefTypeName(type)).c_str(), subtypeRemaining,
        //     record.m_nRefCount.load(std::memory_order_relaxed),
        //     record.m_pAddCaller ? record.m_pAddCaller : "(unknown)", record.m_nAddLine,
        //     record.m_pReleaseCaller ? record.m_pReleaseCaller : "(unknown)", record.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            "[references] NpcMakerEx object %d: release at %s:%d made %s count negative (%d); observed count now %d, last "
            "retain %s:%d, previous release %s:%d.",
            MemoryObjectID(), callerFile ? callerFile : "(unknown)", callerLine,
            WideToUtf8(::ObjectRefTypeName(type)).c_str(), subtypeRemaining,
            record.m_nRefCount.load(std::memory_order_relaxed), record.m_pAddCaller ? record.m_pAddCaller : "(unknown)",
            record.m_nAddLine, record.m_pReleaseCaller ? record.m_pReleaseCaller : "(unknown)", record.m_nReleaseLine
        );
        // Noise: Previous keep/release context is included in the preceding reference-count error.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%d]-[%s]  last_add[%S][%d] "
        //     L"last_release[%S][%d]",
        //     record.m_nRefCount.load(
        //         std::memory_order_relaxed),
        //     ::ObjectRefTypeName(type),
        //     record.m_pAddCaller,
        //     record.m_nAddLine,
        //     record.m_pReleaseCaller,
        //     record.m_nReleaseLine)
        record.m_pReleaseCaller = callerFile;
        record.m_nReleaseLine = callerLine;
        ReportCallStack(LOG_ERROR, false);
        ReportOrt("NpcMakerEx.cpp", 0x31);
    } else {
        record.m_pReleaseCaller = callerFile;
        record.m_nReleaseLine = callerLine;
    }

    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            DestroyReleasedObject();
        } else {
            if (callerFile) {
                // Log.Add(
                //     LOG_ERROR,
                //     "deleting in-timer-object!"
                //     "[NpcMakerEx] at file[%s], line[%d]",
                //     callerFile,
                //     callerLine)
                // Log.Add(LOG_ERROR,
                //     "[references] NpcMakerEx object %d reached zero references while a timer is active. Object destruction
                //     is skipped. Release requested at %s:%d.", MemoryObjectID(), callerFile, callerLine)
                L2_LOG_ERROR(
                    Log,
                    "[references] NpcMakerEx object %d reached zero references while a timer is active; object destruction "
                    "is skipped. Release requested at %s:%d.",
                    MemoryObjectID(), callerFile, callerLine
                );
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     "deleting in-timer-object!"
                //     "[NpcMakerEx]")
                // Log.Add(LOG_ERROR,
                //     "[references] NpcMakerEx object %d reached zero references while a timer is active. Object destruction
                //     is skipped. Release caller is unknown.", MemoryObjectID())
                L2_LOG_ERROR(
                    Log,
                    "[references] NpcMakerEx object %d reached zero references while a timer is active; object destruction "
                    "is skipped. Release caller is unknown.",
                    MemoryObjectID()
                );
            }
            ReportOrt("NpcMakerEx.cpp", 0x31);
        }
        ReleaseClassInstance(L2_MEMORY_CLASS_ID(NpcMakerEx));
    } else if (remaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Invalid Memory Ex(%d) on "
        //     "NpcMakerEx(%d)",
        //     remaining,
        //     MemoryObjectID())
        // Log.Add(LOG_ERROR,
        //     "[references] NpcMakerEx object %d: total reference count is negative (%d) after release at %s:%d.",
        //     MemoryObjectID(), remaining, callerFile ? callerFile : "(unknown)", callerLine)
        L2_LOG_ERROR(
            Log, "[references] NpcMakerEx object %d: total reference count is negative (%d) after release at %s:%d.",
            MemoryObjectID(), remaining, callerFile ? callerFile : "(unknown)", callerLine
        );
        if (callerFile) {
            // Noise: Caller context is included in the preceding total-reference-count error.
            // Log.Add(
            //     LOG_ERROR,
            //     "           Caller : file[%s], line[%d]",
            //     callerFile,
            //     callerLine)
        }
        ReportOrt("NpcMakerEx.cpp", 0x31);
        ReportCallStack(LOG_ERROR, true);
    }
    return remaining;
}

void NpcMakerEx::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    thread_local std::uint32_t reportTick = 0;
    ++reportTick;
    // const int threadId =
    //     CThreadManager::GetThreadID()
    int sequence = 1;

    if (callerFile) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ORT Report][tid:%d][tick:%d][%d] "
        //     L"begin called from file[%S], line[%d] "
        //     L"-------------",
        //     threadId,
        //     static_cast<int>(reportTick),
        //     sequence,
        //     callerFile,
        //     callerLine)
        // Log.Add(LOG_ERROR,
        //     "[references] NpcMakerEx object %d: begin reference report %u, requested at %s:%d.",
        //     MemoryObjectID(), static_cast<unsigned int>(reportTick), callerFile, callerLine)
        L2_LOG_ERROR(
            Log, "[references] NpcMakerEx object %d: begin reference report %u, requested at %s:%d.", MemoryObjectID(),
            static_cast<unsigned int>(reportTick), callerFile, callerLine
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] "
        //     "begin ---------------------------",
        //     threadId,
        //     static_cast<int>(reportTick),
        //     sequence)
        // Log.Add(LOG_ERROR,
        //     "[references] NpcMakerEx object %d: begin reference report %u. Requester unknown.",
        //     MemoryObjectID(), static_cast<unsigned int>(reportTick))
        L2_LOG_ERROR(
            Log, "[references] NpcMakerEx object %d: begin reference report %u; requester unknown.", MemoryObjectID(),
            static_cast<unsigned int>(reportTick)
        );
    }

    for (std::size_t index = 0; index < ::RefHelpers::kCount; ++index) {
        const ::RefHelperStruct& record = m_RefHelpers.m_RefData[index];
        if (!record.m_pAddCaller || !record.m_pReleaseCaller || record.m_nRefCount.load(std::memory_order_relaxed) == 0) {
            continue;
        }

        ++sequence;
        const wchar_t* const typeName = ::ObjectRefTypeName(static_cast<::ObjectRefType>(index));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ORT Report][tid:%d][tick:%d][%d] "
        //     L"[RefCount:%d]-[%s]  last_add[%S][%d] "
        //     L"last_release[%S][%d]",
        //     threadId,
        //     static_cast<int>(reportTick),
        //     sequence,
        //     record.m_nRefCount.load(
        //         std::memory_order_relaxed),
        //     typeName,
        //     record.m_pAddCaller,
        //     record.m_nAddLine,
        //     record.m_pReleaseCaller,
        //     record.m_nReleaseLine)
        // Log.Add(LOG_ERROR,
        //     "[references] NpcMakerEx object %d, report %u, row %d: %s count %d. Last retain %s:%d, last release %s:%d.",
        //     MemoryObjectID(), static_cast<unsigned int>(reportTick), sequence,
        //     WideToUtf8(typeName).c_str(),
        //     record.m_nRefCount.load(std::memory_order_relaxed),
        //     record.m_pAddCaller, record.m_nAddLine, record.m_pReleaseCaller, record.m_nReleaseLine)
        L2_LOG_ERROR(
            Log, "[references] NpcMakerEx object %d, report %u, row %d: %s count %d; last retain %s:%d, last release %s:%d.",
            MemoryObjectID(), static_cast<unsigned int>(reportTick), sequence, WideToUtf8(typeName).c_str(),
            record.m_nRefCount.load(std::memory_order_relaxed), record.m_pAddCaller, record.m_nAddLine,
            record.m_pReleaseCaller, record.m_nReleaseLine
        );
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "[ORT Report][tid:%d][tick:%d][%d] "
    //     "end -----------------------------",
    //     threadId,
    //     static_cast<int>(reportTick),
    //     sequence + 1)
    // Log.Add(LOG_ERROR,
    //     "[references] NpcMakerEx object %d: end reference report %u (%d reference rows).",
    //     MemoryObjectID(), static_cast<unsigned int>(reportTick), sequence - 1)
    L2_LOG_ERROR(
        Log, "[references] NpcMakerEx object %d: end reference report %u (%d reference rows).", MemoryObjectID(),
        static_cast<unsigned int>(reportTick), sequence - 1
    );
}

void NpcMakerEx::AddTimerEx(int timerId, int milliseconds) {
    L2_GUARD_FUNCTION();
    if (timerId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] "
        //     L"NpcMakerEx::AddTimerEx failed. "
        //     L"assert(id<=0), id[%d].",
        //     L"NpcMakerEx.cpp",
        //     0x380,
        //     timerId)
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Rejecting timer %d for maker '%s': timer IDs must be positive.",
        //     timerId, Utf16ToUtf8(GetName()).c_str())
        L2_LOG_ERROR(
            Log, "[NPC maker] Rejecting timer %d for maker '%s': timer IDs must be positive.", timerId,
            Utf16ToUtf8(GetName()).c_str()
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    if (milliseconds < 1) {
        const std::string makerName = Utf16ToUtf8(GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] "
        //     L"NpcMakerEx::AddTimerEx failed. "
        //     L"assert(msec<=0). npc=[%s]",
        //     L"NpcMakerEx.cpp",
        //     0x38a,
        //     makerName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Rejecting timer %d for maker '%s': delay %d ms must be positive.",
        //     timerId, WideToUtf8(makerName).c_str(), milliseconds)
        L2_LOG_ERROR(
            Log, "[NPC maker] Rejecting timer %d for maker '%s': delay %d ms must be positive.", timerId,
            makerName.c_str(), milliseconds
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }

    CIOObject::AddTimer(static_cast<std::uint32_t>(milliseconds), timerId);
}

void NpcMakerEx::TimerExpired(int timerId) {
    if (m_bDeleting) {
        return;
    }

    L2_ADD_REF(this, ::ObjectRefType::Anonymous);
    __async_arg_AsyncTimerExpired argument(timerId);
    Serializer.AddExecutable(this, &NpcMakerExAsyncTimerExpiredAdapter, &argument, L"AsyncTimerExpired");
}

CNpcMakerExReleaseVerifier::CNpcMakerExReleaseVerifier(NpcMakerEx* maker) : m_pNpcMakerEx(maker) {
    L2_ADD_REF(m_pNpcMakerEx, ::ObjectRefType::VerifierRetain);
    AddTimer(30000U, 0);
    L2_RELEASE(this, ::ObjectRefType::CreateDelete);
}

L2_DEFINE_OBJECT_ADD_REF(CNpcMakerExReleaseVerifier)

L2_DEFINE_OBJECT_RELEASE(CNpcMakerExReleaseVerifier)

void CNpcMakerExReleaseVerifier::TimerExpired(int) {
    NpcMakerEx* loopMaker = m_pNpcMakerEx;
    auto definition = loopMaker->mSpawnDefineList.begin();
    while (definition != loopMaker->mSpawnDefineList.end()) {
        if (*definition) {
            (*definition)->SetNpcMakerEx(nullptr);
        }
        ++definition;
        loopMaker = m_pNpcMakerEx;
    }

    const std::int32_t remaining = L2_RELEASE(m_pNpcMakerEx, ::ObjectRefType::VerifierRetain);
    if (remaining > 0) {
        NpcMakerEx* const maker = m_pNpcMakerEx;
        const std::string makerName = Utf16ToUtf8(maker->GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ORT - "
        //     L"NpcMakerEx Release failed. "
        //     L"refcount[%d] name[%s] classid[%d]",
        //     L"NpcMakerEx.cpp",
        //     0x9e,
        //     maker->RefCount(),
        //     makerName.c_str(),
        //     maker->m_inzoneType.classID)
        // Log.Add(LOG_WARN,
        //     "[references] Maker '%s' still has references after the release verifier ran: observed count %d, instance-zone
        //     type %u. Reporting retained references.", WideToUtf8(makerName).c_str(), maker->RefCount(),
        //     static_cast<unsigned int>(maker->m_inzoneType.classID))
        L2_LOG_WARN(
            Log,
            "[references] Maker '%s' still has references after the release verifier ran: observed count %d, instance-zone "
            "type %u; reporting retained references.",
            makerName.c_str(), maker->RefCount(), static_cast<unsigned int>(maker->m_inzoneType.classID)
        );
        m_pNpcMakerEx->ReportOrt(nullptr, 0);
    }
}

NpcRespawnInfo::NpcRespawnInfo(std::uint32_t availableTime, int spawnCount, NpcSpawnDefineEx* spawnDefine) :
    m_nAvailableTime(availableTime), m_nSpawnCount(spawnCount), m_pSpawnDefine(spawnDefine) {
    if (m_pSpawnDefine) {
        L2_ADD_REF(m_pSpawnDefine, ::ObjectRefType::Anonymous);
    }
}

NpcRespawnInfo::~NpcRespawnInfo() {
    if (m_pSpawnDefine) {
        L2_RELEASE(m_pSpawnDefine, ::ObjectRefType::Anonymous);
        m_pSpawnDefine = nullptr;
    }
}

L2_DEFINE_OBJECT_ADD_REF(NpcRespawnInfo)

L2_DEFINE_OBJECT_RELEASE(NpcRespawnInfo)

bool NpcRespawnInfo::operator<(const NpcRespawnInfo& other) const { return m_nAvailableTime < other.m_nAvailableTime; }

bool NpcMakerEx::lessRespawn::operator()(NpcRespawnInfo* left, NpcRespawnInfo* right) const { return *right < *left; }

void NpcMakerEx::AddSpawnDefineEx(NpcSpawnDefineEx* spawnDefine) {
    L2_GUARD_FUNCTION();
    spawnDefine->SetNpcMakerEx(this);
    mSpawnDefineList.push_back(spawnDefine);
    const std::uint32_t defineCountBits = static_cast<std::uint32_t>(mDefineCount) + 1U;
    std::memcpy(&mDefineCount, &defineCountBits, sizeof(mDefineCount));
    if (!spawnDefine->GetNickName().empty()) {
        mNpcSpawnDefineMap[spawnDefine->GetNickName()] = spawnDefine;
    }
    mMaxTotalOfSpawnDefineEx = std::max(mMaxTotalOfSpawnDefineEx, spawnDefine->GetTotal());
}

NpcSpawnDefineEx* NpcMakerEx::GetSpawnDefine(int index) const {
    L2_GUARD_FUNCTION();
    if (index < 0 || index >= static_cast<int>(mSpawnDefineList.size())) {
        return nullptr;
    }
    return runtime::CheckedVectorAt(mSpawnDefineList, static_cast<std::size_t>(index));
}

NpcSpawnDefineEx* NpcMakerEx::GetSpawnDefineByNick(const char16_t* nickname) const {
    L2_GUARD_FUNCTION();
    const auto found = mNpcSpawnDefineMap.find(std::u16string(nickname));
    return found != mNpcSpawnDefineMap.end() ? found->second : nullptr;
}

void NpcMakerEx::RegisterRespawn(int delaySeconds, int spawnCount, NpcSpawnDefineEx* spawnDefine) {
    L2_GUARD_FUNCTION();
    const std::uint32_t availableTime = runtime::GetTickCount() + static_cast<std::uint32_t>(delaySeconds) * 1000U;
    auto* const respawn = new NpcRespawnInfo(availableTime, spawnCount, spawnDefine);
    if (respawn) {
        {
            L2_GUARD(m_RespawnQueueLock);
            mRespawnQueue.push(respawn);
        }
    }
}

void NpcMakerEx::ResetRespawn() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_RespawnQueueLock);
        while (!mRespawnQueue.empty()) {
            NpcRespawnInfo* const respawn = mRespawnQueue.top();
            L2_RELEASE(respawn, ::ObjectRefType::CreateDelete);
            mRespawnQueue.pop();
        }
    }
}

void NpcMakerEx::DoRespawn() {
    L2_GUARD_FUNCTION();
    const std::uint32_t now = runtime::GetTickCount();
    {
        L2_GUARD(m_RespawnQueueLock);
        while (!mRespawnQueue.empty()) {
            NpcRespawnInfo* const respawn = mRespawnQueue.top();
            if (!respawn || now < respawn->m_nAvailableTime) {
                break;
            }
            NpcSpawnDefineEx* const definition = respawn->m_pSpawnDefine;
            if (definition && AtomicIncreaseTotal(definition, respawn->m_nSpawnCount, 1) != 0) {
                definition->Spawn(respawn->m_nSpawnCount, 0);
            }
            L2_RELEASE(respawn, ::ObjectRefType::CreateDelete);
            mRespawnQueue.pop();
        }
    }
}

int NpcMakerEx::AtomicIncreaseTotal(NpcSpawnDefineEx* spawnDefine, int amount, int updateSpawnDefine) {
    L2_GUARD_FUNCTION();
    if (!spawnDefine) {
        return GetTotalNPC();
    }

    L2_GUARD(mNpcCountLock);
    const int current = GetTotalNPC();
    const int maximumNpc = GetMaximumNPC();
    const std::uint32_t expectedBits = static_cast<std::uint32_t>(current) + static_cast<std::uint32_t>(amount);
    std::int32_t expected = 0;
    std::memcpy(&expected, &expectedBits, sizeof(expected));
    if (expected < 0 || expected > maximumNpc) {
        if (!m_bSendIncreaseMakerExFailed) {
            const std::string makerName = Utf16ToUtf8(GetName());
            // Log.Add(
            //     LOG_ERROR,
            //     L"[MakerEx] [%s] failed to "
            //     L"AtomicIncreaseTotal "
            //     L"[current(%d) -> expected(%d) : "
            //     L"maximum_npc(%d)]",
            //     makerName.c_str(),
            //     current,
            //     expected,
            //     maximumNpc)
            // Log.Add(LOG_WARN,
            //     "[NPC maker] Cannot increase the NPC count of maker '%s' by %d, from %d to %d (allowed 0..%d). Count
            //     unchanged. Further quota warnings during increases are suppressed for this maker.",
            //     WideToUtf8(makerName).c_str(), amount, current, expected, maximumNpc)
            L2_LOG_WARN(
                Log,
                "[NPC maker] Cannot increase the NPC count of maker '%s' by %d, from %d to %d (allowed 0..%d). Count "
                "unchanged; further quota warnings during increases are suppressed for this maker.",
                makerName.c_str(), amount, current, expected, maximumNpc
            );
            m_bSendIncreaseMakerExFailed = true;
        }
        return 0;
    }
    if (updateSpawnDefine != 0 && spawnDefine->IncCount(amount) == 0) {
        spawnDefine->DecCount(amount);
        return 0;
    }

    runtime::raw_atomic::StoreI32Release(&mNpcCount, expected);
    return expected;
}

int NpcMakerEx::AtomicDecreaseTotal(NpcSpawnDefineEx* spawnDefine, int amount, int updateSpawnDefine) {
    L2_GUARD_FUNCTION();
    if (!spawnDefine) {
        return GetTotalNPC();
    }

    L2_GUARD(mNpcCountLock);
    const int current = GetTotalNPC();
    const int maximumNpc = GetMaximumNPC();
    const std::uint32_t expectedBits = static_cast<std::uint32_t>(current) - static_cast<std::uint32_t>(amount);
    std::int32_t expected = 0;
    std::memcpy(&expected, &expectedBits, sizeof(expected));
    if (expected < 0 || expected > maximumNpc) {
        if (!m_bSendDecreaseMakerExFailed) {
            const std::string makerName = Utf16ToUtf8(GetName());
            // Log.Add(
            //     LOG_ERROR,
            //     L"[MakerEx] [%s] failed to "
            //     L"AtomicDecreaseTotal "
            //     L"[current(%d) -> expected(%d) : "
            //     L"maximum_npc(%d)]",
            //     makerName.c_str(),
            //     current,
            //     expected,
            //     maximumNpc)
            // Log.Add(LOG_WARN,
            //     "[NPC maker] Cannot decrease the NPC count of maker '%s' by %d, from %d to %d (allowed 0..%d). Count
            //     unchanged. Further quota warnings during decreases are suppressed for this maker.",
            //     WideToUtf8(makerName).c_str(), amount, current, expected, maximumNpc)
            L2_LOG_WARN(
                Log,
                "[NPC maker] Cannot decrease the NPC count of maker '%s' by %d, from %d to %d (allowed 0..%d). Count "
                "unchanged; further quota warnings during decreases are suppressed for this maker.",
                makerName.c_str(), amount, current, expected, maximumNpc
            );
            m_bSendDecreaseMakerExFailed = true;
        }
        return current;
    }
    if (updateSpawnDefine != 0 && spawnDefine->DecCount(amount) == 0) {
        spawnDefine->IncCount(amount);
        return current;
    }

    runtime::raw_atomic::StoreI32Release(&mNpcCount, expected);
    return expected;
}

void NpcMakerEx::Delete() {
    L2_GUARD_FUNCTION();
    m_bDeleting = true;
    new CNpcMakerExReleaseVerifier(this);
    L2_RELEASE(this, ::ObjectRefType::CreateDelete);
}

int NpcMakerEx::RegisterNpcPosEvent(const char16_t* eventName) {
    L2_GUARD_FUNCTION();
    return CNpcMakerDB::Inst().RegisterNpcPosEvent(eventName, this);
}

int NpcMakerEx::RegisterSiegeEventEx(int eventId) {
    L2_GUARD_FUNCTION();
    return CNpcMakerDB::Inst().RegisterSiegeEventEx(eventId, this);
}

int NpcMakerEx::RegisterAgitSiegeEventEx(int eventId) {
    L2_GUARD_FUNCTION();
    return CNpcMakerDB::Inst().RegisterAgitSiegeEventEx(eventId, this);
}

int NpcMakerEx::RegisterDoorEvent(const char16_t* doorName, int inzoneType) {
    L2_GUARD_FUNCTION();
    return CNpcMakerDB::Inst().RegisterDoorEvent(doorName, inzoneType, this);
}

void NpcMakerEx::RegisterFieldCycleEventEx(int eventId) {
    L2_GUARD_FUNCTION();
    CNpcMakerDB::Inst().RegisterFieldCycleEvent(eventId, this);
    const SharedFieldCycleInfo* cycle = SharedFieldCycleInfo::GetInfo(eventId);
    if (cycle->bIsMissionMode) {
        // Current L2NPC.exe 0x004fb280 performs the lookup again for
        // the value passed to FieldCycleEvent.
        cycle = SharedFieldCycleInfo::GetInfo(eventId);
        FieldCycleEvent(eventId, cycle->nowStep);
    }
}

void NpcMakerEx::RegisterOlympiadFieldEventEx() {
    L2_GUARD_FUNCTION();
    CNpcMakerDB::Inst().RegisterOlympiadFieldEvent(0, this);
}

void NpcMakerEx::RegisterOlympiadZoneEventEx(int inzoneType) {
    L2_GUARD_FUNCTION();
    const int previousType = static_cast<int>(GetInZoneClassID());
    if (previousType != 0 && previousType != inzoneType) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] different inzone type events "
        //     L"registered in single inzone maker [%d][%d]",
        //     L"NpcMakerEx.cpp",
        //     0x2b2,
        //     previousType,
        //     inzoneType)
        // Log.Add(LOG_WARN,
        //     "[NPC maker] Maker '%s' changes instance-zone type from %d to %d while registering an Olympiad event.
        //     continuing registration.", Utf16ToUtf8(GetName()).c_str(), previousType, inzoneType)
        L2_LOG_WARN(
            Log,
            "[NPC maker] Maker '%s' changes instance-zone type from %d to %d while registering an Olympiad event; "
            "continuing registration.",
            Utf16ToUtf8(GetName()).c_str(), previousType, inzoneType
        );
    }
    SetInZoneType(static_cast<unsigned int>(inzoneType), 0);
    CNpcMakerDB::Inst().RegisterOlympiadFieldEvent(inzoneType, this);

    g_ns.Send("cd", 0xe4, inzoneType);
}

void NpcMakerEx::RegisterPledgeGameFieldEventEx() {
    L2_GUARD_FUNCTION();
    CNpcMakerDB::Inst().RegisterPledgeGameFieldEvent(0, this);
}

int NpcMakerEx::Residence_GetCastleType(int residenceId) const {
    L2_GUARD_FUNCTION();
    return CResidenceData::Inst(residenceId).m_ResidenceType;
}

void NpcMakerEx::RegisterPledgeGameZoneEventEx(int inzoneType) {
    L2_GUARD_FUNCTION();
    const int previousType = static_cast<int>(GetInZoneClassID());
    if (previousType != 0 && previousType != inzoneType) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] different inzone type events "
        //     L"registered in single inzone maker [%d][%d]",
        //     L"NpcMakerEx.cpp",
        //     0x2e1,
        //     previousType,
        //     inzoneType)
        // Log.Add(LOG_WARN,
        //     "[NPC maker] Maker '%s' changes instance-zone type from %d to %d while registering a pledge-game event.
        //     continuing registration.", Utf16ToUtf8(GetName()).c_str(), previousType, inzoneType)
        L2_LOG_WARN(
            Log,
            "[NPC maker] Maker '%s' changes instance-zone type from %d to %d while registering a pledge-game event; "
            "continuing registration.",
            Utf16ToUtf8(GetName()).c_str(), previousType, inzoneType
        );
    }
    SetInZoneType(static_cast<unsigned int>(inzoneType), 0);
    CNpcMakerDB::Inst().RegisterPledgeGameFieldEvent(inzoneType, this);

    g_ns.Send("chd", 0xfe, 0x0b, inzoneType);
}

void NpcMakerEx::RegistCheckPCDie() {
    L2_GUARD_FUNCTION();
    gGlobalObj.RegistCheckPCDie(this);
}

void NpcMakerEx::RegisterHunterGuildBless() {
    L2_GUARD_FUNCTION();
    CNpcMakerDB::Inst().RegisterHunterGuildBless(this);
}

void NpcMakerEx::RegisterFortressEventEx(int residenceId, int eventId, int spawn) {
    L2_GUARD_FUNCTION();
    CNpcMakerDB::Inst().RegisterFortressEvent(residenceId, eventId, spawn, this);
}

void NpcMakerEx::RegisterInstantZoneEventEx(int inzoneType, int clusterId, int eventId, int spawn) {
    L2_GUARD_FUNCTION();
    const int previousType = static_cast<int>(GetInZoneClassID());
    if (previousType != 0 && previousType != inzoneType) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] different inzone type events "
        //     L"registered in single inzone maker [%d][%d]",
        //     L"NpcMakerEx.cpp",
        //     799,
        //     previousType,
        //     inzoneType)
        // Log.Add(LOG_WARN,
        //     "[NPC maker] Maker '%s' changes instance-zone type from %d to %d while registering an instance-zone event.
        //     continuing registration.", Utf16ToUtf8(GetName()).c_str(), previousType, inzoneType)
        L2_LOG_WARN(
            Log,
            "[NPC maker] Maker '%s' changes instance-zone type from %d to %d while registering an instance-zone event; "
            "continuing registration.",
            Utf16ToUtf8(GetName()).c_str(), previousType, inzoneType
        );
    }
    SetInZoneType(static_cast<unsigned int>(inzoneType), static_cast<unsigned int>(clusterId));
    CNpcMakerDB::Inst().RegisterInstantZoneEvent(inzoneType, clusterId, eventId, spawn, this);
}

void NpcMakerEx::SetDefineExInZoneID(unsigned int inzoneId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004f8384 snapshots the signed element count but
    // reloads the vector begin pointer on every iteration.
    const int definitionCount = static_cast<int>(mSpawnDefineList.size());
    for (int index = 0; index < definitionCount; ++index) {
        runtime::CheckedVectorAt(mSpawnDefineList, static_cast<std::size_t>(index))->SetInZoneID(inzoneId);
    }
}

void NpcMakerEx::RegisterInstantZoneNotifySubscriber() {
    L2_GUARD_FUNCTION();
    const int clusterId = static_cast<int>(GetInZoneClusterID());
    const int classId = static_cast<int>(GetInZoneClassID());
    CNpcMakerDB::Inst().RegisterInstantZoneNotifySubscriber(InstantZoneKey{classId, clusterId}, this);
}

void NpcMakerEx::UnregisterInstantZoneNotifySubscriber() {
    L2_GUARD_FUNCTION();
    const int clusterId = static_cast<int>(GetInZoneClusterID());
    const int classId = static_cast<int>(GetInZoneClassID());
    CNpcMakerDB::Inst().UnregisterInstantZoneNotifySubscriber(InstantZoneKey{classId, clusterId}, this);
}

void NpcMakerEx::NotifyToInstantZoneMaker(int eventId, int argument1, int argument2, int argument3) {
    L2_GUARD_FUNCTION();
    const int clusterId = static_cast<int>(GetInZoneClusterID());
    const int classId = static_cast<int>(GetInZoneClassID());
    const unsigned int inzoneId = GetInZoneID();
    CNpcMakerDB::Inst().NotifyToInstantZoneMakerSubscriber(
        InstantZoneKey{classId, clusterId}, inzoneId, eventId, argument1, argument2, argument3
    );
}

void NpcMakerEx::OnNpcPosEvent(int enabled) {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnNpcPosEvent(this, enabled);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnDoorEvent(int enabled) {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnDoorEvent(this, enabled);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::FieldCycleEvent(int eventId, int state) {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnFieldCycleChangedEvent(this, eventId, state);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OlympiadFieldEvent(int fieldId, int step, int state) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004f8ec4 reads the exact shared row directly.
    SharedOlympiadInfo* const olympiad = SharedOlympiadInfo::GetInfo();
    const unsigned int zeroBased = static_cast<unsigned int>(fieldId) - 1u;
    const sOlympiadFieldInfo* const info =
        zeroBased < SharedOlympiadInfo::FIELD_COUNT ? &olympiad->arrFieldInfo[zeroBased] : nullptr;
    if (!info) {
        return;
    }
    // Current L2NPC.exe 0x004f8f77 reads SID first and type second,
    // despite passing them to the event factory in the opposite order.
    const int inzoneSid = info->nInzoneSId;
    const int inzoneTypeId = info->nInzoneTypeId;
    MakerEvent* const event =
        MakerEventFactory::CreateOnOlympiadFieldStepChangedEvent(this, fieldId, step, state, inzoneTypeId, inzoneSid);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnFortressEvent(
    int residenceId, int eventId, int reply, int argument0, int argument1, int argument2, char16_t* text
) {
    L2_GUARD_FUNCTION();
    MakerEvent* const event =
        MakerEventFactory::CreateOnFortressEvent(this, residenceId, eventId, reply, argument0, argument1, argument2, text);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnInstantZoneEvent(
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
    MakerEvent* const event = MakerEventFactory::CreateOnInstantZoneEvent(
        this, inzoneTypeId, level, inzoneId, eventId, reply, argument0, actionId, scriptArgument1, scriptArgument2,
        scriptArgument3
    );
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnStartSiegeEvent() {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnStartSiegeEvent(this);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnEndSiegeEvent() {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnEndSiegeEvent(this);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnProclaimSiegeEvent() {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnProclaimSiegeEvent(this);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnDestructCTRLTowerEvent() {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnDestructCTRLTowerEvent(this);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnCancelSiegeEvent() {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnCancelSiegeEvent(this);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnExtendedStartSiegeEvent() {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnExtendedStartSiegeEvent(this);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnDestructSupplyEvent() {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnDestructSupplyEvent(this);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnDeclareDominionEvent() {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnDeclareDominionEvent(this);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnStartChangeTimeEvent() {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateStartChangeTimeEvent(this);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnEndChangeTimeEvent() {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateEndChangeTimeEvent(this);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnStartTeamBattleAgitFinalEvent() {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateOnStartTeamBattleAgitFinalEvent(this);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::AgitEvent(int enabled, bool databaseRespawn [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    if (enabled == 0) {
        OnEndSiegeEvent();
    } else if (enabled == 1) {
        OnStartSiegeEvent();
    } else if (enabled == 2) {
        OnStartTeamBattleAgitFinalEvent();
    }
}

void NpcMakerEx::OnPCDieEvent(CCreature& creature) {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreatePCDieEvent(*this, creature);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnDynamicQuestEvent(DynamicQuestType::NPCJobType jobType, char success) {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateDynamicQuestEvent(*this, jobType, success);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnDynamicQuestInfo(int questId, int eventId) {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateDynamicQuestInfoEvent(*this, questId, eventId);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnEventCampaignEvent(EventCampaignType::NPCJobType jobType, char success) {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::CreateEventCampaignEvent(*this, jobType, success);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

void NpcMakerEx::OnHunterGuildBless(bool enabled) {
    L2_GUARD_FUNCTION();
    MakerEvent* const event = MakerEventFactory::Create_ON_HUNTER_GUILD_BLESS_EVENT(*this, enabled);
    if (!event) {
        return;
    }
    HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

int NpcMakerEx::HandleEvent(MakerEvent* event) {
    L2_GUARD_FUNCTION();
    CReloadableNPCEventHandler* const handler = mEventHandler;
    // Noise: SetAI reports a missing class with the maker and class names.
    // Logging again for every event without a bound handler would repeat that
    // setup failure. Keep this existing no-handler return silent.
    if (!handler) {
        return 0;
    }
    return handler->InterlockedHandleEvent(event, &mAIParam);
}

void NpcMakerEx::ReportMakerClassHierarchy(int builderObjectId) {
    L2_GUARD_FUNCTION();
    std::u16string report = u"maker class hierachy";
    CReloadableNPCEventHandler* const handler = mEventHandler;
    CNPCEventHandler* generation = handler ? runtime::raw_atomic::LoadPointerRelaxed(&handler->m_pRealHandler) : nullptr;
    while (generation) {
        report += u"\r\n[";
        report += generation->GetAIClassName();
        report += u']';
        generation = GetSuperEventHandler(generation);
    }

    g_ns.Send("cdS", 0x54, builderObjectId, report.c_str());
}

NpcMakerEx::NpcMakerEx(const NpcMakerEx& other) : CIOObject(other) {
    L2_GUARD_FUNCTION();
    m_bDeleting = false;
    SetName(other.GetName());
    mDefineCount = 0;
    runtime::raw_atomic::StoreI32Relaxed(&mNpcCount, runtime::raw_atomic::LoadI32Relaxed(&other.mNpcCount));
    mMaximumNPC = other.GetMaximumNPC();
    // The shipping copy constructor initializes only the five legacy i_ai
    // slots. The later i_ai5..9, every i_quest/c_ai slot and m_IntList are
    // untouched.
    i_ai0 = 0;
    i_ai1 = 0;
    i_ai2 = 0;
    i_ai3 = 0;
    i_ai4 = 0;
    enabled = other.GetEnabled();
    mDomainID = other.mDomainID;
    runtime::raw_atomic::StoreI32Relaxed(&mMakerKeyFactory, 0);

    // Current EXE 0x004fe140 resolves the copied class name again through
    // SetAI, then copies CAIParameter through SetAIParam. It does not keep
    // the source object's handler pointer as a shortcut.
    SetAI(other.mAI.c_str());
    SetAIParam(other.mAIParam);
    // Current EXE 0x004fe533..0x004fe597 calls the compiler-generated
    // TerritoryInfo assignment after the guard. It copies both territory
    // lists, the generated flag, the lock image and both random-position
    // caches. The earlier copy-constructor shortcut lost the latter state.
    mTerritoryInfo = other.mTerritoryInfo;

    const int definitionCount = static_cast<int>(other.mSpawnDefineList.size());
    for (int index = 0; index < definitionCount; ++index) {
        NpcSpawnDefineEx* const sourceDefinition =
            runtime::CheckedVectorAt(other.mSpawnDefineList, static_cast<std::size_t>(index));
        auto* const definition = sourceDefinition->Copy();
        AddSpawnDefineEx(definition);
    }
    m_inzoneType = other.m_inzoneType;
    m_nInZoneID = other.m_nInZoneID;
    // The target resets these two counters after cloning definitions. It does
    // not initialize m_bFlying in this copy constructor.
    runtime::raw_atomic::StoreI32Relaxed(&m_nInZoneDelete, 0);
    mMaxTotalOfSpawnDefineEx = 0;
    m_bNpcMakerExDebugOn = other.m_bNpcMakerExDebugOn;
    // The shipping copy constructor at 0x004fe140 does not bind its embedded
    // IntList at +0x5f0. All embedded AtomicValueMaker objects are
    // constructed, but only av_quest0/1 and av_ai0/1 receive pointers. The
    // remaining pointer slots are untouched.
    av_ai0 = &mAvAI0;
    av_ai1 = &mAvAI1;
    av_quest0 = &mAvQuest0;
    av_quest1 = &mAvQuest1;
}

} // namespace l2npc

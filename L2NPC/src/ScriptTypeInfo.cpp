#include "ScriptTypeInfo.h"

#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/GlobalObject.h"
#include "L2NPC/src/HateInfo.h"
#include "L2NPC/src/NPC.h"
#include "L2NPC/src/NpcEvent.h"
#include "L2NPC/src/NpcMaker.h"
#include "L2NPC/src/NpcMakerEx.h"
#include "L2NPC/src/NpcSpawnDefineEx.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadManager.h"
#include "TextRuntime.h"

#include <stdexcept>

namespace {

template <typename T>
void RegisterScriptType(ScriptTypeTable&, ScriptTypeId id, const wchar_t* name) {
    auto* const type = new ScriptTypeInfo_T<T>;
    type->Id(static_cast<int>(id));
    type->SetScriptTypeName(name);
    // Register types through the process singleton.
    // The object used to call Initialize may be a different instance.
    ScriptTypeTable::inst()->RegType(static_cast<int>(id), type);
}

template <typename T>
void RegisterScriptEventType(ScriptTypeTable&, int id, const wchar_t* name) {
    auto* const type = new ScriptEventTypeInfo_T<T>;
    type->Id(id);
    type->SetScriptTypeName(name);
    ScriptTypeTable::inst()->RegEventType(id, type);
}

} // namespace

namespace script_type_detail {
namespace {

template <typename Object>
void CollectFromNewObject(ScriptTypeInfo& typeInfo) {
    // ScriptTypeInfo_T<T*>::Init(int2type<1>) in the target deliberately
    // leaves this registration object alive for the process lifetime.
    auto* const object = new Object;
    object->CollectFunc(&typeInfo);
    object->CollectVar(&typeInfo);
}

template <typename Event>
void CollectFromNewEvent(ScriptEventTypeInfo& typeInfo) {
    // ScriptEventTypeInfo_T<T*>::Initialize in the target allocates one
    // registration event, calls its ScriptAccessibleEx CollectFunc and
    // CollectVar slots, and then copies the sentinel-terminated event-name
    // table into ScriptEventTypeInfo.
    auto* const event = new Event;
    event->CollectFunc(&typeInfo);
    event->CollectVar(&typeInfo);

    for (int eventId = 0;; ++eventId) {
        const char16_t* name = event->GetEventName(eventId);
        if (!name) {
            break;
        }
        std::wstring wideName;
        while (*name != u'\0') {
            wideName.push_back(static_cast<wchar_t>(*name++));
        }
        typeInfo.AddEventName(wideName.c_str());
    }
}

} // namespace

void InitializeRegisteredScriptClass(ScriptTypeInfo& typeInfo) {
    switch (static_cast<ScriptTypeId>(typeInfo.Id())) {
        case ScriptTypeId::CreatureData:
            CollectFromNewObject<CSharedCreatureData>(typeInfo);
            return;
        case ScriptTypeId::ItemData:
            CollectFromNewObject<CSharedItemData>(typeInfo);
            return;
        case ScriptTypeId::PledgeData:
            CollectFromNewObject<CSharedPledgeData>(typeInfo);
            return;
        case ScriptTypeId::PartyData:
            CollectFromNewObject<CSharedPartyData>(typeInfo);
            return;
        case ScriptTypeId::StaticObjectData:
            CollectFromNewObject<CSharedStaticObjectData>(typeInfo);
            return;
        case ScriptTypeId::NpcMaker:
            CollectFromNewObject<l2npc::NpcMakerEx>(typeInfo);
            return;
        case ScriptTypeId::HateInfo:
            CollectFromNewObject<HateInfo>(typeInfo);
            return;
        case ScriptTypeId::NpcSpawnDefineEx:
            CollectFromNewObject<NpcSpawnDefineEx>(typeInfo);
            return;
        case ScriptTypeId::CodeInfo:
            CollectFromNewObject<CodeInfo>(typeInfo);
            return;
        case ScriptTypeId::CodeInfoList:
            CollectFromNewObject<CodeInfoList>(typeInfo);
            return;
        case ScriptTypeId::DBNpcInfo:
            CollectFromNewObject<l2npc::DBNpcInfo>(typeInfo);
            return;
        case ScriptTypeId::AtomicValue:
            CollectFromNewObject<AtomicValue>(typeInfo);
            return;
        case ScriptTypeId::RoomInfo:
            CollectFromNewObject<RoomInfo>(typeInfo);
            return;
        case ScriptTypeId::RoomInfoList:
            CollectFromNewObject<RoomInfoList>(typeInfo);
            return;
        case ScriptTypeId::Position:
            CollectFromNewObject<Position>(typeInfo);
            return;
        case ScriptTypeId::PositionList:
            CollectFromNewObject<PositionList>(typeInfo);
            return;
        case ScriptTypeId::FHtml:
            CollectFromNewObject<CFHTML>(typeInfo);
            return;
        case ScriptTypeId::ItemIndexList:
            CollectFromNewObject<ItemIndexList>(typeInfo);
            return;
        case ScriptTypeId::IntList:
            CollectFromNewObject<IntList>(typeInfo);
            return;
        case ScriptTypeId::MpccData:
            CollectFromNewObject<CSharedGeneralObjectData>(typeInfo);
            return;
        case ScriptTypeId::AtomicValueMaker:
            CollectFromNewObject<AtomicValueMaker>(typeInfo);
            return;
        case ScriptTypeId::GlobalObject:
            CollectFromNewObject<GlobalObject>(typeInfo);
            return;

        case ScriptTypeId::Npc:
            CollectFromNewObject<CNPC>(typeInfo);
            return;
        default:
            return;
    }
}

void InitializeRegisteredScriptEvent(ScriptEventTypeInfo& typeInfo) {
    switch (typeInfo.Id()) {
        case 0:
            CollectFromNewEvent<CNPCEvent>(typeInfo);
            return;
        case 1:
            CollectFromNewEvent<MakerEvent>(typeInfo);
            return;
        default:
            return;
    }
}

} // namespace script_type_detail

const wchar_t* ScriptVarInfo::GetScriptVarName() const noexcept { return m_ScriptVarName.c_str(); }

void ScriptVarInfo::SetScriptVarName(const wchar_t* name) { m_ScriptVarName.assign(name); }

void ScriptVarInfo::Offset(int offset) noexcept { mOffset = offset; }

int ScriptVarInfo::Offset() const noexcept { return mOffset; }

void ScriptVarInfo::Type(ScriptTypeInfo* type) noexcept { mType = type; }

ScriptTypeInfo* ScriptVarInfo::Type() const noexcept { return mType; }

ScriptFuncInfo::~ScriptFuncInfo() {
    // Current L2NPC.exe 0x004423e8 clears +0x38 in the destructor body
    // before the compiler destroys mArgTypeList and mName in reverse member
    // order. The descriptors normally live for the process lifetime, but
    // keep the compiled lifecycle when one is released.
    mRetType = nullptr;
}

void ScriptFuncInfo::AddArgType(ScriptTypeInfo* type) { mArgTypeList.push_back(type); }

void ScriptFuncInfo::SetRetType(ScriptTypeInfo* type) noexcept { mRetType = type; }

ScriptTypeInfo* ScriptFuncInfo::RetType() const noexcept { return mRetType; }

std::size_t ScriptFuncInfo::ArgN() const noexcept { return mArgTypeList.size(); }

ScriptTypeInfo* ScriptFuncInfo::ArgType(int index) const noexcept {
    return index >= 0 && static_cast<std::size_t>(index) < mArgTypeList.size()
        ? runtime::CheckedVectorAt(mArgTypeList, static_cast<std::size_t>(index))
        : nullptr;
}

void ScriptFuncInfo::Index(int index) noexcept { mIndex = index; }

int ScriptFuncInfo::Index() const noexcept { return mIndex; }

void ScriptFuncInfo::Name(const wchar_t* name) { mName.assign(name); }

const wchar_t* ScriptFuncInfo::Name() const noexcept { return mName.c_str(); }

void ScriptTypeInfo::Initialize() {}

bool ScriptTypeInfo::IsClass() const { return false; }

int ScriptTypeInfo::Id() const noexcept { return mId; }

void ScriptTypeInfo::Id(int id) noexcept { mId = id; }

void ScriptTypeInfo::SetScriptTypeName(const wchar_t* name) { m_ScriptTypeName.assign(name); }

const wchar_t* ScriptTypeInfo::GetScriptTypeName() const noexcept { return m_ScriptTypeName.c_str(); }

void ScriptTypeInfo::AddFuncInfo(ScriptFuncInfo* function) {
    function->Index(static_cast<int>(mFuncList.size()));
    const std::wstring name = function->Name() ? function->Name() : L"";
    if (mFuncTable.find(name) != mFuncTable.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Duplicated script function, "
        //     L"name[%s], current type[%s]",
        //     L"ScriptTypeInfo.cpp",
        //     0x2f,
        //     function->Name(),
        //     GetScriptTypeName())
        // Log.Add(LOG_WARN,
        //     "[AI types] Ignoring duplicate function '%s' in type '%s'. Keeping the existing registration.",
        //     WideToUtf8(name).c_str(), WideToUtf8(GetScriptTypeName()).c_str())
        L2_LOG_WARN(
            Log, "[AI types] Ignoring duplicate function '%s' in type '%s'; keeping the existing registration.",
            WideToUtf8(name).c_str(), WideToUtf8(GetScriptTypeName()).c_str()
        );
        return;
    }

    mFuncList.push_back(function);
    mFuncTable.emplace(name, function);
}

void ScriptTypeInfo::AddVarInfo(ScriptVarInfo* variable) {
    L2_GUARD_FUNCTION();

    const std::wstring name = variable->GetScriptVarName() ? variable->GetScriptVarName() : L"";
    if (mVarTable.find(name) != mVarTable.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Duplicated script variable, "
        //     L"name[%s], current type[%s]",
        //     L"ScriptTypeInfo.cpp",
        //     0x52,
        //     variable->GetScriptVarName(),
        //     GetScriptTypeName())
        // Log.Add(LOG_WARN,
        //     "[AI types] Ignoring duplicate variable '%s' in type '%s'. Keeping the existing registration.",
        //     WideToUtf8(name).c_str(), WideToUtf8(GetScriptTypeName()).c_str())
        L2_LOG_WARN(
            Log, "[AI types] Ignoring duplicate variable '%s' in type '%s'; keeping the existing registration.",
            WideToUtf8(name).c_str(), WideToUtf8(GetScriptTypeName()).c_str()
        );
        return;
    }

    mVarTable.emplace(name, variable);
}

ScriptVarInfo* ScriptTypeInfo::VarNameToInfo(const wchar_t* name) const {
    const auto found = mVarTable.find(name);
    return found == mVarTable.end() ? nullptr : found->second;
}

void ScriptTypeInfo::SetCompiledOffset(const wchar_t* name, int offset) {
    ScriptVarInfo* field = VarNameToInfo(name);
    if (!field) {
        throw std::logic_error("Compiled AI field is absent from its native type registration");
    }
    field->CompiledOffset(offset);
}

void ScriptTypeInfo::IndexCompiledFields() {
    mCompiledFields.clear();
    for (const auto& [name, field] : mVarTable) {
        const auto [entry, inserted] = mCompiledFields.emplace(field->CompiledOffset(), field);
        if (!inserted && (entry->second->Offset() != field->Offset() || entry->second->Type() != field->Type())) {
            throw std::logic_error("Conflicting compiled AI field offsets in one script type");
        }
    }
}

ScriptVarInfo* ScriptTypeInfo::CompiledField(int offset) const {
    const auto found = mCompiledFields.find(offset);
    return found == mCompiledFields.end() ? nullptr : found->second;
}

ScriptFuncInfo* ScriptTypeInfo::FuncNameToInfo(const wchar_t* name) const {
    const auto found = mFuncTable.find(name);
    return found == mFuncTable.end() ? nullptr : found->second;
}

ScriptFuncInfo* ScriptTypeInfo::IndexToSFNP(int index) const {
    return index >= 0 && static_cast<std::size_t>(index) < mFuncList.size()
        ? runtime::CheckedVectorAt(mFuncList, static_cast<std::size_t>(index))
        : nullptr;
}

ScriptTypeInfo::var_table_type& ScriptTypeInfo::GetVarTable() noexcept { return mVarTable; }

ScriptTypeInfo::func_table_type& ScriptTypeInfo::GetFuncTable() noexcept { return mFuncTable; }

void ScriptEventTypeInfo::AddEventName(const wchar_t* name) {
    const int eventId = static_cast<int>(mEventNameList.size());
    mEventNameTable[std::wstring(name)] = eventId;
    std::wstring storedName;
    storedName.assign(name);
    mEventNameList.push_back(storedName);
}

int ScriptEventTypeInfo::GetEventId(const wchar_t* name) const {
    const auto found = mEventNameTable.find(name ? std::wstring(name) : std::wstring{});
    return found == mEventNameTable.end() ? -1 : found->second;
}

ScriptTypeTable* ScriptTypeTable::inst() {
    static ScriptTypeTable table;
    return &table;
}

void ScriptTypeTable::Initialize(bool compileMode [[maybe_unused]]) {
    // The bool is present in the recovered signature. Neither the current
    // L2NPC machine code nor the 2015 donor reads it.

    // This is the exact current type-list order compiled by the recursive
    // RegScriptType template at 0x00518dfc..0x005178fc.
    RegisterScriptType<void*>(*this, ScriptTypeId::Pointer, L"pointer");
    RegisterScriptType<void>(*this, ScriptTypeId::Void, L"void");
    RegisterScriptType<int>(*this, ScriptTypeId::Int, L"int");
    RegisterScriptType<float>(*this, ScriptTypeId::Float, L"float");
    RegisterScriptType<wchar_t*>(*this, ScriptTypeId::String, L"string");
    RegisterScriptType<double>(*this, ScriptTypeId::Double, L"double");
    RegisterScriptType<std::int64_t>(*this, ScriptTypeId::Int64, L"int64");
    RegisterScriptType<HateInfo*>(*this, ScriptTypeId::HateInfo, L"HateInfo");
    RegisterScriptType<NpcSpawnDefineEx*>(*this, ScriptTypeId::NpcSpawnDefineEx, L"SpawnDefine");
    RegisterScriptType<l2npc::NpcMakerEx*>(*this, ScriptTypeId::NpcMaker, L"NpcMaker");
    RegisterScriptType<l2npc::DBNpcInfo*>(*this, ScriptTypeId::DBNpcInfo, L"DBNpcInfo");
    RegisterScriptType<CodeInfo*>(*this, ScriptTypeId::CodeInfo, L"CodeInfo");
    RegisterScriptType<CodeInfoList*>(*this, ScriptTypeId::CodeInfoList, L"CodeInfoList");
    RegisterScriptType<AtomicValue*>(*this, ScriptTypeId::AtomicValue, L"AtomicValue");
    RegisterScriptType<RoomInfo*>(*this, ScriptTypeId::RoomInfo, L"RoomInfo");
    RegisterScriptType<RoomInfoList*>(*this, ScriptTypeId::RoomInfoList, L"RoomInfoList");
    RegisterScriptType<Position*>(*this, ScriptTypeId::Position, L"Position");
    RegisterScriptType<PositionList*>(*this, ScriptTypeId::PositionList, L"PositionList");
    RegisterScriptType<GlobalObject*>(*this, ScriptTypeId::GlobalObject, L"GlobalObject");
    RegisterScriptType<CFHTML*>(*this, ScriptTypeId::FHtml, L"FHTML");
    RegisterScriptType<CNPC*>(*this, ScriptTypeId::Npc, L"NPC");
    RegisterScriptType<std::vector<FVector>*>(*this, ScriptTypeId::WayPoints, L"WayPoints");
    RegisterScriptType<std::vector<int>*>(*this, ScriptTypeId::WayPointDelays, L"WayPointDelays");
    RegisterScriptType<TelPosList*>(*this, ScriptTypeId::TelPosList, L"TelPosList");
    RegisterScriptType<BuySellList*>(*this, ScriptTypeId::BuySellList, L"BuySellList");
    RegisterScriptType<ItemIndexList*>(*this, ScriptTypeId::ItemIndexList, L"ItemIndexList");
    RegisterScriptType<IntList*>(*this, ScriptTypeId::IntList, L"IntList");
    RegisterScriptType<CSharedStaticObjectData*>(*this, ScriptTypeId::StaticObjectData, L"StaticObjectData");
    RegisterScriptType<CSharedItemData*>(*this, ScriptTypeId::ItemData, L"ItemData");
    RegisterScriptType<CSharedCreatureData*>(*this, ScriptTypeId::CreatureData, L"CreatureData");
    RegisterScriptType<CSharedPledgeData*>(*this, ScriptTypeId::PledgeData, L"PledgeData");
    RegisterScriptType<CSharedPartyData*>(*this, ScriptTypeId::PartyData, L"PartyData");
    RegisterScriptType<CSharedGeneralObjectData*>(*this, ScriptTypeId::MpccData, L"MPCCData");
    RegisterScriptType<AtomicValueMaker*>(*this, ScriptTypeId::AtomicValueMaker, L"AtomicValueMaker");

    RegisterScriptEventType<CNPCEvent*>(*this, 0, L"NpcEvent");
    RegisterScriptEventType<MakerEvent*>(*this, 1, L"MakerEvent");

    for (ScriptTypeInfo* typeInfo : m_ScriptTypeTable) {
        if (typeInfo) {
            typeInfo->Initialize();
            typeInfo->IndexCompiledFields();
        }
    }
    for (ScriptEventTypeInfo* typeInfo : m_ScriptEventTypeTable) {
        if (typeInfo) {
            typeInfo->Initialize();
            typeInfo->IndexCompiledFields();
        }
    }
}

void ScriptTypeTable::RegType(int id, ScriptTypeInfo* typeInfo) {
    if (id >= 0 && id < static_cast<int>(m_ScriptTypeTable.size())) {
        m_ScriptTypeTable[static_cast<std::size_t>(id)] = typeInfo;
    }
}

ScriptTypeInfo* ScriptTypeTable::GetType(int id) const {
    return id >= 0 && id < static_cast<int>(m_ScriptTypeTable.size()) ? m_ScriptTypeTable[static_cast<std::size_t>(id)]
                                                                      : nullptr;
}

void ScriptTypeTable::RegEventType(int id, ScriptEventTypeInfo* typeInfo) {
    if (id >= 0 && id < static_cast<int>(m_ScriptEventTypeTable.size())) {
        m_ScriptEventTypeTable[static_cast<std::size_t>(id)] = typeInfo;
    }
}

ScriptEventTypeInfo* ScriptTypeTable::GetEventType(int id) const {
    return id >= 0 && id < static_cast<int>(m_ScriptEventTypeTable.size())
        ? m_ScriptEventTypeTable[static_cast<std::size_t>(id)]
        : nullptr;
}

ScriptTypeInfo& GetBuiltinScriptType(ScriptTypeId id) {
    ScriptTypeTable* const table = ScriptTypeTable::inst();
    if (ScriptTypeInfo* const typeInfo = table->GetType(static_cast<int>(id))) {
        return *typeInfo;
    }
    return *table->GetType(static_cast<int>(ScriptTypeId::Void));
}

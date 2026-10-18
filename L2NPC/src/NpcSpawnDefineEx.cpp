#include "NpcSpawnDefineEx.h"
#include "Shared/src/TemplateHelper.h"

#include "Config.h"
#include "DBNPC.h"
#include "DelayedCreator.h"
#include "GlobalObject.h"
#include "L2Core/src/inc/Guard.h"
#include "MakerEventFactory.h"
#include "NPC.h"
#include "NPCEventFactory.h"
#include "NpcEvent.h"
#include "NpcMaker.h"
#include "NpcMakerCommon.h"
#include "NpcMakerEx.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "RespawnTimer.h"
#include "Runtime.h"
#include "ScriptTypeInfo.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace {

void AddScriptVariable(ScriptTypeInfo& typeInfo, const wchar_t* name, int offset, ScriptTypeId typeId) {
    auto* const variable = new ScriptVarInfo;
    variable->SetScriptVarName(name);
    variable->Offset(offset);
    variable->Type(&GetBuiltinScriptType(typeId));
    typeInfo.AddVarInfo(variable);
}

std::u16string FromFixedString(const std::array<char16_t, 25>& value) { return std::u16string(value.data()); }

bool CopyFixedString(std::array<char16_t, 25>& destination, const char16_t* value, bool clearOnFailure) {
    // SetNickName and the copy constructor call wcscpy_s directly. Its
    // constraint path clears the first destination character for a null
    // source. The constructor and SetDBName measure first and therefore do
    // not take this branch.
    if (!value && clearOnFailure) {
        destination[0] = u'\0';
        myInvalidParameterHandler(L"(L\"Invalid string pointer\" && 0)", L"wcscpy_s", nullptr, 0, 0);
        return false;
    }
    const std::size_t length = std::char_traits<char16_t>::length(value);
    if (length >= destination.size()) {
        if (clearOnFailure) {
            destination[0] = u'\0';
            myInvalidParameterHandler(L"(L\"Buffer is too small\" && 0)", L"wcscpy_s", nullptr, 0, 0);
        }
        return false;
    }
    std::copy_n(value, length + 1, destination.begin());
    return true;
}

} // namespace

NpcSpawnDefineEx::NpcSpawnDefineEx() {
    L2_GUARD_FUNCTION();
    mTotal = 0;
    mNpcCount = 0;
    mRespawnTime = 0;
    mRespawnRandTime = 0;
    mpMyMaker = nullptr;
    mIsAnywhere = false;
    m_bBossRespawnApplied = false;
    m_nChaseDistance = 0;
    mDBName.fill(u'\0');
    mHasDBName = 0;
    mIsDBLoaded = 0;
    m_minZ = -65536.0;
    mNickName.fill(u'\0');
    mName.fill(u'\0');
    // These two stores are duplicated in the shipping constructor.
    m_bBossRespawnApplied = false;
    m_nChaseDistance = 0;
}

NpcSpawnDefineEx::NpcSpawnDefineEx(const char16_t* name) {
    L2_GUARD_FUNCTION();
    mTotal = 0;
    mNpcCount = 0;
    mRespawnTime = 0;
    mRespawnRandTime = 0;
    mpMyMaker = nullptr;
    mIsAnywhere = false;
    mNickName.fill(u'\0');
    mName.fill(u'\0');
    if (!CopyFixedString(mName, name, false)) {
        const std::string hostName = Utf16ToUtf8(name);
        const std::string namePreview = hostName;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Too long name[%s], length[%d]",
        //     L"NpcSpawnDefineEx.cpp",
        //     0x4c,
        //     hostName.c_str(),
        //     static_cast<int>(
        //         std::char_traits<
        //             char16_t>::length(
        //                 name)))
        // Log.Add(LOG_ERROR,
        //     "[NPC spawn] Definition name has %zu UTF-16 code units (maximum 24). Keeping the empty name. Preview:
        //     '%.400s'%s", std::char_traits<char16_t>::length(name), namePreview.c_str(), namePreview.size() > 400 ? "
        //     [truncated]" : "")
        L2_LOG_ERROR(
            Log,
            "[NPC spawn] Definition name has %zu UTF-16 code units (maximum 24); keeping the empty name. Preview: "
            "'%.400s'%s",
            std::char_traits<char16_t>::length(name), namePreview.c_str(), namePreview.size() > 400 ? " [truncated]" : ""
        );
    }
    mDBName.fill(u'\0');
    mHasDBName = 0;
    mIsDBLoaded = 0;
    m_minZ = -65536.0;
    m_bBossRespawnApplied = false;
    m_nChaseDistance = 0;
}

NpcSpawnDefineEx::NpcSpawnDefineEx(const NpcSpawnDefineEx& other) : BaseSpawnDefine(other), ScriptAccessibleEx(other) {
    L2_GUARD_FUNCTION();
    mTotal = other.mTotal;
    // Current EXE 0x0050665c performs an ordinary volatile copy here.
    mNpcCount = other.mNpcCount;
    mRespawnTime = other.mRespawnTime;
    mRespawnRandTime = other.mRespawnRandTime;
    mpMyMaker = nullptr;
    mIsAnywhere = other.mIsAnywhere;
    CopyFixedString(mName, other.mName.data(), true);
    CopyFixedString(mNickName, other.mNickName.data(), true);
    CopyFixedString(mDBName, other.mDBName.data(), true);
    mAI = other.mAI;
    mAIParams = other.mAIParams;
    mPrivates = other.mPrivates;
    mHasDBName = other.mHasDBName;
    mIsDBLoaded = other.mIsDBLoaded;
    m_minZ = other.m_minZ;
    mSpawnPositions = other.mSpawnPositions;
    m_bBossRespawnApplied = other.m_bBossRespawnApplied;
    m_nChaseDistance = other.m_nChaseDistance;
    // mLock, mNpcSet and mWayPointsParamMap remain freshly constructed.
}

NpcSpawnDefineEx::~NpcSpawnDefineEx() {
    mpMyMaker = nullptr;
    {
        L2_GUARD(mLock);
        mNpcSet.clear();
    }
}

void NpcSpawnDefineEx::CollectFunc(ScriptTypeInfo* typeInfo) {
    ScriptTypeInfo& type = *typeInfo;
    ScriptTypeInfo& voidType = GetBuiltinScriptType(ScriptTypeId::Void);
    ScriptTypeInfo& intType = GetBuiltinScriptType(ScriptTypeId::Int);

    // Current L2NPC.exe 0x00506dd8. AddFuncInfo assigns each index in
    // insertion order, making this the native class-17 ABI order 0..10.
    type.AddFunction(L"Spawn", &NpcSpawnDefineEx::Spawn, &intType, {&intType, &intType});
    type.AddFunction(
        L"SpawnEx", &NpcSpawnDefineEx::SpawnEx, &intType,
        {&intType, &intType, &intType, &intType, &intType, &intType, &intType, &intType, &intType}
    );
    type.AddFunction(L"Spawn2", &NpcSpawnDefineEx::Spawn2, &intType, {&intType, &intType, &intType});
    type.AddFunction(
        L"SpawnEx2", &NpcSpawnDefineEx::SpawnEx2, &intType,
        {&intType, &intType, &intType, &intType, &intType, &intType, &intType, &intType, &intType, &intType}
    );
    type.AddFunction(
        L"Spawn3", &NpcSpawnDefineEx::Spawn3, &intType, {&intType, &intType, &intType, &intType, &intType, &intType}
    );
    type.AddFunction(
        L"SpawnEx3", &NpcSpawnDefineEx::SpawnEx3, &intType,
        {&intType, &intType, &intType, &intType, &intType, &intType, &intType, &intType, &intType, &intType, &intType,
            &intType, &intType}
    );
    type.AddFunction(L"Despawn", &NpcSpawnDefineEx::Despawn, &intType, {});
    type.AddFunction(L"LoadDBNpcInfo", &NpcSpawnDefineEx::LoadDBNpcInfo, &intType, {&intType});
    type.AddFunction(L"SendScriptEvent", &NpcSpawnDefineEx::SendScriptEvent, &voidType, {&intType, &intType, &intType});
    type.AddFunction(L"SetDBLoaded", &NpcSpawnDefineEx::SetDBLoaded, &voidType, {&intType});
    type.AddFunction(L"RegToRespawnTimer", &NpcSpawnDefineEx::RegToRespawnTimer, &voidType, {&intType});
}

void NpcSpawnDefineEx::CollectVar(ScriptTypeInfo* typeInfo) {
    // Current L2NPC.exe 0x00505aa8.
    AddScriptVariable(*typeInfo, L"name", 0x5a, ScriptTypeId::String);
    AddScriptVariable(*typeInfo, L"nickname", 0x28, ScriptTypeId::String);
    AddScriptVariable(*typeInfo, L"total", 0x8c, ScriptTypeId::Int);
    AddScriptVariable(*typeInfo, L"npc_count", 0x90, ScriptTypeId::Int);
    AddScriptVariable(*typeInfo, L"respawn_time", 0x94, ScriptTypeId::Int);
    AddScriptVariable(*typeInfo, L"respawn_rand", 0x98, ScriptTypeId::Int);
    AddScriptVariable(*typeInfo, L"dbname", 0x9c, ScriptTypeId::String);
    AddScriptVariable(*typeInfo, L"has_dbname", 0xd0, ScriptTypeId::Int);
}

ScriptAccessibleEx* NpcSpawnDefineEx::GetScriptAccessibleEx() const { return const_cast<NpcSpawnDefineEx*>(this); }

void NpcSpawnDefineEx::SetNpcMakerEx(l2npc::NpcMakerEx* maker) { mpMyMaker = maker; }

void NpcSpawnDefineEx::SetNickName(const char16_t* value) {
    L2_GUARD_FUNCTION();
    CopyFixedString(mNickName, value, true);
}

void NpcSpawnDefineEx::SetRespawnTime(int value) { mRespawnTime = value; }

void NpcSpawnDefineEx::SetRespawnRandTime(int value) { mRespawnRandTime = value; }

void NpcSpawnDefineEx::SetAI(const char16_t* value) {
    L2_GUARD_FUNCTION();
    mAI = std::u16string(value);
}

void NpcSpawnDefineEx::SetAIParameters(const AiParameterMap& parameters) { mAIParams = parameters; }

void NpcSpawnDefineEx::SetPrivates(const char16_t* value) {
    L2_GUARD_FUNCTION();
    mPrivates = std::u16string(value);
}

bool NpcSpawnDefineEx::AddWayPoints(const char16_t* name, std::vector<FVector> points) {
    mWayPointsParamMap[std::u16string(name)] = points;
    // This is the target's observable return value after assignment.
    return false;
}

void NpcSpawnDefineEx::SetDBLoaded(int value) { mIsDBLoaded = value; }

void NpcSpawnDefineEx::SetDBName(const char16_t* value) {
    L2_GUARD_FUNCTION();
    if (!CopyFixedString(mDBName, value, false)) {
        return;
    }
    mHasDBName = 1;
}

void NpcSpawnDefineEx::AddSpawnPosition(l2npc::SpawnPos* position) {
    L2_GUARD_FUNCTION();
    const double z = static_cast<double>(position->z);
    // COMISD/JA in the target replaces the minimum unless z is strictly
    // above it. This also replaces a NaN minimum.
    if (m_minZ == -65536.0 || !(z > m_minZ)) {
        m_minZ = z;
    }
    mSpawnPositions.push_back(position);
}

l2npc::SpawnPos* NpcSpawnDefineEx::GetNpcSpawnPosition() {
    L2_GUARD_FUNCTION();
    return mSpawnPositions.front();
}

int NpcSpawnDefineEx::IncCount(int amount) {
    runtime::raw_atomic::AddI32(&mNpcCount, amount);
    return runtime::raw_atomic::LoadI32Relaxed(&mNpcCount) >= 0 &&
        runtime::raw_atomic::LoadI32Relaxed(&mNpcCount) <= mTotal;
}

int NpcSpawnDefineEx::DecCount(int amount) {
    runtime::raw_atomic::AddI32(&mNpcCount, runtime::NegateInt32Wrapping(amount));
    if (runtime::raw_atomic::LoadI32Relaxed(&mNpcCount) < 0 ||
        runtime::raw_atomic::LoadI32Relaxed(&mNpcCount) > mTotal) {
        const std::string definitionName = Utf16ToUtf8(GetName());
        const int value = runtime::raw_atomic::LoadI32Relaxed(&mNpcCount);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Name : %s] invalid dec count because of npc quota "
        //     L"[%d / %d]",
        //     definitionName.c_str(),
        //     value,
        //     mTotal)
        // Log.Add(LOG_ERROR,
        //     "[NPC spawn] Definition '%s': observed count %d after decrement is outside quota 0..%d. Reporting failure
        //     without undoing the decrement.", WideToUtf8(definitionName).c_str(), value, mTotal)
        L2_LOG_ERROR(
            Log,
            "[NPC spawn] Definition '%s': observed count %d after decrement is outside quota 0..%d; reporting failure "
            "without undoing the decrement.",
            definitionName.c_str(), value, mTotal
        );
        return 0;
    }
    return 1;
}

int NpcSpawnDefineEx::LoadDBNpcInfo(int argument) {
    L2_GUARD_FUNCTION();
    if (mHasDBName == 0) {
        return 0;
    }
    l2npc::dbnpc.LoadNpcInfoEx(mDBName.data(), this, argument);
    return 1;
}

void NpcSpawnDefineEx::OnDBNpcInfo(l2npc::DBNpcInfo& info) {
    L2_GUARD_FUNCTION();
    if (!mpMyMaker) {
        return;
    }
    MakerEvent* const event = MakerEventFactory::CreateDbNpcInfoEvent(mpMyMaker, this, info);
    if (!event) {
        return;
    }
    mpMyMaker->HandleEvent(event);
    L2_RELEASE(event, ObjectRefType::CreateDelete);
}

void NpcSpawnDefineEx::CheckAndModifyByBossRespawn() {
    L2_GUARD_FUNCTION();
    if (!::g_bBossRespawnFactorEnabled || !m_bBossRespawnApplied) {
        return;
    }
    if (mRespawnTime <= 0) {
        return;
    }

    // Current L2NPC.exe 0x005024e0 has the same publication order as the
    // regular definition: both old field values precede the first store,
    // and the flag is cleared before the final random-time store.
    const int respawnBase = mRespawnTime;
    const std::int32_t respawnProduct = runtime::MultiplyInt32Wrapping(respawnBase, ::g_nBossRespawnFactor);
    const std::int32_t respawnResult = runtime::AddInt32Wrapping(respawnBase, respawnProduct / 100);
    const int randomBase = mRespawnRandTime;
    mRespawnTime = respawnResult;
    const std::int32_t randomProduct = runtime::MultiplyInt32Wrapping(randomBase, ::g_nBossRespawnFactor);
    m_bBossRespawnApplied = false;
    mRespawnRandTime = runtime::AddInt32Wrapping(randomBase, randomProduct / 100);
}

int NpcSpawnDefineEx::CreateMakerKey() {
    L2_GUARD_FUNCTION();
    l2npc::NpcMakerEx* maker = mpMyMaker;
    if (!maker) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NpcSpawnDefineEx::CreateMakerKey maker is null")
        // Log.Add(LOG_ERROR,
        //     "[NPC spawn] Cannot create a maker key for extended definition '%s': maker is missing. Returning key 0.",
        //     Utf16ToUtf8(GetName()).c_str())
        L2_LOG_ERROR(
            Log, "[NPC spawn] Cannot create a maker key for extended definition '%s': maker is missing; returning key 0.",
            Utf16ToUtf8(GetName()).c_str()
        );
        ReportCallStack(LOG_ERROR, false);
        return 0;
    }

    return maker->CreateMakerKey();
}

FVector NpcSpawnDefineEx::GenerateSpawnPos(int& heading, bool withSky) {
    L2_GUARD_FUNCTION();
    FVector output{};
    heading = 0;
    if (!mIsAnywhere) {
        // NpcSpawnDefineEx draws before checking vector size. An invalid
        // empty definition therefore still advances the global MT stream.
        std::uint32_t rollBits = static_cast<std::uint32_t>(genrand64_real2() * 101.0);
        if (mSpawnPositions.empty()) {
            // Shipping uses the narrow overload and nevertheless passes its
            // UTF-16 name buffer to `%s`. Keep that observable bug.
            // The original call is kept below. Operator output converts the name to UTF-8.
            // Log.Add(
            //     LOG_ERROR,
            //     "SpawnDefineName[%s] GetSpawnPositionSize()<=0 "
            //     "== NULL at file[%s], line[%d]",
            //     GetName(),
            //     "NpcSpawnDefineEx.cpp",
            //     0x1bc)
            // Log.Add(LOG_ERROR,
            //     "[NPC spawn] Extended definition '%s' has no fixed spawn positions. Returning position (0, 0, 0) and
            //     heading 0.", Utf16ToUtf8(GetName()).c_str())
            L2_LOG_ERROR(
                Log,
                "[NPC spawn] Extended definition '%s' has no fixed spawn positions; returning position (0, 0, 0) and "
                "heading 0.",
                Utf16ToUtf8(GetName()).c_str()
            );
            return output;
        }

        l2npc::SpawnPos* selected = nullptr;
        for (l2npc::SpawnPos* position : mSpawnPositions) {
            selected = position;
            if (!position) {
                continue;
            }
            rollBits -= static_cast<std::uint32_t>(position->prob);
            std::int32_t signedRoll = 0;
            std::memcpy(&signedRoll, &rollBits, sizeof(signedRoll));
            if (signedRoll < 0) {
                break;
            }
        }
        if (!selected) {
            return output;
        }
        heading = selected->dir;
        output = {
            static_cast<double>(selected->x),
            static_cast<double>(selected->y),
            static_cast<double>(selected->z),
        };
        return output;
    }

    heading = static_cast<int>(genrand64_real2() * 65536.0);
    TerritoryInfo* territory = GetTerritoryInfo();
    // The shipping override does not guard a missing maker here.
    const FVector selected = territory->PickRandomPos(withSky);
    return selected;
}

int NpcSpawnDefineEx::GetChaseDistance() { return m_nChaseDistance; }

CSharedCreatureData* NpcSpawnDefineEx::GetNpcFromKey(int key) {
    L2_GUARD_FUNCTION();
    l2npc::NpcMakerEx* maker = mpMyMaker;
    return maker ? maker->GetNpcFromKey(key) : nullptr;
}

TerritoryInfo* NpcSpawnDefineEx::GetTerritoryInfo() {
    l2npc::NpcMakerEx* maker = mpMyMaker;
    return maker ? maker->GetTerritoryInfo() : nullptr;
}

l2npc::NpcMakerEx* NpcSpawnDefineEx::GetMyMaker() { return mpMyMaker; }

int NpcSpawnDefineEx::GetTotalNPC() {
    l2npc::NpcMakerEx* maker = mpMyMaker;
    return maker ? maker->GetTotalNPC() : 0;
}

bool NpcSpawnDefineEx::IsInBannedTerritory(FVector position, bool includeZ) {
    L2_GUARD_FUNCTION();
    l2npc::NpcMakerEx* maker = mpMyMaker;
    return maker && maker->GetTerritoryInfo()->IsInBannedTerritory(position, includeZ);
}

bool NpcSpawnDefineEx::IsInTerritory(FVector position, bool includeZ) {
    L2_GUARD_FUNCTION();
    l2npc::NpcMakerEx* maker = mpMyMaker;
    return maker && maker->GetTerritoryInfo()->IsInTerritory(position, includeZ);
}

bool NpcSpawnDefineEx::IsInTerritoryWithLogVisitor(FVector position, ZLogVisitor& visitor, bool includeZ) {
    L2_GUARD_FUNCTION();
    l2npc::NpcMakerEx* maker = mpMyMaker;
    return maker && maker->GetTerritoryInfo()->IsInTerritoryWithLogVisitor(position, visitor, includeZ);
}

void NpcSpawnDefineEx::CheckZPos(const FVector& position, bool isPrivate) {
    L2_GUARD_FUNCTION();
    TerritoryInfo* territory = GetTerritoryInfo();
    const double minimumZ = m_minZ;

    // The target converts only Z for the comparison. X/Y and the strings
    // are touched only on the diagnostic branch.
    const int comparisonZ = runtime::Win64CvttDoubleToInt32(position.z);
    if (static_cast<double>(comparisonZ) < minimumZ - 500.0) {
        const int z = runtime::Win64CvttDoubleToInt32(position.z);
        const int y = runtime::Win64CvttDoubleToInt32(position.y);
        const int x = runtime::Win64CvttDoubleToInt32(position.x);
        const std::string firstTerritory = Utf16ToUtf8(territory->GetFirstTerritoryName());
        const std::string definitionName = Utf16ToUtf8(GetName());
        // Shipping passes the double m_MinZ to a `%d` slot. On the Win64
        // vararg ABI the formatter therefore observes the low 32 IEEE bits.
        // Original diagnostic-only bit reinterpretation. Operator output uses the actual minimum Z.
        // std::uint64_t minimumZBits = 0
        // std::memcpy(
        //     &minimumZBits,
        //     &minimumZ,
        //     sizeof(minimumZBits))
        // const int targetNpcPosLogValue =
        //     static_cast<int>(
        //         static_cast<std::uint32_t>(
        //             minimumZBits))
        // Log.Add(
        //     LOG_ERROR,
        //     L"CheckZPos. MakerLog(%30s)'s "
        //     L"(%7d, %7d, %7d) [%s] was dropped. "
        //     L"npcpos(%d) - 500 > z(%d)",
        //     firstTerritory.c_str(),
        //     x,
        //     y,
        //     z,
        //     definitionName.c_str(),
        //     targetNpcPosLogValue,
        //     z)
        // Log.Add(LOG_WARN,
        //     "[NPC spawn] Extended definition '%s', territory '%s': position (%d, %d, %d) is more than 500 units below
        //     minimum Z %.17g. Position is unchanged.", WideToUtf8(definitionName).c_str(),
        //     WideToUtf8(firstTerritory).c_str(), x, y, z, minimumZ)
        L2_LOG_WARN(
            Log,
            "[NPC spawn] Extended definition '%s', territory '%s': position (%d, %d, %d) is more than 500 units below "
            "minimum Z %.17g; position is unchanged.",
            definitionName.c_str(), firstTerritory.c_str(), x, y, z, minimumZ
        );
    }

    ZLogVisitor visitor;
    if (IsInTerritoryWithLogVisitor(position, visitor, true)) {
        return;
    }

    const wchar_t* privateSuffix = isPrivate ? L"[PRIVATE]" : L"";
    if (!visitor.IsEnable()) {
        const int z = runtime::Win64CvttDoubleToInt32(position.z);
        const int y = runtime::Win64CvttDoubleToInt32(position.y);
        const int x = runtime::Win64CvttDoubleToInt32(position.x);
        const std::string firstTerritory = Utf16ToUtf8(territory->GetFirstTerritoryName());
        const std::string definitionName = Utf16ToUtf8(GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"CheckZPos2. MakerLog(Territory:%30s)'s "
        //     L"(%7d, %7d, %7d) NPC[%s]%s is outsider. "
        //     L"[NSDEx]",
        //     firstTerritory.c_str(),
        //     x,
        //     y,
        //     z,
        //     definitionName.c_str(),
        //     privateSuffix)
        // Log.Add(LOG_WARN,
        //     "[NPC spawn] Extended definition '%s'%s is outside territory '%s' at (%d, %d, %d). Position is unchanged.",
        //     WideToUtf8(definitionName).c_str(), WideToUtf8(privateSuffix).c_str(),
        //     WideToUtf8(firstTerritory).c_str(), x, y, z)
        L2_LOG_WARN(
            Log, "[NPC spawn] Extended definition '%s'%s is outside territory '%s' at (%d, %d, %d); position is unchanged.",
            definitionName.c_str(), WideToUtf8(privateSuffix).c_str(), firstTerritory.c_str(), x, y,
            z
        );
        return;
    }

    std::wstring zSuffix;
    if (visitor.GetMinZ() != std::numeric_limits<int>::max()) {
        zSuffix = L", minZ[" + std::to_wstring(visitor.GetMinZ()) + L"]";
    } else if (visitor.GetMaxZ() != std::numeric_limits<int>::min()) {
        zSuffix = L", maxZ[" + std::to_wstring(visitor.GetMaxZ()) + L"]";
    }
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const std::string firstTerritory = Utf16ToUtf8(territory->GetFirstTerritoryName());
    const std::string definitionName = Utf16ToUtf8(GetName());
    // Log.Add(
    //     LOG_ERROR,
    //     L"CheckZPos1. MakerLog(Territory:%30s %s)'s "
    //     L"(%7d, %7d, %7d) NPC[%s]%s is outsider. "
    //     L"[NSDEx]",
    //     firstTerritory.c_str(),
    //     zSuffix.c_str(),
    //     x,
    //     y,
    //     z,
    //     definitionName.c_str(),
    //     privateSuffix)
    // Log.Add(LOG_WARN,
    //     "[NPC spawn] Extended definition '%s'%s is outside territory '%s' at (%d, %d, %d). Bounds%s. Position is
    //     unchanged.", WideToUtf8(definitionName).c_str(), WideToUtf8(privateSuffix).c_str(),
    //     WideToUtf8(firstTerritory).c_str(), x, y, z, WideToUtf8(zSuffix).c_str())
    L2_LOG_WARN(
        Log,
        "[NPC spawn] Extended definition '%s'%s is outside territory '%s' at (%d, %d, %d); bounds%s; position is unchanged.",
        definitionName.c_str(), WideToUtf8(privateSuffix).c_str(), firstTerritory.c_str(), x, y, z,
        WideToUtf8(zSuffix).c_str()
    );
}

void NpcSpawnDefineEx::OnNpcCreated(CNPC* npc) {
    L2_GUARD_FUNCTION();
    l2npc::NpcMakerEx* const maker = mpMyMaker;
    npc->m_data->m_nDomainID = maker ? maker->GetDomainID() : 0;
    base_structure_detail::AtomicIncrement(&NpcSpawnInfo::Inst()->NpcMaker_OnCreate);
    if (npc->m_data->m_BossID == 0 && mHasDBName != 0) {
        const std::u16string databaseName = FromFixedString(mDBName);
        npc->mDBName = databaseName;
    }
}

void NpcSpawnDefineEx::OnNpcDeleted(CNPC* npc) {
    L2_GUARD_FUNCTION();
    base_structure_detail::AtomicIncrement(&NpcSpawnInfo::Inst()->NpcMaker_OnDelete);

    l2npc::NpcMakerEx* const maker = mpMyMaker;
    if (!maker) {
        return;
    }

    maker->UnregisterNpc(npc);
    const bool privateNpc = npc->m_data->m_BossID != 0;
    const int remaining = mpMyMaker->AtomicDecreaseTotal(this, 1, !privateNpc);
    if (!privateNpc) {
        {
            L2_GUARD(mLock);
            mNpcSet.erase(static_cast<unsigned int>(npc->Id()));
        }
    }

    if (mpMyMaker->GetInZoneDeleteCount() != 0 && mpMyMaker->GetInZoneID() != 0) {
        return;
    }
    if (!privateNpc) {
        MakerEvent* const deleted = MakerEventFactory::CreateNpcDeletedEvent(mpMyMaker, this, npc, remaining);
        if (deleted) {
            mpMyMaker->HandleEvent(deleted);
            L2_RELEASE(deleted, ObjectRefType::CreateDelete);
        }
    }
    if (remaining == 0) {
        MakerEvent* const allDeleted = MakerEventFactory::CreateAllNpcDeletedEvent(mpMyMaker);
        if (allDeleted) {
            mpMyMaker->HandleEvent(allDeleted);
            L2_RELEASE(allDeleted, ObjectRefType::CreateDelete);
        }
    }
}

void NpcSpawnDefineEx::OnNpcEnterWorld(CNPC* npc) {
    L2_GUARD_FUNCTION();
    l2npc::NpcMakerEx* const maker = mpMyMaker;
    if (!maker || (maker->GetInZoneDeleteCount() != 0 && maker->GetInZoneID() != 0)) {
        return;
    }

    npc->Save();
    if (npc->m_nAirShipServerID == 0) {
        const bool isPrivate = npc->m_data->m_BossID != 0;
        if (!isPrivate || !npc->m_data->IsPrivateRespawnNoLog()) {
            CheckZPos(npc->m_data->GetPos(), isPrivate);
        }
    }
    if (!maker->RegisterNpc(npc)) {
        npc->Despawn();
        const std::string npcName = Utf16ToUtf8(npc->GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid npc or inzone being deleted "
        //     L"in enter world [%s]",
        //     L"NpcSpawnDefineEx.cpp",
        //     0x25d,
        //     npcName.c_str())
        // Log.Add(LOG_WARN,
        //     "[NPC spawn] Maker rejected registration of NPC '%s' during world entry. Despawn requested.",
        //     WideToUtf8(npcName).c_str())
        L2_LOG_WARN(
            Log, "[NPC spawn] Maker rejected registration of NPC '%s' during world entry; despawn requested.",
            npcName.c_str()
        );
        return;
    }

    CNPCEvent* const created = CNPCEventFactory::CreateCreatedEvent(npc, mIsDBLoaded);
    if (!created) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CreateCreatedEvent failed",
        //     L"NpcSpawnDefineEx.cpp",
        //     0x246)
        // Log.Add(LOG_ERROR,
        //     "[NPC spawn] Could not create the AI Created event for extended definition '%s'. Skipping this event and
        //     continuing enter-world processing.", Utf16ToUtf8(GetName()).c_str())
        L2_LOG_ERROR(
            Log,
            "[NPC spawn] Could not create the AI Created event for extended definition '%s'; skipping this event and "
            "continuing enter-world processing.",
            Utf16ToUtf8(GetName()).c_str()
        );
    } else {
        npc->HandleEvent(created);
        L2_RELEASE(created, ObjectRefType::CreateDelete);
    }

    if (npc->m_data->m_BossID == 0) {
        {
            L2_GUARD(mLock);
            mNpcSet.insert(static_cast<unsigned int>(npc->Id()));
        }
        MakerEvent* const makerEvent = MakerEventFactory::CreateNpcCreatedEvent(mpMyMaker, this, npc);
        if (makerEvent) {
            mpMyMaker->HandleEvent(makerEvent);
            L2_RELEASE(makerEvent, ObjectRefType::CreateDelete);
        }
    }
}

bool NpcSpawnDefineEx::ReserveNPC() {
    L2_GUARD_FUNCTION();
    return mpMyMaker && mpMyMaker->AtomicIncreaseTotal(this, 1, false) != 0;
}

bool NpcSpawnDefineEx::SetAIParameter(CNPC* npc, const char16_t* aiName, AiParameterMap& parameters) {
    L2_GUARD_FUNCTION();
    const char16_t* selectedAi = aiName;
    if (npc->m_data->m_BossID == 0 && !mAI.empty()) {
        selectedAi = mAI.c_str();
    }
    npc->SetAI(selectedAi);
    for (const auto& entry : parameters) {
        npc->SetParameter(entry.first.c_str(), entry.second.c_str());
    }
    if (npc->m_data->m_BossID == 0) {
        if (!mPrivates.empty()) {
            npc->SetParameter(u"Privates", mPrivates.c_str());
        }
        for (const auto& entry : mWayPointsParamMap) {
            npc->SetWayPoints(entry.first.c_str(), entry.second);
        }
        for (const auto& entry : mAIParams) {
            npc->SetParameter(entry.first.c_str(), entry.second.c_str());
        }
    }
    return true;
}

void NpcSpawnDefineEx::SpawnNpc(bool) {
    // Current EXE vtable slot +0xc8 -> 0x005057c4. The thunk ignores the
    // reservation flag and tail-calls Spawn(1, 0).
    Spawn(1, 0);
}

int NpcSpawnDefineEx::Spawn(int count, int baseTime) {
    L2_GUARD_FUNCTION();
    return SpawnEx2(count, baseTime, 0, 0, 0, 0, 0, 0, 0, 0);
}

int NpcSpawnDefineEx::Spawn2(int count, int baseTime, int spread) {
    L2_GUARD_FUNCTION();
    l2npc::NpcMakerEx* const maker = mpMyMaker;
    if (maker && maker->GetInZoneDeleteCount() != 0) {
        return 0;
    }
    return SpawnEx2(count, baseTime, spread, 0, 0, 0, 0, 0, 0, 0);
}

int NpcSpawnDefineEx::Spawn3(int count, int baseTime, int spread, int makerParam1, int makerParam2, int makerParam3) {
    L2_GUARD_FUNCTION();
    l2npc::NpcMakerEx* const maker = mpMyMaker;
    if (maker && maker->GetInZoneDeleteCount() != 0) {
        return 0;
    }
    return SpawnEx3(count, baseTime, spread, 0, 0, 0, 0, makerParam1, makerParam2, makerParam3, 0, 0, 0);
}

int NpcSpawnDefineEx::SpawnEx(int count, int baseTime, int x, int y, int z, int direction, int hp, int mp, int dbValue) {
    L2_GUARD_FUNCTION();
    return SpawnEx2(count, baseTime, 0, x, y, z, direction, hp, mp, dbValue);
}

int NpcSpawnDefineEx::SpawnEx2(
    int count, int baseTime, int spread, int x, int y, int z, int direction, int hp, int mp, int dbValue
) {
    L2_GUARD_FUNCTION();
    const char16_t* const name = GetName();
    if (*name == u'\0') {
        const std::string makerName = Utf16ToUtf8(mpMyMaker->GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] name is null. npc maker name[%s]",
        //     L"NpcSpawnDefineEx.cpp",
        //     0xfd,
        //     makerName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC spawn] SpawnEx2 rejected for maker '%s': the definition name is empty. No spawn requested.",
        //     WideToUtf8(makerName).c_str())
        L2_LOG_ERROR(
            Log, "[NPC spawn] SpawnEx2 rejected for maker '%s': the definition name is empty; no spawn requested.",
            makerName.c_str()
        );
        return 0;
    }

    const int spawnTime = BaseSpawnDefine::GetSpawnTime(baseTime, spread);
    // Noise: Per-request spawn details require [Setting] SpawnLog. Failures remain unconditional.
    // if (mHasDBName != 0) {
    if (mHasDBName != 0 && g_bSpawnLog) {
        const std::u16string databaseName = FromFixedString(mDBName);
        const std::string hostDatabaseName = Utf16ToUtf8(databaseName.c_str());
        const std::string makerName = Utf16ToUtf8(mpMyMaker->GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] SpawnEx2 [%s][%s] "
        //     L"[%d][%d][%d][%d][%d][%d]",
        //     hostDatabaseName.c_str(),
        //     makerName.c_str(),
        //     count,
        //     spawnTime,
        //     spread,
        //     baseTime,
        //     dbValue,
        //     static_cast<std::int32_t>(
        //         runtime::GetTickCount()))
        // Log.Add(LOG_NORMAL,
        //     "[NPC spawn] SpawnEx2 for database NPC '%s', maker '%s': requested count %d, chosen delay %d seconds (base %d,
        //     spread %d), DB value %d.", WideToUtf8(hostDatabaseName).c_str(), WideToUtf8(makerName).c_str(), count,
        //     spawnTime, baseTime, spread, dbValue)
        L2_LOG_INFO(
            Log,
            "[NPC spawn] SpawnEx2 for database NPC '%s', maker '%s': requested count %d, chosen delay %d seconds (base %d, "
            "spread %d), DB value %d.",
            hostDatabaseName.c_str(), makerName.c_str(), count, spawnTime, baseTime, spread, dbValue
        );
    }

    if (spawnTime < 1) {
        for (int index = 0; index < count; ++index) {
            l2npc::SendCreateNpcPacket(
                name, this, 0, x, y, z, direction, hp, mp, dbValue, static_cast<std::uint32_t>(GetInZoneID()), 0, '\0', 0, 0,
                0, 0, '\0', 0, 0, '\0', 0, 0, 0
            );
        }
        return 0;
    }

    auto* creator = new l2npc::DelayedCreator(
        this, name, count, x, y, z, direction, hp, mp, dbValue, static_cast<std::uint32_t>(GetInZoneID()), 0, 0, 0
    );
    creator->AddTimer(static_cast<std::uint32_t>(spawnTime) * 1000U, 0);
    return 0;
}

int NpcSpawnDefineEx::SpawnEx3(
    int count,
    int baseTime,
    int spread,
    int x,
    int y,
    int z,
    int direction,
    int makerParam1,
    int makerParam2,
    int makerParam3,
    int hp,
    int mp,
    int dbValue
) {
    L2_GUARD_FUNCTION();
    const char16_t* const name = GetName();
    if (*name == u'\0') {
        const std::string makerName = Utf16ToUtf8(mpMyMaker->GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] name is null. npc maker name[%s]",
        //     L"NpcSpawnDefineEx.cpp",
        //     0x131,
        //     makerName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC spawn] SpawnEx3 rejected for maker '%s': the definition name is empty. No spawn requested.",
        //     WideToUtf8(makerName).c_str())
        L2_LOG_ERROR(
            Log, "[NPC spawn] SpawnEx3 rejected for maker '%s': the definition name is empty; no spawn requested.",
            makerName.c_str()
        );
        return 0;
    }

    const int spawnTime = BaseSpawnDefine::GetSpawnTime(baseTime, spread);
    // Noise: Per-request spawn details require [Setting] SpawnLog. Failures remain unconditional.
    // if (mHasDBName != 0) {
    if (mHasDBName != 0 && g_bSpawnLog) {
        const std::u16string databaseName = FromFixedString(mDBName);
        const std::string hostDatabaseName = Utf16ToUtf8(databaseName.c_str());
        const std::string makerName = Utf16ToUtf8(mpMyMaker->GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] SpawnEx3 [%s][%s] "
        //     L"[%d][%d][%d][%d][%d][%d][%d][%d][%d]",
        //     hostDatabaseName.c_str(),
        //     makerName.c_str(),
        //     count,
        //     spawnTime,
        //     spread,
        //     makerParam1,
        //     makerParam2,
        //     makerParam3,
        //     baseTime,
        //     dbValue,
        //     static_cast<std::int32_t>(
        //         runtime::GetTickCount()))
        // Log.Add(LOG_NORMAL,
        //     "[NPC spawn] SpawnEx3 for database NPC '%s', maker '%s': requested count %d, chosen delay %d seconds (base %d,
        //     spread %d), DB value %d. Maker parameters (%d, %d, %d).", WideToUtf8(hostDatabaseName).c_str(),
        //     WideToUtf8(makerName).c_str(), count, spawnTime, baseTime, spread, dbValue, makerParam1, makerParam2,
        //     makerParam3)
        L2_LOG_INFO(
            Log,
            "[NPC spawn] SpawnEx3 for database NPC '%s', maker '%s': requested count %d, chosen delay %d seconds (base %d, "
            "spread %d), DB value %d; maker parameters (%d, %d, %d).",
            hostDatabaseName.c_str(), makerName.c_str(), count, spawnTime, baseTime, spread, dbValue,
            makerParam1, makerParam2, makerParam3
        );
    }

    if (spawnTime < 1) {
        for (int index = 0; index < count; ++index) {
            l2npc::SendCreateNpcPacket(
                name, this, 0, x, y, z, direction, hp, mp, dbValue, static_cast<std::uint32_t>(GetInZoneID()), 0, '\0', 0,
                makerParam1, makerParam2, makerParam3, '\0', 0, 0, '\0', 0, 0, 0
            );
        }
        return 0;
    }

    auto* creator = new l2npc::DelayedCreator(
        this, name, count, x, y, z, direction, hp, mp, dbValue, static_cast<std::uint32_t>(GetInZoneID()), makerParam1,
        makerParam2, makerParam3
    );
    creator->AddTimer(static_cast<std::uint32_t>(spawnTime) * 1000U, 0);
    return 0;
}

int NpcSpawnDefineEx::Despawn() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(mLock);
        for (const unsigned int objectId : mNpcSet) {
            if (CNPC* npc = CNPC::GetObjectW(static_cast<int>(objectId))) {
                npc->Despawn();
            }
        }
    }
    return 0;
}

void NpcSpawnDefineEx::RegToRespawnTimer(int absoluteUnixSeconds) {
    L2_GUARD_FUNCTION();
    new l2npc::CRespawnTimer(this, std::u16string(std::u16string_view(mDBName.data())), absoluteUnixSeconds);
}

void NpcSpawnDefineEx::SendScriptEvent(int argument1, int argument2, int argument3) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(mLock);
        GlobalObject& world = gGlobalObj;
        for (const unsigned int objectId : mNpcSet) {
            if (CNPC* npc = CNPC::GetObjectW(static_cast<int>(objectId))) {
                world.SendScriptEventEx(npc->m_data, argument1, argument2, argument3);
            }
        }
    }
}

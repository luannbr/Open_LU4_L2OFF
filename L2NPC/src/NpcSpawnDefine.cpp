#include "NpcMaker.h"

#include "Config.h"
#include "DBNPC.h"
#include "DelayedCreator.h"
#include "L2Core/src/inc/Guard.h"
#include "NPC.h"
#include "NPCEventFactory.h"
#include "NpcEvent.h"
#include "NpcMakerCommon.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Runtime.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Exception.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>

namespace l2npc {
namespace {

bool IsTargetZeroVector(const FVector& position) {
    // FVector equality uses the target's promoted float epsilon and strict
    // component comparisons. NaN so compares unequal to zero.
    constexpr double epsilon = 9.999999747378752e-06;
    return std::fabs(position.x) < epsilon && std::fabs(position.y) < epsilon && std::fabs(position.z) < epsilon;
}

} // namespace

// PDB module 54 owns this process-wide definition and both of its dynamic
// initialization records.
CNpcSpawnDefineWorld g_WorldSpawnDefine;

bool CNpcSpawnDefine::CreateNPC(bool firstSpawn, int count, bool allowDatabaseLoad, int respawnSeconds) {
    L2_GUARD_FUNCTION();
    if (firstSpawn && m_nRespawnTime < 0) {
        return true;
    }
    if (!mpNpcMaker) {
        return false;
    }

    const int createCount = count == 0 ? m_nTotal : count;
    for (int index = 0; index < createCount; ++index) {
        if (!mpNpcMaker->IncreaseTotal(1)) {
            return false;
        }
        IncCount(1);
        if (firstSpawn && m_bDBSaving && allowDatabaseLoad && GetInZoneID() == 0) {
            dbnpc.LoadNpcInfo(GetDBName(), this);
        } else {
            Spawn(1, respawnSeconds);
        }
    }
    return true;
}

void CNpcSpawnDefine::CheckAndModifyByBossRespawn() {
    L2_GUARD_FUNCTION();
    if (::g_bBossRespawnFactorEnabled && m_bBossRespawnApplied && m_nRespawnTime > 0) {
        // Current L2NPC.exe 0x00500888 creates both old field values
        // before publishing the first result, then clears the applied flag
        // between the random product and its final store.
        const int respawnBase = m_nRespawnTime;
        const std::int32_t respawnProduct = runtime::MultiplyInt32Wrapping(respawnBase, ::g_nBossRespawnFactor);
        const std::int32_t respawnResult = runtime::AddInt32Wrapping(respawnBase, respawnProduct / 100);
        const int randomBase = nRespawnRandVarTime;
        m_nRespawnTime = respawnResult;
        const std::int32_t randomProduct = runtime::MultiplyInt32Wrapping(randomBase, ::g_nBossRespawnFactor);
        m_bBossRespawnApplied = false;
        nRespawnRandVarTime = runtime::AddInt32Wrapping(randomBase, randomProduct / 100);
    }
}

int CNpcSpawnDefine::DecCount(int amount) {
    return runtime::raw_atomic::AddI32(&m_nCreated, runtime::NegateInt32Wrapping(amount));
}

int CNpcSpawnDefine::GetPledgeId() { return mpNpcMaker ? mpNpcMaker->GetPledgeId() : 0; }

SpawnPos* CNpcSpawnDefine::GetSpawnPosition(int index) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004fff90 indexes the xvector storage directly.
    // there is no target-side bounds check in this method.
    return runtime::CheckedVectorAt(m_SpawnPositions, static_cast<std::size_t>(index));
}

int CNpcSpawnDefine::GetSpawnPositionSize() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00500034 subtracts the xvector end/begin slots,
    // divides by the eight-byte pointer element size and truncates to int.
    return static_cast<int>(m_SpawnPositions.size());
}

int CNpcSpawnDefine::GetRespawnTime() {
    L2_GUARD_FUNCTION();
    const int respawnTime = m_nRespawnTime;
    const int randomTime = nRespawnRandVarTime;
    if (respawnTime >= 1 && randomTime >= respawnTime) {
        if (TerritoryInfo* const territory = GetTerritoryInfo()) {
            const std::string territoryName = Utf16ToUtf8(territory->GetFirstTerritoryName());
            const std::string definitionName = Utf16ToUtf8(GetName());
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s]->[%s] RespawnRandTime[%d] "
            //     L">= RespawnTime[%d] !!!",
            //     territoryName.c_str(),
            //     definitionName.c_str(),
            //     randomTime,
            //     respawnTime)
            // Log.Add(LOG_WARN,
            //     "[NPC spawn] Definition '%s', territory '%s': respawn spread %d seconds is not below the base time %d
            //     seconds. Using 15 seconds.", WideToUtf8(definitionName).c_str(), WideToUtf8(territoryName).c_str(),
            //     randomTime, respawnTime)
            L2_LOG_WARN(
                Log,
                "[NPC spawn] Definition '%s', territory '%s': respawn spread %d seconds is not below the base time %d "
                "seconds; using 15 seconds.",
                definitionName.c_str(), territoryName.c_str(), randomTime, respawnTime
            );
        }
        return 15;
    }
    if (randomTime < 1) {
        return respawnTime;
    }
    return (respawnTime - randomTime) + LegacyCrtRand() % (randomTime * 2);
}

int CNpcSpawnDefine::IncCount(int amount) { return runtime::raw_atomic::AddI32(&m_nCreated, amount); }

void CNpcSpawnDefine::SetRespawnRandTime(int seconds) {
    L2_GUARD_FUNCTION();
    nRespawnRandVarTime = seconds;
}

void CNpcSpawnDefine::SetRespawnTime(int seconds) {
    L2_GUARD_FUNCTION();
    m_nRespawnTime = seconds;
}

int CNpcSpawnDefine::Spawn(int count, int respawnSeconds) {
    L2_GUARD_FUNCTION();
    return SpawnEx2(count, respawnSeconds, 0, 0, 0, 0, 0, 0, 0);
}

int CNpcSpawnDefine::SpawnEx(int count, int baseTime, int x, int y, int z, int direction, int hp, int mp) {
    L2_GUARD_FUNCTION();
    return SpawnEx2(count, baseTime, 0, x, y, z, direction, hp, mp);
}

int CNpcSpawnDefine::Spawn2(int count, int baseTime, int spread) {
    L2_GUARD_FUNCTION();
    return SpawnEx2(count, baseTime, spread, 0, 0, 0, 0, 0, 0);
}

int CNpcSpawnDefine::SpawnEx2(int count, int baseTime, int spread, int x, int y, int z, int direction, int hp, int mp) {
    L2_GUARD_FUNCTION();
    const int spawnTime = BaseSpawnDefine::GetSpawnTime(baseTime, spread);
    if (spawnTime < 1) {
        for (int index = 0; index < count; ++index) {
            // Current L2NPC.exe 0x00500b98 creates these three
            // non-constant arguments in this order before the packet call.
            const std::uint32_t inZoneId = static_cast<std::uint32_t>(GetInZoneID());
            const int pledgeId = GetPledgeId();
            const char16_t* const name = GetName();
            l2npc::SendCreateNpcPacket(
                name, this, pledgeId, x, y, z, direction, hp, mp, 0, inZoneId, 0, '\0', 0, 0, 0, 0, '\0', 0, 0, '\0', 0, 0, 0
            );
        }
        return 0;
    }

    // The shipping new-expression allocates CIOObject storage first and
    // only then evaluates GetInZoneID followed by GetName. Keep that order
    // explicit on hosts whose function-argument evaluation differs from
    // the target MSVC build.
    DelayedCreator* creator = nullptr;
    void* const storage = CIOObject::operator new(sizeof(DelayedCreator));
    if (storage) {
        const std::uint32_t inZoneId = static_cast<std::uint32_t>(GetInZoneID());
        const char16_t* const name = GetName();
        try {
            creator = ::new (storage) DelayedCreator(this, name, count, x, y, z, direction, hp, mp, 0, inZoneId, 0, 0, 0);
        } catch (...) {
            DelayedCreator::operator delete(storage);
            throw;
        }
    }
    // L2NPC.exe calls AddTimer even when its non-throwing allocator returned
    // null. The resulting target null fault is not normalized.
    creator->AddTimer(static_cast<std::uint32_t>(spawnTime) * 1000U, 0);
    return 0;
}

void CNpcSpawnDefine::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    SpawnNpc(false);
}

int CNpcSpawnDefine::CreateMakerKey() {
    L2_GUARD_FUNCTION();
    if (!mpNpcMaker) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CNpcSpawnDefine::CreateMakerKey maker is null")
        // Log.Add(LOG_ERROR,
        //     "[NPC spawn] Cannot create a maker key for definition '%s': maker is missing. Returning key 0.",
        //     Utf16ToUtf8(GetName()).c_str())
        L2_LOG_ERROR(
            Log, "[NPC spawn] Cannot create a maker key for definition '%s': maker is missing; returning key 0.",
            Utf16ToUtf8(GetName()).c_str()
        );
        ReportCallStack(LOG_ERROR, false);
        return 0;
    }
    return mpNpcMaker->CreateMakerKey();
}

FVector CNpcSpawnDefine::GenerateSpawnPos(int& heading, bool withSky) {
    L2_GUARD_FUNCTION();
    FVector output{};
    if (!bAnywhere) {
        const int spawnPositionCount = GetSpawnPositionSize();
        if (spawnPositionCount < 1) {
            const std::string definitionName = Utf16ToUtf8(GetName());
            // Log.Add(
            //     LOG_ERROR,
            //     "SpawnDefineName[%S] GetSpawnPositionSize()<=0 "
            //     "== NULL at file[%s], line[%d]",
            //     definitionName.c_str(),
            //     "NpcSpawnDefine.cpp",
            //     0x145)
            // Log.Add(LOG_ERROR,
            //     "[NPC spawn] Definition '%s' has no fixed spawn positions. Returning position (0, 0, 0) and heading 0.",
            //     WideToUtf8(definitionName).c_str())
            L2_LOG_ERROR(
                Log, "[NPC spawn] Definition '%s' has no fixed spawn positions; returning position (0, 0, 0) and heading 0.",
                definitionName.c_str()
            );
            heading = 0;
            return output;
        }

        std::uint32_t rollBits = static_cast<std::uint32_t>(genrand64_real2() * 101.0);
        SpawnPos* selected = nullptr;
        for (int index = 0; index < spawnPositionCount; ++index) {
            selected = GetSpawnPosition(index);
            rollBits -= static_cast<std::uint32_t>(selected->prob);
            std::int32_t signedRoll = 0;
            std::memcpy(&signedRoll, &rollBits, sizeof(signedRoll));
            if (signedRoll < 0) {
                break;
            }
        }
        heading = selected->dir;
        output = {
            static_cast<double>(selected->x),
            static_cast<double>(selected->y),
            static_cast<double>(selected->z),
        };
        return output;
    }

    // The regular target leaves the caller's heading unchanged when its
    // maker pointer is null. With a maker it draws heading before selecting
    // the pre-generated ground/sky position.
    if (!mpNpcMaker) {
        return output;
    }
    heading = static_cast<int>(genrand64_real2() * 65536.0);
    const FVector selected = mpNpcMaker->mTerritoryInfo.PickRandomPos(withSky);
    return selected;
}

CSharedCreatureData* CNpcSpawnDefine::GetNpcFromKey(int key) {
    L2_GUARD_FUNCTION();
    return mpNpcMaker->GetNpcFromKey(key);
}

TerritoryInfo* CNpcSpawnDefine::GetTerritoryInfo() { return mpNpcMaker ? &mpNpcMaker->mTerritoryInfo : nullptr; }

int CNpcSpawnDefine::GetTotalNPC() { return mpNpcMaker ? mpNpcMaker->GetTotalNPC() : 0; }

bool CNpcSpawnDefine::IsInBannedTerritory(FVector position, bool includeZ) {
    L2_GUARD_FUNCTION();
    return mpNpcMaker && mpNpcMaker->mTerritoryInfo.IsInBannedTerritory(position, includeZ);
}

bool CNpcSpawnDefine::IsInTerritory(FVector position, bool includeZ) {
    L2_GUARD_FUNCTION();
    return mpNpcMaker && mpNpcMaker->mTerritoryInfo.IsInTerritory(position, includeZ);
}

bool CNpcSpawnDefine::IsInTerritoryWithLogVisitor(FVector position, ZLogVisitor& visitor, bool includeZ) {
    L2_GUARD_FUNCTION();
    return mpNpcMaker && mpNpcMaker->mTerritoryInfo.IsInTerritoryWithLogVisitor(position, visitor, includeZ);
}

void CNpcSpawnDefine::CheckZPos(const FVector& position, bool isPrivate) {
    L2_GUARD_FUNCTION();
    TerritoryInfo* territory = GetTerritoryInfo();
    if (!territory) {
        return;
    }

    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    const std::string firstTerritory = Utf16ToUtf8(territory->GetFirstTerritoryName());
    if (static_cast<double>(z) < m_minZ - 500.0) {
        const std::string definitionName = Utf16ToUtf8(GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"MakerLog(%s)'s (%7d, %7d, %7d) "
        //     L"[%s] was dropped. npcpos(%d) - 500 > z(%d)",
        //     firstTerritory.c_str(),
        //     x,
        //     y,
        //     z,
        //     definitionName.c_str(),
        //     runtime::Win64CvttDoubleToInt32(m_minZ),
        //     z)
        // Log.Add(LOG_WARN,
        //     "[NPC spawn] Definition '%s', territory '%s': position (%d, %d, %d) is more than 500 units below minimum Z
        //     %.17g. Position is unchanged.", WideToUtf8(definitionName).c_str(), WideToUtf8(firstTerritory).c_str(), x, y,
        //     z, m_minZ)
        L2_LOG_WARN(
            Log,
            "[NPC spawn] Definition '%s', territory '%s': position (%d, %d, %d) is more than 500 units below minimum Z "
            "%.17g; position is unchanged.",
            definitionName.c_str(), firstTerritory.c_str(), x, y, z, m_minZ
        );
    }

    ZLogVisitor visitor;
    if (IsInTerritoryWithLogVisitor(position, visitor, true)) {
        return;
    }

    const wchar_t* privateSuffix = isPrivate ? L"[PRIVATE]" : L"";
    if (!visitor.IsEnable()) {
        if (IsTargetZeroVector(position)) {
            return;
        }
        const std::string definitionName = Utf16ToUtf8(GetName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"CheckZPos2. MakerLog(Territory:%30s)'s "
        //     L"(%7d, %7d, %7d) NPC[%s]%s is outsider. "
        //     L"[NSD]",
        //     firstTerritory.c_str(),
        //     x,
        //     y,
        //     z,
        //     definitionName.c_str(),
        //     privateSuffix)
        // Log.Add(LOG_WARN,
        //     "[NPC spawn] Definition '%s'%s is outside territory '%s' at (%d, %d, %d). Position is unchanged.",
        //     WideToUtf8(definitionName).c_str(), WideToUtf8(privateSuffix).c_str(),
        //     WideToUtf8(firstTerritory).c_str(), x, y, z)
        L2_LOG_WARN(
            Log, "[NPC spawn] Definition '%s'%s is outside territory '%s' at (%d, %d, %d); position is unchanged.",
            definitionName.c_str(), WideToUtf8(privateSuffix).c_str(), firstTerritory.c_str(), x, y,
            z
        );
        return;
    }

    // Both target logging branches compare the position against FVector()
    // and suppress the diagnostic for the zero vector.
    if (IsTargetZeroVector(position)) {
        return;
    }

    std::wstring zSuffix;
    if (visitor.GetMinZ() != std::numeric_limits<int>::max()) {
        zSuffix = L", minZ[" + std::to_wstring(visitor.GetMinZ()) + L"]";
    } else if (visitor.GetMaxZ() != std::numeric_limits<int>::min()) {
        zSuffix = L", maxZ[" + std::to_wstring(visitor.GetMaxZ()) + L"]";
    }
    const std::string definitionName = Utf16ToUtf8(GetName());
    // Log.Add(
    //     LOG_ERROR,
    //     L"CheckZPos1. MakerLog(Territory:%30s %s)'s "
    //     L"(%7d, %7d, %7d) NPC[%s]%s is outsider. "
    //     L"[NSD]",
    //     firstTerritory.c_str(),
    //     zSuffix.c_str(),
    //     x,
    //     y,
    //     z,
    //     definitionName.c_str(),
    //     privateSuffix)
    // Log.Add(LOG_WARN,
    //     "[NPC spawn] Definition '%s'%s is outside territory '%s' at (%d, %d, %d). Bounds%s. Position is unchanged.",
    //     WideToUtf8(definitionName).c_str(), WideToUtf8(privateSuffix).c_str(),
    //     WideToUtf8(firstTerritory).c_str(), x, y, z, WideToUtf8(zSuffix).c_str())
    L2_LOG_WARN(
        Log, "[NPC spawn] Definition '%s'%s is outside territory '%s' at (%d, %d, %d); bounds%s; position is unchanged.",
        definitionName.c_str(), WideToUtf8(privateSuffix).c_str(), firstTerritory.c_str(), x, y, z,
        WideToUtf8(zSuffix).c_str()
    );
}

void CNpcSpawnDefine::OnNpcCreated(CNPC* npc) {
    L2_GUARD_FUNCTION();
    if (mpNpcMaker) {
        const int castleId = mpNpcMaker->GetCastleID();
        if (castleId != 0) {
            npc->m_data->m_nResidenceID = castleId;
        }
    }
    npc->m_data->m_nDomainID = mpNpcMaker ? mpNpcMaker->GetDomainID() : 0;
    if (bAnywhere) {
        npc->pstate = 1;
    }
    if (m_bDBSaving && npc->m_data->m_BossID == 0) {
        npc->mDBName = GetDBName();
    }
    base_structure_detail::AtomicIncrement(&NpcSpawnInfo::Inst()->NpcMaker_OnCreate);
}

void CNpcSpawnDefine::OnNpcDeleted(CNPC* npc) {
    L2_GUARD_FUNCTION();
    if (!mpNpcMaker) {
        return;
    }
    mpNpcMaker->OnNpcDeleted(npc);
    if (npc->m_data->m_BossID != 0) {
        DecCount(1);
        return;
    }
    DecCount(1);
    if (!mpNpcMaker->m_bEnabled) {
        return;
    }
    if (bOnDeleteCreate) {
        for (std::size_t index = 0; index < sOnDeleteCreateNpc.size(); ++index) {
            mpNpcMaker->CreateNPC(runtime::CheckedVectorAt(sOnDeleteCreateNpc, index), nOnDeleteCreateRespawnTime);
        }
    }
    if (mpNpcMaker->GetInitialSpawn() != INITIAL_SPAWN_RANDOM3) {
        const int respawnTime = GetRespawnTime();
        if (respawnTime > 0) {
            const int firstTotal = mpNpcMaker->GetTotalNPC();
            if (firstTotal < mpNpcMaker->GetMaximumNPC() && mpNpcMaker->GetTotalNPC() >= 0) {
                AddTimer(static_cast<std::uint32_t>(respawnTime) * 1000U, 0);
            }
        }
    }
    base_structure_detail::AtomicIncrement(&NpcSpawnInfo::Inst()->NpcMaker_OnDelete);
}

void CNpcSpawnDefine::OnNpcEnterWorld(CNPC* npc) {
    L2_GUARD_FUNCTION();
    npc->Save();
    if (npc->m_nAirShipServerID == 0) {
        const bool isPrivate = npc->m_data->m_BossID != 0;
        if (!isPrivate || !npc->m_data->IsPrivateRespawnNoLog()) {
            CheckZPos(npc->m_data->GetPos(), isPrivate);
        }
    }
    CNPCEvent* const event = CNPCEventFactory::CreateCreatedEvent(npc, m_bDBLoaded ? 1 : 0);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CreateCreatedEvent failed",
        //     L"NpcSpawnDefine.cpp",
        //     0x21b)
        // Log.Add(LOG_ERROR,
        //     "[NPC spawn] Could not create the AI Created event for definition '%s'. Skipping this event and continuing
        //     enter-world processing.", Utf16ToUtf8(GetName()).c_str())
        L2_LOG_ERROR(
            Log,
            "[NPC spawn] Could not create the AI Created event for definition '%s'; skipping this event and continuing "
            "enter-world processing.",
            Utf16ToUtf8(GetName()).c_str()
        );
    } else {
        npc->HandleEvent(event);
        L2_RELEASE(event, ::ObjectRefType::CreateDelete);
    }
    if (mpNpcMaker) {
        mpNpcMaker->OnNpcEnterWorld(npc);
    }
    m_bDBLoaded = false;
    if (mpNpcMaker && !mpNpcMaker->m_bEnabled) {
        npc->Despawn();
    }
}

bool CNpcSpawnDefine::ReserveNPC() {
    L2_GUARD_FUNCTION();
    return mpNpcMaker && mpNpcMaker->IncreaseTotal(1);
}

bool CNpcSpawnDefine::SetAIParameter(CNPC* npc, const char16_t* aiName, AiParameterMap& parameters) {
    L2_GUARD_FUNCTION();
    const char16_t* selectedAi = aiName;
    if (npc->m_data->m_BossID == 0) {
        if (GetAI()) {
            selectedAi = GetAI();
        }
    }
    npc->SetAI(selectedAi);
    for (const auto& entry : parameters) {
        npc->SetParameter(entry.first.c_str(), entry.second.c_str());
    }
    if (npc->m_data->m_BossID == 0) {
        if (HavePrivates()) {
            npc->SetParameter(u"Privates", GetPrivates().c_str());
        }
        if (HaveWayPoint() && HaveWayPointDelay()) {
            // The target creates the second argument first.
            std::vector<int> delays = GetWayPointDelays();
            std::vector<FVector> points = GetWayPoints();
            npc->SetParameter(points, delays);
        }
        for (const auto& entry : m_AIParams) {
            npc->SetParameter(entry.first.c_str(), entry.second.c_str());
        }
    }
    return true;
}

void CNpcSpawnDefine::SpawnNpc(bool alreadyReserved) {
    L2_GUARD_FUNCTION();
    if (!mpNpcMaker || !mpNpcMaker->m_bEnabled) {
        return;
    }
    if (!alreadyReserved) {
        mpNpcMaker->IncreaseTotal(1);
    }
    SendCreateNpcPacket();
}

void CNpcSpawnDefine::SendCreateNpcPacket() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00501484 reads these in this exact order.
    const std::uint32_t inZoneId = static_cast<std::uint32_t>(GetInZoneID());
    const int pledgeId = GetPledgeId();
    const char16_t* const name = GetName();
    l2npc::SendCreateNpcPacket(
        name, this, pledgeId, 0, 0, 0, 0, 0, 0, 0, inZoneId, 0, '\0', 0, 0, 0, 0, '\0', 0, 0, '\0', 0, 0, 0
    );
}

CNpcSpawnDefineWorld::CNpcSpawnDefineWorld() : m_eNpcTypeForDeco(NpcTypeForDeco::NotDecoNpc), m_nAgitCIDorPledgeDBID(0) {
    // Current-target constructor @ 0x00502398 installs the process-wide
    // maker before replacing the base vftable with CNpcSpawnDefineWorld's.
    SetNpcMaker(&g_WorldNPCMaker);
}

void CNpcSpawnDefineWorld::SetDecoNpcInfo(NpcTypeForDeco::Type type, int value) {
    m_eNpcTypeForDeco = type;
    m_nAgitCIDorPledgeDBID = value;
}

int CNpcSpawnDefineWorld::GetPledgeDBID() const {
    return m_eNpcTypeForDeco == NpcTypeForDeco::DecoNpcInInstantAgit ? m_nAgitCIDorPledgeDBID : 0;
}

void CNpcSpawnDefineWorld::OnNpcCreated(CNPC* npc) {
    L2_GUARD_FUNCTION();
    CNpcSpawnDefine::OnNpcCreated(npc);
    if (m_eNpcTypeForDeco == NpcTypeForDeco::DecoNpcInRealWorld) {
        npc->m_data->m_nResidenceID = m_nAgitCIDorPledgeDBID;
    }
    base_structure_detail::AtomicIncrement(&NpcSpawnInfo::Inst()->NpcMakerWorld_OnCreate);
}

void CNpcSpawnDefineWorld::OnNpcDeleted(CNPC* npc) {
    L2_GUARD_FUNCTION();
    // The target bypasses CNpcSpawnDefine::OnNpcDeleted here:
    // world definitions have their own maker and counter, with no regular
    // definition decrement/respawn branch.
    GetNpcMaker()->OnNpcDeleted(npc);
    base_structure_detail::AtomicIncrement(&NpcSpawnInfo::Inst()->NpcMakerWorld_OnDelete);
}

} // namespace l2npc

#include "NpcMaker.h"

#include "NPC.h"
#include "NpcMakerDB.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Runtime.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/ThreadManager.h"
#include "TextRuntime.h"

#include <cstdint>
#include <cstring>
#include <string_view>

namespace l2npc {

CNpcMakerWorld g_WorldNPCMaker;

CNpcSpawnDefine::CNpcSpawnDefine(const CNpcSpawnDefine& other) : BaseSpawnDefine(other) {
    L2_GUARD_FUNCTION();
    m_minZ = other.m_minZ;
    m_sName = other.m_sName;
    m_nTotal = other.m_nTotal;
    m_nRespawnTime = other.m_nRespawnTime;
    nRespawnRandVarTime = other.nRespawnRandVarTime;
    bAnywhere = other.bAnywhere;
    m_SpawnPositions = other.m_SpawnPositions;
    mpNpcMaker = nullptr;
    m_sAI = other.m_sAI;
    m_sPrivates = other.m_sPrivates;
    m_sNickName = other.m_sNickName;
    m_WayPoints = other.m_WayPoints;
    m_WayPointDelays = other.m_WayPointDelays;
    m_nCreated = 0;
    m_bDBSaving = other.m_bDBSaving;
    m_DBSavingFlag = other.m_DBSavingFlag;
    m_bDBLoaded = other.m_bDBLoaded;
    m_sDBName = other.m_sDBName;
    m_AIParams = other.m_AIParams;
    // The shipping copy constructor copies these two scalar fields but
    // leaves sOnDeleteCreateNpc empty.
    bOnDeleteCreate = other.bOnDeleteCreate;
    nOnDeleteCreateRespawnTime = other.nOnDeleteCreateRespawnTime;
    m_bBossRespawnApplied = other.m_bBossRespawnApplied;
    m_nChaseDistance = other.m_nChaseDistance;
}

namespace {

std::size_t RefTypeIndex(::ObjectRefType type) { return static_cast<std::size_t>(type); }


} // namespace

CNpcMaker::CNpcMaker() {
    L2_GUARD_FUNCTION();
    m_NpcSpawnDefines.clear();
    runtime::raw_atomic::StoreI32Relaxed(&m_nCreatedNPC, 0);
    m_nMaxNPC = 0;
    m_SpawnTimeType = SPAWN_NORMAL;
    m_nCastleID = 0;
    m_nDomainID = 0;
    m_bEnabled = true;
    runtime::raw_atomic::StoreI32Relaxed(&m_nMakerID, 0);
    m_nColor = -1;
    m_nNpcType = 0;
    m_nPledgeId = 0;
    m_bFlying = false;
    m_InitialSpawn = INITIAL_SPAWN_ALL;
    m_SiegeDefendType = NPC_DEFEND_SIEGE;
}

CNpcMaker::CNpcMaker(const CNpcMaker& other) : CIOObject(other) {
    L2_GUARD_FUNCTION();
    mTerritoryInfo = other.mTerritoryInfo;
    for (CNpcSpawnDefine* sourceSpawn : other.m_NpcSpawnDefines) {
        AddNpcSpawnDefine(sourceSpawn->Copy());
    }
    // Shipping assigns these after AddNpcSpawnDefine has copied every
    // definition, so RANDOM3 diagnostics cannot fire while cloning.
    m_InitialSpawn = other.m_InitialSpawn;
    m_SpawnTimeType = other.m_SpawnTimeType;
    m_nCastleID = other.m_nCastleID;
    m_SiegeDefendType = other.m_SiegeDefendType;
    m_nDomainID = other.m_nDomainID;
    m_nMaxNPC = other.m_nMaxNPC;
    runtime::raw_atomic::StoreI32Relaxed(&m_nCreatedNPC, 0);
    m_bEnabled = other.m_bEnabled;
    m_nColor = other.m_nColor;
    m_nNpcType = other.m_nNpcType;
    m_nPledgeId = other.m_nPledgeId;
    m_bFlying = other.m_bFlying;
}

CNpcMaker::~CNpcMaker() {
    const int spawnDefinitionCount = static_cast<int>(m_NpcSpawnDefines.size());
    for (int index = 0; index < spawnDefinitionCount; ++index) {
        CNpcSpawnDefine* const spawn = runtime::CheckedVectorAt(m_NpcSpawnDefines, static_cast<std::size_t>(index));
        if (!spawn) {
            continue;
        }
        L2_RELEASE(spawn, ::ObjectRefType::CreateDelete);
    }
}

CNpcSpawnDefine* CNpcMaker::GetNpcSpawnDefine(int index) {
    L2_GUARD_FUNCTION();
    return runtime::CheckedVectorAt(m_NpcSpawnDefines, static_cast<std::size_t>(index));
}

int CNpcMaker::GetNpcSpawnDefineSize() {
    L2_GUARD_FUNCTION();
    return static_cast<int>(m_NpcSpawnDefines.size());
}

::MemoryObject* CNpcMaker::AddRef(const char* callerFile, int callerLine, ::ObjectRefType type) {
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);

    ::RefHelperStruct& record = m_RefHelpers.m_RefData[RefTypeIndex(type)];
    record.m_nRefCount.fetch_add(1, std::memory_order_seq_cst);
    record.m_pAddCaller = callerFile;
    record.m_nAddLine = callerLine;
    return this;
}

std::int32_t CNpcMaker::Release(const char* callerFile, int callerLine, ::ObjectRefType type, bool) {
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
        //     "[references] CNpcMaker object %d: release at %s:%d made %s count negative (%d). Observed count now %d, last
        //     retain %s:%d, previous release %s:%d.", MemoryObjectID(), callerFile ? callerFile : "(unknown)", callerLine,
        //     WideToUtf8(::ObjectRefTypeName(type)).c_str(), subtypeRemaining,
        //     record.m_nRefCount.load(std::memory_order_relaxed),
        //     record.m_pAddCaller ? record.m_pAddCaller : "(unknown)", record.m_nAddLine,
        //     record.m_pReleaseCaller ? record.m_pReleaseCaller : "(unknown)", record.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            "[references] CNpcMaker object %d: release at %s:%d made %s count negative (%d); observed count now %d, last "
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
        ReportOrt("NpcMaker.cpp", 0x20);
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
                //     "deleting in-timer-object![CNpcMaker] "
                //     "at file[%s], line[%d]",
                //     callerFile,
                //     callerLine)
                // Log.Add(LOG_ERROR,
                //     "[references] CNpcMaker object %d reached zero references while a timer is active. Object destruction
                //     is skipped. Release requested at %s:%d.", MemoryObjectID(), callerFile, callerLine)
                L2_LOG_ERROR(
                    Log,
                    "[references] CNpcMaker object %d reached zero references while a timer is active; object destruction "
                    "is skipped. Release requested at %s:%d.",
                    MemoryObjectID(), callerFile, callerLine
                );
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     "deleting in-timer-object![CNpcMaker]")
                // Log.Add(LOG_ERROR,
                //     "[references] CNpcMaker object %d reached zero references while a timer is active. Object destruction
                //     is skipped. Release caller is unknown.", MemoryObjectID())
                L2_LOG_ERROR(
                    Log,
                    "[references] CNpcMaker object %d reached zero references while a timer is active; object destruction "
                    "is skipped. Release caller is unknown.",
                    MemoryObjectID()
                );
            }
            ReportOrt("NpcMaker.cpp", 0x20);
        }
        ReleaseClassInstance(L2_MEMORY_CLASS_ID(CNpcMaker));
    } else if (remaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Invalid Memory Ex(%d) on CNpcMaker(%d)",
        //     remaining,
        //     MemoryObjectID())
        // Log.Add(LOG_ERROR,
        //     "[references] CNpcMaker object %d: total reference count is negative (%d) after release at %s:%d.",
        //     MemoryObjectID(), remaining, callerFile ? callerFile : "(unknown)", callerLine)
        L2_LOG_ERROR(
            Log, "[references] CNpcMaker object %d: total reference count is negative (%d) after release at %s:%d.",
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
        ReportOrt("NpcMaker.cpp", 0x20);
        ReportCallStack(LOG_ERROR, true);
    }
    return remaining;
}

void CNpcMaker::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    thread_local std::uint32_t reportTick = 0;
    ++reportTick;
    // const int threadId =
    //     CThreadManager::GetThreadID()
    int sequence = 1;

    if (callerFile) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ORT Report][tid:%d][tick:%d][%d] begin "
        //     L"called from file[%S], line[%d] -------------",
        //     threadId,
        //     static_cast<int>(reportTick),
        //     sequence,
        //     callerFile,
        //     callerLine)
        // Log.Add(LOG_ERROR,
        //     "[references] CNpcMaker object %d: begin reference report %u, requested at %s:%d.",
        //     MemoryObjectID(), static_cast<unsigned int>(reportTick), callerFile, callerLine)
        L2_LOG_ERROR(
            Log, "[references] CNpcMaker object %d: begin reference report %u, requested at %s:%d.", MemoryObjectID(),
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
        //     "[references] CNpcMaker object %d: begin reference report %u. Requester unknown.",
        //     MemoryObjectID(), static_cast<unsigned int>(reportTick))
        L2_LOG_ERROR(
            Log, "[references] CNpcMaker object %d: begin reference report %u; requester unknown.", MemoryObjectID(),
            static_cast<unsigned int>(reportTick)
        );
    }

    for (std::size_t index = 0; index < ::RefHelpers::kCount; ++index) {
        const ::RefHelperStruct& record = m_RefHelpers.m_RefData[index];
        if (!record.m_pAddCaller || !record.m_pReleaseCaller || record.m_nRefCount.load(std::memory_order_relaxed) == 0) {
            continue;
        }

        ++sequence;
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
        //     ::ObjectRefTypeName(
        //         static_cast<::ObjectRefType>(index)),
        //     record.m_pAddCaller,
        //     record.m_nAddLine,
        //     record.m_pReleaseCaller,
        //     record.m_nReleaseLine)
        // Log.Add(LOG_ERROR,
        //     "[references] CNpcMaker object %d, report %u, row %d: %s count %d. Last retain %s:%d, last release %s:%d.",
        //     MemoryObjectID(), static_cast<unsigned int>(reportTick), sequence,
        //     WideToUtf8(::ObjectRefTypeName(static_cast<::ObjectRefType>(index))).c_str(),
        //     record.m_nRefCount.load(std::memory_order_relaxed),
        //     record.m_pAddCaller, record.m_nAddLine, record.m_pReleaseCaller, record.m_nReleaseLine)
        L2_LOG_ERROR(
            Log, "[references] CNpcMaker object %d, report %u, row %d: %s count %d; last retain %s:%d, last release %s:%d.",
            MemoryObjectID(), static_cast<unsigned int>(reportTick), sequence,
            WideToUtf8(::ObjectRefTypeName(static_cast<::ObjectRefType>(index))).c_str(),
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
    //     "[references] CNpcMaker object %d: end reference report %u (%d reference rows).",
    //     MemoryObjectID(), static_cast<unsigned int>(reportTick), sequence - 1)
    L2_LOG_ERROR(
        Log, "[references] CNpcMaker object %d: end reference report %u (%d reference rows).", MemoryObjectID(),
        static_cast<unsigned int>(reportTick), sequence - 1
    );
}

bool CNpcMaker::IncreaseTotal(int amount) {
    L2_GUARD_FUNCTION();
    const int updated = runtime::raw_atomic::AddI32(&m_nCreatedNPC, amount);
    if (m_nMaxNPC == 0 || updated <= m_nMaxNPC) {
        return true;
    }

    const std::string territoryName = Utf16ToUtf8(mTerritoryInfo.GetFirstTerritoryName());
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s] failed to increase total npc because of npc quota "
    //     L"[%d / %d]",
    //     territoryName.c_str(),
    //     runtime::raw_atomic::LoadI32Relaxed(
    //         &m_nCreatedNPC),
    //     m_nMaxNPC)
    // Log.Add(LOG_WARN,
    //     "[NPC maker] Territory '%s': increasing the NPC count by %d produced %d, above quota %d. Reverting this
    //     increment.", WideToUtf8(territoryName).c_str(), amount, updated, m_nMaxNPC)
    L2_LOG_WARN(
        Log,
        "[NPC maker] Territory '%s': increasing the NPC count by %d produced %d, above quota %d; reverting this increment.",
        territoryName.c_str(), amount, updated, m_nMaxNPC
    );
    runtime::raw_atomic::AddI32(&m_nCreatedNPC, runtime::Int32FromBits(0U - static_cast<std::uint32_t>(amount)));
    return false;
}

void CNpcMaker::SetAgitId(int agitId) {
    // The optimized target helper at 0x004e443c calls solely from the
    // already selected SpawnTimeType. It does not keep a separate agit-id
    // member.
    switch (m_SpawnTimeType) {
        case SPAWN_AGIT_ATTACK_EVENT:
            CNpcMakerDB::Inst().RegisterAgitAttackEvent(agitId, this);
            return;
        case SPAWN_AGIT_DEFEND_EVENT:
            CNpcMakerDB::Inst().RegisterAgitDefendEvent(agitId, this);
            return;
        case SPAWN_AGIT_BATTLE_ROYAL_EVENT:
            CNpcMakerDB::Inst().RegisterAgitTB_BattleRoyalEvent(agitId, this);
            return;
        case SPAWN_AGIT_FINAL_EVENT:
            CNpcMakerDB::Inst().RegisterAgitTB_FinalEvent(agitId, this);
            return;
        default:
            return;
    }
}

void CNpcMaker::SetColor(int color) { m_nColor = color; }

int CNpcMaker::GetColor() { return m_nColor; }

void CNpcMaker::SetNpcType(int npcType) { m_nNpcType = npcType; }

int CNpcMaker::GetNpcType() { return m_nNpcType; }

void CNpcMaker::SetPledgeId(int pledgeId) { m_nPledgeId = pledgeId; }

int CNpcMaker::GetPledgeId() { return m_nPledgeId; }

int CNpcMaker::CreateMakerKey() { return runtime::raw_atomic::AddI32(&m_nMakerID, 1); }

void CNpcMaker::DoorName(const char16_t* doorName) { mDoorName = std::u16string(doorName); }

const char16_t* CNpcMaker::DoorName() const { return mDoorName.c_str(); }

TerritoryInfo* CNpcMaker::GetTerritoryInfo() { return &mTerritoryInfo; }

void CNpcMaker::SetInitialSpawn(MakerInitalSpawn initialSpawn) { m_InitialSpawn = initialSpawn; }

MakerInitalSpawn CNpcMaker::GetInitialSpawn() { return m_InitialSpawn; }

void CNpcMaker::SetSpawnTimeType(SpawnTimeType spawnTimeType) { m_SpawnTimeType = spawnTimeType; }

SpawnTimeType CNpcMaker::GetSpawnTimeType() { return m_SpawnTimeType; }

void CNpcMaker::SetCastleID(int castleId) {
    L2_GUARD_FUNCTION();
    m_nCastleID = castleId;
    m_bEnabled = false;
    CNpcMakerDB::Inst().RegisterSiegeEvent(this);
}

int CNpcMaker::GetCastleID() { return m_nCastleID; }

void CNpcMaker::SetSiegeType(SiegeDefendType siegeType) { m_SiegeDefendType = siegeType; }

SiegeDefendType CNpcMaker::GetSiegeType() { return m_SiegeDefendType; }

void CNpcMaker::SetDomainID(int domainId) { m_nDomainID = domainId; }

int CNpcMaker::GetDomainID() { return m_nDomainID; }

void CNpcMaker::SetCastleName(std::u16string castleName) {
    m_SpawnTimeType = SPAWN_DOOR_OPEN;
    mDoorName = castleName;
}

void CNpcMaker::SetMaximumNPC(int maximumNpc) { m_nMaxNPC = maximumNpc; }

void CNpcMaker::SetFlying(bool flying) { m_bFlying = flying; }

void CNpcMaker::AddNpcSpawnDefine(CNpcSpawnDefine* definition) {
    L2_GUARD_FUNCTION();
    if (m_InitialSpawn == INITIAL_SPAWN_RANDOM3) {
        if (definition->IsDBSaving()) {
            const std::u16string databaseName = definition->GetDBName();
            const std::string hostDatabaseName = Utf16ToUtf8(databaseName.c_str());
            // Log.Add(
            //     LOG_ERROR,
            //     L"db saving with random spawn[%d] name[%s]",
            //     1,
            //     hostDatabaseName.c_str())
            // Log.Add(LOG_WARN,
            //     "[NPC maker] Database NPC '%s' uses RANDOM3 spawning with persistence enabled. Retaining this
            //     configuration.", WideToUtf8(hostDatabaseName).c_str())
            L2_LOG_WARN(
                Log,
                "[NPC maker] Database NPC '%s' uses RANDOM3 spawning with persistence enabled; retaining this "
                "configuration.",
                hostDatabaseName.c_str()
            );
        }
        if (m_SpawnTimeType != SPAWN_NORMAL) {
            const std::u16string databaseName = definition->GetDBName();
            const std::string hostDatabaseName = Utf16ToUtf8(databaseName.c_str());
            // Log.Add(
            //     LOG_ERROR,
            //     L"spawn time is not normal with random_XX name[%s]",
            //     hostDatabaseName.c_str())
            // Log.Add(LOG_WARN,
            //     "[NPC maker] Database NPC '%s' combines RANDOM3 spawning with non-normal spawn-time mode %d. Retaining
            //     this configuration.", WideToUtf8(hostDatabaseName).c_str(), static_cast<int>(m_SpawnTimeType))
            L2_LOG_WARN(
                Log,
                "[NPC maker] Database NPC '%s' combines RANDOM3 spawning with non-normal spawn-time mode %d; retaining this "
                "configuration.",
                hostDatabaseName.c_str(), static_cast<int>(m_SpawnTimeType)
            );
        }
    }
    definition->SetNpcMaker(this);
    m_NpcSpawnDefines.push_back(definition);
    if (!definition->GetNickName().empty()) {
        m_NickNameToNpcSpawnDefine[definition->GetNickName()] = definition;
    }
}

void CNpcMaker::CreateNPC(bool firstSpawn, bool allowDatabaseLoad) {
    L2_GUARD_FUNCTION();
    const int mode = static_cast<int>(m_InitialSpawn);
    if (mode < static_cast<int>(INITIAL_SPAWN_ALL) || mode > static_cast<int>(INITIAL_SPAWN_RANDOM3)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Undefined SpawnTime",
        //     L"NpcMaker.cpp",
        //     0x15c)
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Invalid initial-spawn mode %d for maker object %d. Skipping NPC creation.",
        //     mode, MemoryObjectID())
        L2_LOG_ERROR(
            Log, "[NPC maker] Invalid initial-spawn mode %d for maker object %d; skipping NPC creation.", mode,
            MemoryObjectID()
        );
        return;
    }

    const int spawnDefineCount = GetNpcSpawnDefineSize();
    for (int index = 0; index < spawnDefineCount; ++index) {
        CNpcSpawnDefine* definition = GetNpcSpawnDefine(index);
        if (!definition) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"ERROR: NULL NpcSpawnDefine . I[%d] "
            //     L"at file[%s], line[%d]",
            //     index,
            //     L"NpcMaker.cpp",
            //     0x155)
            // Log.Add(LOG_ERROR,
            //     "[NPC maker] Spawn definition %d of %d is null for maker object %d. Skipping this entry.",
            //     index, spawnDefineCount, MemoryObjectID())
            // L2_LOG_ERROR(Log,
            //     "[NPC maker] Spawn definition %d of %d is null for maker object %d. Skipping this entry.",
            //     index, spawnDefineCount, MemoryObjectID())
            L2_LOG_ERROR(
                Log, "[NPC maker] Spawn definition %d of %d is null for maker object %d; skipping this entry.", index + 1,
                spawnDefineCount, MemoryObjectID()
            );
            continue;
        }
        definition->CreateNPC(firstSpawn, 0, allowDatabaseLoad, -1);
    }
}

void CNpcMaker::CreateNPC(std::u16string nickname, int respawnSeconds) {
    L2_GUARD_FUNCTION();
    const auto found = m_NickNameToNpcSpawnDefine.find(nickname);
    if (found == m_NickNameToNpcSpawnDefine.end()) {
        return;
    }

    // The shipping body performs a second tree lookup through
    // operator[] after the successful find instead of reusing the iterator.
    CNpcSpawnDefine& definition = *m_NickNameToNpcSpawnDefine[nickname];
    const int count = definition.GetTotal();
    if (!IncreaseTotal(count)) {
        return;
    }
    definition.IncCount(count);
    definition.Spawn(count, respawnSeconds);
}

void CNpcMaker::OnStart() {
    L2_GUARD_FUNCTION();
    switch (m_SpawnTimeType) {
        case SPAWN_NORMAL:
            CreateNPC(true, true);
            return;

        case SPAWN_DOOR_OPEN:
            CNpcMakerDB::Inst().RegisterDoorEvent(mDoorName.c_str(), this);
            m_bEnabled = false;
            return;

        case SPAWN_POS_EVENT:
            CNpcMakerDB::Inst().RegisterNpcPosEvent(m_EventName.c_str(), this);
            m_bEnabled = false;
            return;

        default:
            return;
    }
}

void CNpcMaker::TimerExpired(int timerId) {
    L2_GUARD_FUNCTION();
    if (GetTotalNPC() == 0) {
        m_bEnabled = true;
        CreateNPC(true, timerId != 0);
        return;
    }
    AddTimer(1000, timerId);
}

void CNpcMaker::DespawnAll() {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    for (const unsigned int objectId : m_NpcSet) {
        CNPC* const npc = CNPC::GetObjectW(static_cast<int>(objectId));
        if (npc) {
            npc->Despawn();
        }
    }
}

void CNpcMaker::SiegeEvent(int residenceId [[maybe_unused]], int event, bool allowDatabaseLoad) {
    L2_GUARD_FUNCTION();
    if (event == 1) {
        if (GetTotalNPC() == 0) {
            m_bEnabled = true;
            CreateNPC(true, allowDatabaseLoad);
        } else {
            AddTimer(1000, allowDatabaseLoad ? 1 : 0);
        }
    } else if (event == 2) {
        m_bEnabled = false;
        DespawnAll();
    }
}

void CNpcMaker::EndTeamBattle() {
    L2_GUARD_FUNCTION();
    m_bEnabled = false;
    DespawnAll();
}

void CNpcMaker::AgitEvent(int enabled, bool allowDatabaseLoad) {
    L2_GUARD_FUNCTION();
    if (enabled == 0) {
        m_bEnabled = false;
        DespawnAll();
    } else if (enabled == 1) {
        m_bEnabled = true;
        CreateNPC(true, allowDatabaseLoad);
    }
}

void CNpcMaker::DoorEvent(int enabled) {
    L2_GUARD_FUNCTION();
    if (enabled == 0) {
        if (!m_bEnabled) {
            return;
        }
        m_bEnabled = false;
        DespawnAll();
        return;
    }
    if (enabled == 1 && !m_bEnabled) {
        m_bEnabled = true;
        CreateNPC(true, true);
    }
}

void CNpcMaker::SetEventName(const char16_t* eventName) {
    L2_GUARD_FUNCTION();
    m_EventName = std::u16string(eventName);
    m_SpawnTimeType = SPAWN_POS_EVENT;
}

const char16_t* CNpcMaker::GetEventName() const { return m_EventName.c_str(); }

void CNpcMaker::SetEnabled(bool enabled) { m_bEnabled = enabled; }

bool CNpcMaker::IsEnabled() const { return m_bEnabled; }

void CNpcMaker::OnNpcPosEvent(bool enabled) {
    L2_GUARD_FUNCTION();
    if (!mTerritoryInfo.RandomposGenerated()) {
        const std::string eventName = Utf16ToUtf8(m_EventName.c_str());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Can't handle NPC POS EVENT(name[%s], "
        //     L"flag[%d]). Random pos not generated.",
        //     L"NpcMaker.cpp",
        //     0x282,
        //     eventName.c_str(),
        //     enabled ? 1 : 0)
        // Log.Add(LOG_WARN,
        //     "[NPC maker] Cannot %s position event '%s': random positions are not initialized. Leaving the maker state
        //     unchanged.", enabled ? "enable" : "disable", WideToUtf8(eventName).c_str())
        // L2_LOG_WARN(Log,
        //     "[NPC maker] Cannot %s position event '%s': random positions are not initialized. Leaving the maker state
        //     unchanged.", enabled ? "enable" : "disable", WideToUtf8(eventName).c_str())
        L2_LOG_WARN(
            Log,
            "[NPC maker] Cannot %s position event '%s' for maker object %d: random positions are not initialized; leaving "
            "the maker state unchanged.",
            enabled ? "enable" : "disable", eventName.c_str(), MemoryObjectID()
        );
        return;
    }
    if (!enabled) {
        if (!m_bEnabled) {
            return;
        }
        m_bEnabled = false;
        DespawnAll();
        return;
    }
    if (!m_bEnabled) {
        m_bEnabled = true;
        CreateNPC(enabled, true);
    }
}

void CNpcMaker::StartTeamBattle(bool allowDatabaseLoad) {
    L2_GUARD_FUNCTION();
    m_bEnabled = true;
    CreateNPC(true, allowDatabaseLoad);
}

int CNpcMaker::DecreaseTotal(int amount) {
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::AddI32(
        &m_nCreatedNPC, runtime::Int32FromBits(0U - static_cast<std::uint32_t>(amount))
    );
}

void CNpcMaker::RegisterNpc(CNPC* npc) {
    L2_GUARD_FUNCTION();
    if (!npc) {
        return;
    }

    L2_GUARD(mLock);
    m_NpcSet.insert(static_cast<unsigned int>(npc->Id()));
    if (npc->m_nMakerID > 0) {
        m_MakerMap[npc->m_nMakerID] = static_cast<int>(npc->Id());
    }
}

void CNpcMaker::UnregisterNpc(CNPC* npc) {
    L2_GUARD_FUNCTION();
    if (!npc) {
        return;
    }

    L2_GUARD(mLock);
    if (npc->m_nMakerID > 0) {
        m_MakerMap.erase(npc->m_nMakerID);
    }
    m_NpcSet.erase(static_cast<unsigned int>(npc->Id()));
}

void CNpcMaker::OnNpcEnterWorld(CNPC* npc) {
    L2_GUARD_FUNCTION();
    RegisterNpc(npc);
}

void CNpcMaker::OnNpcDeleted(CNPC* npc) {
    L2_GUARD_FUNCTION();
    const int remaining = DecreaseTotal(1);
    UnregisterNpc(npc);
    if (m_InitialSpawn != INITIAL_SPAWN_RANDOM3 || remaining != 0) {
        return;
    }

    // NpcMaker.cpp:589-616. RANDOM3 is a batch lifecycle mode: when the
    // maker reaches zero, every definition reserves its complete m_nTotal,
    // increments m_nCreated, and goes through CNpcSpawnDefine::Spawn using
    // that definition's jittered respawn time.
    const int spawnDefineCount = GetNpcSpawnDefineSize();
    for (int index = 0; index < spawnDefineCount; ++index) {
        CNpcSpawnDefine* const definition = GetNpcSpawnDefine(index);
        const int count = definition->GetTotal();
        if (!IncreaseTotal(count)) {
            continue;
        }
        definition->IncCount(count);
        definition->Spawn(count, definition->GetRespawnTime());
    }
}

CSharedCreatureData* CNpcMaker::GetNpcFromKey(int key) {
    int objectId = 0;
    {
        L2_GUARD(mLock);
        const auto found = m_MakerMap.find(key);
        if (found != m_MakerMap.end()) {
            objectId = found->second;
        }
    }

    CNPC* npc = CNPC::GetObjectW(objectId);
    return npc ? npc->m_data : nullptr;
}

int CNpcMaker::GetNpcSetSize() {
    L2_GUARD(mLock);
    return static_cast<int>(m_NpcSet.size());
}

CNpcMakerReleaseVerifier::CNpcMakerReleaseVerifier(CNpcMaker* npcMaker) : m_pNpcMaker(npcMaker) {
    // This body was eliminated from the current image. NASC64_2015
    // NpcMaker.cpp:165-167 keeps the exact reference type, timer delay and
    // call-site lines for the otherwise identical current PDB type.
    L2_ADD_REF(m_pNpcMaker, ::ObjectRefType::VerifierRetain);
    AddTimer(30000U, 0);
    L2_RELEASE(this, ::ObjectRefType::CreateDelete);
}

void CNpcMakerReleaseVerifier::TimerExpired(int) {
    const std::int32_t remaining = L2_RELEASE(m_pNpcMaker, ::ObjectRefType::VerifierRetain);
    if (remaining > 0) {
        m_pNpcMaker->ReportOrt(nullptr, 0);
    }
}

::MemoryObject* CNpcMakerWorld::AddRef(const char* callerFile, int callerLine, ::ObjectRefType type) {
    // Both current primary vtables point at the same ICF-folded body,
    // 0x004e441c. Keep the override body identical to CNpcMaker::AddRef.
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);

    ::RefHelperStruct& record = m_RefHelpers.m_RefData[RefTypeIndex(type)];
    record.m_nRefCount.fetch_add(1, std::memory_order_seq_cst);
    record.m_pAddCaller = callerFile;
    record.m_nAddLine = callerLine;
    return this;
}

std::int32_t CNpcMakerWorld::Release(const char* callerFile, int callerLine, ::ObjectRefType type, bool) {
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
        //     "[references] CNpcMakerWorld object %d: release at %s:%d made %s count negative (%d). Observed count now %d,
        //     last retain %s:%d, previous release %s:%d.", MemoryObjectID(), callerFile ? callerFile : "(unknown)",
        //     callerLine, WideToUtf8(::ObjectRefTypeName(type)).c_str(), subtypeRemaining,
        //     record.m_nRefCount.load(std::memory_order_relaxed),
        //     record.m_pAddCaller ? record.m_pAddCaller : "(unknown)", record.m_nAddLine,
        //     record.m_pReleaseCaller ? record.m_pReleaseCaller : "(unknown)", record.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            "[references] CNpcMakerWorld object %d: release at %s:%d made %s count negative (%d); observed count now %d, "
            "last retain %s:%d, previous release %s:%d.",
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
        ReportOrt("NpcMaker.cpp", 0x1b);
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
                //     "deleting in-timer-object![CNpcMakerWorld] "
                //     "at file[%s], line[%d]",
                //     callerFile,
                //     callerLine)
                // Log.Add(LOG_ERROR,
                //     "[references] CNpcMakerWorld object %d reached zero references while a timer is active. Object
                //     destruction is skipped. Release requested at %s:%d.", MemoryObjectID(), callerFile, callerLine)
                L2_LOG_ERROR(
                    Log,
                    "[references] CNpcMakerWorld object %d reached zero references while a timer is active; object "
                    "destruction is skipped. Release requested at %s:%d.",
                    MemoryObjectID(), callerFile, callerLine
                );
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     "deleting in-timer-object![CNpcMakerWorld]")
                // Log.Add(LOG_ERROR,
                //     "[references] CNpcMakerWorld object %d reached zero references while a timer is active. Object
                //     destruction is skipped. Release caller is unknown.", MemoryObjectID())
                L2_LOG_ERROR(
                    Log,
                    "[references] CNpcMakerWorld object %d reached zero references while a timer is active; object "
                    "destruction is skipped. Release caller is unknown.",
                    MemoryObjectID()
                );
            }
            ReportOrt("NpcMaker.cpp", 0x1b);
        }
        ReleaseClassInstance(L2_MEMORY_CLASS_ID(CNpcMakerWorld));
    } else if (remaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Invalid Memory Ex(%d) on CNpcMakerWorld(%d)",
        //     remaining,
        //     MemoryObjectID())
        // Log.Add(LOG_ERROR,
        //     "[references] CNpcMakerWorld object %d: total reference count is negative (%d) after release at %s:%d.",
        //     MemoryObjectID(), remaining, callerFile ? callerFile : "(unknown)", callerLine)
        L2_LOG_ERROR(
            Log, "[references] CNpcMakerWorld object %d: total reference count is negative (%d) after release at %s:%d.",
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
        ReportOrt("NpcMaker.cpp", 0x1b);
        ReportCallStack(LOG_ERROR, true);
    }
    return remaining;
}

void CNpcMakerWorld::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    thread_local std::uint32_t reportTick = 0;
    ++reportTick;
    // const int threadId =
    //     CThreadManager::GetThreadID()
    int sequence = 1;

    if (callerFile) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ORT Report][tid:%d][tick:%d][%d] begin "
        //     L"called from file[%S], line[%d] -------------",
        //     threadId,
        //     static_cast<int>(reportTick),
        //     sequence,
        //     callerFile,
        //     callerLine)
        // Log.Add(LOG_ERROR,
        //     "[references] CNpcMakerWorld object %d: begin reference report %u, requested at %s:%d.",
        //     MemoryObjectID(), static_cast<unsigned int>(reportTick), callerFile, callerLine)
        L2_LOG_ERROR(
            Log, "[references] CNpcMakerWorld object %d: begin reference report %u, requested at %s:%d.", MemoryObjectID(),
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
        //     "[references] CNpcMakerWorld object %d: begin reference report %u. Requester unknown.",
        //     MemoryObjectID(), static_cast<unsigned int>(reportTick))
        L2_LOG_ERROR(
            Log, "[references] CNpcMakerWorld object %d: begin reference report %u; requester unknown.", MemoryObjectID(),
            static_cast<unsigned int>(reportTick)
        );
    }

    for (std::size_t index = 0; index < ::RefHelpers::kCount; ++index) {
        const ::RefHelperStruct& record = m_RefHelpers.m_RefData[index];
        if (!record.m_pAddCaller || !record.m_pReleaseCaller || record.m_nRefCount.load(std::memory_order_relaxed) == 0) {
            continue;
        }

        ++sequence;
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
        //     ::ObjectRefTypeName(
        //         static_cast<::ObjectRefType>(index)),
        //     record.m_pAddCaller,
        //     record.m_nAddLine,
        //     record.m_pReleaseCaller,
        //     record.m_nReleaseLine)
        // Log.Add(LOG_ERROR,
        //     "[references] CNpcMakerWorld object %d, report %u, row %d: %s count %d. Last retain %s:%d, last release
        //     %s:%d.", MemoryObjectID(), static_cast<unsigned int>(reportTick), sequence,
        //     WideToUtf8(::ObjectRefTypeName(static_cast<::ObjectRefType>(index))).c_str(),
        //     record.m_nRefCount.load(std::memory_order_relaxed),
        //     record.m_pAddCaller, record.m_nAddLine, record.m_pReleaseCaller, record.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            "[references] CNpcMakerWorld object %d, report %u, row %d: %s count %d; last retain %s:%d, last release %s:%d.",
            MemoryObjectID(), static_cast<unsigned int>(reportTick), sequence,
            WideToUtf8(::ObjectRefTypeName(static_cast<::ObjectRefType>(index))).c_str(),
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
    //     "[references] CNpcMakerWorld object %d: end reference report %u (%d reference rows).",
    //     MemoryObjectID(), static_cast<unsigned int>(reportTick), sequence - 1)
    L2_LOG_ERROR(
        Log, "[references] CNpcMakerWorld object %d: end reference report %u (%d reference rows).", MemoryObjectID(),
        static_cast<unsigned int>(reportTick), sequence - 1
    );
}

} // namespace l2npc

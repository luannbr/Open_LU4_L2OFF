#pragma once

#include "Shared/src/MemorySub.h"

#include "BaseSpawnDefine.h"
#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/ScriptAccessible.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TerritoryInfo.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <new>
#include <string>


// Exact PDB enum stored in CNpcMaker::m_SpawnTimeType. In particular,
// AGIT_ATTACK is 2 and AGIT_DEFEND is 3. Reversing these values changes the
// target registration map and siege-related numeric range checks.
enum SpawnTimeType : int {
    SPAWN_NORMAL = 0,
    SPAWN_SIEGE_EVENT = 1,
    SPAWN_AGIT_ATTACK_EVENT = 2,
    SPAWN_AGIT_DEFEND_EVENT = 3,
    SPAWN_DOOR_OPEN = 4,
    SPAWN_AGIT_BATTLE_ROYAL_EVENT = 5,
    SPAWN_AGIT_FINAL_EVENT = 6,
    SPAWN_POS_EVENT = 7,
    SPAWN_FORTRESS_EVENT = 8,
    SPAWN_INSTANT_ZONE_EVENT = 9,
    SPAWN_POS_MAX = 10,
};

enum SiegeDefendType : int {
    NPC_DEFEND_SIEGE = 0,
    PC_DEFEND_SIEGE = 1,
    EXTENDED_DOMINION_SIEGE = 2,
};

// PDB spelling kept unchanged.
enum MakerInitalSpawn : int {
    INITIAL_SPAWN_ALL = 0,
    INITIAL_SPAWN_RANDOM3 = 1,
};

static_assert(
    SPAWN_NORMAL == 0 && SPAWN_SIEGE_EVENT == 1 && SPAWN_AGIT_ATTACK_EVENT == 2 && SPAWN_AGIT_DEFEND_EVENT == 3 &&
        SPAWN_DOOR_OPEN == 4 && SPAWN_AGIT_BATTLE_ROYAL_EVENT == 5 && SPAWN_AGIT_FINAL_EVENT == 6 && SPAWN_POS_EVENT == 7 &&
        SPAWN_FORTRESS_EVENT == 8 && SPAWN_INSTANT_ZONE_EVENT == 9 && SPAWN_POS_MAX == 10,
    "SpawnTimeType values must retain the PDB/target ABI"
);
static_assert(
    NPC_DEFEND_SIEGE == 0 && PC_DEFEND_SIEGE == 1 && EXTENDED_DOMINION_SIEGE == 2,
    "SiegeDefendType values must retain the PDB/target ABI"
);


namespace l2npc {

// Exact script-visible record declared in l2npc/NpcMaker.h:737-773.
// Windows wchar_t is represented as char16_t so every field keeps
// the shipping 64-bit offset on all supported hosts.
class DBNpcInfo final : public ScriptAccessibleEx {
public:
    DBNpcInfo() = default;
    ~DBNpcInfo() override;

    void CollectVar(ScriptTypeInfo* typeInfo) override;

    ScriptAccessibleEx* GetScriptAccessibleEx() const override;

    std::array<char16_t, 25> dbname{};
    std::uint16_t padding = 0;
    int hp = 0;
    int mp = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int death_time = 0;
    int is_alive = 0;
    int db_value = 0;
    int arg1 = 0;
};


// Exact five-int SpawnPos record kept by CNpcSpawnDefine's pointer
// vector. Copies of a definition duplicate the vector but share these
// pointed-to records.
struct SpawnPos {
    SpawnPos(int xx, int yy, int zz, int dd, int pp) {
        L2_GUARD_FUNCTION();
        x = xx;
        y = yy;
        z = zz;
        dir = dd;
        prob = pp;
    }

    int x;
    int y;
    int z;
    int dir;
    int prob;
};


// Recovered regular spawn definition from NpcMaker.h/NpcSpawnDefine.cpp.
// The target object owns all behavioral state below. Parser records and
// opaque pointer tokens belong to the portable dispatcher, not to this class.
class CNpcSpawnDefine : public BaseSpawnDefine {
public:
    CNpcSpawnDefine();
    CNpcSpawnDefine(const CNpcSpawnDefine& other);
    ~CNpcSpawnDefine() override;

    L2_DECLARE_OBJECT_TYPE(CNpcSpawnDefine)

    CNpcSpawnDefine* Copy() { return new CNpcSpawnDefine(*this); }
    bool IsSpawnDefine();
    CNpcSpawnDefine* CastSpawnDefine();

    bool CreateNPC(bool firstSpawn, int count, bool allowDatabaseLoad, int respawnSeconds);
    void AddSpawnPosition(SpawnPos* position);
    void CheckAndModifyByBossRespawn();
    int DecCount(int amount);
    virtual const char16_t* GetAI();
    AiParameterMap& GetAIParameters();
    const char16_t* GetName();
    std::u16string GetNickName();
    int GetTotal();
    int GetPledgeId();
    std::u16string GetPrivates();
    int GetRespawnTime();
    SpawnPos* GetSpawnPosition(int index);
    int GetSpawnPositionSize();
    std::vector<int> GetWayPointDelays();
    std::vector<FVector> GetWayPoints();
    bool HaveAIParameter();
    bool HavePrivates();
    bool HaveWayPoint();
    bool HaveWayPointDelay();
    bool IsAnywhere();
    bool IsOnDeleteCreate();
    int GetOnDeleteCreateRespawnTime();
    int GetDBSavingFlag();
    bool IsDBLoaded();
    bool IsDBSaving();
    std::u16string GetDBName();
    bool IsBossRespawn();
    int IncCount(int amount);
    void SetAI(const char16_t* aiName);
    void SetAIParameters(const AiParameterMap& parameters);
    void SetAnywhere(bool anywhere);
    void SetBossRespawn(bool value);
    void SetChasePC(int distance);
    void SetDBSaving(char16_t* databaseName, int savingFlags);
    void SetDBLoaded(bool value);
    void SetName(char16_t* name);
    void SetNickName(const char16_t* nickname);
    void SetNpcMaker(class CNpcMaker* maker);
    CNpcMaker* GetNpcMaker();
    void SetOnDeleteCreate(int respawnSeconds, std::vector<std::u16string> npcNames);
    void SetPrivates(const char16_t* privates);
    void SetRespawnRandTime(int seconds);
    void SetRespawnTime(int seconds);
    void SetTotal(int total);
    void SetWayPointDelays(std::vector<int> delays);
    void SetWayPoints(std::vector<FVector> points);
    int Spawn(int count, int respawnSeconds);
    int SpawnEx(int count, int baseTime, int x, int y, int z, int direction, int hp, int mp);
    int Spawn2(int count, int baseTime, int spread);
    int SpawnEx2(int count, int baseTime, int spread, int x, int y, int z, int direction, int hp, int mp);
    void SendCreateNpcPacket();
    int CreateMakerKey() override;
    FVector GenerateSpawnPos(int& heading, bool withSky) override;
    int GetChaseDistance() override;
    CSharedCreatureData* GetNpcFromKey(int key) override;
    TerritoryInfo* GetTerritoryInfo() override;
    int GetTotalNPC() override;
    bool IsInBannedTerritory(FVector position, bool includeZ) override;
    bool IsInTerritory(FVector position, bool includeZ) override;
    bool IsInTerritoryWithLogVisitor(FVector position, ZLogVisitor& visitor, bool includeZ) override;
    void CheckZPos(const FVector& position, bool isPrivate);
    void OnNpcCreated(CNPC* npc) override;
    void OnNpcDeleted(CNPC* npc) override;
    void OnNpcEnterWorld(CNPC* npc) override;
    bool ReserveNPC() override;
    bool SetAIParameter(CNPC* npc, const char16_t* aiName, AiParameterMap& parameters) override;
    void SpawnNpc(bool alreadyReserved) override;

    // These three members are public in the target PDB and current EXE
    // layout. All remaining target fields are private.
    bool bOnDeleteCreate;
    std::vector<std::u16string> sOnDeleteCreateNpc;
    int nOnDeleteCreateRespawnTime;

protected:
    void TimerExpired(int timerId) override;

private:
    std::u16string m_sName;
    int m_nTotal;
    int m_nRespawnTime;
    int nRespawnRandVarTime;
    bool bAnywhere;
    // The target vector stores non-owning SpawnPos pointers. Parser lifetime
    // owns the records. Copying a definition copies only these pointers.
    std::vector<SpawnPos*> m_SpawnPositions;
    class CNpcMaker* mpNpcMaker;
    std::u16string m_sAI;
    std::u16string m_sPrivates;
    std::u16string m_sNickName;
    std::vector<FVector> m_WayPoints;
    std::vector<int> m_WayPointDelays;
    volatile std::int32_t m_nCreated;
    bool m_bDBSaving;
    int m_DBSavingFlag;
    bool m_bDBLoaded;
    std::u16string m_sDBName;
    AiParameterMap m_AIParams;
    double m_minZ;
    bool m_bBossRespawnApplied;
    int m_nChaseDistance;
};


inline CNpcSpawnDefine::CNpcSpawnDefine() :
    bOnDeleteCreate(false),
    nOnDeleteCreateRespawnTime(0),
    nRespawnRandVarTime(0),
    mpNpcMaker(nullptr),
    m_nCreated(0),
    m_bDBSaving(false),
    m_bDBLoaded(false),
    m_minZ(-65536.0),
    m_bBossRespawnApplied(false),
    m_nChaseDistance(0) {
    L2_GUARD_FUNCTION();
    m_nTotal = 0;
    m_nRespawnTime = 0;
    bAnywhere = false;
    m_DBSavingFlag = 0;
    m_SpawnPositions.clear();
    bOnDeleteCreate = false;
    nOnDeleteCreateRespawnTime = 0;
    m_bBossRespawnApplied = false;
    m_nChaseDistance = 0;
}

inline CNpcSpawnDefine::~CNpcSpawnDefine() { mpNpcMaker = nullptr; }

inline bool CNpcSpawnDefine::IsSpawnDefine() { return true; }

inline CNpcSpawnDefine* CNpcSpawnDefine::CastSpawnDefine() { return this; }

inline void CNpcSpawnDefine::AddSpawnPosition(SpawnPos* position) {
    L2_GUARD_FUNCTION();
    if (const double z = position->z; m_minZ == -65536.0 || !(z > m_minZ)) {
        m_minZ = z;
    }
    m_SpawnPositions.push_back(position);
}

inline const char16_t* CNpcSpawnDefine::GetAI() {
    L2_GUARD_FUNCTION();
    return m_sAI.empty() ? nullptr : m_sAI.c_str();
}

inline BaseSpawnDefine::AiParameterMap& CNpcSpawnDefine::GetAIParameters() { return m_AIParams; }

inline const char16_t* CNpcSpawnDefine::GetName() {
    L2_GUARD_FUNCTION();
    return m_sName.c_str();
}

inline std::u16string CNpcSpawnDefine::GetNickName() { return m_sNickName; }

inline int CNpcSpawnDefine::GetTotal() { return m_nTotal; }

inline std::u16string CNpcSpawnDefine::GetPrivates() {
    L2_GUARD_FUNCTION();
    return m_sPrivates;
}

inline std::vector<int> CNpcSpawnDefine::GetWayPointDelays() {
    L2_GUARD_FUNCTION();
    return m_WayPointDelays;
}

inline std::vector<FVector> CNpcSpawnDefine::GetWayPoints() {
    L2_GUARD_FUNCTION();
    return m_WayPoints;
}

inline bool CNpcSpawnDefine::HaveAIParameter() { return !m_AIParams.empty(); }

inline bool CNpcSpawnDefine::HavePrivates() {
    L2_GUARD_FUNCTION();
    return !m_sPrivates.empty();
}

inline bool CNpcSpawnDefine::HaveWayPoint() {
    L2_GUARD_FUNCTION();
    return !m_WayPoints.empty();
}

inline bool CNpcSpawnDefine::HaveWayPointDelay() {
    L2_GUARD_FUNCTION();
    return !m_WayPointDelays.empty();
}

inline bool CNpcSpawnDefine::IsAnywhere() { return bAnywhere; }

inline bool CNpcSpawnDefine::IsOnDeleteCreate() { return bOnDeleteCreate; }

inline int CNpcSpawnDefine::GetOnDeleteCreateRespawnTime() { return nOnDeleteCreateRespawnTime; }

inline int CNpcSpawnDefine::GetDBSavingFlag() { return m_DBSavingFlag; }

inline bool CNpcSpawnDefine::IsDBLoaded() { return m_bDBLoaded; }

inline bool CNpcSpawnDefine::IsDBSaving() { return m_bDBSaving; }

inline std::u16string CNpcSpawnDefine::GetDBName() { return m_sDBName; }

inline bool CNpcSpawnDefine::IsBossRespawn() { return m_bBossRespawnApplied; }

inline void CNpcSpawnDefine::SetAI(const char16_t* aiName) {
    L2_GUARD_FUNCTION();
    m_sAI = std::u16string(aiName);
}

inline void CNpcSpawnDefine::SetAIParameters(const AiParameterMap& parameters) { m_AIParams = parameters; }

inline void CNpcSpawnDefine::SetAnywhere(bool anywhere) { bAnywhere = anywhere; }

inline void CNpcSpawnDefine::SetBossRespawn(bool value) { m_bBossRespawnApplied = value; }

inline void CNpcSpawnDefine::SetChasePC(int distance) { m_nChaseDistance = distance; }

inline void CNpcSpawnDefine::SetDBSaving(char16_t* databaseName, int savingFlags) {
    L2_GUARD_FUNCTION();
    m_bDBSaving = true;
    m_DBSavingFlag = savingFlags;
    m_sDBName = std::u16string(databaseName);
}

inline void CNpcSpawnDefine::SetDBLoaded(bool value) { m_bDBLoaded = value; }

inline void CNpcSpawnDefine::SetName(char16_t* name) {
    L2_GUARD_FUNCTION();
    m_sName = std::u16string(name);
}

inline void CNpcSpawnDefine::SetNickName(const char16_t* nickname) {
    L2_GUARD_FUNCTION();
    m_sNickName = std::u16string(nickname);
}

inline void CNpcSpawnDefine::SetNpcMaker(CNpcMaker* maker) { mpNpcMaker = maker; }

inline CNpcMaker* CNpcSpawnDefine::GetNpcMaker() { return mpNpcMaker; }

inline void CNpcSpawnDefine::SetOnDeleteCreate(int respawnSeconds, std::vector<std::u16string> npcNames) {
    bOnDeleteCreate = true;
    nOnDeleteCreateRespawnTime = respawnSeconds;
    sOnDeleteCreateNpc = npcNames;
}

inline void CNpcSpawnDefine::SetPrivates(const char16_t* privates) {
    L2_GUARD_FUNCTION();
    m_sPrivates = std::u16string(privates);
}

inline void CNpcSpawnDefine::SetTotal(int total) { m_nTotal = total; }

inline void CNpcSpawnDefine::SetWayPointDelays(std::vector<int> delays) {
    L2_GUARD_FUNCTION();
    m_WayPointDelays = delays;
}

inline void CNpcSpawnDefine::SetWayPoints(std::vector<FVector> points) {
    L2_GUARD_FUNCTION();
    m_WayPoints = points;
}

inline int CNpcSpawnDefine::GetChaseDistance() { return m_nChaseDistance; }

// Recovered from CNpcMaker/CNpcSpawnDefine in npc maker sources
// (NpcMaker.cpp, NpcMakerCommon.cpp and NpcSpawnDefine.cpp). This is the
// regular npcpos maker, distinct from the script-driven NpcMakerEx.
class CNpcMaker : public CIOObject {
public:
    CNpcMaker();
    CNpcMaker(const CNpcMaker& other);
    ~CNpcMaker() override;

    L2_DECLARE_MEMORY_ALLOCATION(CNpcMaker)

    CNpcMaker* Copy() { return new CNpcMaker(*this); }

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    virtual bool IncreaseTotal(int amount);
    void SetAgitId(int agitId);
    void SetColor(int color);
    int GetColor();
    void SetNpcType(int npcType);
    int GetNpcType();
    void SetPledgeId(int pledgeId);
    int GetPledgeId();
    int CreateMakerKey();
    void DoorName(const char16_t* doorName);
    const char16_t* DoorName() const;
    TerritoryInfo* GetTerritoryInfo();
    void AddNpcSpawnDefine(CNpcSpawnDefine* definition);
    CNpcSpawnDefine* GetNpcSpawnDefine(int index);
    int GetNpcSpawnDefineSize();
    void SetInitialSpawn(MakerInitalSpawn initialSpawn);
    MakerInitalSpawn GetInitialSpawn();
    void SetSpawnTimeType(SpawnTimeType spawnTimeType);
    SpawnTimeType GetSpawnTimeType();
    void SetCastleID(int castleId);
    int GetCastleID();
    void SetSiegeType(SiegeDefendType siegeType);
    SiegeDefendType GetSiegeType();
    void SetDomainID(int domainId);
    int GetDomainID();
    void SetCastleName(std::u16string castleName);
    void SetMaximumNPC(int maximumNpc);
    void SetFlying(bool flying);
    void CreateNPC(bool firstSpawn, bool allowDatabaseLoad);
    void CreateNPC(std::u16string nickname, int respawnSeconds);
    void OnStart();
    void TimerExpired(int timerId) override;
    void DespawnAll();
    void SiegeEvent(int residenceId, int event, bool allowDatabaseLoad);
    void EndTeamBattle();
    void AgitEvent(int enabled, bool allowDatabaseLoad);
    void DoorEvent(int enabled);
    void SetEventName(const char16_t* eventName);
    const char16_t* GetEventName() const;
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void OnNpcPosEvent(bool enabled);
    void StartTeamBattle(bool allowDatabaseLoad);
    int DecreaseTotal(int amount);
    int GetTotalNPC();
    int GetMaximumNPC();

    void RegisterNpc(CNPC* npc);
    void UnregisterNpc(CNPC* npc);
    void OnNpcEnterWorld(CNPC* npc);
    void OnNpcDeleted(CNPC* npc);
    CSharedCreatureData* GetNpcFromKey(int key);
    int GetNpcSetSize();

    // CNpcMaker derives from the same intrusive object root as CNPC and
    // maintains the same 36 per-reference diagnostics in the target.
    ::RefHelpers m_RefHelpers;
    // Exact CNpcMaker::m_bFlying at target +0x498. ParserSpawn stores
    // `flying == 1`, and CNpcMakerDB passes this member to
    // TerritoryInfo::InitializeRandomPos.
    bool m_bFlying;

private:
    // PDB-private source state, in target declaration order.
    // PDB spelling kept unchanged.
    MakerInitalSpawn m_InitialSpawn;
    SpawnTimeType m_SpawnTimeType;
    int m_nCastleID;
    SiegeDefendType m_SiegeDefendType;
    int m_nDomainID;
    // The target constructor uses -1 as the "no team color" sentinel.
    int m_nColor;
    int m_nNpcType;
    int m_nPledgeId;
    std::u16string mDoorName;
    CYieldLock mLock;
    std::set<unsigned int> m_NpcSet;
    // Exact m_NpcSpawnDefines ownership: AddNpcSpawnDefine transfers each
    // object's initial ORT_CREATE_DELETE reference without incrementing it.
    std::vector<CNpcSpawnDefine*> m_NpcSpawnDefines;

public:
    std::map<std::u16string, CNpcSpawnDefine*, runtime::CStringLess<std::u16string>> m_NickNameToNpcSpawnDefine;
    // Exact embedded CNpcMaker::mTerritoryInfo (+0x558 in the target).
    TerritoryInfo mTerritoryInfo;
    int m_nMaxNPC;
    // PDB type 0x1f8a is `volatile long`, not std::atomic<long>. The target
    // uses LOCK XADD only in the mutating helpers and a plain MOV in
    // GetTotalNPC.
    volatile std::int32_t m_nCreatedNPC;
    bool m_bEnabled;
    // Exact CNpcMaker +0x5fc behavior: CreateMakerKey is maker-wide.
    volatile std::int32_t m_nMakerID;
    std::map<int, int> m_MakerMap;
    std::u16string m_EventName;
};


// Current L2NPC.pdb keeps this complete type even though its only creation
// path was linker-eliminated. It inherits CIOObject's AddRef/Release in the
// current build (unlike the older NASC64_2015 specialization), so do not add
// class-specific lifetime overrides that are absent from the target type.
class CNpcMakerReleaseVerifier : public CIOObject {
public:
    explicit CNpcMakerReleaseVerifier(CNpcMaker* npcMaker);
    ~CNpcMakerReleaseVerifier() override = default;

    L2_DECLARE_MEMORY_ALLOCATION(CNpcMakerReleaseVerifier)

    void TimerExpired(int id) override;

private:
    // Current PDB: the sole derived field is at +0x18.
    CNpcMaker* m_pNpcMaker;
};


// Process-wide owner used by RequestNPCSpawn and g_WorldSpawnDefine. Its
// target override ignores the requested amount and always
// succeeds.
class CNpcMakerWorld : public CNpcMaker {
public:
    L2_DECLARE_MEMORY_ALLOCATION(CNpcMakerWorld)

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;
    bool IncreaseTotal(int amount) override;
};

inline int CNpcMaker::GetTotalNPC() {
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::LoadI32Relaxed(&m_nCreatedNPC);
}

inline int CNpcMaker::GetMaximumNPC() {
    L2_GUARD_FUNCTION();
    return m_nMaxNPC;
}

inline bool CNpcMakerWorld::IncreaseTotal(int) {
    L2_GUARD_FUNCTION();
    return true;
}

// Exact CNpcSpawnDefineWorld allocated by RequestNPCSpawn and used by the
// process-wide g_WorldSpawnDefine fallback.
namespace NpcTypeForDeco {

enum Type : int {
    NotDecoNpc = 0,
    DecoNpcInRealWorld = 1,
    DecoNpcInInstantAgit = 2,
};

} // namespace NpcTypeForDeco

class CNpcSpawnDefineWorld : public CNpcSpawnDefine {
public:
    CNpcSpawnDefineWorld();
    void SetDecoNpcInfo(NpcTypeForDeco::Type type, int value);
    bool IsWorldSpawnDefine() override { return true; }
    bool IsInTerritory(FVector, bool) override { return true; }
    bool IsInTerritoryWithLogVisitor(FVector, ZLogVisitor&, bool) override { return true; }
    int GetPledgeDBID() const override;
    void OnNpcCreated(CNPC* npc) override;
    void OnNpcDeleted(CNPC* npc) override;

private:
    NpcTypeForDeco::Type m_eNpcTypeForDeco;
    int m_nAgitCIDorPledgeDBID;
};


// Exact process-wide objects referenced by RequestNPCSpawn and by the
// fallback CreatePacket path.
extern CNpcMakerWorld g_WorldNPCMaker;
extern CNpcSpawnDefineWorld g_WorldSpawnDefine;

} // namespace l2npc

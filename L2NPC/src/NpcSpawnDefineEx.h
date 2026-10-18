#pragma once

#include "Shared/src/MemorySub.h"

#include "BaseSpawnDefine.h"
#include "Shared/src/ScriptAccessible.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>

namespace l2npc {
class DBNpcInfo;
class NpcMakerEx;
struct SpawnPos;
} // namespace l2npc


// Recovered from L2NPC/NpcSpawnDefineEx.h. The target has BaseSpawnDefine as
// its primary base and ScriptAccessibleEx as its secondary base at +0x20.
class NpcSpawnDefineEx : public BaseSpawnDefine, public ScriptAccessibleEx {
public:
    using WayPointsMapType = std::map<std::u16string, std::vector<FVector>>;

    NpcSpawnDefineEx();
    explicit NpcSpawnDefineEx(const char16_t* name);
    NpcSpawnDefineEx(const NpcSpawnDefineEx& other);
    ~NpcSpawnDefineEx() override;

    L2_DECLARE_OBJECT_TYPE(NpcSpawnDefineEx)

    NpcSpawnDefineEx* Copy() {
        // This method is inline in the PDB. Current callers expand it to a
        // nullable allocation followed by the copy constructor.
        return new NpcSpawnDefineEx(*this);
    }
    bool IsSpawnDefineEx() { return true; }
    NpcSpawnDefineEx* CastSpawnDefineEx() { return this; }

    void CollectFunc(ScriptTypeInfo* typeInfo) override;
    void CollectVar(ScriptTypeInfo* typeInfo) override;
    ScriptAccessibleEx* GetScriptAccessibleEx() const override;

    void SetNpcMakerEx(l2npc::NpcMakerEx* maker);
    const char16_t* GetName() const { return mName.data(); }
    void SetNickName(const char16_t* value);
    std::u16string GetNickName() const { return std::u16string(mNickName.data()); }
    void SetTotal(int value) { mTotal = value; }
    int GetTotal() { return mTotal; }
    void SetRespawnTime(int value);
    void SetRespawnRandTime(int value);
    void SetAI(const char16_t* value);
    void SetAIParameters(const AiParameterMap& parameters);
    void SetPrivates(const char16_t* value);
    bool AddWayPoints(const char16_t* name, std::vector<FVector> points);
    void SetAnywhere(bool value) { mIsAnywhere = value; }
    void SetDBLoaded(int value);
    int IsDBLoaded() const { return mIsDBLoaded; }
    void SetDBName(const char16_t* value);

    void AddSpawnPosition(l2npc::SpawnPos* position);
    l2npc::SpawnPos* GetNpcSpawnPosition();
    int IncCount(int amount);
    int DecCount(int amount);

    int Spawn(int count, int baseTime);
    int Spawn2(int count, int baseTime, int spread);
    int Spawn3(int count, int baseTime, int spread, int makerParam1, int makerParam2, int makerParam3);
    int SpawnEx(int count, int baseTime, int x, int y, int z, int direction, int hp, int mp, int dbValue);
    int SpawnEx2(int count, int baseTime, int spread, int x, int y, int z, int direction, int hp, int mp, int dbValue);
    int SpawnEx3(
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
    );
    int Despawn();
    int LoadDBNpcInfo(int argument);
    void OnDBNpcInfo(l2npc::DBNpcInfo& info);
    void RegToRespawnTimer(int absoluteUnixSeconds);
    void SendScriptEvent(int argument1, int argument2, int argument3);

    void CheckAndModifyByBossRespawn();
    void SetBossRespawn(bool value) { m_bBossRespawnApplied = value; }
    bool IsBossRespawn() { return m_bBossRespawnApplied; }
    void SetChasePC(int distance) { m_nChaseDistance = distance; }

    int CreateMakerKey() override;
    FVector GenerateSpawnPos(int& heading, bool withSky) override;
    int GetChaseDistance() override;
    CSharedCreatureData* GetNpcFromKey(int key) override;
    TerritoryInfo* GetTerritoryInfo() override;
    l2npc::NpcMakerEx* GetMyMaker() override;
    int GetTotalNPC() override;
    bool IsInBannedTerritory(FVector position, bool includeZ) override;
    bool IsInTerritory(FVector position, bool includeZ) override;
    bool IsInTerritoryWithLogVisitor(FVector position, ZLogVisitor& visitor, bool includeZ) override;
    void OnNpcCreated(CNPC* npc) override;
    void OnNpcDeleted(CNPC* npc) override;
    void OnNpcEnterWorld(CNPC* npc) override;
    bool ReserveNPC() override;
    bool SetAIParameter(CNPC* npc, const char16_t* aiName, AiParameterMap& parameters) override;
    void SpawnNpc(bool alreadyReserved) override;

    void CheckZPos(const FVector& position, bool isPrivate);

private:
    // PDB supplies the original names/order. The current EXE independently
    // confirms these offsets and its newer, smaller 0x20 tree/vector owners.
    std::array<char16_t, 25> mNickName;
    std::array<char16_t, 25> mName;
    int mTotal;
    volatile std::int32_t mNpcCount;
    int mRespawnTime;
    int mRespawnRandTime;
    std::array<char16_t, 25> mDBName;
    int mHasDBName;
    mutable CIOCriticalSection mLock;
    std::set<unsigned int> mNpcSet;
    bool mIsAnywhere;
    std::vector<l2npc::SpawnPos*> mSpawnPositions;
    l2npc::NpcMakerEx* mpMyMaker;
    std::u16string mAI;
    AiParameterMap mAIParams;
    std::u16string mPrivates;
    WayPointsMapType mWayPointsParamMap;
    int mIsDBLoaded;
    bool m_bBossRespawnApplied;
    int m_nChaseDistance;
    double m_minZ;
};

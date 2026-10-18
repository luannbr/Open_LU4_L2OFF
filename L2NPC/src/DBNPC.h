#pragma once

#include "Shared/src/MemorySub.h"

#include "Protocol.h"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class CNPC;
class NpcSpawnDefineEx;

namespace l2npc {

class CDBNPCSocket;
class CNpcSpawnDefine;
struct CLottoRound;

// Recovered CDBNPC boundary used by CNPC::Save and the process-wide
// DB-saving map. The target queues map operations and drains them from its
// one-second TimerExpired callback instead of sending from the NASC native.
class CDBNPC : public CIOObject {
public:
    CDBNPC();
    ~CDBNPC() override;

    L2_DECLARE_OBJECT_TYPE(CDBNPC)

    // Current EXE 0x0041ce6c/0x0041ccf0. The platform TCP connect is supplied
    // by Portable/DBNPCConnection. All target-visible ownership remains here.
    static bool Init();
    static bool InitDBNPC();

    void SendVersion();
    void SaveNpcInfo(CNPC* npc, const char16_t* databaseName);
    void SaveNpcDie(const char16_t* databaseName, const char16_t* killerMasterName, int reason);

    bool ValueChanged(int key, int value);

    void LoadNpcInfo(std::u16string name, CNpcSpawnDefine* definition);
    void LoadNpcInfoEx(const char16_t* name, NpcSpawnDefineEx* definition, int argument);

    void LoadLottoInfo();
    void Lotto_Drawing(
        int roundNumber, int totalCount, std::int64_t carriedAdena, int chosenNumber, int leftTime, std::int64_t presetReward
    );
    void Lotto_StartNextRound(int roundNumber, int leftTime, std::int64_t carriedAdena);
    void Lotto_SaveRound(CLottoRound round);
    void GetEventRankerInfo(int eventId, int databaseId, int creatureObjectId, int npcObjectId, int value);
    void DecreaseEventTopRankerReward(int eventId, int rewardId, int databaseId);
    void GiveEventData(int databaseId, int creatureIndex, int npcObjectId, int value);
    void RequestKamarokaCheckReward(int reward, int npcObjectId, int databaseId, int creatureObjectId);
    void RequestKamarokaSetReward(int reward, int databaseId, int count);
    void UpdateBossNpcValue(std::u16string databaseName, int value);

    void SaveDBSavingMap(int key, int value);
    void DeleteDBSavingMap(int key);
    void LoadDBSavingMap(int npcObjectId, int key);

    void TimerExpired(int timerId) override;

protected:
    enum JobType : int {
        JOB_SAVE = 0,
        JOB_DELETE = 1,
        JOB_LOAD = 2,
        JOB_MAX = 3,
    };

    using DBValue = std::pair<int, int>;
    using DBJob = std::pair<JobType, DBValue>;
    using DBProcessQueue = std::queue<DBJob, std::deque<DBJob, std::allocator<DBJob>>>;

    // Exact CDBNPC field order from its current constructor at 0x0041e854.
    CYieldLock mDBSaveLock;
    // The adjacent PDB spells this `unsigned long`. Use its Win64 width on
    // every host instead of the LP64 host definition of unsigned long.
    std::uint32_t m_nDBSaveTick;
    int m_nDBSaveCount;
    // Current EXE-only +0x30 lock. The older PDB predates this field, so its
    // original identifier is not proven. The semantic name is kept.
    CYieldLock m_Lock;
    DBProcessQueue m_DBProcessQueue;
    CDBNPCSocket* m_pSocket;
};


// Exact process globals shared by CDBNPC, GlobalObject and the CacheD reply
// callback. Their spellings and source type are recovered from DbNpc.obj in
// the adjacent PDB. Their placement and use are confirmed in
// the current executable. Map operations receive 0x00da9978, its head lives
// at +0x08, and the following lock starts at 0x00da9998, fixing the current
// xmap extent at 0x20 instead of the older PDB's 0x38.
extern CDBNPC dbnpc;
extern std::map<int, int> g_dbsaving_map;
extern CIOCriticalSection g_dbsaving_map_lock;

} // namespace l2npc

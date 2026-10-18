#pragma once

#include "ParseHtml.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Runtime/TargetWideChar.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class User;
class CSocket;
class CUserSocket;

enum MONRACESTATUS : std::int32_t {
    MR_PREPARE = 1,
    MR_BEFORE_SELL = 2,
    MR_SELL_TICKET = 3,
    MR_AFTER_SELL = 4,
    MR_RUN = 5,
    MR_CONFIRM = 6,
    MR_END = 7,
    MR_CLEARED = 8,
};

// Parser-owned record stored by CMonRaceDB. Current L2Server independently
// fixes the complete 0x180-byte extent and every field consumed by the race
// packet producer. The adjacent same-source type record supplies the names.
class CMonRaceData {
public:
    void Init();
    void SetGuts(std::vector<std::int64_t>* values);
    void Debug();

    std::int32_t runner_id;
    std::int32_t npc_classid;
    char16_t runner_name[128];
    std::int32_t sys_string;
    std::int32_t min_speed;
    std::int32_t max_speed;
    std::int32_t guts[20];
    std::int32_t initial_cond;
    std::int32_t initial_win;
    std::int32_t condition;
    std::int32_t m_runcount;
    std::int32_t m_wincount;
    double m_totalruntime;
};

// MonRaceData.cpp's process-global race owner. Its current 0x2aa0 storage
// extent includes the VS2017 tree owner, NCSoft xvector allocator words,
// the real CYieldLock and the complete cached client packet buffer.
class CMonRaceDB {
public:
    struct KeeperLoc {
        FVector m_loc;
        std::int32_t m_radius;
    };

    struct RaceAreaInfo {
        std::int32_t m_nRemoteFee;
        std::vector<FVector> m_points;
    };

    CMonRaceDB();
    ~CMonRaceDB();

    void Init();
    int LoadMonRaceData();
    void Insert(CMonRaceData* data);
    void SetTrackBegin(int trackId, int x, int y, int z);
    void SetTrackEnd(int trackId, int x, int y, int z);
    void AddRaceAreaInfo(int remoteFee, std::vector<FVector> points);
    int GetConfirmTime();
    int GetElapsedTime();
    int GetLastTime();
    int GetTaxRate();
    int GetValidTime();
    int GetRaceFeeRate(FVector position);
    int GetRaceFee(FVector position, int amount);
    bool MakeRacePacket();
    bool SendRacePacket(User* user);
    bool CheckRaceManagerLoc(FVector position);
    bool RequestEndBuyTicket();
    int GetTotalReturnMoney(int amount, int type);
    bool RequestEndRace2(
        int raceId,
        double runtime0,
        double runtime1,
        double runtime2,
        double runtime3,
        double runtime4,
        double runtime5,
        double runtime6,
        double runtime7,
        double firstRuntime,
        double secondRuntime,
        int firstRank,
        int secondRank,
        double firstWinRate,
        double pairWinRate
    );
    bool CheckRunnerCond();
    bool CalcRunnerSpeed();
    bool ModOrder(int first);
    bool SortRunner();
    bool RunRace();
    bool RequestEndRace();
    bool RequestUpdateMon();
    void BroadCastSound();
    void BroadCastMusic();
    bool RunTrack();
    void InitData();
    bool EndRace();
    bool BroadCastRace(int elapsedTime);
    bool ChangeRaceStatus(MONRACESTATUS status);
    bool CreateRace(int raceId);
    void SetInitialWin(int runnerId, int initialWin, int runCount, int winCount);
    bool CheckMonRaceKeeper(int serverId, int serverIndex);
    bool GetRunnerList(std::vector<CMonRaceData*>& runners, bool randomize);
    bool RequestCreateMonRace();
    bool RequestCreateMonTicket(
        int raceId, int ticketType, int firstLane, int secondLane, int ignored, int amount, User* user
    );
    bool TimerCalled();
    void SetTaxRate();

    FVector m_track_begin[8];
    FVector m_track_end[8];
    char16_t m_szMusic[128];
    char16_t m_szSound[128];
    std::int32_t m_nReturnRate;
    std::int32_t m_nResidenceID;
    std::map<std::int32_t, CMonRaceData*> m_racemon;
    std::vector<CMonRaceData*> m_runner;
    bool m_guts[8][20];
    std::int32_t m_speed[8][20];
    MONRACESTATUS m_status;
    std::int32_t m_rank[8];
    std::uint32_t m_dwRequestCreateTick;
    std::uint32_t m_dwCreateTick;
    std::uint32_t m_dwBeginTick;
    std::int32_t m_serverindex[8];
    std::int32_t m_serverid[8];
    double m_winrate1[8];
    double m_winrate2[8][8];
    double m_runtime[8];
    std::vector<KeeperLoc> m_keeperloc;
    std::uint32_t m_dwBroadCastTick;
    std::int32_t m_total_bet1;
    std::int32_t m_total_bet2;
    std::int32_t m_total_del1;
    std::int32_t m_total_del2;
    FVector m_racearea1;
    FVector m_racearea2;
    std::int32_t m_remotefee;
    CYieldLock mLock;
    std::vector<RaceAreaInfo> m_RaceAreas;
    std::int32_t m_nTaxRate;
    bool m_bStart;
    char m_racebuffer[8192];
    std::int32_t m_racebuffersize;
    std::int32_t m_nBroadCast;
    std::int32_t m_nCountDown;
    std::int32_t m_nLastCountDown;
    std::int32_t m_nRaceId;
    std::int32_t m_nServerId;
    std::int32_t m_nServerIndex;
};

// Current MonRaceData.cpp packet reply carrier. The constructor at
// 0x008422c8 stores the sole 32-bit field and fixes the complete extent.
class CMonRacePacket {
public:
    explicit CMonRacePacket(int type);

    bool DoReply(const unsigned char* packet);

private:
    bool BettingResult(int firstCount, int firstTotal, int secondCount, int secondTotal, const unsigned char* packet);
    bool IsAdenaAdddable(User* user, int adenaDatabaseId, std::int64_t amount);
    bool DelMonRaceTicket(
        int characterDatabaseId, int userServerId, int check, int itemDatabaseId, int adenaDatabaseId, std::int64_t adena
    );

    std::int32_t m_nType;
};

// Current RTTI and vtable at 0x01061bb8 prove a direct CParseHTML base and
// exactly one virtual slot (the complete deleting-destructor surface).
class CMonRaceHTML final : public CParseHTML {
public:
    explicit CMonRaceHTML(CUserSocket* socket);
    ~CMonRaceHTML() override;

    bool CheckAge();
    void MonRaceError(int messageId);
    void MonRaceKeeper();
    void MonRaceView1();
    void MonRaceView11();
    void MonRaceView20();
    void MonRaceBuy1(int selectedLane);
    void MonRaceBuy2(int lane, int amountIndex);
    void MonRaceBuy3(int lane, int amountIndex, int ignoredConfirmation);
    void MonRaceBuy4(int lane, int amountIndex, int confirmation);
    void MonRaceBuy11(int selectedLane);
    void MonRaceBuy12(int firstLane, int secondLane);
    void MonRaceBuy13(int firstLane, int secondLane, int amountIndex);
    void MonRaceBuy14(int firstLane, int secondLane, int amountIndex, int confirmation);
    void MonRaceBuy15(int firstLane, int secondLane, int amountIndex, int confirmation);
    void MonRaceSell1(int first, int userServerId, int rowCount, int page, const unsigned char* packet);
    void MonRaceSell2(int first, int userServerId, const unsigned char* packet);
    void MonRaceView31(int first, int userServerId, int rowCount, int page, const unsigned char* packet);


private:
    double GetWinRate(
        int raceId,
        int resultType,
        int selectedFirst,
        int selectedSecond,
        int winningFirst,
        int winningSecond,
        double fallback
    );

    std::uint32_t m_nCountryCode;
};

extern CMonRaceDB g_MonRaceDB;
extern std::int32_t MONRACE_MONEY[9];

bool MonRaceHTML(CSocket* socket, const SharedUtilsWideChar* command);

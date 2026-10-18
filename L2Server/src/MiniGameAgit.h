#pragma once

#include "Agit.h"
#include "Shared/src/Log.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"
#include "Shared/src/platform/Runtime.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CMiniGameAgit;
class CDBSocket;
class CCreature;
class CPledge;
class CParty;
class CUserSocket;
class User;

namespace CMiniGameAgitBuilderCmd {

bool RegisterPledge(CUserSocket*, User*, char16_t*);
bool JoinGame(CUserSocket*, User*, char16_t*);
bool SetTime(CUserSocket*, User*, char16_t*);
bool SetWinner(CUserSocket*, User*, char16_t*);
bool View(CUserSocket*, User*, char16_t*);
bool PledgeCheckLoosely(CUserSocket*, User*, char16_t*);

} // namespace CMiniGameAgitBuilderCmd

class CMiniGameAgitTimeSetting {
public:
    CMiniGameAgitTimeSetting() : m_SecPrepare(3600), m_SecProcess(3000), m_SecClose(600) {}

    void SetTime(int secPrepare, int secProcess, int secClose, bool checkTotalTime) {
        const std::uint32_t totalSeconds = static_cast<std::uint32_t>(secPrepare) + static_cast<std::uint32_t>(secProcess) +
            static_cast<std::uint32_t>(secClose);
        if (checkTotalTime && totalSeconds != 7200U) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] MiniGameAgit Setting Error... "
            //     L"MiniGameAgit total time != 2 Hours",
            //     L"c:\\work\\lineage2build\\server\\main\\l2server\\MiniGameAgit.h",
            //     0x26)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] MiniGameAgit Setting Error... "
            //     L"MiniGameAgit total time != 2 Hours",
            //     L"c:\\work\\lineage2build\\server\\main\\l2server\\MiniGameAgit.h",
            //     0x26)
            L2_LOG_ERROR(
                Log,
                "Mini-game clan hall timing does not total 7200 seconds: prepare %d, process %d, close %d; unsigned 32-bit "
                "total %u. These settings are still applied.",
                secPrepare, secProcess, secClose, static_cast<unsigned>(totalSeconds)
            );
        }

        m_SecPrepare = secPrepare;
        m_SecProcess = secProcess;
        m_SecClose = secClose;
    }

    int GetPrepareSec() const { return m_SecPrepare; }

    int GetProcessSec() const { return m_SecProcess; }

    int GetCloseSec() const { return m_SecClose; }

private:
    int m_SecPrepare;
    int m_SecProcess;
    int m_SecClose;
};


class CMiniGameAgitStatus {
public:
    enum state : std::int32_t {
        REGISTER = 0,
        PREPARE = 1,
        PROCESS = 2,
        CLOSE = 3,
        CLEAN_UP = 4,
        STATUS_MAX = 5,
    };

protected:
    CMiniGameAgitStatus(const CMiniGameAgitTimeSetting& timeSetting) :
        m_State(REGISTER), m_msecElapsed(0), m_msecLastUpdate(0), m_bPause(false), m_TimeSetting(timeSetting) {}

    void LoadFromDB(std::uint32_t msecElapsed) { m_msecElapsed = msecElapsed; }

    bool UpdateState();

    void TurnOn() {
        m_msecElapsed = 1;
        m_msecLastUpdate = runtime::GetTickCount();
    }

    void Pause() { m_bPause = true; }

    void Resume() {
        m_bPause = false;
        m_msecLastUpdate = runtime::GetTickCount();
    }

    void CloseForcely() {
        const std::uint32_t closeStartSeconds = static_cast<std::uint32_t>(m_TimeSetting.GetPrepareSec()) +
            static_cast<std::uint32_t>(m_TimeSetting.GetProcessSec());
        m_msecElapsed = closeStartSeconds * 1000U;
        m_msecLastUpdate = runtime::GetTickCount();
        m_State = CLOSE;
    }

    void Clear() {
        m_State = REGISTER;
        m_msecElapsed = 0;
        m_msecLastUpdate = 0;
        m_bPause = false;
    }

    state GetState() { return m_State; }

    std::uint32_t GetElapsedMillisec() { return m_msecElapsed; }

    bool IsPaused() { return m_bPause; }

    std::uint32_t GetGameStartRemainSec();
    std::uint32_t GetGameEndRemainSec();
    const wchar_t* GetStateString();

private:
    state CalcState();

    void UpdateElapsedTime() {
        if (m_bPause) {
            return;
        }

        if (m_msecLastUpdate == 0) {
            m_msecLastUpdate = runtime::GetTickCount();
            return;
        }

        const std::uint32_t currentTick = runtime::GetTickCount();
        m_msecElapsed += currentTick - m_msecLastUpdate;
        m_msecLastUpdate = currentTick;
    }

    friend class CMiniGameAgit;

    state m_State;
    std::uint32_t m_msecElapsed;
    std::uint32_t m_msecLastUpdate;
    bool m_bPause;
    const CMiniGameAgitTimeSetting& m_TimeSetting;
};


class CMiniGameAgitRegistPledgeEntry {
public:
    struct RegistInfo {
        RegistInfo() : nPoint(0), nTime(0) {}
        RegistInfo(int point, std::int32_t time) : nPoint(point), nTime(time) {}

        int nPoint;
        std::int32_t nTime;
    };

    bool Regist(unsigned int pledgeDbId, int point, bool fromDatabase);
    int Unregist(unsigned int pledgeDbId);
    bool IsRegistered(unsigned int pledgeDbId);
    void Clear();
    int CheckValidPledge(unsigned int pledgeDbId, bool checkRegistered);
    void GetTopN(int count, std::vector<unsigned int>& pledges, bool includeInvalid, bool logResult);
    int GetPoint(unsigned int pledgeDbId);
    void BuilderCmd_ShowInfo(CUserSocket* socket);
    void BuilderCmd_PledgeCheckLoosely(CUserSocket* socket, bool checkLoosely);
    void Broadcast(const char* format, std::va_list arguments);
    void BroadcastExcept(const std::vector<unsigned int>& except, const char* format, std::va_list arguments);

private:
    std::map<unsigned int, RegistInfo> m_Entry; // +0x00
    CYieldLock m_EntrySect; // +0x20
    bool m_bCheckLoosely; // +0x30
    [[maybe_unused]] std::byte m_alignment31To38[7]; // +0x31..+0x37
};


class CMiniGameAgitSelectedPledgeEntry {
public:
    CMiniGameAgitSelectedPledgeEntry();

    bool IsSelected(unsigned int pledgeDbId) const;
    void Clear();
    void Add(unsigned int pledgeDbId);
    int Size() const;
    void GetList(std::vector<unsigned int>& pledges) const;
    int GetIndex(unsigned int pledgeDbId) const;
    void BuilderCmd_ShowInfo(CUserSocket* socket);
    void Broadcast(const char* format, std::va_list arguments);

private:
    unsigned int m_nSelectedPledgeIdList[4]; // +0x00
    int m_nCount; // +0x10
    [[maybe_unused]] std::byte m_alignment14To18[4]; // +0x14..+0x17
};


class CMiniGameAgitPlayerEntry {
public:
    bool AddPlayer(unsigned int userDbId);
    bool DelPlayer(unsigned int userDbId);
    bool IsPlayer(unsigned int userDbId);
    void Clear();
    void DeleteGameItem(const std::vector<int>& itemClassIds);
    void Banish(
        unsigned int winnerPledgeDbId,
        const std::vector<FVector>& challengerRestartPoints,
        const std::vector<FVector>& otherRestartPoints
    );
    void BuilderCmd_ShowInfo(CUserSocket* socket);

private:
    std::set<unsigned int> m_Entry; // +0x00
    CYieldLock m_EntrySect; // +0x20
};


class CUnreturnedPointKeeper {
public:
    void Add(unsigned int pledgeDbId, unsigned int point);
    void Remove(unsigned int pledgeDbId);
    void Clear();
    int Size() const;
    bool HasUnreturnedPoint(unsigned int pledgeDbId) const;
    int GetPoint(unsigned int pledgeDbId);

private:
    std::map<unsigned int, unsigned int> m_PledgePointMap; // +0x00
    CYieldLock m_PledgePointMapLock; // +0x20
};


// Current L2Server MiniGameAgit.obj owns the command logic while the
// NpcSocket.obj call site emits this non-polymorphic carrier's inline
// construction/destruction.
class CMiniGameAgitNpcFuncHandler {
public:
    CMiniGameAgitNpcFuncHandler(std::uint8_t function, int agitId, int npcIndex, int userIndex, int point) :
        m_byFunc(function),
        m_alignment01To04{},
        m_nAgitId(agitId),
        m_nNpcIndex(npcIndex),
        m_nUserIndex(userIndex),
        m_nPoint(point),
        m_alignment14To18{},
        m_pAgit(nullptr) {}

    bool PreCheck();
    void Execute();

private:
    void RegisterPledge();
    void UnregisterPledge();
    void GetStatus();
    void JoinGame();
    void SetWinner();
    void GetUnreturnedPoint();

    std::uint8_t m_byFunc; // +0x00
    [[maybe_unused]] std::byte m_alignment01To04[3]; // +0x01..+0x03
    std::int32_t m_nAgitId; // +0x04
    std::int32_t m_nNpcIndex; // +0x08
    std::int32_t m_nUserIndex; // +0x0c
    std::int32_t m_nPoint; // +0x10
    [[maybe_unused]] std::byte m_alignment14To18[4]; // +0x14..+0x17
    CSPointer<CCreature> m_pUser; // +0x18
    CSPointer<CCreature> m_pNpc; // +0x28
    CMiniGameAgit* m_pAgit; // +0x38
};


class CMiniGameAgit final : public CWorldAgit {
public:
    CMiniGameAgit();
    ~CMiniGameAgit() override;

    L2_DECLARE_OBJECT_TYPE(CMiniGameAgit)

    bool IsMiniGameAgit() const override;
    bool IsBattleTypeAgit() const override;
    CMiniGameAgit* CastMiniGameAgit() const override;
    void TimerExpired(int id) override;
    void StartTimer() override;
    void StartSiege() override;
    void EndSiege() override;
    void PassSiege() override;
    void PauseSiege() override;
    void ResumeSiege() override;

    void SetTime(int prepareSeconds, int processSeconds, int closeSeconds, bool checkTotalTime);
    int AddEntryPledge(CPledge* pledge, int point);
    int RemoveEntryPledge(CPledge* pledge);
    void OnSetWinner(CPledge* pledge);
    int JoinGame(User* user);
    CMiniGameAgitStatus::state GetState();
    void LoadFromDB(int elapsedMilliseconds);
    bool AddEntryPledgeFromDB(unsigned int pledgeDbId, int point);
    void LoadTopNPledgeFromDB();
    void AddMember(unsigned int pledgeDbId, unsigned int userDbId);
    void ClearGame();
    bool IsPlayer(unsigned int userDbId);
    static bool IsPlayer(User* user);
    FVector GetChallengerRestartPoint(unsigned int pledgeDbId);
    int GetUnreturnedPoint(unsigned int pledgeDbId);
    void BuilderCmd_ShowInfo(CUserSocket* socket);
    void BuilderCmd_PledgeCheckLoosely(CUserSocket* socket, bool checkLoosely) {
        m_bCheckLoosely = checkLoosely;
        m_PledgeEntry.BuilderCmd_PledgeCheckLoosely(socket, checkLoosely);
    }
    static CMiniGameAgit* GetMiniGameAgit(int residenceId);

private:
    friend class CMiniGameAgitNpcFuncHandler;
    friend bool CMiniGameAgitBuilderCmd::SetTime(CUserSocket*, User*, char16_t*);

    void PrepareGame();
    void StartGame();
    void RestartGame();
    void PauseGame();
    void ResumeGame();
    void CloseGame();
    void EndGame();
    void PassGame();
    void MakeSelectedEntry(bool logResult);
    void HandleState();
    void BroadcastToAllPledgeMaster(const char* format, ...);
    void BroadcastToSelectedPledgeMaster(const char* format, ...);
    void BroadcastToUnselectedPledgeMaster(const char* format, ...);
    void SendTimeElapsedMessage();
    int GetJoinErrorCode(User* user);
    bool TeleportToGameArea(User* user);
    bool PartyTeleportToGameArea(CParty* party);
    void DeleteGameItem(User* user);

    CMiniGameAgitTimeSetting m_TimeSetting; // +0x498
    [[maybe_unused]] std::byte m_alignment4a4To4a8[0x04]; // +0x4a4
    CMiniGameAgitStatus m_StateHelper; // +0x4a8
    CMiniGameAgitRegistPledgeEntry m_PledgeEntry; // +0x4c0
    CMiniGameAgitSelectedPledgeEntry m_SelectedPledgeEntry; // +0x4f8
    CMiniGameAgitPlayerEntry m_PlayerEntry; // +0x510
    CUnreturnedPointKeeper m_UnreturnedPointKeeper; // +0x540
    unsigned int m_nWinnerPledgeDbId; // +0x570
    bool m_bCheckLoosely; // +0x574
    [[maybe_unused]] std::byte m_alignment575To578[0x03]; // +0x575
    int m_nLastAlarmSec; // +0x578
    [[maybe_unused]] std::byte m_alignment57cTo580[0x04]; // +0x57c
};


class CMiniGameAgitDBHandler {
public:
    static bool ReplyLoadMiniGameAgit(CDBSocket* socket, const unsigned char* packet);
    static bool ReplyAddMiniGameAgitPledge(CDBSocket* socket, const unsigned char* packet);
    static bool ReplyAddMiniGameAgitMember(CDBSocket* socket, const unsigned char* packet);
    static bool ReplyResetMiniGameAgit(CDBSocket* socket, const unsigned char* packet);
};

#pragma once

#include "OlympiadService.h"

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <utility>

class CInstantZone;
class User;

namespace protocol {
struct S_EX_OLYMPIAD_MATCH_INFO;
}

namespace network {
struct IPacket;
}

class WFieldStatus {
public:
    enum type : std::int32_t {
        NO_MATCH = 0,
        PREPARE_MATCH = 1,
        RESPONSE_WAITING = 2,
        WAIT_MATCH = 3,
        START_MATCH = 4,
        PROCESS_MATCH = 5,
        FINISH_MATCH = 6,
        CLEAR_MATCH = 7,
    };
};

struct WEntryMessage {
    std::uint8_t byMessage;
    std::uint8_t m_alignment0001To0004[3]{};
    int nParam1;
    int nParam2;
    bool bParam;
    std::uint8_t m_alignment000dTo0010[3]{};
};

class WOlympiadObserverEntry {
public:
    WOlympiadObserverEntry();
    ~WOlympiadObserverEntry();

    void Broadcast(const char* format, va_list arguments);
    void Broadcast(const char* format, ...);
    void Broadcast(S_EX_PROTOCOL protocol, const network::IPacket& packet);
    bool AddEntry(std::uint32_t sid);
    void RemoveEntry(std::uint32_t sid);
    void RemoveEntryAll();
    std::vector<std::uint32_t> GetEntries() const;

private:
    std::set<std::uint32_t> m_Observers;
};

struct WOlympiadFieldTeamHolder {
    WOlympiadFieldTeamHolder();

    WOlympiadTeam team;
    bool occupied;
    std::uint8_t m_alignment0079To0080[7]{};
};

class WOlympiadMatchBroadcaster {
public:
    WOlympiadMatchBroadcaster();

    void Broadcast(WOlympiadFieldTeamHolder* teams, int remainingTime);
    void ResetBroadCastingTimeTable();
    void SetBroadCastingTimeTable(int index);

private:
    int m_BroadCastingTimeTable[5];
};

class WOlympiadMatch : public CIOObject {
public:
    enum GameResult : std::int32_t {
        GS_NOT_GAME = -1,
        GS_PLAYER1_LOSE = 0,
        GS_PLAYER2_LOSE = 1,
        GS_DRAW = 2,
        GS_MATCHING = 3,
        GS_PENALTY = 4,
    };

    class MatchRule {
    public:
        MatchRule();
        explicit MatchRule(WOlympiadMatch* match);
        virtual ~MatchRule();

        virtual void OnStatusChanged(WFieldStatus::type status);
        virtual void OnTickWait(int elapsedTime);
        virtual void OnTickProcess(int elapsedTime) = 0;
        virtual void OnObserverAdded(std::uint32_t sid);
        virtual void OnPlayerDied(User* user);
        virtual bool CanAttack() const;
        virtual void BroadcastMatchResult() const;
        virtual int GetRewardItemCount(OlympiadTeamEnum team) const;

    protected:
        void BroadcastCountDown(int elapsedTime, int messageId) const;
        void BroadcastMessage(int messageId) const;
        void BroadcastMessageC(int messageId, char value) const;
        void BroadcastMessageToObservers(int messageId) const;
        void BroadcastMessageSToObservers(int messageId, const wchar_t* value) const;
        void GetTeamByGameResult(OlympiadTeamEnum& winner, OlympiadTeamEnum& loser) const;

        WOlympiadMatch* m_pMatch;
    };

    class MatchRuleNormal final : public MatchRule {
    public:
        MatchRuleNormal();
        explicit MatchRuleNormal(WOlympiadMatch* match);
        ~MatchRuleNormal() override;

        void OnStatusChanged(WFieldStatus::type status) override;
        void OnTickProcess(int elapsedTime) override;
        void OnPlayerDied(User* user) override;
        bool CanAttack() const override;
        void BroadcastMatchResult() const override;

    private:
        void BroadcastMatchEndCountDown(int elapsedTime) const;
    };

    class MatchRule3Rounds final : public MatchRule {
    public:
        enum State : std::int32_t {
            SM_NONE = 0,
            SM_ROUND = 1,
            SM_INTER_ROUND = 2,
        };

        enum RoundResult : std::int32_t {
            RR_NONE = 0,
            RR_DRAW = 1,
            RR_PENALTY = 2,
            RR_TEAM1_WIN = 3,
            RR_TEAM2_WIN = 4,
        };

        enum InterRoundProcessStep : std::int32_t {
            IRPS_ACTIVATE_DEFAULT_SKILL = 0,
            IRPS_ACTIVATE_ROUND_RESULT_SKILL = 1,
            IRPS_TELEPORT_TO_START_POS = 2,
            IRPS_MAX = 3,
        };

        MatchRule3Rounds();
        explicit MatchRule3Rounds(WOlympiadMatch* match);
        ~MatchRule3Rounds() override;

        void OnStatusChanged(WFieldStatus::type status) override;
        void OnTickWait(int elapsedTime) override;
        void OnTickProcess(int elapsedTime) override;
        void OnObserverAdded(std::uint32_t sid) override;
        void OnPlayerDied(User* user) override;
        bool CanAttack() const override;
        void BroadcastMatchResult() const override;
        int GetRewardItemCount(OlympiadTeamEnum team) const override;

    private:
        void BroadcastRound() const;
        void BroadcastRoundResult(RoundResult result) const;
        void BroadcastInfo() const;
        void SendInfoTo(std::uint32_t sid) const;
        void ChangeState(State state);
        RoundResult CheckRoundResult() const;
        RoundResult CheckRoundEnd() const;
        int GetElapsedTime() const;
        int GetRemainTime() const;
        void GetInfoPacket(protocol::S_EX_OLYMPIAD_MATCH_INFO& packet) const;
        void RoundEnd(RoundResult result, bool force);
        void MatchEnd();
        bool AreAllResultPenalty() const;
        void AddWinCount(RoundResult result);
        void AddRewardCount(RoundResult result);
        void RewardCountByGameResult();
        void ShowRoundWinnerEffect(RoundResult result) const;
        void ActivateSkillTo(User* user, int skillId) const;
        void ActivateSkillTo(OlympiadTeamEnum team, int skillId) const;
        void ActivateDefaultSkillsTo(OlympiadTeamEnum team) const;
        void RestorePlayersHPMPCP(int percent, RoundResult result) const;
        void RestorePlayerHPMPCP(OlympiadTeamEnum team, float percent) const;
        void DispelDebuffs() const;
        void ResetSkillReuseTime() const;
        CSPointer<User> GetUser(OlympiadTeamEnum team) const;
        const wchar_t* GetName(OlympiadTeamEnum team) const;
        bool IsValidCondition(OlympiadTeamEnum team) const;
        RoundResult ConvertFromString(const std::u16string& value) const;
        void TeleportUsersToStartPos() const;
        bool IsMatchEnd() const;

        int m_nCurrentRound; // +0x10
        State m_eState; // +0x14
        std::int32_t m_tStateChanged; // +0x18
        RoundResult m_aRoundResults[4]; // +0x1c
        int m_anWinCounts[2]; // +0x2c
        int m_anRewardCounts[2]; // +0x34
        bool m_abInterRoundProcessed[3]; // +0x3c
        [[maybe_unused]] std::uint8_t m_alignment003fTo0040{};
        int m_anLastDamages[2]; // +0x40
        std::atomic<int> m_CurrentResult; // +0x48
        [[maybe_unused]] std::uint8_t m_alignment004cTo0050[4]{};
    };

    WOlympiadMatch();
    WOlympiadMatch(const WOlympiadMatch& other);
    ~WOlympiadMatch() override;

    void Init(int fieldId);
    void TimerExpired(int id) override;
    bool PrepareMatch(GameRuleType rule, WOlympiadTeam* team1, WOlympiadTeam* team2, bool fastTesting);
    bool SetConnectedUser(CSPointer<User> user);
    bool CanRegister() const;
    WFieldStatus::type GetStatus() const;
    void SetFieldId(int fieldId);
    int GetFieldId() const;
    bool AddObserver(std::uint32_t sid);
    void RemoveObserver(std::uint32_t sid);
    void EscapePlayer(std::uint32_t sid);
    void PlayerDied(User* user);
    void UpdateUserStatus(std::uint32_t sid, std::uint32_t targetSid);
    void UpdateSpelledInfo(std::uint32_t sid);
    void ChangeObserverField(std::uint32_t sid, int fieldId);
    bool CanAttack() const;
    void RequestPlayersInfo(std::uint32_t sid);
    void ChangeStatus(WFieldStatus::type status, bool force = false);
    void SetInZone(CInstantZone* inZone);
    CInstantZone* GetInZone() const;
    void DoNextStep(std::u16string parameter);
    bool HasGivenPanelty() const;
    bool AsyncAddObserver(std::uint32_t sid);
    void AsyncEscapeObserver(std::uint32_t sid);
    void AsyncUpdateUserStatus(std::uint32_t sid, std::uint32_t targetSid);
    void AsyncUpdateSpelledInfo(std::uint32_t sid, std::uint32_t targetSid);
    void AsyncChangeObserverField(std::uint32_t sid, int fieldId);
    bool AsyncUpdateAllPlayers(std::uint32_t sid);
    void Broadcast(const char* format, ...);
    void Broadcast(S_EX_PROTOCOL protocol, const network::IPacket& packet);
    GameResult GetGameState() const;
    void ProcessMessage();
    void FreezeAll();
    void SetConfig(bool fastTesting);
    void OnAppearingPacket();

    void StatusPrepareMatch(int elapsedTime);
    void StatusResponseWaiting(int elapsedTime);
    void StatusWaitMatch(int elapsedTime);
    void StatusStartMatch();
    void StatusProcessMatch(int elapsedTime);
    void StatusFinishMatch();
    void StatusClearMatch(int elapsedTime);

private:
    friend class MatchRule;
    friend class MatchRuleNormal;
    friend class MatchRule3Rounds;
    friend class WOlympiadMatchBroadcaster;
    friend class WOlympiadService;

    void CancelMatch(bool team1Valid, bool team2Valid);
    void RegisterMessage(std::uint8_t message, int param1, int param2, bool param);
    void _RegisterMessage(std::uint8_t message, int param1, int param2, bool param);
    void SetWaitMatchTimer();
    void SendMatchResult();
    void EscapeAllObserver();
    std::pair<bool, GameResult> ValidateAllTeamReady() const;
    bool ValidateAllTeamReadyForWaitState() const;
    bool CheckAndChangeStatusToNoneAtResponseWait();
    bool CheckAndChangeStatusToFinishAtWait();
    bool CheckAndChangeStatusToFinishAfterGameStart();
    void SetWinner();
    void IncreaseMatchCount();
    void DoFinishJob(OlympiadTeamEnum winner, OlympiadTeamEnum loser, int elapsedTime);
    void DoFinishJobDraw(int elapsedTime);
    void SendToPlayers(S_EX_PROTOCOL protocol, const network::IPacket& packet);
    void SendToPlayers(const char* format, ...);
    void SendVToPlayers(const char* format, va_list arguments);
    int GetElapsedTime() const;
    bool Is1SecElapsed();
    void GivePenalty(GameValidCondition condition);
    void ResetPenalty();
    void CalcPenalty();
    void SetDoor(bool open);
    void RestorePlayers(bool force);
    void DeleteNPCFromCreatureMap();
    void SetBeginTime(bool set);
    bool IsBeginTimeSet() const;
    int GetInzoneTypeID() const;

    CYieldLock m_FieldStatusLock; // +0x018
    WFieldStatus::type m_nStatus; // +0x028
    int m_nMessageRegistered; // +0x02c
    std::int32_t m_tStatusChanged; // +0x030
    std::int32_t m_tPrev; // +0x034
    std::int32_t m_tNow; // +0x038
    GameResult m_eGameResult; // +0x03c
    bool m_bBeginTimeSet; // +0x040
    std::uint8_t m_alignment0041To0044[3]{};
    int m_nFieldId; // +0x044
    WOlympiadObserverEntry m_ObserverEntry; // +0x048
    CYieldLock m_MessageQueueLock; // +0x068
    std::queue<WEntryMessage> m_MessageQueue; // +0x078
    bool m_bCheck1SecElapsed; // +0x0a8
    std::uint8_t m_alignment00a9To00ac[3]{};
    GameRuleType m_GameRule; // +0x0ac
    int m_LivingPlayerCount[2]; // +0x0b0
    GameValidCondition penaltyChecker[2]; // +0x0b8
    WOlympiadFieldTeamHolder m_TeamHolder[2]; // +0x0c0
    std::int32_t beginTime; // +0x1c0
    bool m_bFastTesting; // +0x1c4
    std::uint8_t m_alignment01c5To01c8[3]{};
    CInstantZone* m_pInZone; // +0x1c8
    WOlympiadMatchBroadcaster m_Broadcaster; // +0x1d0
    std::uint8_t m_alignment01e4To01e8[4]{};
    MatchRule* m_pMatchRule; // +0x1e8
    bool m_bDoNextStep; // +0x1f0
    std::uint8_t m_alignment01f1To01f8[7]{};
    std::u16string m_sDoNextStepParam; // +0x1f8
    bool m_bHasGivenPanelty; // +0x220
    std::uint8_t m_alignment0221To0228[7]{};
};

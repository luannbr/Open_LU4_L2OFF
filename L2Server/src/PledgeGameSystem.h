#pragma once

#include "OlympiadSystem.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

class User;
class CUserSocket;
class CCreature;
class CInstantZone;
class CSkillInfo;
class CSummon;
class CTerritory;
class CPledgeGameEntry;
class CPledgeGameField;
class CPledgeGameHtmlCommandHandler;
class CPledgeGameLoadingMessage;
class CParserForSetting;
class CSocket;
struct sPledgeGameFieldInfo;

namespace BuilderCmd {
bool PledgeGame(CUserSocket*, User*, char16_t*);
}

// The current image passes this source-level carrier by reference to Draw/Win,
// but neither body reads or writes it. The only observable contract is its
// eight-byte stack extent.
struct PledgeGameInfo {
    std::array<std::byte, 0x08> opaque{};
};

namespace PledgeGameCondition {

enum type : std::int32_t {
    NONE = 0,
    SEASON_STARTED = 1,
    GAME_OPEN = 2,
    GAME_PREPARE = 3,
    GAME_READY = 4,
    GAME_STARTED = 5,
    GAME_ENDED = 6,
    GAME_CLEAR = 7,
    SEASON_ENDED = 8,
    CONDITION_MAX = 9,
};

} // namespace PledgeGameCondition

namespace PledgeGame {

enum GAMERESULT_TYPE : std::int32_t {
    GRT_NONE = 0,
    GRT_BASE = 0,
    GRT_LOSE = 0,
    GRT_WIN = 1,
    GRT_DRAW = 2,
    GRT_MAX = 3,
};

} // namespace PledgeGame

namespace PledgeGameDefine {

extern const std::int32_t MAX_USER_PER_GAME;

} // namespace PledgeGameDefine

class CPledgeGameUserStateMachine {
public:
    enum UserState : std::int32_t {
        PLEDGEGAME_NOT_USER = 0,
        PLEDGEGAME_PLAYER = 1,
        PLEDGEGAME_OBSERVER = 2,
        PLEDGEGAME_ENTRY_REGISTER = 3,
        PLEDGEGAME_PLAY_WAITING = 4,
        PLEDGEGAME_OB_WAITING = 5,
        PLEDGEGAME_MAX = 6,
    };

    CPledgeGameUserStateMachine() : m_Status(PLEDGEGAME_NOT_USER) {}

    bool CanChangeState(UserState state);
    bool ChangeState(UserState state);

    UserState GetStatus() const { return m_Status; }

private:
    UserState m_Status;
};


class CPledgeGameUser {
public:
    bool IsPlayer();
    bool CanAttack();
    bool IsWaiting();
    bool IsObserver();
    bool IsRegistered();
    bool CanRegister();
    bool IsPlaying();
    bool IsEnemyTo(CPledgeGameUser* target);
    void SetLifeTime(std::int32_t lifeTime);
    std::int32_t GetLifeTime() const { return m_nLifeTime; }
    std::int32_t GetKillCount() const { return m_nKillCount; }
    void InitShuffleNo(std::int32_t playNoCount);
    std::int32_t GetShuffledPlayNo(std::int32_t playNo) const;
    void SetPlayNo(std::int32_t playNo);
    bool RegisterPledgeGameEntry();
    bool WaitingPledgeGamePlaying();
    bool WaitingPledgeGameObserving();
    bool SetPlayer(std::int32_t fieldId, CInstantZone* instantZone, std::int32_t startPositionIndex);
    bool SetObserver(std::int32_t fieldId, CInstantZone* instantZone);
    bool SetReturnPos(FVector& position, bool force);
    CPledgeGameField* GetField();
    void ChangeObserverField(std::int32_t fieldId);
    void ObserverHide(bool hide);
    void CompleteToDespawnPet(bool observer);
    void EscapeFromPledgeGame();
    void OnLeaveWorld();
    const char16_t* GetAliasName(User* viewer);
    void ActivateSkillForEveryUser();
    void ActivateSkillForSurvivor();
    bool PledgeGameStatusReset();
    void SetAlive(bool alive);
    bool IsAlive() const { return m_IsAlive; }
    std::int32_t GetPlayNo() const { return m_PlayNo; }

private:
    friend class CPledgeGameField;
    friend class CPledgeGameEntry;
    friend class CPledgeGameFuncHandler;
    friend class CPledgeGameHtmlCommandHandler;
    friend class CPledgeGameSystem;
    friend class User;
    friend class CPledgeGamePlayers;
    friend class CPledgeGameObserverEntry;
    friend bool BuilderCmd::PledgeGame(CUserSocket*, User*, char16_t*);

    void ReadyForMatch(CInstantZone* instantZone, std::int32_t startPositionIndex);
    void LeaveParty();
    void SetEnterInZone(CInstantZone* instantZone, std::int32_t startPositionIndex);
    void UnequipForbiddenWeapon();
    void ActivateSkill(std::int32_t skillId, std::int32_t skillLevel, bool summonNpc);
    void ActivateSkillForConceal();

    std::int32_t m_nInField; // +0x000..+0x003
    CPledgeGameUserStateMachine m_StateMachine; // +0x004..+0x007
    User* m_pUser; // +0x008..+0x00f
    FVector m_ReturnPos; // +0x010..+0x027
    FVector m_MovePos; // +0x028..+0x03f
    std::int32_t m_FieldId; // +0x040..+0x043
    std::int32_t m_PlayNo; // +0x044..+0x047
    std::int32_t m_Damage; // +0x048..+0x04b
    bool m_IsAlive; // +0x04c
    std::array<std::byte, 0x03> m_alignment4dTo50; // +0x04d..+0x04f
    std::int32_t m_nStartPosIndex; // +0x050..+0x053
    std::int32_t m_nKillCount; // +0x054..+0x057
    std::int32_t m_nLifeTime; // +0x058..+0x05b
    bool m_bGotInvitingMessage; // +0x05c
    std::array<std::byte, 0x03> m_alignment5dTo60; // +0x05d..+0x05f
    PledgeGame::GAMERESULT_TYPE m_nGameResult; // +0x060..+0x063
    std::int32_t m_nRecordSeason; // +0x064..+0x067
    std::int32_t m_nPrizeCount; // +0x068..+0x06b
    std::int32_t m_nSurrenderCount; // +0x06c..+0x06f
    std::int32_t m_nShuffleNo; // +0x070..+0x073
    std::int32_t m_nPlayNoCount; // +0x074..+0x077
    CInstantZone* m_pInstantZone; // +0x078..+0x07f
};


struct UserMatchingPoint {
    unsigned int nUserSID;
    unsigned int nUserDBID;
    std::int64_t nMatchingPoint;
};

struct PledgeGamePlayerInfo {
    PledgeGamePlayerInfo() :
        playNo(0),
        playerSID(0),
        playerDBID(0),
        playerIndex(-1),
        classId(-1),
        killCount(-1),
        lifeTime(0),
        alive(false),
        playerName{},
        nMatchingPointBase(0),
        nMatchingPointDiff(0),
        nMatchingPointTotal(0),
        fWinningRate(0.0F),
        nWinningPoint(0) {}

    int playNo;
    unsigned int playerSID;
    unsigned int playerDBID;
    int playerIndex;
    int classId;
    int killCount;
    int lifeTime;
    bool alive;
    char16_t playerName[25];
    std::int64_t nMatchingPointBase;
    std::int64_t nMatchingPointDiff;
    std::int64_t nMatchingPointTotal;
    float fWinningRate;
    std::int64_t nWinningPoint;
};

static_assert(std::is_trivially_copyable_v<CPledgeGameUserStateMachine>);
static_assert(std::is_trivially_destructible_v<CPledgeGameUserStateMachine>);
static_assert(std::is_aggregate_v<UserMatchingPoint>);
static_assert(std::is_trivially_copyable_v<UserMatchingPoint>);
static_assert(std::is_trivially_destructible_v<UserMatchingPoint>);

static_assert(std::is_trivially_copyable_v<PledgeGamePlayerInfo>);
static_assert(std::is_trivially_destructible_v<PledgeGamePlayerInfo>);

#ifndef L2_GAME_VALID_CONDITION_DEFINED
#define L2_GAME_VALID_CONDITION_DEFINED 1
enum GameValidCondition : std::int32_t {
    GVC_NONE = 0,
    GVC_MIN = 1,
    GVC_INVENTORY = 1,
    GVC_SUBJOB = 2,
    GVC_VALID_TEAM = 3,
    GVC_NOT_CURSED = 4,
    GVC_ALIVE = 5,
    GVC_ACTIVE = 6,
    GVC_FISHING = 7,
    GVC_NOT_TRAINING = 8,
    GVC_MAX = 9,
};
#endif

struct EntryMessage {
    std::uint8_t byMessage{};
    std::array<std::byte, 0x03> m_alignment01To04{};
    std::int32_t nParam1{};
    std::int32_t nParam2{};
    bool bParam{};
    std::array<std::byte, 0x03> m_alignment0dTo10{};
};

struct PledgeGameStartPoint {
    static constexpr std::size_t MAX_START_POINT = 3;
    FVector point[MAX_START_POINT]{};
};

class CPledgeGameObserverEntry {
public:
    bool AddEntry(std::int32_t userSid);
    bool RemoveEntry(std::int32_t userSid);
    void RemoveEntryAll(std::int32_t fieldId);
    void Broadcast(const char* format, va_list arguments);
    void Broadcast(const char* format, ...);

    std::set<std::int32_t> m_ObserverSet;
};

class CPledgeGameEntry : public CIOObject {
public:
    CPledgeGameEntry();
    ~CPledgeGameEntry() override;
    void TimerExpired(int id) override;
    bool AddEntry(User* user);
    bool CheckUser(User* user, bool notify, bool bypassEntryChecks);
    std::int32_t GetEntryCount();
    bool IsRegisteredUser(User* user, bool lock);
    bool RemoveEntry(User* user, bool notify);
    void ClearAllEntry();
    void ArrangeUserToField(CPledgeGameField* fields);

private:
    friend class CPledgeGameSystem;

    CYieldLock m_Lock; // +0x18..+0x27
    std::vector<User*> m_EntryList; // +0x28..+0x47
};

class CPledgeGamePlayers : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CPledgeGamePlayers)

    CPledgeGamePlayers();
    ~CPledgeGamePlayers() override;

    static std::int32_t __GetClassID() noexcept;
    static const wchar_t* __GetClassName() noexcept;
    L2_DECLARE_OBJECT_ALLOCATORS()

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void RestorePlayers();
    void ObserverEnd();
    void InitShuffleNo();
    void SendMatchMemberList(CPledgeGameObserverEntry& observers, std::int32_t targetSid);
    void SendV(const char* format, va_list arguments);
    void SendInformationPage();
    std::int32_t GetAliveUserCount();
    void Draw(PledgeGameInfo& gameInfo);
    void Win(User* winner, PledgeGameInfo& gameInfo);
    User* GetBestKiller();
    std::int32_t GetPlayersCount() const;
    void SendMatchResult(User* winner);
    void SendWinnerName(User* winner);
    void UnregisterAll();
    void Freeze();
    void ResetFreeze(bool activateSurvivorSkill);
    User* GetWinner(bool includeDrawFallback);
    bool UpdatePlayerInfo(User* user);
    bool UpdateInvalidPlayerInfo(std::int32_t userSid);
    bool RegisterUser(User* user);
    bool IsRegisteredUser(User* user);
    bool IsRegisteredUser(std::int32_t userSid);
    bool UnRegisterUser(User* user);
    bool NoOneAlive();
    bool IsAllUserOutExceptOne();
    void CancelHide();
    bool WaitingGame();
    void StartGame();
    void SetPlayer(int fieldId, CInstantZone* instantZone);
    void SendSummonInfo(CPledgeGamePlayers& recipients);
    void HideSummonInfo(CPledgeGamePlayers& recipients);
    void UpdateStatus(CPledgeGameField* field, int status);
    void FinishGame(bool sendResult);
    void EscapeFromPledgeGame();

private:
    friend class CPledgeGameEntry;
    friend class CPledgeGameField;
    friend class CPledgeGameSystem;

    std::int32_t _GetAliveUserCount();
    void _SetPlayersCount();
    User* _GetLiveLongUser();
    User* _GetBestKillUser();
    void ResetFreeze(User* user, bool activateSurvivorSkill);
    void RecvSummonInfo(CSummon* summon);
    void RecvHideSummonInfo(CSummon* summon);
    void SendSummonHPMPInfo(User* user, CSummon* summon);
    void HideSummonHPMPInfo(User* user, CSummon* summon);

    CYieldLock m_Lock; // +0x18..+0x27
    std::vector<PledgeGamePlayerInfo> m_MemberList; // +0x28..+0x47
    std::int32_t m_nFieldId; // +0x48
    std::int32_t m_nMemberCount; // +0x4c
};

class CPledgeGameHtml {
public:
    CPledgeGameHtml();
    ~CPledgeGameHtml();
    void Init();
    void OnGameChanged(std::int32_t fieldId);
    void SendApplyPage(User* user);
    void SendCancelPage(User* user);
    void SendGameList(User* user);

private:
    friend class CPledgeGameLoadingMessage;
    friend class CPledgeGameSystem;
    std::u16string m_Html[3]; // +0x00..+0x5f
    std::map<std::int32_t, const sPledgeGameFieldInfo*> m_mapMatchList; // +0x60..+0x7f
    RWLock lockMapMatchList; // +0x80..+0xb7
};

class CPledgeGameField : public CIOObject {
public:
    CPledgeGameField();
    ~CPledgeGameField() override;
    void TimerExpired(int id) override;
    bool AddEntry(User* user);

    PledgeGameFieldStatus::type GetStatus() const noexcept { return m_nStatus; }

    void RestorePlayers();
    void ProcessMessage();
    void PlayerDied(User* user, CCreature* killer);
    bool IncreaseKillCountByKillingNPC(std::int32_t userSid);
    void RegisterMessage(std::uint8_t message, std::int32_t parameter1, std::int32_t parameter2, bool flag);
    bool AsyncAddObserver(std::int32_t userSid);
    bool AddObserver(std::int32_t userSid);
    void AsyncEscapePlayer(std::int32_t userSid);
    void AsyncEscapeObserver(std::int32_t userSid);
    void AsyncChangeObserverField(std::int32_t userSid, std::int32_t fieldId);
    void PlayerLeaving(std::int32_t userSid);
    void StatusPrepareMatch(std::int32_t elapsed);
    void StatusWaitMatch(std::int32_t elapsed);
    void StatusStartMatch();
    void StatusProcessMatch(std::int32_t elapsed);
    void StatusFinishMatch();
    void StatusClearMatch(std::int32_t elapsed);
    void Init(std::int32_t fieldId);
    void DeleteNPCFromCreatureMap();
    void SendMatchResult();
    void DoFinishJob(User* winner, std::int32_t lifeTime);
    void DoFinishJobDraw(std::int32_t lifeTime);
    void EscapeAllObserver();
    bool SummonUser();
    void ChangeStatus(PledgeGameFieldStatus::type status, bool notify, bool preservePlayers);
    bool CheckGameExit();
    void SetWaitMatchTimer();
    void SetOnePlayerGame();
    void SetNpcRelatedGame(bool enabled);
    bool SpawnNpc(int npcClassId, int count, User* owner);
    void FreezeAll();
    void ResetFreezeAll(bool activateSurvivorSkill);
    void SetWinnerLifeTime();
    User* GetWinner();
    bool AsyncUpdateAllPlayers(int status);
    void AsyncUpdateUserStatus(std::int32_t userSid, int targetSid);
    void Broadcast(const char* format, ...);
    void SendToPlayers(const char* format, ...);

protected:
    void SendVToPlayers(const char* format, va_list arguments);

private:
    friend class CPledgeGameEntry;
    friend class CPledgeGameHtml;
    friend class CPledgeGameHtmlCommandHandler;
    friend class CPledgeGameSystem;
    friend class CPledgeGamePlayers;
    friend class CPledgeGameUser;

public:
    // Current CodeView and SkillFxFunc.cpp expose this registration-state
    // byte as a public gameplay contract.
    bool m_bIsUnRegist{}; // +0x018

private:
    [[maybe_unused]] std::array<std::byte, 0x03> m_alignment019To01c{};
    CYieldLock m_StatusLock; // +0x01c..+0x02b
    PledgeGameFieldStatus::type m_nStatus{}; // +0x02c
    std::int32_t m_nMessageRegistered{}; // +0x030
    std::int32_t m_tStatusChanged{}; // +0x034
    std::int32_t m_tPrev{}; // +0x038
    std::int32_t m_tNow{}; // +0x03c
    [[maybe_unused]] std::int32_t m_nLoserState{}; // +0x040
    bool m_bBeginTimeSet{}; // +0x044
    [[maybe_unused]] std::array<std::byte, 0x03> m_alignment045To048{};
    std::int32_t m_nFieldId{-1}; // +0x048
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment04cTo050{};
    CPledgeGameObserverEntry m_ObserverEntry; // +0x050..+0x06f
    CYieldLock m_MessageQueueLock; // +0x070..+0x07f
    std::deque<EntryMessage> m_MessageQueue; // +0x080..+0x0af
    bool m_bCheck1SecElapsed{}; // +0x0b0
    [[maybe_unused]] std::array<std::byte, 0x03> m_alignment0b1To0b4{};
    [[maybe_unused]] std::int32_t m_nLivingCount{}; // +0x0b4
    [[maybe_unused]] GameValidCondition m_GameValidCondition[2]{}; // +0x0b8..+0x0bf
    CTerritory* m_pTerritory{}; // +0x0c0
    PledgeGameStartPoint m_PlayerStartPos; // +0x0c8..+0x10f
    std::int32_t m_nBeginTime{}; // +0x110
    bool m_bFastTesting{}; // +0x114
    bool m_bOnePlayerMode{}; // +0x115
    bool m_bNpcRelated{}; // +0x116
    [[maybe_unused]] std::byte m_alignment117{};
    CInstantZone* m_pInZone{}; // +0x118
    CPledgeGamePlayers m_Players; // +0x120..+0x16f
};

struct PledgeGameMessage {
    std::int32_t nSeason{};
    std::int32_t nStep{};
    std::int32_t tSeasonStart{};
};

class CPledgeGameFuncHandler {
public:
    CPledgeGameFuncHandler(std::uint8_t function, std::int32_t npcIndex, std::int32_t userIndex, std::int32_t fieldId);
    ~CPledgeGameFuncHandler();

    bool PreCheck();
    void Execute();

private:
    bool AddPledgeGame();
    void RemovePledgeGame();
    void EscapeFromPledgeGame();
    void ObservePledgeGame();
    void AddWinnerRecord();

    std::uint8_t m_Function{}; // +0x00
    [[maybe_unused]] std::array<std::byte, 0x03> m_Alignment01To04{};
    std::int32_t m_NpcIndex{}; // +0x04
    std::int32_t m_UserIndex{}; // +0x08
    std::int32_t m_FieldId{}; // +0x0c
    CSPointer<CCreature> m_User; // +0x10
    CSPointer<CCreature> m_Npc; // +0x20
};


class CPledgeGameSystem : public CIOObject {
public:
    CPledgeGameSystem();
    ~CPledgeGameSystem() override;
    void TimerExpired(int id) override;

    bool CheckUser(User* user);
    void SetFastTesting(bool fastTesting);
    std::int32_t GetSurrenderCount(std::int32_t userSid);
    void GetPledgeGameRecord(int userSid);
    char GetUserFlag(User* user);
    bool IsWinnerOfLastSeason(User* user);
    bool DidWinnerOfLastSeasonGetItem();
    bool CheckTime();
    bool CanChangeStep(PledgeGameCondition::type condition);
    bool SetStep(PledgeGameCondition::type condition);
    bool EndSeason(bool setNextSeason);
    void CheckAutoStart();
    bool OpenGame();
    bool PrepareGame();
    bool OnPrepareGameComplete(std::map<int, std::int64_t>& points);
    bool ChangeStatusPledgeGameField(PledgeGameFieldStatus::type status);
    bool CheckStatusPledgeGameFieldAll(PledgeGameFieldStatus::type status);
    bool IsAllFieldStatus(PledgeGameFieldStatus::type status);
    bool StartSeason();
    void EndPledgeGameSeason(bool setNextSeason);
    void Init();
    void OnGameChanged(std::int32_t fieldId);
    bool RegisterEntry(User* user);
    bool RegisterExitObserverEntry(User* user);
    bool RegisterObserverEntry(User* user, std::int32_t fieldId);
    bool RequestObserverList(User* user);
    bool PledgeGameHTML(User* user);
    void SendRecordToUser(User* user);
    bool UpdateTopRecord(std::int32_t topRecord);
    void SetPledgeGameRecord(
        std::int32_t season, std::int32_t userSid, std::int32_t prizeCount, std::int32_t surrenderCount
    );
    bool IsRegisteredUser(User* user);
    bool GiveItem(User* user, std::int32_t itemClassId, std::int64_t count, bool saveRecord);
    void SavePledgeGameRecord(std::int32_t userDbId, std::int32_t count);
    bool IsValidUser(User* user, std::int32_t& reason);
    bool LeaveGame(User* user, bool includeWaitingPlayer);
    template <class... Args>
    void DebugString(std::u16string_view format, const Args&... arguments) {
        SendDebugText(FormatUtf16(format, arguments...));
    }
    void SendDebugText(std::u16string text);
    const char16_t* GetPlayersName(std::int32_t country, std::int32_t playerNumber);
    std::int32_t GiveItemToWinner(std::int32_t userSid, std::int32_t itemClassId, std::int32_t count, bool force);
    void RegisterPledgeGameMaker(std::int32_t npcMakerId);
    void SetGotItemPledgeGame();
    bool IncreaseKillCountByKillingNPC(CCreature* killer);
    void SetLosePrizeItem(std::int32_t itemClassId, std::int32_t count);
    void SetWinPrizeNormalItem(std::int32_t itemClassId, std::int32_t minimum, std::int32_t maximum);
    void SetWinPrizeRandItem(std::int32_t itemClassId, std::int32_t minimum, std::int32_t maximum);
    void LoadFromDb(
        std::int32_t season,
        std::int32_t step,
        std::int32_t seasonStartTime,
        std::int32_t maxPrizeCount,
        std::int32_t previousPledgeDbId,
        char16_t* previousPledgeName,
        std::int32_t previousCharacterDbId,
        char16_t* previousCharacterName,
        std::int32_t previousWinnerGotItem
    ) {
        const auto copy25 = [](char16_t (&destination)[25], const char16_t* source) {
            if (source == nullptr) {
                destination[0] = u'\0';
                errno = EINVAL;
                runtime::process::HandleInvalidParameter(L"wcscpy_s");
                return;
            }
            for (std::size_t index = 0; index < 25; ++index) {
                destination[index] = source[index];
                if (source[index] == u'\0') {
                    return;
                }
            }
            destination[0] = u'\0';
            errno = ERANGE;
            runtime::process::HandleInvalidParameter(L"wcscpy_s");
        };

        SharedPledgeGameInfo::GetInfo()->m_nSeason = season;
        SharedPledgeGameInfo::GetInfo()->m_nStep = step;
        SharedPledgeGameInfo::GetInfo()->m_nTopRankerTotalPrize = maxPrizeCount;
        SharedPledgeGameInfo::GetInfo()->m_nPrevSeasonWinnerPledgeDbId = previousPledgeDbId;

        SharedPledgeGameInfo* info = SharedPledgeGameInfo::GetInfo();
        std::memset(info->m_PrevSeasonWinnerPledgeName, 0, sizeof(info->m_PrevSeasonWinnerPledgeName));
        copy25(info->m_PrevSeasonWinnerPledgeName, previousPledgeName);

        SharedPledgeGameInfo::GetInfo()->m_nPrevSeasonWinnerCharacterDbId = previousCharacterDbId;
        info = SharedPledgeGameInfo::GetInfo();
        std::memset(info->m_PrevSeasonWinnerCharacterName, 0, sizeof(info->m_PrevSeasonWinnerCharacterName));
        copy25(info->m_PrevSeasonWinnerCharacterName, previousCharacterName);

        SharedPledgeGameInfo::GetInfo()->m_nPrevSeasonWinnerGotItem = previousWinnerGotItem;
        m_tSeasonStart = seasonStartTime;
    }

    static std::vector<std::int32_t> s_lstWday;
    static std::vector<std::int32_t> s_lstHour;

protected:
    void ArrangeEntryToField();
    void ClearEntry();

private:
    friend class CPledgeGameLoadingMessage;
    friend class CPledgeGameEntry;
    friend class CPledgeGameField;
    friend class CPledgeGameHtmlCommandHandler;
    friend class CPledgeGamePlayers;
    friend class CPledgeGameUser;
    friend class CParserForSetting;
    friend bool BuilderCmd::PledgeGame(CUserSocket*, User*, char16_t*);
    void InitPledgeGameField();
    void InitPledgeGameInZone();
    void CheckPlayerNameString(User* user);
    void InitPlayersName(std::int32_t countryCode);
    void IncreasePledgeGameSurrenderCount(std::int32_t userSid, std::int32_t amount);
    bool UnregisterEntry(User* user);

    std::int32_t m_tLastAccouncing{}; // +0x0018
    std::int32_t m_tNow{}; // +0x001c
    std::int32_t m_OpenTime{}; // +0x0020
    std::array<std::byte, 0x04> m_alignment024To028{};
    const CSkillInfo* m_pWaitSkill{}; // +0x0028
    CYieldLock m_PledgeGameLock; // +0x0030..+0x003f
    bool m_bStartGameAutomatically{true}; // +0x0040
    std::array<std::byte, 0x03> m_alignment041To044{};
    std::int32_t m_tSeasonStart{}; // +0x0044
    std::int32_t m_nDbProcessing{}; // +0x0048
    std::int32_t m_nMessageRegistered{}; // +0x004c
    PledgeGameMessage m_msgReceived; // +0x0050..+0x005b
    std::array<std::byte, 0x04> m_alignment05cTo060{};
    COlympiadDefaultSetting m_Setting; // +0x0060..+0x5e97
    std::int32_t m_nMatchTime{}; // +0x5e98
    std::int32_t m_nStartSec{}; // +0x5e9c
    bool m_bGetPenalty{}; // +0x5ea0
    std::array<std::byte, 0x03> m_alignment5ea1To5ea4{};
    std::uint32_t m_TickCount{}; // +0x5ea4
    CPledgeGameHtml m_pledgeGameHtml; // +0x5ea8..+0x5f5f
    std::map<std::int32_t, std::vector<std::u16string>*> m_mapPlayersName; // +0x5f60..+0x5f7f
    std::int32_t m_nActiveInstantZoneCount{6}; // +0x5f80
    std::array<std::byte, 0x04> m_alignment5f84To5f88{};

public:
    CPledgeGameEntry m_Entry; // +0x5f88..+0x5fcf
    CPledgeGameField* m_pPledgeGameFieldList{}; // +0x5fd0
    std::int32_t m_isLoadingPoint{}; // +0x5fd8
    std::uint32_t m_nDebugUserSID{}; // +0x5fdc

private:
    std::int32_t m_nLosePrizeItemType{}; // +0x5fe0
    std::int32_t m_nLosePrizeItemCount{}; // +0x5fe4
    std::int32_t m_nWinPrizeNormalItemType{}; // +0x5fe8
    std::int32_t m_nWinPrizeNormalMinCnt{}; // +0x5fec
    std::int32_t m_nWinPrizeNormalMaxCnt{}; // +0x5ff0
    std::int32_t m_nWinPrizeRandItemType{}; // +0x5ff4
    std::int32_t m_nWinPrizeRandItemMinCnt{}; // +0x5ff8
    std::int32_t m_nWinPrizeRandItemMaxCnt{}; // +0x5ffc
};

class CPledgeGameHtmlCommandHandler {
public:
    static void Handle(CSocket* socket, const char16_t* command);

private:
    static void MoveObserverField(CSocket* socket, std::int32_t fieldId);
};

extern CPledgeGameSystem g_PledgeGameSystem;

#pragma once

#include "OlympiadNetwork.h"
#include "OlympiadSystem.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Obj.h"
#include "Shared/src/OlympiadDefine.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/Utils.h"
#include "Shared/src/YieldLock.h"
#include "Shared/src/proto/OlympiadPacket.h"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CInstantZone;
class CSummon;
class User;
class WOlympiadMatch;
class CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10;
class CDX_WOLYMPIAD_RECORD;

namespace network {
struct IPacket;
}

namespace protocol {
struct RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD;
}

namespace WOlympiadMatchTime {

enum Value : std::int32_t {
    BEFORE_MATCHTIME = 0,
    NOW_MATCHTIME = 1,
    BEFORE_FINISH = 2,
};

} // namespace WOlympiadMatchTime

namespace ObserveResult {

enum Value : std::int32_t {
    Success = 0,
    Failed = 1,
};

} // namespace ObserveResult

enum GameResultEnum : std::int32_t {
    GRE_DRAW = 0,
    GRE_WIN = 1,
    GRE_LOSE = 2,
    GRE_MAX = 3,
};

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

struct WOlympiadStatus {
    WOlympiadStatus() : nStep(0), nMatchTime(0) {}

    int nStep;
    int nMatchTime;
};

struct WOlympiadGameResultInfo {
    int fieldId;
    int elapsedTime;
    GameRuleType gameRule;
    int nInzoneType;
    int nWinnerItemCount;
    int nLoserItemCount;
};

struct WOlympiadMessage {
    WOlympiadMessage();

    int nSeason;
    int nStep;
    std::int32_t tSeasonStart;
    int idLegendary;
};

struct WOlympiadMatchingPoint {
    std::int64_t nMatchingPointBase;
    std::int64_t nMatchingPointDiff;
    std::int64_t nMatchingPointTotal;
    std::int64_t nWinningPoint;
    float fWinningRate;
    std::uint8_t m_alignment0024To0028[4]{};
};

class WOlympiadMemberInfo : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(WOlympiadMemberInfo)

    explicit WOlympiadMemberInfo(const std::CDualKey<int, int>& key);
    WOlympiadMemberInfo(const std::CDualKey<int, int>& key, const olympiad::PkApplication& application);
    ~WOlympiadMemberInfo() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    std::CDualKey<int, int> GetKey() const;
    int GetWorldID() const;
    int GetDBID() const;
    GameRuleType GetGameRule() const;
    ClassType GetClassType() const;
    void SetFieldID(int fieldId);
    int GetFieldID() const;
    bool IsInMatch() const;
    void SetAuthInfo(const olympiad::PkOlympiadAuthInfo& authInfo);
    const olympiad::PkOlympiadAuthInfo& GetAuthInfo() const;
    bool IsPlayer() const;
    GameValidCondition GetPrepareValidCondition() const;
    void SetPrepareValidCondition(GameValidCondition condition);
    void SetPeerDisconnect();
    bool IsPeerDisconnected() const;
    const std::u16string& GetCharName() const;
    const std::u16string& GetWorldCharName() const;
    int GetAccountID() const;
    RaceType GetRace() const;
    SexType GetSex() const;
    int GetLevel() const;
    const std::u16string& GetAccountName() const;
    const olympiad::PkApplication& GetAllication() const;
    void ResetMatchingInfo();
    void SetMatchingPoint(std::int64_t point);
    std::int64_t CalculateMatchingPoint(std::int64_t point) const;
    std::int64_t GetWinningPoint() const;
    bool IsLoadedMatchingPoint() const;
    const WOlympiadMatchingPoint& GetMatchingPoint() const;
    WOlympiadMatchingPoint& GetOlympiadPoint();
    void SetWinningRate(std::int64_t opponentPoint, std::int64_t ownPoint);
    float GetWinningRate() const;
    void SetWinningPoint(std::int64_t point, float rate);

private:
    std::CDualKey<int, int> m_Key; // +0x018
    olympiad::PkApplication m_Application; // +0x020
    bool m_bIsPlayer; // +0x090
    [[maybe_unused]] std::uint8_t m_alignment0091To0094[3]{};
    olympiad::PkOlympiadAuthInfo m_AuthInfo; // +0x094
    mutable std::u16string m_sWorldCharName; // +0x140
    int m_nFieldID; // +0x168
    GameValidCondition m_eGameValidCondition; // +0x16c
    bool m_bPeerDisconnected; // +0x170
    bool m_bMatchingPointLoaded; // +0x171
    [[maybe_unused]] std::uint8_t m_alignment0172To0178[6]{};
    WOlympiadMatchingPoint m_MatchingPoint; // +0x178

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(WOlympiadMemberInfo)
};

struct WOlympiadPlayerInfo {
    int playerWorldId;
    std::uint32_t playerSID;
    int pledgeId;
    char16_t pledgeName[24];
};

class WOlympiadTeam : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(WOlympiadTeam)

    WOlympiadTeam();
    explicit WOlympiadTeam(CSPointer<WOlympiadMemberInfo> member);
    WOlympiadTeam(const WOlympiadTeam& other);
    ~WOlympiadTeam() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool IsRegisteredUser(std::uint32_t sid) const;
    bool IsRegisteredUser(CSPointer<User> user) const;
    bool IsRegisteredUser(std::CDualKey<int, int> key) const;
    void SetConnectedUser(CSPointer<User> user);
    void SetFieldID(int fieldId);
    void UnregisterAll();
    bool IsValidCondition(GameValidCondition condition, int& messageId) const;
    bool IsValidCondition(GameValidCondition condition) const;
    bool IsDisconnected() const;
    bool IsPeerDisconnected() const;
    std::CDualKey<int, int> GetMemberKey() const;
    CSPointer<WOlympiadMemberInfo> GetMember() const;
    std::uint32_t GetUserSID() const;
    const WOlympiadPlayerInfo* GetPlayerInfo() const;
    int GetDamage() const;
    std::uint32_t GetPledgeSID() const;
    int GetOlympiadPoint() const;
    int GetPartyMasterIndex() const;
    const wchar_t* GetPartyMasterName() const;
    void Set(const WOlympiadTeam* other);
    bool IsMatchingPointLoaded() const;
    void SetWinningPoint(std::int64_t point);
    bool WaitingGame();
    void StartGame();
    void FinishGame(bool needRevive);
    void ObserverEnd();
    void ResetStatus();
    void RegisterEntry();
    void EscapeFromOlympiad();
    void SetPlayer(OlympiadTeamEnum team, int fieldId, CInstantZone* inZone);
    void Freeze();
    void RestorePlayers(bool bWithSkill);
    void UpdateStatus(WOlympiadMatch* match, int status);
    void SavePlayers();
    void Win(WOlympiadTeam& rival, const WOlympiadGameResultInfo& result);
    void Draw(WOlympiadTeam& rival, const WOlympiadGameResultInfo& result);
    void PayPenalty(WOlympiadTeam& rival, const WOlympiadGameResultInfo& result, GameValidCondition condition);
    void OnAppearingPacket();
    void IncreaseMatchCount();
    void SendSystemMessage(int msgNo) const;
    void SendV(const char* format, va_list arguments) const;
    void Send(const char* format, ...) const;
    void Send(S_EX_PROTOCOL protocol, const network::IPacket& packet) const;
    void SendCancelMatch(GameValidCondition condition, int messageId) const;
    void SendSummonInfo(WOlympiadTeam& oppTeam);
    void HideSummonInfo(WOlympiadTeam& oppTeam);
    int EncodeMatchResultToBuffer(int opponentDamage, char* buffer, int capacity) const;
    int GetPrevOlympiadPoint() const;
    void SetPrevOlympiadPoint(int point);

private:
    void RecvSummonInfo(CSummon* pSummon) const;
    void RecvHideSummonInfo(CSummon* pSummon) const;
    void SendInvalidMessage(int messageId, User* user) const;
    bool IsValidTeam() const;
    bool IsValidInventory() const;
    bool IsValidSubjob() const;
    bool IsNotCursedUser(int& param) const;
    bool IsAlive() const;
    bool IsActive() const;
    bool IsFishingZone() const;
    bool IsNotTraining() const;
    static void SendSummonHPMPInfo(User* user, CSummon* summon);
    static void HideSummonHPMPInfo(User* user, CSummon* summon);
    const wchar_t* GetPledageName() const;
    ClassType GetClassType() const;
    int GetWorldID() const;
    void SetChangePoint(int point);

    GameRuleType m_GameRule; // +0x18
    int m_PartyMasterIndex; // +0x1c
    int m_ChangePoint; // +0x20
    int m_PrevOlympiadPoint; // +0x24
    CSPointer<WOlympiadMemberInfo> m_spMember; // +0x28
    WOlympiadPlayerInfo m_PlayerInfo; // +0x38

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(WOlympiadTeam)
};

class WOlympiadMatchMaker {
public:
    WOlympiadMatchMaker();
    ~WOlympiadMatchMaker();

    void AddEntry(WOlympiadTeam* team, GameRuleType gameRule);
    void RemoveEntry(std::CDualKey<int, int> key);
    void ClearAllEntry();
    int GetClassGroupEntryCount(int group) const;
    WOlympiadTeam* GetEntry(GameRuleType gameRule);
    void EntrySelectInit();
    void AnnounceToEntry(std::uint32_t sid, GameRuleType gameRule) const;
    bool IsRegisteredUser(std::CDualKey<int, int> key) const;
    bool CheckEntryByGameRule(GameRuleType gameRule) const;
    void AddEntryCount(GameRuleType gameRule, int count);

private:
    void _AddEntryCount(GameRuleType gameRule, int count);

public:
    CYieldLock m_EntryMapLock; // original public boundary
    std::deque<CSPointer<WOlympiadTeam>> m_TeamList[3];

private:
    bool m_RuleSelected[39];
    std::uint8_t m_alignment00c7To00c8{};
    int m_EntryCount[40];
};

class WOlympiadParam {
public:
    enum PeriodType : std::int32_t {
        USER_DEFINED = 0,
        DAILY = 1,
        MONTHLY = 2,
    };

    WOlympiadParam();
    bool GetWantedStatus(std::int32_t now, WOlympiadStatus& status) const;
    WOlympiadCondition::Value GetStep(int index) const;
    bool AbleToAddEntry(std::int32_t now, int start, int end) const;
    void SetTerm(int start, int bonus1, int bonus2, int bonus3, int bonus4, int nominate);
    bool IsMonthly() const;
    void SetMonthly(bool monthly);
    std::int32_t GetNextSeasonStartTime(std::int32_t start) const;
    void GetDebugString(std::u16string& output) const;
    static WOlympiadMatchTime::Value GetMatchTime(WOlympiadCondition::Value step, const L2DateTime& time);
    static WOlympiadMatchTime::Value GetMatchTime(WOlympiadCondition::Value step, PeriodType period, const L2DateTime& time);

private:
    friend class WOlympiadService;

    CYieldLock m_lock;
    bool m_bTermSet;
    std::uint8_t m_alignment0011To0014[3]{};
    int m_aTerm[8];
    PeriodType m_PeriodType;
};

class WOlympiadHtml {
public:
    WOlympiadHtml();
    ~WOlympiadHtml();

    void Init();
    void OnGameChanged(int fieldId);
    void SendGameList(CSPointer<User> user) const;
    void SendGameList(std::CDualKey<int, int> key) const;
    void SendGameListDump(std::CDualKey<int, int> key) const;

private:
    std::map<int, const sOlympiadFieldInfo*> m_mapMatchList;
    RWLock m_Lock;
};

class WOlympiadUsers : public Singleton<WOlympiadUsers> {
public:
    enum MatchLimitType : std::int32_t {
        MLT_GAMERULE = 0,
        MLT_TOTAL = 1,
        MLT_MAX = 2,
    };

    struct WeeklyOlympiadMatchCount {
        int nSeason;
        int nStep;
        int nMatchCount[3];
    };

    WOlympiadUsers();
    ~WOlympiadUsers() override;

    void Init();
    void LoadMatchContFromDB();
    bool IsRegistered(std::CDualKey<int, int> key) const;
    void IncMatchCount(std::CDualKey<int, int> key, GameRuleType rule, int step);
    int GetNoblessIndex(std::CDualKey<int, int> key) const;
    const SharedNoblessInfo* GetNoblessInfo(std::CDualKey<int, int> key) const;
    void SetNoblessInfo(
        std::CDualKey<int, int> key,
        int heroType,
        int winCount,
        int previousPoint,
        int olympiadPoint,
        int matchCount,
        int win,
        int lose,
        int olympiadTradePoint,
        bool update
    );
    void SetMatchCount(std::CDualKey<int, int> key, int season, int step, GameRuleType rule, int count);
    int GetMatchCountByStep(std::CDualKey<int, int> key, int season, int step, GameRuleType rule) const;
    int GetRemainMatchCount(std::CDualKey<int, int> key) const;
    bool CanRegistMatch(std::CDualKey<int, int> key, int season, int step, MatchLimitType& limit, GameRuleType rule);
    void SendMatchCount(std::CDualKey<int, int> receiver, std::CDualKey<int, int> subject) const;
    std::vector<std::CDualKey<int, int>> GetHeroes(int season) const;
    void SendOlympiadRecordTo(std::CDualKey<int, int> receiver, const CDX_WOLYMPIAD_RECORD* record) const;

private:
    CYieldLock m_LockNobless; // +0x08
    std::map<std::CDualKey<int, int>, int> m_mapNoblessToIndex; // +0x18
    std::map<std::CDualKey<int, int>, WeeklyOlympiadMatchCount> m_mapWeeklyMatchCount; // +0x38
    std::map<int, std::set<std::CDualKey<int, int>>> m_mapHeroCache; // +0x58
};

class WOlympiadService : public Singleton<WOlympiadService>, public CIOObject, public IWOlympiadNetworkHostCallback {
public:
    enum StartMatchMakingResult : std::int32_t {
        SMMR_Success = 0,
        SMMR_TimeLimit = 1,
        SMMR_Failed = 2,
    };

    class OlympiadMembers {
    public:
        using MembersFromWorld = std::map<int, CSPointer<WOlympiadMemberInfo>>;

        OlympiadMembers();
        ~OlympiadMembers();

        void AddMember(CSPointer<WOlympiadMemberInfo> member);
        void RemoveMember(std::CDualKey<int, int> key);
        bool IsMember(std::CDualKey<int, int> key) const;
        CSPointer<WOlympiadMemberInfo> FindMember(std::CDualKey<int, int> key) const;
        void Clear();
        std::vector<CSPointer<WOlympiadMemberInfo>> GetMembers(int worldId) const;

    private:
        CYieldLock m_Lock;
        std::map<int, MembersFromWorld> m_Members;
        std::set<std::CDualKey<int, int>> m_MemberKeys;
    };

    class MatchResults {
    public:
        struct Node {
            GameResultEnum eResult;
            int nItemCount;
        };

        MatchResults();
        ~MatchResults();

        bool Pop(std::CDualKey<int, int> key, GameResultEnum& result, int& itemCount);
        void Push(std::CDualKey<int, int> key, GameResultEnum result, int itemCount);

    private:
        CYieldLock m_Lock;
        std::map<std::CDualKey<int, int>, Node> m_mapMatchResults;
    };

    WOlympiadService();
    ~WOlympiadService() override;

    bool Init();
    void InitHtml();
    bool IsAvailable() const;
    void TimerExpired(int id) override;
    void OnPeerDisconnected(int worldId) override;

    int GetSeason() const;
    WOlympiadCondition::Value GetStep() const;
    std::int32_t GetSeasonStart() const;
    int GetNominateSec() const;
    int GetLegendaryWorld() const;
    bool IsPossibleToSetHero() const;
    GameRuleType GetValidRule() const;
    WOlympiadMatch* GetField(int fieldId);
    StartMatchMakingResult StartMatchMaking(CSPointer<WOlympiadMemberInfo> member);
    void StopMatchMaking(std::CDualKey<int, int> key);
    void DisconnectMember(std::CDualKey<int, int> key);
    void UnregisterEntry(std::CDualKey<int, int> key);
    bool RegisterAuthInfo(std::CDualKey<int, int> key, const olympiad::PkOlympiadAuthInfo& authInfo);
    const olympiad::PkOlympiadAuthInfo* FindAuthInfo(std::CDualKey<int, int> key) const;
    CSPointer<WOlympiadMemberInfo> FindMember(std::CDualKey<int, int> key) const;
    bool AssignConnectedUser(CSPointer<User> user);
    bool ObserveMatch(std::CDualKey<int, int> key, int fieldId, std::uint32_t accountId);
    void StopObserving(CSPointer<User> user);
    void ChangeMatchInObserving(CSPointer<User> user, int fieldId);
    void SetPrepareValidCondition(std::CDualKey<int, int> key, GameValidCondition condition);
    void NotifyLastMatchResult(std::CDualKey<int, int> key);
    void GetDebugString(std::u16string& output) const;
    void SendGameList(std::CDualKey<int, int> key) const;
    void SendGameList(CSPointer<User> user) const;
    void SendGameListDump(std::CDualKey<int, int> key) const;
    void SendClassRankByServerTop10(int worldId, const CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10* rank) const;
    void RegisterDbProcessing();
    void UnregisterDbProcessing();
    void SetSeasonStartOnTheFly(std::int32_t seasonStart);
    void SetOlympiadTerm(
        bool monthly,
        bool saveTerm,
        int season,
        int startSec,
        int bonus1Sec,
        int bonus2Sec,
        int bonus3Sec,
        int bonus4Sec,
        int nominateSec,
        bool nominateHeroes,
        bool notifyNewSeason
    );
    void SetOlympiadMonthly(int season, bool nominateHeroes, bool notifyNewSeason);
    void ClearAllEntry();
    void StartOlympiadSeason();
    void SetFromDb(int season, int step, std::int32_t seasonStart, int legendaryId);
    void LoadFromDb(int season, int step, std::int32_t seasonStart, int legendaryId);
    void SetDefaultOlympiadTerm(int season);
    void OnAddUser(bool success, int worldId, int userDbId);
    void NotifyNoblessInfo(std::CDualKey<int, int> key, bool newSeason);
    bool OnLoadMatchPointComplete(const protocol::RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD& reply);
    bool NotifyOlympiadMatchingInfo(int worldId);
    void DoNextStep(int fieldId, std::u16string parameter);
    void SetDebugUser(std::CDualKey<int, int> key);
    bool IsAnnouncingTurn(int intervalSeconds);
    bool IsDebugging() const;
    WOlympiadMatch* FindVaildField();
    void RegisterOlympiadMaker(int instantZoneTypeId);
    template <class... Args>
    void DebugString(std::u16string_view format, const Args&... arguments) {
        SendDebugText(FormatUtf16(format, arguments...));
    }
    void SendDebugText(std::u16string text);
    void OnGameChanged(int fieldId);
    void SaveLastMatchResult(std::CDualKey<int, int> key, GameResultEnum result, int itemCount);
    void SaveOlympiadRecord(
        std::CDualKey<int, int> winner,
        std::CDualKey<int, int> loser,
        int winnerPoint,
        int winnerItemType,
        int winnerItemAmount,
        int loserPoint,
        int loserItemType,
        int loserItemAmount,
        int elapsedTime,
        int resultType,
        GameRuleType gameRule
    );
    void SaveOlympiadPenaltyLog(
        std::CDualKey<int, int> key, int point, int totalPoint, int fieldId, int gameRule, int penaltyType
    );
    void SetForceInstantZoneTypeID(int typeId);
    void ConfirmOlympiadUser(CSPointer<User> user);
    void OnUserDisconnected(CSPointer<User> user);
    void Restart(CSPointer<User> user);
    bool ReturnToOrigin(CSPointer<User> user);

private:
    friend class WOlympiadHtml;

    void ProcessMessage();
    void CheckAutoStart();
    void ConfirmStep();
    void BroadcastOlympiadMatchingInfo();
    void InitOlympiadField();
    void InitOlympiadInZone();
    void StopAllMatchMaking();
    void DoMatch(int gameRule);
    bool LoadMatchingPoint(int gameRule);

    CYieldLock m_FieldMatchLock; // +0x028
    OlympiadMembers* m_pMembers; // +0x038
    std::int32_t m_tLastAccouncing; // +0x040
    std::int32_t m_tSeasonStart; // +0x044
    int m_nDbProcessing; // +0x048
    int m_nMessageRegistered; // +0x04c
    WOlympiadMessage m_msgReceived; // +0x050
    std::vector<WOlympiadMatch> m_vOlympiadFieldList; // +0x060
    WOlympiadMatchMaker m_Entry; // +0x080
    WOlympiadParam m_Param; // +0x1e8
    int m_nMatchTime; // +0x220
    int m_nStartSec; // +0x224
    int m_nBonus1Sec; // +0x228
    int m_nBonus2Sec; // +0x22c
    int m_nBonus3Sec; // +0x230
    int m_nBonus4Sec; // +0x234
    int m_nNominateSec; // +0x238
    std::uint32_t m_TickCount; // +0x23c
    WOlympiadHtml m_olympiadHtml; // +0x240
    std::map<std::CDualKey<int, int>, CSPointer<WOlympiadMemberInfo>> m_sidPlayerMap[3]; // +0x298
    std::CDualKey<int, int> m_keyDebugUser; // +0x2f8
    JobTransactionLock m_MatchMakingJob[3]; // +0x300
    int m_nForceInstantZoneTypeID; // +0x324
    MatchResults* m_pLastMatchResults; // +0x328
};

namespace WOlympiadServiceUtil {

std::int64_t ValidateMatchingPointDiff(std::int64_t basePoint, std::int64_t matchingPointDiff);
bool LinkNobless(CSPointer<User> user);
void BroadcastMessage(int messageId);
void BroadcastOSN_SYSTEM_MESSAGE_NO(int messageId);
void BroadcastOSN_NOTIFY_SEASON(int season, int step, std::int32_t seasonStart, int nominateSec, int legendaryId);
void BroadcastStartSeason(int season);
void BroadcastStopSeason(int season);
void BroadcastOSN_OLYMPIAD_POINT(int point);
void ResetRankInfo(ClassType classType);
void AddRankInfo(ClassType classType, int ranking, const wchar_t* name, int point);
void SetSeasonPlayerCount(int count);

void SendOSA_VERSION(int worldId, bool success);
bool SendOSA_START(std::CDualKey<int, int> key, GameRuleType gameRule, StartResult::Value result, int param1, int param2);
bool SendOSA_STOP(std::CDualKey<int, int> key, StopResult::Value result);
bool SendOSA_OBSERVE(std::CDualKey<int, int> key, ObserveResult::Value result);
bool SendOSQ_PREPARE(std::CDualKey<int, int> key);
bool SendOSN_CONNECT(std::CDualKey<int, int> key);
void SendOSN_STOP(std::CDualKey<int, int> key);
bool SendOSA_MATCH_COUNT(std::CDualKey<int, int> key, GameRuleType gameRule, int matchCount);
void SetEntryCount(GameRuleType gameRule, int count);
bool SendOSN_SYSTEM_MESSAGE_NO(std::CDualKey<int, int> key, int messageId);
bool SendOSN_SYSTEM_MESSAGE(std::CDualKey<int, int> key, const std::u16string& message);
void SendOSN_CANCEL_MATCH(std::CDualKey<int, int> key, GameValidCondition condition, int itemId);
void SendOSN_OLYMPIAD_POINT(std::CDualKey<int, int> key, int point);
void SetFieldInfo(
    int fieldId,
    OlympiadFieldStatus status,
    int player1,
    int player2,
    GameRuleType gameRule,
    int instantZoneTypeId,
    int instantZoneId
);
bool SortMembersWithMatchingPoint(MatchingPoint::MATCHINGPOINT_TYPE type, std::deque<CSPointer<WOlympiadTeam>>& members);
bool GenerateWinningRate(WOlympiadTeam& team1, WOlympiadTeam& team2);
bool UpdateMatchingPoint(int gameResult, WOlympiadTeam& team1, WOlympiadTeam& team2);
void SaveMatchPoints(int nMatchID, const std::vector<CSPointer<WOlympiadMemberInfo>>& members);

} // namespace WOlympiadServiceUtil

namespace WOlympiadServiceConst {

extern bool _MATCHING_POINT_MODE;

} // namespace WOlympiadServiceConst

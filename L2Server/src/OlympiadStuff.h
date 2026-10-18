#pragma once

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/DateTime.h"
#include "Shared/src/IOC.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Obj.h"
#include "Shared/src/OlympiadDefine.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Skill.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

class CInstantZone;
class CUserSocket;
struct SharedNoblessInfo;
struct AlterSkillInfo;
struct stAccountAuthInfo;
class User;
class CParserForSetting;
class WOlympiadMatch;

class OlympiadConnectVerifier final : public CIOObject {
public:
    explicit OlympiadConnectVerifier(CUserSocket* socket);
    ~OlympiadConnectVerifier() override = default;

    void TimerExpired(int id) override;

private:
    CUserSocket* m_pUserSock; // +0x18
};


namespace olympiad {
struct PkOlympiadAuthInfo;
struct PkApplication;
struct OSN_NOBLESS_INFO;
} // namespace olympiad

namespace network {
struct IPacket;
}

enum S_EX_PROTOCOL : std::int32_t;
enum LogOlympiadEnterType : std::int32_t;

namespace WOlympiadStuff {

bool DecodePacket(network::IPacket& packet, const std::uint8_t* source);
void Convert(const olympiad::PkOlympiadAuthInfo* source, stAccountAuthInfo* destination);
void Convert(const stAccountAuthInfo* source, olympiad::PkOlympiadAuthInfo* destination);
void ConvertNoblessInfo(const SharedNoblessInfo* source, olympiad::OSN_NOBLESS_INFO* destination);
User* FindUserBySID(std::uint32_t idUser);
CSPointer<User> FindUserByKey(std::CDualKey<int, int> key);
bool IsAllowedClass(ClassType classType);
bool IsAllowedUser(CSPointer<User> user);
bool HasEnoughSpaceInInventory(CSPointer<User> user);
bool IsTalkableDistance(CSPointer<User> user, int npcIndex);
bool SendPacketTo(std::uint32_t serverId, S_EX_PROTOCOL protocol, const network::IPacket& packet);
void Write_LOG_ADD_OLYMPIAD_ENTRY_InPeer(CSPointer<User> user, GameRuleType gameRule);
void Write_LOG_REMOVE_OLYMPIAD_ENTRY_InPeer(CSPointer<User> user);
void Write_LOG_SET_HERO_InPeer(CSPointer<User> user, int winCount);
void Write_LOG_WOLYMPIAD_REMOVE_ENTRY_InHost(int worldId, int userDbId, const olympiad::PkApplication& application);
void Write_LOG_WOLYMPIAD_ADD_ENTRY_InHost(int worldId, int userDbId, const olympiad::PkApplication& application);
void Write_LOG_WOLYMPIAD_SET_HERO_InHost(int worldId, int userDbId, const char16_t* charName, int classType, int winCount);
void Write_LOG_WOLYMPIAD_SET_NOBLESS_InHost(
    int worldId, int userDbId, const char16_t* charName, int classType, int heroClassType
);
void Write_LOG_WOLYMPIAD_ENTER_INZONE_InHost(
    CSPointer<User> user,
    int instantZoneTypeId,
    int instantZoneId,
    int fieldId,
    LogOlympiadEnterType enterType,
    int matchCount,
    int gameRule
);
void Write_LOG_WOLYMPIAD_EXIT_INZONE_InHost(
    CSPointer<User> user,
    int instantZoneTypeId,
    int instantZoneId,
    int fieldId,
    LogOlympiadEnterType enterType,
    int matchCount
);

} // namespace WOlympiadStuff

namespace StartResult {

enum Value : std::int32_t {
    Success = 0,
    Failed = 1,
    Already = 2,
};

} // namespace StartResult

namespace StopResult {

enum Value : std::int32_t {
    Success = 0,
    NotMember = 1,
    Playing = 2,
};

} // namespace StopResult

namespace WOlympiadCondition {

enum Value : std::int32_t {
    NONE = 0,
    START_SEASON = 1,
    RESET_POINT = 2,
    GIVE_1ST_BONUS = 3,
    GIVE_2ND_BONUS = 4,
    GIVE_3RD_BONUS = 5,
    GIVE_4TH_BONUS = 6,
    NOMINATE_HEROES = 7,
    CONDITION_MAX = 8,
};

} // namespace WOlympiadCondition

struct WOlympiadResetSkillInfo {
    int reuseDelayLimit{}; // +0x00
    std::array<std::byte, 0x04> m_alignment0004To0008{}; // +0x04..+0x07
    std::vector<int> notResetSkillIdList; // +0x08..+0x27
};

class WOlympiadDefaultSetting : public Singleton<WOlympiadDefaultSetting> {
public:

    struct BonusItemData {
        int itemClassId{};
        int itemCount{};
        int itemClassIdForLoser{};
        int itemCountForLoser{};
    };

    WOlympiadDefaultSetting();
    ~WOlympiadDefaultSetting() override;

    void AddForbiddenWeapon(int itemClassId);
    void AddForbiddenItem(int itemClassId);
    void AddGiveSkill(ClassType classType, GameRuleType gameRule, std::int64_t skillKey);
    void AddNotResetSkill(const std::vector<std::int64_t>* skillKeys);
    void SetAdditionalRewardCount(int winner, int loser);
    void SetBonusItemData(
        GameRuleType gameRule, int winnerItemClassId, int winnerItemCount, int loserItemClassId, int loserItemCount
    );
    void SetDaysSetting(int dayOfWeek);
    void SetDenominator(GameRuleType gameRule, int denominator);
    void SetMatchLimit(GameRuleType gameRule, int limit);
    void SetResetSkillLimit(int reuseDelayLimit);
    void SetRewardCount(const std::vector<int>* rewardCounts);
    void SetWeight(GameRuleType gameRule, int weight);

    bool IsAllowedLevel(int level) const;
    bool IsAllowedItem(int itemClassId);
    int GetRewardItem() const;
    int GetStartTime() const;
    int GetEndTime() const;
    bool IsOlympiadStarted(L2DateTime::DayOfWeek dayOfWeek, int hour, int minute) const;
    int GetMatchLimit(GameRuleType gameRule) const;
    int GetMatchMinCount(GameRuleType gameRule) const;
    int GetOlympiadGrade(int rank, int playerCount) const;
    void SetTimeSetting(int hour, int minute);
    void SetEndTime(int hour, int minute);
    void SetMatchMinCount(GameRuleType gameRule, int count);
    void ResetMatchDay(const std::vector<L2DateTime::DayOfWeek>& days);
    BonusItemData* GetBonusItemData(GameRuleType gameRule);
    int GetPenaltyPoint(GameRuleType gameRule, int point) const;
    int GetTransferingPoint(GameRuleType gameRule, int winnerPoint, int loserPoint) const;
    int GetRewardCount(int index) const;
    int GetAdditionalRewardCount(bool winner) const;
    void GiveSkillToPlayer(User* user, GameRuleType gameRule);
    bool IsMatchDayListEmpty() const;
    void InitMatchDay();
    bool IsValidAction(int actionId) const;
    void AddAction(int actionId);
    void SendActionList(User* user);
    void ResetSkillReuseDelay(User* user);

private:
    friend class CParserForSetting;

    std::set<int> m_ForbiddenWeapons; // +0x0008..+0x0027
    std::set<int> m_ForbiddenItems; // +0x0028..+0x0047
    std::vector<CSkillKey> m_GiveSkills[108]; // +0x0048..+0x0dc7
    WOlympiadResetSkillInfo m_ResetSkillInfo; // +0x0dc8..+0x0def
    BonusItemData m_BonusItemData[3]; // +0x0df0..+0x0e1f
    int m_actionCount; // +0x0e20
    std::uint8_t m_buf[0x4f9c]; // +0x0e24..+0x5dbf
    std::uint32_t m_bufLen; // +0x5dc0
    int m_StartTime; // +0x5dc4
    int m_EndTime; // +0x5dc8
    int m_Denominator[3]; // +0x5dcc..+0x5dd7
    int m_Weight[3]; // +0x5dd8..+0x5de3
    int m_MatchLimit[4]; // +0x5de4..+0x5df3
    int SELECTION_MIN_COUNT[3]; // +0x5df4..+0x5dff
    std::vector<L2DateTime::DayOfWeek> m_vMatchDayList; // +0x5e00..+0x5e1f
    std::vector<int> m_vRewardCounts; // +0x5e20..+0x5e3f
    std::CDualKey<int, int> m_AdditionalRewardCount; // +0x5e40..+0x5e47
};


// Current L2Server.exe 0x0091549c/0x00915524 and PDB
// L2Server/OlympiadStuff.h. The shipping object occupies 0x38 bytes.
// The portable object owns a standard map. Its host layout may differ.
class WOlympiadServiceConfig : public Singleton<WOlympiadServiceConfig> {
public:
    struct Server {
        int idWorld{}; // +0x00
        std::uint32_t nInternalAddress{}; // +0x04
        std::uint32_t nExternalAddress{}; // +0x08
        int nPort{}; // +0x0c
    };

    WOlympiadServiceConfig();
    ~WOlympiadServiceConfig() override;

    void Load(const std::u16string& filename);
    bool IsActive() const;
    bool IsAllowedPeer(int worldId) const;
    bool IsHost() const;
    bool IsPeer() const;
    bool IsReturningFromHost(int worldId) const;

    std::map<int, int> m_ClientQuotaMap; // +0x08..+0x27
    Server m_Server; // +0x28..+0x37
};


class WOlympiadUserStateMachine {
public:
    enum UserState : std::int32_t {
        OLYMPIAD_NOT_USER = 0,
        OLYMPIAD_PLAYER1 = 1,
        OLYMPIAD_PLAYER2 = 2,
        OLYMPIAD_OBSERVER = 3,
        OLYMPIAD_ENTRY_REGISTER = 4,
        OLYMPIAD_PLAY_WAITING = 5,
        OLYMPIAD_OB_WAITING = 6,
        OLYMPIAD_MAX = 7,
    };

    WOlympiadUserStateMachine() : m_Status(OLYMPIAD_NOT_USER) {}

    bool CanChangeState(UserState state);
    bool ChangeState(UserState state);

    UserState GetStatus() const { return m_Status; }

private:
    UserState m_Status;
};

// Current L2Server.exe allocates exactly 0x78 bytes and its constructor at
// 009109ec writes every member boundary below. Bodies which still cross the
// wider olympiad service remain declaration-only GAPs. This class is the real
// physical owner, not a test surrogate.
class WOlympiadUser : public MemoryObject {
public:

    explicit WOlympiadUser(User* user);

    L2_DECLARE_MEMORY_ALLOCATION(WOlympiadUser)

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool IsAlive();
    bool CanAttack();
    bool IsEnemyTo(WOlympiadUser* target);
    WOlympiadUserStateMachine::UserState GetStatus() const;
    WOlympiadMatch* GetField();
    int GetFieldID() const noexcept {
        // Current L2Server 0x0096aa08.
        return m_FieldId;
    }
    FVector* GetReturnPos();
    int GetTeamId();
    void SetEnterInZone(CInstantZone* instantZone, int startPositionIndex);
    bool CanRegister();
    bool RegisterOlympiadEntry();
    bool WaitingOlympiadPlaying();
    bool WaitingOlympiadObserving();
    bool SetPlayer(std::uint8_t team, int fieldId, CInstantZone* instantZone, GameRuleType gameRule, int startPositionIndex);
    void ReadyForMatch(CInstantZone* instantZone, int startPositionIndex);
    bool SetObserver(int fieldId, CInstantZone* instantZone);
    void SetAlive(bool alive);
    void ChangeObserverField(int fieldId);
    bool SetReturnPos(FVector& position, bool force);
    void AddDamage(int damage);
    int GetDamage() const;
    void ResetDamage();
    bool ValidInventory();
    bool OlympiadStatusReset();
    bool IsPlayer() const;
    bool IsObserver() const;
    bool IsRegistered() const;
    bool IsPlaying() const;
    int GetStartPositionIndex() const noexcept { return m_nStartPosIndex; }
    void EscapeFromOlympiad();
    void OnDespawnPetByPlayer();
    void OnDespawnPetByObserver();
    void OnDespawnPetInPeer();
    void OnLeaveWorld();

private:
    friend class User;

    void LeaveParty();
    void ObserverHide(bool hide);
    void UnequipForbiddenWeapon();

    WOlympiadUserStateMachine m_StatusMachine; // +0x18..+0x1b
    std::array<std::byte, 0x04> m_alignment001cTo0020; // +0x1c..+0x1f
    User* m_Self; // +0x20..+0x27
    FVector m_ReturnPos; // +0x28..+0x3f
    FVector m_MovePos; // +0x40..+0x57
    GameRuleType m_GameRule; // +0x58..+0x5b
    std::int32_t m_FieldId; // +0x5c..+0x5f
    std::int32_t m_Damage; // +0x60..+0x63
    bool m_IsAlive; // +0x64
    bool m_IsObserver; // +0x65
    std::array<std::byte, 0x02> m_alignment0066To0068; // +0x66..+0x67
    std::int32_t m_nStartPosIndex; // +0x68..+0x6b
    std::array<std::byte, 0x04> m_alignment006cTo0070; // +0x6c..+0x6f
    CInstantZone* m_pInstantZone; // +0x70..+0x77
};


static_assert(std::is_trivially_copyable_v<WOlympiadUserStateMachine>);
static_assert(std::is_trivially_destructible_v<WOlympiadUserStateMachine>);

#pragma once

#include "StaticObject.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>


class CUserSocket;
class CParty;
class CSkillInfo;
class User;

class CSocket;

// The spellings are donor-selected replacement vocabulary. Every value
// consumed or produced below is established by current
// L2Server instructions.
enum EventMatchStatus : std::int32_t {
    EMStatus_MIN = 0,
    EMStatus_READY = 1,
    EMStatus_PLAYING = 2,
    EMStatus_PAUSE = 3,
    EMStatus_MAX = 4,
};

enum EventMatchObserverType : std::int32_t {
    EMOT_NOT_OBSERVER = 0,
    EMOT_BUILDER = 1,
    EMOT_PLAYER = 2,
};

enum ColosseumFenceState : std::int32_t {
    COLOSSEUM_FENCE_NONE = 0,
    COLOSSEUM_FENCE_COLUMN = 1,
    COLOSSEUM_FENCE_WALL = 2,
};

struct EventMatchTeam final {
    std::u16string name;
    std::uint32_t leaderSid = 0;
    std::int32_t locked = 0;
    std::uint32_t memberIds[7]{};
    std::int32_t memberCount = 0;
};

struct EventMatchFenceInfo final {
    ColosseumFenceState state = COLOSSEUM_FENCE_COLUMN;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

using EventMatchObserverMap = ::std::map<std::uint32_t, FVector>;
using EventMatchRuleIdSet = ::std::set<std::int32_t>;

// Exact current two-slot polymorphic rule envelope: deleting destructor and
// ShowInfo. Flag one is blacklist mode. Flag zero is whitelist mode.
class CEventMatchRuleSet {
public:
    CEventMatchRuleSet();
    virtual ~CEventMatchRuleSet();

    virtual void ShowInfo(::CSocket* socket) = 0;

    int IsAllow(int id);
    void AllowAll();
    void DenyAll();
    void Allow(int id);
    void Deny(int id);
    void RuleParsing(char16_t* command);


protected:
    std::int32_t m_bAllowAllOrDenyAll;
    std::int32_t padding_0c;
    EventMatchRuleIdSet m_RuleSet;
};

class CEventMatchItemRuleSet final : public CEventMatchRuleSet {
public:
    ~CEventMatchItemRuleSet() override = default;
    void ShowInfo(::CSocket* socket) override;
};

class CEventMatchSkillRuleSet final : public CEventMatchRuleSet {
public:
    ~CEventMatchSkillRuleSet() override = default;
    void ShowInfo(::CSocket* socket) override;
};

class CEventMatch;
class CEventMatchMgr;

// Exact current User-embedded scalar helper at User+0x5780. The enclosing
// User constructor writes all four logical fields explicitly. This POD does
// not own or keep either match.
class CEventMatchUser final {
public:
    CSPointer<CEventMatch> GetEventMatch();
    CSPointer<CEventMatch> GetManagingEventMatch();
    void Pause(User* user, bool paused);

    std::uint32_t m_nEventMatchId;
    std::uint8_t m_byEventMatchTeamId;
    bool m_bIsPause;
    std::uint16_t padding_06;
    std::uint32_t m_ManagingMatchId;
};

// Exact current L2Server event-match envelope. The observer map owns only
// SID/FVector nodes. Teams likewise store scalar identities only. User and
// Party lifetimes stay outside this object.
class CEventMatch : public CStaticObject {
public:
    enum eEventMatchRule : std::int32_t {
        EMRule_MIN = 0,
        EMRule_DEATH_MATCH = 1,
        EMRule_MAX = 2,
    };

    CEventMatch(
        eEventMatchRule rule,
        FVector position,
        int advertisedFenceWidth,
        int advertisedFenceHeight,
        std::u16string_view firstTeamName,
        std::u16string_view secondTeamName,
        User* manager
    );
    ~CEventMatch() override;

    L2_DECLARE_OBJECT_REGISTRATION(CEventMatch)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    bool IsEventMatch() override;
    CEventMatch* CastEventMatch() override;

    EventMatchStatus GetStatus() const { return m_eStatus; }

    void Appear();
    void Disappear();
    bool SetTeamLeader(int team, std::uint32_t leaderSid);
    bool ResetTeamLeader(int team);

    // Target wchar_t is UTF-16. char16_t is the portable spelling which
    // keeps both target code-unit width and the on-object carrier.
    const char16_t* GetTeamName(int team) const;

    void ObserverHide(User& user, EventMatchObserverType observerType, FVector position);
    void SetObserver(User& user, EventMatchObserverType observerType);

    void SendEmptyTeamInfo(int team, ::CSocket* socket);
    void SendEventMatchInfo(CUserSocket* socket);

    User* GetManager();
    void LeaveManager();

    void Broadcast2Observer(const char* format, ...);
    void Broadcast(const char* format, ...);

    void UpdateUserStatus(User* user, int updateType);
    void SendTeamInfo(int team, User* user, ::CSocket* socket, int broadcastObservers, int updateRoster);
    void SendTeamInfo(int team, CParty* party, ::CSocket* socket, int broadcastObservers, int updateRoster);
    void SendSpelledInfo(User* user, ::CSocket* socket, int broadcastObservers);
    void SendSpelledInfo(CParty* party, ::CSocket* socket, int broadcastObservers);

    void GiveEventMatchId2Team(int team);
    void ClearTeam(int team);
    bool TeamLock(::CSocket* socket, int team);
    bool TeamUnlock(::CSocket* socket, int team);
    void ChangeRelation(int enabled);
    void PauseAllUser(int paused);

    bool MatchStart();
    bool MatchPause();
    bool MatchEnd();

    void ItemRuleParsing(char16_t* command);
    void SkillRuleParsing(char16_t* command);
    void ShowInfo(::CSocket* socket);
    int IsLocked(std::uint32_t userSid, std::uint32_t targetMemberId);
    void SetFenceStatus(ColosseumFenceState state);
    void AddUser(User* user, std::uint8_t team);
    void LeaveUser(User* oldUser, User* replacementUser, std::uint8_t team);
    void DispelAll();
    void UseSkill(User* caster, const std::vector<CSkillInfo*>& skills);
    void Manage(User* manager);


protected:
    ::CYieldLock m_csLock;
    EventMatchStatus m_eStatus;
    eEventMatchRule m_eRule;
    EventMatchTeam m_Team[2];
    EventMatchObserverMap m_ObserverMap;
    EventMatchFenceInfo m_FenceInfo;
    std::int32_t padding_5cc;
    CEventMatchItemRuleSet m_ItemRule;
    CEventMatchSkillRuleSet m_SkillRule;
    std::uint32_t m_ManagerSID;
    std::int32_t m_nPublicId;

private:
    friend class CEventMatchMgr;
    friend class User;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CEventMatch)
};

using EventMatchRegistryMap = ::std::map<std::uint32_t, CEventMatch*>;

// Current singleton owner: one deleting-destructor slot, a 0x10 lock and a
// 0x20 raw-pointer registry. Successful insertion owns the object's initial
// CreateDelete reference. Map node destruction itself never releases it.
class CEventMatchMgr : public Singleton<CEventMatchMgr> {
public:
    CEventMatchMgr();
    ~CEventMatchMgr() override;

    CEventMatchMgr(const CEventMatchMgr&) = delete;
    CEventMatchMgr& operator=(const CEventMatchMgr&) = delete;

    CEventMatch* CreateEventMatch(
        User* manager,
        int rule,
        FVector position,
        int advertisedFenceWidth,
        int advertisedFenceHeight,
        std::u16string_view firstTeamName,
        std::u16string_view secondTeamName
    );
    bool DeleteEventMatch(std::uint32_t publicId);
    CSPointer<CEventMatch> FindEventMatch(std::uint32_t publicId);
    CEventMatch* GetLastEventMatch();
    void ShowList(::CSocket* socket);


private:
    ::CYieldLock m_csLock;
    EventMatchRegistryMap m_EventMatchMap;
};

class CEventMatchBuilderCommandParser {
public:
    using ParserFunction = int (*)(::CSocket*, User*, char16_t*);

    struct stEventMatchCommandMap final {
        const char16_t* pszCommandString;
        ParserFunction func;
    };

    static int Parse(::CSocket* socket, User* user, char16_t* command);

private:
    static int Parse_create(::CSocket*, User*, char16_t*);
    static int Parse_leader(::CSocket*, User*, char16_t*);
    static int Parse_item_rule(::CSocket*, User*, char16_t*);
    static int Parse_skill_rule(::CSocket*, User*, char16_t*);
    static int Parse_lock(::CSocket*, User*, char16_t*);
    static int Parse_unlock(::CSocket*, User*, char16_t*);
    static int Parse_start(::CSocket*, User*, char16_t*);
    static int Parse_pause(::CSocket*, User*, char16_t*);
    static int Parse_end(::CSocket*, User*, char16_t*);
    static int Parse_info(::CSocket*, User*, char16_t*);
    static int Parse_list(::CSocket*, User*, char16_t*);
    static int Parse_score(::CSocket*, User*, char16_t*);
    static int Parse_remove(::CSocket*, User*, char16_t*);
    static int Parse_msg(::CSocket*, User*, char16_t*);
    static int Parse_fence(::CSocket*, User*, char16_t*);
    static int Parse_firecracker(::CSocket*, User*, char16_t*);
    static int Parse_gm(::CSocket*, User*, char16_t*);
    static int Parse_dispelall(::CSocket*, User*, char16_t*);
    static int Parse_useskill(::CSocket*, User*, char16_t*);
    static int Parse_manage(::CSocket*, User*, char16_t*);

    static stEventMatchCommandMap m_CommandMap[20];
};

class CEventMatchOpBuilderCommandParser {
public:
    using ParserFunction = int (*)(::CSocket*, User*, char16_t*);

    struct stEventMatchCommandMap final {
        const char16_t* pszCommandString;
        ParserFunction func;
    };

    static int Parse(::CSocket* socket, User* user, char16_t* command);

private:
    static int Parse_list(::CSocket*, User*, char16_t*);
    static int Parse_observer(::CSocket*, User*, char16_t*);

    static stEventMatchCommandMap m_CommandMap[2];
};

class CEventMatchUserCommandHandler {
public:
    static void AddObserverByNPC(User* user, int matchId);
    static void RemoveObserver(int matchId, User* user);
};

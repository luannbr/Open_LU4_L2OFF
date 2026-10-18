#include "EventMatch.h"
#include "CWorld.h"
#include "PVPMatch.h"
#include "Party.h"
#include "Pet.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Skill.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Config.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Socket.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace {

constexpr std::int32_t kFenceBroadcastRadius = 0x80000;
constexpr std::size_t kRuleInfoCapacity = 0x400;
char16_t kSystemContext[] = u"SYS";

std::atomic<std::int32_t> g_nextEventMatchPublicId{};

std::uint32_t UserSid(User& user) { return reinterpret_cast<CObject*>(&user)->GetSmartID().Sid(); }

FVector UserPosition(User& user) { return reinterpret_cast<CWorldObject*>(&user)->GetPos(); }

User* ResolveUser(std::uint32_t sid) {
    CObject* const object = reinterpret_cast<CObject*>(g_CreatureSmartIdManager.GetObjectW(sid));
    if (!object || !object->IsPC()) {
        return nullptr;
    }
    return object->CastUser();
}

CSPointer<User> FindUser(std::uint32_t sid) { return g_UserMap.FindObject(static_cast<int>(sid)); }

bool ValidTeam(int team) noexcept { return team == 1 || team == 2; }

EventMatchTeam& TeamAt(EventMatchTeam* teams, int team) noexcept { return teams[team - 1]; }

const EventMatchTeam& TeamAt(const EventMatchTeam* teams, int team) noexcept { return teams[team - 1]; }

bool IsSignedDecimalStart(char16_t value) noexcept { return (value >= L'0' && value <= L'9') || value == L'-'; }

void SendRuleMessage(::CSocket* socket, const char16_t* message) {
    static_cast<CUserSocket*>(socket)->SendSystemMessage(kSystemContext, message);
}

template <std::size_t Capacity, class... Args>
void FormatCurrent(std::array<char16_t, Capacity>& destination, const std::u16string_view format, const Args&... arguments) {
    FormatUtf16To(destination.data(), destination.size(), format, arguments...);
}

} // namespace

CSPointer<CEventMatch> CEventMatchUser::GetEventMatch() {
    L2_GUARD_FUNCTION();
    CSPointer<CEventMatch> result = Singleton<CEventMatchMgr>::inst().FindEventMatch(m_nEventMatchId);
    if (!result) {
        m_nEventMatchId = 0;
        m_byEventMatchTeamId = 0;
    }
    return result;
}

CSPointer<CEventMatch> CEventMatchUser::GetManagingEventMatch() {
    L2_GUARD_FUNCTION();
    CSPointer<CEventMatch> result = Singleton<CEventMatchMgr>::inst().FindEventMatch(m_ManagingMatchId);
    if (!result) {
        m_ManagingMatchId = 0;
    }
    return result;
}

void CEventMatchUser::Pause(User* user, bool paused) {
    L2_GUARD_FUNCTION();
    if (!m_bIsPause) {
        user->StopMove();
        {
            FOR_ALL_SUMMON_IN_SUMMON_LIST(user, summon, summon->StopMove(););
        }
    }
    if (m_bIsPause != paused) {
        m_bIsPause = paused;
        if (!paused) {
            user->DoNothing();
            {
                FOR_ALL_SUMMON_IN_SUMMON_LIST(user, summon, summon->DoNothing(););
            }
        }
    }
}

CEventMatchRuleSet::CEventMatchRuleSet() : m_bAllowAllOrDenyAll(1), padding_0c(0) { L2_GUARD_FUNCTION(); }

CEventMatchRuleSet::~CEventMatchRuleSet() = default;

int CEventMatchRuleSet::IsAllow(int id) {
    L2_GUARD_FUNCTION();
    const bool present = m_RuleSet.find(id) != m_RuleSet.end();
    return m_bAllowAllOrDenyAll == 0 ? static_cast<int>(present) : static_cast<int>(!present);
}

void CEventMatchRuleSet::AllowAll() {
    L2_GUARD_FUNCTION();
    m_RuleSet.clear();
    m_bAllowAllOrDenyAll = 1;
}

void CEventMatchRuleSet::DenyAll() {
    L2_GUARD_FUNCTION();
    m_RuleSet.clear();
    m_bAllowAllOrDenyAll = 0;
}

void CEventMatchRuleSet::Allow(int id) {
    L2_GUARD_FUNCTION();
    if (m_bAllowAllOrDenyAll == 0) {
        m_RuleSet.insert(id);
    } else {
        m_RuleSet.erase(id);
    }
}

void CEventMatchRuleSet::Deny(int id) {
    L2_GUARD_FUNCTION();
    if (m_bAllowAllOrDenyAll == 0) {
        m_RuleSet.erase(id);
    } else {
        m_RuleSet.insert(id);
    }
}

void CEventMatchRuleSet::RuleParsing(char16_t* command) {
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    while ((command = ::GetLeftString2(command, &token)), token) {
        if (std::u16string_view(token).substr(0, 9) == u"allow_all") {
            AllowAll();
            continue;
        }
        if (std::u16string_view(token).substr(0, 8) == u"deny_all") {
            DenyAll();
            continue;
        }
        if (token[0] == L'A' && IsSignedDecimalStart(token[1])) {
            Allow(::WindowsWtol(std::u16string_view(token + 1)));
            continue;
        }
        if (token[0] == L'D' && IsSignedDecimalStart(token[1])) {
            Deny(::WindowsWtol(std::u16string_view(token + 1)));
            continue;
        }
        break;
    }
}

void CEventMatchItemRuleSet::ShowInfo(::CSocket* socket) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, kRuleInfoCapacity> message{};
    const wchar_t* const policy = m_bAllowAllOrDenyAll != 0 ? L"AllowAll" : L"DenyAll";
    FormatUtf16To(message.data(), message.size(), u"ItemRule: %ls", policy);
    SendRuleMessage(socket, message.data());

    const wchar_t* const operation = m_bAllowAllOrDenyAll != 0 ? L"Deny" : L"Allow";
    for (const int id : m_RuleSet) {
        FormatUtf16To(message.data(), message.size(), u"ItemRule: %ls(%d)", operation, id);
        SendRuleMessage(socket, message.data());
    }
}

void CEventMatchSkillRuleSet::ShowInfo(::CSocket* socket) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, kRuleInfoCapacity> message{};
    const wchar_t* const policy = m_bAllowAllOrDenyAll != 0 ? L"AllowAll" : L"DenyAll";
    // Current 0071d80c formats this header and then overwrites the buffer
    // without sending it. Preserve that observable omission.
    FormatUtf16To(message.data(), message.size(), u"SkillRule: %ls", policy);

    const wchar_t* const operation = m_bAllowAllOrDenyAll != 0 ? L"Deny" : L"Allow";
    for (const int id : m_RuleSet) {
        FormatUtf16To(message.data(), message.size(), u"SkillRule: %ls(%d)", operation, id);
        SendRuleMessage(socket, message.data());
    }
}

CEventMatch::CEventMatch(
    eEventMatchRule rule,
    FVector position,
    int advertisedFenceWidth,
    int advertisedFenceHeight,
    std::u16string_view firstTeamName,
    std::u16string_view secondTeamName,
    User* manager
) :
    m_eStatus(EMStatus_MIN),
    m_eRule(EMRule_MIN),
    m_Team{},
    m_FenceInfo{COLOSSEUM_FENCE_NONE, 0, 0},
    padding_5cc(0),
    m_ItemRule(),
    m_SkillRule(),
    m_ManagerSID(0),
    m_nPublicId(0) {
    L2_GUARD_FUNCTION();
    SetPos(position);
    m_eStatus = EMStatus_READY;
    m_eRule = rule;
    m_Team[0].name.assign(firstTeamName);
    m_Team[1].name.assign(secondTeamName);
    for (EventMatchTeam& team : m_Team) {
        team.leaderSid = 0;
        team.locked = 0;
    }
    m_FenceInfo.height = advertisedFenceWidth;
    m_FenceInfo.width = advertisedFenceHeight;
    m_FenceInfo.state = COLOSSEUM_FENCE_COLUMN;
    m_nPublicId = g_nextEventMatchPublicId.fetch_add(1, std::memory_order_seq_cst) + 1;
    m_ManagerSID = UserSid(*manager);
    manager->m_EventMatchUser.m_ManagingMatchId = m_nPublicId;
    CStaticObject::OnCreate();
}

CEventMatch::~CEventMatch() {
    L2_GUARD_FUNCTION();
    if (m_eStatus == EMStatus_PAUSE) {
        PauseAllUser(0);
    }

    {
        L2_GUARD(m_csLock);
        for (const auto& observer : m_ObserverMap) {
            User* const user = ResolveUser(observer.first);
            if (user) {
                ObserverHide(*user, EMOT_NOT_OBSERVER, observer.second);
            }
        }
        m_ObserverMap.clear();
    }

    ClearTeam(1);
    ClearTeam(2);
    if (User* const manager = GetManager()) {
        manager->m_EventMatchUser.m_ManagingMatchId = 0;
    }
    g_ServerContext.EventMatch.fetch_sub(1, std::memory_order_relaxed);
}

L2_DEFINE_OBJECT_CLASS_ID(CEventMatch)

L2_DEFINE_OBJECT_CLASS_NAME(CEventMatch, m_pClassName_CEventMatch)

L2_DEFINE_OBJECT_ALLOCATOR(CEventMatch, __GetClassName())

L2_DEFINE_OBJECT_NEW(CEventMatch)

L2_DEFINE_OBJECT_TYPED_RELEASE(CEventMatch)

bool CEventMatch::IsEventMatch() { return true; }

CEventMatch* CEventMatch::CastEventMatch() { return this; }

void CEventMatch::Appear() {
    L2_GUARD_FUNCTION();
    CWorld::Inst().StaticObjectAppear(this, GetPos());
}

void CEventMatch::Disappear() {
    L2_GUARD_FUNCTION();
    CWorld::Inst().StaticObjectDisappear(this);
}

bool CEventMatch::SetTeamLeader(int team, std::uint32_t leaderSid) {
    L2_GUARD_FUNCTION();
    if (ValidTeam(team)) {
        L2_GUARD(m_csLock);
        TeamAt(m_Team, team).leaderSid = leaderSid;
    }
    return ValidTeam(team);
}

bool CEventMatch::ResetTeamLeader(int team) {
    L2_GUARD_FUNCTION();
    if (ValidTeam(team)) {
        L2_GUARD(m_csLock);
        TeamAt(m_Team, team).leaderSid = 0;
    }
    return ValidTeam(team);
}

const char16_t* CEventMatch::GetTeamName(int team) const {
    L2_GUARD_FUNCTION();
    return ValidTeam(team) ? TeamAt(m_Team, team).name.c_str() : nullptr;
}

void CEventMatch::ObserverHide(User& user, EventMatchObserverType observerType, FVector position) {
    L2_GUARD_FUNCTION();
    if (observerType == EMOT_NOT_OBSERVER) {
        if (!user.m_data->m_bGMInvisible) {
            if (user.IsBuilder()) {
                user.SendSystemMessage(kSystemContext, u"Currently, you can be seen.");
            }
        } else {
            {
                L2_GUARD(*user.m_lock);
                user.m_data->m_bGMInvisible = false;
                user.m_bCameraMode = false;
            }
            user.DoNothing();
            user.TeleportToLocation(
                runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                runtime::Win64CvttDoubleToInt32(position.z), 0, 0x10000, TeleportSourceType::ObserverHide1, nullptr, 0
            );
            if (user.IsBuilder()) {
                const std::wstring accountName = Utf16ToWide(std::u16string_view(user.m_sAccountName.data()));
                // Log.Add(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,,,,,,,,,,,%s,%s,,,",
                //     std::int64_t{0x21c},
                //     static_cast<std::int64_t>(user.GetDBID()),
                //     static_cast<std::int64_t>(user.GetAccountID()),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.x)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.y)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.z)),
                //     user.GetName(),
                //     accountName.c_str())
                Log.Add(
                    LOG_BOTH, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,,,,,,,,,,,%s,%s,,,", std::int64_t{0x21c},
                    static_cast<std::int64_t>(user.GetDBID()), static_cast<std::int64_t>(user.GetAccountID()),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)), user.GetName(),
                    accountName.c_str()
                );
                user.SendSystemMessage(kSystemContext, u"Now, you can be seen.");
            }
        }
        user.GetSocket()->Send("chdc", 0xfe, 0x0e, m_nPublicId, 0);
        return;
    }

    if (observerType != EMOT_BUILDER && observerType != EMOT_PLAYER) {
        return;
    }

    if (!user.m_data->m_bGMInvisible) {
        {
            L2_GUARD(*user.m_lock);
            user.m_data->m_bGMInvisible = true;
            user.m_bCameraMode = true;
        }
        user.DoNothing();
        user.TeleportToLocation(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z), 0, 0x10000, TeleportSourceType::ObserverHide2, nullptr, 0
        );
        if (observerType == EMOT_BUILDER) {
            user.SendSystemMessage(kSystemContext, u"Now, you cannot be seen.");
        }
    } else {
        user.DoNothing();
        user.TeleportToLocation(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z), 0, 0x10000, TeleportSourceType::ObserverHide3, nullptr, 0
        );
        if (observerType == EMOT_BUILDER) {
            user.SendSystemMessage(kSystemContext, u"Currently, you cannot be seen.");
        }
    }

    user.GetSocket()->Send(
        "chdccSS", 0xfe, 0x0e, m_nPublicId, static_cast<int>(observerType), static_cast<int>(m_eRule),
        m_Team[0].name.c_str(), m_Team[1].name.c_str()
    );
}

void CEventMatch::SetObserver(User& user, EventMatchObserverType observerType) {
    L2_GUARD_FUNCTION();
    if (observerType == EMOT_BUILDER && !user.IsBuilder()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User(%s) is not builder!!",
        //     kEventMatchLockFile,
        //     0x440,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User(%s) is not builder!!",
        //     kEventMatchLockFile,
        //     0x440,
        //     user.GetName())
        L2_LOG_WARN(
            Log, "Event match %d: character %u cannot enter builder observer mode without builder privileges.", m_nPublicId,
            user.GetDBID()
        );
        return;
    }

    const std::uint32_t sid = UserSid(user);
    if (observerType == EMOT_NOT_OBSERVER) {
        FVector savedPosition{};
        bool found = false;
        {
            L2_GUARD(m_csLock);
            const auto position = m_ObserverMap.find(sid);
            if (position != m_ObserverMap.end()) {
                savedPosition = position->second;
                m_ObserverMap.erase(position);
                found = true;
            }
        }
        if (!found) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Not-Observer(%s) try to cancel observing!!",
            //     kEventMatchLockFile,
            //     0x454,
            //     user.GetName())
            // Noise: A repeated or stale stop-observing request has no observer entry and already returns without changing
            // state. L2_LOG_ERROR(Log,
            //     L"[%s][%d] Not-Observer(%s) try to cancel observing!!",
            //     kEventMatchLockFile,
            //     0x454,
            //     user.GetName())
            return;
        }
        ObserverHide(user, observerType, savedPosition);
        return;
    }

    if (observerType != EMOT_BUILDER && observerType != EMOT_PLAYER) {
        return;
    }

    const FVector eventMatchPosition = GetPos();
    bool inserted = false;
    {
        L2_GUARD(m_csLock);
        inserted = m_ObserverMap.emplace(sid, UserPosition(user)).second;
    }
    if (!inserted) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Observer(%s) try to observe!!",
        //     kEventMatchLockFile,
        //     0x481,
        //     user.GetName())
        // Noise: The character is already registered as an observer. This repeated request returns without changing the
        // saved position. L2_LOG_ERROR(Log,
        //     L"[%s][%d] Observer(%s) try to observe!!",
        //     kEventMatchLockFile,
        //     0x481,
        //     user.GetName())
        return;
    }

    ObserverHide(user, observerType, eventMatchPosition);
    for (int team = 1; team <= 2; ++team) {
        CSPointer<User> leader = FindUser(TeamAt(m_Team, team).leaderSid);
        if (!leader) {
            continue;
        }
        CParty* const party = leader->GetParty();
        if (party) {
            SendTeamInfo(team, party, user.GetSocket(), 0, 0);
            SendSpelledInfo(party, user.GetSocket(), 0);
        } else {
            SendTeamInfo(team, leader.get(), user.GetSocket(), 0, 0);
            SendSpelledInfo(leader.get(), user.GetSocket(), 0);
        }
    }
}

void CEventMatch::SendEmptyTeamInfo(int team, ::CSocket* socket) {
    L2_GUARD_FUNCTION();
    if (socket) {
        socket->Send("chdcd", 0xfe, 0x1c, m_nPublicId, team, 0);
    }
}

void CEventMatch::SendEventMatchInfo(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    ColosseumFenceState state;
    std::int32_t width;
    std::int32_t height;
    FVector position;
    {
        L2_GUARD(m_csLock);
        state = m_FenceInfo.state;
        width = m_FenceInfo.width;
        height = m_FenceInfo.height;
        position = GetPos();
    }

    reinterpret_cast<::CSocket*>(socket)->Send(
        "chddddddd", 0xfe, 3, static_cast<std::int32_t>(m_nId), static_cast<std::int32_t>(state),
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), width, height
    );
}

User* CEventMatch::GetManager() {
    L2_GUARD_FUNCTION();
    return ResolveUser(m_ManagerSID);
}

void CEventMatch::LeaveManager() {
    L2_GUARD_FUNCTION();
    User* const manager = GetManager();
    m_ManagerSID = 0;
    if (manager) {
        manager->m_EventMatchUser.m_ManagingMatchId = 0;
    }
}

void CEventMatch::Broadcast2Observer(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    L2_GUARD(m_csLock);
    for (const auto& observer : m_ObserverMap) {
        User* const user = ResolveUser(observer.first);
        if (user) {
            user->GetSocket()->SendV(format, arguments);
        }
    }
    va_end(arguments);
}

void CEventMatch::Broadcast(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    for (const EventMatchTeam& team : m_Team) {
        CSPointer<User> leader = FindUser(team.leaderSid);
        if (!leader) {
            continue;
        }
        CParty* const party = leader->GetParty();
        if (party) {
            party->BroadcastToAllPartyV(format, arguments);
        } else {
            leader->GetSocket()->SendV(format, arguments);
        }
    }
    {
        L2_GUARD(m_csLock);
        for (const auto& observer : m_ObserverMap) {
            User* const user = ResolveUser(observer.first);
            if (user) {
                user->GetSocket()->SendV(format, arguments);
            }
        }
    }
    va_end(arguments);
}

void CEventMatch::UpdateUserStatus(User* user, int updateType) {
    L2_GUARD_FUNCTION();
    const std::uint32_t sid = user->Id();
    const std::u16string name = user->GetName();
    const int hp = user->GetHPToShow();
    const CSharedCreatureData* const data = user->m_data;
    const int baseHp = static_cast<int>(data->m_dBaseHP);
    const int mp = static_cast<int>(data->m_dMP);
    const int baseMp = static_cast<int>(data->m_dBaseMP);
    const int cp = static_cast<int>(data->m_dCP);
    const int baseCp = static_cast<int>(data->m_dBaseCP);
    const int level = data->m_nLevel;
    const int classId = data->m_nClass;

    Broadcast2Observer("chdSdddddddd", 0xfe, 2, sid, name.c_str(), hp, baseHp, mp, baseMp, cp, baseCp, level, classId);
    if (updateType == 1) {
        Broadcast2Observer("cddcccdcdcdcd", 0x18, sid, 0, 4, 4, 9, hp, 10, baseHp, 0x21, cp, 0x22, baseCp);
    } else if (updateType == 2) {
        Broadcast2Observer("cddcccdcd", 0x18, sid, 0, 4, 2, 0x0b, mp, 0x0c, baseMp, baseCp, level, classId);
    }
}

void CEventMatch::SendTeamInfo(int team, User* user, ::CSocket* socket, int broadcastObservers, int updateRoster) {
    L2_GUARD_FUNCTION();
    EventMatchTeam& state = TeamAt(m_Team, team);
    std::u16string name;
    std::array<char, 0x5000> buffer;
    const auto [hp, baseHp, mp, baseMp, cp, baseCp, level, classId, sex, race] = [&] {
        L2_GUARD(*user->m_lock);
        name = user->GetName();
        const CSharedCreatureData* const data = user->m_data;
        const int hp = static_cast<int>(data->m_dHP);
        const int baseHp = static_cast<int>(data->m_dBaseHP);
        const int mp = static_cast<int>(data->m_dMP);
        const int baseMp = static_cast<int>(data->m_dBaseMP);
        const int cp = static_cast<int>(data->m_dCP);
        const int baseCp = static_cast<int>(data->m_dBaseCP);
        const int level = data->m_nLevel;
        const int classId = data->m_nClass;
        const int sex = data->m_Sex;
        const int race = data->m_Race;
        if (updateRoster != 0) {
            state.memberIds[0] = data->m_nDBID;
        }
        return std::array{hp, baseHp, mp, baseMp, cp, baseCp, level, classId, sex, race};
    }();

    const int length = ::Assemble(
        buffer.data(), static_cast<int>(buffer.size()), "dSdddddddddd", user->Id(), name.c_str(), hp, baseHp, mp, baseMp, cp,
        baseCp, level, classId, sex, race
    );
    if (updateRoster != 0) {
        state.memberCount = length != 0;
    }
    if (length == 0) {
        return;
    }
    if (broadcastObservers != 0) {
        Broadcast2Observer("chdcdb", 0xfe, 0x1c, m_nPublicId, team, 1, length, buffer.data());
    }
    if (socket) {
        socket->Send("chdcdb", 0xfe, 0x1c, m_nPublicId, team, 1, length, buffer.data());
    }
}

void CEventMatch::SendTeamInfo(int team, CParty* party, ::CSocket* socket, int broadcastObservers, int updateRoster) {
    L2_GUARD_FUNCTION();
    EventMatchTeam& state = TeamAt(m_Team, team);
    std::array<char, 0x5000> buffer;
    std::vector<User*> members;
    party->GetAllMember(members);
    int length = 0;
    int count = 0;
    for (std::size_t index = 0; index < members.size(); ++index) {
        User* const user = runtime::CheckedVectorAt(members, index);
        std::u16string name;
        const auto [hp, baseHp, mp, baseMp, cp, baseCp, level, classId, sex, race] = [&] {
            L2_GUARD(*user->m_lock);
            name = user->GetName();
            const CSharedCreatureData* const data = user->m_data;
            const int hp = static_cast<int>(data->m_dHP);
            const int baseHp = static_cast<int>(data->m_dBaseHP);
            const int mp = static_cast<int>(data->m_dMP);
            const int baseMp = static_cast<int>(data->m_dBaseMP);
            const int cp = static_cast<int>(data->m_dCP);
            const int baseCp = static_cast<int>(data->m_dBaseCP);
            const int level = data->m_nLevel;
            const int classId = data->m_nClass;
            const int sex = data->m_Sex;
            const int race = data->m_Race;
            if (updateRoster != 0) {
                state.memberIds[index] = data->m_nDBID;
            }
            return std::array{hp, baseHp, mp, baseMp, cp, baseCp, level, classId, sex, race};
        }();

        const int assembled = ::Assemble(
            buffer.data() + length, static_cast<int>(buffer.size()) - length, "dSdddddddddd", user->Id(), name.c_str(), hp,
            baseHp, mp, baseMp, cp, baseCp, level, classId, sex, race
        );
        if (assembled > 0) {
            length += assembled;
            ++count;
        }
    }
    if (updateRoster != 0) {
        state.memberCount = count;
    }
    if (count <= 0) {
        return;
    }
    if (broadcastObservers != 0) {
        Broadcast2Observer("chdcdb", 0xfe, 0x1c, m_nPublicId, team, count, length, buffer.data());
    }
    if (socket) {
        socket->Send("chdcdb", 0xfe, 0x1c, m_nPublicId, team, count, length, buffer.data());
    }
}

void CEventMatch::SendSpelledInfo(User* user, ::CSocket* socket, int broadcastObservers) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x1000> buffer;
    int length = static_cast<int>(buffer.size());
    const int count = user->PrepareSpelledInfoChangedBuffer(buffer.data(), length);
    if (socket) {
        socket->Send("chdddb", 0xfe, 4, m_nPublicId, user->Id(), count, length, buffer.data());
    }
    if (broadcastObservers != 0) {
        Broadcast2Observer("chdddb", 0xfe, 4, m_nPublicId, user->Id(), count, length, buffer.data());
    }
}

void CEventMatch::SendSpelledInfo(CParty* party, ::CSocket* socket, int broadcastObservers) {
    L2_GUARD_FUNCTION();
    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* const user : members) {
        SendSpelledInfo(user, socket, broadcastObservers);
    }
}

void CEventMatch::GiveEventMatchId2Team(int team) {
    L2_GUARD_FUNCTION();
    CSPointer<User> leader = FindUser(TeamAt(m_Team, team).leaderSid);
    if (leader) {
        CParty* const party = leader->GetParty();
        if (party) {
            std::vector<User*> members;
            party->GetAllMember(members);
            for (User* const user : members) {
                user->m_EventMatchUser.m_nEventMatchId = m_nPublicId;
                user->m_EventMatchUser.m_byEventMatchTeamId = static_cast<std::uint8_t>(team);
            }
        } else {
            leader->m_EventMatchUser.m_nEventMatchId = m_nPublicId;
            leader->m_EventMatchUser.m_byEventMatchTeamId = static_cast<std::uint8_t>(team);
        }
    }
}

void CEventMatch::ClearTeam(int team) {
    L2_GUARD_FUNCTION();
    CSPointer<User> leader = FindUser(TeamAt(m_Team, team).leaderSid);
    if (leader) {
        CParty* const party = leader->GetParty();
        if (party) {
            std::vector<User*> members;
            party->GetAllMember(members);
            for (User* const user : members) {
                user->m_EventMatchUser.m_nEventMatchId = 0;
                user->m_EventMatchUser.m_byEventMatchTeamId = 0;
                user->SetRelationChanged(false);
            }
        } else {
            leader->m_EventMatchUser.m_nEventMatchId = 0;
            leader->m_EventMatchUser.m_byEventMatchTeamId = 0;
            leader->SetRelationChanged(false);
        }
    }
}

bool CEventMatch::TeamLock(::CSocket* socket, int team) {
    L2_GUARD_FUNCTION();
    if (!ValidTeam(team)) {
        SendEmptyTeamInfo(team, socket);
        return false;
    }

    {
        L2_GUARD(m_csLock);
        TeamAt(m_Team, team).locked = 1;
    }

    EventMatchTeam& teamState = TeamAt(m_Team, team);
    CSPointer<User> leader = FindUser(teamState.leaderSid);
    if (!leader) {
        SendEmptyTeamInfo(team, socket);
        return true;
    }

    GiveEventMatchId2Team(team);
    CParty* const party = leader->GetParty();
    if (party) {
        SendTeamInfo(team, party, socket, 1, 1);
        SendSpelledInfo(party, nullptr, 1);
    } else {
        SendTeamInfo(team, leader.get(), socket, 1, 1);
        SendSpelledInfo(leader.get(), nullptr, 1);
    }
    return true;
}

bool CEventMatch::TeamUnlock(::CSocket* socket, int team) {
    L2_GUARD_FUNCTION();
    if (!ValidTeam(team)) {
        return false;
    }

    {
        L2_GUARD(m_csLock);
        TeamAt(m_Team, team).locked = 0;
        ClearTeam(team);
    }
    Broadcast2Observer("chdc", 0xfe, 6, m_nPublicId, team);
    SendEmptyTeamInfo(team, socket);
    return true;
}

void CEventMatch::ChangeRelation(int enabled) {
    L2_GUARD_FUNCTION();
    for (int teamIndex = 0; teamIndex < 2; ++teamIndex) {
        CSPointer<User> leader = FindUser(m_Team[teamIndex].leaderSid);
        if (!leader) {
            continue;
        }
        std::vector<User*> users;
        if (CParty* const party = leader->GetParty()) {
            party->GetAllMember(users);
        } else {
            users.push_back(leader.get());
        }
        for (User* const user : users) {
            user->m_EventMatchUser.m_byEventMatchTeamId = enabled != 0 ? static_cast<std::uint8_t>(teamIndex + 1) : 0;
            user->ParamChanged(std::int64_t{PCB_CHAR_EVENTMATCH});
            user->SetRelationChanged(false);
            if (CPet* const pet = user->GetPet()) {
                static_cast<CCreature*>(pet)->ParamChanged();
            }
            {
                FOR_ALL_SUMMON_IN_SUMMON_LIST(user, summon, static_cast<CCreature*>(summon)->ParamChanged(););
            }
        }
    }
}

void CEventMatch::PauseAllUser(int paused) {
    L2_GUARD_FUNCTION();
    for (const EventMatchTeam& team : m_Team) {
        CSPointer<User> leader = FindUser(team.leaderSid);
        if (!leader) {
            continue;
        }
        if (CParty* const party = leader->GetParty()) {
            std::vector<User*> members;
            party->GetAllMember(members);
            for (User* const user : members) {
                user->m_EventMatchUser.Pause(user, paused != 0);
            }
        } else {
            leader->m_EventMatchUser.Pause(leader.get(), paused != 0);
        }
    }
}

bool CEventMatch::MatchStart() {
    L2_GUARD_FUNCTION();
    if (m_eStatus == EMStatus_PAUSE) {
        PauseAllUser(0);
    }

    L2_GUARD(m_csLock);
    if ((m_eStatus == EMStatus_READY || m_eStatus == EMStatus_PAUSE) && m_Team[0].locked != 0 && m_Team[1].locked != 0) {
        m_eStatus = EMStatus_PLAYING;
        ChangeRelation(1);
        return true;
    }
    return false;
}

bool CEventMatch::MatchPause() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_csLock);
    if (m_eStatus == EMStatus_PLAYING || m_eStatus == EMStatus_READY) {
        m_eStatus = EMStatus_PAUSE;
        PauseAllUser(1);
        return true;
    }
    return false;
}

bool CEventMatch::MatchEnd() {
    L2_GUARD_FUNCTION();
    if (m_eStatus == EMStatus_PAUSE) {
        PauseAllUser(0);
    }

    L2_GUARD(m_csLock);
    if (m_eStatus == EMStatus_PLAYING || m_eStatus == EMStatus_PAUSE) {
        m_eStatus = EMStatus_READY;
        ChangeRelation(0);
        return true;
    }
    return false;
}

void CEventMatch::ItemRuleParsing(char16_t* command) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_csLock);
    m_ItemRule.RuleParsing(command);
}

void CEventMatch::SkillRuleParsing(char16_t* command) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_csLock);
    m_SkillRule.RuleParsing(command);
}

void CEventMatch::ShowInfo(::CSocket* socket) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, kRuleInfoCapacity> message{};
    SendRuleMessage(socket, u"--- Event Match Info begin ---");

    FormatUtf16To(message.data(), message.size(), u"ID: %d", m_nPublicId);
    SendRuleMessage(socket, message.data());

    if (m_eRule == EMRule_DEATH_MATCH) {
        FormatUtf16To(message.data(), message.size(), u"Rule: DEATH_MATCH(%d)", static_cast<int>(m_eRule));
    } else {
        FormatUtf16To(message.data(), message.size(), u"Rule: %d", static_cast<int>(m_eRule));
    }
    SendRuleMessage(socket, message.data());

    const char16_t* statusFormat = u"Status: %d";
    if (m_eStatus == EMStatus_READY) {
        statusFormat = u"Status: READY(%d)";
    } else if (m_eStatus == EMStatus_PLAYING) {
        statusFormat = u"Status: PLAYING(%d)";
    } else if (m_eStatus == EMStatus_PAUSE) {
        statusFormat = u"Status: PAUSE(%d)";
    }
    FormatUtf16To(message.data(), message.size(), statusFormat, static_cast<int>(m_eStatus));
    SendRuleMessage(socket, message.data());

    const double positionZ = GetPos().z;
    const double positionY = GetPos().y;
    const double positionX = GetPos().x;
    FormatUtf16To(
        message.data(), message.size(), u"Position: (%d, %d, %d)", runtime::Win64CvttDoubleToInt32(positionX),
        runtime::Win64CvttDoubleToInt32(positionY), runtime::Win64CvttDoubleToInt32(positionZ)
    );
    SendRuleMessage(socket, message.data());

    {
        L2_GUARD(m_csLock);
        for (const auto& observer : m_ObserverMap) {
            User* const user = ResolveUser(observer.first);
            if (!user) {
                continue;
            }
            FormatCurrent(message, u"Observer: '%s'", user->GetName());
            SendRuleMessage(socket, message.data());
        }
    }

    for (int team = 1; team <= 2; ++team) {
        L2_GUARD(m_csLock);
        const EventMatchTeam& state = TeamAt(m_Team, team);
        const char16_t* const teamName = state.name.c_str();
        const char16_t* const locked = state.locked != 0 ? u"true" : u"false";
        {
            CSPointer<User> leader;
            if (state.leaderSid != 0) {
                leader = FindUser(state.leaderSid);
            }
            if (leader) {
                FormatCurrent(message, u"Team%d: '%s' leader(%s) lock(%s)", team, teamName, leader->GetName(), locked);
            } else {
                FormatCurrent(message, u"Team%d: '%s' lock(%s)", team, teamName, locked);
            }
        }
        SendRuleMessage(socket, message.data());
    }

    {
        L2_GUARD(m_csLock);
        m_ItemRule.ShowInfo(socket);
    }
    {
        L2_GUARD(m_csLock);
        m_SkillRule.ShowInfo(socket);
    }
    SendRuleMessage(socket, u"--- Event Match Info end ---");
}

int CEventMatch::IsLocked(std::uint32_t userSid, std::uint32_t targetMemberId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_csLock);
    for (EventMatchTeam& team : m_Team) {
        bool belongsToTeam = userSid == team.leaderSid;
        if (!belongsToTeam) {
            CSPointer<User> leader = FindUser(team.leaderSid);
            CParty* const party = leader ? leader->GetParty() : nullptr;
            belongsToTeam = party && party->IsMember(userSid);
        }
        if (!belongsToTeam) {
            continue;
        }

        for (std::int32_t index = 0; index < team.memberCount; ++index) {
            if (team.memberIds[index] == targetMemberId) {
                return 0;
            }
        }
        return team.locked;
    }
    return 0;
}

void CEventMatch::SetFenceStatus(ColosseumFenceState state) {
    L2_GUARD_FUNCTION();
    std::uint32_t routingSid = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::optional<FVector> positionStorage;
    {
        L2_GUARD(m_csLock);
        if (m_FenceInfo.state == state) {
            return;
        }
        routingSid = m_Team[0].leaderSid;
        m_FenceInfo.state = state;
        width = m_FenceInfo.width;
        height = m_FenceInfo.height;
        positionStorage.emplace(GetPos());
    }
    const FVector& position = *positionStorage;

    g_ptlUserMap->BroadcastToNeighbor(
        0, static_cast<int>(routingSid), kFenceBroadcastRadius, position, "chddddddd", 0xfe, 3, static_cast<int>(m_nId),
        static_cast<std::int32_t>(state), runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), width, height
    );
}

void CEventMatch::AddUser(User* user, std::uint8_t team) {
    L2_GUARD_FUNCTION();
    User* const manager = GetManager();
    ::CSocket* const socket = manager ? manager->GetSocket() : nullptr;
    GiveEventMatchId2Team(team);
    CParty* const party = user->GetParty();
    if (party) {
        SendTeamInfo(team, party, socket, 1, 0);
        SendSpelledInfo(party, socket, 1);
    }
}

void CEventMatch::LeaveUser(User* oldUser, User* replacementUser, std::uint8_t team) {
    L2_GUARD_FUNCTION();
    if (!oldUser) {
        return;
    }

    oldUser->m_EventMatchUser.m_nEventMatchId = 0;
    oldUser->m_EventMatchUser.m_byEventMatchTeamId = 0;
    oldUser->SetRelationChanged(false);
    if (!replacementUser || !ValidTeam(team)) {
        return;
    }

    User* const manager = GetManager();
    ::CSocket* const socket = manager ? manager->GetSocket() : nullptr;
    EventMatchTeam& state = TeamAt(m_Team, team);
    if (state.leaderSid == UserSid(*oldUser)) {
        state.leaderSid = UserSid(*replacementUser);
    }
    GiveEventMatchId2Team(team);

    CParty* const party = replacementUser->GetParty();
    if (party) {
        SendTeamInfo(team, party, socket, 1, 0);
        SendSpelledInfo(party, socket, 1);
    } else {
        SendTeamInfo(team, replacementUser, socket, 1, 0);
        SendSpelledInfo(replacementUser, socket, 1);
    }
}

void CEventMatch::DispelAll() {
    L2_GUARD_FUNCTION();
    for (const EventMatchTeam& team : m_Team) {
        CSPointer<User> leader = FindUser(team.leaderSid);
        if (!leader) {
            continue;
        }
        if (CParty* const party = leader->GetParty()) {
            std::vector<User*> members;
            party->GetAllMember(members);
            for (User* const user : members) {
                user->DispelAllByGM(SPET_NONE, false);
            }
        } else {
            leader->DispelAllByGM(SPET_NONE, false);
        }
    }
}

void CEventMatch::UseSkill(User* caster, const std::vector<CSkillInfo*>& skills) {
    L2_GUARD_FUNCTION();
    for (const EventMatchTeam& team : m_Team) {
        CSPointer<User> leader = FindUser(team.leaderSid);
        if (!leader) {
            continue;
        }
        std::vector<User*> targets;
        if (CParty* const party = leader->GetParty()) {
            party->GetAllMember(targets);
        } else {
            targets.push_back(leader.get());
        }
        for (User* const target : targets) {
            for (CSkillInfo* const skill : skills) {
                if (skill) {
                    skill->ActivateSkill(caster, target, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
                }
            }
        }
    }
}

void CEventMatch::Manage(User* manager) {
    L2_GUARD_FUNCTION();
    const std::int32_t publicId = m_nPublicId;
    const std::uint8_t status = static_cast<std::uint8_t>(m_eStatus);
    const std::uint8_t fenceState = static_cast<std::uint8_t>(m_FenceInfo.state);
    const char16_t* const firstTeamName = m_Team[0].name.c_str();
    const std::uint8_t firstTeamLocked = static_cast<std::uint8_t>(m_Team[0].locked);
    const char16_t* const secondTeamName = m_Team[1].name.c_str();
    const std::uint8_t secondTeamLocked = static_cast<std::uint8_t>(m_Team[1].locked);
    std::array<char, 0x5000> buffer;
    int length = 0;
    int count = 0;
    for (int teamIndex = 0; teamIndex < 2; ++teamIndex) {
        const EventMatchTeam& team = m_Team[teamIndex];
        CSPointer<User> leader = FindUser(team.leaderSid);
        if (!leader) {
            continue;
        }
        if (CParty* const party = leader->GetParty()) {
            std::vector<User*> members;
            party->GetAllMember(members);
            for (User* const user : members) {
                const auto [sid, name, classId, level] = [&] {
                    L2_GUARD(*user->m_lock);
                    const std::uint32_t sid = user->Id();
                    const std::u16string name = user->GetName();
                    const int classId = user->m_data->m_nClass;
                    const int level = user->Level();
                    return std::tuple{sid, name, classId, level};
                }();
                length += ::Assemble(
                    buffer.data() + length, static_cast<int>(buffer.size()) - length, "ccdSdd", teamIndex + 1,
                    sid == team.leaderSid, sid, name.c_str(), classId, level
                );
                ++count;
            }
        } else {
            const auto [sid, name, classId, level] = [&] {
                L2_GUARD(*leader->m_lock);
                const std::uint32_t sid = leader->Id();
                const std::u16string name = leader->GetName();
                const int classId = leader->m_data->m_nClass;
                const int level = leader->Level();
                return std::tuple{sid, name, classId, level};
            }();
            length += ::Assemble(
                buffer.data() + length, static_cast<int>(buffer.size()) - length, "ccdSdd", teamIndex + 1,
                sid == team.leaderSid, sid, name.c_str(), classId, level
            );
            ++count;
        }
    }
    m_ManagerSID = UserSid(*manager);
    if (manager->GetSocket()) {
        manager->GetSocket()->Send(
            "chdccScScdb", 0xfe, 0x30, publicId, static_cast<int>(status), static_cast<int>(fenceState), firstTeamName,
            static_cast<int>(firstTeamLocked), secondTeamName, static_cast<int>(secondTeamLocked), count, length,
            buffer.data()
        );
    }
}

CEventMatchMgr::CEventMatchMgr() {}

CEventMatchMgr::~CEventMatchMgr() = default;

CEventMatch* CEventMatchMgr::CreateEventMatch(
    User* manager,
    int rule,
    FVector position,
    int advertisedFenceWidth,
    int advertisedFenceHeight,
    std::u16string_view firstTeamName,
    std::u16string_view secondTeamName
) {
    L2_GUARD_FUNCTION();
    if (!manager || rule != CEventMatch::EMRule_DEATH_MATCH) {
        return nullptr;
    }

    if (firstTeamName.empty() || firstTeamName.size() > 30 || secondTeamName.empty() || secondTeamName.size() > 30) {
        return nullptr;
    }

    const auto validFenceScalar = [](int value) noexcept {
        // Exact current CMP/JA sequence accepts unsigned (value-100) <=
        // 0x1324, hence the machine boundary is inclusive at 5000 even
        // though the builder's diagnostic text says "< 5000".
        return static_cast<std::uint32_t>(value) - 100U <= 0x1324U;
    };
    if (!validFenceScalar(advertisedFenceWidth) || !validFenceScalar(advertisedFenceHeight)) {
        return nullptr;
    }

    CEventMatch* const eventMatch = new (true) CEventMatch(
        static_cast<CEventMatch::eEventMatchRule>(rule), position, advertisedFenceWidth, advertisedFenceHeight,
        firstTeamName, secondTeamName, manager
    );

    bool inserted = false;
    {
        L2_GUARD(m_csLock);
        inserted = m_EventMatchMap.emplace(static_cast<std::uint32_t>(eventMatch->m_nPublicId), eventMatch).second;
    }
    if (inserted) {
        eventMatch->Appear();
        if (manager->GetSocket()) {
            manager->GetSocket()->Send("chd", 0xfe, 0x1d, eventMatch->m_nPublicId);
        }
        return eventMatch;
    }

    L2_RELEASE(eventMatch, ObjectRefType::CreateDelete);
    return nullptr;
}

bool CEventMatchMgr::DeleteEventMatch(std::uint32_t publicId) {
    L2_GUARD_FUNCTION();
    CEventMatch* eventMatch = nullptr;
    {
        L2_GUARD(m_csLock);
        const auto position = m_EventMatchMap.find(publicId);
        if (position != m_EventMatchMap.end()) {
            eventMatch = position->second;
            m_EventMatchMap.erase(position);
        }
    }
    if (!eventMatch) {
        return false;
    }

    eventMatch->Disappear();
    eventMatch->Delete();
    L2_RELEASE(eventMatch, ObjectRefType::CreateDelete);
    return true;
}

CSPointer<CEventMatch> CEventMatchMgr::FindEventMatch(std::uint32_t publicId) {
    L2_GUARD_FUNCTION();
    CSPointer<CEventMatch> result;
    L2_GUARD(m_csLock);
    const auto position = m_EventMatchMap.find(publicId);
    result.SetObject(position != m_EventMatchMap.end() ? position->second : nullptr, ObjectRefType::Anonymous);
    return result;
}

CEventMatch* CEventMatchMgr::GetLastEventMatch() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_csLock);
    CEventMatch* result = nullptr;
    for (const auto& entry : m_EventMatchMap) {
        result = entry.second;
    }
    return result;
}

void CEventMatchMgr::ShowList(::CSocket* socket) {
    // Current L2Server 0x0071dd50.
    L2_GUARD_FUNCTION();
    std::array<char, 0x1838> buf;
    std::array<char16_t, kRuleInfoCapacity> szTemp{};
    int nCount = 0;
    int nBufLen = 0;

    SendRuleMessage(socket, u"--- Event Match List begin ---");
    {
        L2_GUARD(m_csLock);
        int i = 0;
        for (auto it = m_EventMatchMap.begin(); it != m_EventMatchMap.end(); ++it, ++i) {
            if (i >= 50) {
                break;
            }
            CEventMatch* const pEventMatch = it->second;
            const char16_t* const packetSecondName = pEventMatch->GetTeamName(2);
            const char16_t* const packetFirstName = pEventMatch->GetTeamName(1);
            nBufLen += ::Assemble(
                buf.data() + nBufLen, static_cast<int>(buf.size()) - nBufLen, "dSS", pEventMatch->m_nPublicId,
                packetFirstName, packetSecondName
            );
            FormatCurrent(szTemp, u"%d: '%s' vs '%s'", pEventMatch->m_nPublicId, packetFirstName, packetSecondName);
            SendRuleMessage(socket, szTemp.data());
            ++nCount;
        }
    }
    SendRuleMessage(socket, u"--- Event Match List end ---");
    socket->Send("chdb", 0xfe, 0x0d, nCount, nBufLen, buf.data());
}

void CEventMatchUserCommandHandler::AddObserverByNPC(User* const user, const int matchId) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    CEventMatch* const eventMatch = Singleton<CEventMatchMgr>::inst().GetLastEventMatch();
    if (eventMatch == nullptr) {
        user->SendSystemMessage(SystemMessageId::MATCH_BEING_PREPARED_TRY_AGAIN_LATER);
        return;
    }

    eventMatch->SetObserver(*user, EMOT_PLAYER);
    CEventMatchHelper::StartObserve(matchId, *user);
}

void CEventMatchUserCommandHandler::RemoveObserver(const int matchId, User* const user) {
    // Current L2Server 0x00726220, original owner EventMatch.cpp.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    CSPointer<CEventMatch> eventMatch =
        Singleton<CEventMatchMgr>::inst().FindEventMatch(static_cast<std::uint32_t>(matchId));
    if (eventMatch) {
        eventMatch->SetObserver(*user, EMOT_NOT_OBSERVER);
    }
}

CEventMatchBuilderCommandParser::stEventMatchCommandMap CEventMatchBuilderCommandParser::m_CommandMap[20] = {
    {u"create", &CEventMatchBuilderCommandParser::Parse_create},
    {u"leader", &CEventMatchBuilderCommandParser::Parse_leader},
    {u"item_rule", &CEventMatchBuilderCommandParser::Parse_item_rule},
    {u"skill_rule", &CEventMatchBuilderCommandParser::Parse_skill_rule},
    {u"lock", &CEventMatchBuilderCommandParser::Parse_lock},
    {u"unlock", &CEventMatchBuilderCommandParser::Parse_unlock},
    {u"start", &CEventMatchBuilderCommandParser::Parse_start},
    {u"pause", &CEventMatchBuilderCommandParser::Parse_pause},
    {u"end", &CEventMatchBuilderCommandParser::Parse_end},
    {u"info", &CEventMatchBuilderCommandParser::Parse_info},
    {u"list", &CEventMatchBuilderCommandParser::Parse_list},
    {u"score", &CEventMatchBuilderCommandParser::Parse_score},
    {u"remove", &CEventMatchBuilderCommandParser::Parse_remove},
    {u"msg", &CEventMatchBuilderCommandParser::Parse_msg},
    {u"fence", &CEventMatchBuilderCommandParser::Parse_fence},
    {u"firecracker", &CEventMatchBuilderCommandParser::Parse_firecracker},
    {u"gm", &CEventMatchBuilderCommandParser::Parse_gm},
    {u"dispelall", &CEventMatchBuilderCommandParser::Parse_dispelall},
    {u"useskill", &CEventMatchBuilderCommandParser::Parse_useskill},
    {u"manage", &CEventMatchBuilderCommandParser::Parse_manage},
};

CEventMatchOpBuilderCommandParser::stEventMatchCommandMap CEventMatchOpBuilderCommandParser::m_CommandMap[2] = {
    {u"list", &CEventMatchOpBuilderCommandParser::Parse_list},
    {u"observer", &CEventMatchOpBuilderCommandParser::Parse_observer},
};

int CEventMatchBuilderCommandParser::Parse(::CSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0071c22c and table 0x015d2bd0..0x015d2d10.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    char16_t* const remainder = ::GetLeftString2(command, &token);
    if (token != nullptr) {
        for (const stEventMatchCommandMap& entry : m_CommandMap) {
            if (std::u16string_view(token).substr(0, std::char_traits<char16_t>::length(entry.pszCommandString)) ==
                entry.pszCommandString) {
                return entry.func(socket, user, remainder);
            }
        }
    }

    SendRuleMessage(
        socket,
        u"//eventmatch [create | leader | item_rule | skill_rule | lock | "
        u"unlock | start | pause | end | info | list | score | remove | "
        u"msg | fence | dispelall | useskill | manage] ..."
    );
    return 0;
}

int CEventMatchBuilderCommandParser::Parse_create(::CSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x00723c0c.
    L2_GUARD_FUNCTION();
    const auto sendUsage = [socket]() {
        SendRuleMessage(
            socket,
            u"//eventmatch create [match_rule] [team1_name] [team2_name] "
            u"[x] [y] [z] [fence_width] [fence_height]"
        );
        return 0;
    };
    char16_t* token = nullptr;
    command = ::GetLeftString2(command, &token);
    if (token == nullptr || !IsSignedDecimalStart(token[0])) {
        return sendUsage();
    }
    const int rule = WindowsWtol(std::u16string_view(token));

    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const std::u16string firstTeamName = std::u16string(token);
    if (firstTeamName.size() > 30) {
        SendRuleMessage(socket, u"0 < length(team1_name) < 30");
        return 0;
    }

    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const std::u16string secondTeamName = std::u16string(token);
    if (secondTeamName.size() > 30) {
        SendRuleMessage(socket, u"0 < length(team2_name) < 30");
        return 0;
    }

    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int x = WindowsWtol(std::u16string_view(token));
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int y = WindowsWtol(std::u16string_view(token));
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int requestedZ = WindowsWtol(std::u16string_view(token));
    if (!CWorld::Inst().IsValidPosition(x, y)) {
        SendRuleMessage(socket, u"invalid position");
        return 0;
    }
    const int z = CWorld::Inst().GetGeo()->FindGround(x, y, requestedZ);

    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int fenceWidth = WindowsWtol(std::u16string_view(token));
    if (static_cast<std::uint32_t>(fenceWidth) - 100U > 0x1324U) {
        SendRuleMessage(socket, u"100 <= fence_width < 5000");
        return 0;
    }

    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        return sendUsage();
    }
    const int fenceHeight = WindowsWtol(std::u16string_view(token));
    if (static_cast<std::uint32_t>(fenceHeight) - 100U > 0x1324U) {
        SendRuleMessage(socket, u"100 <= fence_height < 5000");
        return 0;
    }

    if (Singleton<CEventMatchMgr>::inst().CreateEventMatch(
            user, rule, FVector(x, y, z), fenceWidth, fenceHeight, firstTeamName, secondTeamName
        ) != nullptr) {
        return 1;
    }

    return sendUsage();
}

int CEventMatchBuilderCommandParser::Parse_leader(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00723fa8.
    L2_GUARD_FUNCTION();
    const auto sendUsage = [socket]() {
        SendRuleMessage(socket, u"//eventmatch leader [match_id] [1|2] [leader_id]");
        SendRuleMessage(socket, u"(1: team1, 2: team2)");
        return 0;
    };
    char16_t* token = nullptr;
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int matchId = WindowsWtol(std::u16string_view(token));
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int team = WindowsWtol(std::u16string_view(token));
    if (!ValidTeam(team)) {
        return sendUsage();
    }

    {
        CSPointer<CEventMatch> eventMatch =
            Singleton<CEventMatchMgr>::inst().FindEventMatch(static_cast<std::uint32_t>(matchId));
        if (!eventMatch) {
            SendRuleMessage(socket, u"eventmatch not exists");
            return 0;
        }

        static_cast<void>(::GetLeftString2(command, &token));
        if (token == nullptr) {
            static_cast<void>(eventMatch->ResetTeamLeader(team));
            return sendUsage();
        }
        if (std::u16string_view(token).size() >= 25) {
            SendRuleMessage(socket, u"0 < length(leader_id) < 24");
            static_cast<void>(eventMatch->ResetTeamLeader(team));
            return 0;
        }

        const std::u16string loweredName = LowercaseGameName(token);
        CSPointer<User> leader = GetUserFromNameMap(g_nWorldId, loweredName);
        if (!leader) {
            SendRuleMessage(socket, u"can't find leader_id");
            static_cast<void>(eventMatch->ResetTeamLeader(team));
            return 0;
        }

        static_cast<void>(eventMatch->SetTeamLeader(team, UserSid(*leader.get())));
        return 1;
    }
}

int CEventMatchBuilderCommandParser::Parse_item_rule(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x0072431c.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    char16_t* const remainder = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        SendRuleMessage(
            socket,
            u"//eventmatch item_rule [match_id] [allow_all | deny_all | "
            u"A(item_id) | D(item_id)] ..."
        );
        SendRuleMessage(socket, u"(A: allow, D: deny)");
        return 0;
    }

    CSPointer<CEventMatch> eventMatch = Singleton<CEventMatchMgr>::inst().FindEventMatch(
        static_cast<std::uint32_t>(WindowsWtol(std::u16string_view(token)))
    );
    if (!eventMatch) {
        SendRuleMessage(socket, u"eventmatch not exists");
        return 0;
    }
    eventMatch->ItemRuleParsing(remainder);
    return 1;
}

int CEventMatchBuilderCommandParser::Parse_skill_rule(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x007244cc.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    char16_t* const remainder = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        SendRuleMessage(
            socket,
            u"//eventmatch skill_rule [match_id] [allow_all | deny_all | "
            u"A(skill_id) | D(skill_id)] ..."
        );
        SendRuleMessage(socket, u"(A: allow, D: deny)");
        return 0;
    }

    CSPointer<CEventMatch> eventMatch = Singleton<CEventMatchMgr>::inst().FindEventMatch(
        static_cast<std::uint32_t>(WindowsWtol(std::u16string_view(token)))
    );
    if (!eventMatch) {
        SendRuleMessage(socket, u"eventmatch not exists");
        return 0;
    }
    eventMatch->SkillRuleParsing(remainder);
    return 1;
}

int CEventMatchBuilderCommandParser::Parse_lock(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x0072467c.
    L2_GUARD_FUNCTION();
    const auto sendUsage = [socket]() {
        SendRuleMessage(socket, u"//eventmatch lock [match_id] [1|2]");
        SendRuleMessage(socket, u"(1: team1, 2: team2)");
        return 0;
    };
    char16_t* token = nullptr;
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int matchId = WindowsWtol(std::u16string_view(token));
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        return sendUsage();
    }
    const int team = WindowsWtol(std::u16string_view(token));
    if (!ValidTeam(team)) {
        return sendUsage();
    }
    {
        CSPointer<CEventMatch> eventMatch =
            Singleton<CEventMatchMgr>::inst().FindEventMatch(static_cast<std::uint32_t>(matchId));
        if (!eventMatch) {
            SendRuleMessage(socket, u"eventmatch not exists");
            return 0;
        }
        static_cast<void>(eventMatch->TeamLock(socket, team));
        return 1;
    }
}

int CEventMatchBuilderCommandParser::Parse_unlock(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00724864.
    L2_GUARD_FUNCTION();
    const auto sendUsage = [socket]() {
        SendRuleMessage(socket, u"//eventmatch unlock [match_id] [1|2]");
        SendRuleMessage(socket, u"(1: team1, 2: team2)");
        return 0;
    };
    char16_t* token = nullptr;
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int matchId = WindowsWtol(std::u16string_view(token));
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        return sendUsage();
    }
    const int team = WindowsWtol(std::u16string_view(token));
    if (!ValidTeam(team)) {
        return sendUsage();
    }
    {
        CSPointer<CEventMatch> eventMatch =
            Singleton<CEventMatchMgr>::inst().FindEventMatch(static_cast<std::uint32_t>(matchId));
        if (!eventMatch) {
            SendRuleMessage(socket, u"eventmatch not exists");
            return 0;
        }
        static_cast<void>(eventMatch->TeamUnlock(socket, team));
        return 1;
    }
}

int CEventMatchBuilderCommandParser::Parse_start(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00724a4c.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        SendRuleMessage(socket, u"//eventmatch start [match_id]");
        return 0;
    }
    CSPointer<CEventMatch> eventMatch = Singleton<CEventMatchMgr>::inst().FindEventMatch(
        static_cast<std::uint32_t>(WindowsWtol(std::u16string_view(token)))
    );
    if (!eventMatch) {
        SendRuleMessage(socket, u"eventmatch not exists");
        return 0;
    }
    static_cast<void>(eventMatch->MatchStart());
    return 1;
}

int CEventMatchBuilderCommandParser::Parse_pause(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00724bd8.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        SendRuleMessage(socket, u"//eventmatch pause [match_id]");
        return 0;
    }
    CSPointer<CEventMatch> eventMatch = Singleton<CEventMatchMgr>::inst().FindEventMatch(
        static_cast<std::uint32_t>(WindowsWtol(std::u16string_view(token)))
    );
    if (!eventMatch) {
        SendRuleMessage(socket, u"eventmatch not exists");
        return 0;
    }
    static_cast<void>(eventMatch->MatchPause());
    return 1;
}

int CEventMatchBuilderCommandParser::Parse_end(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00724d64.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        SendRuleMessage(socket, u"//eventmatch end [match_id]");
        return 0;
    }
    CSPointer<CEventMatch> eventMatch = Singleton<CEventMatchMgr>::inst().FindEventMatch(
        static_cast<std::uint32_t>(WindowsWtol(std::u16string_view(token)))
    );
    if (!eventMatch) {
        SendRuleMessage(socket, u"eventmatch not exists");
        return 0;
    }
    static_cast<void>(eventMatch->MatchEnd());
    return 1;
}

int CEventMatchBuilderCommandParser::Parse_info(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00724ef0.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        SendRuleMessage(socket, u"//eventmatch info [match_id]");
        return 0;
    }
    CSPointer<CEventMatch> eventMatch = Singleton<CEventMatchMgr>::inst().FindEventMatch(
        static_cast<std::uint32_t>(WindowsWtol(std::u16string_view(token)))
    );
    if (!eventMatch) {
        SendRuleMessage(socket, u"eventmatch not exists");
        return 0;
    }
    eventMatch->ShowInfo(socket);
    return 1;
}

int CEventMatchBuilderCommandParser::Parse_list(::CSocket* socket, User*, char16_t*) {
    // Current L2Server 0x00725080.
    L2_GUARD_FUNCTION();
    Singleton<CEventMatchMgr>::inst().ShowList(socket);
    return 1;
}

int CEventMatchBuilderCommandParser::Parse_score(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00725130.
    L2_GUARD_FUNCTION();
    const auto sendUsage = [socket]() {
        SendRuleMessage(socket, u"//eventmatch score [match_id] [team1_score] [team2_score]");
        return 0;
    };
    char16_t* token = nullptr;
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int matchId = WindowsWtol(std::u16string_view(token));
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int firstScore = WindowsWtol(std::u16string_view(token));
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        return sendUsage();
    }
    const int secondScore = WindowsWtol(std::u16string_view(token));
    {
        CSPointer<CEventMatch> eventMatch =
            Singleton<CEventMatchMgr>::inst().FindEventMatch(static_cast<std::uint32_t>(matchId));
        if (!eventMatch) {
            SendRuleMessage(socket, u"eventmatch not exists");
            return 0;
        }
        eventMatch->Broadcast("chddd", 0xfe, 0x10, matchId, firstScore, secondScore);
        return 1;
    }
}

int CEventMatchBuilderCommandParser::Parse_remove(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00725324.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        SendRuleMessage(socket, u"//eventmatch remove [match_id]");
        return 0;
    }
    if (Singleton<CEventMatchMgr>::inst().DeleteEventMatch(
            static_cast<std::uint32_t>(WindowsWtol(std::u16string_view(token)))
        )) {
        return 1;
    }
    SendRuleMessage(socket, u"eventmatch not exists");
    return 0;
}

int CEventMatchBuilderCommandParser::Parse_msg(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00725434.
    L2_GUARD_FUNCTION();
    const auto sendUsage = [socket]() {
        SendRuleMessage(socket, u"//eventmatch msg [match_id] [type] [message]");
        return 0;
    };
    char16_t* token = nullptr;
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int matchId = WindowsWtol(std::u16string_view(token));
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int messageType = WindowsWtol(std::u16string_view(token));
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        return sendUsage();
    }
    {
        CSPointer<CEventMatch> eventMatch =
            Singleton<CEventMatchMgr>::inst().FindEventMatch(static_cast<std::uint32_t>(matchId));
        if (!eventMatch) {
            SendRuleMessage(socket, u"eventmatch not exists");
            return 0;
        }
        eventMatch->Broadcast("chcS", 0xfe, 0x0f, messageType, token);
        socket->Send("chcS", 0xfe, 0x0f, messageType, token);
        return 1;
    }
}

int CEventMatchBuilderCommandParser::Parse_fence(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x0072564c.
    L2_GUARD_FUNCTION();
    const auto sendUsage = [socket]() {
        SendRuleMessage(socket, u"//eventmatch fence [match_id] [fence_state]");
        SendRuleMessage(socket, u"(fence_state: 0-none, 1-column, 2-wall)");
        return 0;
    };
    char16_t* token = nullptr;
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int matchId = WindowsWtol(std::u16string_view(token));
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        return sendUsage();
    }
    const int fenceState = WindowsWtol(std::u16string_view(token));
    if (static_cast<std::uint32_t>(fenceState) >= 3U) {
        return sendUsage();
    }
    {
        CSPointer<CEventMatch> eventMatch =
            Singleton<CEventMatchMgr>::inst().FindEventMatch(static_cast<std::uint32_t>(matchId));
        if (!eventMatch) {
            SendRuleMessage(socket, u"eventmatch not exists");
            return 0;
        }
        eventMatch->SetFenceStatus(static_cast<ColosseumFenceState>(fenceState));
        return 1;
    }
}

int CEventMatchBuilderCommandParser::Parse_firecracker(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00722864.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        SendRuleMessage(socket, u"//eventmatch firecracker [who]");
        return 0;
    }
    if (std::u16string_view(token).size() >= 25) {
        SendRuleMessage(socket, u"0 < length(who) < 24");
        return 0;
    }

    const std::u16string loweredName = LowercaseGameName(token);
    CSPointer<User> target = GetUserFromNameMap(g_nWorldId, loweredName);
    if (!target) {
        SendRuleMessage(socket, u"can't find who");
        return 0;
    }

    const FVector position = target->GetPos();
    g_ptlUserMap->BroadcastToNeighbor(*target.get(), 0x80000, position, "chd", 0xfe, 5, static_cast<int>(target->m_nId));
    target->SocialAction(0x84a, 0x800, false);
    return 1;
}

int CEventMatchBuilderCommandParser::Parse_gm(::CSocket* socket, User*, char16_t*) {
    // Current L2Server 0x0071c358.
    L2_GUARD_FUNCTION();
    socket->Send("ch", 0xfe, 7);
    return 1;
}

int CEventMatchBuilderCommandParser::Parse_dispelall(::CSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00725824.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        SendRuleMessage(socket, u"//eventmatch dispelall [match_id]");
        return 0;
    }
    CSPointer<CEventMatch> eventMatch = Singleton<CEventMatchMgr>::inst().FindEventMatch(
        static_cast<std::uint32_t>(WindowsWtol(std::u16string_view(token)))
    );
    if (!eventMatch) {
        SendRuleMessage(socket, u"eventmatch not exists");
        return 0;
    }
    eventMatch->DispelAll();
    return 1;
}

int CEventMatchBuilderCommandParser::Parse_useskill(::CSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x007259b0.
    L2_GUARD_FUNCTION();
    const auto sendUsage = [socket]() {
        SendRuleMessage(socket, u"//eventmatch useskill [match_id] [skill_id] [skill_level] ...");
        return 0;
    };
    char16_t* token = nullptr;
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int matchId = WindowsWtol(std::u16string_view(token));

    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    {
        std::vector<CSkillInfo*> skills;
        while (token != nullptr) {
            const int skillId = WindowsWtol(std::u16string_view(token));
            command = ::GetLeftString2(command, &token);
            if (token == nullptr) {
                return sendUsage();
            }
            const int skillLevel = WindowsWtol(std::u16string_view(token));
            const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel), user);
            if (skill == nullptr) {
                std::array<char16_t, 1024> message{};
                FormatUtf16To(message.data(), message.size(), u"Invalid Skill ID(id:%d, level:%d)", skillId, skillLevel);
                SendRuleMessage(socket, message.data());
                return 0;
            }
            skills.push_back(const_cast<CSkillInfo*>(skill));
            command = ::GetLeftString2(command, &token);
        }

        CSPointer<CEventMatch> eventMatch =
            Singleton<CEventMatchMgr>::inst().FindEventMatch(static_cast<std::uint32_t>(matchId));
        if (!eventMatch) {
            SendRuleMessage(socket, u"eventmatch not exists");
            return 0;
        }
        eventMatch->UseSkill(user, skills);
        return 1;
    }
}

int CEventMatchBuilderCommandParser::Parse_manage(::CSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x00725cf0.
    L2_GUARD_FUNCTION();
    if (user == nullptr || socket == nullptr) {
        return 0;
    }

    char16_t* token = nullptr;
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        SendRuleMessage(socket, u"//eventmatch manage [match_id]");
        return 0;
    }
    CSPointer<CEventMatch> eventMatch = Singleton<CEventMatchMgr>::inst().FindEventMatch(
        static_cast<std::uint32_t>(WindowsWtol(std::u16string_view(token)))
    );
    if (!eventMatch) {
        SendRuleMessage(socket, u"eventmatch not exists");
        return 0;
    }
    eventMatch->Manage(user);
    return 1;
}

int CEventMatchOpBuilderCommandParser::Parse(::CSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0071c418 and table 0x015d2d10..0x015d2d30.
    L2_GUARD_FUNCTION();
    char16_t* token = nullptr;
    char16_t* const remainder = ::GetLeftString2(command, &token);
    if (token != nullptr) {
        for (const stEventMatchCommandMap& entry : m_CommandMap) {
            if (std::u16string_view(token).substr(0, std::char_traits<char16_t>::length(entry.pszCommandString)) ==
                entry.pszCommandString) {
                return entry.func(socket, user, remainder);
            }
        }
    }

    SendRuleMessage(socket, u"//eventmatchop [list | observer] ...");
    return 0;
}

int CEventMatchOpBuilderCommandParser::Parse_list(::CSocket* socket, User*, char16_t*) {
    // Current L2Server 0x00725e9c.
    L2_GUARD_FUNCTION();
    Singleton<CEventMatchMgr>::inst().ShowList(socket);
    return 1;
}

int CEventMatchOpBuilderCommandParser::Parse_observer(::CSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x00725f4c.
    L2_GUARD_FUNCTION();
    const auto sendUsage = [socket]() {
        SendRuleMessage(socket, u"//eventmatchop observer [match_id] [on|off]");
        return 0;
    };
    char16_t* token = nullptr;
    command = ::GetLeftString2(command, &token);
    if (token == nullptr) {
        return sendUsage();
    }
    const int matchId = WindowsWtol(std::u16string_view(token));
    static_cast<void>(::GetLeftString2(command, &token));
    if (token == nullptr) {
        return sendUsage();
    }

    EventMatchObserverType observerType = EMOT_NOT_OBSERVER;
    if (std::u16string_view(token).substr(0, 2) == u"on") {
        observerType = EMOT_BUILDER;
    } else if (std::u16string_view(token).substr(0, 3) != u"off") {
        return sendUsage();
    }

    {
        CSPointer<CEventMatch> eventMatch =
            Singleton<CEventMatchMgr>::inst().FindEventMatch(static_cast<std::uint32_t>(matchId));
        if (!eventMatch) {
            SendRuleMessage(socket, u"eventmatch not exists");
            return 0;
        }
        eventMatch->SetObserver(*user, observerType);
        return 1;
    }
}

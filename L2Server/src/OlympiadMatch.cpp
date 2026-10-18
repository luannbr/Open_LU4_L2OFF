#include "OlympiadMatch.h"
#include "Area.h"
#include "CConfig.h"
#include "CWorld.h"
#include "CursedWeapon.h"
#include "IOCPMessageServer.h"
#include "InstantZone.h"
#include "NpcServer.h"
#include "Object.h"
#include "OlympiadStuff.h"
#include "PledgeGameSystem.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/TextRuntime.h"
#include "Skill.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <utility>

namespace {

GameValidCondition CheckPrepareValidCondition(WOlympiadTeam& team) {
    // Current L2Server 0x008fda3c.
    CSPointer<WOlympiadMemberInfo> member = team.GetMember();
    if (!member) {
        return GVC_VALID_TEAM;
    }

    GameValidCondition condition = member->GetPrepareValidCondition();
    if (condition != GVC_NONE && condition != GVC_VALID_TEAM) {
        return condition;
    }

    const std::uint32_t sid = team.GetUserSID();
    if (sid == 0U) {
        return condition;
    }
    const CSPointer<User> user(WOlympiadStuff::FindUserBySID(sid), ObjectRefType::FunctionChain);
    return user ? GVC_NONE : GVC_VALID_TEAM;
}

GameValidCondition CheckValidConditionAtWait(WOlympiadTeam& team) {
    const GameValidCondition prepared = CheckPrepareValidCondition(team);
    if (prepared != GVC_NONE) {
        return prepared;
    }

    constexpr std::array<GameValidCondition, 3> checks{
        GVC_ALIVE,
        GVC_NOT_CURSED,
        GVC_SUBJOB,
    };
    for (const GameValidCondition condition : checks) {
        if (!team.IsValidCondition(condition)) {
            return condition;
        }
    }
    return GVC_NONE;
}

bool IsAvailableInZone(const CInstantZone* const zone) {
    return zone != nullptr && !zone->IsDeletingObject() && zone->m_Deleted == 0;
}

template <std::size_t Size>
void CopyWideToUtf16(char16_t (&destination)[Size], const wchar_t* source) {
    std::size_t index = 0;
    if (source != nullptr) {
        for (; index + 1 < Size && source[index] != L'\0'; ++index) {
            destination[index] = static_cast<char16_t>(source[index]);
        }
    }
    destination[index] = u'\0';
    for (++index; index < Size; ++index) {
        destination[index] = u'\0';
    }
}

} // namespace

WOlympiadObserverEntry::WOlympiadObserverEntry() {}

WOlympiadObserverEntry::~WOlympiadObserverEntry() = default;

void WOlympiadObserverEntry::Broadcast(const char* const format, va_list arguments) {
    // Current L2Server 0x008dea0c. Invalid user ids are removed while the
    // tree walk is in progress. Every live observer receives an independent
    // copy of the target va_list.
    L2_GUARD_FUNCTION();
    for (auto position = m_Observers.begin(); position != m_Observers.end();) {
        CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(*position));
        if (!user) {
            position = m_Observers.erase(position);
            continue;
        }
        if (CUserSocket* const socket = user->GetSocket()) {
            va_list copy;
            va_copy(copy, arguments);
            socket->SendV(format, copy);
            va_end(copy);
        }
        ++position;
    }
}

void WOlympiadObserverEntry::Broadcast(const char* const format, ...) {
    // Current L2Server 0x008deb40.
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    Broadcast(format, arguments);
    va_end(arguments);
}

void WOlympiadObserverEntry::Broadcast(const S_EX_PROTOCOL protocol, const network::IPacket& packet) {
    // Current L2Server 0x008debe4. SendPacketTo performs the same live-PC,
    // deletion and socket checks as the target and failed ids are erased.
    L2_GUARD_FUNCTION();
    for (auto position = m_Observers.begin(); position != m_Observers.end();) {
        if (!WOlympiadStuff::SendPacketTo(*position, protocol, packet)) {
            position = m_Observers.erase(position);
            continue;
        }
        ++position;
    }
}

bool WOlympiadObserverEntry::AddEntry(const std::uint32_t sid) {
    // Current L2Server 0x008dfae4.
    L2_GUARD_FUNCTION();
    return m_Observers.insert(sid).second;
}

void WOlympiadObserverEntry::RemoveEntry(const std::uint32_t sid) {
    // Current L2Server 0x008e0bac.
    L2_GUARD_FUNCTION();
    m_Observers.erase(sid);
}

void WOlympiadObserverEntry::RemoveEntryAll() {
    // Current L2Server 0x008dfb94.
    L2_GUARD_FUNCTION();
    m_Observers.clear();
}

std::vector<std::uint32_t> WOlympiadObserverEntry::GetEntries() const {
    // Current L2Server 0x008e18a4.
    L2_GUARD_FUNCTION();
    std::vector<std::uint32_t> entries;
    entries.reserve(m_Observers.size());
    for (const std::uint32_t sid : m_Observers) {
        entries.push_back(sid);
    }
    return entries;
}

WOlympiadFieldTeamHolder::WOlympiadFieldTeamHolder() : occupied(false), m_alignment0079To0080{} {}

WOlympiadMatchBroadcaster::WOlympiadMatchBroadcaster() : m_BroadCastingTimeTable{} {}

void WOlympiadMatchBroadcaster::Broadcast(WOlympiadFieldTeamHolder* const teams, const int remainingTime) {
    // Current L2Server 0x008db7c8.
    if (m_BroadCastingTimeTable[0] == 0) {
        m_BroadCastingTimeTable[0] = 1;
        m_BroadCastingTimeTable[1] = 10;
        m_BroadCastingTimeTable[2] = 10;
        m_BroadCastingTimeTable[3] = 59;
        m_BroadCastingTimeTable[4] = 59;
        return;
    }

    for (int index = 1; index < 5; ++index) {
        if (m_BroadCastingTimeTable[index] != remainingTime) {
            continue;
        }
        WOlympiadServiceUtil::SendOSN_SYSTEM_MESSAGE_NO(teams[index & 1].team.GetMemberKey(), (index < 3 ? 1 : 0) + 0x104e);
    }
}

void WOlympiadMatchBroadcaster::ResetBroadCastingTimeTable() {
    // Current owner clears the complete 0x14-byte table.
    std::memset(m_BroadCastingTimeTable, 0, sizeof(m_BroadCastingTimeTable));
}

void WOlympiadMatchBroadcaster::SetBroadCastingTimeTable(const int index) {
    if (static_cast<unsigned int>(index) < std::size(m_BroadCastingTimeTable)) {
        m_BroadCastingTimeTable[index] = 1;
    }
}

WOlympiadMatch::MatchRule::MatchRule() : m_pMatch(nullptr) {}

WOlympiadMatch::MatchRule::MatchRule(WOlympiadMatch* const match) : m_pMatch(match) {}

WOlympiadMatch::MatchRule::~MatchRule() = default;

void WOlympiadMatch::MatchRule::OnStatusChanged(WFieldStatus::type) {}

void WOlympiadMatch::MatchRule::OnTickWait(int) {}

void WOlympiadMatch::MatchRule::OnObserverAdded(std::uint32_t) {}

void WOlympiadMatch::MatchRule::OnPlayerDied(User*) {}

bool WOlympiadMatch::MatchRule::CanAttack() const { return false; }

void WOlympiadMatch::MatchRule::BroadcastMatchResult() const {}

int WOlympiadMatch::MatchRule::GetRewardItemCount(OlympiadTeamEnum) const { return 0; }

void WOlympiadMatch::MatchRule::GetTeamByGameResult(OlympiadTeamEnum& winner, OlympiadTeamEnum& loser) const {
    L2_GUARD_FUNCTION();
    if (m_pMatch->m_eGameResult == GS_PLAYER1_LOSE) {
        winner = OTE_TEAM2;
        loser = OTE_TEAM1;
    } else {
        winner = OTE_TEAM1;
        loser = OTE_TEAM2;
    }
}

void WOlympiadMatch::MatchRule::BroadcastMessage(const int messageId) const {
    L2_GUARD_FUNCTION();
    m_pMatch->Broadcast("ch", 0x62, messageId);
}

void WOlympiadMatch::MatchRule::BroadcastMessageToObservers(const int messageId) const {
    L2_GUARD_FUNCTION();
    m_pMatch->m_ObserverEntry.Broadcast("chc", 0x62, messageId, 1);
}

void WOlympiadMatch::MatchRule::BroadcastMessageSToObservers(const int messageId, const wchar_t* const value) const {
    L2_GUARD_FUNCTION();
    m_pMatch->m_ObserverEntry.Broadcast(
        "chccS", 0x62, messageId, 1, 0x0c, target_utf16::FromRecoveredWindowsWide(value).c_str()
    );
}

void WOlympiadMatch::MatchRule::BroadcastMessageC(const int messageId, const char value) const {
    L2_GUARD_FUNCTION();
    m_pMatch->Broadcast("chccc", 0x62, messageId, 1, 0x14, static_cast<int>(value));
}

void WOlympiadMatch::MatchRule::BroadcastCountDown(const int messageId, const int remainingTime) const {
    L2_GUARD_FUNCTION();
    if (!m_pMatch->m_bCheck1SecElapsed) {
        return;
    }
    constexpr std::array<int, 9> countdown{120, 60, 30, 10, 5, 4, 3, 2, 1};
    if (std::find(countdown.begin(), countdown.end(), remainingTime) != countdown.end()) {
        BroadcastMessageC(messageId, static_cast<char>(remainingTime));
    }
}

WOlympiadMatch::MatchRule3Rounds::MatchRule3Rounds() :
    MatchRule(nullptr),
    m_nCurrentRound(0),
    m_eState(SM_NONE),
    m_tStateChanged(0),
    m_aRoundResults{},
    m_anWinCounts{},
    m_anRewardCounts{},
    m_abInterRoundProcessed{},
    m_alignment003fTo0040(0),
    m_anLastDamages{},
    m_CurrentResult(RR_NONE),
    m_alignment004cTo0050{} {}

WOlympiadMatch::MatchRule3Rounds::MatchRule3Rounds(WOlympiadMatch* const match) :
    MatchRule(match),
    m_nCurrentRound(0),
    m_eState(SM_NONE),
    m_tStateChanged(0),
    m_aRoundResults{},
    m_anWinCounts{},
    m_anRewardCounts{},
    m_abInterRoundProcessed{},
    m_alignment003fTo0040(0),
    m_anLastDamages{},
    m_CurrentResult(RR_NONE),
    m_alignment004cTo0050{} {}

WOlympiadMatch::MatchRule3Rounds::~MatchRule3Rounds() = default;

void WOlympiadMatch::MatchRule3Rounds::OnTickWait(int) {
    L2_GUARD_FUNCTION();
}

int WOlympiadMatch::MatchRule3Rounds::GetElapsedTime() const { return m_pMatch->m_tNow - m_tStateChanged; }

int WOlympiadMatch::MatchRule3Rounds::GetRemainTime() const {
    const int duration = m_eState == SM_ROUND ? 100 : 20;
    return std::max(0, duration - GetElapsedTime());
}

bool WOlympiadMatch::MatchRule3Rounds::AreAllResultPenalty() const {
    L2_GUARD_FUNCTION();
    for (int round = 1; round < 4; ++round) {
        if (m_aRoundResults[round] != RR_PENALTY) {
            return false;
        }
    }
    return true;
}

void WOlympiadMatch::MatchRule3Rounds::AddWinCount(const RoundResult result) {
    L2_GUARD_FUNCTION();
    if (result == RR_TEAM1_WIN) {
        ++m_anWinCounts[OTE_TEAM1];
    } else if (result == RR_TEAM2_WIN) {
        ++m_anWinCounts[OTE_TEAM2];
    }
}

int WOlympiadMatch::MatchRule3Rounds::GetRewardItemCount(const OlympiadTeamEnum team) const {
    return m_anRewardCounts[team];
}

const wchar_t* WOlympiadMatch::MatchRule3Rounds::GetName(const OlympiadTeamEnum team) const {
    L2_GUARD_FUNCTION();
    return m_pMatch->m_TeamHolder[team].team.GetPartyMasterName();
}

bool WOlympiadMatch::MatchRule3Rounds::IsValidCondition(const OlympiadTeamEnum team) const {
    L2_GUARD_FUNCTION();
    return m_pMatch->m_TeamHolder[team].team.IsValidCondition(GVC_VALID_TEAM);
}

bool WOlympiadMatch::MatchRule3Rounds::CanAttack() const {
    return m_pMatch->m_nStatus == WFieldStatus::PROCESS_MATCH && m_eState == SM_ROUND;
}

bool WOlympiadMatch::MatchRule3Rounds::IsMatchEnd() const {
    return !IsValidCondition(OTE_TEAM1) || !IsValidCondition(OTE_TEAM2) || m_nCurrentRound == 3 ||
        m_anWinCounts[OTE_TEAM1] > 1 || m_anWinCounts[OTE_TEAM2] > 1;
}

void WOlympiadMatch::MatchRule3Rounds::GetInfoPacket(protocol::S_EX_OLYMPIAD_MATCH_INFO& packet) const {
    L2_GUARD_FUNCTION();
    packet.nCurrentSet = m_nCurrentRound;
    packet.nRemainSecond = GetRemainTime();
    CopyWideToUtf16(packet.szTeam1Name, GetName(OTE_TEAM1));
    packet.nTeam1WinCount = m_anWinCounts[OTE_TEAM1];
    CopyWideToUtf16(packet.szTeam2Name, GetName(OTE_TEAM2));
    packet.nTeam2WinCount = m_anWinCounts[OTE_TEAM2];
}

void WOlympiadMatch::MatchRule3Rounds::SendInfoTo(const std::uint32_t sid) const {
    L2_GUARD_FUNCTION();
    protocol::S_EX_OLYMPIAD_MATCH_INFO packet{};
    GetInfoPacket(packet);
    WOlympiadStuff::SendPacketTo(sid, static_cast<S_EX_PROTOCOL>(0x1fc), packet);
}

void WOlympiadMatch::MatchRule3Rounds::BroadcastInfo() const {
    L2_GUARD_FUNCTION();
    protocol::S_EX_OLYMPIAD_MATCH_INFO packet{};
    GetInfoPacket(packet);
    m_pMatch->Broadcast(static_cast<S_EX_PROTOCOL>(0x1fc), packet);
}

void WOlympiadMatch::MatchRule3Rounds::OnObserverAdded(const std::uint32_t sid) {
    L2_GUARD_FUNCTION();
    if (m_pMatch->m_nStatus == WFieldStatus::PROCESS_MATCH) {
        SendInfoTo(sid);
    }
}

void WOlympiadMatch::MatchRule3Rounds::OnPlayerDied(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || m_pMatch->m_nStatus != WFieldStatus::PROCESS_MATCH) {
        return;
    }
    CSPointer<User> player(user, ObjectRefType::FunctionChain);
    for (int index = 0; index < 2; ++index) {
        if (!m_pMatch->m_TeamHolder[index].team.IsRegisteredUser(player)) {
            continue;
        }
        int expected = RR_NONE;
        m_CurrentResult.compare_exchange_strong(expected, 4 - (index != 0));
    }
}

WOlympiadMatch::MatchRule3Rounds::RoundResult WOlympiadMatch::MatchRule3Rounds::CheckRoundResult() const {
    // Current L2Server 0x008dbdf0.
    L2_GUARD_FUNCTION();
    const bool team1Valid = IsValidCondition(OTE_TEAM1);
    const bool team2Valid = IsValidCondition(OTE_TEAM2);
    if (!team1Valid) {
        return team2Valid ? RR_NONE : RR_DRAW;
    }
    if (!team2Valid) {
        return RR_NONE;
    }

    const int team1Damage = m_pMatch->m_TeamHolder[OTE_TEAM1].team.GetDamage();
    const int team2Damage = m_pMatch->m_TeamHolder[OTE_TEAM2].team.GetDamage();
    const int team1RoundDamage = team1Damage - m_anLastDamages[OTE_TEAM1];
    const int team2RoundDamage = team2Damage - m_anLastDamages[OTE_TEAM2];
    if (team1RoundDamage == team2RoundDamage) {
        return static_cast<RoundResult>(2 - static_cast<int>(team1Damage != m_anLastDamages[OTE_TEAM1]));
    }
    return static_cast<RoundResult>(static_cast<int>(team2RoundDamage < team1RoundDamage) + 3);
}

WOlympiadMatch::MatchRule3Rounds::RoundResult WOlympiadMatch::MatchRule3Rounds::CheckRoundEnd() const {
    // Current L2Server 0x008dbefc.
    L2_GUARD_FUNCTION();
    const int current = m_CurrentResult.load();
    if (current != RR_NONE) {
        return static_cast<RoundResult>(current);
    }

    const bool team1Valid = IsValidCondition(OTE_TEAM1);
    const bool team2Valid = IsValidCondition(OTE_TEAM2);
    if (!team1Valid && !team2Valid) {
        return RR_DRAW;
    }
    if (team1Valid && team2Valid) {
        return RR_NONE;
    }
    return static_cast<RoundResult>(4 - static_cast<int>(team1Valid));
}

void WOlympiadMatch::MatchRule3Rounds::AddRewardCount(const RoundResult result) {
    // Current L2Server 0x008dbfd4.
    L2_GUARD_FUNCTION();
    const int count = WOlympiadDefaultSetting::inst().GetRewardCount(m_nCurrentRound);
    if (result == RR_TEAM1_WIN) {
        m_anRewardCounts[OTE_TEAM1] += count;
    } else if (result == RR_TEAM2_WIN) {
        m_anRewardCounts[OTE_TEAM2] += count;
    }
}

void WOlympiadMatch::MatchRule3Rounds::RewardCountByGameResult() {
    // Current L2Server 0x008dc090.
    L2_GUARD_FUNCTION();
    OlympiadTeamEnum winner = OTE_MAX;
    OlympiadTeamEnum loser = OTE_MAX;
    GetTeamByGameResult(winner, loser);

    const bool winnerValid = IsValidCondition(winner);
    const bool loserValid = IsValidCondition(loser);
    const int winnerAdditional = WOlympiadDefaultSetting::inst().GetAdditionalRewardCount(true);
    const int loserAdditional = WOlympiadDefaultSetting::inst().GetAdditionalRewardCount(false);

    if (m_pMatch->m_eGameResult == GS_DRAW || m_pMatch->m_eGameResult == GS_PENALTY) {
        if (!winnerValid) {
            m_anRewardCounts[winner] = 0;
        }
        if (!loserValid) {
            m_anRewardCounts[loser] = 0;
        }
        return;
    }

    m_anRewardCounts[winner] = winnerValid ? m_anRewardCounts[winner] + winnerAdditional : 0;
    m_anRewardCounts[loser] = loserValid ? m_anRewardCounts[loser] + loserAdditional : 0;
}

CSPointer<User> WOlympiadMatch::MatchRule3Rounds::GetUser(const OlympiadTeamEnum team) const {
    return g_UserMap.FindObject(static_cast<int>(m_pMatch->m_TeamHolder[team].team.GetUserSID()));
}

void WOlympiadMatch::MatchRule3Rounds::RestorePlayerHPMPCP(const OlympiadTeamEnum team, const float percent) const {
    // Current L2Server 0x008dc20c.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUser(team);
    if (!user || user->m_data == nullptr) {
        return;
    }
    user->IncreaseHP_diff(static_cast<double>(percent) * user->m_data->m_dBaseHP, true, 4, 0, false);
    user->IncreaseMP_diff(static_cast<double>(percent) * user->m_data->m_dBaseMP, 4, 0, false);
    user->IncreaseCP_diff(static_cast<double>(percent) * user->m_data->m_dBaseCP, 4, 0, false);
}

void WOlympiadMatch::MatchRule3Rounds::DispelDebuffs() const {
    // Current L2Server 0x008dc36c.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < 2; ++index) {
        CSPointer<User> user = GetUser(static_cast<OlympiadTeamEnum>(index));
        if (user) {
            user->DispelByCategory(DB_BAD, user.get(), 100, nullptr, 1);
        }
    }
}

void WOlympiadMatch::MatchRule3Rounds::ResetSkillReuseTime() const {
    // Current L2Server 0x008dc454.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < 2; ++index) {
        CSPointer<User> user = GetUser(static_cast<OlympiadTeamEnum>(index));
        if (user) {
            WOlympiadDefaultSetting::inst().ResetSkillReuseDelay(user.get());
        }
    }
}

void WOlympiadMatch::MatchRule3Rounds::RestorePlayersHPMPCP(const int currentRound, RoundResult) const {
    // Current L2Server 0x008ddd94. The RoundResult parameter is unused.
    L2_GUARD_FUNCTION();
    if (currentRound == 2) {
        m_pMatch->RestorePlayers(false);
        return;
    }
    RestorePlayerHPMPCP(OTE_TEAM1, 0.7F);
    RestorePlayerHPMPCP(OTE_TEAM2, 0.7F);
}

WOlympiadMatch::MatchRule3Rounds::RoundResult WOlympiadMatch::MatchRule3Rounds::ConvertFromString(
    const std::u16string& value
) const {
    // Current L2Server 0x008dde68.
    L2_GUARD_FUNCTION();
    if (runtime::StringView(value) == u"draw") {
        return RR_DRAW;
    }
    if (runtime::StringView(value) == u"team1") {
        return RR_TEAM1_WIN;
    }
    if (runtime::StringView(value) == u"team2") {
        return RR_TEAM2_WIN;
    }
    if (runtime::StringView(value) == u"penalty") {
        return RR_PENALTY;
    }
    const auto isTeamName = [this, &value](const OlympiadTeamEnum team) {
        const wchar_t* const name = GetName(team);
        return name != nullptr && runtime::StringView(value) == WideToUtf16(name);
    };
    if (isTeamName(OTE_TEAM1)) {
        return RR_TEAM1_WIN;
    }
    if (isTeamName(OTE_TEAM2)) {
        return RR_TEAM2_WIN;
    }
    return RR_NONE;
}

void WOlympiadMatch::MatchRule3Rounds::BroadcastMatchResult() const {
    // Current L2Server 0x008de6f8.
    L2_GUARD_FUNCTION();
    const int draw = m_pMatch->m_eGameResult == GS_DRAW || m_pMatch->m_eGameResult == GS_PENALTY;
    OlympiadTeamEnum winner = OTE_MAX;
    OlympiadTeamEnum loser = OTE_MAX;
    GetTeamByGameResult(winner, loser);

    const WOlympiadTeam& winnerTeam = m_pMatch->m_TeamHolder[winner].team;
    const WOlympiadTeam& loserTeam = m_pMatch->m_TeamHolder[loser].team;
    std::array<char, 0x5000> winnerBuffer{};
    std::array<char, 0x5000> loserBuffer{};
    const int winnerLength = winnerTeam.EncodeMatchResultToBuffer(
        loserTeam.GetDamage(), winnerBuffer.data(), static_cast<int>(winnerBuffer.size())
    );
    const int loserLength = loserTeam.EncodeMatchResultToBuffer(
        winnerTeam.GetDamage(), loserBuffer.data(), static_cast<int>(loserBuffer.size())
    );

    const auto send = [&](const WOlympiadTeam& receiver) {
        receiver.Send(
            "chddSddbddbcccdd", 0xfe, 0xd5, 2, draw,
            target_utf16::FromRecoveredWindowsWide(winnerTeam.GetPartyMasterName()).c_str(), static_cast<int>(winner) + 1, 1,
            winnerLength, winnerBuffer.data(), static_cast<int>(loser) + 1, 1, loserLength, loserBuffer.data(),
            m_aRoundResults[1] - 1, m_aRoundResults[2] - 1, m_aRoundResults[3] - 1, m_anRewardCounts[winner],
            m_anRewardCounts[loser]
        );
    };
    send(winnerTeam);
    send(loserTeam);
}

void WOlympiadMatch::MatchRule3Rounds::BroadcastRound() const {
    // Current L2Server 0x008df220.
    L2_GUARD_FUNCTION();
    static constexpr int messages[4]{0, 0x1447, 0x1448, 0x1449};
    BroadcastMessage(messages[m_nCurrentRound]);
    BroadcastMessage(0x1441);
}

void WOlympiadMatch::MatchRule3Rounds::MatchEnd() {
    // Current L2Server 0x008df3a8.
    L2_GUARD_FUNCTION();
    if (AreAllResultPenalty()) {
        m_pMatch->m_eGameResult = GS_PENALTY;
        return;
    }
    if (!IsValidCondition(OTE_TEAM1)) {
        m_pMatch->m_eGameResult = GS_PLAYER1_LOSE;
        return;
    }
    if (!IsValidCondition(OTE_TEAM2)) {
        m_pMatch->m_eGameResult = GS_PLAYER2_LOSE;
        return;
    }
    if (m_anWinCounts[OTE_TEAM1] == m_anWinCounts[OTE_TEAM2]) {
        m_pMatch->m_eGameResult = GS_DRAW;
    } else {
        m_pMatch->m_eGameResult = static_cast<GameResult>(m_anWinCounts[OTE_TEAM2] < m_anWinCounts[OTE_TEAM1]);
    }
    BroadcastInfo();
}

void WOlympiadMatch::MatchRule3Rounds::ActivateSkillTo(User* const user, const int skillId) const {
    // Current L2Server 0x008df4b4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadMatch.cpp",
        //     0xba6,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadMatch.cpp",
        //     0xba6,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(Log, "Cannot activate Olympiad round skill %d: user is missing.", skillId);
        return;
    }

    const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(CSkillKey(skillId, 1, 0), nullptr);
    if (skill == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]no skill: %d",
        //     L"OlympiadMatch.cpp",
        //     0xbac,
        //     skillId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]no skill: %d",
        //     L"OlympiadMatch.cpp",
        //     0xbac,
        //     skillId)
        L2_LOG_ERROR(
            Log, "Cannot activate Olympiad round skill %d: level 1, sublevel 0 is absent from the skill database.", skillId
        );
        return;
    }
    if (!skill->ActivateSkill(user, user, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]failed to active skill: %d",
        //     L"OlympiadMatch.cpp",
        //     0xbb2,
        //     skillId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]failed to active skill: %d",
        //     L"OlympiadMatch.cpp",
        //     0xbb2,
        //     skillId)
        L2_LOG_ERROR(
            Log, "Olympiad round skill %d activation failed for character DB ID %u.", skillId,
            static_cast<unsigned int>(user->GetDBID())
        );
        return;
    }

    SkillUseInfo use;
    use.m_skillID = skill->m_nSkillId;
    use.m_skillLevel = skill->m_nLevel;
    use.m_skillSubLevel = skill->m_nSubLevel;
    use.m_castRange = 0;
    use.m_type = 0;
    use.m_param1 = 0;
    int zero1 = 0;
    int zero2 = 0;
    int zero3 = 0;
    int zero4 = 0;
    int zero5 = 0;
    int zero6 = 0;
    DefaultIOBuffer* const packet =
        user->MakePacketForMagicSkillUse(zero1, user, use, zero2, zero3, zero4, zero5, zero6, nullptr);
    g_ptlUserMap->BroadcastToNeighborDist(user, 0x5000, user->GetPos(), user->GetBroadcastDist(0x1000), packet);
    packet->Release();
}

void WOlympiadMatch::MatchRule3Rounds::ActivateDefaultSkillsTo(const OlympiadTeamEnum team) const {
    // Current L2Server 0x008df740.
    L2_GUARD_FUNCTION();
    static constexpr int skills[5]{
        0x7e9b,
        0x7e9c,
        0x7e9d,
        0x7e9e,
        0x7e9f,
    };
    CSPointer<User> user = GetUser(team);
    if (!user) {
        return;
    }
    for (const int skillId : skills) {
        ActivateSkillTo(user.get(), skillId);
    }
}

void WOlympiadMatch::MatchRule3Rounds::BroadcastRoundResult(const RoundResult result) const {
    // Current L2Server 0x008df828.
    L2_GUARD_FUNCTION();
    CSPointer<User> team1 = GetUser(OTE_TEAM1);
    CSPointer<User> team2 = GetUser(OTE_TEAM2);
    switch (result) {
        case RR_TEAM1_WIN:
            if (team1) {
                team1->SendSystemMessage(SystemMessageId::HIDDEN_OLYMPIAD_VICTORY);
            }
            if (team2) {
                team2->SendSystemMessage(SystemMessageId::HIDDEN_OLYMPIAD_DEFEAT);
            }
            BroadcastMessageSToObservers(0x5d9, GetName(OTE_TEAM1));
            break;
        case RR_TEAM2_WIN:
            if (team1) {
                team1->SendSystemMessage(SystemMessageId::HIDDEN_OLYMPIAD_DEFEAT);
            }
            if (team2) {
                team2->SendSystemMessage(SystemMessageId::HIDDEN_OLYMPIAD_VICTORY);
            }
            BroadcastMessageSToObservers(0x5d9, GetName(OTE_TEAM2));
            break;
        case RR_DRAW:
        case RR_PENALTY:
            if (team1) {
                team1->SendSystemMessage(SystemMessageId::HIDDEN_OLYMPIAD_DRAW);
            }
            if (team2) {
                team2->SendSystemMessage(SystemMessageId::HIDDEN_OLYMPIAD_DRAW);
            }
            BroadcastMessageToObservers(0x5da);
            break;
        case RR_NONE:
            BroadcastMessage(0x5d5);
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"OlympiadMatch.cpp",
            //     0xc5e)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"OlympiadMatch.cpp",
            //     0xc5e)
            L2_LOG_ERROR(Log, "Cannot broadcast Olympiad round result %d: unsupported result.", static_cast<int>(result));
            break;
    }
}

void WOlympiadMatch::MatchRule3Rounds::TeleportUsersToStartPos() const {
    // Current L2Server ICF owner 0x008db6a0.
    if (m_pMatch->m_pInZone == nullptr || m_pMatch->m_pInZone->GetConst() == nullptr) {
        return;
    }
    for (int index = 0; index < 2; ++index) {
        CSPointer<User> user = GetUser(static_cast<OlympiadTeamEnum>(index));
        if (!user || user->m_pWOlympiadUser == nullptr) {
            continue;
        }
        user->DoNothing();
        const FVector position =
            m_pMatch->m_pInZone->GetConst()->GetStartPos(user->m_pWOlympiadUser->GetStartPositionIndex());
        if (CWorld::Inst().MoveTo(user.get(), position, false)) {
            user->TeleportToLocation(
                static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
                m_pMatch->m_pInZone->Id(), 0x10000, TeleportSourceType::MOVEAROUND, nullptr, 0
            );
        }
    }
}

void WOlympiadMatch::MatchRule3Rounds::ChangeState(const State state) {
    // Current L2Server 0x008e0798.
    m_tStateChanged = m_pMatch->m_tNow;
    if (state == SM_ROUND) {
        m_anLastDamages[OTE_TEAM1] = m_pMatch->m_TeamHolder[OTE_TEAM1].team.GetDamage();
        m_anLastDamages[OTE_TEAM2] = m_pMatch->m_TeamHolder[OTE_TEAM2].team.GetDamage();
        m_CurrentResult.store(RR_NONE);
        std::memset(m_abInterRoundProcessed, 0, sizeof(m_abInterRoundProcessed));
        ++m_nCurrentRound;
        BroadcastRound();
    } else if (state == SM_INTER_ROUND) {
        DispelDebuffs();
        if (m_nCurrentRound == 2) {
            ResetSkillReuseTime();
        }
    }
    m_eState = state;
    BroadcastInfo();
}

void WOlympiadMatch::MatchRule3Rounds::ActivateSkillTo(const OlympiadTeamEnum team, const int skillId) const {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUser(team);
    if (user) {
        ActivateSkillTo(user.get(), skillId);
    }
}

void WOlympiadMatch::MatchRule3Rounds::OnStatusChanged(const WFieldStatus::type status) {
    // Current L2Server 0x008e0e98.
    if (status == WFieldStatus::WAIT_MATCH) {
        m_eState = SM_NONE;
        m_nCurrentRound = 0;
        std::memset(m_aRoundResults, 0, sizeof(m_aRoundResults));
        std::memset(m_anWinCounts, 0, sizeof(m_anWinCounts));
        std::memset(m_anRewardCounts, 0, sizeof(m_anRewardCounts));
        std::memset(m_anLastDamages, 0, sizeof(m_anLastDamages));
    } else if (status == WFieldStatus::PROCESS_MATCH) {
        ChangeState(SM_ROUND);
    } else if (status == WFieldStatus::FINISH_MATCH) {
        RewardCountByGameResult();
    }
}

void WOlympiadMatch::MatchRule3Rounds::ShowRoundWinnerEffect(const RoundResult result) const {
    // Current L2Server 0x008e0f18.
    L2_GUARD_FUNCTION();
    if (result == RR_TEAM1_WIN) {
        ActivateSkillTo(OTE_TEAM1, 0x7ea1);
    } else if (result == RR_TEAM2_WIN) {
        ActivateSkillTo(OTE_TEAM2, 0x7ea1);
    }
}

void WOlympiadMatch::MatchRule3Rounds::RoundEnd(const RoundResult result, const bool force) {
    // Current L2Server 0x008e11cc.
    L2_GUARD_FUNCTION();
    m_aRoundResults[m_nCurrentRound] = result;
    AddWinCount(result);
    AddRewardCount(result);
    ShowRoundWinnerEffect(result);
    BroadcastMessage(0x1446 - static_cast<int>(force));
    BroadcastRoundResult(result);
    if (!IsMatchEnd()) {
        ChangeState(SM_INTER_ROUND);
        return;
    }
    MatchEnd();
    m_pMatch->ChangeStatus(WFieldStatus::FINISH_MATCH, true);
}

void WOlympiadMatch::MatchRule3Rounds::OnTickProcess(int) {
    // Current L2Server 0x008e1414.
    L2_GUARD_FUNCTION();
    const int elapsed = m_pMatch->m_tNow - m_tStateChanged;
    if (!IsAvailableInZone(m_pMatch->m_pInZone)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]have no inzone: %d",
        //     L"OlympiadMatch.cpp",
        //     0x9e9,
        //     m_pMatch->m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]have no inzone: %d",
        //     L"OlympiadMatch.cpp",
        //     0x9e9,
        //     m_pMatch->m_nFieldId)
        L2_LOG_ERROR(
            Log, "Ending Olympiad match on field %d without a game result: instant zone is unavailable or being deleted.",
            m_pMatch->m_nFieldId
        );
        m_pMatch->m_eGameResult = GS_NOT_GAME;
        m_pMatch->ChangeStatus(WFieldStatus::FINISH_MATCH, true);
        return;
    }

    if (m_eState == SM_ROUND) {
        const RoundResult earlyResult = CheckRoundEnd();
        if (earlyResult != RR_NONE) {
            RoundEnd(earlyResult, false);
            return;
        }
        if (elapsed > 100 || m_pMatch->m_bDoNextStep) {
            const bool forced = m_pMatch->m_bDoNextStep;
            std::u16string parameter = m_pMatch->m_sDoNextStepParam;
            m_pMatch->m_bDoNextStep = false;
            m_pMatch->m_sDoNextStepParam.assign(std::u16string_view{});
            RoundResult result = RR_NONE;
            if (forced) {
                result = ConvertFromString(parameter);
            }
            if (!forced || result == RR_NONE) {
                result = CheckRoundResult();
            }
            RoundEnd(result, true);
            return;
        }
        BroadcastCountDown(0x77b, 100 - elapsed);
        return;
    }

    if (IsMatchEnd()) {
        MatchEnd();
        m_pMatch->ChangeStatus(WFieldStatus::FINISH_MATCH, true);
        return;
    }
    if (elapsed > 20 || m_pMatch->m_bDoNextStep) {
        m_pMatch->m_bDoNextStep = false;
        ChangeState(SM_ROUND);
        return;
    }
    if (elapsed > 3 && !m_abInterRoundProcessed[IRPS_ACTIVATE_DEFAULT_SKILL]) {
        ActivateDefaultSkillsTo(OTE_TEAM1);
        ActivateDefaultSkillsTo(OTE_TEAM2);
        m_abInterRoundProcessed[IRPS_ACTIVATE_DEFAULT_SKILL] = true;
        return;
    }
    if (elapsed > 4 && !m_abInterRoundProcessed[IRPS_ACTIVATE_ROUND_RESULT_SKILL]) {
        RestorePlayersHPMPCP(m_nCurrentRound, m_aRoundResults[m_nCurrentRound]);
        m_abInterRoundProcessed[IRPS_ACTIVATE_ROUND_RESULT_SKILL] = true;
        return;
    }
    if (elapsed > 5 && !m_abInterRoundProcessed[IRPS_TELEPORT_TO_START_POS]) {
        TeleportUsersToStartPos();
        m_abInterRoundProcessed[IRPS_TELEPORT_TO_START_POS] = true;
        return;
    }
    BroadcastCountDown(0xeab, 20 - elapsed);
}

WOlympiadMatch::WOlympiadMatch()
    // Current L2Server 0x008e26d4. The target leaves
    // m_tStatusChanged/m_tPrev/m_tNow and beginTime untouched here.
    :
    m_nStatus(WFieldStatus::NO_MATCH),
    m_nMessageRegistered(0),
    m_eGameResult(GS_NOT_GAME),
    m_bBeginTimeSet(false),
    m_alignment0041To0044{},
    m_nFieldId(-1),
    m_bCheck1SecElapsed(false),
    m_alignment00a9To00ac{},
    m_GameRule(GRT_CLASSLESS),
    m_LivingPlayerCount{},
    penaltyChecker{GVC_NONE, GVC_NONE},
    m_TeamHolder{},
    m_bFastTesting(false),
    m_alignment01c5To01c8{},
    m_pInZone(nullptr),
    m_alignment01e4To01e8{},
    m_pMatchRule(nullptr),
    m_bDoNextStep(false),
    m_alignment01f1To01f8{},
    m_bHasGivenPanelty(false),
    m_alignment0221To0228{} {
    L2_GUARD_FUNCTION();
}

WOlympiadMatch::WOlympiadMatch(const WOlympiadMatch& other) :
    CIOObject(other),
    m_nStatus(other.m_nStatus),
    m_nMessageRegistered(other.m_nMessageRegistered),
    m_tStatusChanged(other.m_tStatusChanged),
    m_tPrev(other.m_tPrev),
    m_tNow(other.m_tNow),
    m_eGameResult(other.m_eGameResult),
    m_bBeginTimeSet(other.m_bBeginTimeSet),
    m_alignment0041To0044{},
    m_nFieldId(other.m_nFieldId),
    m_ObserverEntry(other.m_ObserverEntry),
    m_MessageQueue(other.m_MessageQueue),
    m_bCheck1SecElapsed(other.m_bCheck1SecElapsed),
    m_alignment00a9To00ac{},
    m_GameRule(other.m_GameRule),
    m_LivingPlayerCount{other.m_LivingPlayerCount[0], other.m_LivingPlayerCount[1]},
    penaltyChecker{other.penaltyChecker[0], other.penaltyChecker[1]},
    m_TeamHolder{other.m_TeamHolder[0], other.m_TeamHolder[1]},
    beginTime(other.beginTime),
    m_bFastTesting(other.m_bFastTesting),
    m_alignment01c5To01c8{},
    m_pInZone(other.m_pInZone),
    m_Broadcaster(other.m_Broadcaster),
    m_alignment01e4To01e8{},
    m_pMatchRule(other.m_pMatchRule),
    m_bDoNextStep(other.m_bDoNextStep),
    m_alignment01f1To01f8{},
    m_sDoNextStepParam(other.m_sDoNextStepParam),
    m_bHasGivenPanelty(other.m_bHasGivenPanelty),
    m_alignment0221To0228{} {}

WOlympiadMatch::~WOlympiadMatch() {
    // Current L2Server 0x008e12f0.
    m_pInZone = nullptr;
    delete m_pMatchRule;
    m_pMatchRule = nullptr;
}

void WOlympiadMatch::Init(const int fieldId) {
    // Current L2Server 0x008de0b4.
    L2_GUARD_FUNCTION();
    const int instantZoneTypeId = m_pInZone == nullptr ? 0 : m_pInZone->GetInstantZoneTypeID();
    const int instantZoneId = m_pInZone == nullptr ? 0 : static_cast<int>(m_pInZone->Id());
    WOlympiadServiceUtil::SetFieldInfo(fieldId, FIELD_NONE, 0, 0, GRT_MAX, instantZoneTypeId, instantZoneId);
    m_tNow = 0;
    m_bDoNextStep = false;
    m_pMatchRule = new MatchRule3Rounds(this);
    AddTimer(200, 0);
}

void WOlympiadMatch::TimerExpired(const int id) {
    // Current L2Server 0x008e2544.
    L2_GUARD_FUNCTION();
    if (id == 0x3e9) {
        if (m_nStatus == WFieldStatus::WAIT_MATCH) {
            SetDoor(true);
        }
        return;
    }
    if (id == 0x3ea) {
        if (m_nStatus == WFieldStatus::WAIT_MATCH) {
            RestorePlayers(true);
        }
        return;
    }

    m_tPrev = m_tNow;
    m_tNow = runtime::crt::Time32Now();
    m_bCheck1SecElapsed = m_tPrev != m_tNow;
    const int elapsed = m_tNow - m_tStatusChanged;
    const std::int64_t delay =
        m_nStatus == WFieldStatus::START_MATCH || m_nStatus == WFieldStatus::PROCESS_MATCH ? 200 : 1000;

    ProcessMessage();
    switch (m_nStatus) {
        case WFieldStatus::PREPARE_MATCH:
            StatusPrepareMatch(elapsed);
            break;
        case WFieldStatus::RESPONSE_WAITING:
            StatusResponseWaiting(elapsed);
            break;
        case WFieldStatus::WAIT_MATCH:
            StatusWaitMatch(elapsed);
            break;
        case WFieldStatus::START_MATCH:
            StatusStartMatch();
            break;
        case WFieldStatus::PROCESS_MATCH:
            StatusProcessMatch(elapsed);
            break;
        case WFieldStatus::FINISH_MATCH:
            StatusFinishMatch();
            break;
        case WFieldStatus::CLEAR_MATCH:
            StatusClearMatch(elapsed);
            break;
        default:
            break;
    }
    AddTimer(delay, 0);
}

bool WOlympiadMatch::CanAttack() const {
    // Current L2Server 0x008dafcc.
    return m_pMatchRule->CanAttack();
}

bool WOlympiadMatch::CanRegister() const {
    // Current L2Server 0x008dafc0.
    return m_nStatus == WFieldStatus::NO_MATCH;
}

WFieldStatus::type WOlympiadMatch::GetStatus() const { return m_nStatus; }

void WOlympiadMatch::SetFieldId(const int fieldId) { m_nFieldId = fieldId; }

int WOlympiadMatch::GetFieldId() const { return m_nFieldId; }

void WOlympiadMatch::SetInZone(CInstantZone* const inZone) {
    // Current L2Server 0x008dafdc.
    m_pInZone = inZone;
}

CInstantZone* WOlympiadMatch::GetInZone() const {
    // Current L2Server 0x008dafe4.
    return m_pInZone;
}

void WOlympiadMatch::SetWaitMatchTimer() {
    // Current L2Server 0x008da8ac: fast testing uses seven seconds and the
    // normal route uses fifty-two seconds for both delayed callbacks.
    L2_GUARD_FUNCTION();
    const std::int64_t delay = static_cast<std::int64_t>(m_bFastTesting ? 7 : 52) * 1000;
    AddTimer(delay, 0x3e9);
    AddTimer(delay, 0x3ea);
}

void WOlympiadMatch::IncreaseMatchCount() {
    // Current L2Server 0x008da980.
    L2_GUARD_FUNCTION();
    m_TeamHolder[0].team.IncreaseMatchCount();
    m_TeamHolder[1].team.IncreaseMatchCount();
}

void WOlympiadMatch::RestorePlayers(const bool force) {
    // Current L2Server 0x008daa90.
    L2_GUARD_FUNCTION();
    m_TeamHolder[0].team.RestorePlayers(force);
    m_TeamHolder[1].team.RestorePlayers(force);
}

void WOlympiadMatch::FreezeAll() {
    // Current L2Server 0x008dab4c.
    L2_GUARD_FUNCTION();
    m_TeamHolder[0].team.Freeze();
    m_TeamHolder[1].team.Freeze();
}

void WOlympiadMatch::StatusProcessMatch(const int elapsedTime) {
    // Current L2Server 0x008dac00.
    L2_GUARD_FUNCTION();
    m_pMatchRule->OnTickProcess(elapsedTime);
}

void WOlympiadMatch::SendMatchResult() {
    // Current L2Server 0x008dacac.
    L2_GUARD_FUNCTION();
    m_pMatchRule->BroadcastMatchResult();
}

bool WOlympiadMatch::AsyncUpdateAllPlayers(const std::uint32_t sid) {
    // Current L2Server 0x008dad54.
    L2_GUARD_FUNCTION();
    if (m_nStatus != WFieldStatus::PROCESS_MATCH) {
        return false;
    }
    m_TeamHolder[0].team.UpdateStatus(this, static_cast<int>(sid));
    m_TeamHolder[1].team.UpdateStatus(this, static_cast<int>(sid));
    return true;
}

void WOlympiadMatch::SendToPlayers(const S_EX_PROTOCOL protocol, const network::IPacket& packet) {
    // Current L2Server 0x008dae20.
    L2_GUARD_FUNCTION();
    m_TeamHolder[0].team.Send(protocol, packet);
    m_TeamHolder[1].team.Send(protocol, packet);
}

void WOlympiadMatch::SendVToPlayers(const char* const format, va_list arguments) {
    // Current L2Server 0x008daef0.
    L2_GUARD_FUNCTION();
    va_list first;
    va_copy(first, arguments);
    m_TeamHolder[0].team.SendV(format, first);
    va_end(first);
    va_list second;
    va_copy(second, arguments);
    m_TeamHolder[1].team.SendV(format, second);
    va_end(second);
}

int WOlympiadMatch::GetInzoneTypeID() const {
    // Current L2Server 0x008db0c8.
    L2_GUARD_FUNCTION();
    return m_pInZone == nullptr ? 0 : m_pInZone->GetInstantZoneTypeID();
}

void WOlympiadMatch::RegisterMessage(const std::uint8_t message, const int param1, const int param2, const bool param) {
    // Current L2Server 0x008e19e4.
    L2_GUARD_FUNCTION();
    if (m_nFieldId < 0) {
        return;
    }

    const WEntryMessage entry{message, {}, param1, param2, param, {}};
    {
        L2_GUARD(m_MessageQueueLock);
        m_MessageQueue.push(entry);
    }
    runtime::raw_atomic::ExchangeI32(&m_nMessageRegistered, 1);
}

void WOlympiadMatch::_RegisterMessage(const std::uint8_t message, const int param1, const int param2, const bool param) {
    // Current L2Server 0x008e1af0. The caller already owns the queue lock.
    L2_GUARD_FUNCTION();
    if (m_nFieldId < 0) {
        return;
    }

    m_MessageQueue.push(WEntryMessage{message, {}, param1, param2, param, {}});
    runtime::raw_atomic::ExchangeI32(&m_nMessageRegistered, 1);
}

bool WOlympiadMatch::SetConnectedUser(CSPointer<User> user) {
    // Current L2Server 0x008dd4e8.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadMatch.cpp",
        //     0x7f6,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadMatch.cpp",
        //     0x7f6,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Cannot assign player to Olympiad field %d: user is missing.", m_nFieldId);
        return false;
    }

    const int worldId = user->m_data->m_nWorldId;
    if (m_nStatus != WFieldStatus::WAIT_MATCH) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Failed connect: %d, %d, %d",
        //     L"OlympiadMatch.cpp",
        //     0x7fa,
        //     worldId,
        //     static_cast<int>(user->GetDBID()),
        //     m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Failed connect: %d, %d, %d",
        //     L"OlympiadMatch.cpp",
        //     0x7fa,
        //     worldId,
        //     static_cast<int>(user->GetDBID()),
        //     m_nFieldId)
        L2_LOG_WARN(
            Log, "Cannot assign world %d, character DB ID %d to Olympiad field %d: field status %d is not WAIT_MATCH.",
            worldId, static_cast<int>(user->GetDBID()), m_nFieldId, static_cast<int>(m_nStatus)
        );
        return false;
    }

    const std::CDualKey<int, int> key(worldId, static_cast<int>(user->GetDBID()));
    for (int index = 0; index < 2; ++index) {
        WOlympiadFieldTeamHolder& holder = m_TeamHolder[index];
        if (!holder.occupied || !holder.team.IsRegisteredUser(key)) {
            continue;
        }

        holder.team.SetConnectedUser(user);
        (void)holder.team.WaitingGame();
        holder.team.SetPlayer(static_cast<OlympiadTeamEnum>(index), m_nFieldId, m_pInZone);
        WOlympiadServiceUtil::SetFieldInfo(
            m_nFieldId, FIELD_WAIT, static_cast<int>(m_TeamHolder[0].team.GetUserSID()),
            static_cast<int>(m_TeamHolder[1].team.GetUserSID()), m_GameRule, m_pInZone->GetInstantZoneTypeID(),
            static_cast<int>(m_pInZone->Id())
        );
        WOlympiadService::inst().OnGameChanged(m_nFieldId);
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]Failed set user: %d, %d, %d",
    //     L"OlympiadMatch.cpp",
    //     0x817,
    //     worldId,
    //     static_cast<int>(user->GetDBID()),
    //     m_nFieldId)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d]Failed set user: %d, %d, %d",
    //     L"OlympiadMatch.cpp",
    //     0x817,
    //     worldId,
    //     static_cast<int>(user->GetDBID()),
    //     m_nFieldId)
    L2_LOG_WARN(
        Log, "Cannot assign world %d, character DB ID %d to Olympiad field %d: no occupied team contains this participant.",
        worldId, static_cast<int>(user->GetDBID()), m_nFieldId
    );
    return false;
}

bool WOlympiadMatch::AddObserver(const std::uint32_t sid) {
    // Current L2Server 0x008e23d8.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(sid));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadMatch.cpp",
        //     0x15d)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadMatch.cpp",
        //     0x15d)
        L2_LOG_WARN(
            Log, "Olympiad observer object ID %u was not found for field %d; returning success without adding an observer.",
            sid, m_nFieldId
        );
        return true;
    }

    (void)CCursedWeaponMgr::inst();
    if (user->m_nCursedWeaponClassId > 0) {
        return false;
    }
    if (g_PledgeGameSystem.IsRegisteredUser(user.get())) {
        if (CUserSocket* const socket = user->GetSocket()) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_OLYMPIAD_WHILE_REGISTERED);
        }
        return false;
    }

    if (user->m_pWOlympiadUser->WaitingOlympiadObserving()) {
        RegisterMessage(2, static_cast<int>(sid), 0, false);
        return true;
    }
    if (user->m_pWOlympiadUser->GetStatus() != WOlympiadUserStateMachine::OLYMPIAD_OB_WAITING) {
        if (CUserSocket* const socket = user->GetSocket()) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_OLYMPIAD_WHILE_REGISTERED);
        }
        return false;
    }
    return true;
}

void WOlympiadMatch::RemoveObserver(const std::uint32_t sid) {
    // Current L2Server ICF wrapper.
    RegisterMessage(4, static_cast<int>(sid), 0, false);
}

void WOlympiadMatch::UpdateUserStatus(const std::uint32_t sid, const std::uint32_t targetSid) {
    // Current L2Server ICF wrapper.
    RegisterMessage(6, static_cast<int>(sid), static_cast<int>(targetSid), false);
}

void WOlympiadMatch::UpdateSpelledInfo(const std::uint32_t sid) {
    // Current L2Server ICF wrapper.
    RegisterMessage(7, static_cast<int>(sid), 0, false);
}

void WOlympiadMatch::ChangeObserverField(const std::uint32_t sid, const int fieldId) {
    // Current L2Server ICF wrapper.
    if (static_cast<std::uint32_t>(fieldId) < 850U && fieldId != m_nFieldId) {
        RegisterMessage(8, static_cast<int>(sid), fieldId, false);
    }
}

void WOlympiadMatch::RequestPlayersInfo(const std::uint32_t sid) {
    // Current L2Server ICF wrapper.
    RegisterMessage(9, static_cast<int>(sid), 0, false);
}

void WOlympiadMatch::PlayerDied(User* const user) {
    // Current L2Server 0x008db870.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadMatch.cpp",
        //     0x5f9,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadMatch.cpp",
        //     0x5f9,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(Log, "Cannot process player death on Olympiad field %d: user is missing.", m_nFieldId);
        return;
    }
    L2_GUARD(m_FieldStatusLock);
    m_pMatchRule->OnPlayerDied(user);
}

void WOlympiadMatch::DeleteNPCFromCreatureMap() {
    // Current L2Server 0x008dbb30.
    L2_GUARD_FUNCTION();
    if (m_pInZone != nullptr && !m_pInZone->IsDeletingObject() && m_pInZone->m_Deleted == 0) {
        m_pInZone->DeleteNPCFromCreatureMap();
    }
}

void WOlympiadMatch::SetDoor(const bool open) {
    // Current L2Server 0x008dbbe8.
    L2_GUARD_FUNCTION();
    if (m_pInZone != nullptr && !m_pInZone->IsDeletingObject() && m_pInZone->m_Deleted == 0) {
        m_pInZone->SetAllDoorOpenClose(open);
    }
}

bool WOlympiadMatch::AsyncAddObserver(const std::uint32_t sid) {
    // Current L2Server 0x008e0094.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(sid));
    if (!user) {
        return false;
    }
    if (m_pInZone == nullptr || m_pInZone->IsDeletingObject() || m_pInZone->m_Deleted != 0) {
        user->m_pWOlympiadUser->OlympiadStatusReset();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]no inzone: %d",
        //     L"OlympiadMatch.cpp",
        //     0x614,
        //     m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]no inzone: %d",
        //     L"OlympiadMatch.cpp",
        //     0x614,
        //     m_nFieldId)
        L2_LOG_WARN(
            Log, "Cannot add observer object ID %u to Olympiad field %d: instant zone is unavailable or being deleted.", sid,
            m_nFieldId
        );
        return false;
    }
    if (!m_ObserverEntry.AddEntry(sid)) {
        user->m_pWOlympiadUser->OlympiadStatusReset();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]already exists id: %d",
        //     L"OlympiadMatch.cpp",
        //     0x61f,
        //     sid)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]already exists id: %d",
        //     L"OlympiadMatch.cpp",
        //     0x61f,
        //     sid)
        L2_LOG_WARN(
            Log, "Cannot add observer object ID %u to Olympiad field %d: observer entry already exists.", sid, m_nFieldId
        );
        return false;
    }
    if (!user->m_pWOlympiadUser->SetObserver(m_nFieldId, m_pInZone)) {
        user->m_pWOlympiadUser->OlympiadStatusReset();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]already exists id: %d",
        //     L"OlympiadMatch.cpp",
        //     0x62c,
        //     sid)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]already exists id: %d",
        //     L"OlympiadMatch.cpp",
        //     0x62c,
        //     sid)
        L2_LOG_WARN(
            Log,
            "Cannot set observer state for object ID %u on Olympiad field %d; status reset was requested, but the observer "
            "entry remains registered.",
            sid, m_nFieldId
        );
        return false;
    }
    m_pMatchRule->OnObserverAdded(sid);
    user->ClearSummonReserved();
    return true;
}

void WOlympiadMatch::AsyncUpdateUserStatus(const std::uint32_t sid, const std::uint32_t targetSid) {
    // Current L2Server 0x008e0264.
    L2_GUARD_FUNCTION();
    if (m_nStatus != WFieldStatus::PROCESS_MATCH) {
        return;
    }
    CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(sid));
    if (!user || !user->m_pWOlympiadUser->IsPlayer()) {
        return;
    }

    const int teamId = user->m_pWOlympiadUser->GetTeamId();
    const int objectId = static_cast<int>(user->m_nId);
    const int classId = static_cast<int>(user->m_data->m_nClass);
    const int hp = static_cast<int>(user->m_data->m_dHP);
    const int maxHp = static_cast<int>(user->m_data->m_dBaseHP);
    const int cp = static_cast<int>(user->m_data->m_dCP);
    const int maxCp = static_cast<int>(user->m_data->m_dBaseCP);
    if (targetSid == 0U) {
        Broadcast("chcdSddddd", 0xfe, 0x7b, teamId, objectId, user->GetName(), classId, hp, maxHp, cp, maxCp);
        return;
    }

    CSPointer<User> target = g_UserMap.FindObject(static_cast<int>(targetSid));
    if (target && target->m_pWOlympiadUser->IsObserver()) {
        target->GetSocket()->Send(
            "chcdSddddd", 0xfe, 0x7b, teamId, objectId, user->GetName(), classId, hp, maxHp, cp, maxCp
        );
    }
}

void WOlympiadMatch::AsyncUpdateSpelledInfo(const std::uint32_t sid, const std::uint32_t targetSid) {
    // Current L2Server 0x008e0484.
    L2_GUARD_FUNCTION();
    if (m_nStatus != WFieldStatus::PROCESS_MATCH) {
        return;
    }
    CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(sid));
    if (!user || !user->m_pWOlympiadUser->IsPlayer()) {
        return;
    }

    std::array<char, 0x1000> buffer{};
    int length = static_cast<int>(buffer.size());
    const int count = user->PrepareSpelledInfoChangedBuffer(buffer.data(), length);
    if (targetSid == 0U) {
        Broadcast("chddb", 0xfe, 0x7c, static_cast<int>(sid), count, length, buffer.data());
        return;
    }

    CSPointer<User> target = g_UserMap.FindObject(static_cast<int>(targetSid));
    if (target && target->m_pWOlympiadUser->IsObserver()) {
        target->GetSocket()->Send("chddb", 0xfe, 0x7c, static_cast<int>(sid), count, length, buffer.data());
    }
}

void WOlympiadMatch::AsyncChangeObserverField(const std::uint32_t sid, const int fieldId) {
    // Current L2Server 0x008e0db4.
    L2_GUARD_FUNCTION();
    m_ObserverEntry.RemoveEntry(sid);
    (new COlympiadObserverChangeFieldMessage(sid, fieldId))->Post(0);
}

void WOlympiadMatch::AsyncEscapeObserver(const std::uint32_t sid) {
    // Current L2Server 0x008e0c48.
    L2_GUARD_FUNCTION();
    m_ObserverEntry.RemoveEntry(sid);
    CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(sid));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d",
        //     L"OlympiadMatch.cpp",
        //     0x648,
        //     static_cast<int>(sid))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d",
        //     L"OlympiadMatch.cpp",
        //     0x648,
        //     static_cast<int>(sid))
        L2_LOG_WARN(
            Log,
            "Observer object ID %u was removed from Olympiad field %d, but the user was not found; return to origin was not "
            "requested.",
            sid, m_nFieldId
        );
        return;
    }
    user->m_pWOlympiadUser->EscapeFromOlympiad();
    WOlympiadService::inst().ReturnToOrigin(user);
}

void WOlympiadMatch::Broadcast(const char* const format, ...) {
    // Current L2Server 0x008ded74.
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    va_list observers;
    va_copy(observers, arguments);
    m_ObserverEntry.Broadcast(format, observers);
    va_end(observers);
    SendVToPlayers(format, arguments);
    va_end(arguments);
}

void WOlympiadMatch::Broadcast(const S_EX_PROTOCOL protocol, const network::IPacket& packet) {
    // Current L2Server 0x008decfc.
    L2_GUARD_FUNCTION();
    m_ObserverEntry.Broadcast(protocol, packet);
    SendToPlayers(protocol, packet);
}

void WOlympiadMatch::ProcessMessage() {
    // Current L2Server 0x008e0fd4. The target first consumes the one-bit
    // registration flag, then drains the protected queue one entry at a
    // time and calls exactly the six live message codes.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::CompareExchangeI32(&m_nMessageRegistered, 0, 1) != 1) {
        return;
    }

    for (;;) {
        WEntryMessage message{};
        {
            L2_GUARD(m_MessageQueueLock);
            if (m_MessageQueue.empty()) {
                break;
            }
            message = m_MessageQueue.front();
            m_MessageQueue.pop();
        }

        switch (message.byMessage) {
            case 2:
                AsyncAddObserver(static_cast<std::uint32_t>(message.nParam1));
                break;
            case 4:
                AsyncEscapeObserver(static_cast<std::uint32_t>(message.nParam1));
                break;
            case 6:
                AsyncUpdateUserStatus(static_cast<std::uint32_t>(message.nParam1), 0);
                break;
            case 7:
                AsyncUpdateSpelledInfo(static_cast<std::uint32_t>(message.nParam1), 0);
                break;
            case 8:
                AsyncChangeObserverField(static_cast<std::uint32_t>(message.nParam1), message.nParam2);
                break;
            case 9:
                AsyncUpdateAllPlayers(static_cast<std::uint32_t>(message.nParam1));
                break;
            default:
                break;
        }
    }
}

void WOlympiadMatch::SendToPlayers(const char* const format, ...) {
    // Current L2Server 0x008db98c.
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendVToPlayers(format, arguments);
    va_end(arguments);
}

bool WOlympiadMatch::PrepareMatch(
    const GameRuleType rule, WOlympiadTeam* const team1, WOlympiadTeam* const team2, const bool fastTesting
) {
    // Current L2Server 0x008dd0f0.
    L2_GUARD_FUNCTION();
    if (team1 == nullptr || team2 == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadMatch.cpp",
        //     team1 == nullptr ? 0x133 : 0x134,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadMatch.cpp",
        //     team1 == nullptr ? 0x133 : 0x134,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        L2_LOG_ERROR(
            Log, "Cannot prepare Olympiad field %d: first team available %s, second team available %s.", m_nFieldId,
            team1 != nullptr ? "yes" : "no", team2 != nullptr ? "yes" : "no"
        );
        return false;
    }
    if (m_TeamHolder[0].occupied || m_TeamHolder[1].occupied) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Team Holder is already occupied: %d, %d, %d, %d",
        //     L"OlympiadMatch.cpp",
        //     0x13d,
        //     m_TeamHolder[0].occupied,
        //     m_TeamHolder[1].occupied,
        //     static_cast<int>(rule),
        //     fastTesting)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Team Holder is already occupied: %d, %d, %d, %d",
        //     L"OlympiadMatch.cpp",
        //     0x13d,
        //     m_TeamHolder[0].occupied,
        //     m_TeamHolder[1].occupied,
        //     static_cast<int>(rule),
        //     fastTesting)
        L2_LOG_ERROR(
            Log,
            "Cannot prepare Olympiad field %d: team slots already occupied (first %d, second %d, rule %d, fast testing %d).",
            m_nFieldId, static_cast<int>(m_TeamHolder[0].occupied), static_cast<int>(m_TeamHolder[1].occupied),
            static_cast<int>(rule), static_cast<int>(fastTesting)
        );
        return false;
    }

    m_TeamHolder[0].occupied = true;
    m_TeamHolder[0].team.Set(team1);
    m_TeamHolder[0].team.ObserverEnd();
    m_TeamHolder[0].team.SetFieldID(m_nFieldId);
    m_TeamHolder[1].occupied = true;
    m_TeamHolder[1].team.Set(team2);
    m_TeamHolder[1].team.ObserverEnd();
    m_TeamHolder[1].team.SetFieldID(m_nFieldId);
    m_GameRule = rule;
    m_bFastTesting = fastTesting;
    ChangeStatus(WFieldStatus::PREPARE_MATCH, true);
    return true;
}

void WOlympiadMatch::CalcPenalty() {
    // Current L2Server 0x008dba30.
    L2_GUARD_FUNCTION();
    WOlympiadGameResultInfo result{};
    result.fieldId = m_nFieldId;
    result.elapsedTime = 0;
    result.gameRule = m_GameRule;
    if (penaltyChecker[0] > GVC_NONE) {
        m_TeamHolder[0].team.PayPenalty(m_TeamHolder[1].team, result, penaltyChecker[0]);
    }
    if (penaltyChecker[1] > GVC_NONE) {
        m_TeamHolder[1].team.PayPenalty(m_TeamHolder[0].team, result, penaltyChecker[1]);
    }
    penaltyChecker[0] = GVC_NONE;
    penaltyChecker[1] = GVC_NONE;
}

void WOlympiadMatch::ResetPenalty() {
    m_bHasGivenPanelty = false;
    penaltyChecker[0] = GVC_NONE;
    penaltyChecker[1] = GVC_NONE;
}

void WOlympiadMatch::GivePenalty(const GameValidCondition condition) {
    m_bHasGivenPanelty = true;
    penaltyChecker[0] = condition;
    penaltyChecker[1] = condition;
}

void WOlympiadMatch::ChangeStatus(const WFieldStatus::type status, bool) {
    // Current L2Server 0x008dc9e0. The force argument is not
    // consulted. Callers use it only to document forced transitions.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_FieldStatusLock);
        if (m_nStatus == status) {
            return;
        }
        m_nStatus = status;
        m_tStatusChanged = runtime::crt::Time32Now();
    }

    m_pMatchRule->OnStatusChanged(status);
    switch (status) {
        case WFieldStatus::NO_MATCH:
            CalcPenalty();
            for (WOlympiadFieldTeamHolder& holder : m_TeamHolder) {
                const std::CDualKey<int, int> key = holder.team.GetMemberKey();
                WOlympiadServiceUtil::SendOSN_STOP(key);
                WOlympiadService::inst().UnregisterEntry(key);
                holder.occupied = false;
            }
            break;
        case WFieldStatus::PREPARE_MATCH:
            ResetPenalty();
            break;
        case WFieldStatus::WAIT_MATCH:
            g_npcserver.Send("cddd", 0x6d, m_nFieldId, static_cast<int>(m_GameRule), 1);
            WOlympiadServiceUtil::SetFieldInfo(
                m_nFieldId, FIELD_WAIT, static_cast<int>(m_TeamHolder[0].team.GetUserSID()),
                static_cast<int>(m_TeamHolder[1].team.GetUserSID()), m_GameRule,
                m_pInZone != nullptr ? m_pInZone->GetInstantZoneTypeID() : 0,
                m_pInZone != nullptr ? static_cast<int>(m_pInZone->Id()) : 0
            );
            WOlympiadService::inst().OnGameChanged(m_nFieldId);
            SetWaitMatchTimer();
            break;
        case WFieldStatus::PROCESS_MATCH:
            g_npcserver.Send("cddd", 0x6d, m_nFieldId, static_cast<int>(m_GameRule), 0);
            WOlympiadServiceUtil::SetFieldInfo(
                m_nFieldId, FIELD_MATCH, static_cast<int>(m_TeamHolder[0].team.GetUserSID()),
                static_cast<int>(m_TeamHolder[1].team.GetUserSID()), m_GameRule,
                m_pInZone != nullptr ? m_pInZone->GetInstantZoneTypeID() : 0,
                m_pInZone != nullptr ? static_cast<int>(m_pInZone->Id()) : 0
            );
            WOlympiadService::inst().OnGameChanged(m_nFieldId);
            break;
        case WFieldStatus::FINISH_MATCH:
            FreezeAll();
            WOlympiadServiceUtil::SetFieldInfo(
                m_nFieldId, FIELD_NONE, static_cast<int>(m_TeamHolder[0].team.GetUserSID()),
                static_cast<int>(m_TeamHolder[1].team.GetUserSID()), m_GameRule,
                m_pInZone != nullptr ? m_pInZone->GetInstantZoneTypeID() : 0,
                m_pInZone != nullptr ? static_cast<int>(m_pInZone->Id()) : 0
            );
            WOlympiadService::inst().OnGameChanged(m_nFieldId);
            break;
        default:
            break;
    }
}

void WOlympiadMatch::CancelMatch(const bool team1Valid, const bool team2Valid) {
    // Current L2Server 0x008dd434.
    if (team1Valid) {
        m_TeamHolder[0].team.SendCancelMatch(GVC_VALID_TEAM, 0);
        m_TeamHolder[0].team.ResetStatus();
        if (!m_TeamHolder[1].team.IsPeerDisconnected()) {
            m_bHasGivenPanelty = true;
            penaltyChecker[1] = GVC_VALID_TEAM;
        }
    } else if (team2Valid) {
        m_TeamHolder[1].team.SendCancelMatch(GVC_VALID_TEAM, 0);
        m_TeamHolder[1].team.ResetStatus();
        if (!m_TeamHolder[0].team.IsPeerDisconnected()) {
            m_bHasGivenPanelty = true;
            penaltyChecker[0] = GVC_VALID_TEAM;
        }
    }
    ChangeStatus(WFieldStatus::NO_MATCH, true);
}

std::pair<bool, WOlympiadMatch::GameResult> WOlympiadMatch::ValidateAllTeamReady() const {
    // Current L2Server 0x008decf4.
    const bool team1 = m_TeamHolder[0].team.IsValidCondition(GVC_VALID_TEAM);
    const bool team2 = m_TeamHolder[1].team.IsValidCondition(GVC_VALID_TEAM);
    if (team1 && team2) {
        return {true, GS_NOT_GAME};
    }
    if (team1) {
        return {false, GS_PLAYER2_LOSE};
    }
    if (team2) {
        return {false, GS_PLAYER1_LOSE};
    }
    return {false, GS_DRAW};
}

bool WOlympiadMatch::ValidateAllTeamReadyForWaitState() const {
    // Current L2Server 0x008dafec. The target mutates its penalty fields
    // through the logically-const match object.
    bool allReady = true;
    auto* const self = const_cast<WOlympiadMatch*>(this);
    for (int index = 0; index < 2; ++index) {
        const bool ready = m_TeamHolder[index].occupied && !m_TeamHolder[index].team.IsDisconnected();
        allReady = allReady && ready;
        if (ready) {
            continue;
        }

        WOlympiadTeam& peer = self->m_TeamHolder[index ^ 1].team;
        peer.SendSystemMessage(SystemMessageId::MATCH_CANCELLED_OPPONENT_LOGGED_OUT);
        if (!peer.IsPeerDisconnected()) {
            self->m_bHasGivenPanelty = true;
            self->penaltyChecker[index] = GVC_VALID_TEAM;
        }
    }
    return allReady;
}

bool WOlympiadMatch::CheckAndChangeStatusToNoneAtResponseWait() {
    // Current L2Server 0x008dd618.
    L2_GUARD_FUNCTION();
    const GameValidCondition team1 = CheckPrepareValidCondition(m_TeamHolder[0].team);
    const GameValidCondition team2 = CheckPrepareValidCondition(m_TeamHolder[1].team);
    if (team1 == GVC_NONE && team2 == GVC_NONE) {
        return false;
    }

    if (!(team1 == GVC_VALID_TEAM && team2 == GVC_VALID_TEAM)) {
        if (team1 != GVC_NONE) {
            m_bHasGivenPanelty = true;
            penaltyChecker[0] = team1;
            m_TeamHolder[1].team.SendCancelMatch(team1, 0);
        }
        if (team2 != GVC_NONE) {
            m_bHasGivenPanelty = true;
            penaltyChecker[1] = team2;
            m_TeamHolder[0].team.SendCancelMatch(team2, 0);
        }
    }
    m_eGameResult = GS_NOT_GAME;
    ChangeStatus(WFieldStatus::NO_MATCH, true);
    return true;
}

bool WOlympiadMatch::CheckAndChangeStatusToFinishAtWait() {
    // Current L2Server 0x008dd04c.
    L2_GUARD_FUNCTION();
    if (!IsAvailableInZone(m_pInZone)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]inzone is not available: %d",
        //     L"OlympiadMatch.cpp",
        //     0x3ed,
        //     m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]inzone is not available: %d",
        //     L"OlympiadMatch.cpp",
        //     0x3ed,
        //     m_nFieldId)
        L2_LOG_ERROR(
            Log,
            "Finishing waiting Olympiad match on field %d without a game result: instant zone is unavailable or being "
            "deleted.",
            m_nFieldId
        );
        m_eGameResult = GS_NOT_GAME;
        ChangeStatus(WFieldStatus::FINISH_MATCH, true);
        return true;
    }

    const GameValidCondition team1 = CheckValidConditionAtWait(m_TeamHolder[0].team);
    const GameValidCondition team2 = CheckValidConditionAtWait(m_TeamHolder[1].team);
    if (team1 == GVC_NONE && team2 == GVC_NONE) {
        return false;
    }
    m_eGameResult = team1 == GVC_NONE ? GS_PLAYER2_LOSE : (team2 == GVC_NONE ? GS_PLAYER1_LOSE : GS_DRAW);
    ChangeStatus(WFieldStatus::FINISH_MATCH, true);
    return true;
}

bool WOlympiadMatch::CheckAndChangeStatusToFinishAfterGameStart() {
    // Current L2Server 0x008dcf70.
    L2_GUARD_FUNCTION();
    const bool team1 = m_TeamHolder[0].team.IsValidCondition(GVC_VALID_TEAM);
    const bool team2 = m_TeamHolder[1].team.IsValidCondition(GVC_VALID_TEAM);
    GameResult result = team1 ? (team2 ? GS_MATCHING : GS_PLAYER2_LOSE) : (team2 ? GS_PLAYER1_LOSE : GS_DRAW);
    if (result == GS_MATCHING && IsAvailableInZone(m_pInZone)) {
        return false;
    }
    if (result == GS_MATCHING) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]have no inzone: %d",
        //     L"OlympiadMatch.cpp",
        //     0x416,
        //     m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]have no inzone: %d",
        //     L"OlympiadMatch.cpp",
        //     0x416,
        //     m_nFieldId)
        L2_LOG_ERROR(
            Log,
            "Finishing active Olympiad match on field %d without a game result: instant zone is unavailable or being "
            "deleted.",
            m_nFieldId
        );
        result = GS_NOT_GAME;
    }
    m_eGameResult = result;
    ChangeStatus(WFieldStatus::FINISH_MATCH, true);
    return true;
}

void WOlympiadMatch::StatusPrepareMatch(const int elapsedTime) {
    // Current L2Server 0x008de3f0.
    L2_GUARD_FUNCTION();
    if (m_pInZone == nullptr) {
        ChangeStatus(WFieldStatus::NO_MATCH, true);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]have no inzone: %d",
        //     L"OlympiadMatch.cpp",
        //     0x2e2,
        //     m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]have no inzone: %d",
        //     L"OlympiadMatch.cpp",
        //     0x2e2,
        //     m_nFieldId)
        L2_LOG_ERROR(
            Log, "Olympiad preparation cancelled on field %d: instant zone is missing; field changed to NO_MATCH.",
            m_nFieldId
        );
        return;
    }
    if (!IsAvailableInZone(m_pInZone)) {
        ChangeStatus(WFieldStatus::NO_MATCH, true);
        m_pInZone = nullptr;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]have deleted Inzone: %d",
        //     L"OlympiadMatch.cpp",
        //     0x2ec,
        //     m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]have deleted Inzone: %d",
        //     L"OlympiadMatch.cpp",
        //     0x2ec,
        //     m_nFieldId)
        L2_LOG_ERROR(
            Log,
            "Olympiad preparation cancelled on field %d: instant zone is being deleted; field changed to NO_MATCH and zone "
            "reference cleared.",
            m_nFieldId
        );
        return;
    }

    if (m_pInZone->GetInstantZoneDoorSize() == 0) {
        m_pInZone->InitDoor();
        m_pInZone->SetAllDoorOpenClose(false);
        m_pInZone->InitArea();
    }
    const int duration = m_bFastTesting ? 7 : 62;
    if (m_bCheck1SecElapsed) {
        const bool disconnected1 = m_TeamHolder[0].team.IsDisconnected();
        const bool disconnected2 = m_TeamHolder[1].team.IsDisconnected();
        if (disconnected1 || disconnected2) {
            CancelMatch(!disconnected1, !disconnected2);
            return;
        }
        m_Broadcaster.Broadcast(m_TeamHolder, duration - elapsedTime);
    }
    if (elapsedTime < duration && !m_bDoNextStep) {
        return;
    }

    m_bDoNextStep = false;
    m_Broadcaster.ResetBroadCastingTimeTable();
    if (!ValidateAllTeamReadyForWaitState()) {
        m_TeamHolder[0].team.ResetStatus();
        m_TeamHolder[1].team.ResetStatus();
        ChangeStatus(WFieldStatus::NO_MATCH, true);
        return;
    }

    if (!WOlympiadServiceUtil::SendOSQ_PREPARE(m_TeamHolder[0].team.GetMemberKey())) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadMatch.cpp",
        //     0x31e)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadMatch.cpp",
        //     0x31e)
        L2_LOG_ERROR(
            Log,
            "Cannot send preparation request for first team on Olympiad field %d; response-wait transition still follows.",
            m_nFieldId
        );
    }
    if (!WOlympiadServiceUtil::SendOSQ_PREPARE(m_TeamHolder[1].team.GetMemberKey())) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadMatch.cpp",
        //     0x323)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadMatch.cpp",
        //     0x323)
        L2_LOG_ERROR(
            Log,
            "Cannot send preparation request for second team on Olympiad field %d; response-wait transition still follows.",
            m_nFieldId
        );
    }
    ChangeStatus(WFieldStatus::RESPONSE_WAITING, true);
}

void WOlympiadMatch::StatusResponseWaiting(const int elapsedTime) {
    // Current L2Server 0x008dd848.
    L2_GUARD_FUNCTION();
    if (CheckAndChangeStatusToNoneAtResponseWait()) {
        m_TeamHolder[0].team.ResetStatus();
        m_TeamHolder[1].team.ResetStatus();
        g_npcserver.Send("cddd", 0x6d, m_nFieldId, static_cast<int>(m_GameRule), 0);
        return;
    }
    if (elapsedTime <= 11 && !m_bDoNextStep) {
        return;
    }

    m_bDoNextStep = false;
    for (int index = 0; index < 2; ++index) {
        if (!WOlympiadServiceUtil::SendOSN_CONNECT(m_TeamHolder[index].team.GetMemberKey())) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"OlympiadMatch.cpp",
            //     index == 0 ? 0x86d : 0x872)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"OlympiadMatch.cpp",
            //     index == 0 ? 0x86d : 0x872)
            L2_LOG_ERROR(
                Log,
                "Cannot send connection notification for team %d on Olympiad field %d; match-wait transition still follows.",
                index + 1, m_nFieldId
            );
        }
    }
    ChangeStatus(WFieldStatus::WAIT_MATCH, true);
    if (CArea* const area = m_pInZone->GetAreaByIndex(0)) {
        m_pInZone->SetAreaOnOff(area, false);
    }
}

void WOlympiadMatch::StatusWaitMatch(const int elapsedTime) {
    // Current L2Server 0x008dfc38.
    L2_GUARD_FUNCTION();
    if (CheckAndChangeStatusToFinishAtWait()) {
        g_npcserver.Send("cddd", 0x6d, m_nFieldId, static_cast<int>(m_GameRule), 0);
        return;
    }

    const int duration = m_bFastTesting ? 17 : 62;
    m_pMatchRule->OnTickWait(elapsedTime);
    if (elapsedTime < duration && !m_bDoNextStep) {
        if (m_bCheck1SecElapsed) {
            constexpr std::array<int, 11> notify{60, 50, 40, 30, 20, 10, 5, 4, 3, 2, 1};
            const int remaining = duration - elapsedTime;
            if (std::find(notify.begin(), notify.end(), remaining) != notify.end()) {
                Broadcast(
                    "chccc", 0x62, SystemMessageId::OLYMPIAD_MATCH_STARTS_IN_SECONDS, 1, 0x14,
                    static_cast<int>(static_cast<char>(remaining))
                );
            }
        }
        return;
    }
    if (m_bDoNextStep) {
        SetDoor(true);
        RestorePlayers(true);
        m_bDoNextStep = false;
    }

    const auto result = ValidateAllTeamReady();
    if (result.first) {
        ChangeStatus(WFieldStatus::START_MATCH, true);
    } else {
        m_eGameResult = result.second;
        ChangeStatus(WFieldStatus::FINISH_MATCH, true);
    }
}

void WOlympiadMatch::StatusStartMatch() {
    // Current L2Server 0x008dd2bc.
    L2_GUARD_FUNCTION();
    beginTime = runtime::crt::Time32Now();
    m_bBeginTimeSet = true;
    if (CheckAndChangeStatusToFinishAfterGameStart()) {
        return;
    }

    m_eGameResult = GS_NOT_GAME;
    m_TeamHolder[0].team.StartGame();
    m_TeamHolder[1].team.StartGame();
    constexpr std::array<int, 3> maxPlayers{3, 1, 1};
    const int count = maxPlayers[static_cast<std::size_t>(m_GameRule)];
    m_LivingPlayerCount[0] = count;
    m_LivingPlayerCount[1] = count;
    if (IsAvailableInZone(m_pInZone)) {
        if (CArea* const area = m_pInZone->GetAreaByIndex(0)) {
            m_pInZone->SetAreaOnOff(area, true);
        }
    }
    ChangeStatus(WFieldStatus::PROCESS_MATCH, true);
    m_TeamHolder[0].team.SendSummonInfo(m_TeamHolder[1].team);
    m_TeamHolder[1].team.SendSummonInfo(m_TeamHolder[0].team);
    AsyncUpdateAllPlayers(0);
}

void WOlympiadMatch::DoFinishJob(const OlympiadTeamEnum winner, const OlympiadTeamEnum loser, const int elapsedTime) {
    // Current L2Server 0x008dfe08.
    L2_GUARD_FUNCTION();
    constexpr std::array<int, 3> messages{SystemMessageId::TEAM_WON_OLYMPIAD_MATCH, SystemMessageId::CHARACTER_WON_MATCH,
        SystemMessageId::CHARACTER_WON_MATCH};
    Broadcast(
        "chccS", 0x62, messages[static_cast<std::size_t>(m_GameRule)], 1, 0x0c,
        m_TeamHolder[winner].team.GetPartyMasterName()
    );

    WOlympiadGameResultInfo result{};
    result.fieldId = m_nFieldId;
    result.elapsedTime = elapsedTime;
    result.gameRule = m_GameRule;
    result.nInzoneType = GetInzoneTypeID();
    result.nWinnerItemCount = m_pMatchRule->GetRewardItemCount(winner);
    result.nLoserItemCount = m_pMatchRule->GetRewardItemCount(loser);
    m_TeamHolder[winner].team.Win(m_TeamHolder[loser].team, result);
}

void WOlympiadMatch::DoFinishJobDraw(const int elapsedTime) {
    // Current L2Server 0x008dff70.
    L2_GUARD_FUNCTION();
    Broadcast("chc", 0x62, SystemMessageId::OLYMPIAD_MATCH_DRAW, 0);
    WOlympiadGameResultInfo result{};
    result.fieldId = m_nFieldId;
    result.elapsedTime = elapsedTime;
    result.gameRule = m_GameRule;
    result.nInzoneType = GetInzoneTypeID();
    result.nWinnerItemCount = m_pMatchRule->GetRewardItemCount(OTE_TEAM1);
    result.nLoserItemCount = m_pMatchRule->GetRewardItemCount(OTE_TEAM2);
    m_TeamHolder[0].team.Draw(m_TeamHolder[1].team, result);
}

void WOlympiadMatch::EscapeAllObserver() {
    // Current L2Server 0x008e0a00.
    L2_GUARD_FUNCTION();
    const std::vector<std::uint32_t> entries = m_ObserverEntry.GetEntries();
    for (const std::uint32_t sid : entries) {
        CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(sid));
        if (!user) {
            continue;
        }
        user->m_pWOlympiadUser->EscapeFromOlympiad();
        WOlympiadService::inst().UnregisterEntry({user->m_data->m_nWorldId, static_cast<int>(user->GetDBID())});
        WOlympiadService::inst().ReturnToOrigin(user);
    }
    m_ObserverEntry.RemoveEntryAll();
}

void WOlympiadMatch::StatusFinishMatch() {
    // Current L2Server 0x008e20a0.
    L2_GUARD_FUNCTION();
    int elapsedTime = 0;
    if (m_bBeginTimeSet) {
        elapsedTime = runtime::crt::Time32Now() - beginTime;
        m_bBeginTimeSet = false;
    }

    if (WOlympiadServiceConst::_MATCHING_POINT_MODE && m_eGameResult != GS_NOT_GAME &&
        WOlympiadServiceUtil::GenerateWinningRate(m_TeamHolder[0].team, m_TeamHolder[1].team) &&
        WOlympiadServiceUtil::UpdateMatchingPoint(
            static_cast<int>(m_eGameResult), m_TeamHolder[0].team, m_TeamHolder[1].team
        )) {
        std::vector<CSPointer<WOlympiadMemberInfo>> members;
        members.push_back(m_TeamHolder[0].team.GetMember());
        members.push_back(m_TeamHolder[1].team.GetMember());
        WOlympiadServiceUtil::SaveMatchPoints(m_nFieldId, members);
    }
    if (m_eGameResult != GS_NOT_GAME) {
        IncreaseMatchCount();
    }

    switch (m_eGameResult) {
        case GS_PLAYER1_LOSE:
            DoFinishJob(OTE_TEAM2, OTE_TEAM1, elapsedTime);
            break;
        case GS_PLAYER2_LOSE:
            DoFinishJob(OTE_TEAM1, OTE_TEAM2, elapsedTime);
            break;
        case GS_DRAW:
        case GS_PENALTY:
            DoFinishJobDraw(elapsedTime);
            break;
        case GS_NOT_GAME:
            m_TeamHolder[0].team.SendSystemMessage(SystemMessageId::MATCH_VOIDED_DUE_TO_ARENA_ERROR);
            m_TeamHolder[1].team.SendSystemMessage(SystemMessageId::MATCH_VOIDED_DUE_TO_ARENA_ERROR);
            EscapeAllObserver();
            break;
        default:
            break;
    }

    if (!g_bSendSummonHPMPInfo) {
        m_TeamHolder[0].team.HideSummonInfo(m_TeamHolder[1].team);
        m_TeamHolder[1].team.HideSummonInfo(m_TeamHolder[0].team);
    }
    RestorePlayers(true);
    m_TeamHolder[0].team.FinishGame(false);
    m_TeamHolder[1].team.FinishGame(false);
    if (g_bUseAutoSoulShotUI) {
        m_TeamHolder[0].team.SavePlayers();
        m_TeamHolder[1].team.SavePlayers();
    }
    Broadcast("ch", 0xfe, 0x2d);
    if (m_eGameResult != GS_NOT_GAME) {
        SendMatchResult();
    }
    if (IsAvailableInZone(m_pInZone)) {
        if (CArea* const area = m_pInZone->GetAreaByIndex(0)) {
            m_pInZone->SetAreaOnOff(area, false);
        }
    }
    ChangeStatus(WFieldStatus::CLEAR_MATCH, true);
}

void WOlympiadMatch::StatusClearMatch(const int elapsedTime) {
    // Current L2Server 0x008e1da8.
    L2_GUARD_FUNCTION();
    if (elapsedTime < 23 && !m_bDoNextStep) {
        if (!m_bCheck1SecElapsed) {
            return;
        }
        if (!IsAvailableInZone(m_pInZone)) {
            m_pInZone = nullptr;
            return;
        }
        constexpr std::array<int, 9> notify{40, 30, 20, 10, 5, 4, 3, 2, 1};
        const int remaining = 22 - elapsedTime;
        if (std::find(notify.begin(), notify.end(), remaining) != notify.end()) {
            SendToPlayers("chccc", 0x62, 0x5db, 1, 0x14, static_cast<int>(static_cast<char>(remaining)));
        }
        return;
    }

    m_bDoNextStep = false;
    m_TeamHolder[0].team.EscapeFromOlympiad();
    m_TeamHolder[1].team.EscapeFromOlympiad();
    EscapeAllObserver();
    ChangeStatus(WFieldStatus::NO_MATCH, true);
    SetDoor(false);
    DeleteNPCFromCreatureMap();
}

int WOlympiadMatch::GetElapsedTime() const { return runtime::crt::Time32Now() - m_tStatusChanged; }

bool WOlympiadMatch::Is1SecElapsed() { return m_bCheck1SecElapsed; }

void WOlympiadMatch::SetBeginTime(const bool set) {
    m_bBeginTimeSet = set;
    if (set) {
        beginTime = runtime::crt::Time32Now();
    }
}

bool WOlympiadMatch::IsBeginTimeSet() const { return m_bBeginTimeSet; }

WOlympiadMatch::GameResult WOlympiadMatch::GetGameState() const { return m_eGameResult; }

void WOlympiadMatch::SetConfig(const bool fastTesting) { m_bFastTesting = fastTesting; }

void WOlympiadMatch::DoNextStep(std::u16string parameter) {
    // Current L2Server 0x008f37e4.
    m_bDoNextStep = true;
    m_sDoNextStepParam = std::move(parameter);
}

bool WOlympiadMatch::HasGivenPanelty() const { return m_bHasGivenPanelty; }

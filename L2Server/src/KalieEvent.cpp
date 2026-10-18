#include "KalieEvent.h"
#include "DB.h"
#include "Default.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/TextRuntime.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include "Shared/src/FHtml.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <string>

bool g_bFreeKalieEvent = false;

namespace {

CEvent_Kalie* GetKalieEvent() {
    CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_KALIE);
    return event == nullptr ? nullptr : static_cast<CEvent_Kalie*>(event);
}

CEvent_Kalie* ActiveKalieEvent() {
    CEvent_Kalie* const event = GetKalieEvent();
    return event != nullptr && event->InEventTime() ? event : nullptr;
}

} // namespace

float CEvent_Kalie::CalculateGoalCnt(const std::int32_t state) {
    // Current L2Server 0x0058e3dc.
    L2_GUARD_FUNCTION();
    const auto position = m_mapKalieInfo.find(state);
    if (position == m_mapKalieInfo.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Fail to find Data in KalieInfoMap. CalculateGoalCnt func. state[%d]",
        //     state)
        // L2_LOG_ERROR(Log,
        //     L"Fail to find Data in KalieInfoMap. CalculateGoalCnt func. state[%d]",
        //     state)
        L2_LOG_WARN(Log, "Kalie event state %d has no configuration; using the fallback goal count of 100.", state);
        return 100.0F;
    }
    return static_cast<float>(runtime::raw_atomic::LoadI32Relaxed(&g_nPlayingUserCount)) * position->second.m_fConstA *
        static_cast<float>(position->second.m_nConstB) +
        10.0F;
}

int CEvent_Kalie::RewardItem(const std::int32_t state) {
    // Current L2Server 0x0058e534.
    L2_GUARD_FUNCTION();
    const auto position = m_mapKalieInfo.find(state);
    if (position == m_mapKalieInfo.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Fail to find Data in KalieInfoMap. RewardItem func. state[%d]",
        //     state)
        // L2_LOG_ERROR(Log,
        //     L"Fail to find Data in KalieInfoMap. RewardItem func. state[%d]",
        //     state)
        L2_LOG_ERROR(Log, "Kalie event state %d has no configuration; reward item lookup returns -1.", state);
        return -1;
    }
    return position->second.m_nItemClassID;
}

int CEvent_Kalie::TheNumberOfToken(const std::int32_t state) {
    // Current L2Server 0x0058e660.
    L2_GUARD_FUNCTION();
    const auto position = m_mapKalieInfo.find(state);
    if (position == m_mapKalieInfo.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Fail to find Data in KalieInfoMap. TheNumberOfToken func. state[%d]",
        //     state)
        // L2_LOG_ERROR(Log,
        //     L"Fail to find Data in KalieInfoMap. TheNumberOfToken func. state[%d]",
        //     state)
        L2_LOG_ERROR(Log, "Kalie event state %d has no configuration; using zero reward tokens.", state);
        return 0;
    }
    return position->second.m_nToken;
}

KalieEvent::KalieEvent() :
    m_nKillNpcCnt(0),
    m_nCurrentState(1),
    m_nProgress(0),
    m_nUserID(0),
    m_nGoalCnt(0),
    m_nRewardTokenCnt(1),
    m_nCurrentRewardItem(0),
    m_nTickCnt(0),
    m_nReward(0),
    m_bEveryReward(false),
    m_bEventStarted(false),
    m_nKalieJackPot(0) {
    // Current L2Server 0x0058e784.
}

KalieEvent::~KalieEvent() = default;

KalieEvent& KalieEvent::Inst() {
    // Current L2Server 0x0058e8d4 has no publication lock.
    L2_GUARD_FUNCTION();
    if (KalieEventInst == nullptr) {
        KalieEventInst = new KalieEvent;
    }
    return *KalieEventInst;
}

void KalieEvent::IncreaseKillNpcCnt() {
    // Current L2Server 0x0058dcc8.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::IncrementI32(&m_nKillNpcCnt);
}

void KalieEvent::SetCurrentState(const std::int32_t state) {
    // Current L2Server 0x0058dd54.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&m_nCurrentState, state);
}

void KalieEvent::ResetState() {
    // Current L2Server 0x0058ddf0.
    const std::int32_t previous = runtime::raw_atomic::LoadI32Relaxed(&m_nCurrentState);
    runtime::raw_atomic::ExchangeI32(&m_nCurrentState, 1);
    CLogFormatter formatter{};
    formatter.WriteLog1(0x2335, nullptr, nullptr, nullptr, nullptr, nullptr, m_nCurrentState, previous, 0, 0, 0, 0, 0);
}

bool KalieEvent::NeedChangeProgress() {
    // Current L2Server 0x0058de50.
    L2_GUARD_FUNCTION();
    if (m_nGoalCnt <= 0) {
        return false;
    }
    return static_cast<double>(m_nProgress + 20) <=
        static_cast<double>(m_nKillNpcCnt) * 100.0 / static_cast<double>(m_nGoalCnt);
}

std::int32_t KalieEvent::ChangeRewardValueToZero() {
    // Current L2Server 0x0058df18.
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::CompareExchangeI32(&m_nReward, 0, 1);
}

std::int32_t KalieEvent::ChangeRewardValueToOne() {
    // Current L2Server 0x0058dfac.
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::ExchangeI32(&m_nReward, 1);
}

void KalieEvent::EveryRewardValueOn() {
    // Current L2Server 0x0058e03c.
    L2_GUARD_FUNCTION();
    m_bEveryReward = true;
}

void KalieEvent::KalieEventStart() {
    // Current L2Server 0x0058e0c8.
    L2_GUARD_FUNCTION();
    AddTimer(1000, 0);
}

void KalieEvent::KalieUIOff(User& user) {
    // Current L2Server 0x0058e174.
    L2_GUARD_FUNCTION();
    user.GetSocket()->Send(
        "chddddd", 0xfe, 0x12f, m_nCurrentState, m_nProgress, m_nCurrentRewardItem, user.GetKalieToken(), 0
    );
}

bool KalieEvent::IsSuccessJackPot() {
    // Current L2Server 0x0058e26c.
    const int result = static_cast<int>(::genrand64_real2() * 100.0 + 1.0);
    if (result >= 5) {
        return false;
    }
    runtime::raw_atomic::StoreI32Relaxed(&m_nReward, 1);
    return true;
}

void KalieEvent::KalieUIOn(User& user) {
    // Current L2Server 0x0058e2a8.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SendChangeFreeEvent);
    user.GetSocket()->Send(
        "chddddd", 0xfe, 0x12f, m_nCurrentState, m_nProgress, m_nCurrentRewardItem, user.GetKalieToken(), 1
    );
}

void KalieEvent::SendRewardItemForEveryUser(User& user) {
    // Current L2Server 0x0058e990.
    L2_GUARD_FUNCTION();
    CEvent_Kalie* const event = ActiveKalieEvent();
    if (event == nullptr) {
        return;
    }
    user.GetSocket()->Send("chddddd", 0xfe, 0x12f, 1, 0, event->RewardItem(1), user.GetKalieToken(), -1);
}

void KalieEvent::SendRewardItemToLuckyUser(User& user) {
    // Current L2Server 0x0058eac0.
    L2_GUARD_FUNCTION();
    if (ActiveKalieEvent() == nullptr) {
        return;
    }

    const int userDbId = static_cast<int>(user.GetDBID());
    CFHTML::StrStrMapType parameters;
    const std::u16string title = CFHTML::MakeHTMLFromStrMap(u"g_event_kalie_message_01.htm", parameters, 0);
    const std::u16string contents = CFHTML::MakeHTMLFromStrMap(u"g_event_kalie_message_00.htm", parameters, 0);
    if (m_nCurrentRewardItem > 0) {
        db.RequestWritePostBirth(userDbId, title.c_str(), contents.c_str(), m_nCurrentRewardItem, 8, 1, 0, 0);
    }
}

void KalieEvent::SendKalieInfo(User& user, const int freeEventState) {
    // Current L2Server 0x0058ecfc.
    L2_GUARD_FUNCTION();
    if (ActiveKalieEvent() == nullptr) {
        return;
    }
    L2_GUARD(m_SendChangeFreeEvent);
    int state = 0;
    if (m_bEventStarted) {
        state = g_bFreeKalieEvent ? 1 : freeEventState;
    }
    user.GetSocket()->Send(
        "chddddd", 0xfe, 0x12f, m_nCurrentState, m_nProgress, m_nCurrentRewardItem, user.GetKalieToken(), state
    );
}

void KalieEvent::BroadCastToAllUserChangeInfo() {
    // Current L2Server 0x0058eea8.
    L2_GUARD_FUNCTION();
    if (ActiveKalieEvent() == nullptr) {
        return;
    }
    CUserBroadcaster::inst().BroadcastToAllUser(
        "chddddd", 0xfe, 0x12f, m_nCurrentState, m_nProgress, m_nCurrentRewardItem, -1, -1
    );
}

void KalieEvent::BroadCastToAllUserEventUIOff() {
    // Current L2Server 0x0058efa8.
    L2_GUARD_FUNCTION();
    if (ActiveKalieEvent() == nullptr) {
        return;
    }
    L2_GUARD(m_SendChangeFreeEvent);
    CUserBroadcaster::inst().BroadcastToAllUser(
        "chddddd", 0xfe, 0x12f, m_nCurrentState, m_nProgress, m_nCurrentRewardItem, -1, 0
    );
}

void KalieEvent::BroadCastToAllUserEventUIOn() {
    // Current L2Server 0x0058f0dc.
    L2_GUARD_FUNCTION();
    if (ActiveKalieEvent() == nullptr) {
        return;
    }
    L2_GUARD(m_SendChangeFreeEvent);
    CUserBroadcaster::inst().BroadcastToAllUser(
        "chddddd", 0xfe, 0x12f, m_nCurrentState, m_nProgress, m_nCurrentRewardItem, -1, 1
    );
}

void KalieEvent::GiveTokenItem(User& user, CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x0058f214.
    L2_GUARD_FUNCTION();
    const int tokenCount = user.GetKalieToken();
    if (tokenCount == 0) {
        user.GetSocket()->SendSystemMessage(SystemMessageId::NO_LUCKY_COINS_AVAILABLE);
        return;
    }

    InventoryParams parameters;
    parameters.nItemClassID = 0x8d10;
    parameters.nCount = tokenCount;
    L2_GUARD(*user.m_lock);
    if (!user.AddItemToInventory(parameters)) {
        return;
    }

    user.SetKalieToken(0);
    packet.Assemble("dd", static_cast<int>(user.GetDBID()), user.GetKalieToken());
    SendKalieInfo(user, -1);
}

void KalieEvent::ProcessGivingTokenItem() {
    // Current L2Server 0x0058f3dc.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (m_UserQueue.empty()) {
        return;
    }

    int missingUsers = 0;
    int firstMissingUserSid = 0;
    CPacketHelperEx<20480> packet;
    int processed = 0;
    while (!m_UserQueue.empty() && processed < 100) {
        const int userSid = m_UserQueue.front();
        CSPointer<User> user = g_UserMap.FindObject(userSid);
        if (!user) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user[sid:%d]",
            //     L"KalieEvent.cpp",
            //     0x19f,
            //     userSid)
            // Noise: Summarize missing users once per drain batch instead of logging each skipped queue entry.
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid user[sid:%d]",
            //     L"KalieEvent.cpp",
            //     0x19f,
            //     userSid)
            if (missingUsers++ == 0) {
                firstMissingUserSid = userSid;
            }
        } else {
            GiveTokenItem(*user.get(), packet);
            user->EndTokenProcress();
        }
        m_UserQueue.pop();
        ++processed;
    }
    // Noise: This drain processes at most 100 entries. Remaining claims stay queued for the next pass.
    // if (!m_UserQueue.empty()) {
    //     // Log.Add(
    //     //     LOG_ERROR,
    //     //     L"[%s][%d] m_UserQueue over MAX_PROCESS_QUEUE",
    //     //     L"KalieEvent.cpp",
    //     //     0x1a9)
    //     L2_LOG_ERROR(Log,
    //         L"[%s][%d] m_UserQueue over MAX_PROCESS_QUEUE",
    //         L"KalieEvent.cpp",
    //         0x1a9)
    // }
    if (missingUsers != 0) {
        L2_LOG_WARN(
            Log,
            "Kalie token claims: skipped %d queue entries whose users are absent from the user map (first SID %u); those "
            "entries were removed.",
            missingUsers, static_cast<unsigned int>(firstMissingUserSid)
        );
    }
    db.RequestUpdateKalieEventForUser(packet);
}

void KalieEvent::SetEventInfo() {
    // Current L2Server 0x0058f674.
    L2_GUARD_FUNCTION();
    CEvent_Kalie* const event = GetKalieEvent();
    if (event != nullptr) {
        m_nGoalCnt = static_cast<int>(event->CalculateGoalCnt(m_nCurrentState));
        m_nCurrentRewardItem = event->RewardItem(m_nCurrentState);
    }
    runtime::raw_atomic::ExchangeI32(&m_nKillNpcCnt, 0);
    runtime::raw_atomic::ExchangeI32(&m_nProgress, 0);
}

void KalieEvent::InitEventInfoForUser() {
    // Current L2Server 0x0058f750.
    L2_GUARD_FUNCTION();
    CEvent_Kalie* const event = GetKalieEvent();
    if (event != nullptr) {
        m_nCurrentRewardItem = event->RewardItem(m_nCurrentState);
    }
}

void KalieEvent::InitGoalCntForEventStart() {
    // Current L2Server 0x0058f810.
    L2_GUARD_FUNCTION();
    CEvent_Kalie* const event = GetKalieEvent();
    if (event != nullptr) {
        m_nGoalCnt = static_cast<int>(event->CalculateGoalCnt(m_nCurrentState));
    }
    runtime::raw_atomic::ExchangeI32(&m_nKillNpcCnt, (m_nProgress * m_nGoalCnt) / 100);
}

void KalieEvent::SetRewardValForBuilder() {
    // Current L2Server 0x0058f8ec.
    L2_GUARD_FUNCTION();
    CEvent_Kalie* const event = GetKalieEvent();
    if (event == nullptr) {
        return;
    }
    runtime::raw_atomic::StoreI32Relaxed(&m_nReward, 1);
    m_bEveryReward = true;
    m_nRewardTokenCnt = event->TheNumberOfToken(m_nCurrentState);
}

void KalieEvent::SetStateForBuilder(const std::int32_t state) {
    // Current L2Server 0x0058f9b8.
    SetCurrentState(state);
    SetEventInfo();
    BroadCastToAllUserChangeInfo();
}

void KalieEvent::FreeKalieOff() {
    // Current L2Server 0x0058f9dc.
    L2_GUARD_FUNCTION();
    g_bFreeKalieEvent = false;
    BroadCastToAllUserEventUIOff();
}

void KalieEvent::FreeKalieOn() {
    // Current L2Server 0x0058fa84.
    L2_GUARD_FUNCTION();
    g_bFreeKalieEvent = true;
    BroadCastToAllUserEventUIOn();
}

void KalieEvent::IncreaseState() {
    // Current L2Server 0x0058fb2c.
    L2_GUARD_FUNCTION();
    CEvent_Kalie* const event = GetKalieEvent();
    if (event == nullptr || m_nCurrentState >= static_cast<std::int32_t>(event->m_mapKalieInfo.size())) {
        return;
    }
    const std::int32_t current = runtime::raw_atomic::IncrementI32(&m_nCurrentState);
    CLogFormatter formatter{};
    formatter.WriteLog1(0x2335, nullptr, nullptr, nullptr, nullptr, nullptr, current, current - 1, 0, 0, 0, 0, 0);
    SetEventInfo();
}

void KalieEvent::IncreaseProgress() {
    // Current L2Server 0x0058fc54.
    L2_GUARD_FUNCTION();
    const int goalCount = m_nGoalCnt == 0 ? 1 : m_nGoalCnt;
    int progress =
        static_cast<int>(((static_cast<double>(m_nKillNpcCnt) * 100.0 / static_cast<double>(goalCount)) / 5.0) * 20.0);
    runtime::raw_atomic::ExchangeI32(&m_nProgress, progress);
    if (m_nProgress >= 100) {
        runtime::raw_atomic::ExchangeI32(&m_nProgress, 0);
    }
    if (m_nProgress == 0) {
        IncreaseState();
    }
    db.RequestUpdateKalieEvent(1, m_nCurrentState, m_nProgress);
    BroadCastToAllUserChangeInfo();
}

void KalieEvent::ResetEvent() {
    // Current L2Server 0x0058fd74.
    L2_GUARD_FUNCTION();
    ResetState();
    if (GetKalieEvent() != nullptr) {
        SetEventInfo();
    }
}

void KalieEvent::TimerExpired(int) {
    // Current L2Server 0x0058fe34.
    L2_GUARD_FUNCTION();
    CEvent_Kalie* const event = GetKalieEvent();
    if (event == nullptr) {
        return;
    }
    if (!event->InEventTime()) {
        AddTimer(1000, 0);
        return;
    }

    ++m_nTickCnt;
    if (m_nTickCnt % 60 == 0) {
        m_bEveryReward = false;
    }
    if (m_nTickCnt % 3600 == 0) {
        g_bFreeKalieEvent = false;
        BroadCastToAllUserEventUIOff();
    }
    if (!m_bEventStarted) {
        m_bEventStarted = true;
        InitGoalCntForEventStart();
        CUserBroadcaster::inst().BroadcastToAllUser("chc", 0x62, 0xf31, 0);
        BroadCastToAllUserEventUIOn();
    }

    if (m_nTickCnt % 180 == 0 && IsSuccessJackPot()) {
        runtime::raw_atomic::StoreI32Relaxed(&m_nReward, 1);
        m_nRewardTokenCnt = event->TheNumberOfToken(m_nCurrentState);
    } else if (NeedChangeProgress()) {
        IncreaseProgress();
    }

    ProcessGivingTokenItem();
    AddTimer(1000, 0);
}

void KalieEvent::SuccesssGiveItemToLuckyUser(User* const user) {
    // Current L2Server 0x00590034.
    L2_GUARD_FUNCTION();
    if (ActiveKalieEvent() == nullptr || user == nullptr || user->GetSocket() == nullptr) {
        return;
    }
    user->GetSocket()->Send("ch", 0xfe, 0x130);
    CUserBroadcaster::inst().BroadcastToAllUser("chccScd", 0x62, 0xf08, 2, 0x0c, user->GetName(), 3, m_nCurrentRewardItem);
    ResetEvent();
    EveryRewardValueOn();
    db.RequestUpdateKalieEvent(1, m_nCurrentState, m_nProgress);
}

void KalieEvent::ResetEventForBuild() {
    // Current L2Server 0x005901bc.
    ResetEvent();
    db.RequestUpdateKalieEvent(1, m_nCurrentState, m_nProgress);
    BroadCastToAllUserChangeInfo();
}

void KalieEvent::IncreaseProgressForbuild() {
    // Current L2Server 0x005901f0.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::AddI32(&m_nProgress, 20) == 0) {
        IncreaseState();
    }
    db.RequestUpdateKalieEvent(1, m_nCurrentState, m_nProgress);
    BroadCastToAllUserChangeInfo();
}

#include "BalthusEvent.h"
#include "DB.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/TextRuntime.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include "Shared/src/Config.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Utils.h"
#include "Shared/src/proto/DBPacket.h"
#include "Shared/src/proto/Stream.h"

#include <algorithm>
#include <cerrno>
#include <ctime>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace {

// Noise: Operator source metadata supplies the filename. Keep the old diagnostic-only constant.
// constexpr wchar_t kBalthusEventSourceFile[] = L"BalthusEvent.cpp"

// Noise: The packet encoder does not set OS last-error/errno. That value cannot explain encoding failure.
// int CurrentBalthusLastError() noexcept {
// #if defined(_WIN32)
//     return static_cast<int>(::GetLastError())
// #else
//     return errno
// #endif
// }

bool LocalTimeNow(std::tm& value) noexcept {
    const std::time_t now = static_cast<std::time_t>(runtime::crt::Time32Now());
#if defined(_WIN32)
    return ::localtime_s(&value, &now) == 0;
#else
    return ::localtime_r(&now, &value) != nullptr;
#endif
}

CEvent_Balthus* ActiveBalthusEvent() {
    CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_BALTHUS);
    return event == nullptr ? nullptr : static_cast<CEvent_Balthus*>(event);
}

} // namespace

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     BalthusEvent::kMemoryClassId ==
//         L2ServerMemory::kBalthusEventClassId)

BalthusEvent::BalthusEvent() :
    m_nDrawMinute{},
    m_nPrevMinute(-1),
    m_nCurrentState(0),
    m_nCurrentProgress(0),
    m_nCurrentRewardItem(0),
    m_nCurrentTokenCount(0),
    m_nRewardDistributeFlag(0),
    m_nTokenDistributeFlag(0),
    m_bEventStarted(false),
    m_bEventRunning(false),
    m_nCurrentStateStarttime(0) {
    // Current L2Server 0x00427000.
}

BalthusEvent::~BalthusEvent() = default;

L2_DEFINE_OBJECT_CLASS_ID(BalthusEvent)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(BalthusEvent)

// Current L2Server 0x00422c54.
L2_DEFINE_OBJECT_ALLOCATOR(BalthusEvent, __GetClassName())

L2_DEFINE_OBJECT_NEW(BalthusEvent)

// Current L2Server 0x004270d4.
L2_DEFINE_OBJECT_TYPED_RELEASE(BalthusEvent)

std::int32_t BalthusEvent::EndRewardGivingForUserTimerExpired() {
    // Current L2Server 0x00422cd8.
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::CompareExchangeI32(&m_nRewardDistributeFlag, 0, 1);
}

std::int32_t BalthusEvent::StartRewardGivingForUserTimerExpired() {
    // Current L2Server 0x00422d70.
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::ExchangeI32(&m_nRewardDistributeFlag, 1);
}

void BalthusEvent::ValidateCurrentProgress() {
    // Current L2Server 0x00422e04.
    L2_GUARD_FUNCTION();
    std::tm local{};
    if (!LocalTimeNow(local)) {
        return;
    }

    m_nCurrentProgress = 0;
    for (const int drawMinute : m_nDrawMinute) {
        if (local.tm_min < drawMinute) {
            break;
        }
        ++m_nCurrentProgress;
    }
}

int BalthusEvent::GetProgressPercentage() {
    // Current L2Server 0x00422ec4.
    L2_GUARD_FUNCTION();
    return std::min((m_nCurrentProgress * 5 + 5) * 4, 100);
}

void BalthusEvent::ValidateEventStatus() {
    // Current L2Server 0x00422f60.
    L2_GUARD_FUNCTION();
    if (m_nCurrentProgress < 0) {
        m_nCurrentProgress = 0;
    }
    if (m_nCurrentProgress > 4) {
        m_nCurrentProgress = 4;
        m_bEventRunning = false;
    }
}

void BalthusEvent::ShuffleProgressInterval() {
    // Current L2Server 0x00423514.
    L2_GUARD_FUNCTION();
    const int mode = static_cast<int>(genrand64_real1() * 3.0);
    if (mode == 0) {
        int upperMinute = 12;
        for (int& drawMinute : m_nDrawMinute) {
            drawMinute = upperMinute - static_cast<int>(genrand64_real1() * 7.0 + 1.0);
            upperMinute += 12;
        }
        return;
    }

    int remainingMinutes = 60;
    int accumulatedMinutes = 0;
    int maximumInterval = 26;
    for (std::size_t index = 0; index < m_nDrawMinute.size(); ++index) {
        const int difference = remainingMinutes - maximumInterval;
        const int minimum = std::min(difference, 0);
        const int maximum = std::max(difference, 0);
        const int interval =
            static_cast<int>(genrand64_real1() * static_cast<double>(maximum - minimum + 1) + static_cast<double>(minimum)) +
            5;
        accumulatedMinutes += interval;
        remainingMinutes -= interval;
        if (mode == 1) {
            m_nDrawMinute[index] = accumulatedMinutes;
        } else {
            m_nDrawMinute[m_nDrawMinute.size() - index - 1] = remainingMinutes + 4;
        }
        maximumInterval -= 5;
    }
}

bool BalthusEvent::DrawLottery() {
    // Current L2Server 0x0042935c.
    L2_GUARD_FUNCTION();
    CEvent_Balthus* const event = ActiveBalthusEvent();
    if (event == nullptr || !event->InEventTime()) {
        return false;
    }
    const int rolled = static_cast<int>(genrand64_real1() * 100.0 + 1.0);
    return rolled <= event->GetWinningProbability(m_nCurrentProgress);
}

void BalthusEvent::GiveRewardToLotteryWinner(CSPointer<User> user) {
    // Current L2Server 0x00429454.
    L2_GUARD_FUNCTION();
    CEvent_Balthus* const event = ActiveBalthusEvent();
    if (event == nullptr || !event->InEventTime()) {
        return;
    }

    const int userDbId = static_cast<int>(user->GetDBID());
    CFHTML::StrStrMapType parameters;
    const std::u16string title = CFHTML::MakeHTMLFromStrMap(u"g_event_balthus_message_01.htm", parameters, 0);
    const std::u16string contents = CFHTML::MakeHTMLFromStrMap(u"g_event_balthus_message_00.htm", parameters, 0);
    if (m_nCurrentRewardItem > 0) {
        db.RequestWritePostBirth(userDbId, title.c_str(), contents.c_str(), m_nCurrentRewardItem, 0x0f, 1, 0, 0);
    }
}

void BalthusEvent::SendEventInfo(CSPointer<User> user, const GAME_EVENT::BALTHUS_EVENT_INFO info) {
    // Current L2Server 0x004296f4.
    L2_GUARD_FUNCTION();
    CEvent_Balthus* const event = ActiveBalthusEvent();
    if (event == nullptr || !event->InEventTime() || !user) {
        return;
    }

    protocol::S_EX_BALTHUS_EVENT packet{};
    packet.nCurrentState = m_nCurrentState;
    packet.nProgress = GetProgressPercentage();
    packet.nCurrentRewardItem = m_nCurrentRewardItem;
    packet.nRewardTokenCount = user->m_nBalthusTokenCount;
    packet.nParticipated = m_bEventStarted ? static_cast<int>(info) : 0;
    packet.bRunning = m_bEventRunning;
    packet.nTime = runtime::crt::Time32Now() - m_nCurrentStateStarttime;
    user->GetSocket()->Send(S_EX, S_EX_BALTHUS_EVENT, packet);
}

void BalthusEvent::SendEventUIOff(CSPointer<User> user) {
    // Current L2Server 0x0042382c.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user",
        //     L"BalthusEvent.cpp",
        //     0x103)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user",
        //     L"BalthusEvent.cpp",
        //     0x103)
        L2_LOG_ERROR(Log, "Cannot hide the Balthus event UI: the user reference is empty; no update is sent.");
        return;
    }
    protocol::S_EX_BALTHUS_EVENT packet{};
    packet.nParticipated = -2;
    user->GetSocket()->Send(S_EX, S_EX_BALTHUS_EVENT, packet);
}

void BalthusEvent::BroadcastEventInfo(const GAME_EVENT::BALTHUS_EVENT_INFO info) {
    // Current L2Server 0x0042991c. The current body first typed-encodes the
    // seven fields and then broadcasts that exact payload.
    L2_GUARD_FUNCTION();
    CEvent_Balthus* const event = ActiveBalthusEvent();
    if (event == nullptr || !event->InEventTime()) {
        return;
    }

    protocol::S_EX_BALTHUS_EVENT packet;
    packet.nCurrentState = m_nCurrentState;
    packet.nProgress = GetProgressPercentage();
    packet.nCurrentRewardItem = m_nCurrentRewardItem;
    packet.nRewardTokenCount = -1;
    packet.nParticipated = static_cast<int>(info);
    packet.bRunning = m_bEventRunning;
    packet.nTime = runtime::crt::Time32Now() - m_nCurrentStateStarttime;

    CPacketHelperEx<20480> encoded;
    network::Stream stream(reinterpret_cast<unsigned char*>(encoded.GetPacket()), encoded.GetBufferSize());
    if (!packet.Encode(&stream)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to Encode BalthusEvent Packet, errorNo: %d",
        //     kBalthusEventSourceFile,
        //     0x135,
        //     CurrentBalthusLastError())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to Encode BalthusEvent Packet, errorNo: %d",
        //     kBalthusEventSourceFile,
        //     0x135,
        //     CurrentBalthusLastError())
        L2_LOG_ERROR(
            Log,
            "Cannot encode the Balthus event update (state %d, progress %d%%, reward item %d); this broadcast is skipped.",
            packet.nCurrentState, packet.nProgress, packet.nCurrentRewardItem
        );
        return;
    }

    const int encodedSize = static_cast<int>(stream.GetCursor() - stream.GetBuffer());
    CUserBroadcaster::inst().BroadcastToAllUser("chb", 0xfe, 0x1a3, encodedSize, encoded.GetPacket());
}

void BalthusEvent::BroadcastWinnerInfo(CSPointer<User> user) {
    // Current L2Server 0x00429b2c.
    L2_GUARD_FUNCTION();
    CEvent_Balthus* const event = ActiveBalthusEvent();
    if (event == nullptr || !event->InEventTime() || !user) {
        return;
    }
    CUserBroadcaster::inst().BroadcastToAllUser("chccScd", 0x62, 0xf08, 2, 0x0c, user->GetName(), 3, m_nCurrentRewardItem);
}

void BalthusEvent::WriteTokenLog(CSPointer<User> user, const int tokenCount, const GAME_EVENT::BALTHUS_REASON reason) {
    // Current L2Server 0x00423964.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     L"BalthusEvent.cpp",
        //     0x2a5)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     L"BalthusEvent.cpp",
        //     0x2a5)
        L2_LOG_ERROR(
            Log, "Cannot write the Balthus token audit record: the user reference is empty (token change %d, reason %d).",
            tokenCount, static_cast<int>(reason)
        );
        return;
    }
    CLogFormatter formatter{};
    formatter.WriteLog1(
        0x2354, user.get(), nullptr, nullptr, nullptr, nullptr, user->m_nBalthusTokenCount, tokenCount,
        static_cast<int>(reason), 0, 0, 0, 0xc277
    );
}

void BalthusEvent::GiveToken(CSPointer<User> user, protocol::RQ_UPDATE_BALTHUS_EVENT_USER_INFO& request) {
    // Current L2Server 0x00429cb4.
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }
    const int tokenCount = user->m_nBalthusTokenCount;
    if (tokenCount == 0) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::NO_LUCKY_COINS_AVAILABLE);
        return;
    }

    InventoryParams parameters;
    parameters.nItemClassID = 0xc277;
    parameters.nCount = tokenCount;
    L2_GUARD(*user->m_lock);
    if (!user->AddItemToInventory(parameters)) {
        return;
    }

    runtime::raw_atomic::ExchangeI32(&user->m_nBalthusTokenCount, 0);
    request.userInfoList.push_back(protocol::BalthusEventUserInfo{static_cast<int>(user->GetDBID()), user->m_nBalthusTokenCount});
    WriteTokenLog(user, -tokenCount, GAME_EVENT::BALTHUS_GET_REMAIN_TOKEN);
    SendEventInfo(user, GAME_EVENT::BALTHUS_NO_CHANGE);
}

void BalthusEvent::PushTokenWaitingQueue(const int userSid) {
    // Current L2Server 0x00426094.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_TokenWaitingQueueLock);
    m_TokenWaitingQueue.push_back(userSid);
}

void BalthusEvent::DistributeTokenToUsersInWaitingQueue() {
    // Current L2Server 0x0042a12c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_TokenWaitingQueueLock);
    if (m_TokenWaitingQueue.empty()) {
        return;
    }

    int missingUsers = 0;
    int firstMissingUserSid = 0;
    protocol::RQ_UPDATE_BALTHUS_EVENT_USER_INFO request;
    for (int index = 0; index < 100 && !m_TokenWaitingQueue.empty(); ++index) {
        const int userSid = m_TokenWaitingQueue.front();
        CSPointer<User> user = g_UserMap.FindObject(userSid);
        if (!user) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user[sid:%d]",
            //     L"BalthusEvent.cpp",
            //     0x195,
            //     userSid)
            // Noise: Summarize missing users once per drain batch instead of logging each skipped queue entry.
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid user[sid:%d]",
            //     L"BalthusEvent.cpp",
            //     0x195,
            //     userSid)
            if (missingUsers++ == 0) {
                firstMissingUserSid = userSid;
            }
        } else {
            GiveToken(user, request);
            runtime::raw_atomic::ExchangeI32(&user->m_nTokenRewarding, 0);
        }
        m_TokenWaitingQueue.pop_front();
    }
    if (missingUsers != 0) {
        L2_LOG_WARN(
            Log,
            "Balthus token claims: skipped %d queue entries whose users are absent from the user map (first SID %u); those "
            "entries were removed.",
            missingUsers, static_cast<unsigned int>(firstMissingUserSid)
        );
    }
    db.RequestUpdateBalthusEventUserInfo(request);
}

void BalthusEvent::SendNoticeToLotteryWinner(CSPointer<User> user) {
    // Current L2Server 0x0042a428.
    L2_GUARD_FUNCTION();
    CEvent_Balthus* const event = ActiveBalthusEvent();
    if (event == nullptr || !event->InEventTime() || !user || user->GetSocket() == nullptr) {
        return;
    }
    user->GetSocket()->Send("ch", 0xfe, 0x1a4);
}

void BalthusEvent::BuilderCmd_State(int state) {
    // Current L2Server 0x0042a578.
    L2_GUARD_FUNCTION();
    CEvent_Balthus* const event = ActiveBalthusEvent();
    if (event == nullptr || !event->InEventTime()) {
        return;
    }
    state = std::clamp(state, 1, 24);
    m_bEventRunning = true;
    m_nCurrentState = state;
    m_nCurrentRewardItem = event->BuilderCmd_GetRewardItem(state);
    m_nCurrentTokenCount = event->BuilderCmd_GetTokenCount(state);
    ShuffleProgressInterval();
    ValidateCurrentProgress();
    ValidateEventStatus();
    BroadcastEventInfo(GAME_EVENT::BALTHUS_NO_CHANGE);
}

void BalthusEvent::BuilderCmd_Jackpot() {
    // Current L2Server 0x0042a6a0.
    L2_GUARD_FUNCTION();
    if (!m_bEventRunning) {
        return;
    }
    StartRewardGivingForUserTimerExpired();
    runtime::raw_atomic::ExchangeI32(&m_nTokenDistributeFlag, 1);
    m_bEventRunning = false;
    BroadcastEventInfo(GAME_EVENT::BALTHUS_NO_CHANGE);
}

void BalthusEvent::BuilderCmd_Reset() {
    // Current L2Server 0x0042a76c.
    L2_GUARD_FUNCTION();
    EndRewardGivingForUserTimerExpired();
    runtime::raw_atomic::ExchangeI32(&m_nTokenDistributeFlag, 0);
    m_bEventRunning = true;
    BroadcastEventInfo(GAME_EVENT::BALTHUS_NO_CHANGE);
}

void BalthusEvent::BuilderCmd_Progress(const int progress) {
    // Current L2Server 0x0042a828.
    L2_GUARD_FUNCTION();
    m_nCurrentProgress = progress;
    ValidateEventStatus();
    BroadcastEventInfo(GAME_EVENT::BALTHUS_NO_CHANGE);
}

void BalthusEvent::WriteJackpotLog(CSPointer<User> user) {
    // Current L2Server 0x0042a8d8.
    L2_GUARD_FUNCTION();
    CEvent_Balthus* const event = ActiveBalthusEvent();
    if (event == nullptr) {
        if (user) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CEvent_Balthus is NULL, user_dbid : %d item_id : %d",
            //     L"BalthusEvent.cpp",
            //     0x28d,
            //     static_cast<int>(user->GetDBID()),
            //     m_nCurrentRewardItem)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] CEvent_Balthus is NULL, user_dbid : %d item_id : %d",
            //     L"BalthusEvent.cpp",
            //     0x28d,
            //     static_cast<int>(user->GetDBID()),
            //     m_nCurrentRewardItem)
            L2_LOG_ERROR(
                Log,
                "Cannot write the Balthus jackpot audit record for character %u, reward item %d: the Balthus event "
                "definition is unavailable.",
                user->GetDBID(), m_nCurrentRewardItem
            );
        }
        return;
    }

    CLogFormatter formatter{};
    formatter.WriteLog1(
        0x2353, user.get(), nullptr, nullptr, nullptr, nullptr, event->m_nCurrentRewardSet, m_nCurrentState,
        m_nCurrentProgress + 1, event->GetWinningProbability(m_nCurrentProgress), m_nCurrentTokenCount, 0,
        m_nCurrentRewardItem
    );
}

void BalthusEvent::InitNewLottery() {
    // Current L2Server 0x0042aac8.
    L2_GUARD_FUNCTION();
    CEvent_Balthus* const event = ActiveBalthusEvent();
    if (event == nullptr || !event->InEventTime()) {
        return;
    }

    EndRewardGivingForUserTimerExpired();
    runtime::raw_atomic::ExchangeI32(&m_nTokenDistributeFlag, 0);
    m_bEventRunning = true;
    m_nCurrentState = event->GetCurrentState();
    m_nCurrentRewardItem = event->GetCurrentRewardItem();
    m_nCurrentTokenCount = event->GetCurrentTokenCount();
    m_nCurrentStateStarttime = runtime::crt::Time32Now();
    ShuffleProgressInterval();
    ValidateCurrentProgress();
    ValidateEventStatus();
    BroadcastEventInfo(GAME_EVENT::BALTHUS_NO_CHANGE);
}

void BalthusEvent::TimerExpired(int) {
    // Current L2Server 0x0042abfc.
    L2_GUARD_FUNCTION();
    CEvent_Balthus* const event = ActiveBalthusEvent();
    if (event == nullptr) {
        return;
    }
    if (!event->InEventTime()) {
        // Current L2Server 0x0042AC9F-0x0042ACB0 publishes one timer and
        // jumps directly to the function epilogue.
        AddTimer(1000, 0);
        return;
    }

    std::tm local{};
    if (!LocalTimeNow(local)) {
        AddTimer(1000, 0);
        return;
    }
    const int currentMinute = local.tm_min;
    if (!m_bEventStarted) {
        m_bEventStarted = true;
        m_nPrevMinute = currentMinute;
        CUserBroadcaster::inst().BroadcastToAllUser("chc", 0x62, 0xf31, 0);
        BroadcastEventInfo(GAME_EVENT::BALTHUS_NO_CHANGE);
    }

    if (currentMinute != m_nPrevMinute) {
        if (currentMinute == 0) {
            InitNewLottery();
        }
        runtime::raw_atomic::ExchangeI32(&m_nTokenDistributeFlag, 0);
        if (m_bEventRunning && currentMinute == m_nDrawMinute[static_cast<std::size_t>(m_nCurrentProgress)]) {
            if (!DrawLottery()) {
                WriteJackpotLog(CSPointer<User>());
                ++m_nCurrentProgress;
                ValidateEventStatus();
            } else {
                StartRewardGivingForUserTimerExpired();
                runtime::raw_atomic::ExchangeI32(&m_nTokenDistributeFlag, 1);
                m_bEventRunning = false;
            }
            BroadcastEventInfo(GAME_EVENT::BALTHUS_NO_CHANGE);
        }
    }

    DistributeTokenToUsersInWaitingQueue();
    m_nPrevMinute = currentMinute;
    AddTimer(1000, 0);
}

void BalthusEvent::Init() {
    // Current L2Server 0x0042adc0.
    L2_GUARD_FUNCTION();
    InitNewLottery();
}

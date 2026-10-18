#pragma once

#include "GameEventSystem.h"

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace protocol {
class RQ_UPDATE_BALTHUS_EVENT_USER_INFO;
}

class CDBSocket;

namespace DBPacketExHandler {

bool ResponseLoadBalthusEventUserInfoEx(CDBSocket* socket, const unsigned char* packet);

} // namespace DBPacketExHandler

namespace GAME_EVENT {

enum BALTHUS_REASON : std::int32_t {
    BALTHUS_JACKPOT = 0,
    BALTHUS_GET_REMAIN_TOKEN = 1,
};

} // namespace GAME_EVENT

class BalthusEvent : public CIOObject, public Singleton<BalthusEvent> {
public:
    BalthusEvent();
    ~BalthusEvent() override;

    L2_DECLARE_OBJECT_REGISTRATION(BalthusEvent)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;

    void Init();
    bool IsStartedEvent() const noexcept { return m_bEventStarted; }
    std::int32_t EndRewardGivingForUserTimerExpired();
    std::int32_t StartRewardGivingForUserTimerExpired();
    std::int32_t IsTokenDistributing() const noexcept { return m_nTokenDistributeFlag; }
    int GetTokenCount() const noexcept { return m_nCurrentTokenCount; }
    void ValidateCurrentProgress();
    int GetProgressPercentage();
    void ValidateEventStatus();
    void ShuffleProgressInterval();
    bool DrawLottery();
    void InitNewLottery();

    void SendEventInfo(CSPointer<User> user, GAME_EVENT::BALTHUS_EVENT_INFO info);
    void SendEventUIOff(CSPointer<User> user);
    void BroadcastEventInfo(GAME_EVENT::BALTHUS_EVENT_INFO info);
    void BroadcastWinnerInfo(CSPointer<User> user);
    void GiveRewardToLotteryWinner(CSPointer<User> user);
    void GiveToken(CSPointer<User> user, protocol::RQ_UPDATE_BALTHUS_EVENT_USER_INFO& request);
    void PushTokenWaitingQueue(int userSid);
    void DistributeTokenToUsersInWaitingQueue();
    void SendNoticeToLotteryWinner(CSPointer<User> user);
    void WriteTokenLog(CSPointer<User> user, int tokenCount, GAME_EVENT::BALTHUS_REASON reason);
    void WriteJackpotLog(CSPointer<User> user);

    void BuilderCmd_State(int state);
    void BuilderCmd_Jackpot();
    void BuilderCmd_Reset();
    void BuilderCmd_Progress(int progress);
    int GetCurrentRewardItem() const noexcept { return m_nCurrentRewardItem; }
    int GetCurrentState() const noexcept { return m_nCurrentState; }
    int GetCurrentProgress() const noexcept { return m_nCurrentProgress; }
    int GetProgressInterval(int index) const noexcept { return m_nDrawMinute[static_cast<std::size_t>(index)]; }
    bool IsEventRunning() const noexcept { return m_bEventRunning; }

private:
    friend bool DBPacketExHandler::ResponseLoadBalthusEventUserInfoEx(CDBSocket* socket, const unsigned char* packet);

    CYieldLock m_TokenWaitingQueueLock; // +0x20
    std::deque<int> m_TokenWaitingQueue; // +0x30
    std::array<std::int32_t, 5> m_nDrawMinute; // +0x60
    std::int32_t m_nPrevMinute; // +0x74
    std::int32_t m_nCurrentState; // +0x78
    std::int32_t m_nCurrentProgress; // +0x7c
    std::int32_t m_nCurrentRewardItem; // +0x80
    std::int32_t m_nCurrentTokenCount; // +0x84
    volatile std::int32_t m_nRewardDistributeFlag; // +0x88
    volatile std::int32_t m_nTokenDistributeFlag; // +0x8c
    bool m_bEventStarted; // +0x90
    bool m_bEventRunning; // +0x91
    std::array<std::byte, 0x02> m_alignment92To94{}; // +0x92
    std::int32_t m_nCurrentStateStarttime; // +0x94
};

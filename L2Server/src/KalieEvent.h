#pragma once

#include "GameEventSystem.h"

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

template <std::size_t Capacity>
class CPacketHelperEx;
class CDBSocket;
class User;

namespace DBPacketExHandler {

bool ResponseLoadKalieEventEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadKalieEventUserEx(CDBSocket* socket, const unsigned char* packet);

} // namespace DBPacketExHandler

// Current KalieEvent.cpp process-global switch at 0x015cc4ca. The item-use
// condition and the event manager consume the same byte directly.
extern bool g_bFreeKalieEvent;

class KalieEvent : public CIOObject {
public:

    ~KalieEvent() override;
    void TimerExpired(int id) override;

    static KalieEvent& Inst();

    void IncreaseKillNpcCnt();
    void SetCurrentState(std::int32_t state);
    void ResetState();
    bool NeedChangeProgress();
    std::int32_t ChangeRewardValueToZero();
    std::int32_t ChangeRewardValueToOne();
    void EveryRewardValueOn();
    void KalieEventStart();
    void KalieUIOff(User& user);
    bool IsSuccessJackPot();
    void KalieUIOn(User& user);

    void SendRewardItemForEveryUser(User& user);
    void SendRewardItemToLuckyUser(User& user);
    void SendKalieInfo(User& user, int freeEventState);
    void BroadCastToAllUserChangeInfo();
    void BroadCastToAllUserEventUIOff();
    void BroadCastToAllUserEventUIOn();

    void ProcessGivingTokenItem();
    void SetEventInfo();
    void InitEventInfoForUser();
    void InitGoalCntForEventStart();
    void SetRewardValForBuilder();
    void SetStateForBuilder(std::int32_t state);
    void FreeKalieOff();
    void FreeKalieOn();
    void IncreaseState();
    void IncreaseProgress();
    void ResetEvent();
    void SuccesssGiveItemToLuckyUser(User* user);
    void ResetEventForBuild();
    void IncreaseProgressForbuild();

private:
    friend bool DBPacketExHandler::ResponseLoadKalieEventEx(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ResponseLoadKalieEventUserEx(CDBSocket* socket, const unsigned char* packet);
    friend class CEvent_Kalie;
    friend class CNPC;
    friend class User;

    KalieEvent();

    void GiveTokenItem(User& user, CPacketHelperEx<20480>& packet);

    inline static KalieEvent* KalieEventInst = nullptr;

    volatile std::int32_t m_nKillNpcCnt; // +0x18
    volatile std::int32_t m_nCurrentState; // +0x1c
    volatile std::int32_t m_nProgress; // +0x20
    std::int32_t m_nUserID; // +0x24
    std::int32_t m_nGoalCnt; // +0x28
    std::int32_t m_nRewardTokenCnt; // +0x2c
    std::int32_t m_nCurrentRewardItem; // +0x30
    std::array<std::byte, 0x04> m_alignment34To38{}; // +0x34
    std::int64_t m_nTickCnt; // +0x38
    volatile std::int32_t m_nReward; // +0x40
    bool m_bEveryReward; // +0x44
    bool m_bEventStarted; // +0x45
    std::array<std::byte, 0x02> m_alignment46To48{}; // +0x46
    std::queue<int> m_UserQueue; // +0x48
    CYieldLock m_Lock; // +0x78
    CYieldLock m_SendChangeFreeEvent; // +0x88
    std::int32_t m_nKalieJackPot; // +0x98
    std::array<std::byte, 0x04> m_alignment9cToA0{}; // +0x9c
};

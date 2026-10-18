#include "L2Time.h"
#include "Shared/src/MemorySub.h"

#include "Area.h"
#include "Auth.h"
#include "CConfig.h"
#include "ChannelChatting.h"
#include "DB.h"
#include "Default.h"
#include "Door.h"
#include "ExDrop.h"
#include "Manor.h"
#include "NPSystem.h"
#include "NpcServer.h"
#include "PartyRoomRegistry.h"
#include "PledgeJoinSystem.h"
#include "Relay.h"
#include "Residence.h"
#include "SharedContainer.h"
#include "UIEventCountManager.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include "Shared/src/L2Time.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <ctime>

// Current L2Server.exe 0x16658470 is zero-initialized L2Time.obj storage.
// The exact donor symbol encodes volatile long, std::int32_t preserves the
// Win64 width on portable LP64 hosts.
volatile std::int32_t g_nMaxInZoneNPCCount = 0;
volatile std::int32_t g_nMaxNPCCount = 0;
runtime::SystemTime g_tMaxNPCCount{};
std::int32_t g_nNPCCountSlot = 0;
volatile std::int32_t g_aNPCCount[15]{};

std::queue<RqPledgeInfo> g_RqEmblemQueue;
CYieldLock g_RqEmblemQueueLock;
std::queue<DynamicTerritoryInfo> g_DynamicTerritoryQueue;
CYieldLock g_DynamicTerritoryQueueLock;


CL2Time g_L2Time;

CL2Time::CL2Time() {
    L2_GUARD_FUNCTION();

    m_nHour = 13;
    m_nStartTime = 780;
    m_bIsNight = false;
    m_bIsSunRise = true;
    timeLastTaxChange = runtime::crt::Time32Now();
    timeLastManorResetUpdate = runtime::crt::Time32Now();
    timeLastIncomeUpdate = timeLastTaxChange;
    m_nLastCheckedWeekChanged = runtime::crt::Time32Now();
    m_nLastCheckedDayChanged = runtime::crt::Time32Now();
    m_nLastCheckedManorChanged = runtime::crt::Time32Now();
    m_bForceManorSet = false;
    m_nLastCheckedHourForPacketStat = runtime::crt::Time32Now();
    m_nLastCheckedForServerToUserPacketStat = runtime::crt::Time32Now();
    m_bForceShillienTime = false;
    m_nTime = 0;
    m_nRealHour = 0;
    m_nInitTime = 0;
    m_nLastCheckedMin = 0;
}

CL2Time::~CL2Time() { L2_GUARD_FUNCTION(); }

L2_DEFINE_OBJECT_RELEASE(CL2Time)

void CL2Time::Init() {
    L2_GUARD_FUNCTION();

    m_nTime = m_nStartTime;
    m_nInitTime = runtime::GetTickCount();
    const std::uint32_t tick = runtime::GetTickCount();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"InitL2Time, init[%d], tick[%d], at %02d:%02d",
    //     m_nInitTime,
    //     tick,
    //     m_nTime / 60U,
    //     m_nTime % 60U)
    L2_LOG_INFO(Log, u"InitL2Time, init[%d], tick[%d], at %02d:%02d", m_nInitTime, tick, m_nTime / 60U, m_nTime % 60U);

    AddTimer(10000, 0);
    AddTimer(60000, 2);
    AddTimer(60000, 3);
    AddTimer(60000, 4);
    AddTimer(60000, 5);
    AddTimer(60000, 6);
    AddTimer(g_nCachePingTime, 7);
    if (g_bUserToServerPacketStatMonitor || g_bTimeAnalysisMonitor || g_bNpcDieStatMonitor) {
        AddTimer(60000, 9);
    }
    if (g_bServerToUserPacketStatMonitor) {
        AddTimer(60000, 15);
    }
    AddTimer(60000, 10);
    AddTimer(60000, 11);
    AddTimer(1000, 12);
    AddTimer(5000, 14);
    AddTimer(1000, 16);
    AddTimer(30000, 17);
    AddTimer(3600000, 18);
    AddTimer(60000, 20);
}

bool CL2Time::IsManorSettingEnableTime() {
    L2_GUARD_FUNCTION();

    if (m_bForceManorSet) {
        return true;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);

    const std::uint32_t minuteOfDay = static_cast<std::uint32_t>(local->tm_hour * 60 + local->tm_min);
    return minuteOfDay - 270U >= 930U;
}

void CL2Time::TaxAndIncomeUpdateCheck() {
    L2_GUARD_FUNCTION();

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    if (local == nullptr) {
        return;
    }
    const int hour = local->tm_hour;
    const int minute = local->tm_min;
    const int day = local->tm_mday;
    const int weekDay = local->tm_wday;

    if (hour == 12) {
        if (weekDay != 1) {
            return;
        }
        const std::tm* const lastIncome = runtime::crt::LocalTime32(timeLastIncomeUpdate);
        if (lastIncome != nullptr && lastIncome->tm_mday == day) {
            return;
        }
        g_ResidenceDB.SendResidenceTaxIncome();
        timeLastIncomeUpdate = runtime::crt::Time32Now();
        db.RequestSaveLastTaxChange(timeLastIncomeUpdate, timeLastTaxChange, timeLastManorResetUpdate);
    }

    if (hour == 4) {
        if (minute < 30) {
            return;
        }
        const std::tm* const lastManor = runtime::crt::LocalTime32(timeLastManorResetUpdate);
        if (lastManor != nullptr && lastManor->tm_mday == day) {
            return;
        }
        g_ManorDB.CalcNextAllManor();
        timeLastManorResetUpdate = runtime::crt::Time32Now();
        db.RequestSaveLastTaxChange(timeLastIncomeUpdate, timeLastTaxChange, timeLastManorResetUpdate);
    }

    if (hour == 20) {
        const std::tm* const lastManor = runtime::crt::LocalTime32(timeLastManorResetUpdate);
        if (lastManor == nullptr || lastManor->tm_mday != day || lastManor->tm_hour != 20) {
            g_ManorDB.TransferAllManorData();
            timeLastManorResetUpdate = runtime::crt::Time32Now();
            db.RequestSaveLastTaxChange(timeLastIncomeUpdate, timeLastTaxChange, timeLastManorResetUpdate);
        }
    }
}

bool CL2Time::UpdateNextManor() {
    L2_GUARD_FUNCTION();

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    if (local == nullptr) {
        return false;
    }
    const int day = local->tm_mday;
    const std::tm* const last = runtime::crt::LocalTime32(timeLastManorResetUpdate);
    const bool update = last == nullptr || last->tm_mday != day;
    if (update) {
        g_ManorDB.CalcNextAllManor();
        timeLastManorResetUpdate = runtime::crt::Time32Now();
        db.RequestSaveLastTaxChange(timeLastIncomeUpdate, timeLastTaxChange, timeLastManorResetUpdate);
    }
    return update;
}

void CL2Time::ResetAllUserVoteData() { L2_GUARD_FUNCTION(); }

void CL2Time::CheckVoteDayChanged() {
    L2_GUARD_FUNCTION();

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    if (local == nullptr) {
        return;
    }
    const int hour = local->tm_hour;
    const std::tm* const last = runtime::crt::LocalTime32(m_nLastCheckedDayChanged);
    if (last != nullptr && last->tm_hour == 12 && hour == 13) {
        m_nLastCheckedDayChanged = now;
        g_bNewVote = true;
        ResetAllUserVoteData();
    }
}

void CL2Time::NpcPingStart() {
    L2_GUARD_FUNCTION();
    AddTimer(g_nNpcPingTime, 8);
}

int CL2Time::GetTime() {
    L2_GUARD_FUNCTION();

    const std::uint32_t elapsed = runtime::GetTickCount() - m_nInitTime;
    return static_cast<int>(m_nStartTime + (elapsed / 10000U) % 1440U);
}

void CL2Time::SetTime(unsigned int time) {
    L2_GUARD_FUNCTION();

    m_nTime = time;
    m_nStartTime = time;
    m_nInitTime = runtime::GetTickCount();

    const std::uint32_t hour = m_nTime / 60U;
    if (m_nHour != hour) {
        m_nHour = hour;
        g_npcserver.Send("cddd", 0x1f, m_nHour, m_nTime % 60U, m_bIsNight);
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"SetL2Time, init[%d], tick[%d], at %02d:%02d",
    //     m_nInitTime,
    //     runtime::GetTickCount(),
    //     m_nTime / 60U,
    //     m_nTime % 60U)
    L2_LOG_INFO(
        Log, u"SetL2Time, init[%d], tick[%d], at %02d:%02d", m_nInitTime, runtime::GetTickCount(), m_nTime / 60U,
        m_nTime % 60U
    );
}

bool CL2Time::IsForceShillienMoon() {
    L2_GUARD_FUNCTION();
    return m_bForceShillienTime;
}

void CL2Time::SetForceShillienMoon(bool force) {
    L2_GUARD_FUNCTION();
    m_bForceShillienTime = force;
}

void CL2Time::CheckForServerToUserPacketStat() {
    L2_GUARD_FUNCTION();

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);

    const int minute = local->tm_min;
    runtime::crt::LocalTime32(m_nLastCheckedForServerToUserPacketStat);
    if (minute == (minute / 10) * 10) {
        m_nLastCheckedForServerToUserPacketStat = now;
        if (g_bServerToUserPacketStatMonitor) {
            CPacketStat<768, 254>& packetStat = CPacketStat<768, 254>::Inst();
            // packetStat.Report(LOG_ERROR)
            packetStat.Report(LOG_NORMAL);
            packetStat.Reset();
        }
    }
}

void CL2Time::CheckResetWorldChat() {
    L2_GUARD_FUNCTION();

    const std::int32_t now = runtime::crt::Time32Now();
    if (g_WorldChat.tNextResetTime < now) {
        std::tm* const local = runtime::crt::LocalTime32(now);
        ++local->tm_mday;
        local->tm_hour = 7;
        local->tm_sec = 0;
        local->tm_min = 0;
        g_WorldChat.tNextResetTime = runtime::crt::Mktime32(local);
        db.RequestResetWorldChatPoint();

        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
            user->SetUsedWorldChatPoint(0);
            CUserSocket* const socket = user->GetSocket();
            if (socket != nullptr) {
                socket->Send("chd", 0xfe, 0x175, user->GetWorldChatCount());
            }
        });
        // Log.Add(
        //     LOG_NORMAL,
        //     "[WorldChat] Point Reset")
        L2_LOG_INFO(Log, "[WorldChat] Point Reset");
    }

    AddTimer(60000, 0x0d);
}

void CL2Time::CheckDynamicTerritory() {
    L2_GUARD_FUNCTION();

    L2_GUARD(g_DynamicTerritoryQueueLock);
    while (!g_DynamicTerritoryQueue.empty()) {
        const DynamicTerritoryInfo info = g_DynamicTerritoryQueue.front();
        if (runtime::crt::Time32Now() <= info.expirationTime) {
            break;
        }

        CContainer* const container = CContainerDB::Inst().GetContainer(info.containerId);
        if (container != nullptr) {
            L2_GUARD(container->m_BlockTerritoryLock);
            const auto found =
                std::find(container->m_lBlockTerritoryList.begin(), container->m_lBlockTerritoryList.end(), info.territory);
            if (found != container->m_lBlockTerritoryList.end()) {
                container->m_lBlockTerritoryList.erase(found);
            }
        }
        g_DynamicTerritoryQueue.pop();
    }
}

void CL2Time::CheckHourForPacketStat() {
    L2_GUARD_FUNCTION();

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    if (local == nullptr) {
        return;
    }
    const int hour = local->tm_hour;
    const int minute = local->tm_min;
    const std::tm* const last = runtime::crt::LocalTime32(m_nLastCheckedHourForPacketStat);
    if (last == nullptr || last->tm_hour == hour || minute != 59) {
        return;
    }

    m_nLastCheckedHourForPacketStat = now;
    if (g_bServerToUserPacketStatMonitor) {
        CPacketStat<37, 37>& first = CPacketStat<37, 37>::Inst();
        CPacketStat<103, 103>& second = CPacketStat<103, 103>::Inst();
        CPacketStat<48, 48>& third = CPacketStat<48, 48>::Inst();
        // first.Report(LOG_ERROR)
        first.Report(LOG_NORMAL);
        first.Reset();
        // second.Report(LOG_ERROR)
        second.Report(LOG_NORMAL);
        // third.Report(LOG_ERROR)
        third.Report(LOG_NORMAL);
        // second.ReportRank(
        //     LOG_ERROR,
        //     CPacketStat<103, 103>::PRED_CNT)
        second.ReportRank(LOG_NORMAL, CPacketStat<103, 103>::PRED_CNT);
        // third.ReportRank(
        //     LOG_ERROR,
        //     CPacketStat<48, 48>::PRED_CNT)
        third.ReportRank(LOG_NORMAL, CPacketStat<48, 48>::PRED_CNT);
        second.Reset();
        third.Reset();
    }
    if (g_bUserToServerPacketStatMonitor) {
        CPacketStat<560, 208>& packetStat = CPacketStat<560, 208>::Inst();
        // packetStat.Report(LOG_ERROR)
        packetStat.Report(LOG_NORMAL);
        packetStat.Reset();
    }
    if (g_bTimeAnalysisMonitor) {
        // g_Timer.Report(LOG_ERROR)
        g_Timer.Report(LOG_NORMAL);
        g_Timer.Reset();
    }
    if (g_bNpcDieStatMonitor) {
        // NpcDieStat::Inst().Report(LOG_ERROR)
        NpcDieStat::Inst().Report(LOG_NORMAL);
        NpcDieStat::Inst().Reset();
    }
    if (g_bNPCBroadcastMonitor) {
        // NpcBroadCastStat::Inst().Report(LOG_ERROR)
        NpcBroadCastStat::Inst().Report(LOG_NORMAL);
        NpcBroadCastStat::Inst().Reset();
    }
}

void CL2Time::TimerExpired(const int id) {
    L2_GUARD_FUNCTION();

    switch (id) {
        case 2:
            TaxAndIncomeUpdateCheck();
            AddTimer(60000, 2);
            return;
        case 5:
            return;
        case 3:
            if (!g_bAuthConnected) {
                authd.ReconnectAuth();
            }
            AddTimer(60000, 3);
            return;
        case 4:
            g_bNewVote = false;
            CheckVoteDayChanged();
            AddTimer(60000, 4);
            return;
        case 6:
            ++g_nNPCCountSlot;
            g_aNPCCount[g_nNPCCountSlot % 15] = g_nNPCCount;
            if (g_nMaxNPCCount < g_nNPCCount) {
                g_nMaxNPCCount = g_nNPCCount;
            }
            if (g_nMaxInZoneNPCCount < g_nInZoneNPCCount) {
                g_nMaxInZoneNPCCount = g_nInZoneNPCCount;
            }
            AddTimer(60000, 6);
            return;
        case 7:
            if (g_bCachePingFlag) {
                db.SendCachePing();
            }
            AddTimer(g_nCachePingTime, 7);
            return;
        case 8:
            if (g_bNpcPingFlag) {
                g_npcserver.Send("cd", 0x93, runtime::GetTickCount());
            }
            AddTimer(g_nNpcPingTime, 8);
            return;
        case 9:
            CheckHourForPacketStat();
            if (g_bServerToUserPacketStatMonitor || g_bUserToServerPacketStatMonitor) {
                AddTimer(60000, 9);
            }
            return;
        case 15:
            CheckForServerToUserPacketStat();
            if (g_bServerToUserPacketStatMonitor) {
                AddTimer(60000, 15);
            }
            return;
        case 10:
            ExDropItemSystem::inst().InitLastResetTime();
            AddTimer(60000, 10);
            return;
        case 11:
            CPledgeJoinSystem::Inst()->InitPledgeInfoPerDay();
            AddTimer(60000, 11);
            return;
        case 12: {
            L2_GUARD(g_RqEmblemQueueLock);
            if (!g_RqEmblemQueue.empty()) {
                const RqPledgeInfo info = g_RqEmblemQueue.front();
                db.RequestGetPledgeEmblem(info.userServerId, info.emblemDatabaseId, info.worldId, info.pledgeServerId);
                g_RqEmblemQueue.pop();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR] RequestGetPledgeEmblem "
                //     L"UserSID[%d] UserDBID[%d] EmblemDBID[%d] "
                //     L"World[%d] PledgeSID[%d]",
                //     info.userServerId,
                //     info.userDatabaseId,
                //     info.emblemDatabaseId,
                //     info.worldId,
                //     info.pledgeServerId)
                // Noise: routine dispatch of a queued clan emblem request.
                // this does not indicate a failed request or confirm its result.
                // L2_LOG_ERROR(Log,
                //     L"[NO_ERROR] RequestGetPledgeEmblem "
                //     L"UserSID[%d] UserDBID[%d] EmblemDBID[%d] "
                //     L"World[%d] PledgeSID[%d]",
                //     info.userServerId,
                //     info.userDatabaseId,
                //     info.emblemDatabaseId,
                //     info.worldId,
                //     info.pledgeServerId)
            }
            AddTimer(1000, 12);
            return;
        }
        case 13:
            CheckResetWorldChat();
            return;
        case 14:
            CheckDynamicTerritory();
            AddTimer(5000, 14);
            return;
        case 16:
            AddTimer(1000, 16);
            WorldChatManager::Inst().ProcessSendChat();
            return;
        case 17:
            NPSystem::g_NPD.SendPing();
            AddTimer(30000, 17);
            return;
        case 18:
            AddTimer(3600000, 18);
            UIEventCountManager::Inst()->SaveUIEventCountToDB();
            return;
        case 19:
            if (ChannelChattingClient::Inst().SendKeepAlive()) {
                AddTimer(30000, 19);
            }
            return;
        case 20: {
            RelayClient* const relay = RelayClient::Inst();
            if (relay->sendable != 0) {
                if (!CPartyRoomRegistry::inst().IsEmptyRegisterdRoom()) {
                    relay->SendPartyMatchingInfo();
                    RelayClient::m_bSendPrevPartyRoomPacket = true;
                } else if (RelayClient::m_bSendPrevPartyRoomPacket) {
                    relay->SendPartyMatchingInfo();
                    RelayClient::m_bSendPrevPartyRoomPacket = false;
                }
            }
            AddTimer(60000, 20);
            return;
        }
        default:
            break;
    }

    m_nTime = static_cast<std::uint32_t>(GetTime());
    if (m_nTime > 1439U) {
        m_nTime %= 1440U;
    }
    const std::uint32_t hour = m_nTime / 60U;
    if (m_nHour != hour) {
        m_nHour = hour;
    }
    g_npcserver.Send("cddd", 0x1f, m_nHour, m_nTime % 60U, m_bIsNight);

    if (m_nTime < 360U) {
        if (!m_bIsNight) {
            m_bIsNight = true;
            g_DoorDB.OnSunrise(true);
            CAreaDB::inst().OnSunrise(true);
        }
    } else if (m_bIsNight) {
        m_bIsNight = false;
        g_DoorDB.OnSunrise(false);
        CAreaDB::inst().OnSunrise(false);
    }

    if (m_nTime - 60U > 0x167U && !m_bIsSunRise) {
        // Log.Add(
        //     m_nTime < 422U ? LOG_NORMAL : LOG_ERROR,
        //     m_nTime < 422U
        //         ? L"SunRise time[%d-%d] at %02d:%02d"
        //         : L"Invalid SunRise time[%d-%d] at %02d:%02d",
        //     runtime::GetTickCount(),
        //     m_nInitTime,
        //     m_nHour,
        //     m_nTime % 60U)
        L2_LOG(
            Log, m_nTime < 422U ? LOG_NORMAL : LOG_ERROR,
            m_nTime < 422U ? L"SunRise time[%d-%d] at %02d:%02d" : L"Invalid SunRise time[%d-%d] at %02d:%02d",
            runtime::GetTickCount(), m_nInitTime, m_nHour, m_nTime % 60U
        );
        CUserBroadcaster::inst().BroadcastToCanSunsetUser("c", 0x12);
        m_bIsSunRise = true;
    }
    if (m_nTime - 60U < 0x168U && m_bIsSunRise) {
        // Log.Add(
        //     m_nTime < 62U ? LOG_NORMAL : LOG_ERROR,
        //     m_nTime < 62U
        //         ? L"SunSet time[%d-%d] at %02d:%02d"
        //         : L"Invalid SunSet time[%d-%d] at %02d:%02d",
        //     runtime::GetTickCount(),
        //     m_nInitTime,
        //     m_nHour,
        //     m_nTime % 60U)
        L2_LOG(
            Log, m_nTime < 62U ? LOG_NORMAL : LOG_ERROR,
            m_nTime < 62U ? L"SunSet time[%d-%d] at %02d:%02d" : L"Invalid SunSet time[%d-%d] at %02d:%02d",
            runtime::GetTickCount(), m_nInitTime, m_nHour, m_nTime % 60U
        );
        CUserBroadcaster::inst().BroadcastToCanSunsetUser("c", 0x13);
        m_bIsSunRise = false;
    }
    AddTimer(10000, 0);
}

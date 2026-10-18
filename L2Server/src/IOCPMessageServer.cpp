#include "IOCPMessageServer.h"

#include "AirShip.h"
#include "CConfig.h"
#include "Creature.h"
#include "Door.h"
#include "DynamicQuest.h"
#include "InstantAgit.h"
#include "OlympiadService.h"
#include "OlympiadStuff.h"
#include "Pledge.h"
#include "PledgeGameSystem.h"
#include "Server.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shuttle.h"
#include "StaticObject.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"
#include "Vehicle.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/SystemMail.h"

#include <array>
#include <cstring>
#include <cwchar>
#include <iterator>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#endif

namespace {


int PositionComponent(double value) noexcept { return runtime::Win64CvttDoubleToInt32(value); }

} // namespace

void CShutDownMessage::StopLoader() {
    // Current L2Server.exe 0x007ce054, size 200.
    L2_GUARD_FUNCTION();

    if (!m_StopLoader || !g_bUseResourceLoader) {
        return;
    }

#if defined(_WIN32)
    constexpr DWORD kTargetAllAccess = 0x000f001f;
    HANDLE const mapping = ::OpenFileMappingW(kTargetAllAccess, FALSE, L"L2ResourceLoader_Stopper");
    auto* const stopper = static_cast<unsigned char*>(::MapViewOfFile(mapping, kTargetAllAccess, 0, 0, 0));
#else
    const int descriptor = ::shm_open("/L2ResourceLoader_Stopper", O_RDWR, 0);
    auto* const stopper = static_cast<unsigned char*>(::mmap(nullptr, 1, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0));
#endif
    // The target performs no attachment check and keeps the
    // mapping for process lifetime.
    *stopper = 1;
}

void CShutDownMessage::OnReceive(const int argument) {
    // Current L2Server.exe 0x007ce3d4, size 521.
    L2_GUARD_FUNCTION();

    if (argument == 0) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Shutdown starting...")
        // L2_LOG_INFO(Log,
        //     L"Shutdown starting...")
        L2_LOG_INFO(Log, "Shutdown request received; starting the shutdown countdown.");
        m_nTime = 30;
        g_server.m_bReceivedShutdownMessage = true;
    } else if (argument == 1) {
        CUserBroadcaster::inst().BroadcastToAllUser("chccc", 0x62, 1, 1, 0x14, 1);
        return;
    } else {
        if (argument != 2) {
            return;
        }

        if (m_nTime < 1) {
            ReportMemory();
            ReportInstance();
            StopLoader();
            DontMakeMiniDump();
            ForceCrash(CrashInfo::BY_SHUTDOWN_MESSAGE);
        }

        if (m_nTime < 6 || m_nTime % 10 == 0) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"Server will be shut down in %d sec",
            //     m_nTime)
            // L2_LOG_INFO(Log,
            //     L"Server will be shut down in %d sec",
            //     m_nTime)
            L2_LOG_INFO(
                Log, "Shutdown countdown: %d seconds remain before reports, loader-stop signaling and process termination.",
                m_nTime
            );
            CUserBroadcaster::inst().BroadcastToAllUser(
                "chccc", 0x62, 1, 1, 0x14, static_cast<int>(static_cast<signed char>(m_nTime))
            );
        }
        --m_nTime;
    }

    AddTimer(1000, 2);
}

void CPledgeGameLoadingMessage::OnReceive(int) {
    L2_GUARD_FUNCTION();
    g_PledgeGameSystem.m_pledgeGameHtml.Init();
}

void COlympiadLoadingMessage::OnReceive(int) {
    // Current L2Server 0x007ce1b4.
    L2_GUARD_FUNCTION();
    WOlympiadService::inst().InitHtml();
}

void CRegularAirShipStartMessage::OnReceive(int) {
    // Current L2Server 0x007cf1a0.
    L2_GUARD_FUNCTION();
    static_cast<void>(CAirShipManager::Inst().SailRegularAirShip());
}

void CVehicleEraseMessage::OnReceive(int) {
    // Current L2Server 0x007cdf24.
    L2_GUARD_FUNCTION();
    g_VehicleDB.Reload();
}

void CVehicleStartMessage::OnReceive(int) {
    // Current L2Server 0x007cdfbc.
    L2_GUARD_FUNCTION();
    g_VehicleDB.StartDrive();
}

void CRegisterPendedStaticObjects::OnReceive(int) {
    // Current L2Server 0x007cf0b8.
    L2_GUARD_FUNCTION();
    g_PendingStaticObjects.AppearInWorld();
    CPledge::InitPledge();
    g_DoorDB.StartDoorTimer();
    CShuttleManager::Inst().AllShuttleEnterWorld();
}

COlympiadObserverChangeFieldMessage::COlympiadObserverChangeFieldMessage(
    const std::uint32_t userSid, const std::int32_t fieldId
) noexcept :
    m_UserSid(userSid), m_FieldId(fieldId) {}

void COlympiadObserverChangeFieldMessage::OnReceive(int) {
    // Current L2Server 0x007ce5e0.
    L2_GUARD_FUNCTION();
    CCreature* const object = g_CreatureSmartIdManager.GetObjectW(m_UserSid);
    User* const user = object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
    if (user != nullptr) {
        user->m_pWOlympiadUser->ChangeObserverField(m_FieldId);
    }
}

CDelayedCampaignStarter::CDelayedCampaignStarter(CCampaign* previousCampaign, CCampaign* nextCampaign) noexcept :
    m_pPrevCampaign(previousCampaign), m_pNextCampaign(nextCampaign) {}

void CDelayedCampaignStarter::OnReceive(int) {
    L2_GUARD_FUNCTION();
    DynamicQuestManager::Inst()->OnContinueCampaign(m_pPrevCampaign, m_pNextCampaign);
}

void CInstantAgitPostLoadMessage::OnReceive(int) {
    // Current L2Server 0x007ce32c, original IOCPMessage_server.cpp owner.
    L2_GUARD_FUNCTION();
    CInstantAgitManager::inst().PostLoad();
    CInstantAgitManager::inst().Load();
}

CPledgeGameObserverChangeFieldMessage::CPledgeGameObserverChangeFieldMessage(
    const std::int32_t userSid, const std::int32_t fieldId
) noexcept :
    m_nUserSid(userSid), m_nFieldId(fieldId) {}

void CPledgeGameObserverChangeFieldMessage::OnReceive(int) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserMap.FindObject(m_nUserSid);
    if (user && user->m_PledgeGameUser.IsObserver()) {
        user->m_PledgeGameUser.ChangeObserverField(m_nFieldId);
    }
}

void CGetOnVehicleIOCPMessage::OnReceive(int) {
    // Current L2Server 0x007ce97c.
    L2_GUARD_FUNCTION();

    if (!m_pActor->IsGMInvisible()) {
        const FVector position = m_pActor->GetPos();
        g_ptlUserMap->BroadcastToNeighbor(
            *m_pActor, 0x80000, position, "cddddd", 0x6e, static_cast<int>(m_pActor->m_nId), m_nVehicleID,
            PositionComponent(vRelPos.x), PositionComponent(vRelPos.y), PositionComponent(vRelPos.z)
        );
    } else if (m_pActor->IsPC()) {
        User* const user = m_pActor->CastUser();
        user->GetSocket()->Send(
            "cddddd", 0x6e, static_cast<int>(m_pActor->m_nId), m_nVehicleID, PositionComponent(vRelPos.x),
            PositionComponent(vRelPos.y), PositionComponent(vRelPos.z)
        );
    }

    L2_RELEASE(m_pActor, ObjectRefType::IocpMessageRetain);
}

void CGetOnShuttleIOCPMessage::OnReceive(int) {
    // Current L2Server 0x007ced04.
    L2_GUARD_FUNCTION();

    if (!m_pActor->IsGMInvisible()) {
        const FVector position = m_pActor->GetPos();
        g_ptlUserMap->BroadcastToNeighbor(
            *m_pActor, 0x80000, position, "chddddd", 0xfe, 0xcc, static_cast<int>(m_pActor->m_nId), m_nShuttleID,
            PositionComponent(vRelPos.x), PositionComponent(vRelPos.y), PositionComponent(vRelPos.z)
        );
    } else if (m_pActor->IsPC()) {
        User* const user = m_pActor->CastUser();
        user->GetSocket()->Send(
            "chddddd", 0xfe, 0xcc, static_cast<int>(m_pActor->m_nId), m_nShuttleID, PositionComponent(vRelPos.x),
            PositionComponent(vRelPos.y), PositionComponent(vRelPos.z)
        );
    }

    L2_RELEASE(m_pActor, ObjectRefType::IocpMessageRetain);
}

CGetOffVehicleIOCPMessage::CGetOffVehicleIOCPMessage(CCreature* creature, std::uint32_t vehicleId, FVector position) :
    m_pCreature(creature), m_nVehicleId(vehicleId), m_alignment24(0), m_vPosition(position) {
    L2_ADD_REF(m_pCreature, ObjectRefType::IocpMessageRetain);
}

void CGetOffVehicleIOCPMessage::OnReceive(int) {
    L2_GUARD_FUNCTION();

    if (m_pCreature->IsPC()) {
        User* const user = m_pCreature->CastUser();
        user->GetSocket()->Send(
            "cddddd", 0x6f, static_cast<int>(m_pCreature->m_nId), static_cast<int>(m_nVehicleId),
            PositionComponent(m_vPosition.x), PositionComponent(m_vPosition.y), PositionComponent(m_vPosition.z)
        );
        if (!m_pCreature->IsGMInvisible()) {
            g_ptlUserMap->BroadcastToNeighborExceptSelf(
                m_pCreature, 0x80000, FVector{}, "cddddd", 0x6f, static_cast<int>(m_pCreature->m_nId),
                static_cast<int>(m_nVehicleId), PositionComponent(m_vPosition.x), PositionComponent(m_vPosition.y),
                PositionComponent(m_vPosition.z)
            );
        }
    }
    L2_RELEASE(m_pCreature, ObjectRefType::IocpMessageRetain);
}

CGetOffShuttleIOCPMessage::CGetOffShuttleIOCPMessage(CCreature* creature, std::uint32_t shuttleId, FVector position) :
    m_pCreature(creature), m_nShuttleId(shuttleId), m_alignment24(0), m_vPosition(position) {
    L2_ADD_REF(m_pCreature, ObjectRefType::IocpMessageRetain);
}

void CGetOffShuttleIOCPMessage::OnReceive(int) {
    L2_GUARD_FUNCTION();

    if (m_pCreature->IsPC()) {
        User* const user = m_pCreature->CastUser();
        user->GetSocket()->Send(
            "chddddd", 0xfe, 0xcd, static_cast<int>(m_pCreature->m_nId), static_cast<int>(m_nShuttleId),
            PositionComponent(m_vPosition.x), PositionComponent(m_vPosition.y), PositionComponent(m_vPosition.z)
        );
        if (!m_pCreature->IsGMInvisible()) {
            g_ptlUserMap->BroadcastToNeighborExceptSelf(
                m_pCreature, 0x80000, FVector{}, "chddddd", 0xfe, 0xcd, static_cast<int>(m_pCreature->m_nId),
                static_cast<int>(m_nShuttleId), PositionComponent(m_vPosition.x), PositionComponent(m_vPosition.y),
                PositionComponent(m_vPosition.z)
            );
        }
    }
    L2_RELEASE(m_pCreature, ObjectRefType::IocpMessageRetain);
}

CNPSendCriticalMailIOCPMessage::CNPSendCriticalMailIOCPMessage() noexcept : m_isNeedToCloseSocket(false), m_nAccountID(0) {
    m_szMessage[0] = '\0';
}

void CNPSendCriticalMailIOCPMessage::OnReceive(int) {
    L2_GUARD_FUNCTION();

    std::array<char, 0x25> npUserGuid{};
    if (m_isNeedToCloseSocket) {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(m_nAccountID);
        if (account != accountMap.end()) {
            CUserSocket* const socket = account->second;
            if (!socket->m_szNPUserGUIDA.empty() && socket->m_szNPUserGUIDA[0] != '\0') {
                std::strncpy(npUserGuid.data(), socket->m_szNPUserGUIDA.data(), npUserGuid.size() - 1);
                npUserGuid.back() = '\0';
            }
            socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        }
    }
    if (m_szMessage[0] != '\0') {
        SystemMail::sendNPCriticalMessage(mailServer, m_szMessage, npUserGuid.data());
    }
}

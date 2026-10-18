#include "AirShipEventManager.h"

#include "L2Core/src/inc/Guard.h"
#include "NPC.h"
#include "NPCEventFactory.h"
#include "NpcEvent.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"

int CAirShipEventManager::Register(unsigned int npcSID, int airportId, int platformId, bool forRegular) {
    // Current L2NPC.exe 0x0040e5ec.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);

    const ManagerKeyType key = std::make_pair(airportId, platformId);
    const ManagerMapType::iterator it_manager = m_ManagerMap.find(key);
    if (it_manager != m_ManagerMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] duplicated airship manager. "
        //     L"airport(%d) platform(%d) npc(%d, %d)",
        //     L"AirShipEventManager.cpp",
        //     0x16,
        //     it_manager->first.first,
        //     it_manager->first.second,
        //     static_cast<int>(
        //         it_manager->second),
        //     static_cast<int>(
        //         npcSID))
        // Log.Add(LOG_ERROR,
        //     "[airship] Cannot register manager NPC %u at airport %d, platform %d: NPC %u already owns this platform.
        //     existing registration retained.", npcSID, airportId, platformId, it_manager->second)
        L2_LOG_ERROR(
            Log,
            "[airship] Cannot register manager NPC %u at airport %d, platform %d: NPC %u already owns this platform; "
            "existing registration retained.",
            npcSID, airportId, platformId, it_manager->second
        );
        return -1;
    }

    const AirShipMapType::iterator it_airship = m_AirShipMap.find(npcSID);
    if (it_airship != m_AirShipMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] duplicated airship manager. npc(%d)",
        //     L"AirShipEventManager.cpp",
        //     0x1f,
        //     static_cast<int>(
        //         npcSID))
        // Log.Add(LOG_ERROR,
        //     "[airship] Cannot register manager NPC %u at airport %d, platform %d: this NPC already has an airship
        //     registration, existing registration retained.", npcSID, airportId, platformId)
        L2_LOG_ERROR(
            Log,
            "[airship] Cannot register manager NPC %u at airport %d, platform %d: this NPC already has an airship "
            "registration; existing registration retained.",
            npcSID, airportId, platformId
        );
        return -1;
    }

    int index = -1;
    if (!forRegular) {
        index = SharedPlatformInfo::Inst()->GetPlatformIndex(airportId, platformId);
        if (index == -1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] cannot find shared platform info. "
            //     L"airport(%d) platform(%d). npc(%d)",
            //     L"AirShipEventManager.cpp",
            //     0x2c,
            //     airportId,
            //     platformId,
            //     static_cast<int>(
            //         npcSID))
            // Log.Add(LOG_ERROR,
            //     "[airship] Cannot register manager NPC %u at airport %d, platform %d: shared platform information was not
            //     found, registration rejected.", npcSID, airportId, platformId)
            L2_LOG_ERROR(
                Log,
                "[airship] Cannot register manager NPC %u at airport %d, platform %d: shared platform information was not "
                "found; registration rejected.",
                npcSID, airportId, platformId
            );
            return -1;
        }
    }

    m_ManagerMap.insert(std::make_pair(key, npcSID));
    m_AirShipMap.insert(std::make_pair(npcSID, 0U));
    return index;
}

unsigned int CAirShipEventManager::GetAirShipSID(unsigned int npcSID) {
    // Current L2NPC.exe 0x0040e210.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);

    const AirShipMapType::iterator it = m_AirShipMap.find(npcSID);
    return it == m_AirShipMap.end() ? 0U : it->second;
}

void CAirShipEventManager::OnPacketArrived(const unsigned char* pPacket) {
    // Current L2NPC.exe 0x0040e334.
    L2_GUARD_FUNCTION();

    int jobType;
    int airportId;
    int platformId;
    unsigned int airshipSID;
    ::Disassemble(pPacket, "dddd", &jobType, &airportId, &platformId, &airshipSID);

    unsigned int npcSID;
    {
        L2_GUARD(m_Lock);
        const ManagerKeyType key = std::make_pair(airportId, platformId);
        const ManagerMapType::iterator it_manager = m_ManagerMap.find(key);
        if (it_manager == m_ManagerMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] airship manager is not registered on "
            //     L"airport(%d) platform(%d)",
            //     L"AirShipEventManager.cpp",
            //     0x5f,
            //     airportId,
            //     platformId)
            // Log.Add(LOG_ERROR,
            //     "[airship] Ignoring event type %d for airship %u at airport %d, platform %d: no manager NPC is
            //     registered.", jobType, airshipSID, airportId, platformId)
            L2_LOG_ERROR(
                Log,
                "[airship] Ignoring event type %d for airship %u at airport %d, platform %d: no manager NPC is registered.",
                jobType, airshipSID, airportId, platformId
            );
            return;
        }

        npcSID = it_manager->second;
        if (jobType == 0 || jobType == 1) {
            const AirShipMapType::iterator it_airship = m_AirShipMap.find(npcSID);
            if (it_airship == m_AirShipMap.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] airship manager is not registered on "
                //     L"airport(%d) platform(%d)",
                //     L"AirShipEventManager.cpp",
                //     0x6d,
                //     airportId,
                //     platformId)
                // Log.Add(LOG_ERROR,
                //     "[airship] Ignoring event type %d for airship %u at airport %d, platform %d: manager NPC %u has no
                //     airship-state entry.", jobType, airshipSID, airportId, platformId, npcSID)
                L2_LOG_ERROR(
                    Log,
                    "[airship] Ignoring event type %d for airship %u at airport %d, platform %d: manager NPC %u has no "
                    "airship-state entry.",
                    jobType, airshipSID, airportId, platformId, npcSID
                );
                return;
            }
            it_airship->second = jobType == 0 ? airshipSID : 0U;
        }
    }

    CNPC* const npc = CNPC::GetObjectW(static_cast<int>(npcSID));
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] airship manager NPC(%d) does not exist on "
        //     L"airport(%d) platform(%d)",
        //     L"AirShipEventManager.cpp",
        //     0x81,
        //     static_cast<int>(
        //         npcSID),
        //     airportId,
        //     platformId)
        // Log.Add(LOG_ERROR,
        //     "[airship] Cannot notify manager NPC %u of event type %d for airship %u at airport %d, platform %d: the NPC
        //     object was not found, AI notification skipped.", npcSID, jobType, airshipSID, airportId, platformId)
        L2_LOG_ERROR(
            Log,
            "[airship] Cannot notify manager NPC %u of event type %d for airship %u at airport %d, platform %d: the NPC "
            "object was not found; AI notification skipped.",
            npcSID, jobType, airshipSID, airportId, platformId
        );
        return;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateAirShipEvent(npc, jobType);
    if (!event) {
        return;
    }
    npc->HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

int CAirShipEventManager::IsOccupiedPlatform(int index) {
    // Current L2NPC.exe 0x0040deac.
    L2_GUARD_FUNCTION();
    return static_cast<int>(SharedPlatformInfo::Inst()->IsOccupied(index));
}

#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class CAirShipEventManager {
public:
    static CAirShipEventManager& Inst() {
        static CAirShipEventManager m_instance;
        return m_instance;
    }

    int Register(unsigned int npcSID, int airportId, int platformId, bool forRegular);
    void OnPacketArrived(const unsigned char* pPacket);
    unsigned int GetAirShipSID(unsigned int npcSID);
    int IsOccupiedPlatform(int index);

private:
    using ManagerKeyType = std::pair<int, int>;
    using ManagerMapType = std::map<ManagerKeyType, unsigned int>;
    using AirShipMapType = std::map<unsigned int, unsigned int>;

    CYieldLock m_Lock;
    ManagerMapType m_ManagerMap;
    AirShipMapType m_AirShipMap;
};

// Platform adapters for the exact object-factory mappings opened by L2NPC.
#pragma once

#include "Shared/src/SharedFactory.h"

#include <string>


inline constexpr const char* kPartyDataSharedMemoryName = "L2World_PartyData";
inline constexpr const char* kPartyIndexSharedMemoryName = "L2World_PartyIndex";
inline constexpr const char* kPledgeDataSharedMemoryName = "L2World_PledgeData";
inline constexpr const char* kPledgeIndexSharedMemoryName = "L2World_PledgeIndex";
inline constexpr const char* kGeneralObjectDataSharedMemoryName = "L2World_GeneralObjectData";
inline constexpr const char* kGeneralObjectIndexSharedMemoryName = "L2World_GeneralObjectIndex";
inline constexpr const char* kAllianceDataSharedMemoryName = "L2World_AllianceData";
inline constexpr const char* kAllianceIndexSharedMemoryName = "L2World_AllianceIndex";
inline constexpr const char* kAirShipDataSharedMemoryName = "L2World_AirShipData";
inline constexpr const char* kAirShipConstantSharedMemoryName = "L2World_AirShipConstant";
inline constexpr const char* kAirShipIndexSharedMemoryName = "L2World_AirShipIndex";

bool AttachPartySharedMemory(
    CPartyFactory& factory,
    const std::string& dataName = kPartyDataSharedMemoryName,
    const std::string& indexName = kPartyIndexSharedMemoryName,
    std::string* error = nullptr
);

bool AttachPledgeSharedMemory(
    CPledgeFactory& factory,
    const std::string& dataName = kPledgeDataSharedMemoryName,
    const std::string& indexName = kPledgeIndexSharedMemoryName,
    std::string* error = nullptr
);

bool AttachGeneralObjectSharedMemory(
    CGeneralObjectFactory& factory,
    const std::string& dataName = kGeneralObjectDataSharedMemoryName,
    const std::string& indexName = kGeneralObjectIndexSharedMemoryName,
    std::string* error = nullptr
);

bool AttachAllianceSharedMemory(
    CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>& factory,
    const std::string& dataName = kAllianceDataSharedMemoryName,
    const std::string& indexName = kAllianceIndexSharedMemoryName,
    std::string* error = nullptr
);

bool AttachAirShipSharedMemory(
    CAirShipFactory& factory,
    const std::string& dataName = kAirShipDataSharedMemoryName,
    const std::string& constantName = kAirShipConstantSharedMemoryName,
    const std::string& indexName = kAirShipIndexSharedMemoryName,
    std::string* error = nullptr
);

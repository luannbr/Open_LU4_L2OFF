#pragma once

#include "Shared/src/OlympiadDefine.h"
#include "Shared/src/proto/OlympiadPacket.h"

#include <cstdint>

class WOlympiadSocketHost;
class WOlympiadSocketPeer;

namespace PledgeRaidNetworkUtil {

bool DecodePacket(network::IPacket& packet, const unsigned char* source);
bool IsHost();
bool IsPeer();
// Authorized combined-role mode: the host world is also listed in <Allow>.
bool IsLocalWorld();

} // namespace PledgeRaidNetworkUtil

namespace PledgeRaidPacketHandler {

bool DispatchLocalRequest(OCS_PROTOCOL protocol, int worldId, const std::uint8_t* packet);
bool DispatchLocalResponse(OSC_PROTOCOL protocol, const std::uint8_t* packet);

} // namespace PledgeRaidPacketHandler

namespace PledgeRaidNetworkHost {

bool SendPacket(int worldId, OSC_PROTOCOL protocol, network::IPacket& packet);

} // namespace PledgeRaidNetworkHost

namespace PledgeRaidNetworkPeer {

bool IsAvailable();
bool SendPacket(OCS_PROTOCOL protocol, network::IPacket& packet);

} // namespace PledgeRaidNetworkPeer

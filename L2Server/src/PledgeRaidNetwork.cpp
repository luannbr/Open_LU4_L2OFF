#include "PledgeRaidNetwork.h"

#include "CConfig.h"
#include "IOCPMessageServer.h"
#include "OlympiadNetwork.h"
#include "OlympiadStuff.h"

#include "Shared/src/Log.h"
#include "Shared/src/proto/Stream.h"

#include <array>
#include <memory>

namespace {

template <typename Dispatch>
bool PostLocalPacket(network::IPacket& packet, Dispatch dispatch) {
    // Intentional extension of the original peer-only transport. Keep the wire
    // codec and defer delivery: score retries may hold a lock also used by replies.
    auto bytes = std::make_shared<std::array<unsigned char, 0x4000>>();
    network::Stream stream(bytes->data(), static_cast<int>(bytes->size()));
    if (!packet.Encode(&stream)) {
        L2_LOG_ERROR(Log, "Cannot encode local clan-raid packet; request was not queued.");
        return false;
    }
    PostLambdaMessage([bytes, dispatch]() {
        if (dispatch(bytes->data())) {
            L2_LOG_ERROR(Log, "Local clan-raid packet was rejected by its handler.");
        }
    });
    return true;
}

} // namespace

bool PledgeRaidNetworkUtil::DecodePacket(network::IPacket& packet, const unsigned char* source) {
    L2_GUARD_FUNCTION();
    if (!source) {
        return false;
    }
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    return packet.Decode(&stream);
}

bool PledgeRaidNetworkUtil::IsHost() { return Singleton<WOlympiadServiceConfig>::inst().IsHost(); }

bool PledgeRaidNetworkUtil::IsPeer() { return Singleton<WOlympiadServiceConfig>::inst().IsPeer(); }

bool PledgeRaidNetworkUtil::IsLocalWorld() {
    const auto& config = Singleton<WOlympiadServiceConfig>::inst();
    return config.IsActive() && config.IsHost() && config.IsAllowedPeer(g_nWorldId);
}

bool PledgeRaidNetworkHost::SendPacket(int worldId, OSC_PROTOCOL protocol, network::IPacket& packet) {
    if (worldId == g_nWorldId && PledgeRaidNetworkUtil::IsLocalWorld()) {
        return PostLocalPacket(packet, [protocol](const unsigned char* bytes) {
            return PledgeRaidPacketHandler::DispatchLocalResponse(protocol, bytes);
        });
    }
    return Singleton<WOlympiadNetworkHost>::inst().SendPacket(worldId, protocol, packet);
}

bool PledgeRaidNetworkPeer::IsAvailable() {
    // Original: return Singleton<WOlympiadNetworkPeer>::inst().IsAvailable()
    return PledgeRaidNetworkUtil::IsLocalWorld() || Singleton<WOlympiadNetworkPeer>::inst().IsAvailable();
}

bool PledgeRaidNetworkPeer::SendPacket(OCS_PROTOCOL protocol, network::IPacket& packet) {
    if (PledgeRaidNetworkUtil::IsLocalWorld()) {
        const int worldId = g_nWorldId;
        return PostLocalPacket(packet, [protocol, worldId](const unsigned char* bytes) {
            return PledgeRaidPacketHandler::DispatchLocalRequest(protocol, worldId, bytes);
        });
    }
    return Singleton<WOlympiadNetworkPeer>::inst().SendPacket(protocol, packet);
}

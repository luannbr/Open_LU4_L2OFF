#include "Portable/AdminRewardTransport.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace portable {
namespace {

struct AdminRewardTransport {
    std::mutex mutex;
    std::unordered_map<const AdminRewardSystem*, AdminRewardPacketSink> packetSinks;
};

AdminRewardTransport& Transport() {
    static AdminRewardTransport transport;
    return transport;
}

} // namespace

AdminRewardPacketBinding::AdminRewardPacketBinding(const AdminRewardSystem* rewardSystem, AdminRewardPacketSink sink) :
    rewardSystem_(rewardSystem) {
    BindAdminRewardPacketSink(rewardSystem_, std::move(sink));
}

AdminRewardPacketBinding::~AdminRewardPacketBinding() { BindAdminRewardPacketSink(rewardSystem_, {}); }

void BindAdminRewardPacketSink(const AdminRewardSystem* rewardSystem, AdminRewardPacketSink sink) {
    if (!rewardSystem) {
        return;
    }

    AdminRewardTransport& transport = Transport();
    std::lock_guard<std::mutex> guard(transport.mutex);
    if (sink) {
        transport.packetSinks[rewardSystem] = std::move(sink);
    } else {
        transport.packetSinks.erase(rewardSystem);
    }
}

void SendAdminRewardPacket(
    const AdminRewardSystem* rewardSystem, std::uint8_t opcode, const std::vector<std::uint8_t>& payload
) {
    if (!rewardSystem) {
        return;
    }

    AdminRewardTransport& transport = Transport();
    AdminRewardPacketSink sink;
    {
        std::lock_guard<std::mutex> guard(transport.mutex);
        const auto found = transport.packetSinks.find(rewardSystem);
        if (found != transport.packetSinks.end()) {
            sink = found->second;
        }
    }
    if (sink) {
        sink(opcode, payload);
    }
}

} // namespace portable

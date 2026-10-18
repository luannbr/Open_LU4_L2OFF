#pragma once

#include <cstdint>
#include <functional>
#include <vector>


class AdminRewardSystem;

namespace portable {

using AdminRewardPacketSink = std::function<void(std::uint8_t, const std::vector<std::uint8_t>&)>;

class AdminRewardPacketBinding {
public:
    AdminRewardPacketBinding(const AdminRewardSystem* rewardSystem, AdminRewardPacketSink sink);
    ~AdminRewardPacketBinding();

    AdminRewardPacketBinding(const AdminRewardPacketBinding&) = delete;
    AdminRewardPacketBinding& operator=(const AdminRewardPacketBinding&) = delete;

private:
    const AdminRewardSystem* rewardSystem_;
};

// The target sends through the process-global NPCServer instance. These
// functions bind that process-global boundary to the portable transport
// without adding host state or methods to AdminRewardSystem.
void BindAdminRewardPacketSink(const AdminRewardSystem* rewardSystem, AdminRewardPacketSink sink);
void SendAdminRewardPacket(
    const AdminRewardSystem* rewardSystem, std::uint8_t opcode, const std::vector<std::uint8_t>& payload
);

} // namespace portable

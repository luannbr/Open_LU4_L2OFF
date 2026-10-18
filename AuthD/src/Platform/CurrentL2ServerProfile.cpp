#include "Platform/CurrentL2ServerProfile.h"

#include <atomic>

namespace authd::compatibility {
namespace {

std::atomic_bool currentL2ServerProfile{false};

} // namespace

void ConfigureCurrentL2ServerProfile(bool enabled) noexcept {
    currentL2ServerProfile.store(
        enabled,
        std::memory_order_relaxed);
}

bool SuppressWorldReply(std::uint8_t opcode) noexcept {
    // Selected L2Authd.exe AccountDB::StartCharge (0x00412790) emits
    // AS_CHARGE_START_ACK = 44. The current L2Server's 34-entry receive
    // table ends at 33, although that same image sends charge-start request
    // 39. Suppressing only this unsupported acknowledgement keeps the
    // recovered charging transition while keeping the current socket open.
    return opcode == 44 && currentL2ServerProfile.load(
                               std::memory_order_relaxed);
}

} // namespace authd::compatibility

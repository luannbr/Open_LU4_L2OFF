#pragma once

#include <cstdint>

namespace authd::compatibility {

// The recovered baseline is unchanged when this profile is disabled.
void ConfigureCurrentL2ServerProfile(bool enabled) noexcept;

// Current L2Server.exe CAuthSocket::OnRead (0x004AACAC) rejects world
// replies above opcode 33. The selected AuthD's dual-charge path emits one
// later acknowledgement (opcode 44), which the current peer never consumes.
bool SuppressWorldReply(std::uint8_t opcode) noexcept;

} // namespace authd::compatibility

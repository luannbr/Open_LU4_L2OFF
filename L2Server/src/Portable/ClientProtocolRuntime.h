#pragma once

#include <cstdint>

class CUserSocket;
class CSocket;

namespace runtime::client_protocol {

inline constexpr std::int32_t kCurrentRevision = 162;
inline constexpr std::int32_t kNorthAmericanRevision = 152;
// LU4 (Liberta Ultimata 4) client, Portable/Lu4Protocol.h. Game logic sees it
// as the current revision; its packets are translated at the socket. Once
// recorded it stays until the socket is forgotten: the translated handshake
// records the current revision afterwards.
inline constexpr std::int32_t kLu4Revision = 62025;

// The current CUserSocket layout has no protocol-revision lane because the
// shipping server accepts only revision 162. Alternate portable revisions
// therefore live outside the ABI-exact object.
void RecordNegotiatedRevision(const CUserSocket* socket, std::int32_t revision);

std::int32_t NegotiatedRevision(const CUserSocket* socket) noexcept;

inline bool UsesNorthAmericanProtocol(const CUserSocket* socket) noexcept {
    return NegotiatedRevision(socket) == kNorthAmericanRevision;
}

// Lets mixed-revision broadcasts keep the exact current fast path until a
// P152 socket is actually present. The shipping object layout remains free
// of portable revision state.
bool HasNorthAmericanClients() noexcept;
bool HasLu4Clients() noexcept;
inline bool UsesLu4Protocol(const CUserSocket* socket) noexcept {
    return HasLu4Clients() && NegotiatedRevision(socket) == kLu4Revision;
}

void ForgetSocket(const CUserSocket* socket) noexcept;

using IncomingPacketCallback = bool (*)(CSocket*, const std::uint8_t*);

// Portable multi-revision dispatch. The shipping revision-162 callback maps
// remain the source of truth and are never mutated. The P152 maps copy that
// baseline and override only audited revision differences.
IncomingPacketCallback ResolveBasicIncomingCallback(
    const CUserSocket* socket, int opcode, IncomingPacketCallback currentCallback
) noexcept;

IncomingPacketCallback ResolveExtendedIncomingCallback(
    const CUserSocket* socket, int opcode, int payloadSize, IncomingPacketCallback currentCallback
) noexcept;

int ExtendedPolicyOpcode(const CUserSocket* socket, int opcode) noexcept;

} // namespace runtime::client_protocol

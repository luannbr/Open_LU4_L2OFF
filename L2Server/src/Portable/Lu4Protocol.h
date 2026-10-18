#pragma once

// Game protocol of the LU4 (Liberta Ultimata 4) client. Portable addition,
// not target code.
//
// LU4 numbers its packets differently and uses older (Interlude-like) bodies.
// Game logic keeps working with this server's revision-162 packets: client
// packets are rewritten into them on arrival (CUserSocket::OnRead), and
// server packets are rewritten or dropped just before encryption
// (CSocket::s_outboundHook). The LU4 client closes on any packet it does not
// know, so an untranslated server packet is dropped and logged once.
//
// An LU4 connection is recognised by its first packet, c 0x00, d 62025, d uid,
// d 20, sent before encryption; it is then recorded as revision kLu4Revision
// (Portable/ClientProtocolRuntime.h). Byte layouts come from the official
// service capture (D:\LU4\tools\capture-analysis) and the Lucera port.

#include <cstdint>
#include <vector>

class CSocket;
class CUserSocket;

namespace lu4 {

bool IsVersionPacket(const std::uint8_t* packet, int length);

// Rewrites an LU4 client packet into this server's form (opcode first) in
// `out`, padded with zeros. Returns false when the packet has no translation.
bool TranslateIncoming(CUserSocket* socket, const std::uint8_t* packet, int length, std::vector<std::uint8_t>& out);

// Installs the outbound hook (CSocket::s_outboundHook).
void Install();

} // namespace lu4

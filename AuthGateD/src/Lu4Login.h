#pragma once

// Login wire of the LU4 (Liberta Ultimata 4) client. Portable addition, not
// part of the original AuthGateD; enabled by lu4Mode in etc/config.txt.
//
// The client is VMProtect-protected. The 32-byte MAC that follows every server
// reply and the RSA key of its credentials block are still not reversed. The
// Blowfish key WAS recovered (2026-10-20): it is a standard Blowfish with the
// 16-byte raw key below, obtained offline by inverting the captured key
// schedule in etc/lu4_auth_key.bin (D:\LU4\client-mods\lu4bin-unpacked\
// scripts\bf_invert.py; re-expansion reproduces the table exactly).
//   raw key (big-endian) = BC AF 64 50 71 3E 25 CF E1 72 C2 F4 BE BE 39 B2
//   -> CBlowfishWrapper::Init(key, 16) now reproduces lu4_auth_key.bin, so the
//      captured blob can be replaced by the key. Derivation from the Init
//      (seed scheme) is still not the official GenerateInitialKey.
// What works (proven by the Lucera port in D:\LU4):
//   - send the official Init captured from the live service, unchanged;
//   - encrypt with the Blowfish table the client expands from that Init
//     (captured from its memory: etc/lu4_auth_key.bin, P[18] then S[4][256];
//      equivalently BF_set_key with the 16-byte raw key above);
//   - put a marker in place of the MAC; lu4authhook.dll, injected into the
//     client, accepts it;
//   - take account and password from lu4authhook.dll, which sends them to the
//     AuthD validator before the client connects (the credentials block in
//     the login packet is unreadable).
// Client opcodes: 0x00 login, 0x01 server list, 0x03 server select (0x02 here).
// Each reply: [H length][data][32-byte MAC][34-byte block], then the usual
// zero padding to 8 bytes, checksum and 4 zero bytes.

#include "Blowfish.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lu4 {

bool LoadKeyTable(const std::filesystem::path& file, std::string& error);
const Blowfish_matrix& KeyTable();

// The official Init, 2-byte length included.
const std::vector<unsigned char>& InitPacket();

// LU4 client opcode -> this AuthGateD's opcode; -1 when unknown.
int MapClientOpcode(unsigned char opcode);

// Rewrites an AuthD reply (opcode first) into the LU4 form. Returns false when
// the reply is sent unchanged.
bool ConvertReply(const unsigned char* packet, int length, std::vector<unsigned char>& out);

} // namespace lu4

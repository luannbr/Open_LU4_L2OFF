#pragma once

#include <cstdarg>
#include <cstdint>

// Packet codec implemented once by Shared/ioc_packet.cpp. It is separated
// from the socket/runtime declarations in ioc.h so database-side owners can
// serialize target packets without depending on L2NPC runtime classes.
const char* StrNChr(const char* str, char character, std::uint64_t length);
const unsigned char* VDisassemble(const unsigned char* packet, const char* format, va_list arguments);
const unsigned char* Disassemble(const unsigned char* packet, const char* format, ...);
int VAssemble(char* buffer, int bufferLength, const char* format, va_list arguments);
int Assemble(char* buffer, int bufferLength, const char* format, ...);

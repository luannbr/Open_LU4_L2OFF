#pragma once

#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>

void ThrowDeadlockException();

in_addr StrToInet(const char16_t* address) noexcept;
char16_t* InetToStr(
    const in_addr* address,
    char16_t* destination,
    std::uint32_t destinationCount) noexcept;
char16_t* GetFullDate(
    char16_t* destination,
    std::uint32_t destinationCount) noexcept;

int LNStrNCpy(
    char* destination,
    const char* source,
    int destinationCount) noexcept;

// Target packet assembler formats:
//   c byte, h little-endian uint16, d little-endian uint32,
//   s zero-terminated narrow text, S zero-terminated UTF-16LE text,
//   b byte-count followed by a byte pointer.
int Assemble(
    char* destination,
    int destinationCount,
    const char* format,
    va_list arguments);
int Assemble(
    char* destination,
    int destinationCount,
    const char* format,
    ...);

std::u16string InetToString(const in_addr& address);
std::string Utf16Ascii(const char16_t* value);

const char* GetProductVersion() noexcept;

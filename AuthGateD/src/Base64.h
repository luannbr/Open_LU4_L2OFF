#pragma once

#include <cstdint>

// Original owner: base64.cpp. The target returns the number of output
// characters including the terminating NUL.
std::uint32_t Base64Encode(
    std::uint32_t inputLength,
    const unsigned char* input,
    std::uint32_t outputCapacity,
    char* output) noexcept;

#pragma once

#include <cstddef>
#include <cstdint>

namespace runtime {

// Common implementation behind the compiled AuthD and
// AuthGateD Base64 entry points. Both selected executables use the same
// alphabet, capacity contract and return count (including the trailing NUL).
// Product-owned wrappers remain in their PDB-recorded compilation units.
template <typename Character>
std::uint32_t EncodeBase64Target(
    std::uint32_t inputLength, const unsigned char* input, std::uint32_t outputCapacity, Character* output
) noexcept {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                        "abcdefghijklmnopqrstuvwxyz"
                                        "0123456789+/";

    if (!input || !output) {
        return 0;
    }
    const std::uint32_t required = ((inputLength + 2U) / 3U) * 4U + 1U;
    if (outputCapacity < required) {
        return 0;
    }

    Character* const outputBase = output;
    while (inputLength != 0) {
        output[0] = static_cast<Character>(kAlphabet[input[0] >> 2U]);
        if (inputLength == 1) {
            output[1] = static_cast<Character>(kAlphabet[(input[0] & 3U) << 4U]);
            output[2] = static_cast<Character>('=');
            output[3] = static_cast<Character>('=');
            output += 4;
            break;
        }

        output[1] = static_cast<Character>(kAlphabet[(input[1] >> 4U) + ((input[0] & 3U) << 4U)]);
        if (inputLength == 2) {
            output[2] = static_cast<Character>(kAlphabet[(input[1] & 0x0fU) << 2U]);
            output[3] = static_cast<Character>('=');
            output += 4;
            break;
        }

        output[2] = static_cast<Character>(kAlphabet[((input[1] & 0x0fU) << 2U) + (input[2] >> 6U)]);
        output[3] = static_cast<Character>(kAlphabet[input[2] & 0x3fU]);
        input += 3;
        inputLength -= 3;
        output += 4;
    }

    *output = Character{};
    return static_cast<std::uint32_t>(output + 1 - outputBase);
}

} // namespace runtime

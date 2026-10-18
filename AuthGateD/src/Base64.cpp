#include "Base64.h"

#include "Shared/src/Runtime/Base64.h"

// AuthGateD.exe 0x0040C670, base64.cpp. The public PDB symbol at 0x0040C740
// is the folded entry into this exact body.
std::uint32_t Base64Encode(
    std::uint32_t inputLength, const unsigned char* input, std::uint32_t outputCapacity, char* output
) noexcept {
    return runtime::EncodeBase64Target(inputLength, input, outputCapacity, output);
}

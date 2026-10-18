#pragma once

#include <cstdint>
#include <map>
#include <string>

// Current L2Core baseconverter.cpp owner. The Windows image stores the
// reverse alphabet in a 0x20-byte std::map and performs every conversion in
// 32-bit modular arithmetic.
class CBaseConverter {
public:
    CBaseConverter();

    void ConvertToInt(
        std::uint32_t base,
        const std::u16string& value,
        std::uint32_t* result) const;
    int ReversedNumber(char16_t value) const;

private:
    std::map<char16_t, int>
        m_ReverseTable;
};


// L2Core/baseconverter.cpp owns the legacy four-argument encoder.
// The matching NASC64_2015 PDB supplies the original namespace and signature.
// current L2NPC.exe 0x00551fa4 supplies the shipping body used by SystemMail.
namespace Base64 {

bool Encode(char* source, int length, char* destination, int* outputLength);

} // namespace Base64

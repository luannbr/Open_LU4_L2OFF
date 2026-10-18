#include "Shared/src/StringTokenizer.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <cstdint>
#include <cstring>

namespace {

int TargetLength(std::size_t value) {
    // The shipping code reads the low 32-bit string length into an int.
    return runtime::Int32FromBits(static_cast<std::uint32_t>(value));
}
std::ptrdiff_t StringIndex(int location, int relative) {
    // The target sign-extends both dwords before adding them for the address.
    return static_cast<std::ptrdiff_t>(static_cast<std::int64_t>(location) + static_cast<std::int64_t>(relative));
}

} // namespace

StringTokenizer::StringTokenizer(std::u16string str, std::u16string deli) : m_sStr(str), m_sDeli(deli), m_str(str) {
    // Current L2NPC.exe 0x00539d98. Current Cached.exe 0x0059951c.
    // The store is in the constructor body, after all three string copies.
    m_pLoc = 0;
}

// Current Cached.exe emits the body at 0x00599424 and its scalar deleting
// destructor adapter at 0x005994ec. The host compiler owns the matching
// portable deleting adapter, the three std::u16string members are destroyed in the
// same reverse order.
StringTokenizer::~StringTokenizer() = default;

int StringTokenizer::IsPreSame(const char16_t* str, const char16_t* deli) {
    // Current L2NPC.exe 0x00539c64 and Cached.exe 0x005993e8 are
    // wcsncmp(deli, str, wcslen(deli)) != 0.
    for (std::size_t index = 0; deli[index] != u'\0'; ++index) {
        if (deli[index] != str[index]) {
            return 1;
        }
    }
    return 0;
}

int StringTokenizer::CountTokens() {
    // Current L2NPC.exe 0x00539ec0. Current Cached.exe 0x00599660. A token is
    // one contiguous run not
    // occupied by the complete delimiter. Repeated delimiters are skipped.
    const std::u16string str = m_sStr;
    const std::u16string deli = m_sDeli;
    int location = 0;
    int count = 0;
    int runLength = 0;
    while (location < TargetLength(m_sStr.size())) {
        if (IsPreSame(str.c_str() + StringIndex(location, 0), deli.c_str()) == 0) {
            location =
                runtime::AddInt32Wrapping(location, runtime::SubtractInt32Wrapping(TargetLength(m_sDeli.size()), 1));
            runLength = 0;
        } else {
            runLength = runtime::AddInt32Wrapping(runLength, 1);
            if (runLength == 1) {
                count = runtime::AddInt32Wrapping(count, 1);
            }
        }
        location = runtime::AddInt32Wrapping(location, 1);
    }
    return count;
}

std::u16string StringTokenizer::NextToken() {
    // Current L2NPC.exe 0x0053a0cc. Current Cached.exe 0x0059987c. The target
    // advances m_pLoc only when it
    // encounters a delimiter. So repeated calls after the final
    // unterminated token return that same token again.
    std::u16string result;
    const std::u16string deli = m_sDeli;
    int relative = 0;
    while (relative < runtime::SubtractInt32Wrapping(TargetLength(m_sStr.size()), m_pLoc)) {
        if (IsPreSame(m_sStr.c_str() + StringIndex(m_pLoc, relative), deli.c_str()) == 0) {
            m_pLoc = runtime::AddInt32Wrapping(m_pLoc, runtime::AddInt32Wrapping(TargetLength(deli.size()), relative));
            break;
        }

        const char16_t character = m_sStr.c_str()[StringIndex(m_pLoc, relative)];
        // Cached.exe creates this xwstring-plus-character helper at
        // 0x005997c0. It reserves length+1, copies the source and appends the
        // UTF-16 unit. The std::u16string expression preserves this append behavior.
        result = result + character;
        relative = runtime::AddInt32Wrapping(relative, 1);
    }
    return result;
}

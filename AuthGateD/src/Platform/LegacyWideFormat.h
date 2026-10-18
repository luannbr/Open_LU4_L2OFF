#pragma once

#include <cstdarg>
#include <cstddef>
#include <string>

namespace authgated::portable {

// Host-width-independent entry to the selected Win32 UTF-16 printf rules.
// FileLog owns the recovered formatter. UI owners reuse it through this thin
// transport boundary instead of growing a second formatter.
bool FormatTargetWide(
    std::u16string& output,
    std::size_t capacity,
    const char16_t* format,
    va_list arguments);

} // namespace authgated::portable

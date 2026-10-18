#pragma once

#include <string>
#include <string_view>

namespace nasc {

// Strict Unicode conversion independent of locale and native wchar_t width.
// Invalid UTF-8 and unpaired UTF-16 surrogates throw std::invalid_argument.
[[nodiscard]] std::u16string Utf8ToUtf16(std::string_view input);
[[nodiscard]] std::string Utf16ToUtf8(std::u16string_view input);

} // namespace nasc

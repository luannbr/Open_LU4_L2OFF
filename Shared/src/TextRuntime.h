#pragma once

#include "Shared/src/Runtime/TargetWideChar.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>


namespace target_utf16 {

// A recovered Win64 wchar_t argument is already a sequence of UTF-16 code
// units on Windows. Portable hosts instead transcode their native wide text.
// Keeping this adapter distinct keeps unmatched Windows code units on the
// target platform without pretending that host wchar_t has a portable width.
std::u16string FromRecoveredWindowsWide(std::wstring_view value);
std::u16string FromRecoveredWindowsWide(const wchar_t* value);

// One host unit per target code unit. These operations do not
// combine or validate surrogate pairs and are only for proven Windows
// code-unit algorithms.
std::u16string FromHostCodeUnits(std::wstring_view value);
std::u16string FromHostCodeUnits(const wchar_t* value);
std::wstring ToHostCodeUnits(std::u16string_view value);
std::wstring ToHostCodeUnits(const char16_t* value);
std::wstring ToHostCodeUnitsBounded(const char16_t* value, std::size_t capacity);
std::wstring ToHostCodeUnits(const SharedUtilsWideChar* value, std::size_t length);

std::size_t Length(const char16_t* value) noexcept;
std::size_t BoundedLength(const char16_t* value, std::size_t capacity) noexcept;
// std::wcsncmp for UTF-16: compare at most count units and stop at NUL.
int CompareN(const char16_t* left, const char16_t* right, std::size_t count) noexcept;
// std::wcschr for UTF-16. Searching for NUL finds the terminator.
const char16_t* Find(const char16_t* value, char16_t unit) noexcept;
char16_t* Find(char16_t* value, char16_t unit) noexcept;

// Wire UTF-16 is always little-endian and may be unaligned.
char16_t LoadLittleEndian(const void* source) noexcept;
void StoreLittleEndian(void* destination, char16_t value) noexcept;

} // namespace target_utf16

// A borrowed UTF-16 text argument. It takes a NUL-terminated pointer, where
// null means empty text, a string or a view. The caller keeps the storage
// alive during the call.
class Utf16Text {
public:
    Utf16Text(std::nullptr_t) noexcept {}
    Utf16Text(const char16_t* text) noexcept : view_(text != nullptr ? std::u16string_view(text) : std::u16string_view()) {}
    Utf16Text(const std::u16string& text) noexcept : view_(text) {}
    Utf16Text(std::u16string_view text) noexcept : view_(text) {}

    // False only for a null pointer. Empty text is still present.
    explicit operator bool() const noexcept { return view_.data() != nullptr; }
    std::u16string_view View() const noexcept { return view_; }
    std::u16string String() const { return std::u16string(view_); }

private:
    std::u16string_view view_;
};


// Unicode conversions replace malformed input with U+FFFD unless marked Strict.
// Native-wide functions are for platform and compatibility API boundaries.
std::u16string Utf8ToUtf16(std::string_view value);
std::string Utf16ToUtf8(std::u16string_view value);
// C-string overloads treat null as empty, matching the native-wide boundary.
std::string Utf16ToUtf8(const char16_t* value);
std::string Utf16ToUtf8(const std::u16string& value);
#if defined(_WIN32)
std::string Utf16ToUtf8(const wchar_t* value);
#endif
// Bound by UTF-16 units without splitting a valid surrogate pair.
std::string Utf16ToUtf8(std::u16string_view value, std::size_t maximumCodeUnits);
// Some legacy wide APIs keep malformed UTF-16 units. Keep that policy
// explicit at those boundaries. Ordinary Unicode conversions still replace them.
enum class Utf16WidePolicy { ReplaceInvalid, PreserveInvalid, PreserveInvalidOnWindows };
std::wstring Utf16ToWide(std::u16string_view value, Utf16WidePolicy policy);
std::wstring Utf16ToWide(const char16_t* value, Utf16WidePolicy policy);
#if defined(_WIN32)
std::wstring Utf16ToWide(const wchar_t* value, Utf16WidePolicy policy = Utf16WidePolicy::ReplaceInvalid);
#endif

// Bounded, allocation-free legacy conversion into caller-owned storage. Clears
// the buffer, terminates it, combines valid pairs on 32-bit wchar_t hosts and
// keeps unmatched units. No thread-local storage is owned by TextRuntime.
wchar_t* CopyUtf16ToWide(
    const char16_t* value, std::size_t inputCapacity, wchar_t* output, std::size_t outputCapacity
) noexcept;
template <std::size_t Capacity>
wchar_t* CopyUtf16ToWide(const char16_t* value, std::size_t inputCapacity, std::array<wchar_t, Capacity>& output) noexcept {
    return CopyUtf16ToWide(value, inputCapacity, output.data(), output.size());
}

// Equivalent to the former UTF-16 -> wide -> UTF-16 HTML C-string round trip:
// replace unmatched surrogates and stop at the first NUL, with no wide buffer.
std::u16string SanitizeUtf16(std::u16string_view value);
std::u16string SanitizeUtf16(const char16_t* value);
std::u16string SanitizeUtf16(const std::u16string& value);
#if defined(_WIN32)
std::u16string SanitizeUtf16(const wchar_t* value);
#endif

std::wstring Utf16ToWide(std::u16string_view value);
std::wstring Utf16ToWide(const char16_t* value);
std::wstring Utf16ToWide(const std::u16string& value);
std::u16string WideToUtf16(std::wstring_view value);
std::u16string WideToUtf16(const wchar_t* value);
std::wstring Utf8ToWide(std::string_view value);
// Equivalent to MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...): an
// invalid sequence rejects the complete input instead of inserting U+FFFD.
// Empty input and rejected input both produce the empty string, matching the
// recovered named-mapping call sites.
std::wstring Utf8ToWideStrict(std::string_view value);
std::string WideToUtf8(std::wstring_view value);

// Win64 keeps long at 32 bits. These are the cross-platform boundary for the
// target CRT's _wtol/wcstol(base 10) behavior, including prefix parsing and
// 32-bit saturation on overflow.
std::int32_t WindowsWtol(std::string_view value) noexcept;
std::int32_t WindowsWtol(std::wstring_view value) noexcept;
std::int32_t WindowsWtol(std::u16string_view value) noexcept;
// The same decimal rules as wcstoll(base 10), with 64-bit saturation.
std::int64_t WindowsWcstoi64(std::u16string_view value) noexcept;

// MSVCRT's wide printf family treats an unqualified %s as wchar_t* and %S
// as char*. POSIX follows the ISO wide-printf convention instead, and also
// does not understand Microsoft's I32/I64 length modifiers. Recovered target
// format strings pass through this one portability boundary before reaching
// a host vswprintf implementation.
std::wstring WindowsWideFormatToHost(std::wstring_view format);

// Windows wide printf accepts UTF-16 literals of the process
// C locale. Darwin and other POSIX hosts can reject the same non-ASCII
// format with EILSEQ while running in the default "C" locale, so recovered
// wsprintfW/vswprintf call sites use this boundary as well as the format-
// specifier translation above.
int WindowsVswprintf(wchar_t* destination, std::size_t capacity, const wchar_t* format, std::va_list arguments);
int WindowsSwprintf(wchar_t* destination, std::size_t capacity, const wchar_t* format, ...);

template <std::size_t Capacity>
std::string FixedUtf16ToUtf8(const char16_t (&value)[Capacity]) {
    std::size_t length = 0;
    while (length < Capacity && value[length] != u'\0') {
        ++length;
    }
    return Utf16ToUtf8(std::u16string_view(value, length));
}

template <std::size_t Capacity>
std::u16string_view FixedUtf16View(const char16_t (&value)[Capacity]) {
    return std::u16string_view(value, target_utf16::BoundedLength(value, Capacity));
}

template <typename Character, std::size_t Capacity>
std::u16string_view FixedUtf16View(const std::array<Character, Capacity>& value) {
    static_assert(sizeof(Character) == sizeof(char16_t), "target UTF-16 arrays must use 16-bit code units");
    std::size_t length = 0;
    while (length < Capacity && value[length] != Character{}) {
        ++length;
    }
    return std::u16string_view(reinterpret_cast<const char16_t*>(value.data()), length);
}

template <typename Character, std::size_t Capacity>
std::wstring FixedUtf16ToWide(const std::array<Character, Capacity>& value) {
    return Utf16ToWide(FixedUtf16View(value));
}

// CNPC::SubStr indexes Windows wchar_t/UTF-16 code units, not UTF-8 bytes.
// Portable native implementations use UTF-8 after the VM's explicit
// UTF-16 boundary conversion, so this helper keeps target code-unit
// indexing before returning to that portable boundary.
std::string Utf16Substring(const char* utf8, int startCodeUnit, int codeUnitCount);

// C-array counterpart of the existing bounded std::array conversion.
template <std::size_t Capacity>
std::wstring FixedUtf16ToWide(const char16_t (&value)[Capacity]) {
    return Utf16ToWide(std::u16string_view(value, target_utf16::BoundedLength(value, Capacity)));
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

// printf-style formatting straight into UTF-16 game text.
//
// The format string follows the MSVC wide printf rules of the original
// server: %s, %ls and %ws print a string, %S and %hs print a string too, %c
// prints one UTF-16 unit and the I, I32 and I64 length modifiers work. An
// integer is cut to the size that the length modifier names, as printf reads
// it on Win64, where int and long are 32 bits.
//
// The arguments are typed, so a wrong argument cannot read the stack. A string
// argument can be UTF-16, native wide or UTF-8 text. The result does not
// depend on the string kind that the format names. A null string pointer
// prints "(null)", as in the MSVC CRT. Precision and width count UTF-16 units.
namespace text_format {

struct Argument {
    enum class Kind : std::uint8_t { Signed, Unsigned, Floating, Pointer, Utf16, Wide, Utf8 };

    // A string argument without a known length ends at its first NUL.
    static constexpr std::size_t kNulTerminated = static_cast<std::size_t>(-1);

    Kind kind = Kind::Signed;
    std::int64_t signedValue = 0;
    std::uint64_t unsignedValue = 0;
    double floatingValue = 0.0;
    const void* pointer = nullptr;
    std::size_t length = kNulTerminated;
};

namespace detail {

template <class>
inline constexpr bool kUnsupportedArgument = false;

template <class Character>
inline constexpr bool kTextCharacter =
    std::is_same_v<Character, char16_t> || std::is_same_v<Character, wchar_t> || std::is_same_v<Character, char>;

template <class Character>
constexpr Argument::Kind TextKind() {
    if constexpr (std::is_same_v<Character, char16_t>) {
        return Argument::Kind::Utf16;
    } else if constexpr (std::is_same_v<Character, wchar_t>) {
        return Argument::Kind::Wide;
    } else {
        return Argument::Kind::Utf8;
    }
}

template <class Character>
Argument TextArgument(const Character* text, std::size_t length) {
    Argument argument;
    argument.kind = TextKind<Character>();
    argument.pointer = text;
    argument.length = length;
    return argument;
}

template <class Integer>
Argument IntegerArgument(Integer value) {
    Argument argument;
    if constexpr (std::is_signed_v<Integer>) {
        argument.kind = Argument::Kind::Signed;
        argument.signedValue = static_cast<std::int64_t>(value);
    } else {
        argument.kind = Argument::Kind::Unsigned;
        argument.unsignedValue = static_cast<std::uint64_t>(value);
    }
    return argument;
}

} // namespace detail

template <class Value>
Argument MakeArgument(const Value& value) {
    using Type = std::remove_cvref_t<Value>;
    if constexpr (std::is_enum_v<Type>) {
        return detail::IntegerArgument(static_cast<std::underlying_type_t<Type>>(value));
    } else if constexpr (std::is_integral_v<Type>) {
        return detail::IntegerArgument(value);
    } else if constexpr (std::is_floating_point_v<Type>) {
        Argument argument;
        argument.kind = Argument::Kind::Floating;
        argument.floatingValue = static_cast<double>(value);
        return argument;
    } else if constexpr (std::is_same_v<Type, std::nullptr_t>) {
        Argument argument;
        argument.kind = Argument::Kind::Pointer;
        return argument;
    } else if constexpr (std::is_same_v<Type, std::u16string> || std::is_same_v<Type, std::u16string_view> ||
        std::is_same_v<Type, std::wstring> || std::is_same_v<Type, std::wstring_view> || std::is_same_v<Type, std::string> ||
        std::is_same_v<Type, std::string_view>) {
        return detail::TextArgument(value.data(), value.size());
    } else if constexpr (std::is_array_v<Type>) {
        using Character = std::remove_cv_t<std::remove_extent_t<Type>>;
        static_assert(detail::kTextCharacter<Character>, "only character arrays can be format arguments");
        // A fixed buffer ends at its first NUL, but never past its size.
        std::size_t length = 0;
        while (length < std::extent_v<Type> && value[length] != Character{}) {
            ++length;
        }
        return detail::TextArgument(&value[0], length);
    } else if constexpr (std::is_pointer_v<Type>) {
        using Character = std::remove_cv_t<std::remove_pointer_t<Type>>;
        if constexpr (detail::kTextCharacter<Character>) {
            return detail::TextArgument(static_cast<const Character*>(value), Argument::kNulTerminated);
        } else {
            Argument argument;
            argument.kind = Argument::Kind::Pointer;
            argument.pointer = static_cast<const void*>(value);
            return argument;
        }
    } else {
        static_assert(detail::kUnsupportedArgument<Type>, "unsupported format argument type");
        return {};
    }
}

std::u16string FormatArguments(std::u16string_view format, const Argument* arguments, std::size_t count);

// Copy formatted text into a fixed buffer, as swprintf did. It writes at most
// capacity - 1 units and a NUL, and does not split a surrogate pair. It returns
// the text length, or -1 when the text did not fit.
int CopyFormatted(std::u16string_view text, char16_t* destination, std::size_t capacity) noexcept;

} // namespace text_format

template <class... Args>
std::u16string FormatUtf16(std::u16string_view format, const Args&... arguments) {
    if constexpr (sizeof...(Args) == 0) {
        return text_format::FormatArguments(format, nullptr, 0);
    } else {
        const text_format::Argument list[] = {text_format::MakeArgument(arguments)...};
        return text_format::FormatArguments(format, list, sizeof...(Args));
    }
}

template <class... Args>
int FormatUtf16To(char16_t* destination, std::size_t capacity, std::u16string_view format, const Args&... arguments) {
    return text_format::CopyFormatted(FormatUtf16(format, arguments...), destination, capacity);
}

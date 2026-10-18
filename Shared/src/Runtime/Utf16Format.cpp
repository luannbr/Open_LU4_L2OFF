
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace text_format {
namespace {

enum class Length : std::uint8_t { None, Char, Short, Long, LongLong, Size, LongDouble, Wide };

struct Spec {
    std::string flags;
    int width = -1;
    int precision = -1;
    Length length = Length::None;
    char16_t conversion = u'\0';
};

class ArgumentReader {
public:
    ArgumentReader(const Argument* arguments, std::size_t count) noexcept : arguments_(arguments), count_(count) {}

    // A missing argument reads as zero or as an empty string.
    const Argument* Next() noexcept { return index_ < count_ ? &arguments_[index_++] : nullptr; }

private:
    const Argument* arguments_;
    std::size_t count_;
    std::size_t index_ = 0;
};

std::int64_t SignedValue(const Argument* argument) noexcept {
    if (argument == nullptr) {
        return 0;
    }
    switch (argument->kind) {
        case Argument::Kind::Signed:
            return argument->signedValue;
        case Argument::Kind::Unsigned:
            return static_cast<std::int64_t>(argument->unsignedValue);
        case Argument::Kind::Floating:
            return static_cast<std::int64_t>(argument->floatingValue);
        default:
            return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(argument->pointer));
    }
}

std::uint64_t UnsignedValue(const Argument* argument) noexcept { return static_cast<std::uint64_t>(SignedValue(argument)); }

double FloatingValue(const Argument* argument) noexcept {
    if (argument == nullptr) {
        return 0.0;
    }
    switch (argument->kind) {
        case Argument::Kind::Floating:
            return argument->floatingValue;
        case Argument::Kind::Unsigned:
            return static_cast<double>(argument->unsignedValue);
        default:
            return static_cast<double>(SignedValue(argument));
    }
}

// Win64 printf reads int and long as 32 bits.
std::int64_t CutSigned(std::int64_t value, Length length) noexcept {
    switch (length) {
        case Length::Char:
            return static_cast<std::int8_t>(value);
        case Length::Short:
            return static_cast<std::int16_t>(value);
        case Length::LongLong:
        case Length::Size:
            return value;
        default:
            return static_cast<std::int32_t>(value);
    }
}

std::uint64_t CutUnsigned(std::uint64_t value, Length length) noexcept {
    switch (length) {
        case Length::Char:
            return static_cast<std::uint8_t>(value);
        case Length::Short:
            return static_cast<std::uint16_t>(value);
        case Length::LongLong:
        case Length::Size:
            return value;
        default:
            return static_cast<std::uint32_t>(value);
    }
}

std::u16string TextValue(const Argument* argument) {
    if (argument == nullptr) {
        return {};
    }
    if (argument->kind != Argument::Kind::Utf16 && argument->kind != Argument::Kind::Wide &&
        argument->kind != Argument::Kind::Utf8) {
        return {};
    }
    if (argument->pointer == nullptr) {
        return u"(null)";
    }
    const bool terminated = argument->length == Argument::kNulTerminated;
    switch (argument->kind) {
        case Argument::Kind::Utf16: {
            const auto* text = static_cast<const char16_t*>(argument->pointer);
            return terminated ? std::u16string(text) : std::u16string(text, argument->length);
        }
        case Argument::Kind::Wide: {
            const auto* text = static_cast<const wchar_t*>(argument->pointer);
            return WideToUtf16(terminated ? std::wstring_view(text) : std::wstring_view(text, argument->length));
        }
        default: {
            const auto* text = static_cast<const char*>(argument->pointer);
            return Utf8ToUtf16(terminated ? std::string_view(text) : std::string_view(text, argument->length));
        }
    }
}

void AppendAscii(std::u16string& output, const char* text) {
    for (; *text != '\0'; ++text) {
        output.push_back(static_cast<unsigned char>(*text));
    }
}

void AppendNumber(std::u16string& output, const Spec& spec, const char* conversion, ...) {
    std::string format = "%" + spec.flags;
    if (spec.width >= 0) {
        format += std::to_string(spec.width);
    }
    if (spec.precision >= 0) {
        format += "." + std::to_string(spec.precision);
    }
    format += conversion;

    std::array<char, 512> buffer{};
    std::va_list arguments;
    va_start(arguments, conversion);
    std::va_list copy;
    va_copy(copy, arguments);
    const int length = std::vsnprintf(buffer.data(), buffer.size(), format.c_str(), arguments);
    va_end(arguments);
    if (length < 0) {
        va_end(copy);
        return;
    }
    if (static_cast<std::size_t>(length) < buffer.size()) {
        va_end(copy);
        AppendAscii(output, buffer.data());
        return;
    }
    std::string large(static_cast<std::size_t>(length) + 1, '\0');
    std::vsnprintf(large.data(), large.size(), format.c_str(), copy);
    va_end(copy);
    AppendAscii(output, large.c_str());
}

void AppendPadded(std::u16string& output, const Spec& spec, std::u16string_view text) {
    const std::size_t width = spec.width > 0 ? static_cast<std::size_t>(spec.width) : 0;
    const std::size_t padding = width > text.size() ? width - text.size() : 0;
    const bool left = spec.flags.find('-') != std::string::npos;
    const char16_t fill = !left && spec.flags.find('0') != std::string::npos ? u'0' : u' ';
    if (!left) {
        output.append(padding, fill);
    }
    output.append(text);
    if (left) {
        output.append(padding, u' ');
    }
}

// Read flags, width, precision and the length modifier after '%'.
// The offset ends on the conversion character.
void ReadSpec(std::u16string_view format, std::size_t& offset, ArgumentReader& reader, Spec& spec) {
    while (offset < format.size() && std::u16string_view(u"-+ #0").find(format[offset]) != std::u16string_view::npos) {
        spec.flags.push_back(static_cast<char>(format[offset++]));
    }
    if (offset < format.size() && format[offset] == u'*') {
        ++offset;
        const std::int64_t width = SignedValue(reader.Next());
        if (width < 0) {
            spec.flags.push_back('-');
        }
        spec.width = static_cast<int>(width < 0 ? -width : width);
    } else {
        for (; offset < format.size() && format[offset] >= u'0' && format[offset] <= u'9'; ++offset) {
            spec.width = (spec.width < 0 ? 0 : spec.width * 10) + (format[offset] - u'0');
        }
    }
    if (offset < format.size() && format[offset] == u'.') {
        ++offset;
        spec.precision = 0;
        if (offset < format.size() && format[offset] == u'*') {
            ++offset;
            const std::int64_t precision = SignedValue(reader.Next());
            spec.precision = precision < 0 ? -1 : static_cast<int>(precision);
        } else {
            for (; offset < format.size() && format[offset] >= u'0' && format[offset] <= u'9'; ++offset) {
                spec.precision = spec.precision * 10 + (format[offset] - u'0');
            }
        }
    }
    while (offset < format.size()) {
        const char16_t current = format[offset];
        if (current == u'h') {
            spec.length = spec.length == Length::Short ? Length::Char : Length::Short;
        } else if (current == u'l') {
            spec.length = spec.length == Length::Long ? Length::LongLong : Length::Long;
        } else if (current == u'L') {
            spec.length = Length::LongDouble;
        } else if (current == u'w') {
            spec.length = Length::Wide;
        } else if (current == u'z' || current == u'j' || current == u't') {
            spec.length = Length::Size;
        } else if (current == u'I') {
            if (format.substr(offset + 1, 2) == u"64") {
                spec.length = Length::LongLong;
                offset += 2;
            } else if (format.substr(offset + 1, 2) == u"32") {
                spec.length = Length::None;
                offset += 2;
            } else {
                // A bare I is pointer-sized.
                spec.length = Length::Size;
            }
        } else {
            break;
        }
        ++offset;
    }
}

} // namespace

std::u16string FormatArguments(std::u16string_view format, const Argument* arguments, std::size_t count) {
    std::u16string output;
    output.reserve(format.size() + 16);
    ArgumentReader reader(arguments, count);

    for (std::size_t offset = 0; offset < format.size();) {
        if (format[offset] != u'%') {
            output.push_back(format[offset++]);
            continue;
        }
        const std::size_t start = offset++;
        if (offset < format.size() && format[offset] == u'%') {
            output.push_back(u'%');
            ++offset;
            continue;
        }

        Spec spec;
        ReadSpec(format, offset, reader, spec);
        if (offset >= format.size()) {
            output.append(format.substr(start));
            break;
        }
        spec.conversion = format[offset++];

        switch (spec.conversion) {
            case u'd':
            case u'i':
                AppendNumber(
                    output, spec, "lld", static_cast<long long>(CutSigned(SignedValue(reader.Next()), spec.length))
                );
                break;
            case u'u':
            case u'o':
            case u'x':
            case u'X': {
                const char conversion[] = {'l', 'l', static_cast<char>(spec.conversion), '\0'};
                AppendNumber(
                    output, spec, conversion,
                    static_cast<unsigned long long>(CutUnsigned(UnsignedValue(reader.Next()), spec.length))
                );
                break;
            }
            case u'f':
            case u'F':
            case u'e':
            case u'E':
            case u'g':
            case u'G':
            case u'a':
            case u'A': {
                const char conversion[] = {static_cast<char>(spec.conversion), '\0'};
                AppendNumber(output, spec, conversion, FloatingValue(reader.Next()));
                break;
            }
            case u'c':
            case u'C': {
                // %hc and %C name a narrow character. It is one byte here.
                const std::uint64_t value = UnsignedValue(reader.Next());
                const bool narrow = spec.length == Length::Short || (spec.conversion == u'C' && spec.length == Length::None);
                const char16_t unit =
                    narrow ? static_cast<char16_t>(static_cast<unsigned char>(value)) : static_cast<char16_t>(value);
                AppendPadded(output, spec, std::u16string_view(&unit, 1));
                break;
            }
            case u's':
            case u'S':
            case u'Z': {
                std::u16string text = TextValue(reader.Next());
                if (spec.precision >= 0 && static_cast<std::size_t>(spec.precision) < text.size()) {
                    text.resize(static_cast<std::size_t>(spec.precision));
                }
                AppendPadded(output, spec, text);
                break;
            }
            case u'p': {
                // MSVC prints a Win64 pointer as 16 upper-case hex digits.
                Spec pointerSpec;
                pointerSpec.width = spec.width;
                pointerSpec.flags = spec.flags;
                const Argument* argument = reader.Next();
                const auto value = argument != nullptr && argument->kind == Argument::Kind::Pointer
                    ? static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(argument->pointer))
                    : static_cast<unsigned long long>(UnsignedValue(argument));
                std::array<char, 32> digits{};
                std::snprintf(digits.data(), digits.size(), "%016llX", value);
                std::u16string text;
                AppendAscii(text, digits.data());
                AppendPadded(output, pointerSpec, text);
                break;
            }
            case u'n':
                // The original never relies on %n. Consume its argument only.
                static_cast<void>(reader.Next());
                break;
            default:
                // An unknown conversion stays in the text as written.
                output.append(format.substr(start, offset - start));
                break;
        }
    }
    return output;
}

int CopyFormatted(std::u16string_view text, char16_t* destination, std::size_t capacity) noexcept {
    if (destination == nullptr || capacity == 0) {
        return -1;
    }
    std::size_t length = text.size() < capacity ? text.size() : capacity - 1;
    if (length < text.size() && length > 0 && text[length - 1] >= 0xd800 && text[length - 1] <= 0xdbff) {
        --length;
    }
    for (std::size_t index = 0; index < length; ++index) {
        destination[index] = text[index];
    }
    destination[length] = u'\0';
    return length == text.size() ? static_cast<int>(length) : -1;
}

} // namespace text_format

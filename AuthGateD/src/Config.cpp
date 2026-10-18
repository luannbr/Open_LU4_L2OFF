#include "Shared/src/Runtime/Utf8.h"
#include "Config.h"

#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Shared/src/TextRuntime.h"
#include "GlobalFunc.h"

#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

namespace {

bool IsAsciiAlpha(char16_t character) noexcept {
    return (character >= u'A' && character <= u'Z') ||
        (character >= u'a' && character <= u'z');
}

bool IsAsciiDigit(char16_t character) noexcept {
    return character >= u'0' && character <= u'9';
}

bool IsAsciiAlphaNumeric(char16_t character) noexcept {
    return IsAsciiAlpha(character) || IsAsciiDigit(character);
}

char16_t FoldAscii(char16_t character) noexcept {
    return character >= u'A' && character <= u'Z'
        ? static_cast<char16_t>(character + (u'a' - u'A'))
        : character;
}

bool EqualsIgnoreAsciiCase(
    std::u16string_view left,
    std::u16string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (FoldAscii(left[index]) != FoldAscii(right[index])) return false;
    }
    return true;
}

} // namespace

bool CConfig::LoadFile(
    const std::filesystem::path& path,
    std::u16string& text) const {
    text.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    const std::string bytes{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
    if (bytes.empty()) return true;
    if (bytes.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    const int required = portable::WindowsAnsiToWideChar(
        bytes.data(),
        static_cast<int>(bytes.size()),
        static_cast<char16_t*>(nullptr),
        0);
    if (required <= 0) return false;
    text.resize(static_cast<std::size_t>(required));
    return portable::WindowsAnsiToWideChar(
               bytes.data(),
               static_cast<int>(bytes.size()),
               text.data(),
               required) == required;
}

CConfig::Token CConfig::GetToken(
    const char16_t*& cursor,
    std::u16string& value) {
    value.clear();
    while (*cursor == u' ' || *cursor == u'\t' || *cursor == u'\r') {
        ++cursor;
    }

    const char16_t current = *cursor;
    if (current == u'\0') return Token::End;
    switch (current) {
    case u'\n':
    case u'&':
    case u'(':
    case u')':
    case u',':
    case u'.':
    case u'/':
    case u':':
    case u';':
    case u'=':
        ++cursor;
        return static_cast<Token>(current);
    default:
        break;
    }

    if (current == u'-') {
        ++cursor;
        if (!IsAsciiDigit(*cursor)) return static_cast<Token>(u'-');
        value.push_back(u'-');
    }

    if (IsAsciiDigit(*cursor)) {
        do {
            value.push_back(*cursor++);
        } while (IsAsciiDigit(*cursor));
        return Token::Value;
    }

    if (*cursor == u'"') {
        ++cursor;
        while (*cursor != u'\0' && *cursor != u'"' && *cursor != u'\n') {
            value.push_back(*cursor++);
        }
        if (*cursor == u'"') ++cursor;
        return Token::Value;
    }

    if (IsAsciiAlpha(*cursor)) {
        for (;;) {
            while (IsAsciiAlphaNumeric(*cursor)) {
                value.push_back(*cursor++);
            }
            if (*cursor != u'_') break;
            value.push_back(*cursor++);
        }
        return Token::Value;
    }

    ++cursor;
    return Token::Comment;
}

bool CConfig::Load(const char16_t* path) {
    if (!path) return false;
    std::u16string text;
    if (!LoadFile(std::filesystem::path(runtime::Utf8CodeUnits(Utf16ToUtf8(path))), text)) {
        return false;
    }
    text.push_back(u'\0');

    const char16_t* cursor = text.c_str();
    std::u16string key;
    std::u16string value;
    int state = 0;
    for (;;) {
        const Token token = GetToken(cursor, value);
        if (token == Token::End) break;
        if (state == 3) {
            if (token == Token::NewLine) state = 0;
            continue;
        }
        switch (token) {
        case Token::Value:
            if (state == 0) {
                key = value;
            } else if (state == 2) {
                // stdext::hash_map::insert in the selected image keeps the
                // first occurrence of a key.
                m_mapConfig.emplace(key, value);
            }
            break;
        case Token::NewLine:
            state = 0;
            break;
        case Token::Comment:
            state = 3;
            break;
        case Token::Equal:
            state = 2;
            break;
        default:
            break;
        }
    }

    InitConfig();
    return true;
}

char16_t* CConfig::Get(const char16_t* name) {
    if (!name) return nullptr;
    const auto found = m_mapConfig.find(name);
    return found == m_mapConfig.end() ? nullptr : found->second.data();
}

const char16_t* CConfig::Get(const char16_t* name) const {
    if (!name) return nullptr;
    const auto found = m_mapConfig.find(name);
    return found == m_mapConfig.end() ? nullptr : found->second.c_str();
}

bool CConfig::GetBool(
    const char16_t* name,
    bool defaultValue) const {
    const char16_t* value = Get(name);
    if (!value) return defaultValue;
    const std::u16string_view text(value);
    return EqualsIgnoreAsciiCase(text, u"yes") ||
        EqualsIgnoreAsciiCase(text, u"true") ||
        text == u"1";
}

std::int32_t CConfig::GetInt(
    const char16_t* name,
    std::int32_t defaultValue) const {
    const char16_t* value = Get(name);
    if (!value) return defaultValue;
    const std::string narrow = Utf16Ascii(value);
    std::int32_t result = 0;
    const auto conversion = std::from_chars(
        narrow.data(),
        narrow.data() + narrow.size(),
        result);
    return conversion.ptr == narrow.data() ? 0 : result;
}

in_addr CConfig::GetInetAddr(const char16_t* name) const {
    return StrToInet(Get(name));
}

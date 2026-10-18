#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TextRuntime.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Original inline owner from HtmlActionParamParser.h. The current Win64
// image creates a 0x28 xallocator string followed by the 0x20 MSVC map
// owner. Portable code uses a standard UTF-16 string and map to keep
// the caller-facing text API. It does not emulate those container extents.
class CHTMLActionParamParser {
private:
    using WideString = std::u16string;

public:
    using StrStrMapType = std::map<WideString, WideString>;
    using StrStrMapItrType = StrStrMapType::iterator;

    static CHTMLActionParamParser Parse(const char16_t* source) {
        CHTMLActionParamParser result(source);
        if (!source) {
            return result;
        }

        const std::u16string_view text(source);
        const std::size_t question = text.find(u'?');
        if (question == std::u16string_view::npos) {
            return result;
        }

        std::size_t cursor = question + 1;
        while (cursor < text.size()) {
            const std::size_t equal = text.find(u'=', cursor);
            if (equal == std::u16string_view::npos) {
                break;
            }
            const std::size_t ampersand = text.find(u'&', equal + 1);
            const std::size_t end = ampersand == std::u16string_view::npos ? text.size() : ampersand;

            const std::u16string_view name = TrimSpaces(text.substr(cursor, equal - cursor));
            const std::u16string_view value = TrimSpaces(text.substr(equal + 1, end - equal - 1));
            if (!name.empty() && !value.empty()) {
                result.m_mapParam[WideString(name)] = WideString(value);
            }

            if (ampersand == std::u16string_view::npos) {
                break;
            }
            cursor = ampersand + 1;
        }
        return result;
    }

    int GetIntValue(const char16_t* name) const {
        const char16_t* const value = GetStrValue(name);
        return value ? WindowsWtol(std::u16string_view(value)) : -1;
    }

    std::int64_t GetInt64Value(const char16_t* name) const {
        const char16_t* const value = GetStrValue(name);
        return value ? WindowsWcstoi64(std::u16string_view(value)) : static_cast<std::int64_t>(-1);
    }

    const char16_t* GetStrValue(const char16_t* name) const {
        if (!name) {
            return nullptr;
        }
        const auto found = m_mapParam.find(WideString(name));
        return found == m_mapParam.end() ? nullptr : found->second.c_str();
    }

    const StrStrMapType& GetStrStrMap() const noexcept { return m_mapParam; }

private:
    explicit CHTMLActionParamParser(const char16_t* source) : m_strSource(source ? source : u"") {}

    static std::u16string_view TrimSpaces(std::u16string_view value) {
        std::size_t first = 0;
        while (first < value.size() && value[first] == u' ') {
            ++first;
        }
        std::size_t last = value.size();
        while (last > first && value[last - 1] == u' ') {
            --last;
        }
        return value.substr(first, last - first);
    }

    WideString m_strSource; // +0x00..+0x27
    StrStrMapType m_mapParam; // +0x28..+0x47
};

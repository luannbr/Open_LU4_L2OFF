#pragma once

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SSQ.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Utils.h"

#include <cstddef>
#include <cstdint>

class CUserSocket;


// Current L2Server parsehtml.cpp owner. WCHAR storage in the target is
// represented by std::u16string in the portable build.
class CParseHTML {
public:
    using StrStrMapType = std::map<std::u16string, std::u16string, runtime::CStringLess<std::u16string>>;
    using TemplateMapType = ::std::map<int, std::u16string>;

    CParseHTML();
    explicit CParseHTML(CUserSocket* socket);
    virtual ~CParseHTML();

    bool GetBodyFromFile(const char16_t* fileName, int language);
    void GetTempletFromFile();
    std::u16string GetTempletStr(int templateId);
    std::u16string MakeHTMLFromStrTemplet(int templateId, StrStrMapType& parameters);
    void AddStringMap(std::u16string_view name, std::u16string_view value);
    void AddStringLocalMap(StrStrMapType& parameters, std::u16string_view name, std::u16string_view value);
    // The format follows the original printf rules, see Runtime/Utf16Format.h.
    template <class... Args>
    void AddFormattedStringMap(std::u16string_view name, std::u16string_view format, const Args&... arguments) {
        // Current L2Server 0x00925700.
        L2_GUARD_FUNCTION();
        AddFormattedValue(m_mapParam, name, FormatUtf16(format, arguments...), 0x100);
    }
    template <class... Args>
    void AddFormattedStringLocalMap(
        StrStrMapType& parameters, std::u16string_view name, std::u16string_view format, const Args&... arguments
    ) {
        // Current L2Server 0x009259ac.
        L2_GUARD_FUNCTION();
        AddFormattedValue(parameters, name, FormatUtf16(format, arguments...), 0x123);
    }
    bool SendHTML(std::u16string html);
    bool SendHTML(
        std::u16string html,
        int favoriteMode,
        int argument1,
        int argument2,
        int argument3,
        int argument4,
        int argument5,
        const wchar_t* stringArgument,
        int sendMode
    );
    bool SendErrorMsgHtml(const wchar_t* errorMessage, int htmlType);
    std::u16string GetWorldName(int worldId);
    static std::u16string MakeFormatedDate(time_l2_t dateTime, int option);
    static std::u16string MakeFormatedDateLocal(time_l2_t dateTime, int option);
    static std::u16string ConvertStr(SharedUtilsWideChar* source, int option);


    // Original callers pass this map back to MakeHTMLFromStrTemplet.
    StrStrMapType m_mapParam; // +0x08

private:
    // The original formatted into a 0x5000-unit buffer. Longer text is
    // rejected and the map is unchanged.
    static void AddFormattedValue(
        StrStrMapType& destination, std::u16string_view name, std::u16string value, int failureLine
    );

    TemplateMapType m_mapTemplet; // +0x28

public:
    StrStrMapType m_mapParam2; // +0x48

private:
    std::u16string m_html; // +0x68
    std::int32_t m_nTempletCount; // +0x90
    [[maybe_unused]] std::uint32_t m_alignment94; // +0x94

protected:
    CUserSocket* m_mySocket; // +0x98
};

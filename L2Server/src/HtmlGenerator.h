#pragma once

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>

class CUserSocket;
class CHtmlBuilder;

class CHtmlGeneratorHelper {
public:
    using StringMapType = std::map<std::u16string, std::u16string>;

    CHtmlGeneratorHelper() = default;
    ~CHtmlGeneratorHelper() = default;

    void AddValue(std::u16string& name, std::u16string& value);
    void AppendValue(std::u16string& name, std::u16string& value);
    std::u16string Generate(std::u16string& templet);

private:
    StringMapType m_valueMap; // +0x00..+0x1f
};

class CLocalHtmlGenerator;

class CHtmlGenerator {
public:
    CHtmlGenerator();
    explicit CHtmlGenerator(const char16_t* htmlTemplateFileName);
    virtual ~CHtmlGenerator();

    void SetHtmlTemplateFile(const char16_t* htmlTemplateFileName) { m_htmlTemplateFileName = htmlTemplateFileName; }

    std::u16string Generate();
    void AddValue(std::u16string_view valueName, CLocalHtmlGenerator& localHtmlBuilder);
    void AddValue(std::u16string_view valueName, std::u16string_view value);
    std::u16string GetTemplet(int templetId);

private:
    friend class CHtmlBuilder;

    void ReadTemplet();
    void ParseTemplet();

    const char16_t* m_htmlTemplateFileName; // +0x08
    CHtmlGeneratorHelper m_htmlBuilderHelper; // +0x10..+0x2f
    std::map<int, std::u16string> m_mapTemplet; // +0x30..+0x4f
    std::u16string m_htmlTemplet; // +0x50..+0x77
};

class CLocalHtmlGenerator {
public:
    explicit CLocalHtmlGenerator(std::u16string templet);
    ~CLocalHtmlGenerator() = default;

    std::u16string Generate();
    void AddValue(std::u16string_view valueName, std::u16string_view value);

private:
    std::u16string m_templet; // +0x00..+0x27
    CHtmlGeneratorHelper m_htmlBuilderHelper; // +0x28..+0x47
};

class CHtmlSender {
public:
    explicit CHtmlSender(CUserSocket* userSocket) : m_userSocket(userSocket) {}

    void SendHtml(std::u16string& html);
    void SendHtmlWith3Packet(std::u16string& html);

private:
    void SendHtmlInternal(const char16_t* html);

    CUserSocket* m_userSocket; // +0x00
};

class CNpcHtmlSender {
public:
    explicit CNpcHtmlSender(CUserSocket* userSocket) : m_userSocket(userSocket) {}

    void SendHtml(std::u16string& html);

private:
    void SendHtmlInternal(const char16_t* html);

    CUserSocket* m_userSocket; // +0x00
};

// The original formatted into a 0x4fff-unit buffer. Longer text is an error
// and the result is empty.
std::u16string CheckFormattedHtml(std::u16string text);

// Current L2Server 0x0079e230. The format follows the original printf rules,
// see Runtime/Utf16Format.h.
template <class... Args>
std::u16string FormatString(std::u16string_view format, const Args&... arguments) {
    L2_GUARD_FUNCTION();
    return CheckFormattedHtml(FormatUtf16(format, arguments...));
}
std::u16string AbbreviateString(std::u16string_view value, int length, std::u16string_view abbreviation);

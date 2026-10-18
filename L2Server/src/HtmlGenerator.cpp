#include "HtmlGenerator.h"

#include "HTMLCache.h"
#include "UserSocket.h"

#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::size_t kMaximumHtmlLength = 0x2000;
constexpr std::size_t kMaximumPacketLength = 0x1ce8;
constexpr std::size_t kMaximumThreePacketLength = 0x56b8;

std::u16string TrimSpaces(std::u16string_view value) {
    std::u16string_view view = value;
    while (!view.empty() && view.front() == u' ') {
        view.remove_prefix(1);
    }
    while (!view.empty() && view.back() == u' ') {
        view.remove_suffix(1);
    }
    return std::u16string(view);
}

} // namespace

std::u16string CheckFormattedHtml(std::u16string text) {
    constexpr std::size_t kBufferUnits = 0x4fff;
    if (text.size() >= kBufferUnits) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] FormatString failed. too long argument.",
        //     L"HtmlGenerator.cpp",
        //     0x18e)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] FormatString failed. too long argument.",
        //     L"HtmlGenerator.cpp",
        //     0x18e)
        // Previous: L2_LOG_ERROR(
        //     Log,
        //     "Cannot format HTML text within the 20479-wide-character buffer (formatter result %d); returning an empty "
        //     "string.",
        //     length)
        L2_LOG_ERROR(
            Log, "Cannot format HTML text: %zu UTF-16 units do not fit the %zu-unit buffer; returning an empty string.",
            text.size(), kBufferUnits
        );
        return {};
    }
    return text;
}

void CHtmlGeneratorHelper::AddValue(std::u16string& name, std::u16string& value) {
    // Current L2Server 0x0079eb58.
    L2_GUARD_FUNCTION();
    if (!name.empty() && !value.empty()) {
        m_valueMap[name] = value;
    }
}

void CHtmlGeneratorHelper::AppendValue(std::u16string& name, std::u16string& value) {
    // Current L2Server 0x0079ec30.
    if (name.empty() || value.empty()) {
        return;
    }
    auto position = m_valueMap.find(name);
    if (position == m_valueMap.end()) {
        m_valueMap[name] = value;
        return;
    }
    std::u16string appended(position->second);
    appended += value;
    m_valueMap[name] = std::move(appended);
}

std::u16string CHtmlGeneratorHelper::Generate(std::u16string& templet) {
    // Current L2Server 0x0079dd34.
    L2_GUARD_FUNCTION();
    if (templet.empty()) {
        return {};
    }
    const std::u16string_view source = runtime::StringView(templet);
    if (source.size() > kMaximumHtmlLength) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] HTML size is too big!!! [%d]",
        //     L"HtmlGenerator.cpp",
        //     0x37,
        //     static_cast<int>(source.size()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] HTML size is too big!!! [%d]",
        //     L"HtmlGenerator.cpp",
        //     0x37,
        //     static_cast<int>(source.size()))
        L2_LOG_ERROR(
            Log, "HTML template has %zu UTF-16 code units, exceeding %zu; returning the HTML body-error page.",
            source.size(), kMaximumHtmlLength
        );
        return std::u16string(u"<html><body>Can not get HTML body</body></html>");
    }

    std::u16string output;
    output.reserve(source.size());
    std::size_t cursor = 0;
    while (cursor < source.size()) {
        const std::size_t open = source.find(u"<?", cursor);
        if (open == std::u16string_view::npos) {
            output.append(source.substr(cursor));
            break;
        }
        output.append(source.substr(cursor, open - cursor));
        const std::size_t nameStart = open + 2;
        const std::size_t close = source.find(u"?>", nameStart);
        if (source.size() - nameStart < 3 || close == std::u16string_view::npos) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid html parameter",
            //     L"HtmlGenerator.cpp",
            //     0x65)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid html parameter",
            //     L"HtmlGenerator.cpp",
            //     0x65)
            L2_LOG_ERROR(
                Log,
                "Invalid HTML parameter at UTF-16 offset %zu: insufficient text after '<?' or missing '?>'; appending the "
                "invalid-page marker and stopping expansion.",
                open
            );
            output.append(u"***HTML page is invalid***");
            // The current mutable-buffer implementation has already placed
            // a NUL at the opening marker, then appends its current prefix
            // once more on this error path.
            output.append(source.substr(cursor, open - cursor));
            break;
        }

        std::u16string name = std::u16string(source.substr(nameStart, close - nameStart));
        const auto position = m_valueMap.find(name);
        if (position != m_valueMap.end()) {
            output.append(runtime::StringView(position->second));
        }
        cursor = close + 2;
    }
    return std::u16string(output);
}

CHtmlGenerator::CHtmlGenerator() {
    // Current L2Server 0x0078cc30 leaves the file-name lane
    // untouched. The container and string subobjects are constructed here.
}

CHtmlGenerator::CHtmlGenerator(const char16_t* htmlTemplateFileName) : m_htmlTemplateFileName(htmlTemplateFileName) {
    // Current L2Server 0x0079f604.
    ReadTemplet();
    ParseTemplet();
}

CHtmlGenerator::~CHtmlGenerator() {
    // Current L2Server 0x0069d4e4 clears this non-owning pointer first.
    m_htmlTemplateFileName = nullptr;
}

void CHtmlGenerator::ReadTemplet() {
    // Current L2Server 0x0079da08.
    L2_GUARD_FUNCTION();
    if (!m_htmlTemplateFileName) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] html file name is null",
        //     "HtmlGenerator.cpp",
        //     0x82)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] html file name is null",
        //     "HtmlGenerator.cpp",
        //     0x82)
        L2_LOG_ERROR(Log, "HTML template filename is null; the cache lookup still follows.");
    }
    std::u16string contents;
    g_HTMLCache.GetHTMLFile(m_htmlTemplateFileName, contents, 0);
    m_htmlTemplet.assign(runtime::StringView(contents));
}

void CHtmlGenerator::ParseTemplet() {
    // Current L2Server 0x0079eda0.
    if (m_htmlTemplet.empty()) {
        return;
    }
    std::u16string source(runtime::StringView(m_htmlTemplet));
    if (source.size() > kMaximumHtmlLength) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] HTML size is too big!!! [%d]",
        //     L"HtmlGenerator.cpp",
        //     0x98,
        //     static_cast<int>(source.size()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] HTML size is too big!!! [%d]",
        //     L"HtmlGenerator.cpp",
        //     0x98,
        //     static_cast<int>(source.size()))
        L2_LOG_ERROR(
            Log, "HTML template has %zu UTF-16 code units, exceeding %zu; template extraction is skipped.", source.size(),
            kMaximumHtmlLength
        );
        return;
    }

    constexpr std::u16string_view openMarker = u"<!--TEMPLET";
    constexpr std::u16string_view closeMarker = u"TEMPLET-->";
    std::u16string body;
    body.reserve(source.size());
    std::size_t cursor = 0;
    while (cursor < source.size()) {
        const std::size_t open = source.find(openMarker, cursor);
        if (open == std::u16string_view::npos) {
            body.append(source.substr(cursor));
            break;
        }
        body.append(source.substr(cursor, open - cursor));
        const std::size_t idStart = open + openMarker.size();
        if (idStart >= source.size()) {
            body.append(source.substr(cursor, open - cursor));
            break;
        }
        const int templetId = GetIntFromString(reinterpret_cast<SharedUtilsWideChar*>(source.data() + idStart));

        // The current source advances exactly 12 UTF-16 units from the
        // marker start, irrespective of how many digits GetIntFromString
        // consumed.
        const std::size_t contentStart = open + 12;
        const std::size_t close = source.find(closeMarker, contentStart);
        if (close == std::u16string_view::npos) {
            body.append(source.substr(cursor, open - cursor));
            break;
        }
        std::u16string content = std::u16string(source.substr(contentStart, close - contentStart));
        content = TrimSpaces(content);
        if (!content.empty()) {
            m_mapTemplet[templetId] = std::move(content);
        }
        cursor = close + closeMarker.size();
    }
    m_htmlTemplet.assign(body);
}

std::u16string CHtmlGenerator::GetTemplet(const int templetId) {
    // Current L2Server 0x0079dbc0.
    L2_GUARD_FUNCTION();
    if (templetId == 0) {
        return m_htmlTemplet;
    }
    const auto position = m_mapTemplet.find(templetId);
    return position == m_mapTemplet.end() ? std::u16string() : position->second;
}

std::u16string CHtmlGenerator::Generate() {
    // Current L2Server 0x0079e148.
    L2_GUARD_FUNCTION();
    std::u16string templet = GetTemplet(0);
    return m_htmlBuilderHelper.Generate(templet);
}

void CHtmlGenerator::AddValue(std::u16string_view valueName, std::u16string_view value) {
    // Current L2Server 0x0079f0fc.
    std::u16string name = std::u16string(valueName);
    std::u16string trimmedName = TrimSpaces(name);
    std::u16string trimmedValue = TrimSpaces(value);
    m_htmlBuilderHelper.AddValue(trimmedName, trimmedValue);
}


void CHtmlGenerator::AddValue(std::u16string_view valueName, CLocalHtmlGenerator& localHtmlBuilder) {
    // Current L2Server 0x0079f2cc.
    std::u16string value = TrimSpaces(localHtmlBuilder.Generate());
    std::u16string name = TrimSpaces(std::u16string(valueName));
    m_htmlBuilderHelper.AppendValue(name, value);
}

CLocalHtmlGenerator::CLocalHtmlGenerator(std::u16string templet) : m_templet(std::move(templet)) {
    // Current L2Server 0x007898e0.
}

std::u16string CLocalHtmlGenerator::Generate() { return m_htmlBuilderHelper.Generate(m_templet); }

void CLocalHtmlGenerator::AddValue(std::u16string_view valueName, std::u16string_view value) {
    // Current L2Server 0x0079f434.
    L2_GUARD_FUNCTION();
    std::u16string name = TrimSpaces(std::u16string(valueName));
    std::u16string trimmedValue = TrimSpaces(value);
    m_htmlBuilderHelper.AddValue(name, trimmedValue);
}


std::u16string AbbreviateString(std::u16string_view value, const int length, std::u16string_view abbreviation) {
    // Current L2Server 0x0079e54c.
    L2_GUARD_FUNCTION();
    std::u16string source = std::u16string(value);
    if (static_cast<std::int64_t>(length) < static_cast<std::int64_t>(source.size())) {
        const std::u16string suffix = std::u16string(abbreviation);
        const std::int64_t prefixLength = static_cast<std::int64_t>(length) - static_cast<std::int64_t>(suffix.size());
        const std::size_t count =
            prefixLength < 0 ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(prefixLength);
        std::u16string result(runtime::StringView(source).substr(0, count));
        result.append(suffix);
        return std::u16string(result);
    }
    return source;
}

void CNpcHtmlSender::SendHtmlInternal(const char16_t* html) {
    // Current L2Server 0x0079d9cc.
    if (m_userSocket) {
        m_userSocket->Send("cdSdd", 0x19, 0, html, 0, 0);
    }
}

void CNpcHtmlSender::SendHtml(std::u16string& html) {
    // Current L2Server 0x0079dae8.
    L2_GUARD_FUNCTION();
    if (html.size() <= kMaximumPacketLength) {
        SendHtmlInternal(html.c_str());
        return;
    }
    // Log.Add(LOG_ERROR, L"HTML Too large packet")
    // L2_LOG_ERROR(Log, L"HTML Too large packet")
    L2_LOG_ERROR(
        Log, "Cannot send NPC HTML: %zu UTF-16 code units exceed the %zu limit; the page is not sent.", html.size(),
        kMaximumPacketLength
    );
}

void CHtmlSender::SendHtmlInternal(const char16_t* html) {
    // Current L2Server 0x0079e44c.
    if (!m_userSocket) {
        return;
    }
    const std::u16string favorite = FormatString(u"bypass %s", u"_bbsgetfav");
    m_userSocket->Send(
        "ccSSSSSSSSS", 0x7b, 1, u"bypass _bbshome", favorite.c_str(), u"bypass _bbsloc", u"bypass _bbsclan",
        u"bypass _bbsmemo", u"bypass _maillist_0_1_0_", u"bypass _friendlist_0_", u"", html
    );
}

void CHtmlSender::SendHtml(std::u16string& html) {
    // Current L2Server 0x0079e714.
    L2_GUARD_FUNCTION();
    if (html.size() <= kMaximumPacketLength) {
        SendHtmlInternal(html.c_str());
        return;
    }
    // Log.Add(LOG_ERROR, L"HTML Too large packet")
    // L2_LOG_ERROR(Log, L"HTML Too large packet")
    L2_LOG_ERROR(
        Log,
        "Cannot send community-board HTML in one packet: %zu UTF-16 code units exceed the %zu limit; the page is not sent.",
        html.size(), kMaximumPacketLength
    );
}

void CHtmlSender::SendHtmlWith3Packet(std::u16string& html) {
    // Current L2Server 0x0079e7ec passes character 8 to each L"10N%c"
    // wsprintfW call. The otherwise invisible U+0008 suffix is on wire.
    if (html.size() > kMaximumThreePacketLength) {
        // Log.Add(LOG_ERROR, L"BBS Too larget packet")
        // L2_LOG_ERROR(Log, L"BBS Too larget packet")
        L2_LOG_ERROR(
            Log,
            "Cannot send community-board HTML in three packets: %zu UTF-16 code units exceed the %zu limit; the page is not "
            "sent.",
            html.size(), kMaximumThreePacketLength
        );
        return;
    }
    constexpr std::array<std::u16string_view, 3> prefixes{
        u"101\b",
        u"102\b",
        u"103\b",
    };
    for (std::size_t index = 0; index < prefixes.size(); ++index) {
        const std::size_t offset = index * kMaximumPacketLength;
        const std::size_t count = offset < html.size() ? std::min(kMaximumPacketLength, html.size() - offset) : 0;
        std::u16string packet(prefixes[index]);
        if (count != 0) {
            packet.append(runtime::StringView(html).substr(offset, count));
        }
        SendHtmlInternal(packet.c_str());
    }
}

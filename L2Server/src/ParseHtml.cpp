#include "ParseHtml.h"

#include "CConfig.h"
#include "HTMLCache.h"
#include "User.h"
#include "UserSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdarg>
#include <ctime>
#include <cwchar>
#include <string>
#include <string_view>
#include <type_traits>

namespace {


template <typename WideChar>
std::u16string ToTargetWide(const WideChar* value) {
    if (value == nullptr) {
        return {};
    }
    if constexpr (std::is_same_v<WideChar, char16_t>) {
        return std::u16string(value);
    } else {
        return WideToUtf16(value);
    }
}

template <std::size_t Capacity, typename... Arguments>
std::u16string FormatLimited(const std::u16string_view format, const Arguments&... arguments) {
    std::array<char16_t, Capacity> buffer{};
    FormatUtf16To(buffer.data(), buffer.size(), format, arguments...);
    return std::u16string(buffer.data());
}

bool ConvertDateTime(const time_l2_t dateTime, const bool local, std::tm& result) noexcept {
    if (dateTime < 0) {
        return false;
    }
#if defined(_WIN32)
    const __time32_t input = static_cast<__time32_t>(dateTime);
    return local ? ::_localtime32_s(&result, &input) == 0 : ::_gmtime32_s(&result, &input) == 0;
#else
    const std::time_t input = static_cast<std::time_t>(dateTime);
    return local ? ::localtime_r(&input, &result) != nullptr : ::gmtime_r(&input, &result) != nullptr;
#endif
}

std::u16string FormatDateTime(const time_l2_t dateTime, const int option, const bool local) {
    std::tm value{};
    if (!ConvertDateTime(dateTime, local, value)) {
        // Log.Add(
        //     LOG_ERROR,
        //     local
        //         ? L"MakeFormatedDateLocal err"
        //         : L"MakeFormatedDate err")
        // L2_LOG_ERROR(Log,
        //     local
        //         ? L"MakeFormatedDateLocal err"
        //         : L"MakeFormatedDate err")
        L2_LOG_ERROR(
            Log, "Cannot convert timestamp %lld to %s for HTML date option %d; returning an empty string.",
            static_cast<long long>(dateTime), local ? "local time" : "UTC", option
        );
        return std::u16string(u"");
    }

    std::array<char16_t, 0x80> buffer{};
    switch (option) {
        case 2:
            FormatUtf16To(
                buffer.data(), buffer.size(), u"%04d-%02d-%02d %02d:%02d:%02d", value.tm_year + 1900, value.tm_mon + 1,
                value.tm_mday, value.tm_hour, value.tm_min, value.tm_sec
            );
            break;
        case 3:
            FormatUtf16To(
                buffer.data(), buffer.size(), u"%04d-%02d-%02d", value.tm_year + 1900, value.tm_mon + 1, value.tm_mday
            );
            break;
        case 4:
            FormatUtf16To(buffer.data(), buffer.size(), u"%02d:%02d:%02d", value.tm_hour, value.tm_min, value.tm_sec);
            break;
        case 5:
            FormatUtf16To(
                buffer.data(), buffer.size(), u"%02d/%02d/%02d", (value.tm_year + 1900) % 100, value.tm_mon + 1,
                value.tm_mday
            );
            break;
        case 6:
            if (local) {
                FormatUtf16To(
                    buffer.data(), buffer.size(), u"%04d-%02d-%02d  %02d:%02d", value.tm_year + 1900, value.tm_mon + 1,
                    value.tm_mday, value.tm_hour, value.tm_min
                );
                break;
            }
            [[fallthrough]];
        default:
            FormatUtf16To(
                buffer.data(), buffer.size(), u"%02d-%02d-%02d", value.tm_year - 100, value.tm_mon + 1, value.tm_mday
            );
            break;
    }
    return std::u16string(buffer.data());
}

std::u16string Trim(std::u16string value) {
    const auto isSpace = [](const char16_t codeUnit) {
        return codeUnit == u' ' || codeUnit == u'\t' || codeUnit == u'\r' || codeUnit == u'\n';
    };
    std::size_t begin = 0;
    while (begin < value.size() && isSpace(value[begin])) {
        ++begin;
    }
    std::size_t end = value.size();
    while (begin < end && isSpace(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

} // namespace

void CParseHTML::AddFormattedValue(
    StrStrMapType& destination, std::u16string_view name, std::u16string value, const int failureLine
) {
    constexpr std::size_t kBufferUnits = 0x5000;
    if (value.size() >= kBufferUnits) {
        // Log.Add(
        //     LOG_ERROR,
        //     failureText,
        //     L"parsehtml.cpp",
        //     failureLine,
        //     name ? name : L"")
        // L2_LOG_ERROR(Log,
        //     failureText,
        //     L"parsehtml.cpp",
        //     failureLine,
        //     name ? name : L"")
        // failureText for AddFormattedStringMap:
        //     L"[%s][%d] CParseHTML::AddSTringMap failed. too long argument. name[%s]"
        // failureText for AddFormattedStringLocalMap:
        //     L"[%s][%d] ADdStringLocalMap Failed. too long argument. name[%s]"
        L2_LOG_ERROR(
            Log,
            "Cannot format %s HTML parameter '%s': %zu UTF-16 units do not fit the %zu-unit buffer; the parameter map is "
            "unchanged.",
            failureLine == 0x100 ? "instance" : (failureLine == 0x123 ? "local" : "selected"), Utf16ToUtf8(name).c_str(),
            value.size(), kBufferUnits
        );
        return;
    }

    std::u16string key = Trim(std::u16string(name));
    value = Trim(std::move(value));
    if (key.empty() || value.empty()) {
        return;
    }
    destination[std::u16string(key)] = std::u16string(value);
}

CParseHTML::CParseHTML() :
    m_nTempletCount(0), m_alignment94(0), m_mySocket(nullptr) {
    L2_GUARD_FUNCTION();
    m_html.assign({});
    m_mapParam.clear();
    m_mapTemplet.clear();
}

CParseHTML::CParseHTML(CUserSocket* const socket) :
    m_nTempletCount(0), m_alignment94(0), m_mySocket(socket) {
    L2_GUARD_FUNCTION();
    m_html.assign({});
    m_mapParam.clear();
    m_mapTemplet.clear();
    if (socket == nullptr) {
        // Log.Add(LOG_ERROR, L"CParseHTML invalid socket")
        // L2_LOG_ERROR(Log, L"CParseHTML invalid socket")
        L2_LOG_WARN(Log, "HTML renderer was constructed with a null user socket; it has no send destination.");
    }
}

CParseHTML::~CParseHTML() { m_mySocket = nullptr; }

bool CParseHTML::GetBodyFromFile(const char16_t* const fileName, const int language) {
    L2_GUARD_FUNCTION();
    if (fileName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] html file name is null",
        //     L"parsehtml.cpp",
        //     0x1f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] html file name is null",
        //     L"parsehtml.cpp",
        //     0x1f)
        L2_LOG_ERROR(Log, "HTML body filename is null (language %d); the cache lookup still follows.", language);
    }

    std::u16string body;
    if (!g_HTMLCache.GetHTMLFile(fileName, body, language)) {
        return false;
    }
    m_html.assign(runtime::StringView(body));
    GetTempletFromFile();
    return true;
}

void CParseHTML::GetTempletFromFile() {
    // Current L2Server 0x0092538c.
    L2_GUARD_FUNCTION();
    m_nTempletCount = 0;
    std::u16string source(runtime::StringView(m_html));
    if (source.empty()) {
        return;
    }
    if (source.size() > 0x2000) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] HTML size is too big!!! [%d]",
        //     L"parsehtml.cpp",
        //     0x53,
        //     static_cast<int>(source.size()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] HTML size is too big!!! [%d]",
        //     L"parsehtml.cpp",
        //     0x53,
        //     static_cast<int>(source.size()))
        L2_LOG_ERROR(
            Log,
            "HTML body has %zu UTF-16 code units, exceeding 8192; template extraction is skipped and the template count "
            "remains reset.",
            source.size()
        );
        return;
    }

    constexpr std::u16string_view beginMarker = u"<!--TEMPLET";
    constexpr std::u16string_view endMarker = u"TEMPLET-->";
    std::u16string body;
    std::size_t cursor = 0;
    for (;;) {
        const std::size_t begin = source.find(beginMarker, cursor);
        if (begin == std::u16string::npos) {
            body.append(source, cursor, std::u16string::npos);
            break;
        }
        body.append(source, cursor, begin - cursor);
        const std::size_t idStart = begin + beginMarker.size();
        if (idStart >= source.size()) {
            break;
        }
        const int templateId = GetIntFromString(reinterpret_cast<SharedUtilsWideChar*>(source.data() + idStart));
        const std::size_t contentStart = idStart + 1;
        const std::size_t end = source.find(endMarker, contentStart);
        if (end == std::u16string::npos) {
            break;
        }
        std::u16string content = Trim(source.substr(contentStart, end - contentStart));
        if (!content.empty()) {
            ++m_nTempletCount;
            m_mapTemplet[templateId] = std::u16string(content);
        }
        cursor = end + endMarker.size();
    }
    m_html.assign(body);
}

std::u16string CParseHTML::GetTempletStr(const int templateId) {
    // Current L2Server 0x00922ea8.
    L2_GUARD_FUNCTION();
    const auto position = m_mapTemplet.find(templateId);
    if (position == m_mapTemplet.end()) {
        return std::u16string(u"");
    }
    return position->second;
}

std::u16string CParseHTML::MakeHTMLFromStrTemplet(const int templateId, StrStrMapType& parameters) {
    // Current L2Server 0x00923b6c.
    L2_GUARD_FUNCTION();
    const std::u16string selected = templateId == 0 ? m_html : GetTempletStr(templateId);
    if (selected.empty()) {
        return std::u16string(u"");
    }

    const std::u16string_view selectedView = runtime::StringView(selected);
    const std::size_t terminator = selectedView.find(u'\0');
    const std::size_t sourceLength = terminator == std::u16string_view::npos ? selectedView.size() : terminator;
    if (static_cast<std::int32_t>(sourceLength) >= 0x2001) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] HTML size is too big!!! [%d]",
        //     L"parsehtml.cpp",
        //     0xaa,
        //     static_cast<std::int32_t>(sourceLength))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] HTML size is too big!!! [%d]",
        //     L"parsehtml.cpp",
        //     0xaa,
        //     static_cast<std::int32_t>(sourceLength))
        L2_LOG_ERROR(
            Log, "HTML template %d has %zu UTF-16 code units, exceeding 8192; returning the HTML body-error page.",
            templateId, sourceLength
        );
        return std::u16string(u"<html><body>Can not get HTML body</body></html>");
    }

    std::array<char16_t, 0x2001> sourceBuffer{};
    if (sourceLength >= 0x2000) {
        sourceBuffer[0] = u'\0';
        errno = ERANGE;
        runtime::process::HandleInvalidParameter(L"wcscpy_s");
    } else {
        std::copy_n(selectedView.data(), sourceLength, sourceBuffer.data());
    }
    sourceBuffer[sourceLength] = u'\0';

    const std::u16string source(sourceBuffer.data());
    std::u16string output;
    std::size_t cursor = 0;
    for (;;) {
        const std::size_t begin = source.find(u"<?", cursor);
        if (begin == std::u16string::npos) {
            output.append(source, cursor, std::u16string::npos);
            break;
        }
        output.append(source, cursor, begin - cursor);
        const std::size_t nameStart = begin + 2;
        const std::size_t remaining = source.size() - nameStart;
        const std::size_t end = remaining < 3 ? std::u16string::npos : source.find(u"?>", nameStart);
        if (end == std::u16string::npos) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid html parameter",
            //     L"parsehtml.cpp",
            //     0xd8)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid html parameter",
            //     L"parsehtml.cpp",
            //     0xd8)
            L2_LOG_ERROR(
                Log,
                "HTML template %d has an invalid parameter at UTF-16 offset %zu: insufficient text after '<?' or missing "
                "'?>'; appending the invalid-page marker and stopping expansion.",
                templateId, begin
            );
            output += u"***HTML page is invalid***";
            output.append(source, cursor, begin - cursor);
            break;
        }
        const std::u16string name(std::u16string_view(source).substr(nameStart, end - nameStart));
        const auto parameter = parameters.find(name);
        if (parameter != parameters.end()) {
            output.append(runtime::StringView(parameter->second));
        }
        cursor = end + 2;
    }
    return std::u16string(output);
}

void CParseHTML::AddStringMap(std::u16string_view name, std::u16string_view value) {
    AddStringLocalMap(m_mapParam, name, value);
}

void CParseHTML::AddStringLocalMap(StrStrMapType& parameters, std::u16string_view name, std::u16string_view value) {
    L2_GUARD_FUNCTION();
    // Preserve the former %s formatter's C-string and native buffer limits.
    value = value.substr(0, value.find(u'\0'));
    if (value.size() >= 0x5000 && Utf16ToWide(value).size() >= 0x5000) {
        L2_LOG_ERROR(
            Log,
            "Cannot store HTML parameter '%s': text exceeds the 20479-character formatting limit; the map is unchanged.",
            Utf16ToUtf8(name).c_str()
        );
        return;
    }
    std::u16string key = Trim(std::u16string(name));
    std::u16string text = Trim(std::u16string(value));
    if (!key.empty() && !text.empty()) {
        parameters[std::move(key)] = std::move(text);
    }
}

bool CParseHTML::SendHTML(std::u16string html) {
    // Current L2Server 0x00922d3c.
    L2_GUARD_FUNCTION();
    if (m_mySocket == nullptr) {
        return false;
    }
    User* const user = m_mySocket->GetUser();
    if (user == nullptr) {
        return false;
    }
    if (html.size() < 0x1ce9) {
        const std::u16string hostHtml(html);
        user->ShowHTML(nullptr, hostHtml.c_str(), 0, HTML::LAUNCH, true);
        return true;
    }
    // Log.Add(LOG_ERROR, L"CParseHTML Too large packet")
    // L2_LOG_ERROR(Log, L"CParseHTML Too large packet")
    L2_LOG_ERROR(Log, "Cannot display HTML: %zu UTF-16 code units exceed 7400; the page is not sent.", html.size());
    return false;
}

bool CParseHTML::SendHTML(
    std::u16string html,
    const int favoriteMode,
    const int argument1,
    const int argument2,
    const int argument3,
    const int argument4,
    const int argument5,
    const wchar_t* const stringArgument,
    const int sendMode
) {
    // Current L2Server 0x00922fec.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket->GetUser();
    if (user == nullptr) {
        return false;
    }

    const std::u16string favoriteLink = FormatLimited<0x38>(u"bypass %s", u"_bbsgetfav");
    std::u16string description;
    const char16_t* command = u"";
    switch (favoriteMode) {
        case 1:
            description = u"List boards";
            command = u"_bbsinit";
            break;
        case 2:
            description = u"List articles";
            command = u"_bbslist";
            break;
        case 3:
            description = FormatLimited<0x38>(u"Read %d article ", argument2);
            command = u"_bbsread";
            break;
        case 4:
            description = u"Write";
            command = u"_bbswriteform";
            break;
        case 5:
            description = FormatLimited<0x38>(u"Reply %d article", argument2);
            command = u"_bbsreplyform";
            break;
        case 6:
            description = FormatLimited<0x38>(u"Modify %d article", argument2);
            command = u"_bbsmodify";
            break;
        default:
            break;
    }

    std::u16string favoriteAction;
    if (1 <= favoriteMode && favoriteMode <= 6) {
        favoriteAction = FormatLimited<0xc8>(
            u"bypass %s_%s%s_%d_%d_%d_%d_%d_%s", u"_bbsaddfav", description, command, argument1, argument2, argument3,
            argument4, argument5, stringArgument
        );
    }

    // Log.Add(
    //     LOG_DEBUG,
    //     L"parsehtml html.size[%d], boardid[%d], bid[%d] ",
    //     static_cast<int>(html.size()),
    //     argument1,
    //     argument2)
    // Noise: Per-page board IDs and payload size are routine send-path tracing. The size-failure branches keep actionable
    // diagnostics. L2_LOG_DEBUG(Log,
    //     L"parsehtml html.size[%d], boardid[%d], bid[%d] ",
    //     static_cast<int>(html.size()),
    //     argument1,
    //     argument2)

    const auto sendPacket = [this, &favoriteLink, &favoriteAction](const char16_t* body) {
        m_mySocket->Send(
            "ccSSSSSSSSS", 0x7b, 1, u"bypass _bbshome", favoriteLink.c_str(), u"bypass _bbsloc", u"bypass _bbsclan",
            u"bypass _bbsmemo", u"bypass _maillist_0_1_0_", u"bypass _friendlist_0_", favoriteAction.c_str(), body
        );
    };

    if (sendMode == 2) {
        if (html.size() > 0x1ce8) {
            // Log.Add(LOG_ERROR, L"BBS Too large packet")
            // L2_LOG_ERROR(Log, L"BBS Too large packet")
            L2_LOG_ERROR(
                Log,
                "Cannot send community-board HTML in one packet: %zu UTF-16 code units exceed 7400 (board %d, article %d); "
                "requesting the size-error page instead.",
                html.size(), argument1, argument2
            );
            SendErrorMsgHtml(L"BBS Too large packet ( type 1 ) ", 1);
            return false;
        }
        sendPacket(html.c_str());
        return true;
    }

    if (html.size() > 0x56b8) {
        // Log.Add(LOG_ERROR, L"BBS Too larget packet")
        // L2_LOG_ERROR(Log, L"BBS Too larget packet")
        L2_LOG_ERROR(
            Log,
            "Cannot send community-board HTML in three packets: %zu UTF-16 code units exceed 22200 (board %d, article %d, "
            "send mode %d); requesting the size-error page instead.",
            html.size(), argument1, argument2, sendMode
        );
        SendErrorMsgHtml(L"BBS Too large packet ( type 3 ) ", 1);
        return false;
    }

    constexpr std::size_t kPartLength = 0x1ce8;
    const std::u16string_view source = runtime::StringView(html);
    for (std::size_t index = 0; index < 3; ++index) {
        std::u16string packetBody = index == 0 ? u"101\b" : (index == 1 ? u"102\b" : u"103\b");
        const std::size_t offset = index * kPartLength;
        if (source.size() > offset) {
            packetBody.append(source.substr(offset, std::min(kPartLength, source.size() - offset)));
        }
        sendPacket(packetBody.c_str());
    }
    return true;
}

bool CParseHTML::SendErrorMsgHtml(const wchar_t* const errorMessage, const int htmlType) {
    // Current L2Server 0x00923880.
    L2_GUARD_FUNCTION();
    std::wstring hostHtml = L"<html><body>&nbsp;<br>&nbsp;<br><center><br><hr>Error<br>";
    hostHtml += errorMessage;
    hostHtml += L"<br><hr></center></body></html>";
    std::u16string html(WideToUtf16(hostHtml.c_str()));
    if (htmlType == 0) {
        static_cast<void>(SendHTML(std::move(html)));
    } else {
        static_cast<void>(SendHTML(std::move(html), 10, 0, 0, 0, 0, 0, L"", 2));
    }
    return true;
}

std::u16string CParseHTML::GetWorldName(const int worldId) {
    // Current L2Server 0x00923a28.
    L2_GUARD_FUNCTION();
    const auto position = g_mapWorldName.find(worldId);
    if (position == g_mapWorldName.end()) {
        return std::u16string(u"");
    }
    return position->second;
}

std::u16string CParseHTML::MakeFormatedDate(const time_l2_t dateTime, const int option) {
    // Current L2Server 0x00923ff8.
    L2_GUARD_FUNCTION();
    return FormatDateTime(dateTime, option, false);
}

std::u16string CParseHTML::MakeFormatedDateLocal(const time_l2_t dateTime, const int option) {
    // Current L2Server 0x00924224.
    L2_GUARD_FUNCTION();
    return FormatDateTime(dateTime, option, true);
}

std::u16string CParseHTML::ConvertStr(SharedUtilsWideChar* source, const int option) {
    // Current L2Server 0x0092448c.
    L2_GUARD_FUNCTION();
    const std::u16string original = ToTargetWide(source);
    std::u16string result;

    if (option == 1) {
        SharedUtilsWideChar* cursor = source;
        for (;;) {
            SharedUtilsWideChar* const begin = std::find(
                cursor, cursor + std::char_traits<SharedUtilsWideChar>::length(cursor), static_cast<SharedUtilsWideChar>('<')
            );
            if (*begin == static_cast<SharedUtilsWideChar>(0)) {
                break;
            }
            *begin = static_cast<SharedUtilsWideChar>(0);
            result += ToTargetWide(cursor);
            if (std::char_traits<SharedUtilsWideChar>::length(begin + 1) == 0) {
                break;
            }
            SharedUtilsWideChar* const end = std::find(
                begin + 1, begin + 1 + std::char_traits<SharedUtilsWideChar>::length(begin + 1),
                static_cast<SharedUtilsWideChar>('>')
            );
            if (*end != static_cast<SharedUtilsWideChar>(0)) {
                *end = static_cast<SharedUtilsWideChar>(0);
                cursor = end + 1;
            }
        }
        result += ToTargetWide(cursor);
        return std::u16string(result);
    }

    std::u16string converted(original);
    if (option == 2) {
        converted = ReplaceStr(std::move(converted), u"'", u"\x7f");
    } else if (option == 3) {
        converted = ReplaceStr(std::move(converted), u"\x7f", u"'");
    } else if (option == 4) {
        converted = ReplaceStr(std::move(converted), u"<br/>", u"\n");
        converted = ReplaceStr(std::move(converted), u"&#10;", u" ");
        converted = ReplaceStr(std::move(converted), u"&#160;", u" ");
        converted = ReplaceStr(std::move(converted), u"&", u"&amp;");
        converted = ReplaceStr(std::move(converted), u"<", u"&lt;");
        converted = ReplaceStr(std::move(converted), u">", u"&gt;");
        converted = ReplaceStr(std::move(converted), u"\r", u"");
        converted = ReplaceStr(std::move(converted), u"\n", u"<br1>");
        converted = ReplaceStr(std::move(converted), u"<br1><br1>", u"<br1><br>");
        converted = ReplaceStr(std::move(converted), u"<br><br1>", u"<br><br>");
        converted = ReplaceStr(std::move(converted), u"'", u"&apos;");
        converted = ReplaceStr(std::move(converted), u"\x7f", u"&apos;");
    } else {
        converted = std::u16string();
    }
    return converted;
}

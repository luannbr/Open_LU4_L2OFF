#include "HTMLCache.h"
#include "Shared/src/Runtime/Utf8.h"

#include "CConfig.h"
#include "ToDoList.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Current HTMLCache.cpp helper 0x0078efb0. Native error text is the host
// transport for FormatMessageW. The diagnostic and source line are kept.
void ReportGetLastError() {
#if defined(_WIN32)
    wchar_t* message = nullptr;
    ::FormatMessageW(0x1300, nullptr, ::GetLastError(), 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    if (message != nullptr) {
        // Log.Add(LOG_ERROR, L"[%s][%d] GetLastError = %s", L"htmlcache.cpp", 0x16, message)
        // L2_LOG_ERROR(Log, L"[%s][%d] GetLastError = %s", L"htmlcache.cpp", 0x16, message)
        L2_LOG_ERROR(Log, u"HTML cache operating-system error: %ls", message);
    }
#else
    const std::wstring message = Utf8ToWide(std::system_category().message(errno));
    // Log.Add(LOG_ERROR, L"[%s][%d] GetLastError = %s", L"htmlcache.cpp", 0x16, message.c_str())
    // L2_LOG_ERROR(Log, L"[%s][%d] GetLastError = %s", L"htmlcache.cpp", 0x16, message.c_str())
    L2_LOG_ERROR(Log, u"HTML cache operating-system error: %ls", message.c_str());
#endif
}

namespace {

// Current DWORD at 0x04d39080, shared by the folder completion summaries.
std::uint32_t g_HTMLCacheStartTick = 0;

// The existing synchronous filesystem carrier keeps native enumeration
// and FILE handles in m_hFind/m_hFile. No target layout or cache sidecar.
bool FindNextHtml(std::filesystem::directory_iterator& iterator, const bool advance) {
    std::error_code error;
    const std::filesystem::directory_iterator end;
    if (advance) {
        iterator.increment(error);
    }
    while (!error && iterator != end) {
        std::wstring extension = iterator->path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
            return std::towlower(value);
        });
        if (extension == L".htm") {
            return true;
        }
        iterator.increment(error);
    }
    if (error) {
        L2_LOG_ERROR(
            Log, "HTML directory enumeration failed: %s error %d (%s); this scan stops.", error.category().name(),
            error.value(), error.message().c_str()
        );
    }
    return false;
}

constexpr std::array<const wchar_t*, 9> kMultiLangSuffixes{
    L"_KOREA",
    L"_USA",
    L"_JAPAN",
    L"_TAIWAN",
    L"_CHINA",
    L"_THAILAND",
    L"_PHILIPPINES",
    L"_INDONESIA",
    L"_RUSSIA",
};

std::array<std::wstring, 9> g_MultiLangFolderNames{};

// Clients send country codes up to 13 (UserSocket accepts < 14), but only
// 0..8 have an HTML catalog. The original passed the invalid index on to
// GetFolderName, which reads past kMultiLangSuffixes and crashes this build.
// Use the USA catalog instead and report the substitution once.
constexpr int kFallbackHtmlLanguage = 1;

int ResolveHtmlLanguage(const int language, const char* const operation, const std::u16string_view fileName) {
    if (language >= 0 && language < static_cast<int>(kMultiLangSuffixes.size())) {
        return language;
    }
    static std::atomic<bool> reported{false};
    if (!reported.exchange(true, std::memory_order_relaxed)) {
        L2_LOG_WARN(
            Log,
            "Invalid HTML language %d while %s '%s' (expected 0..8); using language %d (html_USA). "
            "Further substitutions are not logged.",
            language, operation, Utf16ToUtf8(fileName).c_str(), kFallbackHtmlLanguage
        );
    }
    return kFallbackHtmlLanguage;
}

std::u16string HostToUtf16(const wchar_t* value, const std::size_t length = std::wstring_view::npos) {
    if (value == nullptr) {
        return {};
    }
    const std::wstring_view view =
        length == std::wstring_view::npos ? std::wstring_view(value) : std::wstring_view(value, length);
    std::u16string result;
    result.reserve(view.size());
    for (const wchar_t codeUnit : view) {
        const std::uint32_t codePoint = static_cast<std::uint32_t>(codeUnit);
        if (codePoint <= 0xffffU) {
            result.push_back(static_cast<char16_t>(codePoint));
        } else {
            const std::uint32_t adjusted = codePoint - 0x10000U;
            result.push_back(static_cast<char16_t>(0xd800U + (adjusted >> 10U)));
            result.push_back(static_cast<char16_t>(0xdc00U + (adjusted & 0x3ffU)));
        }
    }
    return result;
}

std::wstring Utf16ToHost(const std::u16string_view value) {
    std::wstring result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        std::uint32_t codePoint = value[index];
        if (0xd800U <= codePoint && codePoint <= 0xdbffU && index + 1 < value.size()) {
            const std::uint32_t low = value[index + 1];
            if (0xdc00U <= low && low <= 0xdfffU) {
                codePoint = 0x10000U + ((codePoint - 0xd800U) << 10U) + (low - 0xdc00U);
                ++index;
            }
        }
        result.push_back(static_cast<wchar_t>(codePoint));
    }
    return result;
}

std::u16string LowerUtf16(std::u16string value) {
    for (char16_t& codeUnit : value) {
        codeUnit = static_cast<char16_t>(std::towlower(static_cast<wchar_t>(codeUnit)));
    }
    return value;
}

template <typename Param>
void ResetParam(Param& parameter) {
    parameter.m_ParamName.assign({});
    parameter.m_ParamValue.assign({});
    parameter.m_bClientSide = false;
    parameter.m_bParamValueOnly = false;
}

template <typename Command>
bool ParseHTMLCommand(Command& output, const char16_t* command, const int length) {
    if (command == nullptr || length <= 0) {
        return false;
    }

    using Param = typename decltype(output.m_Params)::value_type;
    output.m_Params.clear();
    std::u16string token;
    Param parameter{};
    int state = 0;

    auto resetToken = [&token]() { token.clear(); };
    auto resetParameter = [&]() {
        ResetParam(parameter);
        resetToken();
    };
    auto publishParameter = [&]() { output.m_Params.push_back(parameter); };

    for (int index = 0; index < length; ++index) {
        const char16_t input = command[index];
        const bool last = index + 1 == length;

        if (state == 0) {
            if (input == L' ') {
                if (token == u"link") {
                    output.m_Cmd.assign(token);
                    state = 2;
                    parameter.m_bParamValueOnly = true;
                } else if (token != u"bypass" && token != u"-h" && !token.empty()) {
                    output.m_Cmd.assign(token);
                    state = 1;
                }
                resetToken();
            } else if (input == L'?') {
                output.m_Cmd.assign(token);
                state = 1;
                resetToken();
            } else {
                token.push_back(static_cast<char16_t>(std::towlower(input)));
                if (last) {
                    if (token.find(u".htm") != std::u16string::npos) {
                        output.m_Cmd.assign(u"link");
                        parameter.m_ParamValue.assign(token);
                        parameter.m_bParamValueOnly = true;
                        publishParameter();
                    } else {
                        output.m_Cmd.assign(token);
                    }
                }
            }
            continue;
        }

        if (state == 1) {
            if (input == L'=') {
                parameter.m_ParamName.assign(token);
                state = 2;
                resetToken();
            } else if (input == L' ') {
                if (!token.empty()) {
                    parameter.m_ParamValue.assign(token);
                    parameter.m_bParamValueOnly = true;
                    publishParameter();
                    resetParameter();
                }
            } else {
                token.push_back(static_cast<char16_t>(std::towlower(input)));
            }
            continue;
        }

        if (input == L'&') {
            if (!parameter.m_bClientSide) {
                parameter.m_ParamValue.assign(token);
            }
            publishParameter();
            resetParameter();
            state = 1;
        } else {
            if (input == L'$') {
                parameter.m_bClientSide = true;
            } else if (input != L' ') {
                token.push_back(static_cast<char16_t>(std::towlower(input)));
            }
            if (last) {
                if (!parameter.m_bClientSide) {
                    parameter.m_ParamValue.assign(token);
                }
                publishParameter();
            }
        }
    }
    return true;
}

std::filesystem::path BuildHtmlPath(const wchar_t* folder, const wchar_t* fileName) {
    // These arguments already use native wchar_t. The former round trip also
    // truncated supplementary characters when converting back on Windows.
    return runtime::ResolveResourcePath(
        std::filesystem::path("..") / std::filesystem::path(folder != nullptr ? folder : L"") /
        std::filesystem::path(fileName != nullptr ? fileName : L"")
    );
}

const wchar_t* ToDoPrefix(const E_TODOLIST_TYPE type) {
    switch (type) {
        case E_TODOLIST_TYPE_EVENT:
            return L"event_";
        case E_TODOLIST_TYPE_INSTANTZONE:
            return L"instantzone_";
        case E_TODOLIST_TYPE_OLYMPIAD:
            return L"olympiad_";
        case E_TODOLIST_TYPE_CASTLEWAR:
            return L"castlewar_";
        case E_TODOLIST_TYPE_DOMINIONWAR:
            return L"dominionwar_";
        case E_TODOLIST_TYPE_FORTRESSWAR:
            return L"fortresswar_";
        case E_TODOLIST_TYPE_AGITWAR:
            return L"agitwar_";
        case E_TODOLIST_TYPE_DAILY_BOSS_RAID:
            return L"raid_evad_";
        default:
            return L"";
    }
}

} // namespace

bool HTMLCmdData::Parse(const char16_t* command, const int length) { return ParseHTMLCommand(*this, command, length); }

int HTMLCmdData::Compare(const HTMLCmdData& other) const {
    if (runtime::StringView(m_Cmd) != runtime::StringView(other.m_Cmd) || m_Params.size() != other.m_Params.size()) {
        return -1;
    }
    for (std::size_t index = 0; index < m_Params.size(); ++index) {
        const HTMLParamData& expected = runtime::CheckedVectorAt(m_Params, index);
        const HTMLParamData& actual = runtime::CheckedVectorAt(other.m_Params, index);
        if (expected.m_bParamValueOnly) {
            if (!actual.m_ParamName.empty() ||
                runtime::StringView(expected.m_ParamValue) != runtime::StringView(actual.m_ParamValue)) {
                return -1;
            }
            continue;
        }
        if (runtime::StringView(expected.m_ParamName) != runtime::StringView(actual.m_ParamName)) {
            return -1;
        }
        if (!expected.m_bClientSide && runtime::StringView(expected.m_ParamValue) != runtime::StringView(actual.m_ParamValue)) {
            return -1;
        }
    }
    return 0;
}

bool HTMLCmdDataToDoList::Parse(const char16_t* command, const int length) {
    // Current 0x0079138c. Unlike the link-validation parser, value spaces
    // are significant (for example, the words in a to-do list title).
    if (command == nullptr || length <= 0) {
        return false;
    }
    m_Params.clear();
    std::u16string token;
    HTMLParamDataToDoList parameter;
    bool resetToken = true;
    bool resetParameter = true;
    int state = 0;
    for (int index = 0; index < length; ++index) {
        if (resetToken) {
            token.clear();
            resetToken = false;
        }
        if (resetParameter) {
            ResetParam(parameter);
            resetParameter = false;
        }
        const char16_t input = command[index];
        const bool last = index + 1 == length;
        if (state == 0) {
            if (input == L' ') {
                if (token == u"link") {
                    m_Cmd.assign(token);
                    state = 2;
                    parameter.m_bParamValueOnly = true;
                    resetToken = true;
                } else if (token == u"bypass" || token == u"-h") {
                    resetToken = true;
                } else if (!token.empty()) {
                    m_Cmd.assign(token);
                    state = 1;
                    resetToken = true;
                }
            } else if (input == L'?') {
                m_Cmd.assign(token);
                state = 1;
                resetToken = true;
            } else {
                token.push_back(static_cast<char16_t>(std::towlower(input)));
                if (last) {
                    if (token.find(u".htm") != std::u16string::npos) {
                        m_Cmd.assign(u"link");
                        parameter.m_ParamValue.assign(token);
                        parameter.m_bParamValueOnly = true;
                        m_Params.push_back(parameter);
                    } else {
                        m_Cmd.assign(token);
                    }
                }
            }
        } else if (state == 1) {
            if (input == L'=') {
                parameter.m_ParamName.assign(token);
                state = 2;
                resetToken = true;
            } else if (input == L' ') {
                if (!token.empty()) {
                    parameter.m_ParamValue.assign(token);
                    parameter.m_bParamValueOnly = true;
                    m_Params.push_back(parameter);
                    resetToken = resetParameter = true;
                }
            } else {
                token.push_back(static_cast<char16_t>(std::towlower(input)));
            }
        } else {
            if (input == L'&') {
                if (!parameter.m_bClientSide) {
                    parameter.m_ParamValue.assign(token);
                }
                m_Params.push_back(parameter);
                resetToken = resetParameter = true;
                state = 1;
            } else if (input == L'$') {
                parameter.m_bClientSide = true;
            } else {
                token.push_back(static_cast<char16_t>(std::towlower(input)));
            }
            if (last) {
                if (!parameter.m_bClientSide) {
                    parameter.m_ParamValue.assign(token);
                }
                m_Params.push_back(parameter);
            }
        }
    }
    return true;
}

const char16_t* HTMLCmdDataToDoList::GetStrValue(const char16_t* const name) const {
    const std::u16string_view target(name ? name : u"");
    for (const HTMLParamDataToDoList& parameter : m_Params) {
        if (runtime::StringView(parameter.m_ParamName) == target) {
            return parameter.m_ParamValue.c_str();
        }
    }
    return nullptr;
}

CHTMLCache::CHTMLCache() :
    m_pszFolder(nullptr),
    m_FindData{},
    m_hFind(nullptr),
    m_hFile(nullptr),
    m_hIOCP(nullptr),
    m_overlappedRead{},
    m_dwBuffer(0),
    m_dwFileSize(0),
    m_Buffer{} {
    L2_GUARD_FUNCTION();
}

CHTMLCache::~CHTMLCache() = default;

int CHTMLCache::OpenFile() {
    // Current 0x0078f04c: open the current enumeration entry, size-check it,
    // and submit the first 512-byte read. The host read completes inline.
    L2_GUARD_FUNCTION();
    const auto& iterator = *static_cast<std::filesystem::directory_iterator*>(m_hFind);
    const auto path = iterator->path();
#if defined(_WIN32)
    m_hFile = ::_wfopen(path.c_str(), L"rb");
#else
    m_hFile = std::fopen(path.c_str(), "rb");
#endif
    if (m_hFile == nullptr) {
        // CreateFileW returns INVALID_HANDLE_VALUE, which the target's null
        // check lets reach the failing IOCP association and this diagnostic.
        // ReportGetLastError()
        const int openError = errno;
        L2_LOG_ERROR(
            Log, "Cannot open HTML file '%s' for caching: errno %d (%s); this file is skipped.",
            runtime::Utf8Bytes(path.u8string()).c_str(), openError, std::generic_category().message(openError).c_str()
        );
        return 0;
    }
    auto* const file = static_cast<std::FILE*>(m_hFile);
    m_overlappedRead.fill(std::byte{});
    const int seekResult = std::fseek(file, 0, SEEK_END);
    const long size = seekResult == 0 ? std::ftell(file) : -1;
    m_dwFileSize = static_cast<std::uint32_t>(size);
    m_dwBuffer = 0;
    if (m_dwFileSize > 0x3f80U) {
        // Log.Add(LOG_ERROR, L"[%s][%d] Too Big HTML File[%s] size[%d]>%d",
        //     L"htmlcache.cpp", 0xc5, path.filename().wstring().c_str(),
        //     m_dwFileSize, 0x3f80)
        // L2_LOG_ERROR(Log, L"[%s][%d] Too Big HTML File[%s] size[%d]>%d",
        //     L"htmlcache.cpp", 0xc5, path.filename().wstring().c_str(),
        //     m_dwFileSize, 0x3f80)
        const int sizeError = errno;
        if (size < 0) {
            L2_LOG_ERROR(
                Log, "Cannot determine HTML file size for '%s': size result %lld, errno %d (%s); this file is skipped.",
                runtime::Utf8Bytes(path.u8string()).c_str(), static_cast<long long>(size), sizeError,
                std::generic_category().message(sizeError).c_str()
            );
        } else {
            L2_LOG_ERROR(
                Log,
                "HTML file '%s' exceeds the cache limit: size %lld bytes, stored byte count %u, limit %u; this file is "
                "skipped.",
                runtime::Utf8Bytes(path.u8string()).c_str(), static_cast<long long>(size),
                static_cast<unsigned>(m_dwFileSize), 0x3f80U
            );
        }
        std::fclose(file);
        m_hFile = nullptr;
        return 0;
    }
    if (std::fseek(file, 0, SEEK_SET) == 0) {
        m_dwBuffer = static_cast<std::uint32_t>(std::fread(m_Buffer.data(), 1, 0x200, file));
        if (!std::ferror(file)) {
            return 1;
        }
    }
    // Log.Add(LOG_ERROR, L"[%s][%d] HTML Caching : First ReadFileEx Failed(%d)",
    //     L"htmlcache.cpp", 0xce, errno)
    // Noise: The initial read failure and its delayed OS-error report are combined using the saved CRT errno.
    // L2_LOG_ERROR(Log, L"[%s][%d] HTML Caching : First ReadFileEx Failed(%d)",
    //     L"htmlcache.cpp", 0xce, errno)
    // ReportGetLastError()
    const int readError = errno;
    L2_LOG_ERROR(
        Log, "Initial seek/read failed for HTML file '%s': errno %d (%s); this file is skipped.",
        runtime::Utf8Bytes(path.u8string()).c_str(), readError, std::generic_category().message(readError).c_str()
    );
    std::fclose(file);
    m_hFile = nullptr;
    return 0;
}

bool CHTMLCache::ReadHTMLFile(const wchar_t* const fileName, std::u16string& output, const wchar_t* const folderName) {
    // Current L2Server.exe 0x0078f8ec.
    L2_GUARD_FUNCTION();
    if (std::wcscmp(fileName, L"chi.htm") == 0 || std::wcslen(fileName) > 1000U) {
        return false;
    }

    // Noise: A cache miss is expected. Successful on-demand reads need no INFO record. Failures below show the resolved
    // path. std::array<wchar_t, 1024> currentPath{}, static_cast<void>(std::swprintf(
    //     currentPath.data(),
    //     currentPath.size(),
    //     L"..\\%ls\\%ls",
    //     folderName,
    //     fileName))
    // // Log.Add(
    // //     LOG_NORMAL,
    // //     L"Uncached HTML[%s]",
    // //     currentPath.data())
    // L2_LOG_INFO(Log,
    //     L"Uncached HTML[%s]",
    //     currentPath.data())


    const std::wstring hostPath = BuildHtmlPath(folderName, fileName).wstring();
    int byteCount = -1;
    char* const image = LoadBinaryAbsolute(hostPath.c_str(), byteCount, true);
    if (image == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"HTML File does not exist folder[%s] FileName[%s] nBuffer[%s]",
        //     folderName,
        //     fileName,
        //     currentPath.data())
        // L2_LOG_ERROR(Log,
        //     L"HTML File does not exist folder[%s] FileName[%s] nBuffer[%s]",
        //     folderName,
        //     fileName,
        //     currentPath.data())
        L2_LOG_ERROR(
            Log, u"Cannot read HTML file '%ls'; the read failed and the output is unchanged. Reporting the call stack.",
            hostPath.c_str()
        );
        ::ReportCallStackNew(const_cast<wchar_t*>(L"HTML"), LOG_ERROR, false);
        return false;
    }

    image[byteCount] = '\0';
    image[byteCount + 1] = '\0';
    const auto* const bytes = reinterpret_cast<const unsigned char*>(image + 2);
    std::u16string contents;
    for (std::size_t index = 0;; ++index) {
        const char16_t codeUnit = static_cast<char16_t>(
            static_cast<std::uint16_t>(bytes[index * 2U]) | (static_cast<std::uint16_t>(bytes[index * 2U + 1U]) << 8U)
        );
        if (codeUnit == u'\0') {
            break;
        }
        contents.push_back(codeUnit);
    }
    output.assign(contents);
    std::free(image);
    return true;
}

bool CHTMLCache::GetHTMLFileContent(
    const char16_t* const fileName, std::u16string& output, const wchar_t* const folderName
) {
    // Current L2Server.exe 0x00791788.
    L2_GUARD_FUNCTION();
    if (fileName == nullptr) {
        return false;
    }
    const std::u16string key(LowerUtf16(std::u16string(fileName)));
    if (key.empty()) {
        ::ReportCallStack(LOG_ERROR, false);
        return false;
    }
    L2_GUARD(m_Lock);
    const auto found = m_mapCache.find(key);
    if (found != m_mapCache.end()) {
        output = found->second;
        return true;
    }
    const std::wstring hostFileName = Utf16ToHost(runtime::StringView(key));
    if (!ReadHTMLFile(hostFileName.c_str(), output, folderName)) {
        return false;
    }
    m_mapCache.emplace(key, output);
    return true;
}

bool CHTMLCache::RemoveCachingHTMLFile(const char16_t* const fileName) {
    L2_GUARD_FUNCTION();
    if (fileName == nullptr) {
        return false;
    }
    const std::u16string key(LowerUtf16(std::u16string(fileName)));
    const bool removed = [&] {
        L2_GUARD(m_Lock);
        const bool removed = m_mapCache.erase(key) != 0;
        return removed;
    }();
    return removed;
}

void CHTMLCache::ClearCaching() {
    // Current L2Server 0x007907c0.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        m_mapCache.clear();
    }
}

int CHTMLCache::IsValidLink(std::vector<HTMLCmdData>& expectedLinks, std::u16string_view command) {
    L2_GUARD_FUNCTION();
    HTMLCmdData parsed;
    parsed.Parse(command.data(), static_cast<int>(command.size()));
    for (const HTMLCmdData& expected : expectedLinks) {
        if (expected.Compare(parsed) == 0) {
            return 1;
        }
    }
    return 0;
}

void CHTMLCache::SetToDoListNameFromHTML(std::u16string& output, char* const html, int length) {
    // Current 0x00792228: the first 200 BYTES are a UTF-16 header.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 101> header{};
    length = std::min(length, 200);
    if (html != nullptr && length > 0) {
        std::memcpy(header.data(), html, static_cast<std::size_t>(length));
    }
    const auto tokens = split(std::u16string(header.data()), std::u16string(u"<>/"));
    for (const auto& token : tokens) {
        const auto lower = LowerUtf16(std::u16string(runtime::StringView(token)));
        if (lower == u"body") {
            break;
        }
        if (lower.find(u"todolist") == std::u16string::npos) {
            continue;
        }
        HTMLCmdDataToDoList command;
        command.Parse(lower.data(), static_cast<int>(lower.size()));
        const char16_t* const name = command.GetStrValue(u"name");
        if (name != nullptr) {
            output = TrimDoubleQuotaionMarks(std::u16string(name));
        }
        return;
    }
}

void CHTMLCache::GetToDoListNameFromHTML(
    std::u16string& output, const std::u16string& fileName, const wchar_t* const folderName
) {
    // Current 0x00792898. A miss publishes both caches but deliberately
    // leaves output empty until the next title-cache lookup.
    L2_GUARD_FUNCTION();
    output.assign(u"");
    if (fileName.empty()) {
        return;
    }
    L2_GUARD(m_Lock);
    const auto found = m_mapTNameCache.find(fileName);
    if (found != m_mapTNameCache.end()) {
        output = found->second;
        return;
    }
    std::u16string contents;
    const std::wstring hostFileName = Utf16ToHost(runtime::StringView(fileName));
    if (ReadHTMLFile(hostFileName.c_str(), contents, folderName)) {
        m_mapCache.emplace(fileName, contents);
        std::array<char, 200> header{};
        // This miss path copies a character count as a BYTE count in the
        // current image. Do not normalize it to contents.size() * 2.
        std::memcpy(header.data(), contents.c_str(), std::min<std::size_t>(contents.size(), header.size()));
        std::u16string name;
        SetToDoListNameFromHTML(name, header.data(), static_cast<int>(contents.size()));
        if (!name.empty()) {
            m_mapTNameCache.emplace(fileName, name);
        }
    }
}

void CHTMLCache::OnIOCallback(const int success, const std::uint32_t transferred, PortableOverlapped* const overlapped) {
    // Current 0x00793058. Drive the same per-file completion/publication
    // sequence synchronously on the host, without a demand-cache lookup.
    L2_GUARD_FUNCTION();
    const bool start = success == 1 && transferred == 0 && overlapped == nullptr;
    if (!start && overlapped != reinterpret_cast<PortableOverlapped*>(m_overlappedRead.data())) {
        // Log.Add(LOG_ERROR, L"[%s][%d] HTML Caching failed", L"htmlcache.cpp", 0x130)
        // L2_LOG_ERROR(Log, L"[%s][%d] HTML Caching failed", L"htmlcache.cpp", 0x130)
        L2_LOG_ERROR(
            Log,
            u"HTML cache '%ls' received an unexpected completion context (success %d, transferred %u bytes); this callback "
            u"is ignored.",
            m_pszFolder ? m_pszFolder : L"<unset>", success, static_cast<unsigned>(transferred)
        );
        return;
    }
    std::unique_ptr<std::filesystem::directory_iterator> enumeration;
    if (start) {
        // Log.Add(LOG_NORMAL, L"HTML Cache start[%s]", m_pszFolder)
        // L2_LOG_INFO(Log, L"HTML Cache start[%s]", m_pszFolder)
        L2_LOG_INFO(Log, u"Preloading HTML cache '%ls'...", m_pszFolder ? m_pszFolder : L"<unset>");
        g_HTMLCacheStartTick = runtime::TickCount32();
        std::error_code error;
        enumeration = std::make_unique<std::filesystem::directory_iterator>(BuildHtmlPath(m_pszFolder, L""), error);
        if (error || !FindNextHtml(*enumeration, false)) {
            // Log.Add(LOG_ERROR, L"No HTML Files")
            // L2_LOG_ERROR(Log, L"No HTML Files")
            if (error) {
                L2_LOG_ERROR(
                    Log, "Cannot enumerate HTML cache '%s': %s error %d (%s); preload did not start.",
                    WideToUtf8(m_pszFolder ? m_pszFolder : L"<unset>").c_str(), error.category().name(), error.value(),
                    error.message().c_str()
                );
            } else {
                L2_LOG_ERROR(
                    Log, u"No .htm entry is available for HTML cache '%ls'; preload did not start.",
                    m_pszFolder ? m_pszFolder : L"<unset>"
                );
            }
            return;
        }
        m_hFind = enumeration.get();
        if (!OpenFile()) {
            // Log.Add(LOG_ERROR, L"%s File Cache Failed", m_pszFolder)
            // L2_LOG_ERROR(Log, L"%s File Cache Failed", m_pszFolder)
            L2_LOG_ERROR(
                Log, u"HTML cache '%ls' could not load its first file; preload stops. The file error is reported above.",
                m_pszFolder ? m_pszFolder : L"<unset>"
            );
            m_hFind = nullptr;
            return;
        }
    } else {
        enumeration.reset(static_cast<std::filesystem::directory_iterator*>(m_hFind));
        m_dwBuffer += transferred;
    }

    for (;;) {
        auto* const file = static_cast<std::FILE*>(m_hFile);
        while (m_dwBuffer < m_dwFileSize) {
            const auto bytes = static_cast<std::uint32_t>(std::fread(m_Buffer.data() + m_dwBuffer, 1, 0x200, file));
            if (std::ferror(file) || bytes == 0) {
                // Log.Add(LOG_ERROR, L"[%s][%d] HTML Caching : First ReadFileEx Failed(%d)",
                //     L"htmlcache.cpp", 0xfc, errno)
                // Noise: One read outcome replaces the duplicate numeric buffer-address row and delayed OS-error row. EOF
                // does not report stale errno. L2_LOG_ERROR(Log, L"[%s][%d] HTML Caching : First ReadFileEx Failed(%d)",
                //     L"htmlcache.cpp", 0xfc, errno)
                // // Log.Add(LOG_ERROR, L"[%s][%d] HTML Caching : %d %d %d",
                // //     L"htmlcache.cpp", 0xfd,
                // //     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(m_Buffer.data() + m_dwBuffer)),
                // //     std::min<std::uint32_t>(m_dwFileSize - m_dwBuffer, 0x200), bytes)
                // L2_LOG_ERROR(Log, L"[%s][%d] HTML Caching : %d %d %d",
                //     L"htmlcache.cpp", 0xfd,
                //     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(m_Buffer.data() + m_dwBuffer)),
                //     std::min<std::uint32_t>(m_dwFileSize - m_dwBuffer, 0x200), bytes)
                // ReportGetLastError()
                const int readError = errno;
                const bool readFailed = std::ferror(file) != 0;
                const std::string readPath = enumeration && *enumeration != std::filesystem::directory_iterator{}
                    ? runtime::Utf8Bytes((*enumeration)->path().u8string())
                    : std::string("<unknown HTML file>");
                if (readFailed) {
                    L2_LOG_ERROR(
                        Log,
                        "Reading HTML file '%s' failed: %u of %u bytes buffered, last read %u bytes; errno %d (%s). Preload "
                        "stops and earlier cache entries are retained.",
                        readPath.c_str(), static_cast<unsigned>(m_dwBuffer), static_cast<unsigned>(m_dwFileSize),
                        static_cast<unsigned>(bytes), readError, std::generic_category().message(readError).c_str()
                    );
                } else {
                    L2_LOG_ERROR(
                        Log,
                        "Unexpected end of HTML file '%s': %u of %u bytes buffered, last read %u bytes. Preload stops and "
                        "earlier cache entries are retained.",
                        readPath.c_str(), static_cast<unsigned>(m_dwBuffer), static_cast<unsigned>(m_dwFileSize),
                        static_cast<unsigned>(bytes)
                    );
                }
                std::fclose(file);
                m_hFile = m_hFind = nullptr;
                return;
            }
            m_dwBuffer += bytes;
        }

        // The target strips the first two bytes unconditionally and appends
        // UTF-16 NUL. Host padding bounds malformed/unterminated inputs.
        // production files all have at least the two-byte prefix.
        std::vector<char> image(std::max<std::uint32_t>(m_dwFileSize, 2) + 2, '\0');
        if (m_dwFileSize >= 2) {
            std::memcpy(image.data(), m_Buffer.data() + 2, m_dwFileSize - 2);
        }
        const std::wstring fileName = (*enumeration)->path().filename().wstring();
        const std::u16string key(LowerUtf16(HostToUtf16(fileName.c_str())));
        m_mapCache.emplace(key, std::u16string(reinterpret_cast<const char16_t*>(image.data())));
        std::u16string title;
        SetToDoListNameFromHTML(title, image.data(), static_cast<int>(m_dwFileSize));
        if (!title.empty()) {
            m_mapTNameCache.emplace(key, title);
        }
        std::fclose(file);
        m_hFile = nullptr;
        do {
            if (!FindNextHtml(*enumeration, true)) {
                // Log.Add(LOG_NORMAL, L"All %s(%d) Files cached [%ds]", m_pszFolder,
                //     static_cast<std::uint32_t>(m_mapCache.size()),
                //     (runtime::TickCount32() - g_HTMLCacheStartTick) / 1000)
                // L2_LOG_INFO(Log, L"All %s(%d) Files cached [%ds]", m_pszFolder,
                //     static_cast<std::uint32_t>(m_mapCache.size()),
                //     (runtime::TickCount32() - g_HTMLCacheStartTick) / 1000)
                L2_LOG_INFO(
                    Log, u"HTML scan ended for cache '%ls': the cache holds %llu entries, elapsed %u seconds.",
                    m_pszFolder ? m_pszFolder : L"<unset>", static_cast<unsigned long long>(m_mapCache.size()),
                    static_cast<unsigned>((runtime::TickCount32() - g_HTMLCacheStartTick) / 1000U)
                );
                m_hFind = nullptr;
                return;
            }
        } while (!OpenFile());
    }
}

CHTMLCacheManager::CHTMLCacheManager() {}

CHTMLCacheManager::~CHTMLCacheManager() = default;

const wchar_t* CHTMLCacheManager::GetFolderName(const int language) {
    // Current L2Server.exe 0x0078fdc8. The target returns a pointer from an
    // already destroyed temporary wstring. The kept host buffer is the
    // portability boundary for that allocator-dependent dangling carrier.
    L2_GUARD_FUNCTION();
    if (!g_bMultiLang) {
        return L"html";
    }
    std::wstring& folder = g_MultiLangFolderNames[static_cast<std::size_t>(language)];
    folder = L"html";
    folder += kMultiLangSuffixes[static_cast<std::size_t>(language)];
    return folder.c_str();
}

void CHTMLCacheManager::MakeToDoListHtmlFileName(std::u16string& output, E_TODOLIST_TYPE& type, const std::u16string& name) {
    L2_GUARD_FUNCTION();
    std::u16string fileName = u"todolist_";
    fileName += HostToUtf16(ToDoPrefix(type));
    fileName += runtime::StringView(name);
    fileName += u".htm";
    output.assign(fileName);
}

bool CHTMLCacheManager::GetHTMLFile(const char16_t* const fileName, std::u16string& output, const int language) {
    // Current L2Server.exe 0x00791bd4.
    L2_GUARD_FUNCTION();
    if (!g_bMultiLang) {
        return m_DefaultCache.GetHTMLFileContent(fileName, output, GetFolderName(language));
    }
    if (language < 0 || 8 < language) {
        // Log.Add(
        //     LOG_WARN,
        //     L"[%s][%d] invalid language[%d]",
        //     L"htmlcache.cpp",
        //     0x1dd,
        //     language)
        // L2_LOG_WARN(Log,
        //     L"[%s][%d] invalid language[%d]",
        //     L"htmlcache.cpp",
        //     0x1dd,
        //     language)
        // L2_LOG_ERROR(Log,
        //     L"Invalid HTML language %d for '%ls' (expected 0..8). The default cache is selected, but folder lookup still
        //     receives this invalid index.", language, fileName ? fileName : L"<null>")
        const int fallback = ResolveHtmlLanguage(language, "reading", fileName ? fileName : u"<null>");
        return m_MultiLangCache[fallback]->GetHTMLFileContent(fileName, output, GetFolderName(fallback));
    }
    return m_MultiLangCache[language]->GetHTMLFileContent(fileName, output, GetFolderName(language));
}

bool CHTMLCacheManager::RemoveCachingHTMLFile(const char16_t* const fileName, const int language) {
    // Current L2Server.exe 0x0079050c.
    L2_GUARD_FUNCTION();
    if (!g_bMultiLang) {
        return m_DefaultCache.RemoveCachingHTMLFile(fileName);
    }
    if (language < 0 || 8 < language) {
        // Log.Add(
        //     LOG_WARN,
        //     L"[%s][%d] invalid language[%d]",
        //     L"htmlcache.cpp",
        //     0x1f1,
        //     language)
        // L2_LOG_WARN(Log,
        //     L"[%s][%d] invalid language[%d]",
        //     L"htmlcache.cpp",
        //     0x1f1,
        //     language)
        // L2_LOG_WARN(Log,
        //     L"Invalid HTML language %d while removing '%ls' from the cache (expected 0..8). Removal uses the default
        //     cache.", language, fileName ? fileName : L"<null>")
        L2_LOG_WARN(
            Log,
            u"Invalid HTML language %d while removing '%ls' from the cache (expected 0..8); removal uses the default cache.",
            language, fileName ? fileName : u"<null>"
        );
        return m_DefaultCache.RemoveCachingHTMLFile(fileName);
    }
    return m_MultiLangCache[language]->RemoveCachingHTMLFile(fileName);
}

void CHTMLCacheManager::ClearCaching() {
    // Current L2Server 0x007908c0 assumes that every
    // multi-language cache was created by the preceding load lifecycle.
    L2_GUARD_FUNCTION();
    if (!g_bMultiLang) {
        m_DefaultCache.ClearCaching();
        return;
    }
    for (int language = 0; language < 9; ++language) {
        m_MultiLangCache[language]->ClearCaching();
    }
}

int CHTMLCacheManager::IsValidLink(
    std::vector<HTMLCmdData>& expectedLinks, std::u16string_view command, const int language
) {
    // Current L2Server.exe 0x00791d30.
    L2_GUARD_FUNCTION();
    if (!g_bMultiLang) {
        return m_DefaultCache.IsValidLink(expectedLinks, command);
    }
    if (language < 0 || 8 < language) {
        // Log.Add(
        //     LOG_WARN,
        //     L"[%s][%d] invalid language[%d]",
        //     L"htmlcache.cpp",
        //     0x205,
        //     language)
        // L2_LOG_WARN(Log,
        //     L"[%s][%d] invalid language[%d]",
        //     L"htmlcache.cpp",
        //     0x205,
        //     language)
        const int fallback = ResolveHtmlLanguage(language, "checking a link from", u"<link>");
        return m_MultiLangCache[fallback]->IsValidLink(expectedLinks, command);
    }
    return m_MultiLangCache[language]->IsValidLink(expectedLinks, command);
}

void CHTMLCacheManager::Load() {
    // Current L2Server.exe 0x00792c00. The portable synchronous callback is
    // the already classified replacement for the target's 2-second IO timer.
    L2_GUARD_FUNCTION();
    if (!g_bMultiLang) {
        m_DefaultCache.m_pszFolder = L"html";
        m_DefaultCache.OnIOCallback(1, 0, nullptr);
        return;
    }
    for (int language = 0; language < 9; ++language) {
        CHTMLCache* const cache = new CHTMLCache();
        cache->m_pszFolder = GetFolderName(language);
        cache->OnIOCallback(1, 0, nullptr);
        m_MultiLangCache[language] = cache;
    }
}

void CHTMLCacheManager::GetToDoListNameFromHTML(
    std::u16string& output, E_TODOLIST_TYPE& type, const std::u16string& name, const int language
) {
    // Current L2Server.exe 0x00792e40.
    L2_GUARD_FUNCTION();
    std::u16string fileName;
    MakeToDoListHtmlFileName(fileName, type, name);
    if (!g_bMultiLang) {
        m_DefaultCache.GetToDoListNameFromHTML(output, fileName, GetFolderName(language));
        return;
    }
    if (language < 0 || 8 < language) {
        // Log.Add(
        //     LOG_WARN,
        //     L"[%s][%d] invalid language[%d]",
        //     L"htmlcache.cpp",
        //     0x241,
        //     language)
        // L2_LOG_WARN(Log,
        //     L"[%s][%d] invalid language[%d]",
        //     L"htmlcache.cpp",
        //     0x241,
        //     language)
        const int fallback = ResolveHtmlLanguage(language, "reading the title of", fileName);
        m_MultiLangCache[fallback]->GetToDoListNameFromHTML(output, fileName, GetFolderName(fallback));
        return;
    }
    m_MultiLangCache[language]->GetToDoListNameFromHTML(output, fileName, GetFolderName(language));
}

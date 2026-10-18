#include "GameGuard.h"

#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/OperatorLog.h"

#include "Blowfish.h"
#include "FileLog.h"
#include "Platform/GameGuardProvider.h"

#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kErrorAllocation = 1;
constexpr std::uint32_t kErrorInactive = 4;
constexpr std::uint32_t kErrorConfig = 5;
constexpr std::uint32_t kErrorNoReply = 0x0b;
constexpr std::uint32_t kErrorProtocolMismatch = 0x0c;
constexpr std::uint32_t kErrorInvalidReply = 0x0d;
constexpr std::uint32_t kErrorInvalidGameGuardVersion = 0x0e;
constexpr std::uint32_t kProtocolVersionCheck = 0;
constexpr std::uint32_t kInitialGameGuardVersion = 20030101;
constexpr std::int32_t kPromotionSeconds = 1800;
constexpr std::int32_t kCandidateExpirySeconds = 3600;
constexpr std::size_t kCandidateCount = 5;

struct VersionCandidate {
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    std::int32_t lastSeen = 0;
    std::int32_t firstSeen = 0;
};

std::uint32_t g_maxActive = 0;
std::uint32_t g_protocolCount = 0;
_GG_AUTH_PROTOCOL* g_protocolHead = nullptr;
std::uint32_t g_currentGameGuardVersion = kInitialGameGuardVersion;
std::array<VersionCandidate, kCandidateCount> g_versionCandidates{};
bool g_useUpdateTimer = false;
std::uint32_t g_useLog = 0;
Blowfish_matrix g_gameGuardBlowfish{};
std::mutex g_gameGuardLock;
std::vector<std::unique_ptr<_GG_AUTH_PROTOCOL>> g_protocolOwners;
std::unordered_map<
    _GG_AUTH_PROTOCOL*,
    std::unique_ptr<authgated::platform::GameGuardProvider>>
    g_providerOwners;

std::int64_t AbsoluteElapsed(
    std::int32_t first,
    std::int32_t second) noexcept {
    std::int64_t difference =
        static_cast<std::int64_t>(first) -
        static_cast<std::int64_t>(second);
    return difference < 0 ? -difference : difference;
}

authgated::platform::GameGuardProvider* ProviderFor(
    _GG_AUTH_PROTOCOL* protocol) noexcept {
    const auto found = g_providerOwners.find(protocol);
    return found == g_providerOwners.end()
        ? nullptr
        : found->second.get();
}

bool IsValidGameGuardVersion(const _GG_VERSION& version) noexcept {
    return version.dwGGVer != 0 &&
        version.wYear >= 2003 && version.wYear <= 2010 &&
        version.wMonth >= 1 && version.wMonth <= 12 &&
        version.wDay >= 1 && version.wDay <= 31 &&
        version.wNum >= 1 && version.wNum <= 99;
}

std::filesystem::path NativeGameGuardPath(
    const char* basePath,
    const std::string& leaf) {
    std::string normalized = basePath ? basePath : "";
#if !defined(_WIN32)
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
#endif
    return normalized.empty()
        ? std::filesystem::path(leaf)
        : std::filesystem::path(normalized) / leaf;
}

// Internal messages use native filesystem strings. The external NpLog callback
// keeps its Windows ANSI input contract.
void WriteGameGuardDiagnostic(int type, const char* message) {
    if ((static_cast<unsigned int>(type) & 3U) == 0 || !message) return;
    CFileLog* logger = CFileLog::g_pDefaultFileLog
        ? CFileLog::g_pDefaultFileLog : &authGateLog;
#if defined(_WIN32)
    const std::u16string wide = portable::Utf16FromWindowsAnsi(message);
#else
    const std::u16string wide = Utf8ToUtf16(message);
#endif
    // Internal type-2 callers report provider/configuration load failures.
    L2_LOG(*logger, (static_cast<unsigned int>(type) & 2U) != 0 ? LOG_ERROR : LOG_DEBUG,
        u"%s", wide.c_str());
}

void NpLogFormat(int type, const char* format, ...) {
    std::array<char, 1024> message{};
    va_list arguments;
    va_start(arguments, format);
    // std::vsnprintf(
    //     message.data(),
    //     message.size(),
    //     format,
    //     arguments)
    const int length = std::vsnprintf(
        message.data(),
        message.size(),
        format,
        arguments);
    va_end(arguments);
    if (length < 0) {
        // NpLog(type, "[GameGuard] Could not format the diagnostic message.")
        // WriteGameGuardDiagnostic(type, "[GameGuard] Could not format the diagnostic message.")
        // Formatter failures are errors even when the requested record was DEBUG.
        WriteGameGuardDiagnostic(2, "[GameGuard] Could not format the diagnostic message.");
        return;
    }
    if (static_cast<std::size_t>(length) >= message.size()) {
        constexpr char marker[] = " [truncated]";
        // std::copy_n(marker, sizeof(marker),
        //     message.data() + message.size() - sizeof(marker))
        std::size_t end = message.size() - sizeof(marker);
#if !defined(_WIN32)
        // Internal native paths use UTF-8. The Windows path keeps ANSI.
        while (end > 0 && (static_cast<unsigned char>(message[end]) & 0xc0U) == 0x80U) --end;
#endif
        std::copy_n(marker, sizeof(marker), message.data() + end);
    }
    // NpLog(type, message.data())
    WriteGameGuardDiagnostic(type, message.data());
}

std::uint32_t LoadAuthProtocol(
    const std::filesystem::path& path,
    bool defaultProtocol) {
    auto protocol =
        std::unique_ptr<_GG_AUTH_PROTOCOL>(
            new (std::nothrow) _GG_AUTH_PROTOCOL());
    // if (!protocol) return kErrorAllocation.
    if (!protocol) {
        if ((g_useLog & 2U) != 0) {
            // NpLogFormat(2,
            //     "[GameGuard] Cannot allocate protocol state for provider \"%s\". The provider was not loaded.",
            //     path.string().c_str())
            // Avoid path conversion and UTF-16 allocation on the allocation-failure path.
            runtime::logging::Write("authgated", runtime::logging::Level::Error,
                "[GameGuard] Cannot allocate protocol state. Provider loading was not attempted.", __FILE__);
        }
        return kErrorAllocation;
    }

    std::uint32_t error = 0;
    auto provider = authgated::platform::LoadGameGuardProvider(
        path.string().c_str(),
        error);
    if (!provider) {
        if ((g_useLog & 2U) != 0) {
            // NpLogFormat(
            //     2,
            //     "[C$][S lib] Fail LoadLibrary : %s",
            //     path.string().c_str())
            NpLogFormat(2,
                "[GameGuard] Could not load protocol provider \"%s\" (provider error %u).",
                path.string().c_str(), static_cast<unsigned int>(error));
        }
        return error;
    }

    protocol->dwVersion = provider->GetVersion(g_useUpdateTimer);
    protocol->nLastTime = runtime::crt::Time32Now();
    _GG_AUTH_PROTOCOL* rawProtocol = protocol.get();
    g_providerOwners.emplace(rawProtocol, std::move(provider));
    g_protocolOwners.push_back(std::move(protocol));

    if (defaultProtocol) {
        rawProtocol->dwOption |= 3U;
        g_protocolHead = rawProtocol;
    } else {
        _GG_AUTH_PROTOCOL* tail = g_protocolHead;
        while (tail && tail->pNext) tail = tail->pNext;
        if (tail) {
            tail->pNext = rawProtocol;
            rawProtocol->pBefore = tail;
        }
    }

    {
        std::lock_guard<std::mutex> guard(g_gameGuardLock);
        ++g_protocolCount;
    }
    return 0;
}

void ClearCandidate(VersionCandidate& candidate) noexcept {
    candidate = {};
}

void ReportCandidates() {
    if ((g_useLog & 1U) == 0) return;
    for (std::size_t index = 0;
         index < g_versionCandidates.size();
         ++index) {
        const VersionCandidate& candidate = g_versionCandidates[index];
        // Noise: unused slots contain only zeros and add no candidate information.
        // This filter affects the diagnostic report only, not version tracking.
        if (candidate.version == 0) continue;
        // NpLogFormat(
        //     1,
        //     "[C$][S lib] [%ld]:[%ld][%ld][%ld][%ld], ",
        //     static_cast<long>(index),
        //     static_cast<long>(candidate.version),
        //     static_cast<long>(candidate.count),
        //     static_cast<long>(candidate.lastSeen),
        //     static_cast<long>(candidate.firstSeen))
        // NpLogFormat(1,
        //     "[GameGuard] Version candidate %ld: version %ld, observations %ld, last seen %ld, first seen %ld (Unix seconds).",
        //     static_cast<long>(index), static_cast<long>(candidate.version),
        //     static_cast<long>(candidate.count), static_cast<long>(candidate.lastSeen),
        //     static_cast<long>(candidate.firstSeen))
        NpLogFormat(1,
            "[GameGuard] Version candidate %zu: version %u, observations %u, last seen %ld, first seen %ld (Unix seconds).",
            index, static_cast<unsigned int>(candidate.version),
            static_cast<unsigned int>(candidate.count), static_cast<long>(candidate.lastSeen),
            static_cast<long>(candidate.firstSeen));
    }
    // NpLogFormat(
    //     1,
    //     "[C$][S lib] CURRENT VER : %ld",
    //     static_cast<long>(g_currentGameGuardVersion))
    // NpLogFormat(1, "[GameGuard] Current accepted version: %ld.",
    //     static_cast<long>(g_currentGameGuardVersion))
    NpLogFormat(1, "[GameGuard] Current accepted version: %u.",
        static_cast<unsigned int>(g_currentGameGuardVersion));
}

void TrackGameGuardVersion(std::uint32_t version) {
    if (version <= g_currentGameGuardVersion) return;

    const std::int32_t now = runtime::crt::Time32Now();
    for (std::size_t index = 0;
         index < g_versionCandidates.size();
         ++index) {
        VersionCandidate& candidate = g_versionCandidates[index];
        if (candidate.version != version || candidate.version == 0) {
            continue;
        }

        ++candidate.count;
        candidate.lastSeen = now;
        if (candidate.count < g_maxActive ||
            now - candidate.firstSeen <= kPromotionSeconds) {
            ReportCandidates();
            return;
        }

        _GG_UPREPORT report;
        report.dwBefore = g_currentGameGuardVersion;
        {
            std::lock_guard<std::mutex> guard(g_gameGuardLock);
            g_currentGameGuardVersion = candidate.version;
        }
        report.dwNext = g_currentGameGuardVersion;
        for (VersionCandidate& item : g_versionCandidates) {
            if (item.version <= g_currentGameGuardVersion ||
                AbsoluteElapsed(item.lastSeen, now) >
                    kCandidateExpirySeconds) {
                ClearCandidate(item);
            }
        }
        report.nType = 1;
        GGAuthUpdateCallback(&report);
        ReportCandidates();
        return;
    }

    for (VersionCandidate& candidate : g_versionCandidates) {
        if (candidate.version != 0) continue;
        candidate.version = version;
        candidate.count = 1;
        candidate.lastSeen = now;
        candidate.firstSeen = now;
        ReportCandidates();
        return;
    }

    // This selection keeps the shipped sentinel behavior:
    // candidate zero is the comparison baseline until another slot wins.
    std::size_t selected = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0;
         index < g_versionCandidates.size();
         ++index) {
        const VersionCandidate& candidate = g_versionCandidates[index];
        if (AbsoluteElapsed(candidate.lastSeen, now) <=
            kCandidateExpirySeconds) {
            continue;
        }
        const std::size_t baseline =
            selected == std::numeric_limits<std::size_t>::max()
            ? 0
            : selected;
        if (g_versionCandidates[baseline].count > candidate.count) {
            selected = index;
        }
    }
    if (selected == std::numeric_limits<std::size_t>::max()) {
        for (std::size_t index = 0;
             index < g_versionCandidates.size();
             ++index) {
            const std::size_t baseline =
                selected == std::numeric_limits<std::size_t>::max()
                ? 0
                : selected;
            if (g_versionCandidates[baseline].lastSeen >
                g_versionCandidates[index].lastSeen) {
                selected = index;
            }
        }
        if (selected == std::numeric_limits<std::size_t>::max()) {
            selected = 0;
        }
    }

    VersionCandidate& replacement = g_versionCandidates[selected];
    replacement.version = version;
    replacement.count = 1;
    replacement.lastSeen = now;
    replacement.firstSeen = now;
    ReportCandidates();
}

void PromoteProtocolIfReady(CCSAuth2& auth) {
    if (auth.m_bNewProtocol == 0 ||
        auth.m_AuthQueryTmp.dwIndex == kProtocolVersionCheck) {
        return;
    }

    _GG_AUTH_PROTOCOL* protocol = auth.m_pProtocol;
    auth.m_bNewProtocol = 0;
    if (!protocol || protocol == g_protocolHead) return;

    ++protocol->dwReferCount;
    const std::int32_t now = runtime::crt::Time32Now();
    protocol->nLastTime = now;
    if (protocol->nFirstTime == 0) protocol->nFirstTime = now;

    std::lock_guard<std::mutex> guard(g_gameGuardLock);
    if (protocol->dwReferCount < g_maxActive ||
        static_cast<std::uint32_t>(now - protocol->nFirstTime) <=
            static_cast<std::uint32_t>(kPromotionSeconds)) {
        return;
    }

    _GG_UPREPORT report;
    report.dwBefore = g_protocolHead->dwVersion;
    if (protocol->pNext) {
        protocol->pBefore->pNext = protocol->pNext;
        protocol->pNext->pBefore = protocol->pBefore;
    } else {
        protocol->pBefore->pNext = nullptr;
    }
    g_protocolHead->pBefore = protocol;
    g_protocolHead->bDisuse = 1;
    protocol->pNext = g_protocolHead;
    protocol->pBefore = nullptr;
    g_protocolHead = protocol;
    report.dwNext = g_protocolHead->dwVersion;
    report.nType = 2;
    GGAuthUpdateCallback(&report);
}

} // namespace

// AuthGateD.exe 0x00416FD0, gameguard.cpp.
void GGAuthUpdateCallback(const _GG_UPREPORT* report) {
    if (!report) return;
    CFileLog* logger = CFileLog::g_pDefaultFileLog
        ? CFileLog::g_pDefaultFileLog
        : &authGateLog;
    // const char16_t* category = report->nType == 1
    //     ? u"GameGuard Ver"
    //     : u"Protocol Num".
    const char16_t* category = report->nType == 1
        ? u"Accepted client version"
        : u"Active protocol version";
    // logger->Log(
    //     LOG_SYSTEM,
    //     u"GGAuth version update [%s] : [%ld] -> [%ld] \n",
    //     category,
    //     static_cast<std::int32_t>(report->dwBefore),
    //     static_cast<std::int32_t>(report->dwNext))
    // L2_LOG(*logger, LOG_SYSTEM,
    //     u"GGAuth version update [%s] : [%ld] -> [%ld] \n",
    //     category,
    //     static_cast<std::int32_t>(report->dwBefore),
    //     static_cast<std::int32_t>(report->dwNext))
    L2_LOG(*logger, LOG_SYSTEM,
        u"[GameGuard] %s changed from %u to %u.",
        category,
        static_cast<unsigned int>(report->dwBefore),
        static_cast<unsigned int>(report->dwNext));
}

// AuthGateD.exe 0x00417010, gameguard.cpp.
void NpLog(int type, const char* message) {
    if ((static_cast<unsigned int>(type) & 3U) == 0 || !message) {
        return;
    }
    CFileLog* logger = CFileLog::g_pDefaultFileLog
        ? CFileLog::g_pDefaultFileLog
        : &authGateLog;
    const std::u16string wide =
        portable::Utf16FromWindowsAnsi(message);
    // logger->Log(LOG_DEBUG, u"%s", wide.c_str())
    L2_LOG_DEBUG(*logger, u"%s", wide.c_str());
}

// ggsrvlib25.lib, AuthGateD.exe 0x00418480.
std::uint32_t InitGameguardAuth(
    const char* path,
    int numActive,
    bool useUpdateTimer,
    std::uint32_t useLog) {
    g_maxActive = static_cast<std::uint32_t>(numActive);
    g_protocolCount = 0;
    g_protocolHead = nullptr;
    g_currentGameGuardVersion = kInitialGameGuardVersion;
    g_versionCandidates.fill({});
    g_useUpdateTimer = useUpdateTimer;
    g_useLog = useLog;
    g_providerOwners.clear();
    g_protocolOwners.clear();

    const std::filesystem::path configPath =
        NativeGameGuardPath(path, "csauth2.cfg");
    std::ifstream config(configPath);
    if (!config) {
        CleanupGameguardAuth();
        if ((g_useLog & 2U) != 0) {
            // NpLogFormat(
            //     2,
            //     "[C$][S lib] Fail LoadAuthProtocol(%s) FAIL TO OPEN CFG FILE",
            //     configPath.string().c_str())
            NpLogFormat(2,
                "[GameGuard] Could not open protocol configuration \"%s\". Initialization failed.",
                configPath.string().c_str());
        }
        return kErrorConfig;
    }

    std::vector<int> protocolNumbers;
    protocolNumbers.reserve(100);
    int protocolNumber = 0;
    while (protocolNumbers.size() < 100 && config >> protocolNumber) {
        protocolNumbers.push_back(protocolNumber);
    }
    if (protocolNumbers.empty()) {
        CleanupGameguardAuth();
        if ((g_useLog & 2U) != 0) {
            // NpLogFormat(
            //     2,
            //     "[C$][S lib] Fail LoadAuthProtocol(%s) FAIL TO LOAD CFG FILE",
            //     configPath.string().c_str())
            NpLogFormat(2,
                "[GameGuard] No protocol numbers could be read from \"%s\". Initialization failed.",
                configPath.string().c_str());
        }
        return kErrorConfig;
    }

    for (std::size_t index = 0; index < protocolNumbers.size(); ++index) {
        const std::string fileName =
            "ggauth" + std::to_string(protocolNumbers[index]) + ".dll";
        const std::filesystem::path providerPath =
            NativeGameGuardPath(path, fileName);
        const std::uint32_t error =
            LoadAuthProtocol(providerPath, index == 0);
        if (error != 0) {
            CleanupGameguardAuth();
            return error;
        }
    }

    static constexpr unsigned char key[] = "@SAU^T2*KY";
    InitializeBlowfishLocal(&g_gameGuardBlowfish, key, 10);
    return 0;
}

// ggsrvlib25.lib, AuthGateD.exe 0x00418800.
void CleanupGameguardAuth() {
    g_protocolHead = nullptr;
    g_providerOwners.clear();
    g_protocolOwners.clear();
    g_protocolCount = 0;
}

// ggsrvlib25.lib, AuthGateD.exe 0x00418C70.
CCSAuth2::CCSAuth2() {
    Init();
}

// ggsrvlib25.lib, AuthGateD.exe 0x00418C80.
CCSAuth2::~CCSAuth2() {
    Close();
}

// ggsrvlib25.lib, AuthGateD.exe 0x00418C90.
void CCSAuth2::Init() {
    m_pProtocol = g_protocolHead;
    m_AuthQuery = {};
    m_AuthAnswer = {};
    m_AuthQueryTmp = {};
    m_bPrtcRef = 0;
    m_dwUserFlag = 0;
    m_GGVer = {};
    m_bNewProtocol = 0;
    m_bActive = 1;
}

// ggsrvlib25.lib, AuthGateD.exe 0x00418CF0.
void CCSAuth2::Close() {
    if (m_bActive == 0) return;
    m_bPrtcRef = 0;
    m_bActive = 0;
}

// ggsrvlib25.lib, AuthGateD.exe 0x00418D70.
std::uint32_t CCSAuth2::GetAuthQuery() {
    if (m_bActive == 0 || !m_pProtocol) return kErrorInactive;

    if (m_bPrtcRef == 0) {
        ++m_pProtocol->dwReferCount;
        if (g_protocolHead &&
            (g_protocolHead->dwOption & 2U) == 0 &&
            g_maxActive < g_protocolHead->dwReferCount) {
            g_protocolHead->dwOption |= 2U;
        }
        m_bPrtcRef = 1;
    }
    if ((m_dwUserFlag & 1U) != 0 &&
        !IsValidGameGuardVersion(m_GGVer)) {
        return kErrorInvalidGameGuardVersion;
    }

    authgated::platform::GameGuardProvider* provider =
        ProviderFor(m_pProtocol);
    if (!provider) return kErrorInactive;
    std::uint32_t result = provider->GetAuthQuery(
        m_pProtocol->dwOption,
        &m_GGVer,
        &m_dwUserFlag,
        &m_AuthQuery,
        &m_AuthAnswer);
    if (result == 0) {
        m_AuthQueryTmp = m_AuthQuery;
        if (m_AuthQuery.dwIndex != kProtocolVersionCheck) {
            result = provider->EncryptQuery(&m_AuthQuery);
        }
        GGEncryptPacket(&m_AuthQuery);
        if (result == 0) return 0;
    }
    if ((g_useLog & 1U) != 0) {
        if (result == kErrorNoReply) {
            // NpLog(1, "[C$][S lib] PrtcGetAuthQuery() : ERROR_GGAUTH_NO_REPLY")
            NpLog(1, "[GameGuard] Authentication query failed: provider reported no reply.");
        } else {
            // NpLogFormat(
            //     1,
            //     "[C$][S lib] PrtcGetAuthQuery() : ERROR_UNKNOWN [%ld]",
            //     static_cast<long>(result))
            NpLogFormat(1,
                "[GameGuard] Authentication query preparation failed (provider error %u).",
                static_cast<unsigned int>(result));
        }
    }
    return result;
}

// ggsrvlib25.lib, AuthGateD.exe 0x00418F20.
std::uint32_t CCSAuth2::CheckAuthAnswer() {
    if (m_bActive == 0 || !m_pProtocol) return kErrorInactive;

    GGDecryptPacket(&m_AuthAnswer);
    authgated::platform::GameGuardProvider* provider =
        ProviderFor(m_pProtocol);
    if (!provider) return kErrorInactive;
    if (m_AuthQueryTmp.dwIndex != kProtocolVersionCheck) {
        provider->DecryptAnswer(&m_AuthAnswer);
    }

    if (m_AuthAnswer.dwIndex > m_pProtocol->dwVersion) {
        if (m_AuthQueryTmp.dwIndex != kProtocolVersionCheck) {
            return kErrorProtocolMismatch;
        }
        m_bNewProtocol = 0;
        for (_GG_AUTH_PROTOCOL* protocol = g_protocolHead;
             protocol;
             protocol = protocol->pNext) {
            if (protocol->dwVersion == m_AuthAnswer.dwIndex &&
                protocol->bDisuse == 0) {
                --m_pProtocol->dwReferCount;
                m_pProtocol = protocol;
                m_bNewProtocol = 1;
                break;
            }
        }
        if (m_bNewProtocol == 0) return kErrorProtocolMismatch;
        provider = ProviderFor(m_pProtocol);
        if (!provider) return kErrorInactive;
    } else if (m_AuthAnswer.dwIndex < m_pProtocol->dwVersion) {
        return kErrorProtocolMismatch;
    }

    if (m_AuthQueryTmp.dwIndex == kProtocolVersionCheck) {
        const std::uint32_t packedVersion = m_AuthAnswer.dwValue2;
        const std::uint32_t year = packedVersion / 1000000U;
        const std::uint32_t month = (packedVersion / 10000U) % 100U;
        const std::uint32_t day = (packedVersion / 100U) % 100U;
        if (packedVersion == 0 || year < 2003 || year > 2010 ||
            month == 0 || month > 12 || day == 0 || day > 31 ||
            packedVersion < g_currentGameGuardVersion) {
            return kErrorInvalidGameGuardVersion;
        }
        m_GGVer.dwGGVer = packedVersion;
        m_GGVer.wYear = static_cast<std::uint16_t>(year);
        m_GGVer.wMonth = static_cast<std::uint16_t>(month);
        m_GGVer.wDay = static_cast<std::uint16_t>(day);
        m_GGVer.wNum = static_cast<std::uint16_t>(packedVersion % 100U);
    }
    if (!IsValidGameGuardVersion(m_GGVer)) {
        return kErrorInvalidGameGuardVersion;
    }

    const std::uint32_t result = provider->CheckAuthAnswer(
        m_pProtocol->dwOption,
        &m_GGVer,
        &m_dwUserFlag,
        &m_AuthQueryTmp,
        &m_AuthAnswer);
    if (result != 0) {
        if ((g_useLog & 1U) != 0) {
            if (result == kErrorInvalidReply) {
                // NpLog(1, "[C$][S lib] PrtcGetAuthQuery() : ERROR_GGAUTH_INVALID_REPLY")
                NpLog(1, "[GameGuard] Authentication answer rejected: provider reported an invalid reply.");
            } else {
                // NpLogFormat(
                //     1,
                //     "[C$][S lib] PrtcGetAuthQuery() : ERROR_UNKNOWN [%ld]",
                //     static_cast<long>(result))
                NpLogFormat(1,
                    "[GameGuard] Authentication answer validation failed (provider error %u).",
                    static_cast<unsigned int>(result));
            }
        }
        return result;
    }

    if (m_AuthQueryTmp.dwIndex == kProtocolVersionCheck &&
        m_AuthAnswer.dwValue2 > g_currentGameGuardVersion) {
        TrackGameGuardVersion(m_AuthAnswer.dwValue2);
    }
    PromoteProtocolIfReady(*this);
    return 0;
}

// ggsrvlib25.lib, AuthGateD.exe 0x00419860.
void GGEncryptPacket(_GG_AUTH_DATA* packet) {
    packet->dwIndex ^= UINT32_C(0x00fd3a9d);
    packet->dwValue1 ^= UINT32_C(0x9d2db902);
    Blowfish_encipher_local(
        &g_gameGuardBlowfish,
        &packet->dwIndex,
        &packet->dwValue2);
    packet->dwValue2 ^= UINT32_C(0x0064d84a);
    packet->dwValue3 ^= UINT32_C(0xf0f4c802);
    Blowfish_encipher_local(
        &g_gameGuardBlowfish,
        &packet->dwValue1,
        &packet->dwValue3);
}

// ggsrvlib25.lib, AuthGateD.exe 0x004198C0.
void GGDecryptPacket(_GG_AUTH_DATA* packet) {
    Blowfish_decipher_local(
        &g_gameGuardBlowfish,
        &packet->dwValue1,
        &packet->dwValue3);
    packet->dwValue2 ^= UINT32_C(0x0b64d24a);
    packet->dwValue3 ^= UINT32_C(0xf674c8b9);
    Blowfish_decipher_local(
        &g_gameGuardBlowfish,
        &packet->dwIndex,
        &packet->dwValue2);
    packet->dwIndex ^= UINT32_C(0x0afd349d);
    packet->dwValue1 ^= UINT32_C(0x9d28b918);
}

#include "Config.h"

#include "L2Core/src/inc/Guard.h"
#include "LottoGame.h"
#include "Portable/NPCServerConfiguration.h"
#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Shared/src/Log.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"
#include "net/Config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#include <io.h>
#else
#include <arpa/inet.h>
#include <unistd.h>
#endif

bool g_bFreeOpenServer = false;
int g_nCountryCode = 0;
double g_dF2PQuestRewardPenaltyPer = 0.0;
std::vector<int> g_vF2PRewardPenaltyQuests;
bool g_bUseServerStartMail = false;
bool g_bRunTestCase = true;
bool g_bUseResourceLoader = false;
bool g_bShowTestCaseProgress = false;
bool g_bTraceNpcAI = false;
bool g_bTraceGlobalObjectAI = false;
bool g_bEnableDirectCommand = false;
bool g_bSetReportNpcEventCount = false;
bool g_bAddTimerCheck = false;
// Current .data at 0x006c1f18 is zero. CConfig::Load publishes the INI
// default 10000 only after the configuration file has been opened.
int g_nAddTimerCheckCount = 0;
bool g_bExceptionMakeMiniDump = false;
in_addr cacheServerAddr{};
short cacheServerPort = 0;
BinaryInfo g_BinaryInfo{};

CConfig config;

int g_TimerOverflowLimit = 0;
int g_TimerOverflowCheckTick = 90;

// These two owners live in the target's zero-filled image range. Their INI
// defaults are not startup initializers: failed configuration loading leaves
// server type DEV (zero) and world ID zero.
L2ServerType::Value g_L2ServerType = L2ServerType::DEV;
int g_nServerId = 1;
int g_nWorldId = 0;
int g_nRaidServerID = -1;
bool g_bFastEnterworld = false;
bool g_bSpawnLog = false;
bool g_bScriptDebugLog = false;
int g_nMaxChaseCount = 0;
int g_nUseChasePenalty = 0;

bool g_bBossRespawnFactorEnabled = false;
int g_nBossRespawnFactor = 0;

namespace {

constexpr const wchar_t* CONFIG_FILE_NAME = L".\\l2npc.ini";

bool EqualsAsciiCaseInsensitive(const std::string& left, const char* right) {
    const std::size_t rightSize = std::char_traits<char>::length(right);
    if (left.size() != rightSize) {
        return false;
    }
    return std::equal(left.begin(), left.end(), right, [](char lhs, char rhs) {
        return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
    });
}

bool ReadBool(const runtime::Config& configuration, const char* section, const char* key, const char* defaultValue) {
    return EqualsAsciiCaseInsensitive(configuration.Get(section, key, defaultValue), "true");
}

L2ServerType::Value GetServerType(const std::string& value) {
    if (EqualsAsciiCaseInsensitive(value, "dev")) {
        return L2ServerType::DEV;
    }
    if (EqualsAsciiCaseInsensitive(value, "live")) {
        return L2ServerType::LIVE;
    }
    if (EqualsAsciiCaseInsensitive(value, "test")) {
        return L2ServerType::TEST;
    }
    if (EqualsAsciiCaseInsensitive(value, "movie")) {
        return L2ServerType::MOVIE;
    }
    if (EqualsAsciiCaseInsensitive(value, "broadcast")) {
        return L2ServerType::BROADCAST;
    }
    if (EqualsAsciiCaseInsensitive(value, "worldserver")) {
        return L2ServerType::WORLDSERVER;
    }
    return L2ServerType::LIVE;
}

std::u16string GetPrivateProfileString(
    const runtime::Config& configuration, const char* section, const char* key, const char* defaultValue, std::size_t bufferCount
) {
    std::u16string value = Utf8ToUtf16(configuration.Get(section, key, defaultValue));
    if (bufferCount == 0) {
        value.clear();
    } else if (value.size() >= bufferCount) {
        value.resize(bufferCount - 1);
    }
    return value;
}

template <std::size_t DestinationCount>
void CopyUtf16(char16_t (&destination)[DestinationCount], std::u16string_view source) {
    std::fill(std::begin(destination), std::end(destination), u'\0');
    const std::size_t count = std::min(source.size(), DestinationCount - 1);
    std::copy_n(source.data(), count, destination);
}

bool CanAccessConfiguration(int mode) {
    const runtime::Config* const configuration = l2npc::portable::GetNPCServerConfiguration();
    if (!configuration || !configuration->Loaded() || configuration->Path().empty()) {
        return false;
    }
#if defined(_WIN32)
    const std::wstring path = Utf8ToWide(configuration->Path());
    return ::_waccess(path.c_str(), mode) == 0;
#else
    return ::access(configuration->Path().c_str(), mode) == 0;
#endif
}

int ConvertMailReceivers(const char16_t* source, char* destination, int destinationCount) {
    return portable::WideCharToWindowsAnsi(source, -1, destination, destinationCount);
}

int TargetWtoi(const wchar_t* value) {
    // Current L2NPC.exe 0x00557564 enters the VS2010 32-bit wcstoxl body.
    // Host wchar_t/long widths therefore cannot be used on LP64 systems.
    while (std::iswspace(*value)) {
        ++value;
    }

    bool negative = false;
    if (*value == L'-' || *value == L'+') {
        negative = *value == L'-';
        ++value;
    }

    const std::uint32_t limit = negative ? 0x80000000U : 0x7fffffffU;
    std::uint32_t result = 0;
    bool overflow = false;
    bool converted = false;
    for (; *value >= L'0' && *value <= L'9'; ++value) {
        converted = true;
        const std::uint32_t digit = static_cast<std::uint32_t>(*value - L'0');
        if (result > (limit - digit) / 10U) {
            overflow = true;
        } else if (!overflow) {
            result = result * 10U + digit;
        }
    }

    if (!converted) {
        return 0;
    }
    if (overflow) {
        errno = ERANGE;
        return negative ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
    }
    if (!negative) {
        return static_cast<int>(result);
    }
    if (result == 0x80000000U) {
        return std::numeric_limits<int>::min();
    }
    return -static_cast<int>(result);
}

} // namespace

void MakePenaltyRewardQuestList(wchar_t* questIds) {
    wchar_t* context = nullptr;
    for (wchar_t* token = std::wcstok(questIds, L";", &context); token != nullptr;
        token = std::wcstok(nullptr, L";", &context)) {
        g_vF2PRewardPenaltyQuests.push_back(TargetWtoi(token));
    }
}

CConfig::CConfig() { L2_GUARD_FUNCTION(); }

CConfig::~CConfig() { L2_GUARD_FUNCTION(); }

void CConfig::SaveTimerOverflow() {
    L2_GUARD_FUNCTION();

    if (!CanAccessConfiguration(2)) {
        const runtime::Config* const configuration = l2npc::portable::GetNPCServerConfiguration();
        // Log.Add(
        //     LOG_ERROR,
        //     L"cannot write file \"%s\"",
        //     CONFIG_FILE_NAME)
        // Log.Add(
        //     LOG_ERROR,
        //     "[config] Cannot access configuration \"%s\" for writing. Timer-overflow settings were not saved.",
        //     configuration ? configuration->Path().c_str() : "<unbound>")
        L2_LOG_ERROR(
            Log, "[config] Cannot access configuration \"%s\" for writing; timer-overflow settings were not saved.",
            configuration ? configuration->Path().c_str() : "<unbound>"
        );
        return;
    }

    // Current L2NPC.exe 0x0041ac8c writes the two values and
    // does not roll the first one back if the second write fails.
    SetPrivateProfile(L"Debug", L"TimerOverflowLimit", g_TimerOverflowLimit, CONFIG_FILE_NAME);
    SetPrivateProfile(L"Debug", L"TimerOverflowCheckTick", g_TimerOverflowCheckTick, CONFIG_FILE_NAME);
}

void CConfig::Load() {
    L2_GUARD_FUNCTION();

    const runtime::Config* const configuration = l2npc::portable::GetNPCServerConfiguration();
    if (!CanAccessConfiguration(0) || !configuration) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"cannot read file \"L2NPC.INI\"")
        // Log.Add(
        //     LOG_ERROR,
        //     "[config] Configuration \"%s\" is unavailable or inaccessible. Keeping current settings.",
        //     configuration ? configuration->Path().c_str() : "<unbound>")
        L2_LOG_ERROR(
            Log, "[config] Configuration \"%s\" is unavailable or inaccessible; keeping current settings.",
            configuration ? configuration->Path().c_str() : "<unbound>"
        );
        return;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"loading l2npc.ini")
    // Log.Add(
    //     LOG_NORMAL,
    //     "[config] Applying NPC settings from \"%s\".",
    //     configuration->Path().c_str())
    L2_LOG_INFO(Log, "[config] Applying NPC settings from \"%s\".", configuration->Path().c_str());

    const std::string serverType = configuration->Get("Setting", "server_type", "live");
    g_L2ServerType = GetServerType(serverType);
    if (g_L2ServerType == L2ServerType::DEV) {
        std::u16string mailTo = GetPrivateProfileString(*configuration, "Setting", "MailTo", "", 0x3ff);
        std::array<char, 0x400> mailToAnsi{};
        ConvertMailReceivers(mailTo.data(), mailToAnsi.data(), static_cast<int>(mailToAnsi.size()));
        SystemMail::SetReceivers(mailToAnsi.data());
    }

    const int worldId = configuration->GetInt("World", "WorldId", 1);
    g_nServerId = worldId;
    g_nWorldId = worldId;

    g_bUseServerStartMail = ReadBool(*configuration, "Setting", "UseServerStartMail", "false");
    std::u16string cacheDAddressWide = GetPrivateProfileString(*configuration, "CacheD", "address", "localhost", 0x3ff);
    std::array<char, 0x800> cacheDAddressAnsi{};
    UnicodeToAnsi(cacheDAddressWide.data(), 0x400, cacheDAddressAnsi.data(), static_cast<int>(cacheDAddressAnsi.size()));
    cacheServerPort = static_cast<std::int16_t>(configuration->GetInt("CacheD", "port", 12008));
    cacheServerAddr.s_addr = ::inet_addr(cacheDAddressAnsi.data());
    CopyUtf16(
        g_BinaryInfo.updateVersion,
        GetPrivateProfileString(
            *configuration, "setting", "binary_update_version", "Please insert a chaotic throne version!", 0x3ff
        )
    );

    g_bSpawnLog = ReadBool(*configuration, "Setting", "SpawnLog", "false");
    l2npc::g_bCanLotto = EqualsAsciiCaseInsensitive(configuration->Get("Setting", "isnonpk", "false"), "false");
    g_nMaxChaseCount = configuration->GetInt("Setting", "MaxChaseCount", 0);
    g_bScriptDebugLog = ReadBool(*configuration, "Setting", "ScriptDebugLog", "false");
    g_bRunTestCase = ReadBool(*configuration, "Setting", "RunTestCase", "true");

    g_bBossRespawnFactorEnabled = ReadBool(*configuration, "Setting", "BossRespawnFactorEnabled", "false");
    g_nBossRespawnFactor = configuration->GetInt("Setting", "BossRespawnFactor", 0);
    if (g_nBossRespawnFactor < -99) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"too small g_nBossRespawnFactor - loading fail")
        // Log.Add(
        //     LOG_WARN,
        //     "[config] [Setting] BossRespawnFactor=%d is below -99 in \"%s\". Using 0 and continuing configuration
        //     loading.", g_nBossRespawnFactor, configuration->Path().c_str())
        L2_LOG_WARN(
            Log,
            "[config] [Setting] BossRespawnFactor=%d is below -99 in \"%s\"; using 0 and continuing configuration loading.",
            g_nBossRespawnFactor, configuration->Path().c_str()
        );
        g_nBossRespawnFactor = 0;
    }

    g_bFastEnterworld = ReadBool(*configuration, "Setting", "FastEnterworld", "false");
    g_bUseResourceLoader = ReadBool(*configuration, "Setting", "UseResourceLoader", "false");
    const std::string showTestCaseProgress = configuration->Get("Setting", "ShowTestCaseProgress", "false");
    g_bShowTestCaseProgress = EqualsAsciiCaseInsensitive(showTestCaseProgress, "true");

    // Current 0x0041add0 compares the still-live ShowTestCaseProgress buffer
    // against these four server types after the initial complete parser.
    // Preserve that source-level quirk exactly.
    if (EqualsAsciiCaseInsensitive(showTestCaseProgress, "dev")) {
        g_L2ServerType = L2ServerType::DEV;
    } else if (EqualsAsciiCaseInsensitive(showTestCaseProgress, "test")) {
        g_L2ServerType = L2ServerType::TEST;
    } else if (EqualsAsciiCaseInsensitive(showTestCaseProgress, "movie")) {
        g_L2ServerType = L2ServerType::MOVIE;
    } else if (EqualsAsciiCaseInsensitive(showTestCaseProgress, "broadcast")) {
        g_L2ServerType = L2ServerType::BROADCAST;
    }

    g_bTraceNpcAI = ReadBool(*configuration, "Setting", "TraceNpcAI", "false");
    g_bTraceGlobalObjectAI = ReadBool(*configuration, "Setting", "TraceGlobalObjectAI", "false");
    g_bEnableDirectCommand = ReadBool(*configuration, "Setting", "direct_command", "false");
    g_bSetReportNpcEventCount = ReadBool(*configuration, "Setting", "ReportNPCEventCount", "false");
    g_bAddTimerCheck = ReadBool(*configuration, "Setting", "AddTimerCheck", "false");
    g_nAddTimerCheckCount = configuration->GetInt("Setting", "AddTimerCheckCount", 10000);
    g_TimerOverflowLimit = configuration->GetInt("Debug", "TimerOverflowLimit", 0);
    g_TimerOverflowCheckTick = configuration->GetInt("Debug", "TimerOverflowCheckTick", 90);
    g_bExceptionMakeMiniDump = true;

    int rewardPenalty = configuration->GetInt("Setting", "F2PQuestRewardPenaltyPer", 100);
    if (rewardPenalty < 1) {
        rewardPenalty = 1;
    }
    g_dF2PQuestRewardPenaltyPer = static_cast<double>(rewardPenalty) / 100.0;
    const std::u16string penaltyQuests =
        GetPrivateProfileString(*configuration, "Setting", "F2PRewardPenaltyQuests", "", 0x3ff);
    std::wstring penaltyQuestBuffer(penaltyQuests.begin(), penaltyQuests.end());
    MakePenaltyRewardQuestList(penaltyQuestBuffer.data());

    l2npc::LOTTO_TICKET_PRICE = configuration->GetInt("Setting", "LottoTicketPrice", 2000);
    l2npc::LOTTO_MINIMUM_WINNER_PRICE = configuration->GetInt("Setting", "LottoMinimumWinnerPrice", -1);
    g_nCountryCode = configuration->GetInt("Setting", "Country", 0);
    g_nRaidServerID = configuration->GetInt("Setting", "RaidServerId", -1);
}

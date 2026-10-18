#pragma once

#include "Shared/src/Config.h"
#include "Shared/src/Runtime/UserInterface.h"

#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
struct HWND__;
struct HINSTANCE__;

// Cached.cpp, current 0x004547e0. This dialog procedure is a real Win32
// control surface and therefore has no synthetic POSIX counterpart.
std::int64_t QsCheckTimeBox(HWND__* window, std::uint32_t message, std::uint64_t wParam, std::int64_t lParam);

#endif

enum class CacheDServerType : int {
    Dev = 0,
    Live = 1,
    Test = 2,
    Movie = 3,
    Broadcast = 4,
    WorldServer = 5,
};

// Semantic mirror of the globals populated by current InitConfig at
// Cached.exe 0x004a1478. This is a portable carrier, not a claim that the
// target placed these values in one C++ object.
struct CacheDConfiguration {
    runtime::UserInterface userInterface = runtime::UserInterface::Console;
    int country = 0;
    std::string logDAddress = "127.0.0.1";
    std::uint16_t logDPort = 3999;
    int logFileDurationMinutes = 30;
    CacheDServerType serverType = CacheDServerType::Live;
    std::string mailTo;
    // Portable safety switch. The shipping call sites remain intact, but
    // SMTP transport is enabled only when this opt-in and explicit endpoint
    // configuration are both present.
    bool systemMailNetworkEnabled = false;
    // Portable-only startup policy. The shipping executable always executes
    // the SerialKey validator. False skips only that gate and leaves the
    // recovered validator/Windows registry transport available for opt-in
    // compatibility runs.
    bool serialKeyValidationEnabled = false;

    std::uint64_t defaultPriceLimit = 2000000;
    std::uint64_t defaultPriceCheck = 1000000;
    int priceChangeRatioLimit = 30;
    std::uint64_t adenaLimit = 2000000;
    int adenaCheck = 1000000;
    int adenaChangeRatioLimit = 30;
    std::uint64_t adenaLogLimit = 500000000;

    int worldId = 1;
    int sqlExecLimitMilliseconds = 5000;
    bool itemClearOnStart = false;
    bool ghostDataClearOnStart = false;
    bool saveQsData = false;
    int loadedWorldDBTimeoutSeconds = 60;
    std::uint32_t waitTimeoutCheckMilliseconds = 0;
    bool deleteAccount = false;

    int timeLimit = 0;
    bool writeCache = false;
    std::string binaryUpdateVersion = "Please insert binary_update_version in setting section!";
    std::vector<int> lateSavingItems;
    bool lateSaving = false;
    int lateSavingAmount = 100;
    bool serverStopperEnabled = false;
    int serverStopperKey = 0;
    std::vector<std::string> adminAddresses;
    double pledgeNameValueRate = 1.0;

    bool gipEnabled = false;
    std::string gipAddress = "127.0.0.1";
    std::uint16_t gipPort = 3999;
    std::uint32_t gipHeartbeatMilliseconds = 10000;

    bool qsCheckEnabled = false;
    int qsCheckResetTime = 55;
    bool showTestCaseProgress = false;
    bool directCommand = false;
    std::string myOdbcSetting;
    int waitingDateForDelete = 7;
    int waitingMinuteForDelete = 60;
    bool useServerStartMail = true;
    bool moveCommission = false;
    int blockPenaltyCount = 0;
    int blockPenaltyLevel = 0;
    int postLimitLevel = 0;
    int trainingTimeInit = 23400;
    int trainingTimeLimit = 300;
    bool minidump = true;
    int minidumpType = 1;

    bool useDBSecurity = false;
    int dbSecurityPenaltyLevel = 0;
    std::string dbSecurityKey = "123456789!";
    bool myOdbcEnabled = false;
    std::string myOdbcWorldDatabase;

    int activateCharacterAmount = 7;
    int deactivateMinLevel = 200;
    int dailyPostCountForFreeUser = -1;
    bool dualBillingSystem = false;
    bool serverToCacheDPacketStatMonitor = false;
    bool cacheDToServerPacketStatMonitor = false;
};

struct CacheDEntryConfiguration {
    bool scriptSecureEnabled = true;
    std::string mailServer = "mail.ncsoft.co.kr";
    std::string developmentMailServer = "dev-mail";
};

// Original current signature and owner: void __cdecl InitConfig(void),
// Cached.cpp, 0x004a1478. It reads .\l2cache.ini from the working directory.
void InitConfig();

// The entry body at 0x0045a794 reads these three values after InitConfig.
void InitEntryConfiguration();

bool IsCacheDConfigurationLoaded();
CacheDConfiguration& GetCacheDConfiguration();

// Current Cached.exe runtime controls at DAT_0083d2d6/DAT_0083d2d8. The
// original data identifiers are absent from the surviving PDB. InitConfig
// seeds them and RequestDBSecuritySetting mutates the same process globals.
extern bool g_bUseDBSecurity;
extern int g_nDBSecurityPenaltyLevel;

// Current process globals consumed by Shared/QsCheck.cpp. The surviving PDB
// does not keep their identifiers. These semantic names are tied to the
// exact storage at DAT_0083d2ac, DAT_0083d2de and DAT_0083d2df.
extern std::uint32_t g_bSaveQsData;
extern bool g_bServerToCacheDPacketStatMonitor;
extern bool g_bCacheDToServerPacketStatMonitor;

// Current Cached.exe DAT_00826a98. RequestMonrace writes this process-wide
// percentage and CWareHouse::ManipulateItems reads it when creating a race
// ticket. The original data identifier is absent from the surviving PDB.
extern int g_MonRaceTaxRate;
const CacheDEntryConfiguration& GetCacheDEntryConfiguration();

// Current Cached.cpp 0x00454d10. The exact guard name survives even though
// the damaged PDB does not keep a public linker record for this function.
void ReportClassWorkRefCount();

// Current Cached.cpp 0x00454b88. The current CacheD image has no kept
// name. The identical source-level owner is named RenameOldLog by the
// matching L2NPC PDB.
void RenameOldLog();

int CacheDMain(int argc, char** argv);

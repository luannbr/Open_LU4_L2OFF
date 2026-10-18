#include "Shared/src/Log.h"
#include "Shared/src/PerfMon.h"
#include "Shared/src/Runtime/ConfigDirectory.h"
#include "Shared/src/Runtime/LogStorage.h"

#include "AccountClient/AccountServerStub.h"
#include "AccountJobFinishSignal.h"
#include "Agit.h"
#include "AgitDecoDb.h"
#include "AirShip.h"
#include "AlchemySystem.h"
#include "Alliance.h"
#include "AnnounceTimer.h"
#include "Area.h"
#include "Artifact.h"
#include "Auction.h"
#include "Auth.h"
#include "BRShopServer.h"
#include "BalthusEvent.h"
#include "BlockUpset.h"
#include "BotReportAgent.h"
#include "BuilderCmdHandler.h"
#include "CConfig.h"
#include "CGeneralObject.h"
#include "CWorld.h"
#include "CacheContextManager.h"
#include "Captcha.h"
#include "CardCollectSystem.h"
#include "CardUpDownGame.h"
#include "Castle.h"
#include "Cleft.h"
#include "CommCli.h"
#include "ConnectThreadManager.h"
#include "Creature.h"
#include "Cubic.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "DBPacketEx.h"
#include "DBSocket.h"
#include "DecoNpcData.h"
#include "Default.h"
#include "Door.h"
#include "DynamicQuest.h"
#include "EnchantOption.h"
#include "EnsoulData.h"
#include "EventCampaign.h"
#include "EventMaker.h"
#include "EventMatch.h"
#include "ExDrop.h"
#include "External/GameGuardServerLibCompat.h"
#include "External/NCGuardServerLibCompat.h"
#include "FactionManager.h"
#include "FieldCycle.h"
#include "FieldCycleMissionData.h"
#include "Fishing.h"
#include "FlyMove.h"
#include "FontCheck.h"
#include "Fortress.h"
#include "GameEventSystem.h"
#include "GarbageCollector.h"
#include "GoodsInventory.h"
#include "HTMLCache.h"
#include "Henna.h"
#include "IOCPMessageServer.h"
#include "IPBlock.h"
#include "InstantAgit.h"
#include "InstantZone.h"
#include "Inventory.h"
#include "IssueManager.h"
#include "Item.h"
#include "ItemAnnounceInfo.h"
#include "ItemAttrChange.h"
#include "L2StatusWnd.h"
#include "L2Time.h"
#include "LuckyGameSystem.h"
#include "MainEvent.h"
#include "Manor.h"
#include "MarketPriceList.h"
#include "MatchingPointDB.h"
#include "MonRaceData.h"
#include "MonsterBook.h"
#include "MultiSell.h"
#include "NCGuard.h"
#include "NPC.h"
#include "NPConst.h"
#include "NPSystem.h"
#include "NewAction.h"
#include "NewInspector.h"
#include "NoblessSystem.h"
#include "NpcServer.h"
#include "NpcSocket.h"
#include "NpcSocketEx.h"
#include "ObjectDB.h"
#include "OlympiadNetwork.h"
#include "OlympiadService.h"
#include "OlympiadServiceProxy.h"
#include "OlympiadStuff.h"
#include "OptionDB.h"
#include "PCParam.h"
#include "Party.h"
#include "PartyRoomInfo.h"
#include "Path.h"
#include "PathToAwakeningNotice.h"
#include "Pet.h"
#include "PetDataSheet.h"
#include "PetitionCli.h"
#include "Pledge.h"
#include "PledgeContribution.h"
#include "PledgeContributionInfo.h"
#include "PledgeFunctionInfo.h"
#include "PledgeGameSystem.h"
#include "PledgeItemInfo.h"
#include "PledgeJoinSystem.h"
#include "PledgeLevelInfo.h"
#include "PledgeMasteryInfo.h"
#include "PledgeMissionInfo.h"
#include "PledgeRaid.h"
#include "PledgeSkillInfo.h"
#include "PotentialSkill.h"
#include "PredefinedListDB.h"
#include "PremiumService.h"
#include "Quest.h"
#include "QuestManager.h"
#include "Raid.h"
#include "Recipe.h"
#include "ReservationAnnounceTimer.h"
#include "ReservationNPCSpawnTimer.h"
#include "Residence.h"
#include "RestrictArea.h"
#include "Server.h"
#include "Setting.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "SharedContainer.h"
#include "SharedFactories.h"
#include "Shuttle.h"
#include "Skill.h"
#include "SkillAcquire.h"
#include "SkillEnchant.h"
#include "StartupLoaderGate.h"
#include "StaticObject.h"
#include "ThreadLocalWorldMap.h"
#include "Transform.h"
#include "User.h"
#include "UserBasicAction.h"
#include "UserPacketEx.h"
#include "UserSocket.h"
#include "VariationDB.h"
#include "Vehicle.h"
#include "WorldCollision.h"

#include "Shared/src/AtomicJob.h"
#include "Shared/src/CategoryDataContent.h"
#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GIP.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOC.h"
#include "Shared/src/L2Time.h"
#include "Shared/src/LocalizationSystem.h"
#include "Shared/src/ManorData.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/PledgeRanking.h"
#include "Shared/src/Runtime/OwnedThread.h"
#include "Shared/src/Runtime/ProcessServices.h"
#include "Shared/src/Runtime/SerialKey.h"
#include "Shared/src/Runtime/SocketTransport.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WaitThreadRuntime.h"
#include "Shared/src/Protocol/VariableBitSet.h"
#include "Shared/src/QueryPacket.h"
#include "Shared/src/ResidenceData.h"
#include "Shared/src/ShareDefine.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "Shared/src/WatchList.h"
#include "Portable/Lu4Protocol.h"

#include "platform/Runtime.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

// Current RTTI/vtable prove this complete 0x10 L2Server.obj owner. Its sole
// virtual slot is CIOThreadCallback::IOThreadNextSyncTimeCallback.
class CIOThreadCallback final : public IOThreadCallback {
public:
    explicit CIOThreadCallback(int threadId) noexcept : IOThreadCallback(threadId) {}

    void IOThreadNextSyncTimeCallback() override;
};


class CReportTimer final : public CIOObject {
public:
    CReportTimer() = default;
    ~CReportTimer() override = default;

    void TimerExpired(int id) override;
};


CReportTimer g_ReportTimer;

// --check-scripts: load every script file, log the result and exit before
// any connection or listener. It runs beside a live server, so it skips the
// single-instance markers and requires its own L2_SHARED_MEMORY_NAMESPACE.
bool g_checkScriptsOnly = false;

void RenameOldLog();
void ReportRefCount();
void TestPacketTableRegistration();
void ListenThread_common();
void ListenThread_mailing();
std::uint32_t ListenThread(void* argument);

void CReportTimer::TimerExpired(int) {
    // Current L2Server 0x007f63f4.
    ReportMemory();
    ReportInstance();
    AddTimer(3600000, 0);
}

// Noise: The portable production tree declares this table but has no writer
// or escaping address. Its zero-filled rows are not observed reference counts.
// Retain the complete former report, including its original string formats,
// and revisit it if work-type instrumentation is introduced.
// void ReportRefCount() {
//     // Current L2Server.cpp 0x007f8f5c. The 512 x 1024-byte carrier,
//     // one-based carrier row and zero-based class row selection are exact.
//     // Bounded incremental formatting keeps the output text without
//     // relying on the target's self-overlapping vsprintf_s call.
//     L2_GUARD_FUNCTION()
//
//     if (runtime::AddInt32Wrapping(
//             g_nClassCount,
//             1) >= 0x201) {
//         return.
//     }
//
//     // Log.Add(
//     //     LOG_NORMAL,
//     //     L"============  RefCount Report START ============")
//     L2_LOG_INFO(Log,
//         L"============  RefCount Report START ============")
//
//     using ReportLine = std::array<char, 0x400>
//     std::array<ReportLine, 0x200> reportLines{}
//     const auto appendValue = [](
//                                  ReportLine& line,
//                                  std::int32_t value) {
//         const std::size_t used =
//             std::strlen(line.data())
//         if (used >= line.size()) {
//             return.
//         }
//         std::snprintf(
//             line.data() + used,
//             line.size() - used,
//             "%15ld",
//             static_cast<long>(value))
//     }
//
//     for (std::int32_t workType = 0
//          workType <
//              static_cast<std::int32_t>(
//                  kMemoryWorkTypeCount)
//          ++workType) {
//         appendValue(reportLines[0], workType)
//     }
//     // Log.Add(
//     //     LOG_NORMAL,
//     //     L"[%30s]\t[%d]\t%s",
//     //     "",
//     //     0,
//     //     reportLines[0].data())
//     L2_LOG_INFO(Log,
//         L"[%30s]\t[%d]\t%s",
//         "",
//         0,
//         reportLines[0].data())
//
//     for (std::int32_t classIndex = 0
//          classIndex < g_nClassCount.
//          ++classIndex) {
//         ReportLine& line = reportLines[
//             static_cast<std::size_t>(classIndex + 1)]
//         for (std::size_t workType = 0
//              workType < kMemoryWorkTypeCount.
//              ++workType) {
//             appendValue(
//                 line,
//                 g_pClassWorkRefCount[
//                     static_cast<std::size_t>(classIndex)]
//                     [workType])
//         }
//         // Log.Add(
//         //     LOG_NORMAL,
//         //     L"[%30s]\t[%d]\t%s",
//         //     g_pClassRefName[
//         //         static_cast<std::size_t>(classIndex)],
//         //     classIndex,
//         //     line.data())
//         L2_LOG_INFO(Log,
//             L"[%30s]\t[%d]\t%s",
//             g_pClassRefName[
//                 static_cast<std::size_t>(classIndex)],
//             classIndex,
//             line.data())
//     }
//
//     // Log.Add(
//     //     LOG_NORMAL,
//     //     L"============  RefCount Report END ============")
//     L2_LOG_INFO(Log,
//         L"============  RefCount Report END ============")
// }
void ReportRefCount() {
    L2_GUARD_FUNCTION();
    L2_LOG_INFO(Log, "[memory] Reference counts by work type are not collected by this build.");
}

namespace {

bool EqualsProfileTrue(std::string_view value) {
    if (value.size() != 4) {
        return false;
    }
    constexpr std::string_view expected = "true";
    for (std::size_t index = 0; index < value.size(); ++index) {
        char character = value[index];
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
        if (character != expected[index]) {
            return false;
        }
    }
    return true;
}

bool ReadScriptSecureEnabled() {
#if defined(_WIN32)
    std::array<wchar_t, 0x400> temporary{};
    ::GetPrivateProfileStringW(
        L"Setting", L"ScriptSecureEnabled", L"true", temporary.data(), static_cast<DWORD>(temporary.size()),
        runtime::ConfigFile("./secure.ini").wstring().c_str()
    );
    return ::_wcsicmp(temporary.data(), L"true") == 0;
#else
    runtime::Config profile;
    static_cast<void>(profile.Load(runtime::ConfigFile("./secure.ini").string(), false));
    return EqualsProfileTrue(profile.Get("Setting", "ScriptSecureEnabled", "true"));
#endif
}

// Portable extension, not a shipping option: the shipping server always checks
// its host-bound serial key. This is the same Portable/EnableSerialKeyValidation
// option as CacheD. The default skips the check, so a deployment without the
// licence service starts. Read once in main before the first check.
bool g_serialKeyValidationEnabled = false;

bool ReadSerialKeyValidationEnabled() {
    runtime::Config profile;
    static_cast<void>(profile.Load(runtime::ConfigFile("./l2server.ini").string(), false));
    return EqualsProfileTrue(profile.Get("Portable", "EnableSerialKeyValidation", "false"));
}

bool HasL2ServerSerialKey() {
#if defined(_WIN32)
    std::array<wchar_t, 0x400> serialKey{};
    return LoadStrFromReg(serialKey.data(), L"SerialKey", L"Software\\PROJECT_L2\\L2SERVER");
#else
    // POSIX has no registry. Current 0x0080a55a checks only successful
    // REG_BINARY retrieval, never the contents, so the portable transport
    // requires only a readable local presence marker.
    std::ifstream serialKey(runtime::ConfigFile("./SerialKey.txt"), std::ios::binary);
    return serialKey.is_open();
#endif
}

std::string ReadL2ServerSerialKey() {
#if defined(_WIN32)
    std::array<wchar_t, 0x400> serialKey{};
    if (!LoadStrFromReg(serialKey.data(), L"SerialKey", L"Software\\PROJECT_L2\\L2SERVER")) {
        return {};
    }
    return WideToUtf8(serialKey.data());
#else
    std::ifstream input(runtime::ConfigFile("./SerialKey.txt"), std::ios::binary);
    if (!input) {
        return {};
    }
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.size() >= 2U && bytes[0] == 0xffU && bytes[1] == 0xfeU) {
        std::string result;
        result.reserve(bytes.size() / 2U);
        for (std::size_t offset = 2U; offset + 1U < bytes.size(); offset += 2U) {
            const std::uint16_t value =
                static_cast<std::uint16_t>(bytes[offset]) | static_cast<std::uint16_t>(bytes[offset + 1U] << 8U);
            if (value == 0U) {
                break;
            }
            if (value < 0x80U) {
                result.push_back(static_cast<char>(value));
            }
        }
        return result;
    }
    const std::size_t start = bytes.size() >= 3U && bytes[0] == 0xefU && bytes[1] == 0xbbU && bytes[2] == 0xbfU ? 3U : 0U;
    return std::string(reinterpret_cast<const char*>(bytes.data() + start), bytes.size() - start);
#endif
}

[[noreturn]] void ExitForInvalidSerialKey() {
    // Log.Add(
    //     LOG_ERROR,
    //     L"Serial Key is Invalid or Expired")
    // L2_LOG_ERROR(Log,
    //     L"Serial Key is Invalid or Expired")
    L2_LOG_ERROR(Log, u"[startup] Serial key validation failed; the process will terminate.");
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
#if defined(_WIN32)
    ::ExitProcess(static_cast<UINT>(-1));
#else
    std::_Exit(-1);
#endif
}

// Noise: Combine the generic failure and its separate detail into one record.
// void LogGameGuardInitializationFailure(const std::uint32_t result) {
//     // Log.Add(
//     //     LOG_ERROR,
//     //     L"Fail to Initialize GameGuardAuth Module!")
//     L2_LOG_ERROR(Log,
//         L"Fail to Initialize GameGuardAuth Module!")
//     switch (result) {
//     case 1:
//         // Log.Add(LOG_ERROR, L"GameGuard :: Fail to Load Memory!")
//         L2_LOG_ERROR(Log, L"GameGuard :: Fail to Load Memory!")
//         break.
//     case 2:
//         // Log.Add(LOG_ERROR, L"GameGuard :: Fail to Load ggauth.dll!")
//         L2_LOG_ERROR(Log, L"GameGuard :: Fail to Load ggauth.dll!")
//         break.
//     case 3:
//         // Log.Add(
//         //     LOG_ERROR,
//         //     L"GameGuard :: Fail to Export function of ggauth.dll!")
//         L2_LOG_ERROR(Log,
//             L"GameGuard :: Fail to Export function of ggauth.dll!")
//         break.
//     case 4:
//         // Log.Add(LOG_ERROR, L"GameGuard :: Wrong Sequence Export !")
//         L2_LOG_ERROR(Log, L"GameGuard :: Wrong Sequence Export !")
//         break.
//     case 5:
//         // Log.Add(LOG_ERROR, L"GameGuard :: Fail to Load csauth2.cfg")
//         L2_LOG_ERROR(Log, L"GameGuard :: Fail to Load csauth2.cfg")
//         break.
//     default:
//         break.
//     }
// }
void LogGameGuardInitializationFailure(const std::uint32_t result) {
    const wchar_t* reason = L"unrecognized failure";
    switch (result) {
        case 1:
            reason = L"memory allocation failed";
            break;
        case 2:
            reason = L"cannot load ggauth.dll";
            break;
        case 3:
            reason = L"cannot resolve ggauth.dll exports";
            break;
        case 4:
            reason = L"invalid export sequence";
            break;
        case 5:
            reason = L"cannot load csauth2.cfg";
            break;
        default:
            break;
    }
    L2_LOG_ERROR(
        Log, u"[GameGuard] Authentication initialization failed (code %u): %ls; startup continues.",
        static_cast<unsigned>(result), reason
    );
}

runtime::InstanceGateResult AcquireL2ServerInstanceGate(void*& nativeHandle, std::string& diagnostic) {
#if defined(_WIN32)
    // Current root tests only GetLastError and does not reject
    // a null handle when the error is anything other than ALREADY_EXISTS.
    nativeHandle = ::CreateMutexW(nullptr, TRUE, L"Global\\_L2SERVER_EXE_");
    return ::GetLastError() == ERROR_ALREADY_EXISTS ? runtime::InstanceGateResult::AlreadyRunning
                                                    : runtime::InstanceGateResult::Acquired;
#else
    return runtime::AcquireNamedInstanceGate(
        false, u"Global\\_L2SERVER_EXE_", "l2server.instance.lock", nativeHandle, diagnostic
    );
#endif
}

void RefreshDaylightSaving() {
#if defined(_WIN32)
    static_cast<void>(::_get_daylight(&g_nDaylightSaving));
#else
    ::tzset();
    g_nDaylightSaving = ::daylight;
#endif
}

#if !defined(_WIN32)
std::filesystem::path ExecutablePathForLogRollover() {
#if defined(__APPLE__)
    std::uint32_t length = 0;
    ::_NSGetExecutablePath(nullptr, &length);
    if (length != 0) {
        std::vector<char> path(length, '\0');
        if (::_NSGetExecutablePath(path.data(), &length) == 0) {
            std::error_code error;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(path.data(), error);
            return error ? std::filesystem::path(path.data()) : canonical;
        }
    }
#else
    std::error_code error;
    const std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error && !path.empty()) {
        return path;
    }
#endif
    return {};
}
#endif

class StartupLoaderMutexLease final {
public:
    ~StartupLoaderMutexLease() { CloseAll(false); }

    void ReleaseOwnedAndClose() { CloseAll(true); }

private:
    void CloseAll(bool releaseOwned) {
#if defined(_WIN32)
        for (std::size_t index = 0; index < handleCount_; ++index) {
            void* const handle = handles_[index];
            if (handle) {
                if (releaseOwned && owned_[index]) {
                    static_cast<void>(::ReleaseMutex(static_cast<HANDLE>(handle)));
                }
                ::CloseHandle(static_cast<HANDLE>(handle));
                handles_[index] = nullptr;
            }
        }
#else
        for (std::size_t index = 0; index < handleCount_; ++index) {
            const int descriptor = descriptors_[index];
            if (descriptor >= 0) {
                if (releaseOwned && owned_[index]) {
                    static_cast<void>(::flock(descriptor, LOCK_UN));
                }
                ::close(descriptor);
                descriptors_[index] = -1;
            }
        }
#endif
        handleCount_ = 0;
    }

public:
    StartupLoaderMutexLease(const StartupLoaderMutexLease&) = delete;
    StartupLoaderMutexLease& operator=(const StartupLoaderMutexLease&) = delete;

    StartupLoaderMutexLease() = default;

    bool Open(
        const wchar_t* windowsName,
        const char* portableName,
        std::uint32_t desiredAccess,
        std::uint32_t presentObservation,
        std::uint32_t& lastError,
        std::string& diagnostic
    ) {
#if defined(_WIN32)
        HANDLE const handle = ::OpenMutexW(static_cast<DWORD>(desiredAccess), TRUE, windowsName);
        lastError = ::GetLastError();
        Keep(handle, false);
        return true;
#else
        static_cast<void>(windowsName);
        static_cast<void>(desiredAccess);
        const std::string path = PortablePath(portableName);
        const int descriptor = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (descriptor < 0) {
            if (errno == ENOENT) {
                lastError = 2;
                return true;
            }
            if (errno == EACCES || errno == EPERM) {
                lastError = 5;
                return true;
            }
            diagnostic = "open(" + path + ") failed: " + std::strerror(errno);
            return false;
        }

        if (::flock(descriptor, LOCK_EX | LOCK_NB) == 0) {
            // A stale file has no Win32 named-object counterpart.
            static_cast<void>(::flock(descriptor, LOCK_UN));
            ::close(descriptor);
            lastError = 2;
            return true;
        }
        const int error = errno;
        if (error == EWOULDBLOCK || error == EAGAIN) {
            Keep(descriptor, false);
            lastError = presentObservation;
            return true;
        }
        ::close(descriptor);
        diagnostic = "flock(" + path + ") failed: " + std::strerror(error);
        return false;
#endif
    }

    bool Create(const wchar_t* windowsName, const char* portableName, std::uint32_t& lastError, std::string& diagnostic) {
#if defined(_WIN32)
        HANDLE const handle = ::CreateMutexW(nullptr, TRUE, windowsName);
        lastError = ::GetLastError();
        Keep(handle, true);
        return true;
#else
        static_cast<void>(windowsName);
        const std::string path = PortablePath(portableName);
        const int descriptor = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (descriptor < 0) {
            if (errno == EACCES || errno == EPERM) {
                // Current code advances for every CreateMutex error except
                // ERROR_ALREADY_EXISTS, even when no handle was returned.
                lastError = 5;
                return true;
            }
            diagnostic = "open(" + path + ") failed: " + std::strerror(errno);
            return false;
        }
        if (::flock(descriptor, LOCK_EX | LOCK_NB) == 0) {
            Keep(descriptor, true);
            lastError = 0;
            return true;
        }
        const int error = errno;
        ::close(descriptor);
        if (error == EWOULDBLOCK || error == EAGAIN) {
            lastError = 183;
            return true;
        }
        diagnostic = "flock(" + path + ") failed: " + std::strerror(error);
        return false;
#endif
    }

private:
#if defined(_WIN32)
    void Keep(HANDLE handle, bool owned) {
        if (handle && handleCount_ < handles_.size()) {
            handles_[handleCount_] = handle;
            owned_[handleCount_++] = owned;
        }
    }

    std::array<void*, 4> handles_{};
#else
    static std::string PortablePath(const char* name) {
        // This is the producer side of L2NPC's existing
        // Portable/ProcessStartupGate protocol.
        std::string path{"/tmp/l2npc_process_"};
        for (const char* cursor = name; cursor && *cursor; ++cursor) {
            const unsigned char value = static_cast<unsigned char>(*cursor);
            const bool safe = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9') || value == '_' || value == '-';
            path.push_back(safe ? static_cast<char>(value) : '_');
        }
        path.append(".lock");
        return path;
    }

    void Keep(int descriptor, bool owned) {
        if (descriptor >= 0 && handleCount_ < descriptors_.size()) {
            descriptors_[handleCount_] = descriptor;
            owned_[handleCount_++] = owned;
        }
    }

    std::array<int, 4> descriptors_{{-1, -1, -1, -1}};
#endif
    std::array<bool, 4> owned_{};
    std::size_t handleCount_ = 0;
};

bool ObserveStartupLoaderGate(
    bool useResourceLoader,
    StartupLoaderMutexLease& lease,
    l2server::StartupLoaderGateResult& result,
    std::string& diagnostic
) {
    l2server::StartupLoaderGateObservations observations;
    diagnostic.clear();
    if (!lease.Open(
            L"Global\\_L2MUTEX_LOADER_", "Global\\_L2MUTEX_LOADER_", 0, 0, observations.globalLoaderLastError, diagnostic
        )) {
        return false;
    }

    if (useResourceLoader) {
        if (observations.globalLoaderLastError == 2) {
            result = l2server::EvaluateStartupLoaderGate(true, observations);
            return true;
        }
        if (!lease.Open(
                L"_L2MUTEX_NOW_LOADING_", "_L2MUTEX_NOW_LOADING_", 0x001f0001, 5, observations.loadingMarkerLastError,
                diagnostic
            )) {
            return false;
        }
        if (observations.loadingMarkerLastError == 5) {
            result = l2server::EvaluateStartupLoaderGate(true, observations);
            return true;
        }
        if (!lease.Create(
                L"Global\\_L2MUTEX_USE_LOADER_", "Global\\_L2MUTEX_USE_LOADER_", observations.modeMarkerLastError, diagnostic
            )) {
            return false;
        }
    } else {
        if (observations.globalLoaderLastError != 2) {
            result = l2server::EvaluateStartupLoaderGate(false, observations);
            return true;
        }
        if (!lease.Create(
                L"_L2MUTEX_NOT_USE_LOADER_", "_L2MUTEX_NOT_USE_LOADER_", observations.modeMarkerLastError, diagnostic
            )) {
            return false;
        }
    }

    result = l2server::EvaluateStartupLoaderGate(useResourceLoader, observations);
    return true;
}

const char* StartupLoaderFailureMessage(l2server::StartupLoaderGateFailure failure) {
    using Failure = l2server::StartupLoaderGateFailure;
    switch (failure) {
        case Failure::LoaderMustBeTerminated:
            // return "You must terminate the loader first."
            return "[startup] Resource loader is running while UseResourceLoader=false; stop the loader before starting "
                   "this server.";
        case Failure::DuplicateStandaloneServer:
            // return "Duplicated Server?! (without loader)"
            return "[startup] The standalone-server marker already exists; startup stops.";
        case Failure::LoaderMustBeStarted:
            // return "You must start the loader first."
            return "[startup] UseResourceLoader=true requires the resource loader to be started first; startup stops.";
        case Failure::LoaderStillLoading:
            // return "You must wait for the end of loading data!!!"
            return "[startup] Resource loader is still loading data; wait for it to finish before starting this server.";
        case Failure::DuplicateLoaderServer:
            // return "Duplicated Server?! (with loader)"
            return "[startup] The server-with-loader marker already exists; startup stops.";
        case Failure::None:
            return "";
    }
    return "";
}

bool WaitForBackgroundThreadInterval(std::chrono::milliseconds interval) {
    const portable::WaitThreadRuntimeServices runtime = portable::GetWaitThreadRuntimeServices();
    if (runtime.waitForStop) {
        return runtime.waitForStop(interval);
    }
    std::this_thread::sleep_for(interval);
    return runtime.stopRequested && runtime.stopRequested();
}

void PerfMonThreadCommon() {
    for (;;) {
        l2npc::g_PerfMon.GetPerfMon();
        if (WaitForBackgroundThreadInterval(
                std::chrono::milliseconds(static_cast<std::uint32_t>(l2npc::g_PerfMon.m_nInterval))
            )) {
            return;
        }
    }
}

void PerfMonThreadMailing() {
    // Log.Add(LOG_NORMAL, L"Perfmon in mailing mode")
    // L2_LOG_INFO(Log, L"Perfmon in mailing mode")
    L2_LOG_INFO(Log, u"[performance] Starting the monitor with exception reporting enabled.");
    try {
        PerfMonThreadCommon();
    } catch (...) {
        runtime::process::HandleUnhandledSharedThreadException("L2Server.cpp", 0x17f, "Fri May 11 05:00:41 2018");
    }
}

std::uint32_t PerfMonThread(void*) {
    constexpr int threadId = ThreadID::PERFMON;
    CThreadManager::RegisterThisThread(threadId);
    InitializeCurrentIOBufferPool(0x80);
    if (ExceptionMailing()) {
        PerfMonThreadMailing();
    } else {
        PerfMonThreadCommon();
    }
    CThreadManager::SetThread(threadId, nullptr);
    return 0;
}

[[noreturn]] void TerminateListenerStartup(const wchar_t* message, bool sleepBeforeExit) {
    L2_LOG_ERROR(Log, u"[startup] Listener initialization failed: %ls; terminating the process.", message);
#if defined(_WIN32)
    ::MessageBoxW(nullptr, message, L"Error", 0);
#else
    // Log.Add(LOG_ERROR, L"%ls", message)
    // Noise: The main log above covers this failure on both platforms.
    // L2_LOG_ERROR(Log, L"%ls", message)
#endif
    if (sleepBeforeExit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
#if defined(_WIN32)
    ::ExitProcess(static_cast<UINT>(-1));
#else
    std::_Exit(-1);
#endif
}

[[maybe_unused]] void ReportSmartIdManagerMemory() {
    // Current L2Server 0x007f5688. Item and Alliance pass through a float
    // conversion before variadic promotion. The remaining extents are
    // converted directly to double.
    constexpr float kBytesToMegabytesFloat = 1.0F / (1024.0F * 1024.0F);
    constexpr double kBytesToMegabytesDouble = 1.0 / (1024.0 * 1024.0);

    // Log.Add(
    //     LOG_NORMAL,
    //     L"ItemSmartIdManager          [%05.1f]MB",
    //     static_cast<double>(
    //         static_cast<float>(sizeof(CItemSmartIdManager)) *
    //         kBytesToMegabytesFloat))
    // L2_LOG_INFO(Log,
    //     L"ItemSmartIdManager          [%05.1f]MB",
    //     static_cast<double>(
    //         static_cast<float>(sizeof(CItemSmartIdManager)) *
    //         kBytesToMegabytesFloat))
    L2_LOG_INFO(
        Log, u"[memory] Item ID manager object size: %.1f MiB.",
        static_cast<double>(static_cast<float>(sizeof(CItemSmartIdManager)) * kBytesToMegabytesFloat)
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"CreatureSmartIdManager      [%05.1f]MB",
    //     static_cast<double>(sizeof(CCreatureSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    // L2_LOG_INFO(Log,
    //     L"CreatureSmartIdManager      [%05.1f]MB",
    //     static_cast<double>(sizeof(CCreatureSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    L2_LOG_INFO(
        Log, u"[memory] Creature ID manager object size: %.1f MiB.",
        static_cast<double>(sizeof(CCreatureSmartIdManager)) * kBytesToMegabytesDouble
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"StaticObjectSmartIdManager  [%05.1f]MB",
    //     static_cast<double>(sizeof(CStaticObjectSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    // L2_LOG_INFO(Log,
    //     L"StaticObjectSmartIdManager  [%05.1f]MB",
    //     static_cast<double>(sizeof(CStaticObjectSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    L2_LOG_INFO(
        Log, u"[memory] Static object ID manager object size: %.1f MiB.",
        static_cast<double>(sizeof(CStaticObjectSmartIdManager)) * kBytesToMegabytesDouble
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"PledgeSmartIdManager        [%05.1f]MB",
    //     static_cast<double>(
    //         sizeof(CPledge::PledgeSmartIdManager)) *
    //         0.0009765625 * 0.0009765625)
    // L2_LOG_INFO(Log,
    //     L"PledgeSmartIdManager        [%05.1f]MB",
    //     static_cast<double>(
    //         sizeof(CPledge::PledgeSmartIdManager)) *
    //         0.0009765625 * 0.0009765625)
    L2_LOG_INFO(
        Log, u"[memory] Clan ID manager object size: %.1f MiB.",
        static_cast<double>(sizeof(CPledge::PledgeSmartIdManager)) * 0.0009765625 * 0.0009765625
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"PartySmartIdManager         [%05.1f]MB",
    //     static_cast<double>(sizeof(CPartySmartIdManager)) *
    //         kBytesToMegabytesDouble)
    // L2_LOG_INFO(Log,
    //     L"PartySmartIdManager         [%05.1f]MB",
    //     static_cast<double>(sizeof(CPartySmartIdManager)) *
    //         kBytesToMegabytesDouble)
    L2_LOG_INFO(
        Log, u"[memory] Party ID manager object size: %.1f MiB.",
        static_cast<double>(sizeof(CPartySmartIdManager)) * kBytesToMegabytesDouble
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AllianceSmartIdManager      [%05.1f]MB",
    //     static_cast<double>(
    //         static_cast<float>(sizeof(CAllianceSmartIdManager)) *
    //         kBytesToMegabytesFloat))
    // L2_LOG_INFO(Log,
    //     L"AllianceSmartIdManager      [%05.1f]MB",
    //     static_cast<double>(
    //         static_cast<float>(sizeof(CAllianceSmartIdManager)) *
    //         kBytesToMegabytesFloat))
    L2_LOG_INFO(
        Log, u"[memory] Alliance ID manager object size: %.1f MiB.",
        static_cast<double>(static_cast<float>(sizeof(CAllianceSmartIdManager)) * kBytesToMegabytesFloat)
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"GeneralObjectManager        [%05.1f]MB",
    //     static_cast<double>(sizeof(CGeneralObjectSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    // L2_LOG_INFO(Log,
    //     L"GeneralObjectManager        [%05.1f]MB",
    //     static_cast<double>(sizeof(CGeneralObjectSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    L2_LOG_INFO(
        Log, u"[memory] General object ID manager object size: %.1f MiB.",
        static_cast<double>(sizeof(CGeneralObjectSmartIdManager)) * kBytesToMegabytesDouble
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"VehicleSmartIdManager       [%05.1f]MB",
    //     static_cast<double>(sizeof(CVehicleSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    // L2_LOG_INFO(Log,
    //     L"VehicleSmartIdManager       [%05.1f]MB",
    //     static_cast<double>(sizeof(CVehicleSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    L2_LOG_INFO(
        Log, u"[memory] Vehicle ID manager object size: %.1f MiB.",
        static_cast<double>(sizeof(CVehicleSmartIdManager)) * kBytesToMegabytesDouble
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AirShipSmartIdManager       [%05.1f]MB",
    //     static_cast<double>(sizeof(CAirShipSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    // L2_LOG_INFO(Log,
    //     L"AirShipSmartIdManager       [%05.1f]MB",
    //     static_cast<double>(sizeof(CAirShipSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    L2_LOG_INFO(
        Log, u"[memory] Airship ID manager object size: %.1f MiB.",
        static_cast<double>(sizeof(CAirShipSmartIdManager)) * kBytesToMegabytesDouble
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"ShuttleSmartIdManager       [%05.1f]MB",
    //     static_cast<double>(sizeof(CShuttleSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    // L2_LOG_INFO(Log,
    //     L"ShuttleSmartIdManager       [%05.1f]MB",
    //     static_cast<double>(sizeof(CShuttleSmartIdManager)) *
    //         kBytesToMegabytesDouble)
    L2_LOG_INFO(
        Log, u"[memory] Shuttle ID manager object size: %.1f MiB.",
        static_cast<double>(sizeof(CShuttleSmartIdManager)) * kBytesToMegabytesDouble
    );
}

} // namespace

// Original rollover body kept. The logging overlay below observes errors
// without changing the path, rename policy, fallback or startup continuation.
// void RenameOldLog() {
//     // Current L2Server.cpp 0x007f8d80. The unused executable-stem local and
//     // unchecked rename result are intentional target behavior.
// #if defined(_WIN32)
//     std::array<wchar_t, 0x104> modulePath{}
//     if (::GetModuleFileNameW(
//             nullptr,
//             modulePath.data(),
//             static_cast<DWORD>(modulePath.size())) == 0) {
//         modulePath[0] = L'\0'.
//     }
//
//     wchar_t* fileName =
//         std::wcsrchr(modulePath.data(), L'\\')
//     fileName = fileName == nullptr
//         ? modulePath.data()
//         : fileName + 1.
//
//     std::array<wchar_t, 0x104> executableName{}
//     std::wcsncpy(
//         executableName.data(),
//         L"Unknown",
//         executableName.size() - 1)
//     ::lstrcpyW(executableName.data(), fileName)
//     if (wchar_t* extension =
//             std::wcsrchr(executableName.data(), L'.')) {
//         *extension = L'\0'
//     }
//
//     ::lstrcpyW(fileName, L"LinError.txt")
//     struct _stat32 status{}.
//     if (::_wstat32(modulePath.data(), &status) != 0) {
//         return.
//     }
//
//     std::tm* const localTime =
//         ::_localtime32(&status.st_mtime)
//     std::array<wchar_t, 0x104> backupPath{}
//     std::wcsncpy(
//         backupPath.data(),
//         L"Unknown",
//         backupPath.size() - 1)
//     ::swprintf_s(
//         backupPath.data(),
//         backupPath.size(),
//         L"%ls.%04d-%02d-%02d-%02d-%02d-%02d.bak",
//         modulePath.data(),
//         localTime->tm_year + 1900,
//         localTime->tm_mon + 1,
//         localTime->tm_mday,
//         localTime->tm_hour,
//         localTime->tm_min,
//         localTime->tm_sec)
//     ::_wrename(modulePath.data(), backupPath.data())
// #else
//     const std::filesystem::path executablePath =
//         ExecutablePathForLogRollover()
//     std::error_code error
//     const std::filesystem::path directory =
//         executablePath.empty()
//         ? std::filesystem::current_path(error)
//         : executablePath.parent_path()
//     if (error) {
//         return.
//     }
//
//     const std::filesystem::path logPath =
//         directory / "LinError.txt".
//     const std::filesystem::file_time_type writeTime =
//         std::filesystem::last_write_time(logPath, error)
//     if (error) {
//         return.
//     }
//
//     const auto systemWriteTime =
//         std::chrono::time_point_cast<
//             std::chrono::system_clock::duration>(
//             writeTime -
//             std::filesystem::file_time_type::clock::now() +
//             std::chrono::system_clock::now())
//     const std::int32_t timestamp =
//         static_cast<std::int32_t>(
//             std::chrono::system_clock::to_time_t(
//                 systemWriteTime))
//     std::tm* const localTime =
//         runtime::crt::LocalTime32(timestamp)
//
//     std::array<wchar_t, 0x104> backupPath{}
//     std::wcsncpy(
//         backupPath.data(),
//         L"Unknown",
//         backupPath.size() - 1)
//     const std::wstring logPathWide = logPath.wstring()
//     std::swprintf(
//         backupPath.data(),
//         backupPath.size(),
//         L"%ls.%04d-%02d-%02d-%02d-%02d-%02d.bak",
//         logPathWide.c_str(),
//         localTime->tm_year + 1900,
//         localTime->tm_mon + 1,
//         localTime->tm_mday,
//         localTime->tm_hour,
//         localTime->tm_min,
//         localTime->tm_sec)
//     error.clear()
//     std::filesystem::rename(
//         logPath,
//         std::filesystem::path(backupPath.data()),
//         error)
// #endif
// }
void RenameOldLog() {
    // Current L2Server.cpp 0x007f8d80. The unused executable-stem local and
    // unchecked rename result are intentional target behavior.
#if defined(_WIN32)
    std::array<wchar_t, 0x104> modulePath{};
    if (::GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size())) == 0) {
        {
            const runtime::logging::StorageErrorState preserveErrors;
            L2_LOG_WARN(
                Log,
                "[crash reports] Cannot determine the executable path (Windows error %lu); looking for LinError.txt in the "
                "current directory.",
                static_cast<unsigned long>(preserveErrors.savedWindowsError)
            );
        }
        modulePath[0] = L'\0';
    }

    wchar_t* fileName = std::wcsrchr(modulePath.data(), L'\\');
    fileName = fileName == nullptr ? modulePath.data() : fileName + 1;

    std::array<wchar_t, 0x104> executableName{};
    std::wcsncpy(executableName.data(), L"Unknown", executableName.size() - 1);
    ::lstrcpyW(executableName.data(), fileName);
    if (wchar_t* extension = std::wcsrchr(executableName.data(), L'.')) {
        *extension = L'\0';
    }

    ::lstrcpyW(fileName, L"LinError.txt");
    struct _stat32 status{};
    if (::_wstat32(modulePath.data(), &status) != 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        // Noise: No previous crash report is an expected startup state.
        if (preserveErrors.savedErrno != ENOENT) {
            L2_LOG_WARN(
                Log, u"[crash reports] Cannot read metadata for \"%ls\" (errno %d); backup skipped, startup continues.",
                modulePath.data(), preserveErrors.savedErrno
            );
        }
        return;
    }

    std::tm* const localTime = ::_localtime32(&status.st_mtime);
    std::array<wchar_t, 0x104> backupPath{};
    std::wcsncpy(backupPath.data(), L"Unknown", backupPath.size() - 1);
    const int backupFormatResult = ::swprintf_s(
        backupPath.data(), backupPath.size(), L"%ls.%04d-%02d-%02d-%02d-%02d-%02d.bak", modulePath.data(),
        localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour, localTime->tm_min,
        localTime->tm_sec
    );
    if (backupFormatResult < 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(
            Log,
            u"[crash reports] Cannot format the backup filename for \"%ls\" (errno %d); rename will still be attempted.",
            modulePath.data(), preserveErrors.savedErrno
        );
    }
    const int renameResult = ::_wrename(modulePath.data(), backupPath.data());
    if (renameResult != 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(
            Log,
            u"[crash reports] Cannot move \"%ls\" to \"%ls\" (errno %d); startup continues without completing this backup.",
            modulePath.data(), backupPath.data(), preserveErrors.savedErrno
        );
    }
#else
    const std::filesystem::path executablePath = ExecutablePathForLogRollover();
    if (executablePath.empty()) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(
            Log, "[crash reports] Executable path is unavailable; looking for LinError.txt in the current directory."
        );
    }
    std::error_code error;
    const std::filesystem::path directory =
        executablePath.empty() ? std::filesystem::current_path(error) : executablePath.parent_path();
    if (error) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(
            Log,
            "[crash reports] Cannot determine the current directory (%s error %d); LinError.txt backup skipped, startup "
            "continues.",
            error.category().name(), error.value()
        );
        return;
    }

    const std::filesystem::path logPath = directory / "LinError.txt";
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(logPath, error);
    if (error) {
        const runtime::logging::StorageErrorState preserveErrors;
        // Noise: No previous crash report is an expected startup state.
        if (error && error != std::errc::no_such_file_or_directory) {
            L2_LOG_WARN(
                Log,
                "[crash reports] Cannot read the modification time of \"%s\" (%s error %d); backup skipped, startup "
                "continues.",
                logPath.c_str(), error.category().name(), error.value()
            );
        }
        return;
    }

    const auto systemWriteTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        writeTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    const std::int32_t timestamp = static_cast<std::int32_t>(std::chrono::system_clock::to_time_t(systemWriteTime));
    std::tm* const localTime = runtime::crt::LocalTime32(timestamp);

    std::array<char16_t, 0x104> backupPath{};
    std::u16string_view(u"Unknown").copy(backupPath.data(), backupPath.size() - 1);
    const std::wstring logPathWide = logPath.wstring();
    const int backupFormatResult = FormatUtf16To(
        backupPath.data(), backupPath.size(), u"%ls.%04d-%02d-%02d-%02d-%02d-%02d.bak", logPathWide.c_str(),
        localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour, localTime->tm_min,
        localTime->tm_sec
    );
    if (backupFormatResult < 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(
            Log, "[crash reports] Cannot format the backup filename for \"%s\" (errno %d); rename will still be attempted.",
            logPath.c_str(), preserveErrors.savedErrno
        );
    }
    error.clear();
    std::filesystem::rename(logPath, std::filesystem::path(backupPath.data()), error);
    if (error) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(
            Log,
            "[crash reports] Cannot move \"%s\" to \"%s\" (%s error %d); startup continues without completing this backup.",
            logPath.c_str(), Utf16ToUtf8(backupPath.data()).c_str(), error.category().name(), error.value()
        );
    }
#endif
}

void TestPacketTableRegistration() {
    // Current L2Server.cpp 0x007faea4. Keep the eight registration scans and
    // the final DB-ex callback-address report in their exact target order.
    static_cast<void>(gPacketTableFromClient.TestRegistration());
    static_cast<void>(g_UserPacketEx.TestRegistration());
    static_cast<void>(gPacketTableFromCache.TestRegistration());
    static_cast<void>(g_DBPacketTableEx.TestRegistration());
    static_cast<void>(g_DxPacketTable.TestRegistration());
    static_cast<void>(g_QueryPacketTable.TestRegistration());
    static_cast<void>(gPacketTableFromNpc.TestRegistration());
    static_cast<void>(g_NPCSocketEx.TestRegistration());
    g_DBPacketTableEx.LogFuncAddrRange();
}

void CIOThreadCallback::IOThreadNextSyncTimeCallback() {
    // Current L2Server.exe 0x008007fc. The two guard scopes and the manager
    // order are literal: world-map publication precedes every smart-id/GC
    // pipe read, and the IO heartbeat is published between those phases.
    {
        GuardHelper worldSync(L"World_Sync", CThreadManager::GetThreadID());
        g_ptlCreatureMap->Read();
        g_ptlUserMap->Read();
        g_ptlItemMap->Read();
        g_ptlAirShipMap->Read();
        g_ptlStaticObjectMap->Read();
        g_IOSyncDeadlockChecker.UpdateCurrentThread();
    }
    {
        GuardHelper garbageCollect(L"garbage collect", CThreadManager::GetThreadID());
        ItemSmartIdManager()->GarbageCollect();
        g_CreatureSmartIdManager.GarbageCollect();
        g_StaticObjectSmartIdManager.GarbageCollect();
        CPledge::GarbageCollect();
        g_PartySmartIdManager.GarbageCollect();
        GeneralObjectGarbageCollect();
        g_AirShipSmartIdManager.GarbageCollect();
        CShuttle::s_ShuttleSmartIdManager.GarbageCollect();
        g_UserGC.Collect();
    }
}

void InitializeSharedVector() {
    // Current L2Server 0x007f88a0. Keep the mapping creation, Init calls and
    // diagnostics in their exact target order.
    L2_LOG_INFO(Log, "[startup] Initializing shared world state.");
    NpcSpawnInfo::Inst()->Init();
    CSharedAgitDecoInfo::Inst()->Init();
    SharedCleftInfo::Inst()->Init();
    SharedEventInfo::Inst()->Init();

    SharedNoblessInfoVector& noblessInfo = SharedNoblessInfo::Inst();
    // Log.AddT(
    //     LOG_NORMAL,
    //     L"[%s] Init",
    //     noblessInfo.GetName())
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"[%s] Init",
    //     noblessInfo.GetName())
    static_cast<void>(noblessInfo);

    SharedPlatformInfo::Inst()->Init();

    CManorData& manorData = CManorData::Inst(0);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"CManorData Init. [%d]",
    //     manorData.m_nManorId)
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"CManorData Init. [%d]",
    //     manorData.m_nManorId)
    static_cast<void>(manorData);

    CResidenceData& residenceData = CResidenceData::Inst(0);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"CResidenceData Init. [%x]",
    //     runtime::PointerLow32(&residenceData))
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"CResidenceData Init. [%x]",
    //     runtime::PointerLow32(&residenceData))
    static_cast<void>(residenceData);

    SharedFieldCycleInfo* const fieldCycleInfo = SharedFieldCycleInfo::GetInfo(0);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"SharedFieldCycleInfo Init. [%d]",
    //     fieldCycleInfo->cycleID)
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"SharedFieldCycleInfo Init. [%d]",
    //     fieldCycleInfo->cycleID)
    static_cast<void>(fieldCycleInfo);

    SharedSSQInfo* const ssqInfo = SharedSSQInfo::GetInfo();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"SharedSSQInfo Init. [%x]",
    //     runtime::PointerLow32(ssqInfo))
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"SharedSSQInfo Init. [%x]",
    //     runtime::PointerLow32(ssqInfo))
    static_cast<void>(ssqInfo);

    SharedTimeAttackInfo* const timeAttackInfo = SharedTimeAttackInfo::GetInfo(1, 0);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"SharedTimeAttackInfo Init. [%x]",
    //     runtime::PointerLow32(timeAttackInfo))
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"SharedTimeAttackInfo Init. [%x]",
    //     runtime::PointerLow32(timeAttackInfo))
    static_cast<void>(timeAttackInfo);

    SharedRimPointInfo* const rimPointInfo = SharedRimPointInfo::GetInfo();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"SharedRimPointInfo Init. [%x]",
    //     runtime::PointerLow32(rimPointInfo))
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"SharedRimPointInfo Init. [%x]",
    //     runtime::PointerLow32(rimPointInfo))
    static_cast<void>(rimPointInfo);

    SharedOlympiadInfo* const olympiadInfo = SharedOlympiadInfo::GetInfo();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"SharedOlympiadInfo Init. [%x]",
    //     runtime::PointerLow32(olympiadInfo))
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"SharedOlympiadInfo Init. [%x]",
    //     runtime::PointerLow32(olympiadInfo))
    static_cast<void>(olympiadInfo);

    SharedPledgeGameInfo* const pledgeGameInfo = SharedPledgeGameInfo::GetInfo();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"SharedPledgeGameInfo Init. [%x]",
    //     runtime::PointerLow32(pledgeGameInfo))
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"SharedPledgeGameInfo Init. [%x]",
    //     runtime::PointerLow32(pledgeGameInfo))
    static_cast<void>(pledgeGameInfo);

    SharedPlatformInfo* const platformInfo = SharedPlatformInfo::Inst();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"SharedPlatformInfo Init. [%x]",
    //     runtime::PointerLow32(platformInfo))
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"SharedPlatformInfo Init. [%x]",
    //     runtime::PointerLow32(platformInfo))
    static_cast<void>(platformInfo);

    SharedCastleInfo* const castleInfo = SharedCastleInfo::GetInfo(0);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"SharedCastleInfo Init. [%x]",
    //     runtime::PointerLow32(castleInfo))
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"SharedCastleInfo Init. [%x]",
    //     runtime::PointerLow32(castleInfo))
    static_cast<void>(castleInfo);

    SharedRaidServerData* const raidServerData = SharedRaidServerData::GetInfo();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"SharedRaidServerData Init. [%x]",
    //     runtime::PointerLow32(raidServerData))
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"SharedRaidServerData Init. [%x]",
    //     runtime::PointerLow32(raidServerData))
    static_cast<void>(raidServerData);

    SharedPledgeRaidInfo* const pledgeRaidInfo = SharedPledgeRaidInfo::GetInfo();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"SharedPledgeRaidInfo Init. [%x]",
    //     runtime::PointerLow32(pledgeRaidInfo))
    // Noise: Per-object addresses and initial IDs add no result beyond the shared-state stage marker.
    // L2_LOG_INFO(Log,
    //     L"SharedPledgeRaidInfo Init. [%x]",
    //     runtime::PointerLow32(pledgeRaidInfo))
    static_cast<void>(pledgeRaidInfo);
}

void InitializeSharedFactory() {
    // Current L2Server 0x00800394. The portability adapter owns the named
    // producer mappings while retaining the exact successful target order
    // and diagnostics.
    std::string error;
    if (!l2server::L2ServerSharedFactories::InitializeEager(&error)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "InitializeSharedFactory failed: %s",
        //     error.c_str())
        // L2_LOG_ERROR(Log,
        //     "InitializeSharedFactory failed: %s",
        //     error.c_str())
        L2_LOG_ERROR(Log, "[startup] Shared factory initialization failed; startup continues. Details: %s", error.c_str());
    }
}

void InitailizeThreadLocalMap() {
    // Current spelling and exact 0x008006d4 allocation/publication order.
    g_ptlCreatureMap = new CThreadLocalWorldMap2<120000, 600000, 16>;
    g_ptlCreatureMap->Initialize(L"Thread Local World Map [Creature    ]");

    g_ptlUserMap = new CUserThreadLocalWorldMap;
    g_ptlUserMap->Initialize(L"Thread Local World Map [User        ]");

    g_ptlItemMap = new CThreadLocalWorldMap2<900000, 112500, 16>;
    g_ptlItemMap->Initialize(L"Thread Local World Map [Item        ]");

    g_ptlAirShipMap = new CThreadLocalWorldMap2<1000, 3000, 16>;
    g_ptlAirShipMap->Initialize(L"Thread Local World Map [AirShip     ]");

    g_ptlStaticObjectMap = new CThreadLocalWorldMap2<10001, 20002, 16>;
    g_ptlStaticObjectMap->Initialize(L"Thread Local World Map [StaticObject]");
}

// Current L2Server.obj byte 0x015d566a starts enabled. WndProc command 0x803d
// toggles the byte at 0x008081f8..0x00808201. CNPC::SendNPCInfo reads it at
// 0x0088cb29.
bool g_isPacketOptimizeMode = true;

// Current L2Server.obj owns this 0x2058-byte process-lifetime object at
// 0x15c70ef0. Its compiler initializer registers the matching base teardown.
// the next object in this TU begins at 0x15c72f50 after alignment padding.
CL2StatusWnd g_L2StatusWnd;

// Current L2Server.obj owns the process-lifetime HTML cache manager at
// 0x015c72f50. Keep the storage in this TU instead of moving it into the
// Shared HTMLCache owner.
CHTMLCacheManager g_HTMLCache;

// Current L2Server.obj process state at 0x02fd4322. The damaged PDB does
// not keep the source identifier, but LoadData, WndProc and the two NCGuard
// packet terminals fix this one-byte lifetime and meaning.
bool g_NCGuardServerStarted = false;

// Current L2Server.obj stores these two LoadData spans at 0x15c70cf8 and
// 0x15c70cd0. Their source names do not survive. ListenThread_common reads
// both values later when publishing the target TimeCheck diagnostics.
std::uint32_t g_LoadDataBootstrapMilliseconds = 0;
std::uint32_t g_LoadDataPostLoadMilliseconds = 0;

// Current L2Server.obj listener spans 0x15c70cfc..0x15c70d04.
std::uint32_t g_ListenBeforeLoadDataMilliseconds = 0;
std::uint32_t g_ListenLoadDataMilliseconds = 0;
std::uint32_t g_ListenAfterLoadDataMilliseconds = 0;

// Current L2Server.obj 0x15c70cd4..0x15c70cf4. The pinned image has no
// writer for these retired OMP-stage counters. ListenThread_common still
// publishes their zero-filled process values in its diagnostic sequence.
std::uint32_t g_LoadDataOmpMilliseconds = 0;
std::uint32_t g_LoadGeoDataSeconds = 0;
std::uint32_t g_LoadPathNodeSeconds = 0;
std::uint32_t g_LoadSkillOmpSeconds = 0;
std::uint32_t g_LoadSecondOmpSeconds = 0;
std::uint32_t g_LoadThirdOmpSeconds = 0;
std::uint32_t g_LoadClanTableSeconds = 0;
std::uint32_t g_LoadFourthOmpSeconds = 0;
std::uint32_t g_LoadFifthSeconds = 0;

void LoadArmorEnchantBonus();
void LoadWeaponEnchantExBonus();

void LoadData() {
    // Current L2Server 0x00804ce4. Every published loader below is already
    // resolved-locked in LOAD_DATA_STATUS.tsv. LoadBuilderCmdHandler at
    // 0x0057141c publishes all 899 primary and six low-level records at this
    // exact position. No partial administrative registry remains.
    L2_GUARD_FUNCTION();

    const std::uint32_t loadStart = runtime::GetTickCount();
    if (g_serialKeyValidationEnabled) {
        const std::string serialKey = ReadL2ServerSerialKey();
        const runtime::SerialKeyHostIdentity identity = runtime::ReadSerialKeyHostIdentity();
        if (serialKey.empty() || !runtime::ValidateSerialKey(serialKey, identity)) {
            ExitForInvalidSerialKey();
        }
    }

    // Log.Add(LOG_NORMAL, L"Init GameGuardAuth Module")
    // L2_LOG_INFO(Log, L"Init GameGuardAuth Module")
    L2_LOG_INFO(Log, u"[startup] Initializing GameGuard authentication.");
    const std::uint32_t gameGuardResult = InitGameguardAuth("", 1000U, 0U, 0U);
    if (gameGuardResult != 0U) {
        LogGameGuardInitializationFailure(gameGuardResult);
    }

    CNCGuardManager::inst().Init();
    if (g_bInitedNCGuard) {
        // Log.Add(LOG_NORMAL, L"Init NCGuard Module")
        // L2_LOG_INFO(Log, L"Init NCGuard Module")
        L2_LOG_INFO(Log, u"[startup] Initializing NCGuard.");
        const NCGuardServerResult initializeResult =
            InitializeNCGuardServerW(L".\\NCGuard\\x64\\", L".\\NCGuard\\", NCGuardSendHandler, NCGuardEventHandler);
        if (initializeResult == 0U) {
            g_NCGuardServerStarted = true;
            const NCGuardServerResult startResult = StartNCGuardServer();
            if (startResult == 0U) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR]Start NCGuard Module Success")
                // L2_LOG_ERROR(Log,
                //     L"[NO_ERROR]Start NCGuard Module Success")
                L2_LOG_INFO(Log, u"[NCGuard] Module started.");
                // Current 0x00805115 emits the success text
                // when the bool return is false. Preserve that odd branch.
                // Logging overlay: keep the false branch and describe its actual failure.
                if (!CNCGuardManager::inst().LoadPenaltyPattern()) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[NO_ERROR]Load NCGuard Penalty Pattern Success")
                    // L2_LOG_ERROR(Log,
                    //     L"[NO_ERROR]Load NCGuard Penalty Pattern Success")
                    L2_LOG_ERROR(Log, u"[NCGuard] Penalty patterns could not be loaded; startup continues.");
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Start NCGuard Module fail[%d]",
                //     static_cast<std::uint32_t>(startResult))
                // L2_LOG_ERROR(Log,
                //     L"Start NCGuard Module fail[%d]",
                //     static_cast<std::uint32_t>(startResult))
                L2_LOG_ERROR(
                    Log, u"[NCGuard] Cannot start the module (code %u); startup continues.",
                    static_cast<unsigned>(startResult)
                );
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Init NCGuard Module fail[%d]",
            //     static_cast<std::uint32_t>(initializeResult))
            // L2_LOG_ERROR(Log,
            //     L"Init NCGuard Module fail[%d]",
            //     static_cast<std::uint32_t>(initializeResult))
            L2_LOG_ERROR(
                Log, u"[NCGuard] Cannot initialize the module (code %u); startup continues.",
                static_cast<unsigned>(initializeResult)
            );
        }
    }

    // Log.Add(LOG_NORMAL, L"Loading TelQuestPath")
    // L2_LOG_INFO(Log, L"Loading TelQuestPath")
    L2_LOG_INFO(Log, u"[startup] Loading teleport quest paths.");
    QuestManager::LoadTelQuestPath();
    // Log.Add(LOG_NORMAL, L"Loading PotentialSkill")
    // L2_LOG_INFO(Log, L"Loading PotentialSkill")
    L2_LOG_INFO(Log, u"[startup] Loading potential skills.");
    CPotentialSkillDB::Inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading MonRaceData")
    // L2_LOG_INFO(Log, L"Loading MonRaceData")
    L2_LOG_INFO(Log, u"[startup] Loading monster race data.");
    g_MonRaceDB.LoadMonRaceData();

    // Log.Add(LOG_NORMAL, L"Loading BuilderCmdHandler")
    // L2_LOG_INFO(Log, L"Loading BuilderCmdHandler")
    L2_LOG_INFO(Log, u"[startup] Loading builder command handlers.");
    LoadBuilderCmdHandler();

    // Log.Add(LOG_NORMAL, L"Loading HtmlCmdHandler")
    // L2_LOG_INFO(Log, L"Loading HtmlCmdHandler")
    L2_LOG_INFO(Log, u"[startup] Loading HTML command handlers.");
    CUserSocket::LoadHtmlCmdHandler();
    g_LoadDataBootstrapMilliseconds = runtime::GetTickCount() - loadStart;

    // Log.Add(LOG_NORMAL, L"Loading GeoData")
    // L2_LOG_INFO(Log, L"Loading GeoData")
    L2_LOG_INFO(Log, u"[startup] Loading geodata.");
    CWorld::Inst().GetGeo()->Load();
    // Log.Add(LOG_NORMAL, L"Loading PathNode")
    // L2_LOG_INFO(Log, L"Loading PathNode")
    L2_LOG_INFO(Log, u"[startup] Loading path nodes.");
    const std::uint32_t pathStart = runtime::GetTickCount();
    g_pnWorld.Load();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Complete to load PathNode [%ds]",
    //     (runtime::GetTickCount() - pathStart) / 1000U)
    // L2_LOG_INFO(Log,
    //     L"Complete to load PathNode [%ds]",
    //     (runtime::GetTickCount() - pathStart) / 1000U)
    L2_LOG_INFO(Log, u"[startup] Path-node loading returned after %u s.", (runtime::GetTickCount() - pathStart) / 1000U);

    // Log.Add(LOG_NORMAL, L"Loading category data")
    // L2_LOG_INFO(Log, L"Loading category data")
    L2_LOG_INFO(Log, u"[startup] Loading category data.");
    CCategoryDataDB::Inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading PredefinedListData")
    // L2_LOG_INFO(Log, L"Loading PredefinedListData")
    L2_LOG_INFO(Log, u"[startup] Loading predefined lists.");
    PredefinedListDB::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading SkillData")
    // L2_LOG_INFO(Log, L"Loading SkillData")
    L2_LOG_INFO(Log, u"[startup] Loading skill definitions.");
    g_SkillDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading SkillSynergyData")
    // L2_LOG_INFO(Log, L"Loading SkillSynergyData")
    L2_LOG_INFO(Log, u"[startup] Loading skill synergy data.");
    g_SkillDB.LoadSkillSynergy();
    // Log.Add(LOG_NORMAL, L"Loading items")
    // L2_LOG_INFO(Log, L"Loading items")
    L2_LOG_INFO(Log, u"[startup] Loading item definitions.");
    g_ObjectDB.LoadItem(false);
    // Log.Add(LOG_NORMAL, L"Verify PredefinedListData")
    // L2_LOG_INFO(Log, L"Verify PredefinedListData")
    L2_LOG_INFO(Log, u"[startup] Verifying predefined lists.");
    PredefinedListDB::inst().VerifyPredefinedListDB();
    // Log.Add(LOG_NORMAL, L"Loading SetItems")
    // L2_LOG_INFO(Log, L"Loading SetItems")
    L2_LOG_INFO(Log, u"[startup] Loading item sets.");
    g_ObjectDB.LoadSetItem(false);
    // Log.Add(LOG_NORMAL, L"Loading ShapeShiftedSetItems")
    // L2_LOG_INFO(Log, L"Loading ShapeShiftedSetItems")
    L2_LOG_INFO(Log, u"[startup] Loading shape-shifted item sets.");
    g_ObjectDB.LoadShapeShiftedSetItem(false);
    // Log.Add(LOG_NORMAL, L"Loading UseCountItem")
    // L2_LOG_INFO(Log, L"Loading UseCountItem")
    L2_LOG_INFO(Log, u"[startup] Loading item use limits.");
    g_ObjectDB.LoadUseCountItem(false);
    // Log.Add(LOG_NORMAL, L"Loading CombinationItems")
    // L2_LOG_INFO(Log, L"Loading CombinationItems")
    L2_LOG_INFO(Log, u"[startup] Loading item combinations.");
    g_ObjectDB.LoadCombinationItem(false);
    g_ObjectDB.LoadUpgradeSystem(false);
    // Log.Add(LOG_NORMAL, L"Loading SoulShotEffects")
    // L2_LOG_INFO(Log, L"Loading SoulShotEffects")
    L2_LOG_INFO(Log, u"[startup] Loading soulshot effects.");
    g_ObjectDB.LoadSoulShotEffect(false);
    // Log.Add(LOG_NORMAL, L"Loading ExDropData")
    // L2_LOG_INFO(Log, L"Loading ExDropData")
    L2_LOG_INFO(Log, u"[startup] Loading extra drop data.");
    ExDropItemSystem::inst().LoadExDropData();
    CCardCollectSystem::inst().LoadCardCollectSystemData();
    g_AlchemyDB.LoadAlchemyData();

    // Log.Add(LOG_NORMAL, L"Loading Exp")
    // L2_LOG_INFO(Log, L"Loading Exp")
    L2_LOG_INFO(Log, u"[startup] Loading experience tables.");
    CSetting::inst().LoadExpData();
    // Log.Add(LOG_NORMAL, L"Loading Beauty")
    // L2_LOG_INFO(Log, L"Loading Beauty")
    L2_LOG_INFO(Log, u"[startup] Loading appearance data.");
    CSetting::inst().LoadBeautyData();
    // Log.Add(LOG_NORMAL, L"Loading Setting")
    // L2_LOG_INFO(Log, L"Loading Setting")
    L2_LOG_INFO(Log, u"[startup] Loading game settings.");
    CSetting::inst().LoadSetting();
    // Log.Add(LOG_NORMAL, L"Loading MiniGame")
    // L2_LOG_INFO(Log, L"Loading MiniGame")
    L2_LOG_INFO(Log, u"[startup] Loading minigame data.");
    CSetting::inst().LoadMinigame();

    // Log.Add(LOG_NORMAL, L"Loading PetData")
    // L2_LOG_INFO(Log, L"Loading PetData")
    L2_LOG_INFO(Log, u"[startup] Loading pet data.");
    CPet::InitPet();
    PetDataSheetTable& petData = PetDataSheetTable::Inst();
    constexpr const wchar_t* kPetDataPath = L"..\\Script\\petdata.txt";
    if (!petData.Load(kPetDataPath)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Can't load %s",
        //     L"L2Server.cpp",
        //     0x700,
        //     kPetDataPath)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Can't load %s",
        //     L"L2Server.cpp",
        //     0x700,
        //     kPetDataPath)
        L2_LOG_ERROR(
            Log, "[startup] Cannot load petdata.txt from the configured script directory; initialization continues."
        );
    }
    CSummon::InitSummon();
    petData.Verify();

    // Log.Add(LOG_NORMAL, L"Loading NPC")
    // L2_LOG_INFO(Log, L"Loading NPC")
    L2_LOG_INFO(Log, u"[startup] Loading NPC definitions.");
    g_ObjectDB.LoadNPC(false, false);
    // Log.Add(LOG_NORMAL, L"Loading PC_Parameter")
    // L2_LOG_INFO(Log, L"Loading PC_Parameter")
    L2_LOG_INFO(Log, u"[startup] Loading player character parameters.");
    g_PCParamDB.Load(false);
    OptionDB::inst().Load();
    g_VariationDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading SkillAcquire")
    // L2_LOG_INFO(Log, L"Loading SkillAcquire")
    L2_LOG_INFO(Log, u"[startup] Loading skill acquisition data.");
    g_SkillAcquireDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading SkillEnchantData")
    // L2_LOG_INFO(Log, L"Loading SkillEnchantData")
    L2_LOG_INFO(Log, u"[startup] Loading skill enchantment data.");
    g_SkillEnchantDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading Ensoul Datas")
    // L2_LOG_INFO(Log, L"Loading Ensoul Datas")
    L2_LOG_INFO(Log, u"[startup] Loading ensoul options.");
    g_EnsoulManager.Load(false);
    // Log.Add(LOG_NORMAL, L"Loading Faction")
    // L2_LOG_INFO(Log, L"Loading Faction")
    L2_LOG_INFO(Log, u"[startup] Loading factions.");
    CFactionManager::LoadLexYacc();
    // Log.Add(LOG_NORMAL, L"Loading monster book")
    // L2_LOG_INFO(Log, L"Loading monster book")
    L2_LOG_INFO(Log, u"[startup] Loading the monster book.");
    MonsterBook::Script::inst().LoadScript();
    CAreaDB::inst().LoadArea();
    CAreaDB::inst().SortInstantArea();
    CWorld::Inst().GetGeo()->ExpandWaterSector();

    // Log.Add(LOG_NORMAL, L"Loading DoorData")
    // L2_LOG_INFO(Log, L"Loading DoorData")
    L2_LOG_INFO(Log, u"[startup] Loading doors.");
    g_DoorDB.LoadDoor(false);
    // Log.Add(LOG_NORMAL, L"Loading TrapData")
    // L2_LOG_INFO(Log, L"Loading TrapData")
    L2_LOG_INFO(Log, u"[startup] Loading traps.");
    g_DoorDB.LoadTrap();
    // Log.Add(LOG_NORMAL, L"Loading InstantZone")
    // L2_LOG_INFO(Log, L"Loading InstantZone")
    L2_LOG_INFO(Log, u"[startup] Loading instance zones.");
    g_InstantZoneDB.Load();
    const std::uint32_t clanStart = runtime::GetTickCount();
    // Log.Add(LOG_NORMAL, L"Compute Clan Relation")
    // L2_LOG_INFO(Log, L"Compute Clan Relation")
    L2_LOG_INFO(Log, u"[startup] Computing NPC clan relationships.");
    ComputeClanHelpTable();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Complete to compute Clan table [%ds]",
    //     (runtime::GetTickCount() - clanStart) / 1000U)
    // L2_LOG_INFO(Log,
    //     L"Complete to compute Clan table [%ds]",
    //     (runtime::GetTickCount() - clanStart) / 1000U)
    L2_LOG_INFO(
        Log, u"[startup] NPC clan relationship computation finished in %u s.", (runtime::GetTickCount() - clanStart) / 1000U
    );

    // Log.Add(LOG_NORMAL, L"Loading Vehicle")
    // L2_LOG_INFO(Log, L"Loading Vehicle")
    L2_LOG_INFO(Log, u"[startup] Loading vehicles.");
    g_VehicleDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading Residence")
    // L2_LOG_INFO(Log, L"Loading Residence")
    L2_LOG_INFO(Log, u"[startup] Loading residences.");
    g_ResidenceDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading Manor")
    // L2_LOG_INFO(Log, L"Loading Manor")
    L2_LOG_INFO(Log, u"[startup] Loading manor data.");
    g_ManorDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading Recipe")
    // L2_LOG_INFO(Log, L"Loading Recipe")
    L2_LOG_INFO(Log, u"[startup] Loading recipes.");
    g_RecipeDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading Henna")
    // L2_LOG_INFO(Log, L"Loading Henna")
    L2_LOG_INFO(Log, u"[startup] Loading henna data.");
    g_HennaDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading MultiSell")
    // L2_LOG_INFO(Log, L"Loading MultiSell")
    L2_LOG_INFO(Log, u"[startup] Loading multisell lists.");
    g_MultiSellDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading ItemAttrChange")
    // L2_LOG_INFO(Log, L"Loading ItemAttrChange")
    L2_LOG_INFO(Log, u"[startup] Loading item attribute changes.");
    g_ItemAttrChangeDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading QuestItems")
    // L2_LOG_INFO(Log, L"Loading QuestItems")
    L2_LOG_INFO(Log, u"[startup] Loading quest items.");
    LoadQuestItem();
    // Log.Add(LOG_NORMAL, L"Loading World Plane Collision")
    // L2_LOG_INFO(Log, L"Loading World Plane Collision")
    L2_LOG_INFO(Log, u"[startup] Loading world collision planes.");
    g_WorldPlaneCollision.Load();
    // Log.Add(LOG_NORMAL, L"Loading NewInspectorDB")
    // L2_LOG_INFO(Log, L"Loading NewInspectorDB")
    L2_LOG_INFO(Log, u"[startup] Loading NewInspector data.");
    g_NewInspectorDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading agit deco")
    // L2_LOG_INFO(Log, L"Loading agit deco")
    L2_LOG_INFO(Log, u"[startup] Loading residence decorations.");
    CResidenceDecoDb::Inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading agit deco npc")
    // L2_LOG_INFO(Log, L"Loading agit deco npc")
    L2_LOG_INFO(Log, u"[startup] Loading residence decoration NPCs.");
    CDecoNpcDB::Inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading Fishingdata")
    // L2_LOG_INFO(Log, L"Loading Fishingdata")
    L2_LOG_INFO(Log, u"[startup] Loading fishing data.");
    g_FishingDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading CursedWeapon")
    // L2_LOG_INFO(Log, L"Loading CursedWeapon")
    L2_LOG_INFO(Log, u"[startup] Loading cursed weapons.");
    CCursedWeaponMgr::inst().LoadFromScript();
    // Log.Add(LOG_NORMAL, L"Loading UserBasicAction")
    // L2_LOG_INFO(Log, L"Loading UserBasicAction")
    L2_LOG_INFO(Log, u"[startup] Loading basic player actions.");
    g_UserBasicAction.Load();
    // Log.Add(LOG_NORMAL, L"Loading Transform")
    // L2_LOG_INFO(Log, L"Loading Transform")
    L2_LOG_INFO(Log, u"[startup] Loading transformations.");
    g_TransformDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading NPC Auction")
    // L2_LOG_INFO(Log, L"Loading NPC Auction")
    L2_LOG_INFO(Log, u"[startup] Loading auction NPC data.");
    CAuctionNPCDB::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading Convert Item")
    // L2_LOG_INFO(Log, L"Loading Convert Item")
    L2_LOG_INFO(Log, u"[startup] Loading item conversions.");
    g_ConvertItemDB.Load();
    // Log.Add(LOG_NORMAL, L"Loading Cubic & Agathion")
    // L2_LOG_INFO(Log, L"Loading Cubic & Agathion")
    L2_LOG_INFO(Log, u"[startup] Loading cubics and agathions.");
    SpriteDB::Inst().Load();

    if (!g_PremiumSetting.cashshopPause) {
        // Log.Add(LOG_NORMAL, L"Loading Product")
        // L2_LOG_INFO(Log, L"Loading Product")
        L2_LOG_INFO(Log, u"[startup] Loading shop products.");
        if (!g_ShopServer.Load()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Loading Fail in productdata.txt, set premiumservice pause")
            // L2_LOG_ERROR(Log,
            //     L"Loading Fail in productdata.txt, set premiumservice pause")
            L2_LOG_ERROR(Log, u"[startup] Cannot load productdata.txt; the cash shop will be paused.");
            g_PremiumSetting.cashshopPause = true;
        }
    }
    // Log.Add(LOG_NORMAL, L"Loading PremiumService Setting")
    // L2_LOG_INFO(Log, L"Loading PremiumService Setting")
    L2_LOG_INFO(Log, u"[startup] Loading premium service settings.");
    g_PremiumSvc.Load();
    // Log.Add(LOG_NORMAL, L"Loading AirShip")
    // L2_LOG_INFO(Log, L"Loading AirShip")
    L2_LOG_INFO(Log, u"[startup] Loading airships.");
    CAirShipManager::Inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading FieldCycle")
    // L2_LOG_INFO(Log, L"Loading FieldCycle")
    L2_LOG_INFO(Log, u"[startup] Loading field cycles.");
    g_FieldCycleSystem.Load();
    // Log.Add(LOG_NORMAL, L"Loading FieldCycleMissionData")
    // L2_LOG_INFO(Log, L"Loading FieldCycleMissionData")
    L2_LOG_INFO(Log, u"[startup] Loading field cycle missions.");
    FieldCycleMissionDB::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading Enchant Option")
    // L2_LOG_INFO(Log, L"Loading Enchant Option")
    L2_LOG_INFO(Log, u"[startup] Loading enchantment options.");
    CEnchantOptionDB::Inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading QuestSystem")
    // L2_LOG_INFO(Log, L"Loading QuestSystem")
    L2_LOG_INFO(Log, u"[startup] Loading quest system data.");
    CQuestSystem::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading UserQuestData...")
    // L2_LOG_INFO(Log, L"Loading UserQuestData...")
    L2_LOG_INFO(Log, u"[startup] Loading user quest definitions.");
    if (QuestManager::inst().LoadUserQuest(false) == 0) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Loading UserQuestData.txt failed!!!")
        // L2_LOG_INFO(Log,
        //     L"Loading UserQuestData.txt failed!!!")
        L2_LOG_ERROR(Log, u"[startup] Cannot load UserQuestData.txt; initialization continues.");
    }
    // Log.Add(LOG_NORMAL, L"Loading DynamicQuestData")
    // L2_LOG_INFO(Log, L"Loading DynamicQuestData")
    L2_LOG_INFO(Log, u"[startup] Loading dynamic quest data.");
    DynamicQuestManager::Inst()->Init();
    // Log.Add(LOG_NORMAL, L"Loading Elevator")
    // L2_LOG_INFO(Log, L"Loading Elevator")
    L2_LOG_INFO(Log, u"[startup] Loading elevators.");
    CShuttleManager::Inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading Flymove")
    // L2_LOG_INFO(Log, L"Loading Flymove")
    L2_LOG_INFO(Log, u"[startup] Loading flight movement data.");
    CFlyMoveManager::Inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading EventCampaign")
    // L2_LOG_INFO(Log, L"Loading EventCampaign")
    L2_LOG_INFO(Log, u"[startup] Loading event campaigns.");
    CEventCampaignManager::Inst()->Init();
    // Log.Add(LOG_NORMAL, L"Loading LuckyGameData")
    // L2_LOG_INFO(Log, L"Loading LuckyGameData")
    L2_LOG_INFO(Log, u"[startup] Loading Lucky Game data.");
    g_CLuckyGameDB.LoadLuckyGamedata();
    // Log.Add(LOG_NORMAL, L"Loading PathToAwakeningData")
    // L2_LOG_INFO(Log, L"Loading PathToAwakeningData")
    L2_LOG_INFO(Log, u"[startup] Loading Path to Awakening data.");
    CPathToAwakeningNotice::Inst()->LoadAwakeningNotice();
    // Log.Add(LOG_NORMAL, L"Loading PledgeLevelInfo")
    // L2_LOG_INFO(Log, L"Loading PledgeLevelInfo")
    L2_LOG_INFO(Log, u"[startup] Loading clan level data.");
    PledgeLevelDB::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading PledgeMasteryInfo")
    // L2_LOG_INFO(Log, L"Loading PledgeMasteryInfo")
    L2_LOG_INFO(Log, u"[startup] Loading clan mastery data.");
    PledgeMasteryDB::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading PledgeContributionInfo")
    // L2_LOG_INFO(Log, L"Loading PledgeContributionInfo")
    L2_LOG_INFO(Log, u"[startup] Loading clan contribution data.");
    PledgeContributionDB::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading PledgeFunctionInfo")
    // L2_LOG_INFO(Log, L"Loading PledgeFunctionInfo")
    L2_LOG_INFO(Log, u"[startup] Loading clan function data.");
    PledgeFunctionDB::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading PledgeSkillInfo")
    // L2_LOG_INFO(Log, L"Loading PledgeSkillInfo")
    L2_LOG_INFO(Log, u"[startup] Loading clan skill data.");
    PledgeSkillDB::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading PledgeItemInfo")
    // L2_LOG_INFO(Log, L"Loading PledgeItemInfo")
    L2_LOG_INFO(Log, u"[startup] Loading clan item data.");
    PledgeItemDB::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading PledgeMissionInfo")
    // L2_LOG_INFO(Log, L"Loading PledgeMissionInfo")
    L2_LOG_INFO(Log, u"[startup] Loading clan missions.");
    PledgeMissionDB::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading ItemAnnounceInfo")
    // L2_LOG_INFO(Log, L"Loading ItemAnnounceInfo")
    L2_LOG_INFO(Log, u"[startup] Loading item announcement rules.");
    ItemAnnounceDB::inst().Load();
    // Log.Add(LOG_NORMAL, L"Loading Localization Datas")
    // L2_LOG_INFO(Log, L"Loading Localization Datas")
    L2_LOG_INFO(Log, u"[startup] Loading localization data.");
    g_Localization.LoadLocalizationData(0x0e);
    // Log.Add(LOG_NORMAL, L"Loading CardUpDownGame Data")
    // L2_LOG_INFO(Log, L"Loading CardUpDownGame Data")
    L2_LOG_INFO(Log, u"[startup] Loading Card Up/Down game data.");
    CardUpDownGame::Script::inst().LoadData();
    // Log.Add(LOG_NORMAL, L"Loading Artifact Data")
    // L2_LOG_INFO(Log, L"Loading Artifact Data")
    L2_LOG_INFO(Log, u"[startup] Loading artifacts.");
    CArtifactDB::inst().LoadData();

    const std::uint32_t postLoadStart = runtime::GetTickCount();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NewInspector] CRC [%d]",
    //     g_NewInspectorDB.GetCRC())
    // Noise: The same CRC is reported once below at INFO. The checksum itself is not an error.
    // L2_LOG_ERROR(Log,
    //     L"[NewInspector] CRC [%d]",
    //     g_NewInspectorDB.GetCRC())
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[NO_ERROR] [NewInspector] CRC [%d]",
    //     g_NewInspectorDB.GetCRC())
    // L2_LOG_INFO(Log,
    //     L"[NO_ERROR] [NewInspector] CRC [%d]",
    //     g_NewInspectorDB.GetCRC())
    L2_LOG_INFO(Log, u"[startup] NewInspector data CRC: %u.", static_cast<unsigned>(g_NewInspectorDB.GetCRC()));
    // Log.Add(LOG_NORMAL, L"PostLoading SkillDB")
    // L2_LOG_INFO(Log, L"PostLoading SkillDB")
    L2_LOG_INFO(Log, u"[startup] Running post-load initialization for skills.");
    g_SkillDB.PostLoad();
    // Log.Add(LOG_NORMAL, L"PostLoading ObjectDB")
    // L2_LOG_INFO(Log, L"PostLoading ObjectDB")
    L2_LOG_INFO(Log, u"[startup] Running post-load initialization for objects.");
    g_ObjectDB.PostLoad();
    // Log.Add(LOG_NORMAL, L"PostLoading InstantZone")
    // L2_LOG_INFO(Log, L"PostLoading InstantZone")
    L2_LOG_INFO(Log, u"[startup] Running post-load initialization for instance zones.");
    g_InstantZoneDB.PostLoad();
    // Log.Add(LOG_NORMAL, L"PostLoading AreaData")
    // L2_LOG_INFO(Log, L"PostLoading AreaData")
    L2_LOG_INFO(Log, u"[startup] Running post-load initialization for areas.");
    CAreaDB::inst().PostLoad();
    g_FishingDB.PostLoad();
    static_cast<void>(SharedRimPointInfo::GetInfo());
    g_NPCLoadingDataMap.clear();
    // Log.Add(LOG_NORMAL, L"Loading ArmorEnchantBonus")
    // L2_LOG_INFO(Log, L"Loading ArmorEnchantBonus")
    L2_LOG_INFO(Log, u"[startup] Loading armor enchantment bonuses.");
    LoadArmorEnchantBonus();
    // Log.Add(LOG_NORMAL, L"Loading WeaponEnchantEx")
    // L2_LOG_INFO(Log, L"Loading WeaponEnchantEx")
    L2_LOG_INFO(Log, u"[startup] Loading weapon enchantment bonuses.");
    LoadWeaponEnchantExBonus();
    g_LoadDataPostLoadMilliseconds = runtime::GetTickCount() - postLoadStart;
}

void ListenThread_common() {
    // Current L2Server.cpp 0x008088d0. This is the complete non-GUI
    // startup coordinator reached by the LISTEN wrapper in the pinned image.
    L2_GUARD_FUNCTION();

    const std::uint32_t listenerStart = runtime::GetTickCount();
    InitializeRandomNumber();
    RenameOldLog();

    const int socketResult = portable::InitializeSocketSubsystem();
    if (socketResult != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "WSAStartup error 0x%x",
        //     socketResult)
        // L2_LOG_ERROR(Log,
        //     "WSAStartup error 0x%x",
        //     socketResult)
        L2_LOG_ERROR(
            Log, "[startup] Socket subsystem initialization failed (code %d); listener initialization stops.", socketResult
        );
        return;
    }

    bool enablePledgeContribution = true;
    CPVPMatchSystem::inst().Init();
    CPacketStat<200, 200>::Inst().Init(L"ServerToNpc");
    CPacketStat<312, 254>::Inst().Init(L"NpcToServer");
    CPacketStat<768, 254>::Inst().Init(L"ServerToUser");
    CPacketStat<560, 208>::Inst().Init(L"UserToServer");
    CPacketStat<37, 37>::Inst().Init(L"NPCInfoPacket");
    CPacketStat<103, 103>::Inst().Init(L"CharInfoPacketStat1");
    CPacketStat<48, 48>::Inst().Init(L"CharInfoPacketStat2");
    CSendNPCInfoStat::Inst().Init();
    NpcDieStat::Inst().Reset();
    NpcBroadCastStat::Inst().Reset();
    CPledge::SetPledgePenalty();

    StartupLoaderMutexLease scriptMutex;
    std::uint32_t scriptMutexLastError = 0;
    std::string scriptMutexDiagnostic;
    if (!scriptMutex.Create(
            L"_L2SERVER_SCRIPT_LOADING_", "_L2SERVER_SCRIPT_LOADING_", scriptMutexLastError, scriptMutexDiagnostic
        )) {
        // Log.Add(
        //     LOG_ERROR,
        //     "L2Server script-loading mutex transport failed: %s",
        //     scriptMutexDiagnostic.c_str())
        // L2_LOG_ERROR(Log,
        //     "L2Server script-loading mutex transport failed: %s",
        //     scriptMutexDiagnostic.c_str())
        L2_LOG_ERROR(
            Log, "[startup] Cannot create the script-loading marker; listener initialization stops. Details: %s",
            scriptMutexDiagnostic.c_str()
        );
        return;
    }
    if (scriptMutexLastError == 183 && !g_checkScriptsOnly) {
        L2_LOG_ERROR(Log, u"[startup] The script-loading marker already exists; listener initialization stops.");
#if defined(_WIN32)
        ::MessageBoxW(nullptr, L"L2cached.EXE is already running.", L"Error", 0);
#else
        // Log.Add(
        //     LOG_ERROR,
        //     L"L2cached.EXE is already running.")
        // Noise: The marker failure is recorded above. It does not establish that CacheD is running.
        // L2_LOG_ERROR(Log,
        //     L"L2cached.EXE is already running.")
#endif
        return;
    }

    g_ListenBeforeLoadDataMilliseconds = runtime::GetTickCount() - listenerStart;
    const std::uint32_t loadStart = runtime::GetTickCount();
    LoadData();
    g_ListenLoadDataMilliseconds = runtime::GetTickCount() - loadStart;
    scriptMutex.ReleaseOwnedAndClose();
    if (g_checkScriptsOnly) {
        L2_LOG_INFO(
            Log, u"[check-scripts] Script loading finished in %u ms; exiting.",
            static_cast<unsigned>(g_ListenLoadDataMilliseconds)
        );
        Log.FlushLogFile();
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }

    const std::uint32_t afterLoadStart = runtime::GetTickCount();
    if (CThreadManager::GetThreadID() != ThreadID::LISTEN) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ERROR] g_tlThreadID(%d) is not equal to LISTEN_THREAD_ID",
        //     CThreadManager::GetThreadID())
        // L2_LOG_ERROR(Log,
        //     "[ERROR] g_tlThreadID(%d) is not equal to LISTEN_THREAD_ID",
        //     CThreadManager::GetThreadID())
        L2_LOG_ERROR(
            Log, "[startup] Listener initialization is running with unexpected worker ID %d; registering the listener role.",
            CThreadManager::GetThreadID()
        );
    }
    CThreadManager::RegisterThisThread(ThreadID::LISTEN);
    InitClientDecryptionKey();

    void* perfMonThread = nullptr;
    if (l2npc::g_PerfMon.Init()) {
        perfMonThread = runtime::CreateOwnedThread(&PerfMonThread, nullptr, false);
        CThreadManager::SetThread(ThreadID::PERFMON, perfMonThread);
    }

    const int ioThreadCount = GetIOThreadCount();
    InitIOThread();
    for (int threadId = 0; threadId < ioThreadCount; ++threadId) {
        CreateIOThread2(new CIOThreadCallback(threadId));
    }

    CCleft::inst().AddTimer(1000, 0);
    CBlockUpsetManager::inst().AddTimer(1000, 0);
    // The LogD reader can dispatch packets after the IO workers start.
    runtime::log::StartLogReceiving(Log);
    Log.ConnectToLogD();

    if (!db.InitCacheD()) {
        TerminateListenerStartup(L"Couldn't connect to CacheD", true);
    }
    db.SendCacheVersion();
    constexpr int kCacheRevision = 373;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] RQ_VERSION(%d) during %d seconds",
    //     kCacheRevision,
    //     g_CacheD_nCheckTimeout)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] RQ_VERSION(%d) during %d seconds",
    //     kCacheRevision,
    //     g_CacheD_nCheckTimeout)
    L2_LOG_INFO(
        Log, u"[CacheD] Waiting for revision %d check (configured timeout %d s).", kCacheRevision, g_CacheD_nCheckTimeout
    );
    // if (!db.WaitForCheckVersion(
    //         static_cast<unsigned long>(
    //             g_CacheD_nCheckTimeout))) {
    const bool cacheVersionChecked = db.WaitForCheckVersion(static_cast<unsigned long>(g_CacheD_nCheckTimeout));
    if (!cacheVersionChecked) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Timeout for checking RQ_VERSION(%d)",
        //     kCacheRevision)
        // L2_LOG_ERROR(Log,
        //     L"Timeout for checking RQ_VERSION(%d)",
        //     kCacheRevision)
        L2_LOG_ERROR(
            Log, u"[CacheD] Revision %d check did not complete (configured timeout %d s).", kCacheRevision,
            g_CacheD_nCheckTimeout
        );
        if (!g_CacheD_bCheckCrash) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed but ignore to crash by CrashOnFailed in [CacheD]")
            // L2_LOG_ERROR(Log,
            //     L"Failed but ignore to crash by CrashOnFailed in [CacheD]")
            L2_LOG_WARN(Log, u"[CacheD] Startup continues without a confirmed revision check because CrashOnFailed=false.");
        } else {
            ForceCrash(CrashInfo::CACHE_VERSION_FAILED);
        }
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] Success to check RQ_VERSION(%d)",
    //     kCacheRevision)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] Success to check RQ_VERSION(%d)",
    //     kCacheRevision)
    if (cacheVersionChecked) {
        L2_LOG_INFO(Log, u"[CacheD] Revision %d check completed.", kCacheRevision);
    }

    if (g_AccountServerSetting.bConnect) {
        AccountServerStub& accountServer = AccountServerStub::Inst();
        accountServer.Init(g_AccountServerSetting.serverAddr, static_cast<std::uint16_t>(g_AccountServerSetting.serverPort));
        if (accountServer.Connect(10)) {
            accountServer.ACS_TEST_DB_SP();
            accountServer.ASC_TEST_MORPHEUS();
            // Log.Add(
            //     LOG_ERROR,
            //     L"AccountServer connected")
            // L2_LOG_ERROR(Log,
            //     L"AccountServer connected")
            L2_LOG_INFO(Log, u"[startup] Connected to AccountServer.");
        }
    }

    static_cast<void>(IPBlockManager::inst().Init());
    CCommCli::Inst()->CreateThread();
    // CCommCli::Inst()->ConnectToCommD()
    // Portable change: the original also connected here with [L2COMM]
    // usesvr=0, where every later use of the service is refused.
    if (commUseSvr == 1) {
        CCommCli::Inst()->ConnectToCommD();
    }
    g_L2Time.Init();
    CAnnounceTimer::Inst()->Init();
    CReservationAnnounceTimer::inst().Init();

    const std::u16string eventDataPath(u"..\\Script\\eventdata.xml");
    if (!g_bLoadEventDataFromDB) {
        CEventManager::inst().Init(eventDataPath);
    } else {
        CEventManager::inst().InitFromDB();
    }
    CEventMakerManager::inst().Init(eventDataPath);

    // Log.Add(LOG_NORMAL, L"InstantAgitDataChecker.exe")
    // L2_LOG_INFO(Log, L"InstantAgitDataChecker.exe")
    L2_LOG_INFO(Log, u"[startup] Initializing instance residences.");
    CInstantAgitManager::inst().Init();
    DynamicQuestManager::Inst()->Load();
    CEventCampaignManager::Inst()->Load();
    g_ShopServer.Init();
    g_InstantZoneMgr.GetRimRank();
    CTreeLoc::Init();
    g_JobFinishSignal.Init();
    TimeAttackBoard::inst().OnStart();
    CPledgeRanking::RequestLoadRanking();
    CMarketPriceList::Load();

    CAlliance::InitAlliance();
    CCastle::InitCastle();
    CWorldAgit::InitWorldAgit();
    CFortress::InitFortress();
    CParty::Stat::inst().Init();
    CPartyRoomHistoryManager::inst().Init();
    CPledgeJoinSystem::Inst()->Init();

    WOlympiadServiceConfig& olympiadConfig = WOlympiadServiceConfig::inst();
    olympiadConfig.Load(std::u16string(u"..\\Script\\OlympiadData.xml"));
    if (!olympiadConfig.IsHost()) {
        NoblessSystem::Init();
    } else {
        WOlympiadUsers::inst().Init();
    }

    if (g_L2ServerType != L2ServerType::WORLDSERVER) {
        g_PledgeGameSystem.Init();
        static_cast<void>(g_MatchingPointDB.LoadLexYacc());
    }
    g_FieldCycleSystem.RequestFieldCycleInfo();
    g_InstantZoneMgr.LoadInzoneMaxDBID();
    db.RequestLoadLastTaxChange();
    db.RequestWorldChatResetTime();
    g_MonRaceDB.Init();
    g_ManorDB.LoadFromDB();
    CAreaDB::inst().StartAreaTimer();
    CCursedWeaponMgr::Init();
    CAuctionManager::inst().Load();

    g_PledgeTimer.AddTimer(1000, 0);
    // Current ListenThread_common 0x0080970b starts the process-global
    // residence expel timer before the hourly report timer.
    g_ExpelTimer.AddTimer(1000, 0);
    g_ReportTimer.AddTimer(3600000, 0);
    CAirShipManager::Inst().LoadAirShipFromDB();
    (new CRegularAirShipStartMessage)->Post(0);
    if (g_bUseVehicle) {
        (new CVehicleStartMessage)->Post(0);
    }
    g_HTMLCache.Load();
    (new CRegisterPendedStaticObjects)->Post(0);
    g_ResidenceDB.ResidenceGuardLoad();
    static_cast<void>(g_FontCheck.Init());
    static_cast<void>(g_FontCheck.Init());
    g_RestrictArea.Load();
    static_cast<void>(g_RestrictArea.LoadScript(L"..\\Script\\announce_sphere.txt", true));
    CUserSocket::MakePacketBlockList();
    db.RequestLoadTransferredPledgeMaster();

    if (g_L2ServerType == L2ServerType::DEV) {
        g_TestNPC = new CNPC;
        g_TestNPC->OnCreate();
    }
    CConnectThreadManager::Inst().CreateThread();
    CPremiumInstantRankingSystem::Inst().Init();
    RaidServerManager::inst().Load();
    RaidClientManager::inst().Load();

    g_npcserver.Run(g_nNPCPort, &NpcSocket::Allocate, false, 0, 0);
    lu4::Install();
    g_server.Run(g_nUserPort, &CUserSocket::Allocate, false, 0, 0);
    if (!olympiadConfig.IsHost()) {
        RaidServerManager::inst().Run(
            g_nRaidPort, +[](std::uint64_t socket) -> CSocket* { return RaidClientSocket::Allocate(socket); }, false, 0, 0
        );
    }

    if (!authd.InitAuthD()) {
        TerminateListenerStartup(L"Couldn't connect to AuthD", false);
    }
    if (g_nUseReconnectAuth != 0) {
        authd.SendAuthConnected();
    }

    if (g_bUseNPLogin) {
        if (!NPSystem::Initialize()) {
            TerminateListenerStartup(L"Init NP System failed", false);
        }
        if (!NPSystem::g_NPD.ConnectNP() || !NPSystem::g_NPD.SendConnectServer()) {
            TerminateListenerStartup(L"NP Connect Failed", false);
        }
        // Log.Add(LOG_ERROR, L"NP Connect Success")
        // L2_LOG_ERROR(Log, L"NP Connect Success")
        L2_LOG_INFO(Log, u"[startup] Connected to NP and submitted the server registration request.");
    }

    static_cast<void>(petitionCli.Init());
    static_cast<void>(petitionCli.CreateThread());
    GIP.Init();
    static_cast<void>(g_PremiumSvc.Init());
    g_PremiumSvc.CreateThread();
    static_cast<void>(g_GoodsInventory.Init());
    g_GoodsInventory.CreateThread();
    CBotReportAgent::Inst()->Init();
    static_cast<void>(Singleton<CEventMatchMgr>::inst());
    if (g_bUseServerStartMail) {
        SystemMail::sendServerStartCompleted(mailServer);
    }
    RaidClientManager::inst().CreateThread();

    WOlympiadDefaultSetting& olympiadSetting = WOlympiadDefaultSetting::inst();
    if (olympiadSetting.IsMatchDayListEmpty()) {
        olympiadSetting.InitMatchDay();
    }

    if (!olympiadConfig.IsActive()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]World Olympiad is not available",
        //     L"L2Server.cpp",
        //     0xf85)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]World Olympiad is not available",
        //     L"L2Server.cpp",
        //     0xf85)
        L2_LOG_INFO(Log, u"[startup] World Olympiad is inactive for this server.");
    } else if (olympiadConfig.IsHost()) {
        WOlympiadNetworkHost::inst().Init(g_nOlympiadPort, &WOlympiadService::inst());
        if (!WOlympiadService::inst().Init()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]failed to initialize service",
            //     L"L2Server.cpp",
            //     0xf6b)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]failed to initialize service",
            //     L"L2Server.cpp",
            //     0xf6b)
            L2_LOG_ERROR(Log, u"[startup] World Olympiad host service initialization failed; startup continues.");
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]World Olympiad Host is available",
            //     L"L2Server.cpp",
            //     0xf6f)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]World Olympiad Host is available",
            //     L"L2Server.cpp",
            //     0xf6f)
            L2_LOG_INFO(Log, u"[startup] World Olympiad host service initialized.");
        }
        enablePledgeContribution = false;
    } else if (olympiadConfig.IsPeer()) {
        if (!WOlympiadNetworkPeer::inst().Init(&WOlympiadServiceProxy::inst())) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d] failed to initialize peer",
            //     L"L2Server.cpp",
            //     0xf76)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d] failed to initialize peer",
            //     L"L2Server.cpp",
            //     0xf76)
            L2_LOG_ERROR(Log, u"[startup] World Olympiad peer network initialization failed; startup continues.");
        }
        if (!WOlympiadServiceProxy::inst().Init()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d] failed to initialize service proxy",
            //     L"L2Server.cpp",
            //     0xf7b)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d] failed to initialize service proxy",
            //     L"L2Server.cpp",
            //     0xf7b)
            L2_LOG_ERROR(Log, u"[startup] World Olympiad service proxy initialization failed; startup continues.");
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]World Olympiad Peer is available",
            //     L"L2Server.cpp",
            //     0xf7f)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]World Olympiad Peer is available",
            //     L"L2Server.cpp",
            //     0xf7f)
            L2_LOG_INFO(Log, u"[startup] World Olympiad service proxy initialized.");
        }
    }

    if (enablePledgeContribution && g_L2ServerType != L2ServerType::WORLDSERVER) {
        if (!PledgeContributionSystem::inst().Init()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]failed to initialize Contribution system",
            //     L"L2Server.cpp",
            //     0xf91)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]failed to initialize Contribution system",
            //     L"L2Server.cpp",
            //     0xf91)
            L2_LOG_ERROR(Log, u"[startup] Clan contribution system initialization failed; startup continues.");
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][PLEDGE][%s][%d]Contribution system is available",
            //     L"L2Server.cpp",
            //     0xf95)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][PLEDGE][%s][%d]Contribution system is available",
            //     L"L2Server.cpp",
            //     0xf95)
            L2_LOG_INFO(Log, u"[startup] Clan contribution system initialized.");
        }
    }

    static_cast<void>(PledgeRaidSystem::inst().Init());
    static_cast<void>(g_CaptchaManager.Init());
    g_CaptchaManager.CreateThread();
    CategoryContent::CategoryContentInit();

    g_ListenAfterLoadDataMilliseconds = runtime::GetTickCount() - afterLoadStart;
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] Before LoadData LoadingTime : %d",
    //     static_cast<int>(
    //         g_ListenBeforeLoadDataMilliseconds / 1000U))
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] Before LoadData LoadingTime : %d",
    //     static_cast<int>(
    //         g_ListenBeforeLoadDataMilliseconds / 1000U))
    L2_LOG_INFO(
        Log, u"[startup timing] Initialization before data loading: %d s.",
        static_cast<int>(g_ListenBeforeLoadDataMilliseconds / 1000U)
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] LoadData LoadingTime : %d",
    //     static_cast<int>(
    //         g_ListenLoadDataMilliseconds / 1000U))
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] LoadData LoadingTime : %d",
    //     static_cast<int>(
    //         g_ListenLoadDataMilliseconds / 1000U))
    L2_LOG_INFO(Log, u"[startup timing] Data loading: %d s.", static_cast<int>(g_ListenLoadDataMilliseconds / 1000U));
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData BeforeOMP LoadingTime : %d",
    //     static_cast<int>(
    //         g_LoadDataBootstrapMilliseconds / 1000U))
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData BeforeOMP LoadingTime : %d",
    //     static_cast<int>(
    //         g_LoadDataBootstrapMilliseconds / 1000U))
    L2_LOG_INFO(
        Log, u"[startup timing] Data-loading bootstrap: %d s.", static_cast<int>(g_LoadDataBootstrapMilliseconds / 1000U)
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData OMP LoadingTime : %d",
    //     static_cast<int>(g_LoadDataOmpMilliseconds / 1000U))
    // Noise: This retired OMP timing counter has no producer and always reports zero.
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData OMP LoadingTime : %d",
    //     static_cast<int>(g_LoadDataOmpMilliseconds / 1000U))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData -OMP[loadGeoDataSecond] : %d",
    //     static_cast<int>(g_LoadGeoDataSeconds))
    // Noise: This retired OMP timing counter has no producer and always reports zero.
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData -OMP[loadGeoDataSecond] : %d",
    //     static_cast<int>(g_LoadGeoDataSeconds))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData -OMP[loadPathNodeSecond] : %d",
    //     static_cast<int>(g_LoadPathNodeSeconds))
    // Noise: This retired OMP timing counter has no producer and always reports zero.
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData -OMP[loadPathNodeSecond] : %d",
    //     static_cast<int>(g_LoadPathNodeSeconds))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData -OMP[loadSkillOMPSecond] : %d",
    //     static_cast<int>(g_LoadSkillOmpSeconds))
    // Noise: This retired OMP timing counter has no producer and always reports zero.
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData -OMP[loadSkillOMPSecond] : %d",
    //     static_cast<int>(g_LoadSkillOmpSeconds))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData -OMP[loadSecondOMPSecond] : %d",
    //     static_cast<int>(g_LoadSecondOmpSeconds))
    // Noise: This retired OMP timing counter has no producer and always reports zero.
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData -OMP[loadSecondOMPSecond] : %d",
    //     static_cast<int>(g_LoadSecondOmpSeconds))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData --OMP[loadSecondOMPSecond] time:%d, call:%d",
    //     static_cast<int>(totalAddAreaTime / 1000U),
    //     totalADDAreaCallCount)
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData --OMP[loadSecondOMPSecond] time:%d, call:%d",
    //     static_cast<int>(totalAddAreaTime / 1000U),
    //     totalADDAreaCallCount)
    L2_LOG_INFO(
        Log, u"[startup timing] Area insertion: %u ms across %d calls.", static_cast<unsigned>(totalAddAreaTime),
        totalADDAreaCallCount
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData ---OMP[loadSecondOMPSecond] time1:%d",
    //     static_cast<int>(totalAddAreaTime1 / 1000U))
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData ---OMP[loadSecondOMPSecond] time1:%d",
    //     static_cast<int>(totalAddAreaTime1 / 1000U))
    L2_LOG_INFO(
        Log, u"[startup timing] Area geodata setup, including dynamic/static fields: %u ms.",
        static_cast<unsigned>(totalAddAreaTime1)
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData ---OMP[loadSecondOMPSecond] time2:%d",
    //     static_cast<int>(totalAddAreaTime2 / 1000U))
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData ---OMP[loadSecondOMPSecond] time2:%d",
    //     static_cast<int>(totalAddAreaTime2 / 1000U))
    L2_LOG_INFO(Log, u"[startup timing] Dynamic field setup: %u ms.", static_cast<unsigned>(totalAddAreaTime2));
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData ---OMP[loadSecondOMPSecond] time3:%d",
    //     static_cast<int>(totalAddAreaTime3 / 1000U))
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData ---OMP[loadSecondOMPSecond] time3:%d",
    //     static_cast<int>(totalAddAreaTime3 / 1000U))
    L2_LOG_INFO(Log, u"[startup timing] Static field setup: %u ms.", static_cast<unsigned>(totalAddAreaTime3));
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile1:%d, callcount[%d]",
    //     static_cast<int>(profile1 / 1000U),
    //     callCount1)
// Noise: Without L2_STARTUP_DIAGNOSTICS the counters are not collected, so zeros would imply measurements that were not made.
#if defined(L2_STARTUP_DIAGNOSTICS)
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile1:%d, callcount[%d]",
    //     static_cast<int>(profile1 / 1000U),
    //     callCount1)
    L2_LOG_INFO(
        Log, u"[startup timing] Static fields, geodata zone lookup: %u ms across %d calls.", static_cast<unsigned>(profile1),
        callCount1
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile2:%d, callcount[%d]",
    //     static_cast<int>(profile2 / 1000U),
    //     callCount2)
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile2:%d, callcount[%d]",
    //     static_cast<int>(profile2 / 1000U),
    //     callCount2)
    L2_LOG_INFO(
        Log, u"[startup timing] Static fields, geodata sector lookup: %u ms across %d calls.",
        static_cast<unsigned>(profile2), callCount2
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile3:%d, callcount[%d]",
    //     static_cast<int>(profile3 / 1000U),
    //     callCount3)
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile3:%d, callcount[%d]",
    //     static_cast<int>(profile3 / 1000U),
    //     callCount3)
    L2_LOG_INFO(
        Log, u"[startup timing] Static fields, existing field checks: %u ms across %d calls.",
        static_cast<unsigned>(profile3), callCount3
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile4:%d, callcount[%d]",
    //     static_cast<int>(profile4 / 1000U),
    //     callCount4)
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile4:%d, callcount[%d]",
    //     static_cast<int>(profile4 / 1000U),
    //     callCount4)
    L2_LOG_INFO(
        Log, u"[startup timing] Static fields, territory containment checks: %u ms across %d calls.",
        static_cast<unsigned>(profile4), callCount4
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile5:%d, callcount[%d]",
    //     static_cast<int>(profile5 / 1000U),
    //     callCount5)
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile5:%d, callcount[%d]",
    //     static_cast<int>(profile5 / 1000U),
    //     callCount5)
    L2_LOG_INFO(
        Log, u"[startup timing] Static fields, base cell lookup: %u ms across %d calls.", static_cast<unsigned>(profile5),
        callCount5
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile6:%d, callcount[%d]",
    //     static_cast<int>(profile6 / 1000U),
    //     callCount6)
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile6:%d, callcount[%d]",
    //     static_cast<int>(profile6 / 1000U),
    //     callCount6)
    L2_LOG_INFO(
        Log, u"[startup timing] Static fields, water field processing: %u ms across %d calls.",
        static_cast<unsigned>(profile6), callCount6
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile7:%d, callcount[%d]",
    //     static_cast<int>(profile7 / 1000U),
    //     callCount7)
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData ----OMP[loadSecondOMPSecond] profile7:%d, callcount[%d]",
    //     static_cast<int>(profile7 / 1000U),
    //     callCount7)
    L2_LOG_INFO(
        Log, u"[startup timing] Static fields, other field processing: %u ms across %d calls.",
        static_cast<unsigned>(profile7), callCount7
    );
#else
    L2_LOG_INFO(
        Log, "[startup timing] Detailed static field profiling is disabled. "
             "Build the l2server-startup-diagnostics profile to enable it."
    );
#endif
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData -OMP[loadThirdOMPSecond] : %d",
    //     static_cast<int>(g_LoadThirdOmpSeconds))
    // Noise: This retired OMP timing counter has no producer and always reports zero.
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData -OMP[loadThirdOMPSecond] : %d",
    //     static_cast<int>(g_LoadThirdOmpSeconds))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData -OMP[loadClanTableSecond] : %d",
    //     static_cast<int>(g_LoadClanTableSeconds))
    // Noise: This retired OMP timing counter has no producer and always reports zero.
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData -OMP[loadClanTableSecond] : %d",
    //     static_cast<int>(g_LoadClanTableSeconds))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData -OMP[loadfourthOMPSecond] : %d",
    //     static_cast<int>(g_LoadFourthOmpSeconds))
    // Noise: This retired OMP timing counter has no producer and always reports zero.
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData -OMP[loadfourthOMPSecond] : %d",
    //     static_cast<int>(g_LoadFourthOmpSeconds))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData -OMP[loadFifthSecond] LoadingTime : %d",
    //     static_cast<int>(g_LoadFifthSeconds))
    // Noise: This retired OMP timing counter has no producer and always reports zero.
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData -OMP[loadFifthSecond] LoadingTime : %d",
    //     static_cast<int>(g_LoadFifthSeconds))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] -LoadData AfterOMP LoadingTime : %d",
    //     static_cast<int>(
    //         g_LoadDataPostLoadMilliseconds / 1000U))
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] -LoadData AfterOMP LoadingTime : %d",
    //     static_cast<int>(
    //         g_LoadDataPostLoadMilliseconds / 1000U))
    L2_LOG_INFO(
        Log, u"[startup timing] Data post-load initialization: %d s.",
        static_cast<int>(g_LoadDataPostLoadMilliseconds / 1000U)
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] After  LoadData LoadingTime : %d",
    //     static_cast<int>(
    //         g_ListenAfterLoadDataMilliseconds / 1000U))
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] After  LoadData LoadingTime : %d",
    //     static_cast<int>(
    //         g_ListenAfterLoadDataMilliseconds / 1000U))
    L2_LOG_INFO(
        Log, u"[startup timing] Initialization after data loading: %d s.",
        static_cast<int>(g_ListenAfterLoadDataMilliseconds / 1000U)
    );
    if (pMaxHorizonTerritory != nullptr) {
        const std::wstring territoryName = Utf16ToWide(pMaxHorizonTerritory->sName.c_str());
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[TimeCheck] maxHorizonLength[%d], pMaxHorizonTerritory[%s]",
        //     maxHorizonLength,
        //     territoryName.c_str())
        // L2_LOG_INFO(Log,
        //     L"[TimeCheck] maxHorizonLength[%d], pMaxHorizonTerritory[%s]",
        //     maxHorizonLength,
        //     territoryName.c_str())
        L2_LOG_INFO(
            Log, u"[startup timing] Largest static field X span: %d coordinate units; territory %ls.", maxHorizonLength,
            territoryName.c_str()
        );
    }
    if (pMaxVerticalTerritory != nullptr) {
        const std::wstring territoryName = Utf16ToWide(pMaxVerticalTerritory->sName.c_str());
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[TimeCheck] maxVerticalLength[%d], pMaxVerticalTerritory[%s]",
        //     maxVerticalLength,
        //     territoryName.c_str())
        // L2_LOG_INFO(Log,
        //     L"[TimeCheck] maxVerticalLength[%d], pMaxVerticalTerritory[%s]",
        //     maxVerticalLength,
        //     territoryName.c_str())
        L2_LOG_INFO(
            Log, u"[startup timing] Largest static field Y span: %d coordinate units; territory %ls.", maxVerticalLength,
            territoryName.c_str()
        );
    }
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[TimeCheck] CTerritory::SetStaticField total loop count [%d]",
    //     totalloopCount)
// Noise: The disabled-profiling message above covers this counter when it is not collected.
#if defined(L2_STARTUP_DIAGNOSTICS)
    // L2_LOG_INFO(Log,
    //     L"[TimeCheck] CTerritory::SetStaticField total loop count [%d]",
    //     totalloopCount)
    L2_LOG_INFO(Log, u"[startup timing] Static field grid iterations: %d.", totalloopCount);
#endif

    BalthusEvent::inst().Init();
    BalthusEvent::inst().AddTimer(1000, 0);
    static_cast<void>(CItemRestrict::Inst().LoadData());
    static_cast<void>(TalkChannelCache::inst());
    ServerAutoStarter serverAutoStarter;
    serverAutoStarter.Do();
    BatchAutoStarter batchAutoStarter;
    batchAutoStarter.Do();
    if (g_bTestEventServer) {
        NullUserSocket::InitializeNullUserSocket();
    }
    CReservationNPCSpawnTimer::inst().Init();
    static_cast<void>(WaitThread(nullptr));
    portable::CleanupSocketSubsystem();

    if (perfMonThread != nullptr) {
        runtime::JoinOwnedThread(perfMonThread);
        runtime::DestroyOwnedThread(perfMonThread);
        CThreadManager::SetThread(ThreadID::PERFMON, nullptr);
    }
}

void ListenThread_mailing() {
    // Current 0x0080a1c8 is the mailing-mode wrapper and contains only the
    // call into the common body. Process-wide exception transport owns the
    // outer SEH policy.
    ListenThread_common();
}

std::uint32_t ListenThread(void*) {
    // Current L2Server.cpp 0x0080a218.
    CThreadManager::RegisterThisThread(ThreadID::LISTEN);
    const int threadId = CThreadManager::GetThreadID();
    g_IOBufferPools[threadId] = new (std::nothrow) CNewMwsrQueue<DefaultIOBuffer>(0x100);
    g_IOBuffer8kPools[threadId] = new (std::nothrow) CNewMwsrQueue<IOBuffer8k>(0x200);
    g_IOBuffer32kPools[threadId] = new (std::nothrow) CNewMwsrQueue<IOBuffer32k>(0x20);
    if (ExceptionMailing()) {
        ListenThread_mailing();
    } else {
        ListenThread_common();
    }
    return UINT32_MAX;
}

int main(int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--check-scripts") {
            g_checkScriptsOnly = true;
            std::copy(argv + index + 1, argv + argc, argv + index);
            --argc;
            break;
        }
    }
    if (g_checkScriptsOnly) {
        // Without its own namespace the check run attaches to the live
        // server's shared locks and log mappings and stalls that server.
        // Static objects open them before main, so the caller must set it.
        const char* scope = std::getenv("L2_SHARED_MEMORY_NAMESPACE");
        if (scope == nullptr || *scope == '\0') {
            std::fputs(
                "[check-scripts] Set L2_SHARED_MEMORY_NAMESPACE to a unique value before starting; "
                "otherwise the check shares memory with a running L2Server.\n",
                stderr
            );
            return 2;
        }
        // Script errors would otherwise wait for a click on a message box.
        g_bPopUpWindowsEnabled = false;
    }
    if (!runtime::InitializeDirectoryArguments(argc, argv)) {
        return 2;
    }

    L2Exception::CreateExceptionSystem();
    SetExceptionMailing();

    void* instanceHandle = nullptr;
    std::string instanceDiagnostic;
    const runtime::InstanceGateResult instanceResult = g_checkScriptsOnly
        ? runtime::InstanceGateResult::Acquired
        : AcquireL2ServerInstanceGate(instanceHandle, instanceDiagnostic);
    if (instanceResult == runtime::InstanceGateResult::AlreadyRunning) {
        // Log.Add(
        //     LOG_ERROR,
        //     "L2SERVER.EXE is already running.")
        // L2_LOG_ERROR(Log,
        //     "L2SERVER.EXE is already running.")
        L2_LOG_ERROR(Log, "[startup] Another server instance holds the single-instance marker; startup stops.");
        runtime::CloseNamedInstanceGate(instanceHandle);
        return -1;
    }
    if (instanceResult == runtime::InstanceGateResult::Error) {
        // Log.Add(
        //     LOG_ERROR,
        //     "L2Server single-instance gate failed: %s",
        //     instanceDiagnostic.c_str())
        // L2_LOG_ERROR(Log,
        //     "L2Server single-instance gate failed: %s",
        //     instanceDiagnostic.c_str())
        L2_LOG_ERROR(
            Log, "[startup] Cannot acquire the single-instance marker; startup stops. Details: %s",
            instanceDiagnostic.c_str()
        );
        runtime::CloseNamedInstanceGate(instanceHandle);
        return -1;
    }

    RefreshDaylightSaving();
    g_serialKeyValidationEnabled = ReadSerialKeyValidationEnabled();
    if (!g_serialKeyValidationEnabled) {
        L2_LOG_INFO(
            Log,
            "[startup] Serial key validation is disabled (Portable/EnableSerialKeyValidation=false in l2server.ini)."
        );
    }
    if (g_serialKeyValidationEnabled && !HasL2ServerSerialKey()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "L2Server startup stopped: SerialKey registry value or "
        //     "portable SerialKey.txt is missing")
        // L2_LOG_ERROR(Log,
        //     "L2Server startup stopped: SerialKey registry value or "
        //     "portable SerialKey.txt is missing")
        L2_LOG_ERROR(
            Log,
            "[startup] Cannot read the SerialKey registry value or SerialKey.txt in the configured directory; startup stops."
        );
        runtime::CloseNamedInstanceGate(instanceHandle);
        return -1;
    }

    CThreadManager::SetThreadCount(GetIOThreadCount());
    g_bScriptSecureEnabled = ReadScriptSecureEnabled();

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] L2Server Server Start")
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] L2Server Server Start")
    L2_LOG_INFO(Log, u"[startup] Starting server initialization.");
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] [EnterWorldSerializer] "
    //     L"MaxSpawnNumPerOneTick : %d",
    //     g_nMaxSpawnNumPerOneTick)
    // Noise: This is printed before configuration is loaded and does not report the effective spawn limit.
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] [EnterWorldSerializer] "
    //     L"MaxSpawnNumPerOneTick : %d",
    //     g_nMaxSpawnNumPerOneTick)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] BuildDate : %s",
    //     CBuildDate::inst().BuildDateTime())
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] BuildDate : %s",
    //     CBuildDate::inst().BuildDateTime())
    L2_LOG_INFO(Log, u"[startup] Build date: %ls.", CBuildDate::inst().BuildDateTime());

    runtime::Config deploymentPaths;
    const auto configurationFile = runtime::ConfigFile("./l2server.ini");
    // if (deploymentPaths.Load(configurationFile.string(), false) &&
    //     !runtime::ConfigureResourceDirectories(deploymentPaths, configurationFile)) {
    //     runtime::CloseNamedInstanceGate(instanceHandle)
    //     return 2
    // }
    if (deploymentPaths.Load(configurationFile.string(), false)) {
        // Portable diagnostics, separate from the recovered CConfig tables
        // and from both later ConfigureLog publications.
        // const int debugLogging = deploymentPaths.GetInt("Logging", "Debug", 0)
        // runtime::log::SetDebugLoggingEnabled(debugLogging == 1)
        // if (debugLogging != 0 && debugLogging != 1) {
        //     L2_LOG_WARN(Log,
        //         "[config] Logging.Debug must be 0 or 1. Received %d. Debug logging remains disabled.",
        //         debugLogging)
        // }
        const auto debugLogging = deploymentPaths.Get("Logging", "Debug", "0");
        runtime::log::SetDebugLoggingEnabled(debugLogging == "1");
        if (debugLogging != "0" && debugLogging != "1") {
            L2_LOG_WARN(
                Log, "[config] Logging.Debug must be exactly 0 or 1. Invalid value; debug logging remains disabled."
            );
        }
        if (!runtime::ConfigureResourceDirectories(deploymentPaths, configurationFile)) {
            runtime::CloseNamedInstanceGate(instanceHandle);
            return 2;
        }
    }
    config.Load();

    static_cast<void>(CContainerDB::Inst());
    static_cast<void>(IssueManager::CreateInstance());

    StartupLoaderMutexLease loaderMutexes;
    l2server::StartupLoaderGateResult loaderGate;
    std::string loaderDiagnostic;
    // The check run keeps clear of the live server's loader markers.
    if (g_checkScriptsOnly) {
        loaderGate.proceedToWorldInitialization = true;
    } else if (!ObserveStartupLoaderGate(g_bUseResourceLoader, loaderMutexes, loaderGate, loaderDiagnostic)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "L2Server loader-mutex transport failed: %s",
        //     loaderDiagnostic.c_str())
        // L2_LOG_ERROR(Log,
        //     "L2Server loader-mutex transport failed: %s",
        //     loaderDiagnostic.c_str())
        L2_LOG_ERROR(
            Log, "[startup] Cannot inspect resource-loader markers; startup stops. Details: %s", loaderDiagnostic.c_str()
        );
        runtime::CloseNamedInstanceGate(instanceHandle);
        return -1;
    }
    if (!loaderGate.proceedToWorldInitialization) {
        // Log.Add(
        //     LOG_ERROR,
        //     "%s",
        //     StartupLoaderFailureMessage(loaderGate.failure))
        L2_LOG_ERROR(Log, "%s", StartupLoaderFailureMessage(loaderGate.failure));
        runtime::CloseNamedInstanceGate(instanceHandle);
        return -1;
    }

    CWorld& world = CWorld::Inst();
    world.InitGeo();
    if (world.GetGeo() == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "L2Server geodata allocation failed before startup "
        //     "publication")
        // L2_LOG_ERROR(Log,
        //     "L2Server geodata allocation failed before startup "
        //     "publication")
        L2_LOG_ERROR(Log, "[startup] Cannot allocate geodata storage; startup stops.");
        runtime::CloseNamedInstanceGate(instanceHandle);
        return -1;
    }

    CThreadManager::SetThisThreadAsMainThread();
    InitializeCurrentIOBufferPool(0x80);
    InitializeSharedFactory();
    ReportSmartIdManagerMemory();
    InitializeSharedVector();
    InitailizeThreadLocalMap();

    CCacheContextManager& cacheContext = CCacheContextManager::inst();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"CCacheContextManager::Inst [%d]",
    //     cacheContext.GetLastRegisteredSlot())
    // Noise: This pre-load internal slot/flag value is not a useful initialization result.
    // L2_LOG_INFO(Log,
    //     L"CCacheContextManager::Inst [%d]",
    //     cacheContext.GetLastRegisteredSlot())
    static_cast<void>(cacheContext);

    SharedUserMapDensity::Inst().Init();
    static_cast<void>(CInventory::GetLockManager());

    CClanHelpTable& clanHelpTable = CClanHelpTable::inst();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"CClanHelpTable::Inst [%d]",
    //     clanHelpTable.GetFlag(0, 0) ? 1 : 0)
    // Noise: This pre-load internal slot/flag value is not a useful initialization result.
    // L2_LOG_INFO(Log,
    //     L"CClanHelpTable::Inst [%d]",
    //     clanHelpTable.GetFlag(0, 0) ? 1 : 0)
    static_cast<void>(clanHelpTable);

    runtime::DisableCurrentProcessPriorityBoost();
    CVariableBitSet::InitBitmaskArray();
    InitializeAtomicPetDBJob();
    g_UserGC.Initialize();

    std::uint32_t readinessLastError = 0;
    if (!loaderMutexes.Create(
            L"_L2MUTEX_L2SERVER_IS_INITED_", "_L2MUTEX_L2SERVER_IS_INITED_", readinessLastError, loaderDiagnostic
        )) {
        // Log.Add(
        //     LOG_ERROR,
        //     "L2Server readiness-mutex transport failed: %s",
        //     loaderDiagnostic.c_str())
        // L2_LOG_ERROR(Log,
        //     "L2Server readiness-mutex transport failed: %s",
        //     loaderDiagnostic.c_str())
        L2_LOG_ERROR(
            Log, "[startup] Cannot create the initialization marker; startup stops. Details: %s", loaderDiagnostic.c_str()
        );
        runtime::CloseNamedInstanceGate(instanceHandle);
        return -1;
    }
    if (readinessLastError == 183 && !g_checkScriptsOnly) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Duplicated Server?! (l2server.exe mutex already exist)")
        // L2_LOG_ERROR(Log,
        //     "Duplicated Server?! (l2server.exe mutex already exist)")
        L2_LOG_ERROR(Log, "[startup] The server initialization marker already exists; startup stops.");
        runtime::CloseNamedInstanceGate(instanceHandle);
        return -1;
    }

    if (g_WatchList.ParseWatchList()) {
        // Log.Add(LOG_NORMAL, L"watch list loaded")
        // L2_LOG_INFO(Log, L"watch list loaded")
        L2_LOG_INFO(Log, u"[startup] Watch list loaded.");
    }

    // The current Win32 window/resource setup is a host-only presentation
    // boundary. The portable console publishes the same log/listener state
    // directly after the readiness marker.
    InitializeRandomNumber();
    Log.Set(nullptr);
    Log.Enable(true);
    // Log.Add(LOG_NORMAL, L"Starting ListenThread")
    // L2_LOG_INFO(Log, L"Starting ListenThread")
    L2_LOG_INFO(Log, u"[startup] Starting the listener initialization thread.");

    void* const listenerThread = runtime::CreateOwnedThread(&ListenThread, nullptr, false);
    if (listenerThread == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "cannot create L2Server listen thread")
        // L2_LOG_ERROR(Log,
        //     "cannot create L2Server listen thread")
        L2_LOG_ERROR(Log, "[startup] Cannot create the listener initialization thread; startup stops.");
        runtime::CloseNamedInstanceGate(instanceHandle);
        return -1;
    }
    CThreadManager::SetThread(ThreadID::LISTEN, listenerThread);
    server_up_time = runtime::crt::Time32Now();
    TestPacketTableRegistration();

    runtime::JoinOwnedThread(listenerThread);
    runtime::DestroyOwnedThread(listenerThread);
    CThreadManager::SetThread(ThreadID::LISTEN, nullptr);
    runtime::CloseNamedInstanceGate(instanceHandle);
    return -1;
}

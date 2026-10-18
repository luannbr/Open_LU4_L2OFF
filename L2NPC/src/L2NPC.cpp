// l2npc.cpp  -  process entry point for the reconstructed, cross-platform
// L2NPC. The nearby PDB assigns WinMain and the process-level helpers to
// d:\work\l2npc\l2npc.cpp.
//
// L2NPC is the AI/NPC server of Lineage II High Five. NPCServer::Connect creates
// one outbound, full-duplex CServerSocket to L2Server. That same socket carries
// both NPCServer::Send traffic and all 200 inbound callbacks. There is no
// independent listening socket in the original.
//
// The deliberately-different stack (POSIX sockets + std::thread instead of Win32
// IOCP, console instead of GUI) is the allowed exception. The wire protocol is
// byte-identical. Network/serialization/runtime/config come from Shared.
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "L2Core/src/inc/GuardProfile.h"
#include "L2NPC/src/AIManager.h"
#include "L2NPC/src/AirShipEventManager.h"
#include "L2NPC/src/Config.h"
#include "L2NPC/src/DBNPC.h"
#include "L2NPC/src/DBNPCSocket.h"
#include "L2NPC/src/Default.h"
#include "L2NPC/src/L2NPCStatusWnd.h"
#include "L2NPC/src/LottoGame.h"
#include "L2NPC/src/NPC.h"
#include "L2NPC/src/NPCServer.h"
#include "L2NPC/src/NpcMakerDB.h"
#include "L2NPC/src/ScriptTypeInfo.h"
#include "L2NPC/src/ServerSocket.h"
#include "NPCEventHandler.h"
#include "Portable/Console.h"
#include "Portable/CreatureSharedMemory.h"
#include "Portable/DBNPCConnection.h"
#include "Shared/src/Runtime/IOThreadRuntime.h"
#include "Portable/ItemSharedMemory.h"
#include "Shared/src/Runtime/L2ServerClient.h"
#include "Portable/NPCServerConfiguration.h"
#include "Portable/NPCServerConnection.h"
#include "Portable/NPCServerLifecycle.h"
#include "Portable/NPCServerRuntime.h"
#include "Portable/NPCServerThread.h"
#include "Portable/ProcessStartupGate.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Portable/SharedObjectSharedMemory.h"
#include "Shared/src/Runtime/SocketTransport.h"
#include "Portable/StaticObjectSharedMemory.h"
#include "Shared/src/Runtime/SystemMailTransport.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WaitThreadRuntime.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Protocol.h" // Shared: framing + opcode dispatch
#include "Runtime.h" // Shared: portable runtime
#include "Serialize.h" // Shared: Assemble/Disassemble
#include "Shared/src/CenteredSharedRWLock.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Inventory.h"
#include "Shared/src/Log.h"
#include "Shared/src/ManorData.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/ConfigDirectory.h"
#include "Shared/src/Runtime/GeoDataSource.h"
#include "Shared/src/Runtime/LogStorage.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/SharedFactoryMapping.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/ResidenceData.h"
#include "Shared/src/ShareDefine.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/Utils.h"
#include "Shared/src/World.h"
#include "TextRuntime.h"
#include "net/Config.h" // Shared: ini reader

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WS2tcpip.h>
#include <Windows.h>
#elif defined(__APPLE__)
#include <arpa/inet.h>
#include <mach-o/dyld.h>
#else
#include <arpa/inet.h>
#endif

namespace {

// File-local constants kept by L2NPC.obj.
const wchar_t* const LIN_ERROR_TXT = L"LinError.txt";

bool IsTrueSetting(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value == "true";
}

constexpr const char* DISABLED_SYSTEM_MAIL_SERVER = "disabled.invalid";
constexpr const char* DISABLED_SYSTEM_MAIL_RECIPIENT = "disabled@example.invalid";

bool HasExplicitSystemMailConfiguration(const std::string& server, const std::string& recipient) {
    return !server.empty() && !recipient.empty() && server != DISABLED_SYSTEM_MAIL_SERVER &&
        recipient != DISABLED_SYSTEM_MAIL_RECIPIENT;
}

std::string CacheServerAddressText() {
    std::array<char, INET_ADDRSTRLEN> text{};
#if defined(_WIN32)
    const char* const result = ::InetNtopA(AF_INET, &cacheServerAddr, text.data(), static_cast<DWORD>(text.size()));
#else
    const char* const result = ::inet_ntop(AF_INET, &cacheServerAddr, text.data(), text.size());
#endif
    return result ? std::string(result) : std::string{"255.255.255.255"};
}
const wchar_t* const CONFIG_FILE_NAME = L".\\l2npc.ini";
const wchar_t* const CONFIG_SECURE_FILE_NAME = L".\\secure.ini";

// Shared file inventory compiled in L2NPC.obj. The adjacent PDB records the
// older 89-entry revision. The shipping image contains this 140-entry table
// unchanged (including the paired basename entries used for generated PCHs).
const wchar_t* const g_DataScriptFile[] = {
    L"..\\Script\\airship.txt",
    L"..\\Script\\announce_sphere.txt",
    L"..\\Script\\areadata.txt",
    L"..\\Script\\ArmorEnchantBonusData.txt",
    L"..\\Script\\auctiondata.txt",
    L"..\\Script\\BuilderCmdAlias.txt",
    L"..\\Script\\castledata.txt",
    L"..\\Script\\castledata_world.txt",
    L"category_pch.txt",
    L"..\\Script\\category_pch.txt",
    L"..\\Script\\categorydata.txt",
    L"..\\Script\\convertdata.txt",
    L"..\\Script\\CubicData.txt",
    L"..\\Script\\cursedweapondata.txt",
    L"..\\Script\\decodata.txt",
    L"..\\Script\\doordata.txt",
    L"..\\Script\\Dyedata.txt",
    L"editer_pch.txt",
    L"..\\Script\\editer_pch.txt",
    L"..\\Script\\enchantOption.txt",
    L"..\\Script\\expdata.txt",
    L"..\\Script\\FieldCycle.txt",
    L"fishing_pch.txt",
    L"..\\Script\\fishing_pch.txt",
    L"..\\Script\\fishingdata.txt",
    L"..\\Script\\formationinfo.txt",
    L"..\\Script\\freewayInfo.txt",
    L"..\\Script\\instantzonedata.txt",
    L"item_pch.txt",
    L"..\\Script\\item_pch.txt",
    L"item_pch2.txt",
    L"..\\Script\\item_pch2.txt",
    L"Itemdata.txt",
    L"..\\Script\\Itemdata.txt",
    L"..\\Script\\itemdata_reload.txt",
    L"..\\Script\\setitemdata.txt",
    L"..\\Script\\maker_event_name_list.txt",
    L"..\\Script\\manordata.txt",
    L"manual_pch.txt",
    L"..\\Script\\manual_pch.txt",
    L"..\\Script\\minigame.txt",
    L"..\\Script\\monrace.txt",
    L"..\\Script\\multisell.txt",
    L"..\\Script\\Itemattrchange.txt",
    L"npc_pch.txt",
    L"..\\Script\\npc_pch.txt",
    L"..\\Script\\npcdata.txt",
    L"..\\Script\\npcdata_reload.txt",
    L"npcpos.txt",
    L"npcpos_world.txt",
    L"..\\Script\\npcpos.txt",
    L"..\\Script\\npcpos_world.txt",
    L"..\\Script\\npcpos_for_server.txt",
    L"option_pch.txt",
    L"..\\Script\\option_pch.txt",
    L"..\\Script\\optiondata.txt",
    L"..\\Script\\PC_parameter.txt",
    L"..\\Script\\petdata.txt",
    L"..\\Script\\PremiumService.txt",
    L"quest_pch.txt",
    L"..\\Script\\quest_pch.txt",
    L"quest_pch2.txt",
    L"..\\Script\\quest_pch2.txt",
    L"..\\Script\\questdata.txt",
    L"..\\Script\\questsystem.txt",
    L"..\\Script\\recipe.txt",
    L"..\\Script\\RestrictAreaData.txt",
    L"..\\Script\\setting.txt",
    L"..\\Script\\setting_test.txt",
    L"..\\Script\\setting_broadcast.txt",
    L"..\\Script\\elevatordata.txt",
    L"skill_pch.txt",
    L"..\\Script\\skill_pch.txt",
    L"skill_pch2.txt",
    L"..\\Script\\skill_pch2.txt",
    L"skill_pch3.txt",
    L"..\\Script\\skill_pch3.txt",
    L"..\\Script\\skillacquire.txt",
    L"..\\Script\\skilldata.txt",
    L"..\\Script\\skilldata_reload.txt",
    L"..\\Script\\skillenchantdata.txt",
    L"..\\Script\\superpointinfo.txt",
    L"..\\Script\\transform.txt",
    L"..\\Script\\TrapData.txt",
    L"..\\Script\\UserBasicAction.txt",
    L"..\\Script\\variationdata.txt",
    L"..\\Script\\vehicledata.txt",
    L"..\\Script\\watch_list.txt",
    L"..\\Script\\dynamicquest.txt",
    L"..\\Script\\flymovedata.txt",
    L"..\\Script\\InstantAgit.txt",
    L"..\\Script\\skillsynergydata.txt",
    L"..\\Script\\UserQuestData.txt",
    L"..\\Script\\IssueData.txt",
    L"..\\Script\\ex_drop.txt",
    L"..\\Script\\hairexdata.txt",
    L"..\\Script\\faceexdata.txt",
    L"..\\Script\\colorexdata.txt",
    L"..\\Script\\levelupdata.txt",
    L"..\\Script\\eventcampaign.txt",
    L"userquestdata_pch.txt",
    L"..\\Script\\userquestdata_pch.txt",
    L"..\\Script\\shapeshiftedsetitemdata.txt",
    L"..\\Script\\Localization\\itemdata.txt",
    L"..\\Script\\Localization\\multisell.txt",
    L"..\\Script\\Localization\\hairexdata.txt",
    L"..\\Script\\Localization\\faceexdata.txt",
    L"..\\Script\\Localization\\colorexdata.txt",
    L"..\\Script\\Localization\\npcpos.txt",
    L"..\\Script\\luckygamedata.txt",
    L"..\\Script\\pathtoawakening.txt",
    L"..\\Script\\card_collect_system.txt",
    L"..\\Script\\clanhelptable.dat",
    L"..\\Script\\dbjob.txt",
    L"..\\Script\\combinationitemdata.txt",
    L"..\\Script\\soulshoteffectdata.txt",
    L"..\\Script\\teleportquestpath.txt",
    L"..\\Script\\ratingpoint.txt",
    L"..\\Script\\alchemydata.txt",
    L"..\\Script\\honor.txt",
    L"..\\Script\\honor_pch.txt",
    L"honor_pch.txt",
    L"..\\Script\\WeaponEnchantValidateEx.txt",
    L"..\\Script\\ensouldata.txt",
    L"..\\Script\\ensouldata_pch.txt",
    L"ensouldata_pch.txt",
    L"..\\Script\\deconpcdata.txt",
    L"..\\Script\\factiondata.txt",
    L"..\\Script\\attendancedata.txt",
    L"..\\Script\\monsterbook_trophy.txt",
    L"..\\Script\\monsterbook.txt",
    L"predefinedlist_pch.txt",
    L"..\\Script\\predefinedlist_pch.txt",
    L"..\\Script\\predefinedlistdata.txt",
    L"..\\Script\\cardupdowngame.txt",
    L"..\\Script\\upgradesystem.txt",
    L"..\\Script\\PledgeMission.txt",
    L"..\\Script\\UseCountItem.txt",
    L"..\\Script\\ItemAnnounce.txt",
    L"..\\Script\\artifact.txt",
};
static_assert(sizeof(g_DataScriptFile) / sizeof(g_DataScriptFile[0]) == 140, "shipping g_DataScriptFile inventory changed");

volatile std::sig_atomic_t g_shutdownSignal = 0;

void RequestSignalShutdown(const int signalNumber) {
    // Keep the signal boundary async-signal-safe. The main/console thread
    // observes this flag and performs the cooperative shutdown itself.
    g_shutdownSignal = signalNumber;
}

bool SignalShutdownRequested() { return g_shutdownSignal != 0; }

std::filesystem::path GetExecutablePath() {
#if defined(_WIN32)
    std::array<wchar_t, 260> path{};
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size() - 1));
    if (length != 0 && length < path.size()) {
        return std::filesystem::path(path.data(), path.data() + length);
    }
#elif defined(__APPLE__)
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
    // GetModuleFileNameW leaves the target helper with a relative
    // LinError.txt path when it fails. An empty path lets RenameOldLog use
    // that same current-directory fallback without mistaking the directory
    // itself for an executable filename.
    return {};
}

} // namespace

// Original rollover body kept. The logging overlay below observes errors
// without changing the path, rename policy, fallback or startup continuation.
// void RenameOldLog() {
//     // Current L2NPC.exe 0x00458184. PDB l2npc.cpp:88-131.
//     const std::filesystem::path executablePath = GetExecutablePath()
//     std::error_code error
//     const std::filesystem::path directory =
//         executablePath.empty() ? std::filesystem::current_path(error) : executablePath.parent_path()
//     if (error)
//         return.
//     const std::filesystem::path logPath = directory / std::filesystem::path(LIN_ERROR_TXT)
//
//     const std::filesystem::file_status status = std::filesystem::status(logPath, error)
//     if (error || !std::filesystem::exists(status)) {
//         return.
//     }
//
//     const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(logPath, error)
//     if (error)
//         return.
//
//     const auto systemWriteTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
//         writeTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
//     )
//     const std::time_t timestamp = std::chrono::system_clock::to_time_t(systemWriteTime)
//     // _localtime64 is dereferenced without a null check at 0x00458253.
//     // std::localtime retains that failure boundary on every host.
//     const std::tm* const localTime = std::localtime(&timestamp)
//
//     std::array<wchar_t, 260> fileNameOld{}
//     std::wcsncpy(fileNameOld.data(), L"Unknown", fileNameOld.size() - 1)
//     const std::wstring logPathWide = logPath.wstring()
//     std::swprintf(
//         fileNameOld.data(), fileNameOld.size(), L"%ls.%04d-%02d-%02d-%02d-%02d-%02d.bak", logPathWide.c_str(),
//         localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour, localTime->tm_min,
//         localTime->tm_sec
//     )
//
//     const std::filesystem::path oldPath(fileNameOld.data())
//     error.clear()
//     std::filesystem::rename(logPath, oldPath, error)
// }
// MSVC rejects preprocessor directives inside macro arguments. Windows paths
// are wide, so the log format text is chosen here before the logging call.
#if defined(_WIN32)
#define L2NPC_PATH_LOG_TEXT(windowsText, otherText) windowsText
#else
#define L2NPC_PATH_LOG_TEXT(windowsText, otherText) otherText
#endif

void RenameOldLog() {
    // Current L2NPC.exe 0x00458184. PDB l2npc.cpp:88-131.
    const std::filesystem::path executablePath = GetExecutablePath();
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
    const std::filesystem::path logPath = directory / std::filesystem::path(LIN_ERROR_TXT);

    const std::filesystem::file_status status = std::filesystem::status(logPath, error);
    if (error || !std::filesystem::exists(status)) {
        const runtime::logging::StorageErrorState preserveErrors;
        // Noise: No previous crash report is an expected startup state.
        if (error && error != std::errc::no_such_file_or_directory) {
            L2_LOG_WARN(
                Log,
                L2NPC_PATH_LOG_TEXT(
                    L"[crash reports] Cannot read metadata for \"%ls\" (%hs error %d); backup skipped, startup continues.",
                    "[crash reports] Cannot read metadata for \"%s\" (%s error %d); backup skipped, startup continues."
                ),
                logPath.c_str(), error.category().name(), error.value()
            );
        }
        return;
    }

    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(logPath, error);
    if (error) {
        const runtime::logging::StorageErrorState preserveErrors;
        // Noise: No previous crash report is an expected startup state.
        if (error && error != std::errc::no_such_file_or_directory) {
            L2_LOG_WARN(
                Log,
                L2NPC_PATH_LOG_TEXT(
                    L"[crash reports] Cannot read the modification time of \"%ls\" (%hs error %d); backup skipped, "
                    L"startup continues.",
                    "[crash reports] Cannot read the modification time of \"%s\" (%s error %d); backup skipped, "
                    "startup continues."
                ),
                logPath.c_str(), error.category().name(), error.value()
            );
        }
        return;
    }

    const auto systemWriteTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        writeTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(systemWriteTime);
    // _localtime64 is dereferenced without a null check at 0x00458253.
    // std::localtime retains that failure boundary on every host.
    const std::tm* const localTime = std::localtime(&timestamp);

    std::array<wchar_t, 260> fileNameOld{};
    std::wcsncpy(fileNameOld.data(), L"Unknown", fileNameOld.size() - 1);
    const std::wstring logPathWide = logPath.wstring();
    const int backupFormatResult = std::swprintf(
        fileNameOld.data(), fileNameOld.size(), L"%ls.%04d-%02d-%02d-%02d-%02d-%02d.bak", logPathWide.c_str(),
        localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour, localTime->tm_min,
        localTime->tm_sec
    );
    if (backupFormatResult < 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(
            Log,
            L2NPC_PATH_LOG_TEXT(
                L"[crash reports] Cannot format the backup filename for \"%ls\" (errno %d); rename will still be "
                L"attempted.",
                "[crash reports] Cannot format the backup filename for \"%s\" (errno %d); rename will still be "
                "attempted."
            ),
            logPath.c_str(), preserveErrors.savedErrno
        );
    }

    const std::filesystem::path oldPath(fileNameOld.data());
    error.clear();
    std::filesystem::rename(logPath, oldPath, error);
    if (error) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(
            Log,
            L2NPC_PATH_LOG_TEXT(
                L"[crash reports] Cannot move \"%ls\" to \"%ls\" (%hs error %d); startup continues without completing "
                L"this backup.",
                "[crash reports] Cannot move \"%s\" to \"%s\" (%s error %d); startup continues without completing "
                "this backup."
            ),
            logPath.c_str(), oldPath.c_str(), error.category().name(), error.value()
        );
    }
}

void InitializeSharedFactory() {
    // L2NPC.exe 0x0045937c. PDB module L2NPC.obj, l2npc.cpp:136-145.
    // Inst() opens each factory's target-named mappings. The displayed size
    // includes the four-byte object-id index paired with every data record.
    constexpr double bytesToMegabytes = 1.0 / (1024.0 * 1024.0);

    CCreatureFactory::Inst();
    // Log.Add(
    //     LOG_NORMAL, L"CCreatureFactory            [%05.1f]MB",
    //     static_cast<double>(runtime::MultiplyInt32Wrapping(SharedDefine_Creature.max_data, 0x3edc)) * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "component=shared_memory event=factory_capacity factory=creature configured_records=%d
    //     data_and_index_mib=%.1f", SharedDefine_Creature.max_data, static_cast<double>(SharedDefine_Creature.max_data) *
    //     0x3edc * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "[shared memory] Creature capacity: %d records. Data and index require %.1f MiB.",
    //     SharedDefine_Creature.max_data,
    //     static_cast<double>(SharedDefine_Creature.max_data) * 0x3edc * bytesToMegabytes
    // )
    L2_LOG_INFO(
        Log, "[shared memory] Creature capacity: %d records; data and index require %.1f MiB.",
        SharedDefine_Creature.max_data, static_cast<double>(SharedDefine_Creature.max_data) * 0x3edc * bytesToMegabytes
    );

    CItemFactory::Inst();
    // Log.Add(
    //     LOG_NORMAL, L"CItemFactory                [%05.1f]MB",
    //     static_cast<double>(runtime::MultiplyInt32Wrapping(SharedDefine_Item.max_data, 0xa4)) * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "component=shared_memory event=factory_capacity factory=item configured_records=%d
    //     data_and_index_mib=%.1f", SharedDefine_Item.max_data, static_cast<double>(SharedDefine_Item.max_data) * 0xa4 *
    //     bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "[shared memory] Item capacity: %d records. Data and index require %.1f MiB.",
    //     SharedDefine_Item.max_data,
    //     static_cast<double>(SharedDefine_Item.max_data) * 0xa4 * bytesToMegabytes
    // )
    L2_LOG_INFO(
        Log, "[shared memory] Item capacity: %d records; data and index require %.1f MiB.", SharedDefine_Item.max_data,
        static_cast<double>(SharedDefine_Item.max_data) * 0xa4 * bytesToMegabytes
    );

    CStaticObjectFactory::Inst();
    // Log.Add(
    //     LOG_NORMAL, L"CStaticObjectFactory        [%05.1f]MB",
    //     static_cast<double>(runtime::MultiplyInt32Wrapping(SharedDefine_StaticObject.max_data, 0x94)) *
    //     bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "component=shared_memory event=factory_capacity factory=static_object configured_records=%d
    //     data_and_index_mib=%.1f", SharedDefine_StaticObject.max_data,
    //     static_cast<double>(SharedDefine_StaticObject.max_data) * 0x94 * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "[shared memory] Static-object capacity: %d records. Data and index require %.1f MiB.",
    //     SharedDefine_StaticObject.max_data,
    //     static_cast<double>(SharedDefine_StaticObject.max_data) * 0x94 * bytesToMegabytes
    // )
    L2_LOG_INFO(
        Log, "[shared memory] Static-object capacity: %d records; data and index require %.1f MiB.",
        SharedDefine_StaticObject.max_data, static_cast<double>(SharedDefine_StaticObject.max_data) * 0x94 * bytesToMegabytes
    );

    CPledgeFactory::Inst();
    // Log.Add(
    //     LOG_NORMAL, L"CPledgeFactory              [%05.1f]MB",
    //     static_cast<double>(runtime::MultiplyInt32Wrapping(SharedDefine_Pledge.max_data, 0x804)) * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "component=shared_memory event=factory_capacity factory=pledge configured_records=%d
    //     data_and_index_mib=%.1f", SharedDefine_Pledge.max_data, static_cast<double>(SharedDefine_Pledge.max_data) * 0x804
    //     * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "[shared memory] Pledge capacity: %d records. Data and index require %.1f MiB.",
    //     SharedDefine_Pledge.max_data,
    //     static_cast<double>(SharedDefine_Pledge.max_data) * 0x804 * bytesToMegabytes
    // )
    L2_LOG_INFO(
        Log, "[shared memory] Pledge capacity: %d records; data and index require %.1f MiB.", SharedDefine_Pledge.max_data,
        static_cast<double>(SharedDefine_Pledge.max_data) * 0x804 * bytesToMegabytes
    );

    CPartyFactory::Inst();
    // Log.Add(
    //     LOG_NORMAL, L"CPartyFactory               [%05.1f]MB",
    //     static_cast<double>(runtime::MultiplyInt32Wrapping(SharedDefine_Party.max_data, 0xb4)) * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "component=shared_memory event=factory_capacity factory=party configured_records=%d
    //     data_and_index_mib=%.1f", SharedDefine_Party.max_data, static_cast<double>(SharedDefine_Party.max_data) * 0xb4 *
    //     bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "[shared memory] Party capacity: %d records. Data and index require %.1f MiB.",
    //     SharedDefine_Party.max_data,
    //     static_cast<double>(SharedDefine_Party.max_data) * 0xb4 * bytesToMegabytes
    // )
    L2_LOG_INFO(
        Log, "[shared memory] Party capacity: %d records; data and index require %.1f MiB.", SharedDefine_Party.max_data,
        static_cast<double>(SharedDefine_Party.max_data) * 0xb4 * bytesToMegabytes
    );

    CAllianceFactory::Inst();
    // Log.Add(
    //     LOG_NORMAL, L"CAllianceFactory            [%05.1f]MB",
    //     static_cast<double>(runtime::MultiplyInt32Wrapping(SharedDefine_Alliance.max_data, 0x11c)) * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "component=shared_memory event=factory_capacity factory=alliance configured_records=%d
    //     data_and_index_mib=%.1f", SharedDefine_Alliance.max_data, static_cast<double>(SharedDefine_Alliance.max_data) *
    //     0x11c * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "[shared memory] Alliance capacity: %d records. Data and index require %.1f MiB.",
    //     SharedDefine_Alliance.max_data,
    //     static_cast<double>(SharedDefine_Alliance.max_data) * 0x11c * bytesToMegabytes
    // )
    L2_LOG_INFO(
        Log, "[shared memory] Alliance capacity: %d records; data and index require %.1f MiB.",
        SharedDefine_Alliance.max_data, static_cast<double>(SharedDefine_Alliance.max_data) * 0x11c * bytesToMegabytes
    );

    CGeneralObjectFactory::Inst();
    // Log.Add(
    //     LOG_NORMAL, L"CGeneralObjectFactory       [%05.1f]MB",
    //     static_cast<double>(runtime::MultiplyInt32Wrapping(SharedDefine_GeneralObject.max_data, 0x11c)) *
    //     bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "component=shared_memory event=factory_capacity factory=general_object configured_records=%d
    //     data_and_index_mib=%.1f", SharedDefine_GeneralObject.max_data,
    //     static_cast<double>(SharedDefine_GeneralObject.max_data) * 0x11c * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "[shared memory] General-object capacity: %d records. Data and index require %.1f MiB.",
    //     SharedDefine_GeneralObject.max_data,
    //     static_cast<double>(SharedDefine_GeneralObject.max_data) * 0x11c * bytesToMegabytes
    // )
    L2_LOG_INFO(
        Log, "[shared memory] General-object capacity: %d records; data and index require %.1f MiB.",
        SharedDefine_GeneralObject.max_data,
        static_cast<double>(SharedDefine_GeneralObject.max_data) * 0x11c * bytesToMegabytes
    );

    CAirShipFactory::Inst();
    // Log.Add(
    //     LOG_NORMAL, L"CAirShipFactory             [%05.1f]MB",
    //     static_cast<double>(runtime::MultiplyInt32Wrapping(SharedDefine_AirShip.max_data, 0x5c)) * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "component=shared_memory event=factory_capacity factory=airship configured_records=%d
    //     data_and_index_mib=%.1f", SharedDefine_AirShip.max_data, static_cast<double>(SharedDefine_AirShip.max_data) * 0x5c
    //     * bytesToMegabytes
    // )
    // Log.Add(
    //     LOG_NORMAL, "[shared memory] Airship capacity: %d records. Data and index require %.1f MiB.",
    //     SharedDefine_AirShip.max_data,
    //     static_cast<double>(SharedDefine_AirShip.max_data) * 0x5c * bytesToMegabytes
    // )
    L2_LOG_INFO(
        Log, "[shared memory] Airship capacity: %d records; data and index require %.1f MiB.", SharedDefine_AirShip.max_data,
        static_cast<double>(SharedDefine_AirShip.max_data) * 0x5c * bytesToMegabytes
    );
}

void InitializeSharedVector() {
    // L2NPC.exe 0x00458314. PDB module L2NPC.obj, l2npc.cpp:148-166.
    // Keep the original call and diagnostic order: some of the Init methods
    // only announce an already producer-owned mapping.
    NpcSpawnInfo::Inst()->Init();
    CSharedAgitDecoInfo::Inst()->Init();
    SharedCleftInfo::Inst()->Init();
    SharedEventInfo::Inst()->Init();

    SharedNoblessInfoVector& noblessInfo = SharedNoblessInfo::Inst();
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.AddT(LOG_NORMAL, L"[%s] Init", noblessInfo.GetName())
    (void)noblessInfo;

    SharedPlatformInfo::Inst()->Init();

    CManorData& manorData = CManorData::Inst(0);
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.Add(LOG_NORMAL, L"CManorData Init. [%d]", manorData.m_nManorId)
    (void)manorData;

    CResidenceData& residenceData = CResidenceData::Inst(0);
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.Add(LOG_NORMAL, L"CResidenceData Init. [%x]", runtime::PointerLow32(&residenceData))
    (void)residenceData;

    SharedFieldCycleInfo* fieldCycleInfo = SharedFieldCycleInfo::GetInfo(0);
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.Add(LOG_NORMAL, L"SharedFieldCycleInfo Init. [%d]", fieldCycleInfo->cycleID)
    (void)fieldCycleInfo;

    SharedSSQInfo* ssqInfo = SharedSSQInfo::GetInfo();
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.Add(LOG_NORMAL, L"SharedSSQInfo Init. [%x]", runtime::PointerLow32(ssqInfo))
    (void)ssqInfo;

    // 004583e6 zeroes EDX and passes ECX=1.
    SharedTimeAttackInfo* timeAttackInfo = SharedTimeAttackInfo::GetInfo(1, 0);
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.Add(LOG_NORMAL, L"SharedTimeAttackInfo Init. [%x]", runtime::PointerLow32(timeAttackInfo))
    (void)timeAttackInfo;

    SharedRimPointInfo* rimPointInfo = SharedRimPointInfo::GetInfo();
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.Add(LOG_NORMAL, L"SharedRimPointInfo Init. [%x]", runtime::PointerLow32(rimPointInfo))
    (void)rimPointInfo;

    SharedOlympiadInfo* olympiadInfo = SharedOlympiadInfo::GetInfo();
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.Add(LOG_NORMAL, L"SharedOlympiadInfo Init. [%x]", runtime::PointerLow32(olympiadInfo))
    (void)olympiadInfo;

    SharedPledgeGameInfo* pledgeGameInfo = SharedPledgeGameInfo::GetInfo();
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.Add(LOG_NORMAL, L"SharedPledgeGameInfo Init. [%x]", runtime::PointerLow32(pledgeGameInfo))
    (void)pledgeGameInfo;

    SharedPlatformInfo* platformInfo = SharedPlatformInfo::Inst();
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.Add(LOG_NORMAL, L"SharedPlatformInfo Init. [%x]", runtime::PointerLow32(platformInfo))
    (void)platformInfo;

    SharedCastleInfo* castleInfo = SharedCastleInfo::GetInfo(0);
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.Add(LOG_NORMAL, L"SharedCastleInfo Init. [%x]", runtime::PointerLow32(castleInfo))
    (void)castleInfo;

    SharedRaidServerData* raidServerData = SharedRaidServerData::GetInfo();
    // Noise: Startup identity/address dump does not report a readiness result or a failure.
    // Log.Add(LOG_NORMAL, L"SharedRaidServerData Init. [%x]", runtime::PointerLow32(raidServerData))
    (void)raidServerData;
}

#if defined(_WIN32)

HINSTANCE hInst = nullptr;
wchar_t szTitle[100]{};
wchar_t szWindowClass[100]{};

void MessageLoop_common(HACCEL accelerator) {
    // L2NPC.exe 0x00457d7c. PDB L2NPC.obj, l2npc.cpp:60-72.
    // The shipping body leaves MSG uninitialized before GetMessageW. Its
    // historical -1 path therefore continues with whatever fields Windows
    // left in the record instead of receiving a portable zero fallback.
    MSG message;
    while (::GetMessageW(&message, nullptr, 0, 0)) {
        if (!::TranslateAcceleratorW(message.hwnd, accelerator, &message)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }
}

void MessageLoop_mailing(HACCEL accelerator) {
    // The adjacent PDB contains the exception-mailing wrapper. In the
    // shipping image (0x00457dd4) the wrapper has collapsed to this call.
    MessageLoop_common(accelerator);
}

INT_PTR About(HWND dialog, UINT message, WPARAM wordParameter, LPARAM) {
    // L2NPC.exe 0x00457e18. PDB l2npc.cpp:685-700, resource dialog 103.
    if (message == WM_INITDIALOG) {
        return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(wordParameter) == IDOK || LOWORD(wordParameter) == IDCANCEL)) {
        ::EndDialog(dialog, LOWORD(wordParameter));
        return TRUE;
    }
    return FALSE;
}

BOOL InitInstance(HINSTANCE instance, int showCommand) {
    // L2NPC.exe 0x004584a4. PDB L2NPC.obj, l2npc.cpp:351-415.
    hInst = instance;

    int x = 100;
    int y = 100;
    int width = 640;
    int height = 480;
    HKEY key;
    DWORD size = sizeof(int);
    DWORD type;
    ::RegCreateKeyW(HKEY_LOCAL_MACHINE, L"Software\\PROJECT_L2\\L2NPC", &key);
    ::RegQueryValueExW(key, L"posx", nullptr, &type, reinterpret_cast<BYTE*>(&x), &size);
    ::RegQueryValueExW(key, L"posy", nullptr, &type, reinterpret_cast<BYTE*>(&y), &size);
    ::RegQueryValueExW(key, L"sizex", nullptr, &type, reinterpret_cast<BYTE*>(&width), &size);
    ::RegQueryValueExW(key, L"sizey", nullptr, &type, reinterpret_cast<BYTE*>(&height), &size);
    ::RegCloseKey(key);

    HWND window = ::CreateWindowExW(
        0, szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, x, y, width, height, nullptr, nullptr, instance, nullptr
    );

    LegacyCrtSrand(static_cast<std::uint32_t>(runtime::crt::Time32Now()));
    LARGE_INTEGER seed;
    ::QueryPerformanceCounter(&seed);
    init_genrand64(static_cast<std::uint64_t>(seed.QuadPart));

    Log.Set(reinterpret_cast<HWND__*>(window));
    if (!window) {
        return FALSE;
    }

    Log.Enable(true);
    // Noise: Logger initialization is extra with the window/startup lifecycle messages.
    // Log.Add(LOG_NORMAL, "log started")
    // Log.Add(LOG_ERROR, "[NO_ERROR] L2NPC Server Start")
    // Log.Add(LOG_NORMAL, "component=startup event=window_initialized")
    // Log.Add(LOG_NORMAL, "[startup] Window initialized.")
    L2_LOG_INFO(Log, "[startup] Window initialized.");
    ::ShowWindow(window, showCommand);
    ::UpdateWindow(window);

    L2NPCStatusWnd::Inst().AttachToMainWnd(
        reinterpret_cast<HINSTANCE__*>(instance), reinterpret_cast<HWND__*>(window), showCommand
    );

    HMENU menu = ::GetMenu(window);
    ::CheckMenuItem(menu, 0x800e, MF_BYCOMMAND | (g_bTraceNpcAI ? MF_CHECKED : MF_UNCHECKED));
    ::CheckMenuItem(menu, 0x800d, MF_BYCOMMAND | (g_bTraceGlobalObjectAI ? MF_CHECKED : MF_UNCHECKED));
    return TRUE;
}

INT_PTR DirectCommandBox(HWND dialog, UINT message, WPARAM wordParameter, LPARAM) {
    // L2NPC.exe 0x00458700. PDB L2NPC.obj, l2npc.cpp:431-470.
    L2_GUARD_FUNCTION();

    if (message == WM_INITDIALOG) {
        return TRUE;
    }
    if (message != WM_COMMAND) {
        return FALSE;
    }

    const WORD commandId = LOWORD(wordParameter);
    if (commandId != IDOK && commandId != IDCANCEL) {
        return FALSE;
    }

    if (commandId == IDOK) {
        // The target reserves 0x5000 WCHARs on the stack even though the
        // dialog read itself is capped at 64 characters.
        std::array<wchar_t, 0x5000> command{};
        ::SendDlgItemMessageW(dialog, 1000, WM_GETTEXT, 64, reinterpret_cast<LPARAM>(command.data()));

        char16_t* token = nullptr;
        GetLeftString(reinterpret_cast<char16_t*>(command.data()), &token);
        const wchar_t* const firstToken = reinterpret_cast<const wchar_t*>(token);

        if (std::wcscmp(firstToken, L"Crash") == 0) {
            g_IsBySystemManager = true;
            ForceCrash(CrashInfo::BY_CRASH_COMMAND);
        } else if (std::wcscmp(firstToken, L"ShowClass") == 0) {
            CNPCAIManager::ShowAllNpcAI();
        }
    }

    ::EndDialog(dialog, commandId);
    return TRUE;
}

INT_PTR TimerOverflowLimit(HWND dialog, UINT message, WPARAM wordParameter, LPARAM) {
    // L2NPC.exe 0x004588e0. PDB l2npc.cpp:703-745. Edit control
    // 1001 belongs to dialog resource 0x8015.
    constexpr int editControl = 1001;
    if (message == WM_INITDIALOG) {
        wchar_t value[64]{};
        _itow_s(g_TimerOverflowLimit, value, std::size(value), 10);
        ::SendDlgItemMessageW(dialog, editControl, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(value));
        return TRUE;
    }
    if (message != WM_COMMAND) {
        return FALSE;
    }

    if (LOWORD(wordParameter) == IDOK) {
        wchar_t value[64]{};
        ::SendDlgItemMessageW(dialog, editControl, WM_GETTEXT, std::size(value), reinterpret_cast<LPARAM>(value));
        g_TimerOverflowLimit = _wtoi(value);
        config.SaveTimerOverflow();
    } else if (LOWORD(wordParameter) != IDCANCEL) {
        return TRUE;
    }

    ::EndDialog(dialog, LOWORD(wordParameter));
    return TRUE;
}

INT_PTR TimerOverflowCheckTick(HWND dialog, UINT message, WPARAM wordParameter, LPARAM) {
    // L2NPC.exe 0x004589f4. PDB l2npc.cpp:748-790. Edit control
    // 1002 belongs to dialog resource 138.
    constexpr int editControl = 1002;
    if (message == WM_INITDIALOG) {
        wchar_t value[64]{};
        _itow_s(g_TimerOverflowCheckTick, value, std::size(value), 10);
        ::SendDlgItemMessageW(dialog, editControl, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(value));
        return TRUE;
    }
    if (message != WM_COMMAND) {
        return FALSE;
    }

    if (LOWORD(wordParameter) == IDOK) {
        wchar_t value[64]{};
        ::SendDlgItemMessageW(dialog, editControl, WM_GETTEXT, std::size(value), reinterpret_cast<LPARAM>(value));
        g_TimerOverflowCheckTick = _wtoi(value);
        config.SaveTimerOverflow();
    } else if (LOWORD(wordParameter) != IDCANCEL) {
        return TRUE;
    }

    ::EndDialog(dialog, LOWORD(wordParameter));
    return TRUE;
}

namespace {

void CrashFromWndProc(CrashInfo::ReasonType reason, const std::source_location location = std::source_location::current()) {
    // The source used the same crash macro at five WndProc branches.
    g_IsBySystemManager = true;
    ForceCrash(reason, location);
}

void SetMenuCheck(HMENU menu, UINT command, bool checked) {
    ::CheckMenuItem(menu, command, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

} // namespace

LRESULT WndProc(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter) {
    // Current L2NPC.exe 0x0045bb2c. The adjacent PDB assigns WndProc to
    // L2NPC.obj/l2npc.cpp:483-681. Commands added after that PDB are taken
    // directly from the shipping image.
    wchar_t hello[100];
    ::LoadStringW(hInst, 106, hello, static_cast<int>(std::size(hello)));
    HMENU const menu = ::GetMenu(window);

    switch (message) {
        case WM_CREATE:
            g_ns.Start();
            return 0;

        case WM_DESTROY:
            CrashFromWndProc(CrashInfo::BY_DESTROY_EVENT);
            return 0;

        case WM_MOVE: {
            // The shipping image passes the stack object without assigning
            // WINDOWINFO::cbSize first. Preserve that source-level quirk.
            WINDOWINFO windowInfo;
            ::GetWindowInfo(window, &windowInfo);
            if ((windowInfo.dwStyle & 0x21000000U) == 0) {
                HKEY key;
                ::RegCreateKeyW(HKEY_LOCAL_MACHINE, L"Software\\PROJECT_L2\\L2NPC", &key);
                const DWORD dataSize = sizeof(int);
                ::RegSetValueExW(
                    key, L"posx", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&windowInfo.rcWindow.left), dataSize
                );
                ::RegSetValueExW(
                    key, L"posy", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&windowInfo.rcWindow.top), dataSize
                );
                const int width = windowInfo.rcWindow.right - windowInfo.rcWindow.left;
                const int height = windowInfo.rcWindow.bottom - windowInfo.rcWindow.top;
                ::RegSetValueExW(key, L"sizex", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&width), dataSize);
                ::RegSetValueExW(key, L"sizey", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&height), dataSize);
                ::RegCloseKey(key);
            }
            Log.Redraw();
            return 0;
        }

        case WM_SIZE:
            L2NPCStatusWnd::Inst().OnMainWndResized(
                static_cast<std::uint64_t>(wordParameter), static_cast<std::int64_t>(longParameter)
            );
            return 0;

        case WM_PAINT:
            Log.Redraw();
            return 0;

        case WM_QUIT:
            CrashFromWndProc(CrashInfo::BY_QUIT_MENU);
            break;

        case WM_KEYDOWN:
            if (wordParameter == VK_F2 && g_bEnableDirectCommand) {
                ::DialogBoxParamW(hInst, MAKEINTRESOURCEW(136), window, DirectCommandBox, 0);
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wordParameter)) {
                case 0x8003:
                    CrashFromWndProc(CrashInfo::BY_EXIT_MENU);
                    return 0;

                case 0x68:
                    ::DialogBoxParamW(hInst, MAKEINTRESOURCEW(103), window, About, 0);
                    return 0;

                case 0x69:
                    CrashFromWndProc(CrashInfo::BY_SHUTDOWN_MENU);
                    return 0;

                case 0x81:
                    g_Timer.Report(LOG_NORMAL);
                    return 0;

                case 0x82:
                    g_Timer.Reset();
                    return 0;

                case 0x84:
                    // Log.Add(
                    //     LOG_ERROR, L"queued timer count, %I64d",
                    //     static_cast<long long>(g_nTimerCount.load(std::memory_order_seq_cst))
                    // )
                    L2_LOG_INFO(
                        Log, "[timers] Menu-requested report: %lld queued timers.",
                        static_cast<long long>(g_nTimerCount.load(std::memory_order_seq_cst))
                    );
                    return 0;

                case 0x85:
                    g_bSilentWhenNoUser = !g_bSilentWhenNoUser;
                    SetMenuCheck(menu, 0x85, g_bSilentWhenNoUser);
                    return 0;

                case 0x8004:
                    ReportInstance();
                    return 0;

                case 0x8005:
                    ReportMemory();
                    return 0;

                case 0x8006:
                case 0x8007: {
                    const bool replaceClass = LOWORD(wordParameter) == 0x8007;
                    g_nNPCSpawnCount.exchange(0, std::memory_order_seq_cst);
                    // Log.Add(LOG_ERROR, replaceClass ? L"IDM_AI_RELOAD_LIVE" : L"IDM_AI_RELOAD_DEV")
                    // Previous: L2_LOG_ERROR(Log, replaceClass ? L"IDM_AI_RELOAD_LIVE" : L"IDM_AI_RELOAD_DEV")
                    L2_LOG_INFO(
                        Log,
                        replaceClass ? L"[AI reload] Reloading live AI classes."
                                     : L"[AI reload] Reloading development AI classes."
                    );
                    CNPCAIManager::LoadNPCAI(replaceClass);
                    // Log.Add(
                    //     LOG_ERROR,
                    //     replaceClass ? L"after  reload: queued timer "
                    //                    L"count, %I64d, %d"
                    //                  : L"after  reload: queued timer "
                    //                    L"count, %I64d, spawn count: %d",
                    //     static_cast<long long>(g_nTimerCount.load(std::memory_order_seq_cst)),
                    //     g_nNPCSpawnCount.load(std::memory_order_seq_cst)
                    // )
                    // Previous: L2_LOG_ERROR(Log,
                    //     replaceClass ? L"after  reload: queued timer "
                    //                    L"count, %I64d, %d"
                    //                  : L"after  reload: queued timer "
                    //                    L"count, %I64d, spawn count: %d",
                    //     static_cast<long long>(g_nTimerCount.load(std::memory_order_seq_cst)),
                    //     g_nNPCSpawnCount.load(std::memory_order_seq_cst)
                    // )
                    L2_LOG_INFO(
                        Log, "[AI reload] Completed %s reload: %lld queued timers, %d spawned NPCs.",
                        replaceClass ? "live" : "development",
                        static_cast<long long>(g_nTimerCount.load(std::memory_order_seq_cst)),
                        g_nNPCSpawnCount.load(std::memory_order_seq_cst)
                    );
                    return 0;
                }

                case 0x8008:
                    Log.ConnectToLogD();
                    return 0;

                case 0x8009:
                    CrashFromWndProc(CrashInfo::BY_CRASH_REPORT);
                    return 0;

                case 0x800a:
                    return 0;

                case 0x800d:
                    g_bTraceGlobalObjectAI = !g_bTraceGlobalObjectAI;
                    SetMenuCheck(menu, 0x800d, g_bTraceGlobalObjectAI);
                    return 0;

                case 0x800e:
                    g_bTraceNpcAI = !g_bTraceNpcAI;
                    SetMenuCheck(menu, 0x800e, g_bTraceNpcAI);
                    return 0;

                case 0x800f:
                    g_bShowMallocLog = !g_bShowMallocLog;
                    SetMenuCheck(menu, 0x800f, g_bShowMallocLog);
                    return 0;

                case 0x8010:
                    GuardProfileHelper::ResetGuardProfile();
                    return 0;

                case 0x8011:
                    ReportGuardProfile();
                    return 0;

                case 0x8014:
                    ::DialogBoxParamW(hInst, MAKEINTRESOURCEW(0x8015), window, TimerOverflowLimit, 0);
                    return 0;

                case 0x8016:
                    ::DialogBoxParamW(hInst, MAKEINTRESOURCEW(138), window, TimerOverflowCheckTick, 0);
                    return 0;

                case 0x8017:
                    g_bShowStatusWindow = !g_bShowStatusWindow;
                    SetMenuCheck(menu, 0x8017, g_bShowStatusWindow);
                    if (g_bShowStatusWindow) {
                        L2NPCStatusWnd::Inst().Show();
                    } else {
                        L2NPCStatusWnd::Inst().Hide();
                    }
                    return 0;

                case 0x8018:
                    // l2npc::ServerToNpcPacketStat::Inst().Report(LOG_ERROR)
                    l2npc::ServerToNpcPacketStat::Inst().Report(LOG_NORMAL);
                    // l2npc::NpcToServerPacketStat::Inst().Report(LOG_ERROR)
                    l2npc::NpcToServerPacketStat::Inst().Report(LOG_NORMAL);
                    return 0;

                case 0x8019:
                    l2npc::ServerToNpcPacketStat::Inst().Reset();
                    l2npc::NpcToServerPacketStat::Inst().Reset();
                    return 0;

                case 0x801a:
                    // l2npc::ServerToNpcPacketStat::Inst().ReportRank(LOG_ERROR, l2npc::ServerToNpcPacketStat::PRED_CNT)
                    l2npc::ServerToNpcPacketStat::Inst().ReportRank(LOG_NORMAL, l2npc::ServerToNpcPacketStat::PRED_CNT);
                    // l2npc::ServerToNpcPacketStat::Inst().ReportRank(LOG_ERROR, l2npc::ServerToNpcPacketStat::PRED_BYTES)
                    l2npc::ServerToNpcPacketStat::Inst().ReportRank(LOG_NORMAL, l2npc::ServerToNpcPacketStat::PRED_BYTES);
                    // l2npc::NpcToServerPacketStat::Inst().ReportRank(LOG_ERROR, l2npc::NpcToServerPacketStat::PRED_CNT)
                    l2npc::NpcToServerPacketStat::Inst().ReportRank(LOG_NORMAL, l2npc::NpcToServerPacketStat::PRED_CNT);
                    // l2npc::NpcToServerPacketStat::Inst().ReportRank(LOG_ERROR, l2npc::NpcToServerPacketStat::PRED_BYTES)
                    l2npc::NpcToServerPacketStat::Inst().ReportRank(LOG_NORMAL, l2npc::NpcToServerPacketStat::PRED_BYTES);
                    return 0;

                case 0x801b:
                    g_bSetReportNpcEventCount = !g_bSetReportNpcEventCount;
                    SetMenuCheck(menu, 0x801b, g_bSetReportNpcEventCount);
                    return 0;

                default:
                    break;
            }
            break;

        case WM_SYSCOMMAND:
            if (wordParameter == SC_MINIMIZE) {
                ::ShowWindow(window, SW_SHOWMINIMIZED);
                return 0;
            }
            break;

        default:
            break;
    }

    return ::DefWindowProcW(window, message, wordParameter, longParameter);
}

ATOM MyRegisterClass(HINSTANCE instance) {
    // Current L2NPC.exe 0x0045c558. PDB l2npc.cpp:319-338.
    WNDCLASSEXW windowClass;
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = instance;
    windowClass.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(107));
    windowClass.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszMenuName = MAKEINTRESOURCEW(109);
    windowClass.lpszClassName = szWindowClass;
    windowClass.hIconSm = ::LoadIconW(instance, MAKEINTRESOURCEW(108));
    return ::RegisterClassExW(&windowClass);
}

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCommand) {
    // Current L2NPC.exe 0x0045c5e4. Adjacent PDB l2npc.cpp:172-302.

    server_up_time = runtime::crt::Time32Now();
    InitializeExceptionRuntime();

    const wchar_t* errorMessage = nullptr;
    ::CreateMutexW(nullptr, TRUE, L"Global\\_L2NPC_EXE_");
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        errorMessage = L"L2NPC.EXE is already running.";
    }

    if (!errorMessage) {
        ::CreateMutexW(nullptr, TRUE, L"Global\\_L2SERVER_NPC_REMOVING_");
        if (::GetLastError() == ERROR_ALREADY_EXISTS) {
            errorMessage = L"L2Server's NPC is Removing!!!!";
        }
    }

    if (errorMessage) {
        ::MessageBoxW(nullptr, errorMessage, L"Error", 0);
        return -1;
    }

    RenameOldLog();
    config.Load();

    CThreadManager::SetThisThreadAsMainThread();
    SYSTEM_INFO systemInfo{};
    ::GetSystemInfo(&systemInfo);
    CThreadManager::SetThreadCount(std::min(8, static_cast<int>(systemInfo.dwNumberOfProcessors) / 2));
    InitializeCurrentIOBufferPool(0x40);

    std::array<wchar_t, 1024> secureSetting{};
    ::GetPrivateProfileStringW(
        L"Setting", L"ScriptSecureEnabled", L"true", secureSetting.data(), static_cast<DWORD>(secureSetting.size()),
        CONFIG_SECURE_FILE_NAME
    );
    g_bScriptSecureEnabled = ::_wcsicmp(secureSetting.data(), L"true") == 0;

    std::array<wchar_t, 1000> settingBuffer{};
    mailServer = static_cast<char*>(std::malloc(1000));
    ::GetPrivateProfileStringW(L"Setting", L"MailServer", L"mail.ncsoft.co.kr", settingBuffer.data(), 999, CONFIG_FILE_NAME);
    UnicodeToAnsi(reinterpret_cast<char16_t*>(settingBuffer.data()), 1000, mailServer, 1000);

    // Portable deployment safety: the recovered executable's implicit
    // defaults point at NCSoft infrastructure. Preserve those target values
    // above, but do not permit any DNS lookup or TCP connection unless the
    // operator supplies all three settings.
    std::array<wchar_t, 16> enableSystemMail{};
    std::array<wchar_t, 1000> explicitMailServer{};
    std::array<wchar_t, 1024> explicitMailTo{};
    ::GetPrivateProfileStringW(
        L"Setting", L"EnableSystemMail", L"false", enableSystemMail.data(), static_cast<DWORD>(enableSystemMail.size()),
        CONFIG_FILE_NAME
    );
    ::GetPrivateProfileStringW(
        L"Setting", L"MailServer", L"", explicitMailServer.data(), static_cast<DWORD>(explicitMailServer.size()),
        CONFIG_FILE_NAME
    );
    ::GetPrivateProfileStringW(
        L"Setting", L"MailTo", L"", explicitMailTo.data(), static_cast<DWORD>(explicitMailTo.size()), CONFIG_FILE_NAME
    );
    const bool enableExternalSystemMail = ::_wcsicmp(enableSystemMail.data(), L"true") == 0 &&
        explicitMailServer[0] != L'\0' && explicitMailTo[0] != L'\0' &&
        ::_wcsicmp(explicitMailServer.data(), L"disabled.invalid") != 0 &&
        ::_wcsicmp(explicitMailTo.data(), L"disabled@example.invalid") != 0;
    if (enableExternalSystemMail) {
        std::array<char, 0x800> explicitMailToAnsi{};
        UnicodeToAnsi(
            reinterpret_cast<char16_t*>(explicitMailTo.data()), static_cast<int>(explicitMailTo.size()),
            explicitMailToAnsi.data(), static_cast<int>(explicitMailToAnsi.size())
        );
        SystemMail::SetReceivers(explicitMailToAnsi.data());
    }
    runtime::ConfigureSystemMailNetwork(enableExternalSystemMail);

    const wchar_t* const defaultNpcPosFile =
        g_L2ServerType == L2ServerType::WORLDSERVER ? L"npcpos_world.txt" : L"npcpos.txt";
    ::GetPrivateProfileStringW(L"Setting", L"NpcPosFile", defaultNpcPosFile, settingBuffer.data(), 999, CONFIG_FILE_NAME);
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    g_ns.m_NpcPosFile.assign(reinterpret_cast<const char16_t*>(settingBuffer.data()));

    g_nIsEventServer = ::GetPrivateProfileIntW(L"Setting", L"IsEventServer", 0, CONFIG_FILE_NAME);

    ScriptTypeTable::inst()->Initialize(false);
    // EXE 0x004191b8 is a linked three-byte no-op in this build.

    if (!g_bUseResourceLoader) {
        ::OpenMutexW(0, TRUE, L"_L2MUTEX_NOT_USE_LOADER_");
        if (::GetLastError() == ERROR_FILE_NOT_FOUND) {
            errorMessage = L"L2Server is not using loader now. "
                           L"([L2NPC.ini] - [Setting] - "
                           L"UseResourceLoader=false)";
        } else {
            ::OpenMutexW(0, TRUE, L"_L2MUTEX_L2SERVER_IS_INITED_");
            if (::GetLastError() == ERROR_FILE_NOT_FOUND) {
                errorMessage = L"L2Server's SharedMemory "
                               L"does not inited.";
            }
        }
    } else {
        ::OpenMutexW(0, TRUE, L"Global\\_L2MUTEX_USE_LOADER_");
        if (::GetLastError() == ERROR_FILE_NOT_FOUND) {
            errorMessage = L"L2Server is using loader now. "
                           L"([L2NPC.ini] - [Setting] - "
                           L"UseResourceLoader=true)";
        } else {
            ::OpenMutexW(0, TRUE, L"_L2MUTEX_L2SERVER_IS_INITED_");
            if (::GetLastError() == ERROR_FILE_NOT_FOUND) {
                errorMessage = L"L2Server's SharedMemory "
                               L"is not inited.";
            }
        }
    }

    if (errorMessage) {
        ::MessageBoxW(nullptr, errorMessage, L"Error", 0);
        return -1;
    }

    InitializeSharedFactory();
    InitializeSharedVector();

    CSharedRWLock* const firstSharedLock = CInventory::GetLockManager().GetLock(0);
    // Noise: An internal lock address is not useful operational startup information.
    // Log.Add(LOG_NORMAL, "First SharedLock pointer = 0x%x", runtime::PointerLow32(firstSharedLock))
    (void)firstSharedLock;

    SharedUserMapDensity::Inst().Init();

    const unsigned int airShipSid = CAirShipEventManager::Inst().GetAirShipSID(0);
    // Noise: The initial airship ID does not report manager readiness or a failure.
    // Log.Add(LOG_NORMAL, "CAirShipEventManager. %d", static_cast<int>(airShipSid))
    (void)airShipSid;

    l2npc::ServerToNpcPacketStat::Inst().Init(L"ServerToNpc");
    l2npc::NpcToServerPacketStat::Inst().Init(L"NpcToServer");

    // Noise: The local startup marker also reaches the console. A second record duplicates it.
    // Log.Add(LOG_NORMAL, "L2NPC Server Start")
    // Log.Add(LOG_LOCAL, "L2NPC Server Start")
    // Log.Add(LOG_LOCAL, "component=startup event=shared_state_phase_finished")
    // Log.Add(LOG_LOCAL, "[startup] Shared-state initialization phase finished.")
    L2_LOG(Log, LOG_LOCAL, "[startup] Shared-state initialization phase finished.");

    l2geo::CWorld::Inst().InitGeo();
    CThreadManager::SetThisThreadAsMainThread();

    ::LoadStringW(instance, 105, szTitle, static_cast<int>(std::size(szTitle)));
    ::LoadStringW(instance, 109, szWindowClass, static_cast<int>(std::size(szWindowClass)));
    MyRegisterClass(instance);

    if (!InitInstance(instance, showCommand)) {
        return 0;
    }

    HACCEL const accelerator = ::LoadAcceleratorsW(instance, MAKEINTRESOURCEW(109));
    if (ExceptionMailing()) {
        MessageLoop_mailing(accelerator);
    } else {
        MessageLoop_common(accelerator);
    }
    return -1;
}

#endif

// Cross-platform process composition root. The recovered target class remains
// in L2NPC/NPCServer.{h,cpp}, and default.cpp owns process-global g_ns. Host
// transports and cooperative teardown stay outside that target layout. The
// recovered WinMain above is kept as target evidence. The CMake product
// enters here on Windows as well so these required bindings are
// not omitted from the runnable server.
struct PortableNPCServerHost {
    explicit PortableNPCServerHost(l2npc::portable::ProcessStartupGate& gate) : startupGate(gate) {}

    l2npc::portable::ProcessStartupGate& startupGate;
    uint32_t bootTick = 0;
    runtime::Config cfg;
    l2npc::portable::NPCServerConfigurationBinding npcServerConfiguration;
    network::L2ServerClient l2s{"l2server"};
    network::L2ServerClient cache{"cached"};
    l2npc::NPCServerLifecycle lifecycle;
    l2npc::portable::NPCServerThreadBinding npcServerThread;
    l2npc::portable::NPCServerRuntimeBinding npcServerRuntime;
    portable::WaitThreadRuntimeBinding waitThreadRuntime;
    bool npcServerThreadCreated = false;
    // Portable process-composition state. None of these fields are part of
    // NPCServer, CServerSocket, CDBNPC or CDBNPCSocket target layouts.
    l2npc::portable::NPCServerConnectionBinding l2ServerConnectionTransport;
    l2npc::portable::DBNPCConnectionBinding cacheDConnectionTransport;
    portable::SocketTransportBinding l2ServerSocketTransport;
    portable::SocketTransportBinding cacheDSocketTransport;
    IOThreadRuntime ioThreadRuntime;
    std::recursive_mutex transportMutex;
    l2npc::CServerSocket* serverSocket = nullptr;
    l2npc::CDBNPCSocket* dbNpcSocket = nullptr;
    bool ioInstalled = false;
    bool l2ServerConnected = false;
    std::uint64_t l2ServerGeneration = 0;
    bool cacheDConnected = false;
    std::uint64_t cacheDGeneration = 0;
    // Current L2NPC.exe owns one process-global CLog.
    CLog& logd = Log;
    int worldId = 1;
    std::string l2sHost = "127.0.0.1";
    int l2sPort = 2002; // l2npc.ini [server] port = 0x7D2
    std::string cacheHost = "127.0.0.1";
    int cachePort = 12008; // original default 0x2EE8
    std::atomic<bool> fatalError{false};
    std::once_flag shutdownOnce;

    ~PortableNPCServerHost() { Shutdown(); }

    bool StopRequested() const { return lifecycle.StopRequested(); }

    bool WaitForRetryOrStop() { return lifecycle.WaitForStopFor(std::chrono::seconds(1)); }

    l2npc::NPCServerLifecycleState LifecycleState() const { return lifecycle.State(); }

    bool Ready() const { return LifecycleState() == l2npc::NPCServerLifecycleState::Ready; }

    void ConfigureTransportBindings() {
        l2ServerConnectionTransport.Reset(
            [this](const char* address, int port) -> l2npc::portable::NPCServerTransportConnection {
                {
                    std::lock_guard guard(transportMutex);
                    if (serverSocket || l2ServerConnected) {
                        return {};
                    }
                }

                network::L2ServerClient::ConnectOptions options;
                // NPCServer::Connect at current 0x004fed14 calls inet_addr,
                // binds INADDR_ANY:0, and performs a blocking connect before
                // constructing CServerSocket.
                options.startReceiving = false;
                options.blocking = true;
                options.numericIPv4Only = true;
                options.bindEphemeralIPv4 = true;
                const network::SocketConnectResult connectResult = l2s.ConnectWithResult(address ? address : "", port, options);
                if (!connectResult) {
                    l2npc::portable::NPCServerTransportConnection connection;
                    connection.connectResult = connectResult;
                    return connection;
                }

                std::uint64_t generation = 0;
                {
                    std::lock_guard<std::recursive_mutex> guard(transportMutex);
                    if (serverSocket || l2ServerConnected) {
                        return {};
                    }
                    l2ServerConnected = true;
                    generation = ++l2ServerGeneration;
                }

                l2npc::portable::NPCServerTransportConnection connection;
                connection.socket = generation;
                connection.connectResult = connectResult;
                connection.abortSink = [this, generation] {
                    bool closePeer = false;
                    {
                        std::lock_guard<std::recursive_mutex> guard(transportMutex);
                        if (l2ServerGeneration != generation || serverSocket) {
                            return;
                        }
                        l2ServerConnected = false;
                        closePeer = true;
                    }
                    if (closePeer) {
                        l2s.RejectPeerProtocol();
                    }
                };
                connection.attachSink = [this, generation](CSocket& baseSocket) {
                    auto* const socket = dynamic_cast<l2npc::CServerSocket*>(&baseSocket);
                    if (!socket) {
                        return false;
                    }
                    {
                        std::lock_guard<std::recursive_mutex> guard(transportMutex);
                        if (!l2ServerConnected || l2ServerGeneration != generation || serverSocket) {
                            return false;
                        }
                    }
                    l2ServerSocketTransport.Reset(
                        socket,
                        [this,
                            generation](const std::uint8_t* data, int length, portable::SocketWriteCompletion completion) {
                            {
                                std::lock_guard<std::recursive_mutex> guard(transportMutex);
                                if (!l2ServerConnected || l2ServerGeneration != generation) {
                                    SetSocketLastError(10038);
                                    return portable::SocketWriteSubmission::Failed();
                                }
                            }
                            return l2s.SubmitRawWrite(data, length, std::move(completion));
                        },
                        [this, generation] {
                            {
                                std::lock_guard<std::recursive_mutex> guard(transportMutex);
                                if (l2ServerGeneration != generation) {
                                    return;
                                }
                            }
                            l2s.RejectPeerProtocol();
                        },
                        [this, generation] {
                            std::lock_guard<std::recursive_mutex> guard(transportMutex);
                            if (!l2ServerConnected || l2ServerGeneration != generation) {
                                return;
                            }
                            l2s.InitializeTargetSocket();
                        },
                        [this, generation] {
                            {
                                std::lock_guard<std::recursive_mutex> guard(transportMutex);
                                if (l2ServerGeneration != generation) {
                                    return;
                                }
                            }
                            l2s.RejectPeerProtocol(false);
                        },
                        [this, generation](std::size_t capacity) {
                            std::lock_guard<std::recursive_mutex> guard(transportMutex);
                            if (!l2ServerConnected || l2ServerGeneration != generation) {
                                return false;
                            }
                            l2s.SetNextReadCapacity(static_cast<int>(capacity));
                            return true;
                        }
                    );
                    {
                        std::lock_guard<std::recursive_mutex> guard(transportMutex);
                        if (!l2ServerConnected || l2ServerGeneration != generation || serverSocket) {
                            l2ServerSocketTransport.Reset();
                            return false;
                        }
                        serverSocket = socket;
                    }
                    return true;
                };
                return connection;
            }
        );

        cacheDConnectionTransport.Reset([this]() -> l2npc::portable::DBNPCTransportConnection {
            network::L2ServerClient::ConnectOptions options;
            // CDBNPC::InitDBNPC at current 0x0041ccf0 consumes the
            // numeric inet_addr result stored by CConfig, binds an
            // ephemeral local IPv4 endpoint and blocks in connect.
            options.startReceiving = false;
            options.blocking = true;
            options.numericIPv4Only = true;
            options.bindEphemeralIPv4 = true;
            // Unlike NPCServer::Connect, current CDBNPC::InitDBNPC does
            // not close its SOCKET after bind/connect failure.
            options.abandonSocketOnBindOrConnectFailure = true;
            const network::SocketConnectResult connectResult = cache.ConnectWithResult(cacheHost, cachePort, options);
            if (!connectResult) {
                l2npc::portable::DBNPCTransportConnection connection;
                connection.endpointHost = cacheHost;
                connection.endpointPort = static_cast<std::uint16_t>(cachePort);
                connection.connectResult = connectResult;
                return connection;
            }

            std::uint64_t generation = 0;
            {
                std::lock_guard<std::recursive_mutex> guard(transportMutex);
                cacheDConnected = true;
                generation = ++cacheDGeneration;
            }
            l2npc::portable::DBNPCTransportConnection connection;
            connection.endpointHost = cacheHost;
            connection.endpointPort = static_cast<std::uint16_t>(cachePort);
            connection.socket = generation;
            connection.connectResult = connectResult;
            connection.attachSink = [this, generation](l2npc::CDBNPCSocket& socket) {
                {
                    std::lock_guard<std::recursive_mutex> guard(transportMutex);
                    if (!cacheDConnected || cacheDGeneration != generation || dbNpcSocket) {
                        return false;
                    }
                }
                cacheDSocketTransport.Reset(
                    &socket,
                    [this, generation](const std::uint8_t* data, int length, portable::SocketWriteCompletion completion) {
                        {
                            std::lock_guard<std::recursive_mutex> guard(transportMutex);
                            if (!cacheDConnected || cacheDGeneration != generation) {
                                SetSocketLastError(10038);
                                return portable::SocketWriteSubmission::Failed();
                            }
                        }
                        return cache.SubmitRawWrite(data, length, std::move(completion));
                    },
                    [this, generation] {
                        {
                            std::lock_guard<std::recursive_mutex> guard(transportMutex);
                            if (cacheDGeneration != generation) {
                                return;
                            }
                        }
                        cache.RejectPeerProtocol();
                    },
                    [this, generation] {
                        std::lock_guard<std::recursive_mutex> guard(transportMutex);
                        if (!cacheDConnected || cacheDGeneration != generation) {
                            return;
                        }
                        cache.InitializeTargetSocket();
                    },
                    [this, generation] {
                        {
                            std::lock_guard<std::recursive_mutex> guard(transportMutex);
                            if (cacheDGeneration != generation) {
                                return;
                            }
                        }
                        cache.RejectPeerProtocol(false);
                    },
                    [this, generation](std::size_t capacity) {
                        std::lock_guard<std::recursive_mutex> guard(transportMutex);
                        if (!cacheDConnected || cacheDGeneration != generation) {
                            return false;
                        }
                        cache.SetNextReadCapacity(static_cast<int>(capacity));
                        return true;
                    }
                );
                {
                    std::lock_guard<std::recursive_mutex> guard(transportMutex);
                    if (!cacheDConnected || cacheDGeneration != generation || dbNpcSocket) {
                        cacheDSocketTransport.Reset();
                        return false;
                    }
                    dbNpcSocket = &socket;
                }
                return true;
            };
            return connection;
        });
    }

    void InstallIORuntime() {
        std::lock_guard<std::recursive_mutex> guard(transportMutex);
        if (ioInstalled) {
            return;
        }
        ioThreadRuntime.Install();
        ioInstalled = true;
    }

    void StartIOThreads(int threadCount, IOThreadRuntime::CallbackFactory callbackFactory) {
        if (ioThreadRuntime.Running()) {
            return;
        }
        {
            std::lock_guard<std::recursive_mutex> guard(transportMutex);
            if (!ioInstalled) {
                throw std::logic_error("CreateIOThread2 requires InitIOThread");
            }
        }
        ioThreadRuntime.Start(threadCount, std::move(callbackFactory));
    }

    void StopIORuntime() {
        {
            std::lock_guard<std::recursive_mutex> guard(transportMutex);
            l2ServerConnected = false;
            ioInstalled = false;
        }
        ioThreadRuntime.Uninstall();
    }

    bool FeedL2ServerData(const std::uint8_t* data, std::size_t length) {
        ::CSPointer<l2npc::CServerSocket> socket;
        {
            std::lock_guard<std::recursive_mutex> guard(transportMutex);
            if (serverSocket) {
                socket.SetObject(serverSocket, ::ObjectRefType::TemporaryUse);
            }
        }
        if (!socket) {
            return false;
        }
        auto* const ioSocket = static_cast<CIOSocket*>(socket.get());
        if (!portable::FeedSocketRead(ioSocket, data, length)) {
            return false;
        }
        return true;
    }

    bool FeedCacheDData(const std::uint8_t* data, std::size_t length) {
        ::CSPointer<l2npc::CDBNPCSocket> socket;
        {
            std::lock_guard<std::recursive_mutex> guard(transportMutex);
            if (dbNpcSocket) {
                socket.SetObject(dbNpcSocket, ::ObjectRefType::TemporaryUse);
            }
        }
        if (!socket) {
            return false;
        }
        auto* const ioSocket = static_cast<CIOSocket*>(socket.get());
        if (!portable::FeedSocketRead(ioSocket, data, length)) {
            return false;
        }
        return true;
    }

    void OnL2ServerDisconnected(int readError) {
        ::CSPointer<l2npc::CServerSocket> socket;
        {
            std::lock_guard<std::recursive_mutex> guard(transportMutex);
            if (serverSocket) {
                socket.SetObject(serverSocket, ::ObjectRefType::TemporaryUse);
            }
        }
        if (socket) {
            portable::FeedSocketTerminal(static_cast<CIOSocket*>(socket.get()), readError);
        }
        {
            std::lock_guard<std::recursive_mutex> guard(transportMutex);
            l2ServerConnected = false;
        }
    }

    void OnCacheDDisconnected(int readError) {
        ::CSPointer<l2npc::CDBNPCSocket> socket;
        {
            std::lock_guard<std::recursive_mutex> guard(transportMutex);
            if (dbNpcSocket) {
                socket.SetObject(dbNpcSocket, ::ObjectRefType::TemporaryUse);
            }
        }
        if (socket) {
            portable::FeedSocketTerminal(static_cast<CIOSocket*>(socket.get()), readError);
        }
        {
            std::lock_guard<std::recursive_mutex> guard(transportMutex);
            cacheDConnected = false;
        }
    }

    void ReleaseRecoveredSocketOwners() {
        portable::CancelTimersForOwner(reinterpret_cast<CIOObject*>(&l2npc::g_NpcMakerDB));

        ::CSPointer<l2npc::CServerSocket> retainedServerSocket;
        {
            std::lock_guard<std::recursive_mutex> guard(transportMutex);
            if (serverSocket) {
                retainedServerSocket.SetObject(serverSocket, ::ObjectRefType::TemporaryUse);
            }
            ++l2ServerGeneration;
            l2ServerConnected = false;
            serverSocket = nullptr;
        }
        l2ServerSocketTransport.Reset();
        if (retainedServerSocket) {
            // Cooperative host shutdown has joined the native reader and
            // therefore owns cancellation of its outstanding WSARecv keep.
            portable::CancelSocketReads(retainedServerSocket.get());
            portable::CancelTimersForOwner(retainedServerSocket.get());
            L2_RELEASE(retainedServerSocket, ::ObjectRefType::Anonymous);
            L2_RELEASE(retainedServerSocket, ::ObjectRefType::CreateDelete);
        }

        ::CSPointer<l2npc::CDBNPCSocket> retainedDbSocket;
        {
            std::lock_guard<std::recursive_mutex> guard(transportMutex);
            if (dbNpcSocket) {
                retainedDbSocket.SetObject(dbNpcSocket, ::ObjectRefType::TemporaryUse);
            }
            ++cacheDGeneration;
            cacheDConnected = false;
            dbNpcSocket = nullptr;
        }
        cacheDSocketTransport.Reset();
        if (retainedDbSocket) {
            portable::CancelSocketReads(retainedDbSocket.get());
            L2_RELEASE(retainedDbSocket, ::ObjectRefType::CreateDelete);
        }

        portable::CancelTimersForOwner(&l2npc::dbnpc);
        portable::CancelTimersForOwner(&l2npc::g_LottoGameManager);
        l2ServerConnectionTransport.Reset();
        cacheDConnectionTransport.Reset();
    }

    bool PreparePortableRuntime() {
        if (StopRequested()) {
            return false;
        }
        bootTick = runtime::GetTickCount();
        // Noise: four decorative banner lines are replaced by one startup event below.
        // Log.Add(LOG_NORMAL, "========================================")
        // Log.Add(LOG_NORMAL, " L2NPC (reconstructed, cross-platform)")
        // Log.Add(LOG_NORMAL, " Lineage II High Five / CT26_P4  (AI/NPC server)")
        // Log.Add(LOG_NORMAL, "========================================")
        // Log.Add(LOG_NORMAL, "component=startup event=starting service=npc")
        // Log.Add(LOG_NORMAL, "[startup] Starting the NPC service.")
        L2_LOG_INFO(Log, "[startup] Starting the NPC service.");

        // NPCServer::Load returns false immediately when .\l2npc.ini cannot
        // be read. Keep an environment override for portable deployment,
        // but keep the target's mandatory-config startup boundary.
        const char* iniEnv = std::getenv("L2NPC_INI");
        std::string iniPath = runtime::HasConfigDirectory() ? runtime::ConfigFile("l2npc.ini").string()
                                                               : (iniEnv ? iniEnv : "l2npc.ini");
        if (!cfg.Load(iniPath)) {
            // Log.Add(LOG_ERROR, "Cannot read required configuration file %s.", iniPath.c_str())
            // Log.Add(LOG_ERROR, "component=config event=load_failed path=\"%s\", startup aborted", iniPath.c_str())
            // Log.Add(LOG_ERROR, "[config] Could not load \"%s\". Startup aborted.", iniPath.c_str())
            L2_LOG_ERROR(Log, "[config] Could not load \"%s\". Startup aborted.", iniPath.c_str());
            return false;
        }
        if (!runtime::ConfigureResourceDirectories(cfg, iniPath)) {
            return false;
        }
        // Log.Add(LOG_NORMAL, "Loaded config: %s", iniPath.c_str())
        // Log.Add(LOG_NORMAL, "component=config event=loaded path=\"%s\"", iniPath.c_str())
        // Noise: runtime::Config::Load already reports this path and the key count. No new load occurs here.
        // Log.Add(LOG_NORMAL, "[config] Loaded \"%s\".", iniPath.c_str())

        // WinMain loads the process-global CConfig while the initial TLS
        // thread id is still zero. It then publishes the MAIN role and the
        // active I/O-worker count before constructing the main thread's
        // default buffer pool. Keep that ordering here. Several target
        // containers consult CThreadManager long before IOThreadRuntime is
        // started.
        npcServerConfiguration.Reset(&cfg);
        ::config.Load();
        CThreadManager::SetThisThreadAsMainThread();
        CThreadManager::SetThreadCount(::GetIOThreadCount());
        InitializeCurrentIOBufferPool(0x40);

        // Current L2NPC.exe reads this immediately after CConfig::Load with
        // GetPrivateProfileStringW(..., "true", ".\\secure.ini"). A missing
        // file therefore enables the .l2enc path.
        std::string scriptSecureValue = "true";
        const std::filesystem::path secureIniPath = runtime::ConfigFile("secure.ini");
        std::error_code secureIniError;
        if (std::filesystem::exists(secureIniPath, secureIniError)) {
            runtime::Config secureConfiguration;
            if (secureConfiguration.Load(secureIniPath.string())) {
                scriptSecureValue = secureConfiguration.Get("Setting", "ScriptSecureEnabled", "true");
            }
        }
        std::transform(
            scriptSecureValue.begin(), scriptSecureValue.end(), scriptSecureValue.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); }
        );
        g_bScriptSecureEnabled = scriptSecureValue == "true";

        std::u16string mailServerWide = Utf8ToUtf16(cfg.Get("Setting", "MailServer", "mail.ncsoft.co.kr"));
        if (mailServerWide.size() > 998U) {
            mailServerWide.resize(998U);
        }
        mailServer = static_cast<char*>(std::malloc(1000));
        UnicodeToAnsi(mailServerWide.data(), 1000, mailServer, 1000);

        const std::string explicitMailServer = cfg.Get("Setting", "MailServer", "");
        const std::string explicitMailTo = cfg.Get("Setting", "MailTo", "");
        const bool enableExternalSystemMail = IsTrueSetting(cfg.Get("Setting", "EnableSystemMail", "false")) &&
            HasExplicitSystemMailConfiguration(explicitMailServer, explicitMailTo);
        if (enableExternalSystemMail) {
            SystemMail::SetReceivers(explicitMailTo.c_str());
        }
        runtime::ConfigureSystemMailNetwork(enableExternalSystemMail);

        const char* const defaultNpcPosFile =
            g_L2ServerType == L2ServerType::WORLDSERVER ? "npcpos_world.txt" : "npcpos.txt";
        std::u16string npcPosFile = Utf8ToUtf16(cfg.Get("Setting", "NpcPosFile", defaultNpcPosFile));
        if (npcPosFile.size() > 998U) {
            npcPosFile.resize(998U);
        }
        g_ns.m_NpcPosFile.assign(npcPosFile);
        g_nIsEventServer = cfg.GetInt("Setting", "IsEventServer", 0);

        worldId = g_nWorldId;
        l2sHost = cfg.Get("server", "address", "127.0.0.1");
        l2sPort = cfg.GetInt("server", "port", 2002);
        cacheHost = CacheServerAddressText();
        cachePort = static_cast<std::uint16_t>(cacheServerPort);

        // Current L2NPC.exe calls ScriptTypeTable::Initialize(false) here,
        // after configuration/secure.ini and before any shared-state or AI
        // startup. Registration is two-phase: all table slots
        // exist before the per-class virtual Initialize calls begin.
        ScriptTypeTable::inst()->Initialize(false);

        std::string l2ServerGateDiagnostic;
        switch (startupGate.CheckL2ServerState(g_bUseResourceLoader, &l2ServerGateDiagnostic)) {
            case l2npc::portable::L2ServerStartupState::ResourceLoaderModeMismatch:
                // Log.Add(
                //     LOG_ERROR, "%s",
                //     g_bUseResourceLoader ? "L2Server is using loader now. "
                //                            "([L2NPC.ini] - [Setting] - "
                //                            "UseResourceLoader=true)"
                //                          : "L2Server is not using loader now. "
                //                            "([L2NPC.ini] - [Setting] - "
                //                            "UseResourceLoader=false)"
                // )
                // Log.Add(
                //     LOG_ERROR, "component=startup event=loader_marker_missing peer=l2server expected_resource_loader=%s.
                //     startup aborted. Check L2Server startup and Setting.UseResourceLoader", g_bUseResourceLoader ? "true"
                //     : "false"
                // )
                // Log.Add(
                //     LOG_ERROR, "[startup] The L2Server loader marker is missing (expected resource loader: %s). Startup
                //     aborted. Check L2Server startup and Setting.UseResourceLoader.", g_bUseResourceLoader ? "true" :
                //     "false"
                // )
                L2_LOG_ERROR(
                    Log,
                    "[startup] The L2Server loader marker is missing (expected resource loader: %s). Startup aborted. Check "
                    "L2Server startup and Setting.UseResourceLoader.",
                    g_bUseResourceLoader ? "true" : "false"
                );
                return false;
            case l2npc::portable::L2ServerStartupState::SharedMemoryNotInitialized:
                // Log.Add(
                //     LOG_ERROR, "%s",
                //     g_bUseResourceLoader ? "L2Server's SharedMemory is not inited." : "L2Server's SharedMemory does not
                //     inited."
                // )
                // Log.Add(LOG_ERROR, "component=startup event=shared_memory_marker_missing peer=l2server, startup aborted
                // start L2Server and wait for shared-memory initialization"), Log.Add(LOG_ERROR, "[startup] The L2Server
                // shared-memory marker is missing. Startup aborted. Start L2Server and wait for shared-memory
                // initialization.")
                L2_LOG_ERROR(
                    Log,
                    "[startup] The L2Server shared-memory marker is missing. Startup aborted. Start L2Server and wait for "
                    "shared-memory initialization."
                );
                return false;
            case l2npc::portable::L2ServerStartupState::Ready:
                break;
        }
        if (!l2ServerGateDiagnostic.empty()) {
            // WinMain only treats ERROR_FILE_NOT_FOUND as absence and
            // continues after every other OpenMutex error.
            // Log.Add(LOG_WARN, "L2Server startup mutex warning: %s", l2ServerGateDiagnostic.c_str())
            // Log.Add(
            //     LOG_WARN, "component=startup event=peer_marker_check_warning peer=l2server reason=\"%s\". Startup
            //     continues", l2ServerGateDiagnostic.c_str()
            // )
            // Log.Add(
            //     LOG_WARN, "[startup] Could not verify the L2Server marker: %s. Startup continues.",
            //     l2ServerGateDiagnostic.c_str()
            // )
            L2_LOG_WARN(
                Log, "[startup] Could not verify the L2Server marker: %s. Startup continues.", l2ServerGateDiagnostic.c_str()
            );
        }

        if (StopRequested()) {
            return false;
        }

        InitializeSharedFactory();

        // CNPC::Set binds the first CreatePacket dword directly to this
        // 120000-entry factory. The data and object-id index mappings are
        // separate in the original CCreatureFactory.
        const char* creatureDataMemoryEnv = std::getenv("L2NPC_CREATURE_DATA_SHM");
        const char* creatureIndexMemoryEnv = std::getenv("L2NPC_CREATURE_INDEX_SHM");
        const std::string creatureDataMemoryName = creatureDataMemoryEnv && *creatureDataMemoryEnv
            ? creatureDataMemoryEnv
            : cfg.Get("SharedMemory", "CreatureData", kCreatureDataSharedMemoryName);
        const std::string creatureIndexMemoryName = creatureIndexMemoryEnv && *creatureIndexMemoryEnv
            ? creatureIndexMemoryEnv
            : cfg.Get("SharedMemory", "CreatureIndex", kCreatureIndexSharedMemoryName);
        std::string creatureMemoryError;
        CCreatureFactory& creatureFactory = CCreatureFactory::Inst();
        const bool creatureMemoryReady = (creatureDataMemoryName == kCreatureDataSharedMemoryName &&
                                             creatureIndexMemoryName == kCreatureIndexSharedMemoryName &&
                                             runtime::SharedFactoryMappingAccess::HasView(creatureFactory)) ||
            AttachCreatureSharedMemory(
                creatureFactory, creatureDataMemoryName, creatureIndexMemoryName, &creatureMemoryError
            );
        if (creatureMemoryReady) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "Mapped live creature factory %s / %s "
            //     "(120000 records, 0x3ed8 bytes each).",
            //     creatureDataMemoryName.c_str(), creatureIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "component=shared_memory event=factory_attached factory=creature data=\"%s\" index=\"%s\"",
            //     creatureDataMemoryName.c_str(), creatureIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "[shared memory] Attached creature storage: data \"%s\", index \"%s\".",
            //     creatureDataMemoryName.c_str(), creatureIndexMemoryName.c_str()
            // )
            L2_LOG_INFO(
                Log, "[shared memory] Attached creature storage: data \"%s\", index \"%s\".", creatureDataMemoryName.c_str(),
                creatureIndexMemoryName.c_str()
            );
        } else {
            // Log.Add(LOG_ERROR, "Required creature shared factory is unavailable: %s.", creatureMemoryError.c_str())
            // Log.Add(
            //     LOG_ERROR, "component=shared_memory event=factory_unavailable factory=creature reason=\"%s\". Startup
            //     aborted", creatureMemoryError.c_str()
            // )
            // Log.Add(
            //     LOG_ERROR, "[shared memory] Creature storage is unavailable: %s. Startup aborted.",
            //     creatureMemoryError.c_str()
            // )
            L2_LOG_ERROR(
                Log, "[shared memory] Creature storage is unavailable: %s. Startup aborted.", creatureMemoryError.c_str()
            );
            return false;
        }

        // These are three distinct CSharedFactory specializations in the
        // target. Their diagnostic sizes are record bytes plus the separate
        // four-byte index entry: 0xb0+4, 0x800+4, and 0x118+4.
        const char* partyDataMemoryEnv = std::getenv("L2NPC_PARTY_DATA_SHM");
        const char* partyIndexMemoryEnv = std::getenv("L2NPC_PARTY_INDEX_SHM");
        const std::string partyDataMemoryName = partyDataMemoryEnv && *partyDataMemoryEnv
            ? partyDataMemoryEnv
            : cfg.Get("SharedMemory", "PartyData", kPartyDataSharedMemoryName);
        const std::string partyIndexMemoryName = partyIndexMemoryEnv && *partyIndexMemoryEnv
            ? partyIndexMemoryEnv
            : cfg.Get("SharedMemory", "PartyIndex", kPartyIndexSharedMemoryName);
        std::string partyMemoryError;
        CPartyFactory& partyFactory = CPartyFactory::Inst();
        const bool partyMemoryReady =
            (partyDataMemoryName == kPartyDataSharedMemoryName && partyIndexMemoryName == kPartyIndexSharedMemoryName &&
                runtime::SharedFactoryMappingAccess::HasView(partyFactory)) ||
            AttachPartySharedMemory(partyFactory, partyDataMemoryName, partyIndexMemoryName, &partyMemoryError);
        if (partyMemoryReady) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "Mapped live party factory %s / %s "
            //     "(10003 records, 0xb0 bytes each).",
            //     partyDataMemoryName.c_str(), partyIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "component=shared_memory event=factory_attached factory=party data=\"%s\" index=\"%s\"",
            //     partyDataMemoryName.c_str(), partyIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "[shared memory] Attached party storage: data \"%s\", index \"%s\".",
            //     partyDataMemoryName.c_str(), partyIndexMemoryName.c_str()
            // )
            L2_LOG_INFO(
                Log, "[shared memory] Attached party storage: data \"%s\", index \"%s\".", partyDataMemoryName.c_str(),
                partyIndexMemoryName.c_str()
            );
        } else {
            // Log.Add(LOG_ERROR, "Required party shared factory is unavailable: %s.", partyMemoryError.c_str())
            // Log.Add(
            //     LOG_ERROR, "component=shared_memory event=factory_unavailable factory=party reason=\"%s\". Startup
            //     aborted", partyMemoryError.c_str()
            // )
            // Log.Add(
            //     LOG_ERROR, "[shared memory] Party storage is unavailable: %s. Startup aborted.",
            //     partyMemoryError.c_str()
            // )
            L2_LOG_ERROR(
                Log, "[shared memory] Party storage is unavailable: %s. Startup aborted.", partyMemoryError.c_str()
            );
            return false;
        }

        const char* pledgeDataMemoryEnv = std::getenv("L2NPC_PLEDGE_DATA_SHM");
        const char* pledgeIndexMemoryEnv = std::getenv("L2NPC_PLEDGE_INDEX_SHM");
        const std::string pledgeDataMemoryName = pledgeDataMemoryEnv && *pledgeDataMemoryEnv
            ? pledgeDataMemoryEnv
            : cfg.Get("SharedMemory", "PledgeData", kPledgeDataSharedMemoryName);
        const std::string pledgeIndexMemoryName = pledgeIndexMemoryEnv && *pledgeIndexMemoryEnv
            ? pledgeIndexMemoryEnv
            : cfg.Get("SharedMemory", "PledgeIndex", kPledgeIndexSharedMemoryName);
        std::string pledgeMemoryError;
        CPledgeFactory& pledgeFactory = CPledgeFactory::Inst();
        const bool pledgeMemoryReady =
            (pledgeDataMemoryName == kPledgeDataSharedMemoryName && pledgeIndexMemoryName == kPledgeIndexSharedMemoryName &&
                runtime::SharedFactoryMappingAccess::HasView(pledgeFactory)) ||
            AttachPledgeSharedMemory(pledgeFactory, pledgeDataMemoryName, pledgeIndexMemoryName, &pledgeMemoryError);
        if (pledgeMemoryReady) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "Mapped live pledge factory %s / %s "
            //     "(10002 records, 0x800 bytes each).",
            //     pledgeDataMemoryName.c_str(), pledgeIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "component=shared_memory event=factory_attached factory=pledge data=\"%s\" index=\"%s\"",
            //     pledgeDataMemoryName.c_str(), pledgeIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "[shared memory] Attached pledge storage: data \"%s\", index \"%s\".",
            //     pledgeDataMemoryName.c_str(), pledgeIndexMemoryName.c_str()
            // )
            L2_LOG_INFO(
                Log, "[shared memory] Attached pledge storage: data \"%s\", index \"%s\".", pledgeDataMemoryName.c_str(),
                pledgeIndexMemoryName.c_str()
            );
        } else {
            // Log.Add(LOG_ERROR, "Required pledge shared factory is unavailable: %s.", pledgeMemoryError.c_str())
            // Log.Add(
            //     LOG_ERROR, "component=shared_memory event=factory_unavailable factory=pledge reason=\"%s\". Startup
            //     aborted", pledgeMemoryError.c_str()
            // )
            // Log.Add(
            //     LOG_ERROR, "[shared memory] Pledge storage is unavailable: %s. Startup aborted.",
            //     pledgeMemoryError.c_str()
            // )
            L2_LOG_ERROR(
                Log, "[shared memory] Pledge storage is unavailable: %s. Startup aborted.", pledgeMemoryError.c_str()
            );
            return false;
        }

        // The target's InitializeSharedFactory opens AllianceData and
        // AllianceIndex. SharedDefine_Alliance.max_class is zero, so it
        // does not open AllianceConstant or AllianceMap.
        const char* allianceDataMemoryEnv = std::getenv("L2NPC_ALLIANCE_DATA_SHM");
        const char* allianceIndexMemoryEnv = std::getenv("L2NPC_ALLIANCE_INDEX_SHM");
        const std::string allianceDataMemoryName = allianceDataMemoryEnv && *allianceDataMemoryEnv
            ? allianceDataMemoryEnv
            : cfg.Get("SharedMemory", "AllianceData", kAllianceDataSharedMemoryName);
        const std::string allianceIndexMemoryName = allianceIndexMemoryEnv && *allianceIndexMemoryEnv
            ? allianceIndexMemoryEnv
            : cfg.Get("SharedMemory", "AllianceIndex", kAllianceIndexSharedMemoryName);
        std::string allianceMemoryError;
        auto& allianceFactory =
            CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>::Inst();
        const bool allianceMemoryReady = (allianceDataMemoryName == kAllianceDataSharedMemoryName &&
                                             allianceIndexMemoryName == kAllianceIndexSharedMemoryName &&
                                             runtime::SharedFactoryMappingAccess::HasView(allianceFactory)) ||
            AttachAllianceSharedMemory(
                allianceFactory, allianceDataMemoryName, allianceIndexMemoryName, &allianceMemoryError
            );
        if (allianceMemoryReady) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "Mapped live alliance factory %s / %s "
            //     "(5000 records, 0x118 bytes each).",
            //     allianceDataMemoryName.c_str(), allianceIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "component=shared_memory event=factory_attached factory=alliance data=\"%s\" index=\"%s\"",
            //     allianceDataMemoryName.c_str(), allianceIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "[shared memory] Attached alliance storage: data \"%s\", index \"%s\".",
            //     allianceDataMemoryName.c_str(), allianceIndexMemoryName.c_str()
            // )
            L2_LOG_INFO(
                Log, "[shared memory] Attached alliance storage: data \"%s\", index \"%s\".", allianceDataMemoryName.c_str(),
                allianceIndexMemoryName.c_str()
            );
        } else {
            // Log.Add(LOG_ERROR, "Required alliance shared factory is unavailable: %s.", allianceMemoryError.c_str())
            // Log.Add(
            //     LOG_ERROR, "component=shared_memory event=factory_unavailable factory=alliance reason=\"%s\". Startup
            //     aborted", allianceMemoryError.c_str()
            // )
            // Log.Add(
            //     LOG_ERROR, "[shared memory] Alliance storage is unavailable: %s. Startup aborted.",
            //     allianceMemoryError.c_str()
            // )
            L2_LOG_ERROR(
                Log, "[shared memory] Alliance storage is unavailable: %s. Startup aborted.", allianceMemoryError.c_str()
            );
            return false;
        }

        const char* generalDataMemoryEnv = std::getenv("L2NPC_GENERAL_OBJECT_DATA_SHM");
        const char* generalIndexMemoryEnv = std::getenv("L2NPC_GENERAL_OBJECT_INDEX_SHM");
        const std::string generalDataMemoryName = generalDataMemoryEnv && *generalDataMemoryEnv
            ? generalDataMemoryEnv
            : cfg.Get("SharedMemory", "GeneralObjectData", kGeneralObjectDataSharedMemoryName);
        const std::string generalIndexMemoryName = generalIndexMemoryEnv && *generalIndexMemoryEnv
            ? generalIndexMemoryEnv
            : cfg.Get("SharedMemory", "GeneralObjectIndex", kGeneralObjectIndexSharedMemoryName);
        std::string generalMemoryError;
        CGeneralObjectFactory& generalObjectFactory = CGeneralObjectFactory::Inst();
        const bool generalMemoryReady = (generalDataMemoryName == kGeneralObjectDataSharedMemoryName &&
                                            generalIndexMemoryName == kGeneralObjectIndexSharedMemoryName &&
                                            runtime::SharedFactoryMappingAccess::HasView(generalObjectFactory)) ||
            AttachGeneralObjectSharedMemory(
                generalObjectFactory, generalDataMemoryName, generalIndexMemoryName, &generalMemoryError
            );
        if (generalMemoryReady) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "Mapped live general-object factory %s / %s "
            //     "(1020000 records, 0x118 bytes each).",
            //     generalDataMemoryName.c_str(), generalIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "component=shared_memory event=factory_attached factory=general_object data=\"%s\"
            //     index=\"%s\"", generalDataMemoryName.c_str(), generalIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "[shared memory] Attached general-object storage: data \"%s\", index \"%s\".",
            //     generalDataMemoryName.c_str(), generalIndexMemoryName.c_str()
            // )
            L2_LOG_INFO(
                Log, "[shared memory] Attached general-object storage: data \"%s\", index \"%s\".",
                generalDataMemoryName.c_str(), generalIndexMemoryName.c_str()
            );
        } else {
            // Log.Add(LOG_ERROR, "Required general-object shared factory is unavailable: %s.", generalMemoryError.c_str())
            // Log.Add(
            //     LOG_ERROR, "component=shared_memory event=factory_unavailable factory=general_object reason=\"%s\".
            //     startup aborted", generalMemoryError.c_str()
            // )
            // Log.Add(
            //     LOG_ERROR, "[shared memory] General-object storage is unavailable: %s. Startup aborted.",
            //     generalMemoryError.c_str()
            // )
            L2_LOG_ERROR(
                Log, "[shared memory] General-object storage is unavailable: %s. Startup aborted.",
                generalMemoryError.c_str()
            );
            return false;
        }

        // SharedDefine_AirShip has max_data=1000 and max_class=100. The
        // target opens these three mappings in Data/Constant/Index order.
        // its defined AirShipMap name is not opened by L2NPC.
        const char* airShipDataMemoryEnv = std::getenv("L2NPC_AIRSHIP_DATA_SHM");
        const char* airShipConstantMemoryEnv = std::getenv("L2NPC_AIRSHIP_CONSTANT_SHM");
        const char* airShipIndexMemoryEnv = std::getenv("L2NPC_AIRSHIP_INDEX_SHM");
        const std::string airShipDataMemoryName = airShipDataMemoryEnv && *airShipDataMemoryEnv
            ? airShipDataMemoryEnv
            : cfg.Get("SharedMemory", "AirShipData", kAirShipDataSharedMemoryName);
        const std::string airShipConstantMemoryName = airShipConstantMemoryEnv && *airShipConstantMemoryEnv
            ? airShipConstantMemoryEnv
            : cfg.Get("SharedMemory", "AirShipConstant", kAirShipConstantSharedMemoryName);
        const std::string airShipIndexMemoryName = airShipIndexMemoryEnv && *airShipIndexMemoryEnv
            ? airShipIndexMemoryEnv
            : cfg.Get("SharedMemory", "AirShipIndex", kAirShipIndexSharedMemoryName);
        std::string airShipMemoryError;
        CAirShipFactory& airShipFactory = CAirShipFactory::Inst();
        const bool airShipMemoryReady = (airShipDataMemoryName == kAirShipDataSharedMemoryName &&
                                            airShipConstantMemoryName == kAirShipConstantSharedMemoryName &&
                                            airShipIndexMemoryName == kAirShipIndexSharedMemoryName &&
                                            runtime::SharedFactoryMappingAccess::HasView(airShipFactory)) ||
            AttachAirShipSharedMemory(
                airShipFactory, airShipDataMemoryName, airShipConstantMemoryName, airShipIndexMemoryName, &airShipMemoryError
            );
        if (airShipMemoryReady) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "Mapped live airship factory %s / %s / %s "
            //     "(1000 records, 0x58 bytes each. "
            //     "100 constant entries).",
            //     airShipDataMemoryName.c_str(), airShipConstantMemoryName.c_str(), airShipIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "component=shared_memory event=factory_attached factory=airship data=\"%s\" constant=\"%s\"
            //     index=\"%s\"", airShipDataMemoryName.c_str(), airShipConstantMemoryName.c_str(),
            //     airShipIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "[shared memory] Attached airship storage: data \"%s\", constants \"%s\", index \"%s\".",
            //     airShipDataMemoryName.c_str(), airShipConstantMemoryName.c_str(), airShipIndexMemoryName.c_str()
            // )
            L2_LOG_INFO(
                Log, "[shared memory] Attached airship storage: data \"%s\", constants \"%s\", index \"%s\".",
                airShipDataMemoryName.c_str(), airShipConstantMemoryName.c_str(), airShipIndexMemoryName.c_str()
            );
        } else {
            // Log.Add(LOG_ERROR, "Required airship shared factory is unavailable: %s.", airShipMemoryError.c_str())
            // Log.Add(
            //     LOG_ERROR, "component=shared_memory event=factory_unavailable factory=airship reason=\"%s\". Startup
            //     aborted", airShipMemoryError.c_str()
            // )
            // Log.Add(
            //     LOG_ERROR, "[shared memory] Airship storage is unavailable: %s. Startup aborted.",
            //     airShipMemoryError.c_str()
            // )
            L2_LOG_ERROR(
                Log, "[shared memory] Airship storage is unavailable: %s. Startup aborted.", airShipMemoryError.c_str()
            );
            return false;
        }

        // CStaticObjectFactory opens separate data and object-id index
        // mappings. SharedDefine_StaticObject.max_class is zero, so no
        // constant mapping is opened.
        const char* staticObjectMemoryEnv = std::getenv("L2NPC_STATIC_OBJECT_SHM");
        const char* staticObjectIndexMemoryEnv = std::getenv("L2NPC_STATIC_OBJECT_INDEX_SHM");
        const std::string staticObjectMemoryName = staticObjectMemoryEnv && *staticObjectMemoryEnv
            ? staticObjectMemoryEnv
            : cfg.Get("SharedMemory", "StaticObjectData", kStaticObjectSharedMemoryName);
        const std::string staticObjectIndexMemoryName = staticObjectIndexMemoryEnv && *staticObjectIndexMemoryEnv
            ? staticObjectIndexMemoryEnv
            : cfg.Get("SharedMemory", "StaticObjectIndex", kStaticObjectIndexSharedMemoryName);
        std::string staticObjectMemoryError;
        CStaticObjectFactory& staticObjectFactory = CStaticObjectFactory::Inst();
        const bool staticObjectMemoryReady = (staticObjectMemoryName == kStaticObjectSharedMemoryName &&
                                                 staticObjectIndexMemoryName == kStaticObjectIndexSharedMemoryName &&
                                                 runtime::SharedFactoryMappingAccess::HasView(staticObjectFactory)) ||
            AttachStaticObjectSharedMemory(
                staticObjectFactory, staticObjectMemoryName, staticObjectIndexMemoryName, &staticObjectMemoryError
            );
        if (staticObjectMemoryReady) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "Mapped live static-object factory %s / %s "
            //     "(10001 records, 0x90 bytes each).",
            //     staticObjectMemoryName.c_str(), staticObjectIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "component=shared_memory event=factory_attached factory=static_object data=\"%s\"
            //     index=\"%s\"", staticObjectMemoryName.c_str(), staticObjectIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "[shared memory] Attached static-object storage: data \"%s\", index \"%s\".",
            //     staticObjectMemoryName.c_str(), staticObjectIndexMemoryName.c_str()
            // )
            L2_LOG_INFO(
                Log, "[shared memory] Attached static-object storage: data \"%s\", index \"%s\".",
                staticObjectMemoryName.c_str(), staticObjectIndexMemoryName.c_str()
            );
        } else {
            // Log.Add(LOG_ERROR, "Required static-object shared factory is unavailable: %s.",
            // staticObjectMemoryError.c_str()), Log.Add(
            //     LOG_ERROR, "component=shared_memory event=factory_unavailable factory=static_object reason=\"%s\". Startup
            //     aborted", staticObjectMemoryError.c_str()
            // )
            // Log.Add(
            //     LOG_ERROR, "[shared memory] Static-object storage is unavailable: %s. Startup aborted.",
            //     staticObjectMemoryError.c_str()
            // )
            L2_LOG_ERROR(
                Log, "[shared memory] Static-object storage is unavailable: %s. Startup aborted.",
                staticObjectMemoryError.c_str()
            );
            return false;
        }

        // CItemFactory opens Data, Constant, and Index in this order.
        // Current L2Server.exe creates ItemConstant as
        // max_class * 0x1e0 bytes. CSharedItemConstant and the consumer's
        // direct indexed reads use that same stride.
        const char* itemDataMemoryEnv = std::getenv("L2NPC_ITEM_DATA_SHM");
        const char* legacyItemMemoryEnv = std::getenv("L2NPC_ITEM_SHM");
        const char* itemConstantMemoryEnv = std::getenv("L2NPC_ITEM_CONSTANT_SHM");
        const char* itemIndexMemoryEnv = std::getenv("L2NPC_ITEM_INDEX_SHM");
        const std::string itemDataMemoryName = itemDataMemoryEnv && *itemDataMemoryEnv ? itemDataMemoryEnv
            : legacyItemMemoryEnv && *legacyItemMemoryEnv                              ? legacyItemMemoryEnv
                                                          : cfg.Get("SharedMemory", "ItemData", kItemSharedMemoryName);
        const std::string itemConstantMemoryName = itemConstantMemoryEnv && *itemConstantMemoryEnv
            ? itemConstantMemoryEnv
            : cfg.Get("SharedMemory", "ItemConstant", kItemConstantSharedMemoryName);
        const std::string itemIndexMemoryName = itemIndexMemoryEnv && *itemIndexMemoryEnv
            ? itemIndexMemoryEnv
            : cfg.Get("SharedMemory", "ItemIndex", kItemIndexSharedMemoryName);
        std::string itemMemoryError;
        CItemFactory& itemFactory = CItemFactory::Inst();
        const bool itemMemoryReady =
            (itemDataMemoryName == kItemSharedMemoryName && itemConstantMemoryName == kItemConstantSharedMemoryName &&
                itemIndexMemoryName == kItemIndexSharedMemoryName &&
                runtime::SharedFactoryMappingAccess::HasView(itemFactory)) ||
            AttachItemSharedMemory(
                itemFactory, itemDataMemoryName, itemConstantMemoryName, itemIndexMemoryName, &itemMemoryError
            );
        if (itemMemoryReady) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "Mapped live item factory %s / %s / %s "
            //     "(900000 records, 0xa0 bytes each).",
            //     itemDataMemoryName.c_str(), itemConstantMemoryName.c_str(), itemIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "component=shared_memory event=factory_attached factory=item data=\"%s\" constant=\"%s\"
            //     index=\"%s\"", itemDataMemoryName.c_str(), itemConstantMemoryName.c_str(), itemIndexMemoryName.c_str()
            // )
            // Log.Add(
            //     LOG_NORMAL, "[shared memory] Attached item storage: data \"%s\", constants \"%s\", index \"%s\".",
            //     itemDataMemoryName.c_str(), itemConstantMemoryName.c_str(), itemIndexMemoryName.c_str()
            // )
            L2_LOG_INFO(
                Log, "[shared memory] Attached item storage: data \"%s\", constants \"%s\", index \"%s\".",
                itemDataMemoryName.c_str(), itemConstantMemoryName.c_str(), itemIndexMemoryName.c_str()
            );
        } else {
            // Log.Add(LOG_ERROR, "Required item shared factory is unavailable: %s.", itemMemoryError.c_str())
            // Log.Add(
            //     LOG_ERROR, "component=shared_memory event=factory_unavailable factory=item reason=\"%s\". Startup
            //     aborted", itemMemoryError.c_str()
            // )
            // Log.Add(
            //     LOG_ERROR, "[shared memory] Item storage is unavailable: %s. Startup aborted.",
            //     itemMemoryError.c_str()
            // )
            L2_LOG_ERROR(Log, "[shared memory] Item storage is unavailable: %s. Startup aborted.", itemMemoryError.c_str());
            return false;
        }

        InitializeSharedVector();

        CSharedRWLock* const firstSharedLock = CInventory::GetLockManager().GetLock(0);
        // Noise: An internal lock address is not useful operational startup information.
        // Log.Add(LOG_NORMAL, "First SharedLock pointer = 0x%x", runtime::PointerLow32(firstSharedLock))
        (void)firstSharedLock;

        // Current L2NPC.exe constructs CSharedArray<long,87040> with the
        // fixed UserMapDensity name at this point, after both shared-state
        // initialization passes.
        SharedUserMapDensity::Inst().Init();

        const unsigned int airShipSid = CAirShipEventManager::Inst().GetAirShipSID(0);
        // Noise: The initial airship ID does not report manager readiness or a failure.
        // Log.Add(LOG_NORMAL, "CAirShipEventManager. %d", static_cast<int>(airShipSid))
        (void)airShipSid;

        l2npc::ServerToNpcPacketStat::Inst().Init(L"ServerToNpc");
        l2npc::NpcToServerPacketStat::Inst().Init(L"NpcToServer");
        if (StopRequested()) {
            return false;
        }

        // Noise: The local startup marker also reaches the console. A second record duplicates it.
        // Log.Add(LOG_NORMAL, "L2NPC Server Start")
        // Log.Add(LOG_LOCAL, "L2NPC Server Start")
        // Log.Add(LOG_LOCAL, "component=startup event=shared_state_phase_finished")
        // Log.Add(LOG_LOCAL, "[startup] Shared-state initialization phase finished.")
        L2_LOG(Log, LOG_LOCAL, "[startup] Shared-state initialization phase finished.");

        // WinMain reaches CWorld::InitGeo only after both target shared-state
        // initialization passes. Bind the platform TCP transports at that
        // same process-composition boundary. Target socket ownership remains
        // in NPCServer and CDBNPC.
        l2geo::CWorld::Inst().InitGeo();
        CThreadManager::SetThisThreadAsMainThread();
        ConfigureTransportBindings();
        // The current L2NPC.exe is only a consumer of
        // L2World_GeoDataIndex and the number-suffixed zone/cell/offset
        // mappings. File loading and mapping creation belong to the
        // producer process and must not run as a startup fallback here.
        GeoDataLoadStats geoDataStats;
        const GeoDataSharedMemoryNames defaultGeoDataNames;
        GeoDataSharedMemoryNames geoDataSharedNames;
        const auto configuredSharedMemoryName =
            [this](const char* environmentName, const char* configKey, const char* exactName) {
                const char* environmentValue = std::getenv(environmentName);
                return environmentValue && *environmentValue ? std::string(environmentValue)
                                                             : cfg.Get("SharedMemory", configKey, exactName);
            };
        geoDataSharedNames.index =
            configuredSharedMemoryName("L2NPC_GEODATA_INDEX_SHM", "GeoDataIndex", defaultGeoDataNames.index.c_str());
        geoDataSharedNames.zones =
            configuredSharedMemoryName("L2NPC_GEODATA_ZONES_SHM", "GeoDataZones", defaultGeoDataNames.zones.c_str());
        geoDataSharedNames.cells =
            configuredSharedMemoryName("L2NPC_GEODATA_CELLS_SHM", "GeoDataCells", defaultGeoDataNames.cells.c_str());
        geoDataSharedNames.cellOffsets = configuredSharedMemoryName(
            "L2NPC_GEODATA_CELL_OFFSETS_SHM", "GeoDataCellOffest", defaultGeoDataNames.cellOffsets.c_str()
        );
        std::string geoDataSharedError;
        CGeoData* const geodata = l2geo::CWorld::Inst().GetGeo();
        const bool defaultGeoDataMapping = geoDataSharedNames.index == defaultGeoDataNames.index &&
            geoDataSharedNames.zones == defaultGeoDataNames.zones && geoDataSharedNames.cells == defaultGeoDataNames.cells &&
            geoDataSharedNames.cellOffsets == defaultGeoDataNames.cellOffsets;
        const bool reusedGeoDataMapping =
            geodata && defaultGeoDataMapping && l2geo::portable::GeoDataSource::HasSharedMemory(*geodata);
        bool geoDataReady = reusedGeoDataMapping ||
            (geodata &&
                l2geo::portable::GeoDataSource::AttachSharedMemory(
                    *geodata, geoDataSharedNames, &geoDataSharedError, &geoDataStats
                ));
        const int publishedGeoDataZoneCount = geodata ? l2geo::portable::GeoDataSource::SharedMemoryZoneCount(*geodata) : 0;
        geoDataReady = geoDataReady && publishedGeoDataZoneCount > 0;
        if (!geoDataReady && geoDataSharedError.empty()) {
            geoDataSharedError = "geodata index " + geoDataSharedNames.index + " contains no published zones";
        }
        if (reusedGeoDataMapping && geoDataReady) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "Mapped live geodata %s: %d published zones.",
            //     geoDataSharedNames.index.c_str(),
            //     publishedGeoDataZoneCount)
            // Previous: L2_LOG_INFO(Log,
            //     "Mapped live geodata %s: %d published zones.",
            //     geoDataSharedNames.index.c_str(),
            //     publishedGeoDataZoneCount)
            L2_LOG_INFO(
                Log, "[geodata] Mapped live index %s with %d published zones.", geoDataSharedNames.index.c_str(),
                publishedGeoDataZoneCount
            );
        } else if (geoDataReady) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "Mapped live geodata %s: %llu zones, %llu cells, "
            //     "%llu flat/%llu simple/%llu complex sectors, "
            //     "%llu mapped bytes.",
            //     geoDataSharedNames.index.c_str(), static_cast<unsigned long long>(geoDataStats.zones),
            //     static_cast<unsigned long long>(geoDataStats.cells),
            //     static_cast<unsigned long long>(geoDataStats.flatSectors),
            //     static_cast<unsigned long long>(geoDataStats.simpleSectors),
            //     static_cast<unsigned long long>(geoDataStats.complexSectors),
            //     static_cast<unsigned long long>(geoDataStats.mappedBytes)
            // )
            // Previous: L2_LOG_INFO(Log,
            //     "Mapped live geodata %s: %llu zones, %llu cells, "
            //     "%llu flat/%llu simple/%llu complex sectors, "
            //     "%llu mapped bytes.",
            //     geoDataSharedNames.index.c_str(), static_cast<unsigned long long>(geoDataStats.zones),
            //     static_cast<unsigned long long>(geoDataStats.cells),
            //     static_cast<unsigned long long>(geoDataStats.flatSectors),
            //     static_cast<unsigned long long>(geoDataStats.simpleSectors),
            //     static_cast<unsigned long long>(geoDataStats.complexSectors),
            //     static_cast<unsigned long long>(geoDataStats.mappedBytes)
            // )
            L2_LOG_INFO(
                Log,
                "[geodata] Mapped live index %s: %llu zones, %llu cells, "
                "%llu flat/%llu simple/%llu complex sectors, "
                "%llu mapped bytes.",
                geoDataSharedNames.index.c_str(), static_cast<unsigned long long>(geoDataStats.zones),
                static_cast<unsigned long long>(geoDataStats.cells),
                static_cast<unsigned long long>(geoDataStats.flatSectors),
                static_cast<unsigned long long>(geoDataStats.simpleSectors),
                static_cast<unsigned long long>(geoDataStats.complexSectors),
                static_cast<unsigned long long>(geoDataStats.mappedBytes)
            );
        }
        if (!geoDataReady) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Geodata is unavailable: %s. Startup cannot continue "
            //     "without the target's required geodata mappings.",
            //     geoDataSharedError.c_str()
            // )
            // Previous: L2_LOG_ERROR(Log,
            //     "Geodata is unavailable: %s. Startup cannot continue "
            //     "without the target's required geodata mappings.",
            //     geoDataSharedError.c_str()
            // )
            L2_LOG_ERROR(
                Log,
                "[geodata] Data is unavailable: %s. Startup cannot continue "
                "without the required mappings.",
                geoDataSharedError.c_str()
            );
            return false;
        }
        if (StopRequested()) {
            return false;
        }

        // The original receives on the same socket returned by NPCServer::Connect.
        l2s.SetReceiveSink(
            [this](const uint8_t* data, int length) {
                if (length <= 0 || !FeedL2ServerData(data, static_cast<std::size_t>(length))) {
                    l2s.RejectPeerProtocol();
                }
            },
            [this] {
                const int readError = l2s.LastReadError();
                // Log before terminal dispatch: the recovered OnClose path is fatal.
                // Log.Add(LOG_ERROR,
                //     "[L2Server] Connection to %s:%d ended (%s, receive code %d). Shutdown is required.",
                //     l2s.Host().c_str(), l2s.Port(),
                //     readError == 0 ? "no receive error recorded" : "receive failure or local transport shutdown",
                //     readError)
                L2_LOG_ERROR(
                    Log, "[L2Server] Connection to %s:%d ended (%s, receive code %d); shutdown is required.",
                    l2s.Host().c_str(), l2s.Port(),
                    readError == 0 ? "no receive error recorded" : "receive failure or local transport shutdown", readError
                );
                OnL2ServerDisconnected(readError);
                fatalError.store(true);
                // Noise: The terminal receive status is reported before fatal dispatch. Repeating it here can be too late
                // and adds no context. Log.Add(
                //     LOG_ERROR,
                //     "L2Server closed the full-duplex connection "
                //     "(shipping L2NPC treats this as fatal)."
                // )
            }
        );

        cache.SetReceiveSink(
            [this](const uint8_t* data, int length) {
                if (length <= 0 || !FeedCacheDData(data, static_cast<std::size_t>(length))) {
                    cache.RejectPeerProtocol();
                }
            },
            [this] {
                const int readError = cache.LastReadError();
                // Log before terminal dispatch: the recovered OnClose path is fatal.
                // Log.Add(LOG_ERROR,
                //     "[CacheD] Connection to %s:%d ended (%s, receive code %d). Shutdown is required.",
                //     cache.Host().c_str(), cache.Port(),
                //     readError == 0 ? "no receive error recorded" : "receive failure or local transport shutdown",
                //     readError)
                L2_LOG_ERROR(
                    Log, "[CacheD] Connection to %s:%d ended (%s, receive code %d); shutdown is required.",
                    cache.Host().c_str(), cache.Port(),
                    readError == 0 ? "no receive error recorded" : "receive failure or local transport shutdown", readError
                );
                OnCacheDDisconnected(readError);
                fatalError.store(true);
                // Noise: The terminal receive status is reported before fatal dispatch. Repeating it here can be too late
                // and adds no context. Log.Add(
                //     LOG_ERROR,
                //     "CacheD closed the full-duplex connection "
                //     "(shipping L2NPC treats this as fatal)."
                // )
            }
        );

        l2npc::portable::NPCServerRuntimeServices services;
        services.stopRequested = [this] { return StopRequested() || fatalError.load(std::memory_order_acquire); };
        services.waitForRetryOrStop = [this] {
            if (fatalError.load(std::memory_order_acquire)) {
                return true;
            }
            const bool stopped = WaitForRetryOrStop();
            return stopped || fatalError.load(std::memory_order_acquire);
        };
        services.initializeIO = [this] { InstallIORuntime(); };
        services.startIOThreads = [this](int threadCount, l2npc::portable::IOThreadCallbackFactory callbackFactory) {
            StartIOThreads(threadCount, std::move(callbackFactory));
        };
        services.startSocketReceivers = [this] {
            runtime::log::StartLogReceiving(logd);
            l2s.StartReceiving();
            cache.StartReceiving();
        };
        services.startupReady = [this] {
            lifecycle.MarkReady();
            // Log.Add(
            //     LOG_NORMAL,
            //     "Boot complete in %u ms "
            //     "(WorldId=%d). Type .help "
            //     "for commands.",
            //     runtime::GetTickCount() - bootTick, worldId
            // )
            // Log.Add(
            //     LOG_NORMAL, "component=startup event=ready elapsed_ms=%u world_id=%d. Type .help for commands",
            //     runtime::GetTickCount() - bootTick, worldId
            // )
            // Log.Add(
            //     LOG_NORMAL, "[startup] Ready after %u ms (world %d). Type .help for commands.",
            //     runtime::GetTickCount() - bootTick, worldId
            // )
            L2_LOG_INFO(
                Log, "[startup] Ready after %u ms (world %d). Type .help for commands.", runtime::GetTickCount() - bootTick,
                worldId
            );
        };
        services.startupFailed = [this] { lifecycle.MarkFailed(); };
        portable::WaitThreadRuntimeServices waitServices;
        waitServices.stopRequested = services.stopRequested;
        waitServices.waitForStop = [this](std::chrono::milliseconds timeout) {
            if (fatalError.load(std::memory_order_acquire)) {
                return true;
            }
            const bool stopped = lifecycle.WaitForStopFor(timeout);
            return stopped || fatalError.load(std::memory_order_acquire);
        };
        waitThreadRuntime.Reset(std::move(waitServices));
        npcServerRuntime.Reset(std::move(services));
        return true;
    }

    // NPCServer::Start in the current target creates a dedicated worker,
    // publishes it in CThreadManager slot 11, and that worker registers TLS
    // role LISTEN before entering NPCServer::Run. The portable console keeps
    // the GUI/main role on its own thread and only substitutes std::thread for
    // _beginthreadex. Like the target, Start returns as soon as that worker is
    // published. Ready/Failed is reported asynchronously.
    bool Start() {
        npcServerThreadCreated = false;
        if (!PreparePortableRuntime()) {
            return false;
        }

        // InitInstance seeds the two MAIN-thread streams immediately around
        // CreateWindowEx. The portable frontend has no Win32 window, but it
        // keeps the same _time32/QPC sources before publishing LISTEN.
        // These generators are TLS-backed, so the new LISTEN thread still
        // starts from the target image's default state.
        LegacyCrtSrand(static_cast<std::uint32_t>(runtime::crt::Time32Now()));
#if defined(_WIN32)
        LARGE_INTEGER performanceCounter;
        ::QueryPerformanceCounter(&performanceCounter);
        const std::uint64_t randomSeed = static_cast<std::uint64_t>(performanceCounter.QuadPart);
#else
        const std::uint64_t randomSeed =
            static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
        init_genrand64(randomSeed);

        npcServerThread.Reset([this]() -> void* {
            npcServerThreadCreated = lifecycle.Start(
                [this] {
                    NPCServerThread(nullptr);
                    if (lifecycle.State() == l2npc::NPCServerLifecycleState::Starting && !StopRequested()) {
                        lifecycle.MarkFailed();
                    }
                    const bool succeeded = lifecycle.State() != l2npc::NPCServerLifecycleState::Failed;
                    if (!succeeded && !StopRequested()) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "NPCServer startup failed. The "
                        //     "portable console remains available "
                        //     "for status and .quit."
                        // )
                        // Log.Add(LOG_ERROR, "component=startup event=failed worker=listen, console status and .quit remain
                        // available"), Log.Add(LOG_ERROR, "[startup] Listener worker failed. Console status and .quit remain
                        // available.")
                        L2_LOG_ERROR(Log, "[startup] Listener worker failed. Console status and .quit remain available.");
                    }
                    return succeeded;
                },
                [](std::exception_ptr failure) {
                    try {
                        if (failure) {
                            std::rethrow_exception(failure);
                        }
                    } catch (const std::exception& error) {
                        // Log.Add(LOG_ERROR, "NPCServer worker failed: %s", error.what())
                        // Log.Add(
                        //     LOG_ERROR, "component=runtime event=worker_failed worker=listen reason=\"%s\"",
                        //     error.what()
                        // )
                        // Log.Add(
                        //     LOG_ERROR, "[runtime] Listener worker failed: %s.",
                        //     error.what()
                        // )
                        L2_LOG_ERROR(Log, "[runtime] Listener worker failed: %s.", error.what());
                    } catch (...) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "NPCServer worker failed with an "
                        //     "unknown exception."
                        // )
                        // Log.Add(LOG_ERROR, "component=runtime event=worker_failed worker=listen
                        // reason=unknown_exception"), Log.Add(LOG_ERROR, "[runtime] Listener worker failed with an unknown
                        // exception.")
                        L2_LOG_ERROR(Log, "[runtime] Listener worker failed with an unknown exception.");
                    }
                },
                [] { CThreadManager::SetThread(ThreadID::LISTEN, nullptr); }
            );
            return npcServerThreadCreated ? lifecycle.NativeHandle() : nullptr;
        });
        g_ns.Start();
        if (!npcServerThreadCreated) {
            // Log.Add(LOG_ERROR, "Could not create NPCServer listen thread.")
            // Log.Add(LOG_ERROR, "component=startup event=worker_creation_failed worker=listen, startup aborted")
            // Log.Add(LOG_ERROR, "[startup] Could not create the listener worker. Startup aborted.")
            L2_LOG_ERROR(Log, "[startup] Could not create the listener worker. Startup aborted.");
        }
        return npcServerThreadCreated;
    }

    bool FatalError() const { return fatalError.load(); }

    void Shutdown() {
        std::call_once(shutdownOnce, [this] {
            // Log.Add(
            //     LOG_NORMAL,
            //     "Shutting down: requesting the portable worker stop "
            //     "and closing L2Server/CacheD transports..."
            // )
            // Log.Add(LOG_NORMAL, "component=shutdown event=stopping, stopping workers and closing L2Server/CacheD
            // connections"), Log.Add(LOG_NORMAL, "[shutdown] Stopping workers and closing L2Server and CacheD
            // connections.")
            L2_LOG_INFO(Log, "[shutdown] Stopping workers and closing L2Server and CacheD connections.");

            // The Windows target terminates the process from its close
            // assertion. Portable hosts need a cooperative boundary:
            // request/wake first, unblock transport reads, then wait until
            // NPCServer::Run no longer touches process transport state.
            lifecycle.RequestStop();
            l2s.Close();
            cache.Close();
            lifecycle.Join();

            // Teardown starts only after the boot/listen thread has left.
            // LogD still owns completion callbacks into the IO runtime, so
            // stop it before joining those workers.
            runtime::log::StopLogTransport(logd);
            StopIORuntime();
            ReleaseRecoveredSocketOwners();
            lifecycle.MarkStopped();
            // Log.Add(LOG_NORMAL, "L2NPC stopped.")
            // Log.Add(LOG_NORMAL, "component=shutdown event=stopped")
            // Log.Add(LOG_NORMAL, "[shutdown] Stopped.")
            L2_LOG_INFO(Log, "[shutdown] Stopped.");
        });
    }
};

int main(int argc, char** argv) {
    runtime::logging::SetThreadName("main");
    if (!runtime::InitializeDirectoryArguments(argc, argv)) {
        return 2;
    }

    server_up_time = runtime::crt::Time32Now();
    InitializeExceptionRuntime();

    l2npc::portable::ProcessStartupGate startupGate;
    std::string startupGateDiagnostic;
    switch (startupGate.Acquire(&startupGateDiagnostic)) {
        case l2npc::portable::ProcessStartupGateResult::AlreadyRunning:
            // Log.Add(LOG_ERROR, "L2NPC.EXE is already running.")
            // Log.Add(LOG_ERROR, "component=startup event=already_running, startup aborted")
            // Log.Add(LOG_ERROR, "[startup] An NPC instance is already running. Startup aborted.")
            L2_LOG_ERROR(Log, "[startup] An NPC instance is already running. Startup aborted.");
            return 1;
        case l2npc::portable::ProcessStartupGateResult::NpcRemovalInProgress:
            // Log.Add(LOG_ERROR, "L2Server's NPC is Removing!!!!")
            // Log.Add(LOG_WARN, "component=startup event=peer_npc_cleanup_in_progress peer=l2server, startup aborted, retry
            // after cleanup finishes"), Log.Add(LOG_WARN, "[startup] L2Server is still cleaning up the previous NPC
            // instance. Startup aborted, retry after cleanup finishes.")
            L2_LOG_WARN(
                Log,
                "[startup] L2Server is still cleaning up the previous NPC instance. Startup aborted; retry after cleanup "
                "finishes."
            );
            return 1;
        case l2npc::portable::ProcessStartupGateResult::Ready:
            break;
    }
    if (!startupGateDiagnostic.empty()) {
        // WinMain ignores named-mutex errors other than ALREADY_EXISTS and
        // continues. Keep that boundary while making the OS failure visible.
        // Log.Add(LOG_WARN, "Process startup mutex warning: %s", startupGateDiagnostic.c_str())
        // Log.Add(
        //     LOG_WARN, "component=startup event=process_marker_check_warning reason=\"%s\". Startup continues",
        //     startupGateDiagnostic.c_str()
        // )
        // Log.Add(
        //     LOG_WARN, "[startup] Could not verify the process marker: %s. Startup continues.",
        //     startupGateDiagnostic.c_str()
        // )
        L2_LOG_WARN(
            Log, "[startup] Could not verify the process marker: %s. Startup continues.", startupGateDiagnostic.c_str()
        );
    }

    RenameOldLog();
    std::signal(SIGINT, RequestSignalShutdown);
    std::signal(SIGTERM, RequestSignalShutdown);

    PortableNPCServerHost srv(startupGate);
    if (!srv.Start()) {
        srv.Shutdown();
        return 1;
    }

    l2npc::portable::Console con;
    const auto readyForCommand = [&srv](const char* command) {
        const l2npc::NPCServerLifecycleState state = srv.LifecycleState();
        if (state == l2npc::NPCServerLifecycleState::Ready) {
            return true;
        }
        // Log.Add(
        //     LOG_WARN,
        //     "%s is unavailable while L2NPC is %s. "
        //     ".quit remains available.",
        //     command, l2npc::NPCServerLifecycleStateName(state)
        // )
        // Previous: L2_LOG_WARN(Log,
        //     "%s is unavailable while L2NPC is %s. "
        //     ".quit remains available.",
        //     command, l2npc::NPCServerLifecycleStateName(state)
        // )
        L2_LOG_WARN(
            Log, "[console] Command .%s is unavailable while the server state is %s; .quit remains available.", command,
            l2npc::NPCServerLifecycleStateName(state)
        );
        return false;
    };

    con.Register("status", "show AI-server status and both original full-duplex links", [&](auto&) {
        const l2npc::NPCServerLifecycleState state = srv.LifecycleState();
        if (state != l2npc::NPCServerLifecycleState::Ready) {
            // Log.Add(
            //     LOG_NORMAL, "lifecycle=%s fatal=%s", l2npc::NPCServerLifecycleStateName(state),
            //     srv.FatalError() ? "yes" : "no"
            // )
            // Previous: L2_LOG_INFO(Log, "lifecycle=%s fatal=%s", l2npc::NPCServerLifecycleStateName(state),
            //     srv.FatalError() ? "yes" : "no"
            // )
            L2_LOG_INFO(
                Log, "[status] State: %s; fatal error: %s.", l2npc::NPCServerLifecycleStateName(state),
                srv.FatalError() ? "yes" : "no"
            );
            return;
        }
        // Log.Add(
        //     LOG_NORMAL, "uptime=%u ms  worldId=%d  l2server=%s  cached=%s  npc=%zu  ai=%s",
        //     runtime::GetTickCount() - srv.bootTick, srv.worldId, srv.l2s.IsConnected() ? "connected" : "down",
        //     srv.cache.IsConnected() ? "connected" : "down",
        //     static_cast<std::size_t>(std::max<std::int32_t>(0, runtime::raw_atomic::LoadI32Relaxed(&g_nNPCCount))),
        //     CNPCAIManager::s_bLoaded ? "loaded" : "missing"
        // )
        // Previous: L2_LOG_INFO(Log, "uptime=%u ms  worldId=%d  l2server=%s  cached=%s  npc=%zu  ai=%s",
        //     runtime::GetTickCount() - srv.bootTick, srv.worldId, srv.l2s.IsConnected() ? "connected" : "down",
        //     srv.cache.IsConnected() ? "connected" : "down",
        //     static_cast<std::size_t>(std::max<std::int32_t>(0, runtime::raw_atomic::LoadI32Relaxed(&g_nNPCCount))),
        //     CNPCAIManager::s_bLoaded ? "loaded" : "missing"
        // )
        L2_LOG_INFO(
            Log, "[status] Uptime %u ms; world %d; L2Server %s; CacheD %s; %zu NPCs; AI %s.",
            runtime::GetTickCount() - srv.bootTick, srv.worldId, srv.l2s.IsConnected() ? "connected" : "down",
            srv.cache.IsConnected() ? "connected" : "down",
            static_cast<std::size_t>(std::max<std::int32_t>(0, runtime::raw_atomic::LoadI32Relaxed(&g_nNPCCount))),
            CNPCAIManager::s_bLoaded ? "loaded" : "missing"
        );
    });

    con.Register("netstat", "show the original L2Server and CacheD connections", [&](auto&) {
        if (!readyForCommand("netstat")) {
            return;
        }
        // Log.Add(
        //     LOG_NORMAL, "netstat: L2Server %s:%d state=%s", srv.l2s.Host().c_str(), srv.l2s.Port(),
        //     srv.l2s.IsConnected() ? "CONNECTED" : "down"
        // )
        // Previous: L2_LOG_INFO(Log, "netstat: L2Server %s:%d state=%s", srv.l2s.Host().c_str(), srv.l2s.Port(),
        //     srv.l2s.IsConnected() ? "CONNECTED" : "down"
        // )
        L2_LOG_INFO(
            Log, "[network] L2Server %s:%d is %s.", srv.l2s.Host().c_str(), srv.l2s.Port(),
            srv.l2s.IsConnected() ? "connected" : "down"
        );
        // Log.Add(
        //     LOG_NORMAL, "netstat: CacheD   %s:%d state=%s", srv.cache.Host().c_str(), srv.cache.Port(),
        //     srv.cache.IsConnected() ? "CONNECTED" : "down"
        // )
        // Previous: L2_LOG_INFO(Log, "netstat: CacheD   %s:%d state=%s", srv.cache.Host().c_str(), srv.cache.Port(),
        //     srv.cache.IsConnected() ? "CONNECTED" : "down"
        // )
        L2_LOG_INFO(
            Log, "[network] CacheD %s:%d is %s.", srv.cache.Host().c_str(), srv.cache.Port(),
            srv.cache.IsConnected() ? "connected" : "down"
        );
    });

    con.Register("aireload", "target AI reload: .aireload <dev|live>", [&](const std::vector<std::string>& args) {
        if (!readyForCommand("aireload")) {
            return;
        }
        if (args.size() != 1 || (args[0] != "dev" && args[0] != "live")) {
            // Log.Add(LOG_ERROR, "usage: .aireload <dev|live>")
            // Previous: L2_LOG_ERROR(Log, "usage: .aireload <dev|live>")
            L2_LOG_WARN(Log, "[console] Usage: .aireload <dev|live>.");
            return;
        }
        const bool replaceClass = args[0] == "live";
        g_nNPCSpawnCount.exchange(0, std::memory_order_seq_cst);
        // Log.Add(LOG_ERROR, replaceClass ? L"IDM_AI_RELOAD_LIVE" : L"IDM_AI_RELOAD_DEV")
        // Previous: L2_LOG_ERROR(Log, replaceClass ? L"IDM_AI_RELOAD_LIVE" : L"IDM_AI_RELOAD_DEV")
        L2_LOG_INFO(
            Log, replaceClass ? L"[AI reload] Reloading live AI classes." : L"[AI reload] Reloading development AI classes."
        );
        CNPCAIManager::LoadNPCAI(replaceClass);
        // Log.Add(
        //     LOG_ERROR,
        //     replaceClass ? L"after  reload: queued timer "
        //                    L"count, %I64d, %d"
        //                  : L"after  reload: queued timer "
        //                    L"count, %I64d, spawn count: %d",
        //     g_nTimerCount.load(std::memory_order_seq_cst), g_nNPCSpawnCount.load(std::memory_order_seq_cst)
        // )
        // Previous: L2_LOG_ERROR(Log,
        //     replaceClass ? L"after  reload: queued timer "
        //                    L"count, %I64d, %d"
        //                  : L"after  reload: queued timer "
        //                    L"count, %I64d, spawn count: %d",
        //     g_nTimerCount.load(std::memory_order_seq_cst), g_nNPCSpawnCount.load(std::memory_order_seq_cst)
        // )
        L2_LOG_INFO(
            Log, "[AI reload] Completed %s reload: %lld queued timers, %d spawned NPCs.",
            replaceClass ? "live" : "development", static_cast<long long>(g_nTimerCount.load(std::memory_order_seq_cst)),
            g_nNPCSpawnCount.load(std::memory_order_seq_cst)
        );
    });

    int rc = con.Run([&srv] { return !srv.FatalError() && !SignalShutdownRequested(); });
    if (g_shutdownSignal != 0) {
        // Log.Add(
        //     LOG_NORMAL,
        //     "Portable host received shutdown signal %d.",
        //     static_cast<int>(g_shutdownSignal))
        // Log.Add(
        //     LOG_NORMAL, "component=shutdown event=signal_received signal=%d",
        //     static_cast<int>(g_shutdownSignal)
        // )
        // Log.Add(
        //     LOG_NORMAL, "[shutdown] Received signal %d.",
        //     static_cast<int>(g_shutdownSignal)
        // )
        L2_LOG_INFO(Log, "[shutdown] Received signal %d.", static_cast<int>(g_shutdownSignal));
    }
    const bool fatalError = srv.FatalError();
    const bool startupFailed = srv.LifecycleState() == l2npc::NPCServerLifecycleState::Failed;
    srv.Shutdown();
    return fatalError || startupFailed ? 1 : rc;
}

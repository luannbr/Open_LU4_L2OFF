#include "NPCServer.h"
#include "Shared/src/Runtime/ConfigDirectory.h"

#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/AIManager.h"
#include "L2NPC/src/Config.h"
#include "L2NPC/src/DBNPC.h"
#include "L2NPC/src/DBNPCSocket.h"
#include "L2NPC/src/EventManager.h"
#include "L2NPC/src/FormationManager.h"
#include "L2NPC/src/Freeway.h"
#include "L2NPC/src/LottoGame.h"
#include "L2NPC/src/NPC.h"
#include "L2NPC/src/NpcMakerDB.h"
#include "L2NPC/src/ServerSocket.h"
#include "L2NPC/src/SuperPoint.h"
#include "L2NPC/src/User.h"
#include "Portable/NPCServerConfiguration.h"
#include "Portable/NPCServerConnection.h"
#include "Portable/NPCServerRuntime.h"
#include "Portable/NPCServerThread.h"
#include "Shared/src/Runtime/SocketTransport.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/Win64Integer.h"
#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"
#include "Shared/src/LocalizationSystem.h"
#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"
#include "net/Config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

// PDB S_GDATA32 `g_nFileDuration`, contributed by NPCServer.obj. The current
// image initializes it to 30 before CLog's dynamic construction.
int g_nFileDuration = 30;

namespace {

template <std::size_t Size>
void CopyFixedNarrow(std::array<char, Size>& destination, std::string_view source) {
    const std::size_t count = std::min(source.size(), Size - 1);
    std::copy_n(source.data(), count, destination.data());
    destination[count] = '\0';
}

template <std::size_t Size>
void CopyFixedUtf16(std::array<char16_t, Size>& destination, std::string_view source) {
    const std::u16string converted = Utf8ToUtf16(source);
    // NPCServer::Load passes 0x3ff to GetPrivateProfileStringW for every
    // value copied through this helper. The Win32 API therefore returns at
    // most 0x3fe UTF-16 code units plus the terminator even though the final
    // NPCServer fields themselves have room for 0x800 code units.
    constexpr std::size_t profileValueLimit = 0x3fe;
    const std::size_t count = std::min(converted.size(), std::min(Size - 1, profileValueLimit));
    std::copy_n(converted.data(), count, destination.data());
    destination[count] = u'\0';
}

bool EqualsAsciiCaseInsensitive(std::string left, std::string right) {
    const auto fold = [](unsigned char value) { return static_cast<char>(std::tolower(value)); };
    std::transform(left.begin(), left.end(), left.begin(), fold);
    std::transform(right.begin(), right.end(), right.begin(), fold);
    return left == right;
}

// Current L2NPC.exe 0x004ff69c. The adjacent PDB donor identifies the
// anonymous source class and NPCServer.cpp ownership. The current callback
// additionally publishes its worker heartbeat after draining the same
// process-global CSmartIdManager pipe.
class CIOThreadCallbackNpc final : public IOThreadCallback {
public:
    explicit CIOThreadCallbackNpc(int threadId) noexcept : IOThreadCallback(threadId) {}

    void IOThreadNextSyncTimeCallback() override {
        g_CreatureSmartIdManager.GarbageCollect();
        g_IOSyncDeadlockChecker.UpdateCurrentThread();
    }
};


} // namespace

NPCServer::NPCServer() : m_NpcPosFile(u"npcpos.txt") {
    L2_GUARD_FUNCTION();

    // Current L2NPC.exe 0x004ff71c. GetSystemInfo supplies the logical
    // processor count.
    m_nNPCs = 0;
    m_nCreated = 0;
    m_nTamed = 0;
    m_bShutDown = false;
    m_szMap[0] = u'\0';
    m_nThreads = 0;
    m_szServerAddress[0] = '\0';
    m_nServerPort = 0;
    m_timeStart = 0;
    m_szUser[0] = u'\0';
    m_szPassword[0] = u'\0';
    m_bViewDetail = false;
    m_nAdena = 0;
    m_pServerSocket = nullptr;
#if defined(_WIN32)
    SYSTEM_INFO systemInfo;
    ::GetSystemInfo(&systemInfo);
    const std::uint32_t processorCount = systemInfo.dwNumberOfProcessors;
#else
    const std::uint32_t processorCount = static_cast<std::uint32_t>(std::thread::hardware_concurrency());
#endif
    m_nThreads = static_cast<std::int32_t>(processorCount * UINT32_C(2));
    m_servertype = NORMAL_SERVER;
    m_hThread = nullptr;
}

NPCServer::~NPCServer() {
    L2_GUARD_FUNCTION();

    // Current L2NPC.exe 0x004ff5c4 does not close either handle here.
    m_pServerSocket = nullptr;
    m_hThread = nullptr;
}

bool NPCServer::InitWinsock() {
    L2_GUARD_FUNCTION();

    // Current L2NPC.exe 0x004fec48.
    const int error = portable::InitializeSocketSubsystem();
    if (error != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Can't initialize winsock. code=%d",
        //     error)
        // Log.Add(LOG_ERROR,
        //     "[startup] Socket subsystem initialization failed (code %d). Startup cannot continue.",
        //     error)
        L2_LOG_ERROR(Log, "[startup] Socket subsystem initialization failed (code %d); startup cannot continue.", error);
    }
    return error == 0;
}

bool NPCServer::Load() {
    L2_GUARD_FUNCTION();

    // Current L2NPC.exe 0x004ff00c. GetPrivateProfileString/Int are replaced
    // by the portable INI view. Target-owned fields and process globals are
    // still written here.
    const runtime::Config* configuration = l2npc::portable::GetNPCServerConfiguration();
    runtime::Config fallback;
    if (!configuration) {
        const char* const overridePath = std::getenv("L2NPC_INI");
        const std::string path = runtime::HasConfigDirectory()
            ? runtime::ConfigFile("l2npc.ini").string()
            : (overridePath && *overridePath ? overridePath : "l2npc.ini");
        if (!fallback.Load(path)) {
            // LOGE(
            //     "cannot read file \"L2NPC.INI\"")
            // Log.Add(LOG_ERROR, "[startup] Cannot load NPC configuration from '%s', startup cannot continue.",
            // path.c_str())
            L2_LOG_ERROR(Log, "[startup] Cannot load NPC configuration from '%s'; startup cannot continue.", path.c_str());
            return false;
        }
        configuration = &fallback;
    }
    if (!configuration->Loaded()) {
        // LOGE(
        //     "cannot read file \"L2NPC.INI\"")
        // Log.Add(LOG_ERROR, "[startup] NPC configuration '%s' is not loaded, startup cannot continue.",
        // configuration->Path().c_str())
        L2_LOG_ERROR(
            Log, "[startup] NPC configuration '%s' is not loaded; startup cannot continue.", configuration->Path().c_str()
        );
        return false;
    }

    std::u16string serverAddressWide = Utf8ToUtf16(configuration->Get("server", "address", "lunar"));
    if (serverAddressWide.size() > 0x3feU) {
        serverAddressWide.resize(0x3feU);
    }
    std::vector<char> serverAddressAnsi(std::max<std::size_t>(0x800U, serverAddressWide.size() * 3U + 1U), '\0');
    UnicodeToAnsi(serverAddressWide.data(), 0x400, serverAddressAnsi.data(), 0x800);
    CopyFixedNarrow(m_szServerAddress, std::string_view(serverAddressAnsi.data()));
    m_nServerPort = configuration->GetInt("server", "port", 2002);
    CopyFixedUtf16(m_szUser, configuration->Get("db", "user", ""));
    CopyFixedUtf16(m_szPassword, configuration->Get("db", "password", ""));
    // Both the current EXE and the later donor pass literal "map" as the
    // profile filename for this one lookup, not the l2npc.ini path.
    // GetPrivateProfileStringW does not diagnose a missing profile.
    runtime::Config mapConfiguration;
    const std::string mapDirectory =
        mapConfiguration.Load("map", false) ? mapConfiguration.Get("map", "directory", "") : std::string{};
    CopyFixedUtf16(m_szMap, mapDirectory);

    runtime::log::LogOptions logOptions;
    std::u16string logDAddressWide = Utf8ToUtf16(configuration->Get("LogD", "address", "127.0.0.1"));
    if (logDAddressWide.size() > 0x3feU) {
        logDAddressWide.resize(0x3feU);
    }
    std::vector<char> logDAddressAnsi(std::max<std::size_t>(0x800U, logDAddressWide.size() * 3U + 1U), '\0');
    UnicodeToAnsi(logDAddressWide.data(), 0x400, logDAddressAnsi.data(), 0x800);
    logOptions.logDAddress.assign(logDAddressAnsi.data());
    logOptions.logDPort = static_cast<std::uint16_t>(configuration->GetInt("LogD", "port", 3999));
    g_nFileDuration = configuration->GetInt("LogD", "FileDuration", 30);
    // Current 0x004ff00c evaluates IDIV's remainder before the positive
    // duration gate. FileDuration=0 therefore keeps the shipping integer
    // divide exception instead of being normalized to the default first.
    const int fileDurationRemainder = runtime::Win64SignedRemainder(60, g_nFileDuration);
    if (fileDurationRemainder > 0 || g_nFileDuration < 1) {
        g_nFileDuration = 30;
    }
    g_nWorldId = configuration->GetInt("World", "WorldId", 1);
    logOptions.worldId = g_nWorldId;
    logOptions.fileDurationMinutes = g_nFileDuration;
    runtime::log::ConfigureLog(Log, std::move(logOptions));
    g_nUseChasePenalty = configuration->GetInt("Setting", "ChasePenalty", 0);
    const std::string freeOpenServer = configuration->Get("Setting", "FreeOpenServer", "false");
    // GetPrivateProfileStringW's count is stored first in the current body,
    // then the immediately following _wcsicmp result overwrites it.
    ::g_bFreeOpenServer = !freeOpenServer.empty();
    ::g_bFreeOpenServer = EqualsAsciiCaseInsensitive(freeOpenServer, "true");
    return true;
}

bool NPCServer::LoadNpcposData(int serverType [[maybe_unused]]) {
    // Exact donor name/signature and NPCServer.cpp:170-207. Current target
    // 0x004ff864 confirms the same body. serverType survives in the public
    // ABI but neither build reads it.

#if defined(_WIN32)
    std::string path = "..\\script\\";
#else
    // The path denotes the same parent/script directory. Only the native
    // separator differs from the Win32 source literal.
    std::string path = "../script/";
#endif
    path += Utf16ToUtf8(runtime::StringView(m_NpcPosFile));
    const std::wstring widePath = Utf8ToWide(path);

    // Log.Add(
    //     LOG_NORMAL,
    //     L"Loading %s",
    //     widePath.c_str())
    L2_LOG_INFO(Log, u"[NPC maker] Loading spawn definitions from '%ls'.", widePath.c_str());
    const bool loaded = l2npc::g_NpcMakerDB.Load(widePath.c_str());
    if (!loaded) {
        // Noise: CNpcMakerDB::Load already reports the read/parser failure with file and applicable lexer line.
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Loading file '%s' failed",
        //     widePath.c_str())
    }
    return loaded;
}

void NPCServer::Run() {
    L2_GUARD_FUNCTION();

    // Current L2NPC.exe 0x004ff988. The calls and their order below follow
    // the shipping body. NPCServerRuntimeServices replaces only Sleep,
    // IOCP thread creation/WaitThread and activation of the host socket
    // readers.
    const l2npc::portable::NPCServerRuntimeServices runtime = l2npc::portable::GetNPCServerRuntimeServices();
    const auto stopRequested = [&runtime] { return runtime.stopRequested && runtime.stopRequested(); };
    const auto waitForRetryOrStop = [&runtime] {
        if (runtime.waitForRetryOrStop) {
            return runtime.waitForRetryOrStop();
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return false;
    };
    const auto startupFailed = [&runtime] {
        if (runtime.startupFailed) {
            runtime.startupFailed();
        }
    };

    if (stopRequested()) {
        return;
    }
    if (!Load() || !InitWinsock()) {
        startupFailed();
        return;
    }

    l2npc::gPacketTableFromServer.TestRegistration();
    l2npc::gPacketTableFromCache.TestRegistration();
    CCategoryDataDB::Inst().Load();
    l2geo::g_SuperPointManager.Load();
    l2geo::CSuperPointInfoManager::Inst().Load();
    g_FreewayManager.Load();
    g_FormationManager.Load();

    // Log.Add(
    //     LOG_NORMAL,
    //     L"Loading npc ai")
    // Log.Add(LOG_NORMAL,
    //     "[startup] Loading NPC AI...")
    L2_LOG_INFO(Log, "[startup] Loading NPC AI...");
    if (!CNPCAIManager::LoadNPCAI(false)) {
        L2_LOG_ERROR(Log, "[startup] NPC AI loading failed; terminating L2NPC.");
        // Noise: The error is recorded in the main log. Retain only the Windows modal copy.
        // PopUpWindow(
        //     L"NPC AI loading failed!")
#if defined(_WIN32)
        PopUpWindow(L"NPC AI loading failed!");
#endif
        ForceCrash(CrashInfo::NPC_AI_LOADING_FAILED);
    }

    LoadNpcposData(g_L2ServerType);
    g_Localization.LoadLocalizationData(14);
    if (stopRequested()) {
        return;
    }

    // InitIOThread owns the termination reset, completion backend and fixed
    // buffer-pool order, matching current 0x00455978.
    InitIOThread();

    // LOG(
    //     "trying to connect Server (%s:%d)",
    //     m_szServerAddress.data(),
    //     m_nServerPort)
    // Log.Add(LOG_NORMAL,
    //     "[L2Server] Connecting to %s:%u. Failed startup attempts retry after a one-second wait unless stopped.",
    //     m_szServerAddress.data(), static_cast<unsigned int>(static_cast<std::uint16_t>(m_nServerPort)))
    L2_LOG_INFO(
        Log, "[L2Server] Connecting to %s:%u; failed startup attempts retry after a one-second wait unless stopped.",
        m_szServerAddress.data(), static_cast<unsigned int>(static_cast<std::uint16_t>(m_nServerPort))
    );
    do {
        CSocket* const connected = Connect(m_szServerAddress.data(), m_nServerPort, l2npc::CServerSocket::Allocate);
        m_pServerSocket = static_cast<l2npc::CServerSocket*>(connected);
        if (m_pServerSocket) {
            break;
        }
        if (waitForRetryOrStop()) {
            return;
        }
        // Noise: The preceding failure identifies the endpoint and stage. Startup retry policy is already reported once.
        // LOG(
        //     "trying to connect Server (%s:%d)",
        //     m_szServerAddress.data(),
        //     m_nServerPort)
    } while (!stopRequested());
    if (!m_pServerSocket || stopRequested()) {
        return;
    }

    if (g_bFastEnterworld) {
        l2npc::g_NpcMakerDB.WaitForL2ServerPreparation();
    }

    // Log.Add(LOG_NORMAL,
    //     "[CacheD] Starting the NPC connection, failed startup attempts retry after a one-second wait unless stopped.")
    L2_LOG_INFO(
        Log, "[CacheD] Starting the NPC connection; failed startup attempts retry after a one-second wait unless stopped."
    );
    while (!l2npc::CDBNPC::Init()) {
        if (stopRequested()) {
            return;
        }
        // Noise: CDBNPC::InitDBNPC reports the failed stage and endpoint for this attempt.
        // LOG("trying to connect DB")
        if (waitForRetryOrStop()) {
            return;
        }
    }

    l2npc::dbnpc.AddTimer(1000U, 0);
    Log.ConnectToLogD();

    const int ioThreadCount = ::GetIOThreadCount();
    if (ioThreadCount > 0 && runtime.startIOThreads) {
        runtime.startIOThreads(ioThreadCount, [](int threadId) { return std::make_shared<CIOThreadCallbackNpc>(threadId); });
    }
    if (runtime.startSocketReceivers) {
        runtime.startSocketReceivers();
    }
    if (stopRequested()) {
        return;
    }

    // The two sscanf_s calls in the target only recover these embedded
    // revision integers before sending them.
    Send("cddd", 0, 272, 289, 0x4b88);
    // Log.Add(
    //     LOG_NORMAL,
    //     "NC_VERSION(%d) ->",
    //     272)
    // Log.Add(LOG_NORMAL,
    //     "[L2Server] Submitted the NPC protocol announcement (revision %d). Peer acceptance is not yet confirmed.",
    //     272)
    L2_LOG_INFO(
        Log, "[L2Server] Submitted the NPC protocol announcement (revision %d); peer acceptance is not yet confirmed.", 272
    );

    ReverseHateManager::inst().Init();
    l2npc::EventManager::Inst().LoadEventData();
    if (!g_bFastEnterworld) {
        l2npc::g_NpcMakerDB.CreateAllNPC();
    }
    l2npc::g_LottoGameManager.Initialize();

    // Current L2NPC.exe keeps this discarded call immediately before
    // WaitThread. Its 0x00516b64 body returns true, but Run does not branch on
    // the result.
    ExceptionMailing();
    if (runtime.startupReady) {
        runtime.startupReady();
    }
    WaitThread(nullptr);
}

void NPCServerThread_common() {
    // Current L2NPC.exe 0x004ffd94 is only this call. The nearby PDB supplies
    // the original source-level name.
    g_ns.Run();
}

void NPCServerThread_mailing() {
    // The nearby PDB exposes the wrapper. Current 0x004ffde0 selects the same
    // wrapper because ExceptionMailing at 0x00516b64 always returns true.
    NPCServerThread_common();
}

unsigned NPCServerThread(void* argument [[maybe_unused]]) {

    // Current L2NPC.exe 0x004ffde0: register LISTEN (11), construct its
    // 0x40-entry default buffer pool, then select the mailing/common wrapper.
    CThreadManager::RegisterThisThread(ThreadID::LISTEN);
    InitializeCurrentIOBufferPool(0x40);
    if (ExceptionMailing()) {
        NPCServerThread_mailing();
    } else {
        NPCServerThread_common();
    }
    return 0;
}

void NPCServer::Start() {
    L2_GUARD_FUNCTION();

    // Current L2NPC.exe 0x004ffe6c. The portable binding replaces only
    // _beginthreadex. Publication remains the target's m_hThread write and
    // CThreadManager LISTEN-slot update.
    m_hThread = l2npc::portable::StartNPCServerThread();
    CThreadManager::SetThread(ThreadID::LISTEN, m_hThread);
}

CSocket* NPCServer::Connect(char* address, int port, SocketAllocator allocator) {
    L2_GUARD_FUNCTION();

    // Current L2NPC.exe 0x004fed14. Exact donor NPCServer.cpp:117-160.
    // Portable/NPCServerConnection replaces only the socket/bind/connect
    // system calls. The allocator, ORT_ANONYMOUS keep and virtual
    // Initialize call remain in the original owner and order.
    const l2npc::portable::NPCServerTransportConnection connection =
        l2npc::portable::ConnectNPCServerTransport(address, static_cast<std::uint16_t>(port));
    if (!connection) {
        // switch (connection.connectResult.failure) {
        // case network::SocketConnectFailure::CreateSocket:
        //     Log.Add(
        //         LOG_WARN,
        //         "Can't make socket")
        //     break.
        // case network::SocketConnectFailure::BindSocket:
        //     Log.Add(
        //         LOG_WARN,
        //         "Can't bind socket")
        //     break.
        // case network::SocketConnectFailure::None:
        //     break.
        // default:
        //     Log.Add(
        //         LOG_WARN,
        //         "failed to make connection. code=%d",
        //         connection.connectResult.error)
        //     break.
        // }
        const char* stage = "preparing the transport binding";
        const char* errorDomain = "transport";
        switch (connection.connectResult.failure) {
            case network::SocketConnectFailure::Initialize:
                stage = "initializing the transport";
                break;
            case network::SocketConnectFailure::ResolveAddress:
                stage = "resolving the address";
                errorDomain = "getaddrinfo";
                break;
            case network::SocketConnectFailure::CreateSocket:
                stage = "creating the socket";
                break;
            case network::SocketConnectFailure::BindSocket:
                stage = "binding the local socket";
                break;
            case network::SocketConnectFailure::ConnectSocket:
                stage = "connecting the socket";
                break;
            case network::SocketConnectFailure::None:
                break;
        }
        // Log.Add(LOG_WARN,
        //     "[L2Server] Could not establish the connection to %s:%u while %s (%s code %d%s).",
        //     address ? address : "(unavailable)",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), stage, errorDomain,
        //     connection.connectResult.error,
        //     connection.connectResult.error == 0 ? ", no error code was reported" : "")
        L2_LOG_WARN(
            Log, "[L2Server] Could not establish the connection to %s:%u while %s (%s code %d%s).",
            address ? address : "(unavailable)", static_cast<unsigned int>(static_cast<std::uint16_t>(port)), stage,
            errorDomain, connection.connectResult.error,
            connection.connectResult.error == 0 ? "; no error code was reported" : ""
        );
        return nullptr;
    }

    CSocket* const socket = allocator(connection.socket);
    if (!connection.Attach(*socket)) {
        // Log.Add(LOG_ERROR,
        //     "[L2Server] Could not attach the NPC socket to the connection to %s:%u. Initialization failed.",
        //     address ? address : "(unavailable)",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)))
        L2_LOG_ERROR(
            Log, "[L2Server] Could not attach the NPC socket to the connection to %s:%u; initialization failed.",
            address ? address : "(unavailable)", static_cast<unsigned int>(static_cast<std::uint16_t>(port))
        );
        L2_RELEASE(socket, ::ObjectRefType::CreateDelete);
        connection.Abort();
        return nullptr;
    }

    L2_ADD_REF(socket, ::ObjectRefType::Anonymous);
    socket->Initialize();
    return socket;
}

void NPCServer::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();

    // Current L2NPC.exe 0x004feec0. The original method is only a null
    // connection guard followed by the virtual CSocket::SendV call.
    if (!m_pServerSocket) {
        return;
    }

    va_list arguments;
    va_start(arguments, format);
    m_pServerSocket->SendV(format, arguments);
    va_end(arguments);
}

int NPCServer::PendingWrite() {
    L2_GUARD_FUNCTION();

    // Current L2NPC.exe 0x004fef70 reads CSocket::m_nPendingWrite directly
    // and returns zero while m_pServerSocket is null.
    return m_pServerSocket ? m_pServerSocket->PendingWrite() : 0;
}

void NPCServer::CloseServerSocket() {
    L2_GUARD_FUNCTION();

    // The nearby PDB keeps this declaration, but the current linker removed
    // its body. The NASC64_2015 build supplies the socket close followed by
    // WSACleanup.
    if (m_pServerSocket) {
        m_pServerSocket->Close();
    }
    portable::CleanupSocketSubsystem();
}

void NPCServer::SendV(const char* format, va_list arguments) {
    L2_GUARD_FUNCTION();

    // PDB declaration, linker-eliminated in the current image. The donor body
    // is the same null guard and virtual SendV dispatch used by Send.
    if (m_pServerSocket) {
        m_pServerSocket->SendV(format, arguments);
    }
}

void NPCServer::SendBuffer(std::uint8_t* buffer, int length) {
    L2_GUARD_FUNCTION();

    // PDB declaration, linker-eliminated in the current image. The surviving
    // donor body enters the ordinary socket assembler with format "b".
    if (m_pServerSocket) {
        m_pServerSocket->Send("b", buffer, length);
    }
}

void NPCServer::SendInstantTeleport(
    CSharedCreatureData& target,
    const FVector& destination,
    int heading,
    int argument4,
    std::int64_t argument5,
    TeleportSourceType::Enum sourceType,
    CNPC* source,
    int tailArgument
) {
    // Current L2NPC.exe 0x004ff3ac reads all eight source arguments. The
    // nearby PDB build records only seven, while NASC64_2015 independently
    // keeps the eight-argument declaration, so the machine code wins here.
    const int sourceObjectId = source ? static_cast<int>(source->Id()) : 0;
    const int sourceNpcClassId = source ? source->m_data->m_nClassID : 0;
    // Current 0x004ff3ac resolves both source fields before taking the target
    // position snapshot. A non-null source has an unchecked m_data invariant.
    const FVector oldPosition = target.GetPos();
    g_ns.Send(
        "cddddddQdddcSdddd", 0x1f, target.m_nId, runtime::Win64CvttDoubleToInt32(destination.x),
        runtime::Win64CvttDoubleToInt32(destination.y), runtime::Win64CvttDoubleToInt32(destination.z), heading,
        argument4, static_cast<long long>(argument5), static_cast<int>(sourceType), sourceObjectId, sourceNpcClassId,
        target.m_bIsUser ? 1 : 0, target.Name(), runtime::Win64CvttDoubleToInt32(oldPosition.x),
        runtime::Win64CvttDoubleToInt32(oldPosition.y), runtime::Win64CvttDoubleToInt32(oldPosition.z), tailArgument
    );
}

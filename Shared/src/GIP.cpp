#include "Shared/src/Log.h"
#include "Shared/src/Runtime/SourceFile.h"
#include "Shared/src/Runtime/Utf8.h"

#include "Shared/src/GIP.h"

#include "Shared/src/Runtime/L2ServerClient.h"
#include "Shared/src/Runtime/NativeThreadHandle.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/SocketTransport.h"
#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Runtime.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GIPSocket.h"
#include "Shared/src/IOC.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

struct CGameInfoPipelinePortableAccess {
    static std::array<char16_t, 256>& FileName(CGameInfoPipeline& pipeline) { return pipeline.m_fileName; }

    static std::uint64_t& FileHandle(CGameInfoPipeline& pipeline) { return pipeline.m_fileHandle; }

    static int& FileYear(CGameInfoPipeline& pipeline) { return pipeline.m_fileYear; }
    static int& FileMonth(CGameInfoPipeline& pipeline) { return pipeline.m_fileMonth; }
    static int& FileDay(CGameInfoPipeline& pipeline) { return pipeline.m_fileDay; }
    static int& FileHour(CGameInfoPipeline& pipeline) { return pipeline.m_fileHour; }
    static int& FileHalfHour(CGameInfoPipeline& pipeline) { return pipeline.m_fileHalfHour; }
    static CGIPSocket*& Socket(CGameInfoPipeline& pipeline) { return pipeline.m_socket; }
    static bool& Connected(CGameInfoPipeline& pipeline) { return pipeline.m_connected; }
    static bool& Sendable(CGameInfoPipeline& pipeline) { return pipeline.m_sendable; }
    static std::uint32_t& WorkerThreadId(CGameInfoPipeline& pipeline) { return pipeline.m_workerThreadId; }
    static std::uint64_t& WorkerThreadHandle(CGameInfoPipeline& pipeline) { return pipeline.m_workerThreadHandle; }
};

namespace {

struct GIPConnectionContext {
    explicit GIPConnectionContext(std::uint64_t value) noexcept : generation(value) {}

    std::uint64_t generation;
    CGIPSocket* socket = nullptr;
};

// Portable diagnostic counters only. The recovered pipeline layout is unchanged.
enum class GIPFileOperation : std::size_t { Open, Write, Flush, Close, Inspect, Remove, Count };

struct GIPPortableState {
    GIPPortableState() : transport("gip") {}

    std::recursive_mutex connectionMutex;
    std::mutex fileMutex;
    std::mutex workerMutex;
    std::condition_variable wake;
    std::thread worker;
    network::L2ServerClient transport;
    portable::SocketTransportBinding socketTransport;
    runtime::process::GameInfoPipelineConfiguration config;
    std::atomic<std::uint64_t> generation{0};
    std::ofstream file;
    std::filesystem::path filePath;
    // Protected by fileMutex, like the stream they describe.
    std::array<std::uint64_t, static_cast<std::size_t>(GIPFileOperation::Count)> fileFailures{};
    std::uint64_t totalFileFailures = 0;
    std::uint64_t skippedFileRecords = 0;
    bool started = false;
    bool stopping = false;
};

void CountGIPDiagnostic(std::uint64_t& count) noexcept {
    if (count != UINT64_MAX) {
        ++count;
    }
}

bool ReportGIPDiagnosticCount(std::uint64_t count) noexcept { return count != 0 && (count & (count - 1)) == 0; }

// File failures must remain visible during CRT teardown and must not recurse
// through CLog's own files or depend on the lifetime of the global CLog object.
void EmitGIPDiagnostic(runtime::logging::Level level, std::array<char, 8192>& message, int length) noexcept {
    if (length < 0) {
        // runtime::logging::Write("GIP", level,
        //     "[GIP] File or record-format diagnostic could not be formatted.")
        runtime::logging::Write("GIP", level, "[GIP] File or record-format diagnostic could not be formatted.", __FILE__);
        return;
    }
    if (static_cast<std::size_t>(length) >= message.size()) {
        constexpr std::string_view suffix = " [truncated]";
        // std::copy(suffix.begin(), suffix.end(),
        //     message.end() - 1 - static_cast<std::ptrdiff_t>(suffix.size()))
        // message.back() = '\0'.
        std::size_t end = message.size() - 1 - suffix.size();
        while (end > 0 && (static_cast<unsigned char>(message[end]) & 0xc0U) == 0x80U) {
            --end;
        }
        std::copy(suffix.begin(), suffix.end(), message.data() + end);
        message[end + suffix.size()] = '\0';
    }
    // runtime::logging::Write("GIP", level, message.data())
    runtime::logging::Write("GIP", level, message.data(), __FILE__);
}

void ReportGIPFileFailure(GIPPortableState& state, GIPFileOperation operation, const std::error_code& error = {}) noexcept {
    const auto index = static_cast<std::size_t>(operation);
    auto& failures = state.fileFailures[index];
    CountGIPDiagnostic(failures);
    CountGIPDiagnostic(state.totalFileFailures);
    // Noise: Repeated failures of the same file operation are reported at
    // counts 1, 2, 4, .... Recovery/shutdown reports the accumulated total.
    if (!ReportGIPDiagnosticCount(failures)) {
        return;
    }
    const int savedErrno = errno;
    const auto level = operation == GIPFileOperation::Inspect || operation == GIPFileOperation::Remove
        ? runtime::logging::Level::Warning
        : runtime::logging::Level::Error;
    try {
        constexpr const char* descriptions[] = {"Cannot open local output file", "Cannot write local output file",
            "Cannot flush local output file", "Cannot close local output file", "Cannot inspect the previous output file",
            "Cannot remove an empty output file"};
        constexpr const char* outcomes[] = {"Local records will be skipped while no file is open.",
            "The write did not complete; failed output is not replayed.", "Buffered output could not be confirmed flushed.",
            "File closure could not be confirmed successful.", "Empty-file cleanup was skipped.",
            "The empty file could not be removed."};
        std::array<char, 256> detail{};
        if (error) {
            // This is the filesystem operation's error_code, not a stale errno.
            std::snprintf(
                detail.data(), detail.size(), "; filesystem error %d, category %.80s", error.value(), error.category().name()
            );
        }
        std::array<char, 8192> message{};
        const int length = std::snprintf(
            message.data(), message.size(), "[GIP] %s \"%s\" (%llu failures%s). %s", descriptions[index],
            runtime::Utf8Bytes(state.filePath.u8string()).c_str(), static_cast<unsigned long long>(failures),
            detail.data(), outcomes[index]
        );
        EmitGIPDiagnostic(level, message, length);
    } catch (...) {
        // runtime::logging::Write("GIP", level,
        //     "[GIP] A file operation failed, its diagnostic details could not be formatted.")
        runtime::logging::Write(
            "GIP", level, "[GIP] A file operation failed; its diagnostic details could not be formatted.", __FILE__
        );
    }
    errno = savedErrno;
}

void ReportGIPFileState(GIPPortableState& state, bool opened) noexcept {
    if (state.totalFileFailures == 0 && state.skippedFileRecords == 0) {
        return;
    }
    if (!opened && state.skippedFileRecords == 0 &&
        state.fileFailures[static_cast<std::size_t>(GIPFileOperation::Open)] == 0 &&
        state.fileFailures[static_cast<std::size_t>(GIPFileOperation::Write)] == 0 &&
        state.fileFailures[static_cast<std::size_t>(GIPFileOperation::Flush)] == 0 &&
        state.fileFailures[static_cast<std::size_t>(GIPFileOperation::Close)] == 0) {
        return; // Cleanup warnings alone do not make shutdown an ERROR.
    }
    const int savedErrno = errno;
    const auto level = opened ? runtime::logging::Level::Info : runtime::logging::Level::Error;
    try {
        std::array<char, 8192> message{};
        const int length = std::snprintf(
            message.data(), message.size(),
            "[GIP] %s \"%s\" after %llu file-operation failures; %llu local records were skipped while no file was open. "
            "Failed output is not replayed.",
            opened ? "Opened local output file" : "Stopped local output for",
            runtime::Utf8Bytes(state.filePath.u8string()).c_str(),
            static_cast<unsigned long long>(state.totalFileFailures),
            static_cast<unsigned long long>(state.skippedFileRecords)
        );
        EmitGIPDiagnostic(level, message, length);
    } catch (...) {
        // runtime::logging::Write("GIP", level,
        //     "[GIP] Local file status changed, its diagnostic details could not be formatted.")
        runtime::logging::Write(
            "GIP", level, "[GIP] Local file status changed; its diagnostic details could not be formatted.", __FILE__
        );
    }
    if (opened) {
        state.fileFailures.fill(0);
        state.totalFileFailures = 0;
        state.skippedFileRecords = 0;
    }
    errno = savedErrno;
}

void ReportGIPFormatFailure() noexcept {
    static thread_local std::uint64_t failures = 0;
    CountGIPDiagnostic(failures);
    // Noise: Do not report each repeated formatting failure on a busy worker.
    if (!ReportGIPDiagnosticCount(failures)) {
        return;
    }
    const int savedErrno = errno;
    std::array<char, 8192> message{};
    const int length = std::snprintf(
        message.data(), message.size(),
        "[GIP] Record formatting failed or exceeded 499 wide characters; discarding the record (%llu failures on this "
        "thread).",
        static_cast<unsigned long long>(failures)
    );
    EmitGIPDiagnostic(runtime::logging::Level::Error, message, length);
    errno = savedErrno;
}

using GIPPortableStatePtr = std::shared_ptr<GIPPortableState>;
using GIPPortableStateMap = std::unordered_map<CGameInfoPipeline*, GIPPortableStatePtr>;

std::mutex& StateMapMutex() {
    static auto* const mutex = new std::mutex;
    return *mutex;
}

GIPPortableStateMap& StateMap() {
    static auto* const states = new GIPPortableStateMap;
    return *states;
}

GIPPortableStatePtr PortableState(CGameInfoPipeline* pipeline) {
    std::lock_guard<std::mutex> guard(StateMapMutex());
    GIPPortableStateMap& states = StateMap();
    const auto found = states.find(pipeline);
    if (found != states.end()) {
        return found->second;
    }
    auto state = std::make_shared<GIPPortableState>();
    states.emplace(pipeline, state);
    return state;
}

GIPPortableStatePtr DetachPortableState(CGameInfoPipeline* pipeline) {
    std::lock_guard<std::mutex> guard(StateMapMutex());
    GIPPortableStateMap& states = StateMap();
    const auto found = states.find(pipeline);
    if (found == states.end()) {
        return {};
    }
    GIPPortableStatePtr state = found->second;
    states.erase(found);
    return state;
}

void StopPortableState(CGameInfoPipeline& pipeline, const GIPPortableStatePtr& state) {
    if (!state) {
        return;
    }

    {
        std::lock_guard<std::mutex> guard(state->workerMutex);
        state->stopping = true;
    }
    state->wake.notify_all();
    state->generation.fetch_add(1, std::memory_order_acq_rel);
    state->transport.Close();
    if (state->worker.joinable()) {
        state->worker.join();
    }
    if (state->config.logicalThreadId >= 0) {
        CThreadManager::SetThread(state->config.logicalThreadId, nullptr);
    }

    {
        std::lock_guard<std::mutex> guard(state->fileMutex);
        if (state->file.is_open()) {
            const bool flushStartedGood = state->file.good();
            state->file.flush();
            if (flushStartedGood && !state->file.good()) {
                ReportGIPFileFailure(*state, GIPFileOperation::Flush);
            }
            const bool closeStartedGood = state->file.good();
            state->file.close();
            if (closeStartedGood && !state->file.good()) {
                ReportGIPFileFailure(*state, GIPFileOperation::Close);
            }
        }
        ReportGIPFileState(*state, false);
        CGameInfoPipelinePortableAccess::FileHandle(pipeline) = UINT64_MAX;
    }
    state->socketTransport.Reset();
    CGameInfoPipelinePortableAccess::Socket(pipeline) = nullptr;
    CGameInfoPipelinePortableAccess::Connected(pipeline) = false;
    CGameInfoPipelinePortableAccess::Sendable(pipeline) = false;
    CGameInfoPipelinePortableAccess::WorkerThreadId(pipeline) = 0;
    CGameInfoPipelinePortableAccess::WorkerThreadHandle(pipeline) = 0;
}

std::int64_t CurrentEpochSeconds() { return static_cast<std::int64_t>(std::time(nullptr)); }

void AppendAscii(std::u16string& destination, const std::string& value) {
    for (const char character : value) {
        destination.push_back(static_cast<char16_t>(static_cast<unsigned char>(character)));
    }
}

std::string GIPFileName(int year, int month, int day, int hour, int halfHour, int worldId) {
#if defined(L2_BUILD_L2SERVER)
    constexpr int kProductNumber = 1;
#else
    // Current Cached.exe uses 2. Retain the existing peer value for other
    // products that share this physical owner.
    constexpr int kProductNumber = 2;
#endif
    std::array<char, 256> name{};
    const int length = std::snprintf(
        name.data(), name.size(), "%04d-%02d-%02d-100%02d-%d-%02d-%d.log", year, month, day, hour, halfHour, worldId,
        kProductNumber
    );
    if (length <= 0) {
        return {};
    }
    return std::string(name.data(), std::min(static_cast<std::size_t>(length), name.size() - 1U));
}

void CopyTargetFileName(CGameInfoPipeline& pipeline, const std::string& fileName) {
    const std::u16string value = Utf8ToUtf16(std::string("GIP\\") + fileName);
    const std::size_t count = std::min(value.size(), CGameInfoPipelinePortableAccess::FileName(pipeline).size() - 1U);
    std::fill(
        CGameInfoPipelinePortableAccess::FileName(pipeline).begin(),
        CGameInfoPipelinePortableAccess::FileName(pipeline).end(), u'\0'
    );
    std::copy_n(value.begin(), count, CGameInfoPipelinePortableAccess::FileName(pipeline).begin());
}

void CreateNewGIPFileLocked(
    CGameInfoPipeline& pipeline, const GIPPortableStatePtr& state, int year, int month, int day, int hour, int halfHour
) {
    if (state->file.is_open()) {
        const bool flushStartedGood = state->file.good();
        state->file.flush();
        if (flushStartedGood && !state->file.good()) {
            ReportGIPFileFailure(*state, GIPFileOperation::Flush);
        }
        const bool closeStartedGood = state->file.good();
        state->file.close();
        if (closeStartedGood && !state->file.good()) {
            ReportGIPFileFailure(*state, GIPFileOperation::Close);
        }
        std::error_code sizeError;
        const std::uintmax_t size = std::filesystem::file_size(state->filePath, sizeError);
        if (!sizeError && size == 0U) {
            std::error_code removeError;
            std::filesystem::remove(state->filePath, removeError);
            if (removeError) {
                ReportGIPFileFailure(*state, GIPFileOperation::Remove, removeError);
            }
        }
        if (sizeError) {
            ReportGIPFileFailure(*state, GIPFileOperation::Inspect, sizeError);
        }
    }

    CGameInfoPipelinePortableAccess::FileYear(pipeline) = year;
    CGameInfoPipelinePortableAccess::FileMonth(pipeline) = month;
    CGameInfoPipelinePortableAccess::FileDay(pipeline) = day;
    CGameInfoPipelinePortableAccess::FileHour(pipeline) = hour;
    CGameInfoPipelinePortableAccess::FileHalfHour(pipeline) = halfHour;

    const std::string fileName = GIPFileName(year, month, day, hour, halfHour, state->config.worldId);
    std::error_code directoryError;
    std::filesystem::create_directories("GIP", directoryError);
    state->filePath = std::filesystem::path("GIP") / fileName;
    state->file.open(state->filePath, std::ios::binary | std::ios::out | std::ios::app);
    CGameInfoPipelinePortableAccess::FileHandle(pipeline) = state->file.is_open() ? UINT64_C(1) : UINT64_MAX;
    CopyTargetFileName(pipeline, fileName);
    if (!state->file.is_open()) {
        ReportGIPFileFailure(*state, GIPFileOperation::Open, directoryError);
    } else {
        ReportGIPFileState(*state, true);
    }
}

void OnPortableBytes(
    const GIPPortableStatePtr& state,
    const std::shared_ptr<GIPConnectionContext>& connection,
    const std::uint8_t* data,
    int length
) {
    if (!state || !connection || !data || length <= 0) {
        return;
    }
    if (state->generation.load(std::memory_order_acquire) != connection->generation) {
        return;
    }
    CGIPSocket* const socket = connection->socket;
    if (!socket ||
        !portable::FeedSocketRead(static_cast<CIOSocketEx<IOBuffer8k>*>(socket), data, static_cast<std::size_t>(length))) {
        state->transport.RejectPeerProtocol();
    }
}

void OnPortableClose(const GIPPortableStatePtr& state, const std::shared_ptr<GIPConnectionContext>& connection) {
    if (!state || !connection) {
        return;
    }
    if (state->generation.load(std::memory_order_acquire) != connection->generation) {
        return;
    }
    CGIPSocket* const socket = connection->socket;
    if (!socket) {
        return;
    }
    const int readError = state->transport.LastReadError();
    portable::FeedSocketTerminal(static_cast<CIOSocketEx<IOBuffer8k>*>(socket), readError);
    connection->socket = nullptr;
}

} // namespace

CGameInfoPipeline GIP;

void GIPConnectThread_common() {
    // Current Cached.exe 0x004d9be4, 95 bytes, exact SHA-256
    // 29b0f0bd08366f91c1f051c39bd4d93fbfe33cd2ee0b0b27aa687c776561791e.
    // The shipping loop is process-lifetime. The condition-variable wake is
    // solely the portable equivalent which lets orderly process shutdown
    // interrupt Sleep without changing any live iteration.
    const GIPPortableStatePtr state = PortableState(&GIP);
    for (;;) {
        {
            std::lock_guard<std::mutex> guard(state->workerMutex);
            if (state->stopping) {
                return;
            }
        }

        const int threadId = CThreadManager::GetThreadID();
        g_dwGuardStartTick[threadId] = runtime::GetTickCount();
        GIP.TryConnectingToDaemon();

        std::unique_lock<std::mutex> guard(state->workerMutex);
        const auto delay = std::chrono::milliseconds(state->config.heartbeatMilliseconds);
        if (state->wake.wait_for(guard, delay, [&state] { return state->stopping; })) {
            return;
        }
    }
}

void GIPConnectThread_mailing() {
    // Current Cached.exe 0x004d9c44 is the SEH/mailing boundary around the
    // common loop. A normal return exists only for portable shutdown.
    try {
        GIPConnectThread_common();
    } catch (...) {
        runtime::process::HandleUnhandledSharedThreadException(L2_SOURCE_FILE, __LINE__, __TIMESTAMP__);
    }
}

unsigned int GIPConnectThread(void* argument) {
    // Current Cached.exe 0x004d9c94 registers logical thread 0x11, builds
    // both 0x200-entry IO-buffer pools and enters the constant-true mailing
    // branch. The original parameter is unused.
    const GIPPortableStatePtr state = PortableState(&GIP);
    if (state->config.logicalThreadId >= 0) {
        CThreadManager::RegisterThisThread(state->config.logicalThreadId);
    }
    if (state->config.ioBufferPoolSize > 0) {
        const int threadId = CThreadManager::GetThreadID();
        g_IOBufferPools[threadId] = new (std::nothrow)
#if defined(L2_BUILD_L2SERVER)
            CNewMwsrQueue<DefaultIOBuffer>(state->config.ioBufferPoolSize);
#else
            CMwsrQueue<DefaultIOBuffer>(state->config.ioBufferPoolSize);
#endif
        g_IOBuffer8kPools[threadId] = new (std::nothrow)
#if defined(L2_BUILD_L2SERVER)
            CNewMwsrQueue<IOBuffer8k>(state->config.ioBufferPoolSize);
#else
            CMwsrQueue<IOBuffer8k>(state->config.ioBufferPoolSize);
#endif
    }

#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004dacec is the constant true condition.
    GIPConnectThread_mailing();
#else
    if (ExceptionMailing()) {
        GIPConnectThread_mailing();
    } else {
        GIPConnectThread_common();
    }
#endif
    return 0;
}

CGameInfoPipeline::CGameInfoPipeline() :
    m_socket(nullptr),
    m_connected(false),
    m_sendable(false),
    m_padding00a{},
    m_workerThreadId(0),
    m_workerThreadHandle(0),
    m_connectionLock{},
    m_fileHandle(UINT64_MAX),
    m_fileName{},
    m_fileLock{},
    m_fileYear(0),
    m_fileMonth(0),
    m_fileDay(0),
    m_fileHour(0),
    m_fileHalfHour(0),
    m_failCount(0) {
    // Current Cached.exe constructor 0x004d9d6c creates the worker suspended.
    // Init performs the sole release.
    L2_GUARD_FUNCTION();
    const GIPPortableStatePtr state = PortableState(this);
    state->worker = std::thread([state] {
        {
            std::unique_lock<std::mutex> guard(state->workerMutex);
            state->wake.wait(guard, [&state] { return state->started || state->stopping; });
            if (state->stopping) {
                return;
            }
        }
        GIPConnectThread(nullptr);
    });
    m_workerThreadHandle =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(runtime::NativeThreadHandle(state->worker)));
    m_workerThreadId = runtime::NativeThreadId(state->worker);
}

CGameInfoPipeline::~CGameInfoPipeline() {
    L2_GUARD_FUNCTION();
    const GIPPortableStatePtr state = DetachPortableState(this);
    StopPortableState(*this, state);
}

void runtime::StopGameInfoPipeline(CGameInfoPipeline& pipeline) {
    StopPortableState(pipeline, DetachPortableState(&pipeline));
}

void CGameInfoPipeline::SetSendable(bool sendable) {
    L2_GUARD_FUNCTION();
    m_sendable = sendable;
    if (!sendable) {
        m_failCount = 0;
    }
}

void CGameInfoPipeline::Init() {
    // Current Cached.exe 0x004d9328, 155 bytes, SHA-256
    // 3e4a26cc745ddaf51c3e9e32dce83e6b290ab204c857094c654daff981c8e605.
    // ResumeThread on the constructor-created suspended Win32 worker becomes
    // the equivalent condition-variable release on portable hosts.
    L2_GUARD_FUNCTION();
    const GIPPortableStatePtr state = PortableState(this);
    state->config = runtime::process::ReadGameInfoPipelineConfiguration();
    if (!state->config.enabled) {
        return;
    }
    if (state->config.logicalThreadId >= 0) {
        // The Windows constructor publishes the suspended _beginthreadex
        // handle immediately. The portable worker is condition-variable
        // suspended, so publish its pthread token at the equivalent Init
        // transition immediately before releasing it.
        CThreadManager::SetThread(
            state->config.logicalThreadId, reinterpret_cast<void*>(static_cast<std::uintptr_t>(m_workerThreadHandle))
        );
    }
    {
        std::lock_guard<std::mutex> guard(state->workerMutex);
        state->started = true;
    }
    state->wake.notify_all();
}

bool CGameInfoPipeline::ConnectToDaemon() {
    L2_GUARD_FUNCTION();
    const GIPPortableStatePtr state = PortableState(this);
    const std::uint64_t generation = state->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    const auto connection = std::make_shared<GIPConnectionContext>(generation);
    state->transport.SetReceiveSink(
        [state, connection](const std::uint8_t* data, int length) { OnPortableBytes(state, connection, data, length); },
        [state, connection] { OnPortableClose(state, connection); }
    );

    network::L2ServerClient::ConnectOptions options;
    options.startReceiving = false;
    options.blocking = true;
    options.numericIPv4Only = true;
    if (!state->transport.Connect(state->config.address, state->config.port, options)) {
        const std::uint32_t previous = m_failCount++;
        if (previous % 6U == 0U) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Can't connect to GIP server")
            // Log.Add(
            //     LOG_ERROR,
            //     "[GIP] Cannot connect to %s:%u. Retry interval is %u ms.",
            //     state->config.address.c_str(),
            //     static_cast<unsigned int>(state->config.port),
            //     static_cast<unsigned int>(state->config.heartbeatMilliseconds))
            L2_LOG_ERROR(
                Log, "[GIP] Cannot connect to %s:%u; retry interval is %u ms.", state->config.address.c_str(),
                static_cast<unsigned int>(state->config.port), static_cast<unsigned int>(state->config.heartbeatMilliseconds)
            );
        }
        return false;
    }

    auto* const socket = new CGIPSocket(generation, GIPPacketTable);
    connection->socket = socket;
    m_socket = socket;
    const std::weak_ptr<GIPPortableState> weakState = state;
    state->socketTransport.Reset(
        socket,
        [weakState, generation](const std::uint8_t* data, int length, portable::SocketWriteCompletion completion) {
            const GIPPortableStatePtr locked = weakState.lock();
            if (!locked || locked->generation.load(std::memory_order_acquire) != generation) {
                SetSocketLastError(10038);
                return portable::SocketWriteSubmission::Failed();
            }
            return locked->transport.SubmitRawWrite(data, length, std::move(completion));
        },
        [weakState, generation] {
            const GIPPortableStatePtr locked = weakState.lock();
            if (!locked || locked->generation.load(std::memory_order_acquire) != generation) {
                return;
            }
            locked->transport.RejectPeerProtocol();
        },
        [weakState, generation] {
            const GIPPortableStatePtr locked = weakState.lock();
            if (!locked || locked->generation.load(std::memory_order_acquire) != generation) {
                return;
            }
            locked->transport.InitializeTargetSocket();
        },
        [weakState, generation] {
            const GIPPortableStatePtr locked = weakState.lock();
            if (!locked || locked->generation.load(std::memory_order_acquire) != generation) {
                return;
            }
            locked->transport.RejectPeerProtocol(false);
        },
        [weakState, generation](std::size_t capacity) {
            const GIPPortableStatePtr locked = weakState.lock();
            if (!locked || locked->generation.load(std::memory_order_acquire) != generation) {
                return false;
            }
            locked->transport.SetNextReadCapacity(static_cast<int>(capacity));
            return true;
        }
    );

    socket->Initialize();
    if (socket->m_hSocket.load(std::memory_order_acquire) == UINT64_C(0xffffffffffffffff)) {
        const std::uint32_t previous = m_failCount++;
        if (previous % 6U == 0U) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Connected to GIP server, But Some error has occurred")
            // Log.Add(
            //     LOG_ERROR,
            //     "[GIP] Connection %llu to %s:%u closed during socket initialization. Retry interval is %u ms.",
            //     static_cast<unsigned long long>(generation),
            //     state->config.address.c_str(),
            //     static_cast<unsigned int>(state->config.port),
            //     static_cast<unsigned int>(state->config.heartbeatMilliseconds))
            L2_LOG_ERROR(
                Log, "[GIP] Connection %llu to %s:%u closed during socket initialization; retry interval is %u ms.",
                static_cast<unsigned long long>(generation), state->config.address.c_str(),
                static_cast<unsigned int>(state->config.port), static_cast<unsigned int>(state->config.heartbeatMilliseconds)
            );
        }
        return false;
    }
    state->transport.StartReceiving();

    // Log.Add(
    //     LOG_NORMAL,
    //     L"Connected to GIP server")
    // Log.Add(
    //     LOG_NORMAL,
    //     "[GIP] TCP connection %llu to %s:%u established. Starting the handshake.",
    //     static_cast<unsigned long long>(generation),
    //     state->config.address.c_str(),
    //     static_cast<unsigned int>(state->config.port))
    L2_LOG_INFO(
        Log, "[GIP] TCP connection %llu to %s:%u established; starting the handshake.",
        static_cast<unsigned long long>(generation), state->config.address.c_str(),
        static_cast<unsigned int>(state->config.port)
    );
    socket->Send("hhhhd", 1, 0x20, static_cast<int>(static_cast<std::int16_t>(state->config.worldId)), 2, 0);
    m_failCount = 0;
    return true;
}

void CGameInfoPipeline::_CreateNewGIPFile(int year, int month, int day, int hour, int halfHour) {
    L2_GUARD_FUNCTION();
    const GIPPortableStatePtr state = PortableState(this);
    CreateNewGIPFileLocked(*this, state, year, month, day, hour, halfHour);
}

void CGameInfoPipeline::FlushGIPFile() {
    L2_GUARD_FUNCTION();
    const GIPPortableStatePtr state = PortableState(this);
    std::lock_guard<std::mutex> guard(state->fileMutex);
    if (!state->file.is_open()) {
        return;
    }
    constexpr std::string_view message = "\nException occured. Flush Flush!!!\n";
    state->file.write(message.data(), static_cast<std::streamsize>(message.size()));
    if (!state->file.good()) {
        ReportGIPFileFailure(*state, GIPFileOperation::Write);
    }
    const bool flushStartedGood = state->file.good();
    state->file.flush();
    if (flushStartedGood && !state->file.good()) {
        ReportGIPFileFailure(*state, GIPFileOperation::Flush);
    }
}

void CGameInfoPipeline::TryConnectingToDaemon() {
    L2_GUARD_FUNCTION();
    const GIPPortableStatePtr state = PortableState(this);
    std::lock_guard<std::recursive_mutex> guard(state->connectionMutex);
    if (!state->config.enabled) {
        return;
    }

    if (!m_connected) {
        m_connected = ConnectToDaemon();
    } else if (!m_sendable) {
        // Log.Add(
        //     LOG_WARN,
        //     "[GIP] Handshake with %s:%u is still incomplete at the next connection check. Closing before retrying.",
        //     state->config.address.c_str(),
        //     static_cast<unsigned int>(state->config.port))
        L2_LOG_WARN(
            Log, "[GIP] Handshake with %s:%u is still incomplete at the next connection check; closing before retrying.",
            state->config.address.c_str(), static_cast<unsigned int>(state->config.port)
        );
        m_connected = false;
        if (m_socket) {
            m_socket->Close();
        }
    } else {
        ++m_failCount;
        if (m_socket) {
            m_socket->Send("h", 0xff);
        }
    }
}

void CGameInfoPipeline::CheckGIPFile() {
    L2_GUARD_FUNCTION();
    const GIPPortableStatePtr state = PortableState(this);
    std::lock_guard<std::mutex> guard(state->fileMutex);
    const runtime::SystemTime now = runtime::GetLocalTimeNow();
    const int halfHour = now.wMinute / 30;
    if (m_fileYear != now.wYear || m_fileMonth != now.wMonth || m_fileDay != now.wDay || m_fileHour != now.wHour ||
        m_fileHalfHour != halfHour) {
        _CreateNewGIPFile(now.wYear, now.wMonth, now.wDay, now.wHour, halfHour);
    }
}

void CGameInfoPipeline::AddV(const wchar_t* format, va_list arguments) {
    L2_GUARD_FUNCTION();
    std::array<wchar_t, 500> command{};
    int formatted = 0;
    {
        GuardHelper formatGuard(L"GIP::AddV - vswprintf_s", CThreadManager::GetThreadID());
        const std::wstring hostFormat = WindowsWideFormatToHost(format);
        va_list copy;
        va_copy(copy, arguments);
        formatted = std::vswprintf(command.data(), command.size(), hostFormat.c_str(), copy);
        va_end(copy);
    }
    if (formatted < 0) {
        ReportGIPFormatFailure();
    }
    if (formatted <= 0) {
        return;
    }

    // Both target formatting calls use `%d` for the _time64 result. The
    // Win64 vararg consumer therefore observes its low signed 32 bits.
    const std::int32_t timestamp = static_cast<std::int32_t>(CurrentEpochSeconds());
    if (!m_connected || !m_sendable) {
        CheckGIPFile();
        const GIPPortableStatePtr state = PortableState(this);
        // Current helper 0x004c9508 is WideCharToMultiByte(CP_ACP), not a
        // UTF-8 conversion.
        std::string line = portable::WindowsAnsiFromWide(command.data());
        line += ", ";
        line += std::to_string(timestamp);
        line += ")\r\n";
        std::lock_guard<std::mutex> guard(state->fileMutex);
        if (state->file.is_open()) {
            state->file.write(line.data(), static_cast<std::streamsize>(line.size()));
            if (!state->file.good()) {
                ReportGIPFileFailure(*state, GIPFileOperation::Write);
            }
        } else {
            // Noise: The failed open already explains the unavailable file.
            // count skipped records for recovery/shutdown instead of repeating it.
            CountGIPDiagnostic(state->skippedFileRecords);
        }
        return;
    }

    std::u16string line = WideToUtf16(command.data());
    line += u", ";
    AppendAscii(line, std::to_string(timestamp));
    line.push_back(u')');

    CPacketHelperEx<20480> packet;
    packet.Assemble("hhS", 4, static_cast<int>(line.size()), line.c_str());
    // Current AddV sends the helper buffer from byte zero while removing
    // only the final UTF-16 NUL produced by S. The connected/sendable path
    // dereferences m_socket unconditionally.
    m_socket->Send("b", packet.GetLength() - 2, packet.GetPacket());
}

void CGameInfoPipeline::_Add(const wchar_t* format, ...) {
    L2_GUARD_FUNCTION();
    const GIPPortableStatePtr state = PortableState(this);
    if (!state->config.enabled) {
        return;
    }

    va_list arguments;
    va_start(arguments, format);
    // Original: AddV(format, arguments)
    try {
        AddV(format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

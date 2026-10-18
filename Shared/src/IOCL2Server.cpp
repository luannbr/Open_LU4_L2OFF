#include "IOC.h"
#include "Shared/src/Runtime/DirectLog.h"

#include "Auth.h"
#include "Captcha.h"
#include "NpcSocket.h"
#include "SecureINIFileReader.h"

#include "L2Core/src/inc/FileUtils.h"
#include "L2Core/src/inc/GuardProfile.h"
#include "Shared/src/Runtime/AcceptedSocketTransport.h"
#include "Shared/src/Runtime/IOThreadRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/SourceFile.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WaitThreadRuntime.h"
#include "Shared/src/Runtime/Win64Integer.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GIP.h"
#include "Shared/src/IOCs.h"
#include "Shared/src/LockStatusStack.h"
#include "Shared/src/Log.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/proto/Stream.h"
#include "platform/Runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#include <Windows.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

std::int64_t g_WorldChatPacketSendCount = 0;
std::int64_t g_WorldChatPacketSendSize = 0;
CIOCriticalSection g_globalLockTimer;
std::vector<void*> g_vHandle;
std::vector<CIOObject*> g_vObject;
CIOSyncDeadlockChecker<16> g_IOSyncDeadlockChecker;
CThreadLocalTimer g_ThreadLocalTimer;

namespace {

// Portable replacement for current L2Server's one completion-port handle.
// Its logical queue structure and callback order remain in this physical
// Shared/ioc.cpp creation.
IOThreadRuntime g_l2ServerIOThreadRuntime;

std::int64_t SignedTickDifference(std::uint64_t current, std::uint64_t previous) noexcept {
    const std::uint64_t difference = current - previous;
    std::int64_t result = 0;
    static_assert(sizeof(result) == sizeof(difference));
    std::memcpy(&result, &difference, sizeof(result));
    return result;
}

void CrashInvalidIOLockStack(const wchar_t* guardName, const std::source_location location) {
    GuardHelper guard(guardName, CThreadManager::GetThreadID());
    ForceCrash(CrashInfo::LOCK_STACK_IS_NOT_EMPTY, location);
}

void CheckIOLockStack(const wchar_t* guardName, const std::source_location location = std::source_location::current()) {
    const int threadId = CThreadManager::GetThreadID();
    if (!g_LockStack[static_cast<std::size_t>(threadId)].IsEmpty()) {
        CrashInvalidIOLockStack(guardName, location);
    }
}

#if !defined(_WIN32)
int DescriptorFromWaitHandle(void* handle) {
    const std::uintptr_t encoded = reinterpret_cast<std::uintptr_t>(handle);
    if (encoded == 0 || encoded - 1 > static_cast<std::uintptr_t>(std::numeric_limits<int>::max())) {
        return -1;
    }
    return static_cast<int>(encoded - 1);
}

std::optional<std::size_t> WaitForNativeSocket(
    void* const* handles, std::size_t handleCount, std::chrono::milliseconds timeout
) {
    std::vector<pollfd> descriptors;
    descriptors.reserve(handleCount);
    for (std::size_t index = 0; index < handleCount; ++index) {
        descriptors.push_back({DescriptorFromWaitHandle(handles[index]), static_cast<short>(POLLIN | POLLPRI), 0});
    }
    if (descriptors.empty()) {
        return std::nullopt;
    }
    const auto boundedTimeout =
        std::min<std::int64_t>(std::max<std::int64_t>(timeout.count(), 0), std::numeric_limits<int>::max());
    const int result = ::poll(descriptors.data(), descriptors.size(), static_cast<int>(boundedTimeout));
    if (result <= 0) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        if ((descriptors[index].revents & (POLLIN | POLLPRI)) != 0) {
            return index;
        }
    }
    return std::nullopt;
}
#endif

} // namespace

DefaultIOBuffer* MakeConstBufferFromPacketEx(
    const char packetId, const std::int16_t extendedPacketId, const network::IPacket& packet
) {
    // Current L2Server.exe 0x007cd928, physical owner Shared/ioc.cpp.
    L2_GUARD_FUNCTION();

    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    network::Stream stream(buffer->m_Buffer.data() + 5, 0x4ffb);
    if (!packet.Encode(&stream)) {
        buffer->Release();
        return nullptr;
    }

    const std::uint16_t packetSize = static_cast<std::uint16_t>(stream.GetCursor() - stream.GetBuffer() + 5);
    std::memcpy(buffer->m_Buffer.data(), &packetSize, sizeof(packetSize));
    buffer->m_Buffer[2] = static_cast<std::uint8_t>(packetId);
    std::memcpy(buffer->m_Buffer.data() + 3, &extendedPacketId, sizeof(extendedPacketId));
    buffer->m_dwSize = packetSize;
    return buffer;
}

// Current Singleton<CIODeadlockChecker>::inst at 0x00728b20 inlines this
// complete construction contract: last tick zero, thread -1, active true,
// then DeadlockCrashInfo {-1, 0, 0, false}. The default member initializers
// in ioc.h express those exact stores and keep the physical 0x40 layout.
CIODeadlockChecker::CIODeadlockChecker() = default;

CIODeadlockChecker::DeadlockCrashInfo::DeadlockCrashInfo() = default;

void CIODeadlockChecker::DeadlockCrashInfo::SetCrashInfo(int threadId, std::uint32_t crashTick, std::uint32_t lastTick) {
    m_bForcedCrash = true;
    m_CrashTick = crashTick;
    m_nLastTick = lastTick;
    m_nThreadID = threadId;
}

bool CIODeadlockChecker::DeadlockCrashInfo::ForcedCrashByDeadlockChecker() {
    // Current PrintCrashSummary 0x00728dcd reads the exact +0x0c byte.
    return m_bForcedCrash;
}

std::uint32_t CIODeadlockChecker::DeadlockCrashInfo::GetCrashTick() { return m_CrashTick; }

std::uint32_t CIODeadlockChecker::DeadlockCrashInfo::GetLastTick() { return m_nLastTick; }

int CIODeadlockChecker::DeadlockCrashInfo::GetThreadID() { return m_nThreadID; }

void CIODeadlockChecker::DeadlockCrashInfo::PrintLog(void* file) {
    // Current L2Server.exe 0x007cbc04. MSVC's argument evaluation loads the
    // two ticks before the thread ID and computes the unsigned difference
    // from those captured values.
    const std::uint32_t lastTick = m_nLastTick;
    const std::uint32_t crashTick = m_CrashTick;
    const std::uint32_t difference = crashTick - lastTick;
    const int threadId = m_nThreadID;
    hprintf(
        file,
        L"\tDeadlock or Super-Lag!!! ThreadID(%d), "
        L"CurrentTick[%u] - DeadlockCheckerTick[%u] = %u\r\n",
        threadId, crashTick, lastTick, difference
    );
}

void CIODeadlockChecker::TimerExpired(int) {
    // Current L2Server.exe 0x00727d3c: refresh the watchdog tick, capture the
    // logical TLS thread, then tail-rearm timer id zero for exactly 5000 ms.
    SetShortTimeStamp(&m_nLastTick, 0);
    m_nThreadID = CThreadManager::GetThreadID();
    AddTimer(5000, 0);
}

void CIODeadlockChecker::Init() {
    m_nThreadID = CThreadManager::GetThreadID();
    TimerExpired(0);
}

bool CIODeadlockChecker::IsOver49Days(std::uint32_t currentTick, std::uint32_t lastTick) { return currentTick < lastTick; }

bool CIODeadlockChecker::IsTimeOver(std::uint32_t currentTick, std::uint32_t lastTick) {
    return m_bActive && !IsOver49Days(currentTick, lastTick) && static_cast<std::int32_t>(currentTick - lastTick) > 60000;
}

void CIODeadlockChecker::TestDeadlock() {
    const std::uint32_t lastTick = m_nLastTick;
    const std::uint32_t currentTick = runtime::GetTickCount();
    if (!IsTimeOver(currentTick, lastTick)) {
        return;
    }
    // Noise: ProcessDeadlock below reports the same failure with both ticks and the crash action.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[TestDeadlock] last: %u, cur: %u",
    //     lastTick,
    //     currentTick)
    Singleton<CIODeadlockChecker>::inst().ProcessDeadlock(currentTick, lastTick);
}

void CIODeadlockChecker::Test49Days() {
    const std::uint32_t lastTick = m_nLastTick;
    const std::uint32_t currentTick = runtime::GetTickCount();
    if (!Singleton<CIODeadlockChecker>::inst().IsOver49Days(currentTick, lastTick)) {
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[Test49Days] last: %u, cur: %u",
    //     lastTick,
    //     currentTick)
    // Log.Add(
    //     LOG_ERROR,
    //     "[watchdog] The 32-bit tick counter decreased from %u ms to %u ms. The legacy rollover check is forcing a crash
    //     report.", static_cast<unsigned>(lastTick), static_cast<unsigned>(currentTick))
    L2_LOG_ERROR(
        Log,
        "[watchdog] The 32-bit tick counter decreased from %u ms to %u ms; the legacy rollover check is forcing a crash "
        "report.",
        static_cast<unsigned>(lastTick), static_cast<unsigned>(currentTick)
    );
    ForceCrash(CrashInfo::SERVER_BOOT_TIME_IS_OVER_49_DAYS);
}

void CIODeadlockChecker::ProcessDeadlock(std::uint32_t currentTick, std::uint32_t lastTick) {
    // Current L2Server.exe 0x007cbaa0.
    L2_GUARD_FUNCTION();
    m_DeadlockCrashInfo.SetCrashInfo(m_nThreadID, currentTick, lastTick);
    const std::uint32_t differenceSeconds = (currentTick - lastTick) / 1000U;
    // Log.Add(
    //     LOG_LOCAL,
    //     L"!!! ThreadID(%d), deadlock or super-lag "
    //     L"detected!!!  (diff %u sec) ",
    //     m_nThreadID,
    //     differenceSeconds)
    // Log.Add(
    //     LOG_LOCAL,
    //     "[watchdog] IO timer heartbeat has not refreshed for %u seconds (last tick %u ms, current tick %u ms. Last
    //     heartbeat on worker %d). Forcing a crash report. A stalled heartbeat alone does not prove a deadlock.",
    //     static_cast<unsigned>(differenceSeconds), static_cast<unsigned>(lastTick),
    //     static_cast<unsigned>(currentTick), m_nThreadID)
    // runtime::log::WriteErrorWithLocalCopy(
    //     Log,
    //     "[watchdog] IO timer heartbeat has not refreshed for %u seconds (last tick %u ms, current tick %u ms. Last
    //     heartbeat on worker %d). Forcing a crash report. A stalled heartbeat alone does not prove a deadlock.",
    //     static_cast<unsigned>(differenceSeconds), static_cast<unsigned>(lastTick),
    //     static_cast<unsigned>(currentTick), m_nThreadID)
    L2_LOG_ERROR_WITH_LOCAL_COPY(
        Log,
        "[watchdog] IO timer heartbeat has not refreshed for %u seconds (last tick %u ms, current tick %u ms; last "
        "heartbeat on worker %d); forcing a crash report. A stalled heartbeat alone does not prove a deadlock.",
        static_cast<unsigned>(differenceSeconds), static_cast<unsigned>(lastTick), static_cast<unsigned>(currentTick),
        m_nThreadID
    );
    // Log.Add(
    //     LOG_ERROR,
    //     L"!!! ThreadID(%d), deadlock or super-lag "
    //     L"detected!!!  (diff %u sec) ",
    //     m_DeadlockCrashInfo.m_nThreadID,
    //     differenceSeconds)
    // Noise: WriteErrorWithLocalCopy above already emits this error once and keeps both destinations.
    // Log.Add(
    //     LOG_ERROR,
    //     "[watchdog] IO timer heartbeat has not refreshed for %u seconds (last tick %u ms, current tick %u ms. Last
    //     heartbeat on worker %d). Forcing a crash report. A stalled heartbeat alone does not prove a deadlock.",
    //     static_cast<unsigned>(differenceSeconds), static_cast<unsigned>(lastTick),
    //     static_cast<unsigned>(currentTick), m_DeadlockCrashInfo.m_nThreadID)
    ForceCrash(CrashInfo::BY_DEADLOCK_CHECKER);
}

CDeadlockCheckerInactivator::CDeadlockCheckerInactivator() { Singleton<CIODeadlockChecker>::inst().m_bActive = false; }

CDeadlockCheckerInactivator::~CDeadlockCheckerInactivator() {
    CIODeadlockChecker& checker = Singleton<CIODeadlockChecker>::inst();
    CIOObject::SetShortTimeStamp(&checker.m_nLastTick, 0);
    checker.m_nThreadID = CThreadManager::GetThreadID();
    checker.m_bActive = true;
}

void CAuthPacket::OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) {
    L2_DECLARE_AUTO_TIMER(timer, JOB_AUTHSOCKET__TOTAL);
    CAuthPacketBase::OnIOCallback(success, transferred, overlapped);
}

void CNPCPacket::OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) {
    L2_DECLARE_AUTO_TIMER(timer, JOB_NPCSOCKET__TOTAL);
    CNPCPacketBase::OnIOCallback(success, transferred, overlapped);
}

void CCaptchaPacket::OnIOCallback(int, std::uint32_t transferred, PortableOverlapped*) {
    // Current L2Server.exe 0x007cd804, physical owner Shared/ioc.cpp. The
    // Captcha protocol owns a 16-bit opcode, so this specialization advances
    // two bytes instead of the generic CPacketEx one-byte packet id.
    L2_DECLARE_AUTO_TIMER(timer, JOB_CAPTCHASOCKET_TOTAL);
    const std::uint8_t* const packet = m_pBuf->m_Buffer.data() + static_cast<std::size_t>(transferred) + 2;
    if (m_pFunc(m_pSocket, packet)) {
        m_pSocket->Close();
    }
    L2_RELEASE(m_pSocket, ObjectRefType::IoRetain);
    m_pBuf->Release();
    CAbstractPacket::g_nPendingPacket.fetch_sub(1, std::memory_order_seq_cst);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

int GetIOThreadCount() {
    // Current L2Server.exe 0x007cb144, physical owner Shared/ioc.cpp: use
    // signed CMP/CMOVG: keep negative int32 lanes and cap only above 16.
#if defined(_WIN32)
    SYSTEM_INFO systemInfo;
    ::GetSystemInfo(&systemInfo);
    const unsigned processors = systemInfo.dwNumberOfProcessors;
#else
    const unsigned processors = std::thread::hardware_concurrency();
#endif
    return std::min(16, runtime::Int32FromBits(processors));
}

void InitializeIOBufferPool() {
    // Current L2Server.exe 0x007cb278 reads the already loaded process-wide
    // SecureINI owner after the CConfig::Load startup predecessor.
    L2_GUARD_FUNCTION();
    const int totalBufferCount =
        l2server::g_SecureINIFileDB.GetInt(u"Setting", u"IOBufferCount", 0x20000, u".\\l2server.ini");
    const int threadCount = GetIOThreadCount();
    // The current body performs IDIV before testing the positive loop bound.
    const int buffersPerThread = runtime::Win64SignedDivide(totalBufferCount, threadCount);
    if (threadCount <= 0) {
        return;
    }
    for (int threadId = 0; threadId < threadCount; ++threadId) {
        g_IOBufferPools[threadId] = new (std::nothrow) CNewMwsrQueue<DefaultIOBuffer>(buffersPerThread);
        g_IOBuffer8kPools[threadId] = new (std::nothrow) CNewMwsrQueue<IOBuffer8k>(0x200);
        g_IOBuffer32kPools[threadId] = new (std::nothrow) CNewMwsrQueue<IOBuffer32k>(0x80);
    }
}

void InitIOThread() {
    // Current L2Server.exe 0x007cb3d8 creates one event and one completion
    // port. IOThreadRuntime replaces those kernel transports only.
    L2_GUARD_FUNCTION();
    g_bTerminating = 0;
    g_l2ServerIOThreadRuntime.Install();
    InitializeIOBufferPool();
}

void AddLog(char* format, ...) {
    L2_GUARD_FUNCTION();
    char formatted[0x200]{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(formatted, sizeof(formatted), format ? format : "", arguments);
    va_end(arguments);
}

template <std::size_t ThreadCount>
void CIOSyncDeadlockChecker<ThreadCount>::CheckForDeadlock() {
    const std::uint64_t currentTick = runtime::GetTickCount();
    for (std::size_t threadId = 0; threadId < ThreadCount; ++threadId) {
        const std::uint64_t lastTick = m_lastTickArr[threadId].tick64.fetch_add(0, std::memory_order_seq_cst);
        const std::int64_t difference = SignedTickDifference(currentTick, lastTick);
        if (lastTick == 0 || difference <= 60000) {
            continue;
        }
        m_lastTickArr[threadId].tick64.exchange(0, std::memory_order_seq_cst);
        const auto seconds = static_cast<unsigned>(difference / 1000);
        // constexpr const wchar_t* format =
        //     L"!!! [IOSyncDeadlock] ThreadID(%d), "
        //     L"deadlock or super-lag detected!!!  "
        //     L"(diff %u sec) ".
        // Log.Add(
        //     LOG_LOCAL,
        //     format,
        //     static_cast<int>(threadId),
        //     seconds)
        // Log.Add(
        //     LOG_LOCAL,
        //     "[watchdog] IO worker %d has not refreshed its synchronization heartbeat for %u seconds (last tick %llu ms,
        //     current tick %llu ms), capturing call and lock stacks.", static_cast<int>(threadId), seconds,
        //     static_cast<unsigned long long>(lastTick), static_cast<unsigned long long>(currentTick))
        // runtime::log::WriteErrorWithLocalCopy(
        //     Log,
        //     "[watchdog] IO worker %d has not refreshed its synchronization heartbeat for %u seconds (last tick %llu ms,
        //     current tick %llu ms), capturing call and lock stacks.", static_cast<int>(threadId), seconds,
        //     static_cast<unsigned long long>(lastTick), static_cast<unsigned long long>(currentTick))
        L2_LOG_ERROR_WITH_LOCAL_COPY(
            Log,
            "[watchdog] IO worker %d has not refreshed its synchronization heartbeat for %u seconds (last tick %llu ms, "
            "current tick %llu ms); capturing call and lock stacks.",
            static_cast<int>(threadId), seconds, static_cast<unsigned long long>(lastTick),
            static_cast<unsigned long long>(currentTick)
        );
        // Log.Add(
        //     LOG_ERROR,
        //     format,
        //     static_cast<int>(threadId),
        //     seconds)
        // Noise: WriteErrorWithLocalCopy above already emits this error once and keeps both destinations.
        // Log.Add(
        //     LOG_ERROR,
        //     "[watchdog] IO worker %d has not refreshed its synchronization heartbeat for %u seconds (last tick %llu ms,
        //     current tick %llu ms), capturing call and lock stacks.", static_cast<int>(threadId), seconds,
        //     static_cast<unsigned long long>(lastTick), static_cast<unsigned long long>(currentTick))
        ReportCallStack(static_cast<int>(threadId), LOG_ERROR, false);
        ReportLockStack(static_cast<int>(threadId), L"Deadlock", LOG_ERROR);
    }
}

template <std::size_t ThreadCount>
void CIOSyncDeadlockChecker<ThreadCount>::UpdateCurrentThread() {
    const int threadId = CThreadManager::GetThreadID();
    if (threadId < 0 || threadId >= static_cast<int>(ThreadCount)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[IOSyncDeadlock] nThreadID over array index[%d]",
        //     threadId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[watchdog] Cannot update the IO synchronization heartbeat: worker index %d is outside the range 0..%llu. This
        //     update is skipped.", threadId, static_cast<unsigned long long>(ThreadCount - 1))
        L2_LOG_ERROR(
            Log,
            "[watchdog] Cannot update the IO synchronization heartbeat: worker index %d is outside the range 0..%llu; this "
            "update is skipped.",
            threadId, static_cast<unsigned long long>(ThreadCount - 1)
        );
        return;
    }
    m_lastTickArr[static_cast<std::size_t>(threadId)].tick64.exchange(runtime::GetTickCount(), std::memory_order_seq_cst);
}

template class CIOSyncDeadlockChecker<16>;

bool CIOObject::SetTimeStamp(std::int32_t* timestamp) {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    auto* const rawTimestamp = reinterpret_cast<volatile std::int32_t*>(timestamp);
    const std::int32_t previous = runtime::raw_atomic::LoadI32(rawTimestamp);
    if (previous < now) {
        runtime::raw_atomic::ExchangeI32(rawTimestamp, now);
    }
    return previous < now;
}

bool CIOObject::IsInTime(std::int32_t timestamp, int additionalSeconds) {
    const std::int32_t now = runtime::crt::Time32Now();
    const std::int32_t deadline =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(timestamp) + static_cast<std::uint32_t>(additionalSeconds));
    return timestamp != 0 && now < deadline;
}

bool CIOObject::SetShortTimeStamp(std::uint32_t* timestamp, std::uint32_t additionalMilliseconds) {
    L2_GUARD_FUNCTION();
    const std::uint32_t next = runtime::GetTickCount() + additionalMilliseconds;
    auto* const rawTimestamp = reinterpret_cast<volatile std::int32_t*>(timestamp);
    const std::uint32_t previous = static_cast<std::uint32_t>(runtime::raw_atomic::LoadI32Relaxed(rawTimestamp));
    if (previous < next) {
        runtime::raw_atomic::ExchangeI32(rawTimestamp, static_cast<std::int32_t>(next));
    }
    return previous < next;
}

bool CIOObject::IsInShortTime(std::uint32_t timestamp, std::uint32_t additionalMilliseconds) {
    return timestamp != 0 && runtime::GetTickCount() < timestamp + additionalMilliseconds;
}

namespace {

constexpr std::uint64_t kInvalidNativeSocket = UINT64_C(0xffffffffffffffff);
constexpr std::size_t kMaximumWaitObjects = 64;

void CloseNativeSocket(std::uint64_t socket) {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

// Current CIOObject::AddTimer2 clamps before entering the physical
// CThreadLocalTimer owner recovered in ThreadSafeLibrary.cpp.
constexpr std::int64_t kMinimumTimerMilliseconds = 80;

std::int64_t CurrentTimerTick() noexcept { return static_cast<std::int64_t>(runtime::GetTickCount()); }

int g_shortTimerWarningCount = 0;

} // namespace

CIOServer::CIOServer() {
    L2_GUARD_FUNCTION();
    m_hSocket = kInvalidNativeSocket;
    m_hAcceptEvent = nullptr;
}

CIOServer::~CIOServer() {
    L2_GUARD_FUNCTION();
    Close();
}

int CIOServer::Create(int port) {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    const SOCKET listenerSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    m_hSocket = static_cast<std::uint64_t>(listenerSocket);
#else
    const int listenerSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    m_hSocket = listenerSocket < 0 ? kInvalidNativeSocket : static_cast<std::uint64_t>(listenerSocket);
#endif
    if (m_hSocket == kInvalidNativeSocket) {
        // ::AddLog(
        //     const_cast<char*>("socket error %d"),
        //     GetSocketLastError())
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot create a TCP listener socket on 0.0.0.0:%u (socket error %d). Listener startup failed.",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError())
        L2_LOG_ERROR(
            Log, "[listener] Cannot create a TCP listener socket on 0.0.0.0:%u (socket error %d); listener startup failed.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError()
        );
        return 0;
    }

#if !defined(_WIN32)
    const int reuseAddress = 1;
    ::setsockopt(listenerSocket, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress));
#endif

    ::sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<std::uint16_t>(port));

#if defined(_WIN32)
    const int bindResult =
        ::bind(static_cast<SOCKET>(m_hSocket), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
#else
    const int bindResult = ::bind(static_cast<int>(m_hSocket), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
#endif
    if (bindResult != 0) {
        // ::AddLog(
        //     const_cast<char*>("bind error %d"),
        //     GetSocketLastError())
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot bind the TCP listener on 0.0.0.0:%u (socket error %d). Closing the listener socket.",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError())
        L2_LOG_ERROR(
            Log, "[listener] Cannot bind the TCP listener on 0.0.0.0:%u (socket error %d); closing the listener socket.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError()
        );
        Close();
        return 0;
    }

#if defined(_WIN32)
    const int listenResult = ::listen(static_cast<SOCKET>(m_hSocket), 5);
#else
    const int listenResult = ::listen(static_cast<int>(m_hSocket), 5);
#endif
    if (listenResult != 0) {
        // ::AddLog(
        //     const_cast<char*>("listen error %d"),
        //     GetSocketLastError())
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot listen on the TCP socket on 0.0.0.0:%u (socket error %d). Closing the listener socket.",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError())
        L2_LOG_ERROR(
            Log, "[listener] Cannot listen on the TCP socket on 0.0.0.0:%u (socket error %d); closing the listener socket.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError()
        );
        Close();
        return 0;
    }

#if defined(_WIN32)
    m_hAcceptEvent = ::WSACreateEvent();
    ::WSAEventSelect(static_cast<SOCKET>(m_hSocket), static_cast<WSAEVENT>(m_hAcceptEvent), FD_ACCEPT);
#else
    const int descriptor = static_cast<int>(m_hSocket);
    const int flags = ::fcntl(descriptor, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
    }
    m_hAcceptEvent = reinterpret_cast<void*>(static_cast<std::uintptr_t>(descriptor) + 1U);
#endif

    if (!RegisterWait(m_hAcceptEvent)) {
        // ::AddLog(
        //     const_cast<char*>("RegisterWait error on port %d"),
        //     port)
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot register the accept wait for TCP port %u. Closing the listener socket and stopping
        //     startup.", static_cast<unsigned>(static_cast<std::uint16_t>(port)))
        L2_LOG_ERROR(
            Log,
            "[listener] Cannot register the accept wait for TCP port %u; closing the listener socket and stopping startup.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port))
        );
        Close();
        return 0;
    }
    return 1;
}

void CIOServer::Close() {
    L2_GUARD_FUNCTION();
    if (m_hSocket != kInvalidNativeSocket) {
        CloseNativeSocket(m_hSocket);
        m_hSocket = kInvalidNativeSocket;
    }
    if (m_hAcceptEvent) {
#if defined(_WIN32)
        ::WSACloseEvent(static_cast<WSAEVENT>(m_hAcceptEvent));
#endif
        m_hAcceptEvent = nullptr;
    }
}

void CIOServer::OnWaitCallback() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    ::WSAResetEvent(static_cast<WSAEVENT>(m_hAcceptEvent));
#endif
    portable::PostIOCompletion(this, 0, nullptr);
}

void CIOServer::OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) {
    L2_GUARD_FUNCTION();
    static_cast<void>(success);
    static_cast<void>(transferred);
    static_cast<void>(overlapped);

    ::sockaddr_in clientAddress{};
#if defined(_WIN32)
    int clientAddressLength = sizeof(clientAddress);
    const SOCKET accepted =
        ::accept(static_cast<SOCKET>(m_hSocket), reinterpret_cast<sockaddr*>(&clientAddress), &clientAddressLength);
    const std::uint64_t acceptedSocket = static_cast<std::uint64_t>(accepted);
#else
    socklen_t clientAddressLength = sizeof(clientAddress);
    const int accepted =
        ::accept(static_cast<int>(m_hSocket), reinterpret_cast<sockaddr*>(&clientAddress), &clientAddressLength);
    const std::uint64_t acceptedSocket = accepted < 0 ? kInvalidNativeSocket : static_cast<std::uint64_t>(accepted);
#endif

    if (acceptedSocket == kInvalidNativeSocket) {
        const int error = GetSocketLastError();
        if (error != 10035 && m_hSocket != kInvalidNativeSocket) {
            // ::AddLog(
            //     const_cast<char*>("accept error: %d"),
            //     error)
            // Log.Add(
            //     LOG_ERROR,
            //     "[listener] Cannot accept the pending connection on listener socket %llu (socket error %d). This accept
            //     attempt is abandoned.", static_cast<unsigned long long>(m_hSocket), error)
            L2_LOG_ERROR(
                Log,
                "[listener] Cannot accept the pending connection on listener socket %llu (socket error %d); this accept "
                "attempt is abandoned.",
                static_cast<unsigned long long>(m_hSocket), error
            );
        }
        return;
    }

    CIOServerSocket* const socket = CreateSocket(acceptedSocket, &clientAddress);
    if (!socket) {
        CloseNativeSocket(acceptedSocket);
        // Log.Add(LOG_ERROR, "Socket Create fail")
        // Log.Add(
        //     LOG_WARN,
        //     "[listener] The socket factory declined connection %llu from %u.%u.%u.%u:%u. The accepted socket has been
        //     closed.", static_cast<unsigned long long>(acceptedSocket),
        //     static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 24) & 0xffU),
        //     static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 16) & 0xffU),
        //     static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 8) & 0xffU),
        //     static_cast<unsigned>(ntohl(clientAddress.sin_addr.s_addr) & 0xffU),
        //     static_cast<unsigned>(ntohs(clientAddress.sin_port)))
        L2_LOG_WARN(
            Log,
            "[listener] The socket factory declined connection %llu from %u.%u.%u.%u:%u; the accepted socket has been "
            "closed.",
            static_cast<unsigned long long>(acceptedSocket),
            static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 24) & 0xffU),
            static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 16) & 0xffU),
            static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 8) & 0xffU),
            static_cast<unsigned>(ntohl(clientAddress.sin_addr.s_addr) & 0xffU),
            static_cast<unsigned>(ntohs(clientAddress.sin_port))
        );
        return;
    }
    if (!portable::BindAcceptedSocketTransport(socket, acceptedSocket)) {
        // Log.Add(LOG_ERROR,
        //     "[listener] Cannot attach the IO transport to accepted connection %llu. Closing the connection before
        //     initialization.", static_cast<unsigned long long>(acceptedSocket))
        L2_LOG_ERROR(
            Log,
            "[listener] Cannot attach the IO transport to accepted connection %llu; closing the connection before "
            "initialization.",
            static_cast<unsigned long long>(acceptedSocket)
        );
        socket->Close();
        return;
    }
    socket->Initialize();
}

int CIOObject::RegisterWait(void* handle) {
    L2_GUARD_FUNCTION();
    L2_GUARD(g_globalLockTimer);
    if (g_vHandle.size() >= kMaximumWaitObjects) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CIOObject::RegisterWait failed. "
        //     L"count of handle >= %d",
        //     L"..\\Shared\\ioc.cpp",
        //     0x221,
        //     static_cast<int>(kMaximumWaitObjects))
        // Log.Add(
        //     LOG_ERROR,
        //     "[IO wait] Cannot register another wait handle: %llu handles are registered and the limit is %llu.
        //     registration is rejected.", static_cast<unsigned long long>(g_vHandle.size()), static_cast<unsigned long
        //     long>(kMaximumWaitObjects))
        L2_LOG_ERROR(
            Log,
            "[IO wait] Cannot register another wait handle: %llu handles are registered and the limit is %llu; registration "
            "is rejected.",
            static_cast<unsigned long long>(g_vHandle.size()), static_cast<unsigned long long>(kMaximumWaitObjects)
        );
        return 0;
    }
    g_vHandle.push_back(handle);
    g_vObject.push_back(this);
    return 1;
}

void SafeIOCallback(CIOObject* object, int id, std::int64_t uid) {
    L2_GUARD_FUNCTION();
    object->OnIOCallback(
        1, static_cast<std::uint32_t>(id), reinterpret_cast<PortableOverlapped*>(static_cast<std::uintptr_t>(uid))
    );
}

bool CIOObject::AddTimer(const std::int64_t delayMilliseconds, const int id) { return AddTimer2(delayMilliseconds, id, 0); }

bool CIOObject::AddTimer2(std::int64_t delayMilliseconds, int id, std::int64_t uid) {
    L2_GUARD_FUNCTION();
    if (delayMilliseconds < kMinimumTimerMilliseconds) {
        const int previous = g_shortTimerWarningCount++;
        if (previous < 30) {
            // LogIOError(
            //     L"dwTime[%d] < 80 !!!!!!!",
            //     static_cast<std::int32_t>(delayMilliseconds))
            // Log.Add(
            //     LOG_WARN,
            //     "[timer] Timer %d requested a delay of %lld ms (uid %lld). Clamped to 80 ms. Short-delay diagnostics are
            //     limited to the first 30 requests.", id, static_cast<long long>(delayMilliseconds), static_cast<long
            //     long>(uid))
            // Noise: a zero delay is a normal "run soon" request, for example a
            // vehicle start, so it is not a warning. Other short delays are.
            L2_LOG(
                Log, delayMilliseconds == 0 ? LOG_NORMAL : LOG_WARN,
                "[timer] Timer %d requested a delay of %lld ms (uid %lld); clamped to 80 ms. Short-delay diagnostics are "
                "limited to the first 30 requests.",
                id, static_cast<long long>(delayMilliseconds), static_cast<long long>(uid)
            );
            // zero-delay requests (including vehicle start)
            // keep the warning without a full stack. Preserve the recovered
            // unconditional report below. Other short delays still report it.
            // runtime::process::ReportCallStack(2, false)
            if (delayMilliseconds != 0) {
                runtime::process::ReportCallStack(2, false);
            }
        }
        delayMilliseconds = kMinimumTimerMilliseconds;
    }

    // Current L2Server only increments this diagnostic counter. Its complete
    // image has no decrementing xref. Normal dispatch therefore leaves the
    // cumulative value intact.
    g_ObjectInTimer.fetch_add(1, std::memory_order_seq_cst);
    L2_ADD_REF(this, ObjectRefType::Timer);
    const std::int64_t dueTick = runtime::AddInt64Wrapping(CurrentTimerTick(), delayMilliseconds);
    SetInTimer(true);
    g_ThreadLocalTimer.AddTimer(this, dueTick, id, uid);
    return true;
}

void CIOObject::TimerDispatch() {
    L2_GUARD_FUNCTION();
    g_ThreadLocalTimer.Dispatch();
}

namespace portable {

std::size_t CancelTimersForOwner(CIOObject* owner, std::size_t* nonzeroUidCount) {
    if (nonzeroUidCount) {
        *nonzeroUidCount = 0;
    }
    if (!owner) {
        return 0;
    }

    const std::size_t cancelled = g_ThreadLocalTimer.CancelOwnerForPortableShutdown(owner, nonzeroUidCount);
    if (cancelled == 0) {
        return 0;
    }

    // Portable embedded-host teardown only. The shipping process has no
    // cancellation API because these owners normally live to process exit.
    owner->SetInTimer(false);
    for (std::size_t index = 0; index < cancelled; ++index) {
        L2_RELEASE(owner, ObjectRefType::Timer);
    }
    return cancelled;
}

} // namespace portable

std::uint32_t WaitThread(void*) {
    // Current L2Server.exe 0x007cc924. The registered-wait presence is
    // snapshotted once. A non-empty snapshot still reads the live vectors on
    // every iteration, matching the shipping registered-wait behavior.
    Singleton<CIODeadlockChecker>::inst().Init();
    const portable::WaitThreadRuntimeServices runtime = portable::GetWaitThreadRuntimeServices();
    const bool hasRegisteredWaits = !g_vHandle.empty();

    for (;;) {
        GuardProfileHelper::InitializeGuardProfilePerThread();
        const int threadId = CThreadManager::GetThreadID();
        g_dwGuardStartTick[threadId] = runtime::GetTickCount();
        L2_GUARD_FUNCTION();

        const std::uint32_t iterationStart = runtime::GetTickCount();
        g_IOSyncDeadlockChecker.CheckForDeadlock();
        Singleton<CIODeadlockChecker>::inst().TestDeadlock();
        Singleton<CIODeadlockChecker>::inst().Test49Days();
        g_Timer.TurnOn();
        CIOObject::TimerDispatch();
        g_Timer.TurnOff();

        const std::uint32_t afterDispatch = runtime::GetTickCount();
        const std::uint32_t untilTenSeconds = iterationStart + 10000U - afterDispatch;
        const std::uint32_t waitMilliseconds = untilTenSeconds < 100U ? iterationStart + 10000U - runtime::GetTickCount() : 100U;
        const std::chrono::milliseconds timeout(waitMilliseconds);

        bool portableStopSignalled = false;
        if (hasRegisteredWaits) {
            std::optional<std::size_t> signalled;
#if defined(_WIN32)
            const DWORD result = ::WaitForMultipleObjects(
                static_cast<DWORD>(g_vHandle.size()), reinterpret_cast<const HANDLE*>(g_vHandle.data()), FALSE,
                waitMilliseconds
            );
            if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + g_vHandle.size()) {
                signalled = static_cast<std::size_t>(result - WAIT_OBJECT_0);
            }
#else
            if (runtime.waitForObjects) {
                signalled = runtime.waitForObjects(g_vHandle.data(), g_vHandle.size(), timeout);
            } else {
                signalled = WaitForNativeSocket(g_vHandle.data(), g_vHandle.size(), timeout);
            }
#endif
            if (signalled) {
                runtime::CheckedVectorAt(g_vObject, *signalled)->OnWaitCallback();
            }
        } else if (runtime.waitForStop) {
            portableStopSignalled = runtime.waitForStop(timeout);
        } else {
            std::this_thread::sleep_for(timeout);
        }

        if (g_bTerminating != 0) {
            ForceCrash(CrashInfo::CLOSE_IO_THREAD);
        }

        // Cooperative teardown is observed only after the complete shipping
        // iteration, so it cannot suppress watchdog, timer or crash behavior.
        if (portableStopSignalled || (runtime.stopRequested && runtime.stopRequested())) {
            return 0;
        }
    }
}

IOThreadCallback::IOThreadCallback(int threadId) noexcept : m_Thread(threadId) {}

void IOThreadCallback::IOThreadNextSyncTime() {
    // Current L2Server.exe 0x007cc748.
    L2_GUARD_FUNCTION();
    g_Timer.TurnOn();
    CIOObject::TimerDispatch();
    IOThreadNextSyncTimeCallback();
    g_Timer.TurnOff();
}

void IOThreadCallback::IOThread_common() {
    // Current L2Server.exe 0x007cce44. IOThreadRuntime replaces only
    // GetQueuedCompletionStatus. Deadlines, profiling, timer and callback
    // ordering remain in this owner.
    L2_GUARD_FUNCTION();
    std::uint32_t nextSyncTime = runtime::GetTickCount();
    InitializeRandomNumber();

    for (;;) {
        GuardProfileHelper::InitializeGuardProfilePerThread();
        const int threadId = CThreadManager::GetThreadID();
        g_dwGuardStartTick[threadId] = runtime::GetTickCount();
        GuardHelper iteration(L"void IOThread_common 1", threadId);

        const std::uint32_t now = runtime::GetTickCount();
        std::uint32_t waitMilliseconds = 0;
        if (nextSyncTime < now) {
            IOThreadNextSyncTime();
            nextSyncTime = runtime::GetTickCount() + 50U;
            waitMilliseconds = 50U;
        } else {
            waitMilliseconds = nextSyncTime - now;
        }

        IOThreadRuntime::QueueEntry entry;
        g_nRunningThread.fetch_add(1, std::memory_order_seq_cst);
        const IOThreadRuntime::BoundWaitResult waitResult = IOThreadRuntime::WaitBoundCompletion(waitMilliseconds, entry);
        g_nRunningThread.fetch_sub(1, std::memory_order_seq_cst);

        // The current body turns the general timer on after every IOCP wait,
        // including timeout. Only a dispatched callback turns it off here.
        g_Timer.TurnOn();
        if (waitResult == IOThreadRuntime::BoundWaitResult::Stop) {
            // This return is the portable process-lifetime escape. Complete
            // one synchronization boundary before releasing the worker.
            IOThreadNextSyncTime();
            return;
        }
        if (waitResult == IOThreadRuntime::BoundWaitResult::Timeout) {
            continue;
        }

        if (entry.success || entry.overlapped != nullptr || entry.object != nullptr) {
            CheckIOLockStack(L"Invalid LockStack before io!!!");
            if (entry.object) {
                if (!entry.success && entry.socketError != 0) {
                    SetSocketLastError(entry.socketError);
                }
                entry.object->OnIOCallback(entry.success, entry.transferred, entry.overlapped);
            }
            g_Timer.TurnOff();
            CheckIOLockStack(L"Invalid LockStack after io!!!");
        }
    }
}

void IOThreadCallback::IOThread_mailing() {
    // Current L2Server.exe 0x007cd200 wraps the process-lifetime loop in SEH.
    try {
        IOThread_common();
    } catch (const std::exception& error) {
        if (std::getenv("L2_IO_EXCEPTION_TRACE") != nullptr) {
            // std::fprintf(
            //     stderr,
            //     "[IO_EXCEPTION_TRACE] thread=%d what=%s\n",
            //     CThreadManager::GetThreadID(),
            //     error.what())
            L2_LOG_DIRECT(
                stderr, runtime::logging::Level::Error, "[IO exception trace] IO callback threw on worker %d: %s.",
                CThreadManager::GetThreadID(), error.what()
            );
#if defined(__APPLE__) || defined(__unix__)
            std::array<void*, 128> frames{};
            const int frameCount = ::backtrace(frames.data(), static_cast<int>(frames.size()));
            ::backtrace_symbols_fd(frames.data(), frameCount, ::fileno(stderr));
#endif
            std::fflush(stderr);
        }
        ELOG(L"*%S:%d(%S) exception\n", L2_SOURCE_FILE, __LINE__, __TIMESTAMP__);
        Log.FlushLogFile();
        GIP.FlushGIPFile();
        send_exception(true);
    } catch (...) {
        ELOG(L"*%S:%d(%S) exception\n", L2_SOURCE_FILE, __LINE__, __TIMESTAMP__);
        Log.FlushLogFile();
        GIP.FlushGIPFile();
        send_exception(true);
    }
}

void IOThreadCallback::IOThread_debug() {
    // Current L2Server.exe 0x007cd250 reaches INT3 only if its infinite loop
    // returns. Portable cooperative teardown is the sole non-shipping return.
    L2_GUARD_FUNCTION();
    IOThread_common();
}

std::uint32_t IOThread2(void* argument) {
    // Current L2Server.exe 0x007cd2dc.
    auto* const callback = static_cast<IOThreadCallback*>(argument);
    if (!callback) {
        return 0;
    }
    CThreadManager::RegisterThisThread(callback->m_Thread);
    if (ExceptionMailing()) {
        callback->IOThread_mailing();
    } else {
        callback->IOThread_debug();
    }
    return 0;
}

bool CreateIOThread2(IOThreadCallback* callback) {
    // Current L2Server.exe 0x007cd314.
    L2_GUARD_FUNCTION();
    if (callback) {
#if defined(_WIN32)
        SYSTEM_INFO systemInformation{};
        ::GetSystemInfo(&systemInformation);
#else
        static_cast<void>(std::thread::hardware_concurrency());
#endif
        IOThreadRuntime::StartBoundThread(callback);
    }
    // The target returns only pointer non-nullness and does not check the
    // native thread-creation result.
    return callback != nullptr;
}

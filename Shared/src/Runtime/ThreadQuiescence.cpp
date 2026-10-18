#include "Shared/src/Runtime/ThreadQuiescence.h"

#include "Shared/src/Runtime/NativeThreadHandle.h"

#if defined(__APPLE__)
#include <mach/mach.h>
#include <pthread.h>
#elif defined(__unix__)
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#endif

namespace runtime {

#if defined(__unix__) && !defined(__APPLE__)
namespace {

constexpr int kSuspendSignal = SIGUSR1;
constexpr int kResumeSignal = SIGUSR2;

int g_suspendAcknowledgedRead = -1;
int g_suspendAcknowledgedWrite = -1;
bool g_threadQuiescenceAvailable = false;

// These values are touched only by signal handlers running on their owning
// thread. A resume delivered outside a suspended handler is deliberately
// ignored, matching ResumeThread's failure on a non-suspended Win32 thread.
thread_local volatile sig_atomic_t g_suspendDepth = 0;
thread_local volatile sig_atomic_t g_resumeTickets = 0;

void ResumeSignalHandler(int) noexcept {
    if (g_resumeTickets < g_suspendDepth) {
        ++g_resumeTickets;
    }
}

void SuspendSignalHandler(int) noexcept {
    const int savedError = errno;
    ++g_suspendDepth;

    const std::uint8_t acknowledged = 1;
    ssize_t written = 0;
    do {
        written = ::write(g_suspendAcknowledgedWrite, &acknowledged, sizeof(acknowledged));
    } while (written < 0 && errno == EINTR);

    if (written == static_cast<ssize_t>(sizeof(acknowledged))) {
        sigset_t waitMask;
        ::sigfillset(&waitMask);
        ::sigdelset(&waitMask, kSuspendSignal);
        ::sigdelset(&waitMask, kResumeSignal);
        while (g_resumeTickets == 0) {
            ::sigsuspend(&waitMask);
        }
        --g_resumeTickets;
    }

    --g_suspendDepth;
    errno = savedError;
}

bool InstallSignalTransport() noexcept {
    int acknowledgedPipe[2]{-1, -1};
    if (::pipe(acknowledgedPipe) != 0) {
        return false;
    }

    for (const int descriptor : acknowledgedPipe) {
        const int descriptorFlags = ::fcntl(descriptor, F_GETFD);
        if (descriptorFlags < 0 || ::fcntl(descriptor, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0) {
            ::close(acknowledgedPipe[0]);
            ::close(acknowledgedPipe[1]);
            return false;
        }
    }

    struct sigaction resumeAction{};
    resumeAction.sa_handler = &ResumeSignalHandler;
    ::sigemptyset(&resumeAction.sa_mask);
    resumeAction.sa_flags = SA_RESTART;
    if (::sigaction(kResumeSignal, &resumeAction, nullptr) != 0) {
        ::close(acknowledgedPipe[0]);
        ::close(acknowledgedPipe[1]);
        return false;
    }

    struct sigaction suspendAction{};
    suspendAction.sa_handler = &SuspendSignalHandler;
    ::sigemptyset(&suspendAction.sa_mask);
    suspendAction.sa_flags = SA_RESTART;
    if (::sigaction(kSuspendSignal, &suspendAction, nullptr) != 0) {
        ::close(acknowledgedPipe[0]);
        ::close(acknowledgedPipe[1]);
        return false;
    }

    g_suspendAcknowledgedRead = acknowledgedPipe[0];
    g_suspendAcknowledgedWrite = acknowledgedPipe[1];
    return true;
}

} // namespace
#endif

void InstallThreadQuiescenceTransport() noexcept {
#if defined(__unix__) && !defined(__APPLE__)
    if (!g_threadQuiescenceAvailable) {
        g_threadQuiescenceAvailable = InstallSignalTransport();
    }
#endif
}

bool SuspendNativeThread(void* handle) noexcept {
    if (!handle) {
        return false;
    }
#if defined(__APPLE__)
    const pthread_t thread = NativeThreadHandleFromOpaque(handle);
    const mach_port_t machThread = ::pthread_mach_thread_np(thread);
    return machThread != MACH_PORT_NULL && ::thread_suspend(machThread) == KERN_SUCCESS;
#elif defined(__unix__)
    if (!g_threadQuiescenceAvailable) {
        return false;
    }
    const pthread_t thread = NativeThreadHandleFromOpaque(handle);
    if (::pthread_kill(thread, kSuspendSignal) != 0) {
        return false;
    }

    std::uint8_t acknowledged = 0;
    ssize_t received = 0;
    do {
        received = ::read(g_suspendAcknowledgedRead, &acknowledged, sizeof(acknowledged));
    } while (received < 0 && errno == EINTR);
    return received == static_cast<ssize_t>(sizeof(acknowledged));
#else
    return false;
#endif
}

bool ResumeNativeThread(void* handle) noexcept {
    if (!handle) {
        return false;
    }
#if defined(__APPLE__)
    const pthread_t thread = NativeThreadHandleFromOpaque(handle);
    const mach_port_t machThread = ::pthread_mach_thread_np(thread);
    return machThread != MACH_PORT_NULL && ::thread_resume(machThread) == KERN_SUCCESS;
#elif defined(__unix__)
    if (!g_threadQuiescenceAvailable) {
        return false;
    }
    const pthread_t thread = NativeThreadHandleFromOpaque(handle);
    return ::pthread_kill(thread, kResumeSignal) == 0;
#else
    return false;
#endif
}

} // namespace runtime

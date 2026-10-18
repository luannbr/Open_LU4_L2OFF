#include "Shared/src/IOCs.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/ProcessRuntime.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr std::uint8_t kDefaultLock = 0;

#if !defined(_WIN32)
struct PortableCriticalSectionRegistry {
    std::mutex lock;
    std::unordered_map<CIOCriticalSection*, std::recursive_mutex*> backends;
};

PortableCriticalSectionRegistry& GetPortableCriticalSectionRegistry() {
    // Process lifetime is deliberate. This registry is also the bootstrap
    // transport for CIOCriticalSection globals reached before their owning
    // translation unit's dynamic constructor, so it cannot participate in
    // cross-translation-unit shutdown ordering.
    static PortableCriticalSectionRegistry* const registry = new PortableCriticalSectionRegistry;
    return *registry;
}

std::recursive_mutex& EnsurePortableCriticalSection(CIOCriticalSection* criticalSection) {
    // A target CRITICAL_SECTION is valid before process-specific factory
    // registrations execute because the MSVC CRT initializes its Shared
    // owner first. Static archives do not keep that cross-translation-unit
    // order on every ELF/Mach-O linker. Keep the same global owner/layout and
    // lazily create only its native mutex backend when an early registry
    // initializer reaches Enter.
    PortableCriticalSectionRegistry& registry = GetPortableCriticalSectionRegistry();
    std::lock_guard<std::mutex> guard(registry.lock);
    const auto found = registry.backends.find(criticalSection);
    if (found != registry.backends.end()) {
        return *found->second;
    }

    auto* const backend = new std::recursive_mutex;
    registry.backends.emplace(criticalSection, backend);
    // Preserve the target CRITICAL_SECTION image for layout/debugging, but
    // never use an automatic object's pre-constructor bytes as ownership
    // evidence. Those bytes are indeterminate on C++ hosts, unlike zeroed
    // process globals, and were the cause of the ID-452 stack-socket crash.
    auto* const storage = reinterpret_cast<l2portable_detail::CriticalSectionStorage*>(criticalSection);
    storage->DebugInfo = backend;
    return *backend;
}

std::recursive_mutex* RemovePortableCriticalSection(CIOCriticalSection* criticalSection) {
    PortableCriticalSectionRegistry& registry = GetPortableCriticalSectionRegistry();
    std::lock_guard<std::mutex> guard(registry.lock);
    const auto found = registry.backends.find(criticalSection);
    if (found == registry.backends.end()) {
        return nullptr;
    }
    std::recursive_mutex* const backend = found->second;
    registry.backends.erase(found);
    auto* const storage = reinterpret_cast<l2portable_detail::CriticalSectionStorage*>(criticalSection);
    storage->DebugInfo = nullptr;
    return backend;
}

std::recursive_mutex& PortableCriticalSection(CIOCriticalSection* criticalSection) {
    return EnsurePortableCriticalSection(criticalSection);
}
#endif

} // namespace

// Current L2NPC.exe 0x00457594. Shared/iocs.cpp lines 9-15.
CIOCriticalSection::CIOCriticalSection() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    static_assert(sizeof(CRITICAL_SECTION) == sizeof(m_CriticalSection));
    ::InitializeCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(&m_CriticalSection));
#else
    EnsurePortableCriticalSection(this);
    m_CriticalSection.LockCount = -1;
    m_CriticalSection.RecursionCount = 0;
    m_CriticalSection.OwningThread = nullptr;
    m_CriticalSection.LockSemaphore = nullptr;
    m_CriticalSection.SpinCount = 0;
#endif
}

CIOCriticalSection::CIOCriticalSection(unsigned long spinCount) {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    static_assert(sizeof(CRITICAL_SECTION) == sizeof(m_CriticalSection));
    ::InitializeCriticalSectionAndSpinCount(reinterpret_cast<LPCRITICAL_SECTION>(&m_CriticalSection), spinCount);
#else
    EnsurePortableCriticalSection(this);
    m_CriticalSection.LockCount = -1;
    m_CriticalSection.RecursionCount = 0;
    m_CriticalSection.OwningThread = nullptr;
    m_CriticalSection.LockSemaphore = nullptr;
    m_CriticalSection.SpinCount = static_cast<std::uint32_t>(spinCount);
#endif
}

// Current L2NPC.exe 0x00457628. Shared/iocs.cpp lines 27-33.
CIOCriticalSection::~CIOCriticalSection() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    ::DeleteCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(&m_CriticalSection));
#else
    delete RemovePortableCriticalSection(this);
#endif
}

// Current L2NPC.exe 0x00457818. Current Cached.exe 0x004e80b0.
// Shared/iocs.cpp lines 36-40.
void CIOCriticalSection::Enter(const wchar_t* pszFile, int nLine) {
    const std::uint64_t address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
    runtime::process::RecordLockWaiting(pszFile, nLine, address, kDefaultLock);
#if defined(_WIN32)
    ::EnterCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(&m_CriticalSection));
#else
    PortableCriticalSection(this).lock();
#endif
    // The shipping body reads the logical thread id again after the
    // potentially blocking EnterCriticalSection call.
    runtime::process::RecordLockEntered(pszFile, nLine, address, kDefaultLock);
}

// Current L2NPC.exe 0x004578dc. Current Cached.exe 0x004e8174.
// Shared/iocs.cpp lines 43-46.
void CIOCriticalSection::Leave(const wchar_t* pszFile, int nLine) {
    const std::uint64_t address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
    runtime::process::RecordLockReleased(pszFile, static_cast<std::uint32_t>(nLine), address, kDefaultLock);
#if defined(_WIN32)
    ::LeaveCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(&m_CriticalSection));
#else
    PortableCriticalSection(this).unlock();
#endif
}

// Current L2NPC.exe 0x00457944. TotalChecker2015's exact matching source
// checksum confirms that ReadLock has the same forwarding body.
void CIOCriticalSection::WriteLock(const wchar_t* pszFile, int nLine) {
    L2_GUARD_FUNCTION();
    Enter(pszFile, nLine);
}

void CIOCriticalSection::ReadLock(const wchar_t* pszFile, int nLine) {
    L2_GUARD_FUNCTION();
    Enter(pszFile, nLine);
}

// Current L2NPC.exe 0x004579fc. Original iocs.cpp line 61.
void CIOCriticalSection::WriteUnlock() {
    L2_GUARD_FUNCTION();
    Leave(L"CS WriteUnlock Released", 0x3d);
}

// Exact matching iocs.cpp lines 76-79. Line 79 is 0x4f.
void CIOCriticalSection::ReadUnlock() {
    L2_GUARD_FUNCTION();
    Leave(L"CS ReadUnlock Released", 0x4f);
}

// Recovered centered shared reader/writer lock API.
#pragma once

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/ThreadManager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>


class CSharedRWLock;

namespace portable {

struct SharedRWLockTransportAccess;

} // namespace portable

inline constexpr const wchar_t* kInventoryLockLogicalName = L"InventoryLock";
inline constexpr const char* kInventoryLockSharedMemoryName = "L2CenteredSharedLock_MMF_InventoryLock";
inline constexpr std::uint32_t kInventoryLockSpinCount = 4000;
inline constexpr std::size_t kInventoryLockCount = 32768;

class CenteredSharedRWLockBackend {
public:
    using ReadLockTransition = void (*)(void* context);

    CenteredSharedRWLockBackend(const wchar_t* logicalName, std::size_t lockCount, std::uint32_t spinCount);
    CenteredSharedRWLockBackend(
        const wchar_t* logicalName,
        std::size_t lockCount,
        std::uint32_t spinCount,
        const std::string& mappingName,
        bool createMapping
    );
    ~CenteredSharedRWLockBackend();

    CenteredSharedRWLockBackend(const CenteredSharedRWLockBackend&) = delete;
    CenteredSharedRWLockBackend& operator=(const CenteredSharedRWLockBackend&) = delete;

    bool AttachSharedMemory(const std::string& mappingName, std::string* error = nullptr);
    bool HasSharedMemory() const;

    void ReadLock(std::size_t slot, bool concession, ReadLockTransition entered, ReadLockTransition rearm, void* context);
    void WriteLock(std::size_t slot);
    void Done(std::size_t slot, bool writeLock);
    void* SharedInfo(std::size_t slot);
    void PrepareSlot(std::size_t slot);
    void ReleaseSlot(std::size_t slot);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class CSharedRWLock {
public:
    struct SHAREDINFO {
        std::uint32_t m_dwSpinCount = 0;
        std::int32_t m_lLockCount = 0;
        std::uint32_t m_dwThreadID = 0;
        std::int32_t m_lRecurseCount = 0;
        std::uint32_t m_dwWritingThreadID = 0;
        std::int32_t m_nWaitingReaders = 0;
        std::int32_t m_nWaitingReadOnly = 0;
        std::int32_t m_nWaitingWriters = 0;
        std::int32_t m_nActive = 0;
        std::int32_t m_nReadOnly = 0;
    };
    using PSHAREDINFO = SHAREDINFO*;

    explicit CSharedRWLock(const wchar_t* name);
    CSharedRWLock(const wchar_t* name, std::uint32_t number, PSHAREDINFO reservedInfo, std::uint32_t spinCount);
    ~CSharedRWLock();

    CSharedRWLock(const CSharedRWLock&) = delete;
    CSharedRWLock& operator=(const CSharedRWLock&) = delete;

    void WriteLock(const wchar_t* callerFile, int callerLine);
    void ReadLock(const wchar_t* callerFile, int callerLine, bool concession);
    void WriteUnlock(const wchar_t* callerFile, int callerLine);
    void ReadUnlock(const wchar_t* callerFile, int callerLine);
    void SetSpinCount(std::uint32_t spinCount);

private:
    template <std::size_t>
    friend class CCenteredSharedRWLock;
    friend struct portable::SharedRWLockTransportAccess;

    int TryEnter();
    void Enter();
    void Leave();
    void Done(const wchar_t* callerFile, int callerLine, bool writeLock);

    // Exact current PDB layout (Shared/SharedRWLock.cpp). On portable hosts
    // the opaque handle fields carry the backend binding. No extra ABI field
    // is appended to the target object.
    void* m_hfm;
    void* m_hevt;
    void* m_hsemReaders;
    void* m_hsemWriters;
    PSHAREDINFO m_psi;
    bool private_psi;
    std::array<std::byte, 7> m_targetPadding;
};


namespace portable {

// The cross-platform transport occupies the target's opaque Win32 handle
// fields without adding methods or storage to CSharedRWLock.
struct SharedRWLockTransportAccess {
    static void Bind(CSharedRWLock& lock, CenteredSharedRWLockBackend* backend, std::size_t slot);
    static CenteredSharedRWLockBackend* Backend(const CSharedRWLock& lock);
    static std::size_t Slot(const CSharedRWLock& lock);
};

} // namespace portable

template <std::size_t LockCount>
class CCenteredSharedRWLock {
public:
    static_assert(LockCount != 0 && (LockCount & (LockCount - 1)) == 0, "centered shared-lock count must be a power of two");

    explicit CCenteredSharedRWLock(const wchar_t* logicalName, std::uint32_t spinCount) {
        L2_GUARD_FUNCTION();
        auto* const backend = new CenteredSharedRWLockBackend(logicalName, LockCount, spinCount);
        m_hfm = backend;
        // L2NPC.exe 0x004544e0 stores the mapped view here. POSIX
        // mappings interleave process-shared synchronization objects
        // with SHAREDINFO, so only slot zero is representable in this
        // target-layout field. Every lock receives its actual slot
        // from the transport below.
        m_psi = static_cast<CSharedRWLock::PSHAREDINFO>(backend->SharedInfo(0));
        for (std::size_t index = 0; index < LockCount; ++index) {
            CSharedRWLock* const lock = new (std::nothrow) CSharedRWLock(
                logicalName, static_cast<std::uint32_t>(index),
                static_cast<CSharedRWLock::PSHAREDINFO>(backend->SharedInfo(index)), spinCount
            );
            if (lock) {
                portable::SharedRWLockTransportAccess::Bind(*lock, backend, index);
                // The Windows target creates the event and both
                // semaphores inside each CSharedRWLock constructor,
                // before publishing the pointer and advancing to the
                // next of the 32768 slots. POSIX keeps the same eager
                // slot boundary even though its synchronization objects
                // live in the mapping.
                backend->PrepareSlot(index);
            }
            locks[index] = lock;
        }
    }

    ~CCenteredSharedRWLock() {
        L2_GUARD_FUNCTION();
        for (CSharedRWLock* lock : locks) {
            delete lock;
        }
        delete static_cast<CenteredSharedRWLockBackend*>(m_hfm);
    }

    CCenteredSharedRWLock(const CCenteredSharedRWLock&) = delete;
    CCenteredSharedRWLock& operator=(const CCenteredSharedRWLock&) = delete;

    CSharedRWLock* GetLock(int id) {
        L2_GUARD_FUNCTION();
        const auto unsignedId = static_cast<std::uint32_t>(id);
        return locks[static_cast<std::size_t>(unsignedId) & (LockCount - 1)];
    }

private:
    std::array<CSharedRWLock*, LockCount> locks;
    void* m_hfm;
    CSharedRWLock::PSHAREDINFO m_psi;
};

using CInventoryLockManager = CCenteredSharedRWLock<kInventoryLockCount>;

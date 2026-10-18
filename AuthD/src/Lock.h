#pragma once

#include "Platform/CriticalSection.h"

#include <cstdint>

// L2Authd.pdb type 0x117D.
enum LockType : std::int32_t {
    eCriticalSection = 0,
    eSystemSpinLock = 1,
    eCustomSpinLock = 2,
};

// Original owner: Src/lock.h. L2Authd.pdb type 0x6ED6, sizeof 0x20.
class CLock {
public:
    // The compiled __dflt_ctor_closure and all of its EXE call sites establish
    // the two default values below.
    CLock(
        LockType type = eCriticalSection,
        std::uint32_t spinCount = 0);
    ~CLock();

    void Enter();
    void Leave();
    bool TryEnter();

protected:
    void Wait();

    authd::platform::CriticalSectionStorage32 m_critical;
    std::int32_t m_customLock;
    LockType m_type;
};

// Original owner: Src/lock.h. L2Authd.pdb type 0x6E41, sizeof 0x20.
class CRWLock {
public:
    CRWLock();
    ~CRWLock();

    void ReadLock();
    void ReadUnlock();
    void WriteLock();
    bool WriteTryLock();
    void WriteUnlock();

private:
    authd::platform::CriticalSectionStorage32 mutex;

    // The target field is a 32-bit Win32 semaphore HANDLE at +0x18. The
    // portable value is the same binary semaphore count: one means available,
    // zero means owned. This keeps both the field width and its behavior.
    std::uint32_t dataLock;
    std::int32_t readerCount;
};

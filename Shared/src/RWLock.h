#pragma once

#include "CriticalSectionStorage.h"

#include <cstddef>
#include <cstdint>


// Current CacheD and L2NPC instantiate this same 0x38-byte Shared/RWLock in
// CObjectMap. The OS wait primitive and diagnostics remain process adapters.
class RWLock {
public:
    RWLock();
    ~RWLock();

    RWLock(const RWLock&) = delete;
    RWLock& operator=(const RWLock&) = delete;

    void ReadLock(const wchar_t* callerFile, int callerLine);
    void ReadUnlock();
    void WriteLock(const wchar_t* callerFile, int callerLine);
    void WriteUnlock();

private:
    l2portable_detail::CriticalSectionStorage mutex;
    void* dataLock;
    std::int32_t readerCount;
    std::uint32_t targetPadding;
};

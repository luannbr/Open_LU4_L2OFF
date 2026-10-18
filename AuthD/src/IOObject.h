#pragma once

#include "Platform/WindowsTypes.h"
#include "Memory.h"

#include "Shared/src/Runtime/RawAtomic.h"

#include <cstdint>

// Original owner: Src/ioobject.h. In the selected Win32 ABI this class is
// 0x08 bytes: vptr at +0 and refcount at +4. MemoryObject is the one-byte
// empty base placed at +4 by MSVC and contributes no additional storage.
class CIOObject : public MemoryObject {
public:
    CIOObject();
    virtual ~CIOObject();

    int AddRef();
    void ReleaseRef();
    int GetRef() const noexcept {
        return runtime::raw_atomic::LoadI32Relaxed(
            &refcount);
    }

    virtual void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped);
    virtual void OnTimerCallback();
    virtual int RegisterTimer(
        unsigned int waitTime,
        int setEvent);
    virtual void OnEventCallback();
    virtual bool RegisterEvent(void* hEvent);

    int PostObject(
        int dwDataSize,
        void* completionPort);

protected:
    std::int32_t refcount;
};

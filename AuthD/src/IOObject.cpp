#include "IOObject.h"

#include "Job.h"

#include "Shared/src/Runtime/CompletionPort.h"

#include <cstdint>

// L2Authd.exe 0x00447760, Src/ioobject.cpp:15-17.
CIOObject::CIOObject()
    : refcount(0) {}

// L2Authd.exe 0x00447790, Src/ioobject.cpp:20-22.
CIOObject::~CIOObject() = default;

// L2Authd.exe 0x004477B0, Src/ioobject.cpp:25-28. The target returns a
// separate aligned read after InterlockedIncrement, instead of the return
// value of InterlockedIncrement itself.
int CIOObject::AddRef() {
    runtime::raw_atomic::IncrementI32(
        &refcount);
    return runtime::raw_atomic::LoadI32Relaxed(
        &refcount);
}

// L2Authd.exe 0x004477D0, Src/ioobject.cpp:31-34.
void CIOObject::ReleaseRef() {
    if (runtime::raw_atomic::DecrementI32(
            &refcount) == 0) {
        delete this;
    }
}

// L2Authd.exe 0x00447820, Src/ioobject.cpp:61-63.
void CIOObject::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    [[maybe_unused]] std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {}

// L2Authd.exe 0x00447830, Src/ioobject.cpp:66-69.
void CIOObject::OnTimerCallback() {
    ReleaseRef();
}

// L2Authd.exe 0x00447850, Src/ioobject.cpp:42-50.
int CIOObject::RegisterTimer(
    unsigned int waitTime,
    int setEvent) {
    const int registered =
        job.PushTimer(this, waitTime, setEvent);
    if (registered != 0) {
        AddRef();
    }
    return registered != 0;
}

// L2Authd.exe 0x00447890, Src/ioobject.cpp:72-73.
void CIOObject::OnEventCallback() {}

// L2Authd.exe 0x004478A0, Src/ioobject.cpp:53-58.
bool CIOObject::RegisterEvent(void* hEvent) {
    return job.PushEvent(hEvent, this) != 0;
}

// L2Authd.exe 0x004478D0, Src/ioobject.cpp:37-39. Only the completion-port
// transport changes on non-Windows hosts. Transferred bytes, completion key
// and null OVERLAPPED payload keep the target call contract.
int CIOObject::PostObject(
    int dwDataSize,
    void* completionPort) {
    return runtime::completion_port::Post(
               completionPort,
               static_cast<std::uint32_t>(dwDataSize),
               reinterpret_cast<std::uintptr_t>(this),
               nullptr)
        ? 1
        : 0;
}

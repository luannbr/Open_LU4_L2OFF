#include "Thread.h"

#include "Shared/src/Runtime/OwnedThread.h"

#include <cstring>

// AuthGateD.exe 0x00412D70, Thread.cpp.
IoThread* OsThreadCreate(IoThreadProc threadProc, void* parameter, const char* name) {
    auto* descriptor = new IoThread;
    descriptor->fThreadProc = threadProc;
    std::strncpy(descriptor->name, name, sizeof(descriptor->name));

    void* const nativeThread = runtime::CreateOwnedThread(threadProc, parameter, false);
    if (!nativeThread) {
        delete descriptor;
        return nullptr;
    }

    descriptor->hThread = nativeThread;
    descriptor->threadId = runtime::OwnedThreadId(nativeThread);
    return descriptor;
}

namespace authgated::platform {

void JoinIoThread(IoThread* descriptor) noexcept {
    if (!descriptor || !descriptor->hThread) {
        return;
    }
    runtime::JoinOwnedThread(descriptor->hThread);
}

void DestroyIoThread(IoThread* descriptor) noexcept {
    if (!descriptor) {
        return;
    }
    runtime::DestroyOwnedThread(descriptor->hThread);
    descriptor->hThread = nullptr;
    delete descriptor;
}

} // namespace authgated::platform

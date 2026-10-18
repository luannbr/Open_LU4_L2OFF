#pragma once

#include <cstdint>

using IoThreadProc = std::uint32_t (*)(void* parameter);

struct IoThread {
    void* hThread = nullptr;
    std::uint32_t threadId = 0;
    IoThreadProc fThreadProc = nullptr;
    char name[16]{};
};

IoThread* OsThreadCreate(
    IoThreadProc threadProc,
    void* parameter,
    const char* name);

namespace authgated::platform {

void JoinIoThread(IoThread* thread) noexcept;
void DestroyIoThread(IoThread* thread) noexcept;

} // namespace authgated::platform

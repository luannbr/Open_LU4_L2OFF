#pragma once

#include <cstdint>

class CWindowMgr;

namespace authgated::platform {

void RegisterWindowRuntime(CWindowMgr* owner);
void UnregisterWindowRuntime(CWindowMgr* owner) noexcept;
void SetWindowTimer(
    CWindowMgr* owner,
    std::uint32_t id,
    std::uint32_t milliseconds);
void KillWindowTimer(
    CWindowMgr* owner,
    std::uint32_t id) noexcept;
void RunWindowMessageLoop(CWindowMgr* owner);
void RequestWindowQuit(CWindowMgr* owner) noexcept;

} // namespace authgated::platform

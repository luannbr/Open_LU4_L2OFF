#pragma once

namespace runtime {

// Installs only the native transport needed by the recovered
// L2Exception::SuspendThreadExceptMe/ResumeThreadExceptMe owner. Windows uses
// its original SuspendThread/ResumeThread calls directly in Exception.cpp.
void InstallThreadQuiescenceTransport() noexcept;

bool SuspendNativeThread(void* handle) noexcept;
bool ResumeNativeThread(void* handle) noexcept;

} // namespace runtime

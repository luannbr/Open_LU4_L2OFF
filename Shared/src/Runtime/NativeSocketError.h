#pragma once

namespace portable {

// WinSock-compatible error boundary shared by every portable socket owner.
// POSIX errno values are translated to the numeric contract observed by the
// recovered Windows products.
int GetNativeSocketLastError() noexcept;
void SetNativeSocketLastError(int error) noexcept;

} // namespace portable

#pragma once

#include <cstdint>

namespace portable {

using NativeListenerReadySink = void (*)(void* owner);

// Associates a nonblocking listening socket with a recovered owner without
// adding host fields to that owner's target layout. One readiness signal is
// outstanding at a time, matching a resettable WSA FD_ACCEPT event.
bool BindNativeListenerTransport(void* owner, std::uint64_t nativeSocket, NativeListenerReadySink readySink);
void AcknowledgeNativeListenerTransport(const void* owner);
void UnbindNativeListenerTransport(const void* owner);
void StopNativeListenerTransports();

} // namespace portable

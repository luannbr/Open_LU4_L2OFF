#pragma once

#include "Shared/src/IOC.h"

#include <cstdint>

namespace portable {

// Cross-platform native half of the WSARecv/IOCP association created by
// CIOServer::OnIOCallback. The target CIOServerSocket remains byte-for-byte
// unchanged. This owner is keyed to it through SocketTransport just as an
// IOCP completion key was keyed to the object in the Windows process.
bool BindAcceptedSocketTransport(CIOServerSocket* socket, std::uint64_t nativeSocket);

// CacheD's shipping WaitThread is process-lifetime, so accepted sockets are
// normally reclaimed only by their peer or process exit. The portable host
// has a cooperative stop path. It closes those native operations before the
// emulated completion port is drained, without adding a method or field to a
// recovered target owner.
void StopAcceptedSocketTransports();

} // namespace portable

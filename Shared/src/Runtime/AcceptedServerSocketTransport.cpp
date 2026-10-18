#include "Shared/src/Runtime/AcceptedSocketTransport.h"

#include "Shared/src/Runtime/NativeSocketTransport.h"

namespace portable {

// Server/Main/Shared wrapper around the product-neutral native transport.
// AuthD and AuthGateD bind their different CIOSocket classes directly through
// BindNativeSocketTransport and therefore do not compile this owner.
bool BindAcceptedSocketTransport(CIOServerSocket* socket, std::uint64_t nativeSocket) {
    return BindNativeSocketTransport(
        socket, nativeSocket,
        [](void* owner, const std::uint8_t* data, std::size_t length) {
            return FeedSocketRead(static_cast<CIOServerSocket*>(owner), data, length);
        },
        [](void* owner, int error) { FeedSocketTerminal(static_cast<CIOServerSocket*>(owner), error); }
    );
}

void StopAcceptedSocketTransports() { StopNativeSocketTransports(); }

} // namespace portable

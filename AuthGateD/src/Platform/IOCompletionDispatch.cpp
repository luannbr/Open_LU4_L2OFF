#include "Platform/IOCompletionDispatch.h"

#include "AuthPacket.h"
#include "ClientPacket.h"
#include "IOObject.h"
#include "IOServer.h"
#include "IOSocket.h"

namespace authgated::platform {

void DispatchIOCompletion(
    CIOObject* object,
    int success,
    std::uint32_t transferred,
    _OVERLAPPED* overlapped) {
    if (auto* socket = dynamic_cast<CIOSocket*>(object)) {
        socket->OnIOCallback(success, transferred, overlapped);
        return;
    }
    if (auto* server = dynamic_cast<CIOServer*>(object)) {
        server->OnIOCallback(success, transferred, overlapped);
    }
}

void DispatchWorkCompletion(
    CIOObject* object,
    int success,
    std::uint32_t transferred,
    _OVERLAPPED* overlapped) {
    if (auto* clientPacket =
            dynamic_cast<CClientPacket*>(object)) {
        clientPacket->OnIOCallback(
            success,
            transferred,
            overlapped);
        return;
    }
    if (auto* authPacket =
            dynamic_cast<CAuthPacket*>(object)) {
        authPacket->OnIOCallback(
            success,
            transferred,
            overlapped);
    }
}

} // namespace authgated::platform

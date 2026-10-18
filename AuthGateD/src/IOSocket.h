#pragma once

#include "IOBuffer.h"
#include "IOObject.h"
#include "Lock.h"
#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstddef>
#include <cstdint>

class CIOSocket;
class CIOServer;

namespace authgated::portable {

bool FeedSocketRead(
    CIOSocket* socket,
    const std::uint8_t* data,
    std::size_t length);
void FeedSocketTerminal(
    CIOSocket* socket,
    int error);

struct IOSocketTransportAccess;

} // namespace authgated::portable

// Original owner: IOSocket.h / IOSocket.cpp. AuthGateD.pdb type 0x9c in
// the selected x86 ABI. The virtual declaration order is the target vtable:
// deleting destructor, OnIOCallback, OnClose, OnCreate, OnRead,
// OnReadCallback and OnWriteCallback.
class CIOSocket : public CIOObject {
public:
    CIOSocket();
    ~CIOSocket() override;

    CIOSocket(const CIOSocket&) = delete;
    CIOSocket& operator=(const CIOSocket&) = delete;

    void Reset();
    bool Connect(in_addr address, int port);
    void CloseSocket();
    bool Initialize(void* completionPort);

    virtual void OnIOCallback(
        int success,
        std::uint32_t transferred,
        _OVERLAPPED* overlapped);
    virtual void OnClose(std::uint32_t socket);
    virtual void OnCreate();
    virtual void OnRead();
    virtual void OnReadCallback(std::uint32_t transferred);
    virtual void OnWriteCallback(std::uint32_t transferred);

    void Read(std::uint32_t bytesLeft);
    void Write(CIOBuffer* buffer);

protected:
    CLock m_lock;
    _OVERLAPPED m_overlappedRead{};
    _OVERLAPPED m_overlappedWrite{};
    CIOBuffer* m_pReadBuf = nullptr;
    CIOBuffer* m_pFirstBuf = nullptr;
    CIOBuffer* m_pLastBuf = nullptr;
    std::int32_t m_nPendingWrite = 0;
    in_addr m_addr;
    std::int32_t m_port;
    std::uint32_t m_hSocket = UINT32_C(0xffffffff);
    std::uint32_t m_iLastIO = 0;
    std::uint32_t m_iFirstIO = 0;

private:
    friend class CClientSocketContainer;
    friend class CIOServer;
    friend struct authgated::portable::IOSocketTransportAccess;
};

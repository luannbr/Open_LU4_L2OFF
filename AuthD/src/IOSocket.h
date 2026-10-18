#pragma once

#include "Platform/CriticalSection.h"
#include "Platform/WindowsTypes.h"
#include "IOBuffer.h"
#include "IOObject.h"
#include "Lock.h"

#include <cstddef>
#include <cstdint>

class CIOSocket;

namespace authd::portable {

// Host transport entry points kept outside the recovered CIOSocket surface.
// The future IOServer backend uses these after a native receive completes.
// packet ownership and virtual dispatch remain in CIOSocket itself.
bool FeedSocketRead(
    CIOSocket* socket,
    const std::uint8_t* data,
    std::size_t length);
void FeedSocketTerminal(
    CIOSocket* socket,
    int error);

struct IOSocketTransportAccess;

} // namespace authd::portable

// Original owner: Src/iosocket.h / Src/iosocket.cpp. L2Authd.pdb type
// 0x62BB is 0x5c bytes in the selected Win32 ABI. Access, declaration order
// and the five introduced virtual slots follow field list 0x62BA exactly.
class CIOSocket : public CIOObject {
public:
    explicit CIOSocket(std::uint32_t socket);
    ~CIOSocket() override;

    virtual void OnClose() = 0;
    virtual void OnCreate();
    virtual void OnRead() = 0;

    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;
    virtual void OnReadCallback(
        std::uint32_t dwTransferred);
    virtual void OnWriteCallback(
        std::uint32_t dwTransferred);

    void CloseSocket();
    void Initialize(void* completionPort);
    void Read(std::uint32_t dwLeft);
    std::int32_t PendingWrite() const noexcept {
        return m_nPendingWrite;
    }
    void Write(char* data, std::uint32_t size);
    void Write(CIOBuffer* buffer);

protected:
    authd::platform::CriticalSectionStorage32 m_cs;
    _OVERLAPPED m_overlappedRead;
    _OVERLAPPED m_overlappedWrite;
    CIOBuffer* m_pReadBuf;
    CIOBuffer* m_pFirstBuf;
    CIOBuffer* m_pLastBuf;
    std::int32_t m_nPendingWrite;

public:
    std::uint32_t m_hSocket;

private:
    friend struct authd::portable::IOSocketTransportAccess;
};

// L2Authd.pdb type 0x89D7, original owner Src/iosocket.h.
class CPacketServer : public CIOObject {
public:
    using ServerPacketFunc = bool (*)(
        CIOSocket*,
        const unsigned char*);

    static CPacketServer* Alloc();
    static void FreeAll();

    void Free();
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    CPacketServer();
    ~CPacketServer() override;

    CIOSocket* m_pSocket;
    CIOBuffer* m_pBuf;
    ServerPacketFunc m_pFunc;

    static std::int32_t g_nPendingPacket;
};

// L2Authd.pdb type 0x89CD. This one-byte object exists only to run FreeAll at
// process shutdown. Its sixteen slots and cursors are public static members.
class CPacketServerPool {
public:
    class CSlot {
    public:
        CSlot();
        ~CSlot() = default;

        CPacketServer* m_pPacket;
        CLock m_lock;
    };

    ~CPacketServerPool();

    static CSlot g_slot[16];
    static std::int32_t g_nAlloc;
    static std::int32_t g_nFree;
};

extern CPacketServerPool thePacketPool;

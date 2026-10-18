#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/TemplateHelper.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CDBSocket;
class User;

namespace DBSocketUtil {

bool ParseItem(const unsigned char*& packet, User& user, bool sendMessage, bool adenaMessage);

} // namespace DBSocketUtil

using CDBCallback = bool (*)(CDBSocket*, const std::uint8_t*);

bool DummyPacket(CDBSocket* socket, const std::uint8_t* packet);

using CDBCallbackMap = ::FixedSizeCallbackMap<199, CDBCallback>;

// The current image points the primary sockets at this object's callback
// array. Its 199 concrete registrations remain owned by the individual
// current DB packet handlers. No fallback table is manufactured here.
extern CDBCallbackMap gPacketTableFromCache;
extern CDBCallback* dbPacketTable;


class CDBSocket : public ::CIOSocket {
public:
    explicit CDBSocket(std::uint64_t socket, CDBCallback* packetTable);
    ~CDBSocket() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool CheckPendingWrite() override;
    void Send(::RQ_PROTOCOL protocol, ::RQ_EX_PROTOCOL extendedProtocol, network::IPacket& packet) override;
    void Send(const char* format, ...) override;
    void SendV(const char* format, va_list arguments) override;
    void OnClose() override;
    void OnRead() override;

protected:
    std::uint64_t m_socket;
    std::int32_t m_threadCount;
    CDBCallback* m_packetTable;
    ::SocketMode m_mode;
    std::int32_t m_packetLength;

private:
    L2_DECLARE_OBJECT_REGISTRATION(CDBSocket)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CDBSocket)
};

using CDBPacketBase = ::CPacketEx<::DefaultIOBuffer, CDBSocket, CDBCallback>;

class CDBPacket final : public CDBPacketBase {
public:
    CDBPacket() = default;
    ~CDBPacket() override = default;

    void OnIOCallback(int success, std::uint32_t transferred, ::PortableOverlapped* overlapped) override;

private:
    std::int32_t m_nDatabaseID = 0;

    friend class CDBSocket;
};

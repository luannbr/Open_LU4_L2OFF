#pragma once

#include "NPProtocol.h"

#include "Shared/src/IOC.h"

#include <cstddef>
#include <cstdint>

using CNPPacketSocket = CIOSocketEx<IOBuffer32k>;
using CNPPacketTransportFunc = bool (*)(CNPPacketSocket*, const std::uint8_t*);
using CNPPacketBase = CPacketEx<IOBuffer32k, CNPPacketSocket, CNPPacketTransportFunc>;


namespace NPSystem {
class CNPSocket;
}

class CNPPacket : public CNPPacketBase {
public:
    using CPacket = CNPPacketBase;
    using NPPacketFunc = bool (*)(NPSystem::CNPSocket*, NPSystem::STSRequestTI*, NPSystem::CSTSResponse*);

    CNPPacket();
    ~CNPPacket() override = default;

    NPSystem::STSRequestTI m_Request;
    NPSystem::CSTSResponse* m_pResponse;
    NPPacketFunc m_pNPFunc;

private:
    void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override;
};


namespace NPSystem {


class CNPSocket : public CIOSocketEx<IOBuffer32k> {
public:
    static constexpr int MAX_EXECUTION_QUEUE = 0x1000;

    explicit CNPSocket(std::uint64_t socket);
    ~CNPSocket() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void Clear();
    void SendBuffer(int command, int transactionId, IOBuffer32k* buffer);
    void GeneratePacketHistory(char* buffer, std::uint64_t bufferSize);
    bool CheckPendingWrite() override;

protected:
    static constexpr int MAX_DEBUG_PACKET_COUNT = 8;

    void OnRead() override;
    void OnClose() override;

    CSTSResponse* m_pResponse;
    SocketMode m_nMode;
    std::int32_t m_nPacketLength;
    volatile std::int32_t m_nLastCommandIndex;
    std::int32_t m_nLastCommands[MAX_DEBUG_PACKET_COUNT];
    std::int32_t m_nLastTRIDs[MAX_DEBUG_PACKET_COUNT];

private:
    L2_DECLARE_OBJECT_REGISTRATION(CNPSocket)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CNPSocket)
};


} // namespace NPSystem

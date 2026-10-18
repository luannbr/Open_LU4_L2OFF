#pragma once

#include "Shared/src/IOC.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CCommSocket;

using CommCallback = bool (*)(CCommSocket*, const std::uint8_t*);

// Current commsocket.cpp table 0x015cce60: opcodes 0..25 followed by the
// physical CommDummyPacket sentinel.
extern CommCallback g_CommPacketTable[27];

class CCommSocket final : public CIOSocket {
public:
    CCommSocket(std::uint64_t socket, CommCallback* packetTable);
    ~CCommSocket() override;

    L2_DECLARE_OBJECT_TYPE(CCommSocket)

    void Send(const char* format, ...) override;
    void SendV(const char* format, va_list arguments) override;

protected:
    void OnClose() override;
    void OnRead() override;

private:
    std::uint64_t m_socket; // +0xb0
    [[maybe_unused]] std::int32_t m_threadCount; // +0xb8
    [[maybe_unused]] std::uint32_t m_alignmentBC; // +0xbc
    CommCallback* m_packetTable; // +0xc0
    SocketMode m_mode; // +0xc8
    std::int32_t m_packetLength; // +0xcc
};

using CCommPacketBase = CPacketEx<DefaultIOBuffer, CCommSocket, CommCallback>;

class CCommPacket final : public CCommPacketBase {
public:
    CCommPacket() = default;
    ~CCommPacket() override = default;
};

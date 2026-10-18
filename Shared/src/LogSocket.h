#pragma once

#include "Shared/src/MemorySub.h"

#include "Shared/src/IOC.h"

#include <cstddef>
#include <cstdint>

class CLogSocket;

using LogPacketFunc = bool (*)(CLogSocket*, const std::uint8_t*);

using CLogPacketBuffer = CIOBufferEx<20480>;

// Shared/logSocket.h is instantiated by each server executable. Cached.exe
// selects the CIOCriticalSection-backed specialization. L2NPC.exe and
// L2Server.exe select the CExecutionSerializerFast-backed specialization
// from the same template.
#if defined(L2_BUILD_CACHED)
using CLogSocketBase = CIOSocketEx<DefaultIOBuffer, CIOSocketCriticalSectionWriteControl>;
#else
using CLogSocketBase = CIOSocket;
#endif

// Exact LogD opcodes used by the 2018 L2NPC target.
enum class LogDPacket : std::uint8_t {
    Send = 0,
    SetCheckStatus = 1,
    CheckLog = 2,
    ServerStarted = 3,
};

// Exact PDB global `LogPacketFunc logdbPacketTable[3]`.
extern LogPacketFunc logdbPacketTable[3];

// CLogSocket is a direct CIOSocketEx specialization in both targets. It is
// not either process's CSocket class.
class CLogSocket : public CLogSocketBase {
public:
    static constexpr int kMaximumBodySize = 0x4ffe;
    static constexpr int kMaximumFrameSize = kMaximumBodySize + 2;
#if defined(L2_BUILD_CACHED)
    static constexpr int kServerType = 2;
#elif defined(L2_BUILD_L2SERVER)
    static constexpr int kServerType = 3;
#else
    static constexpr int kServerType = 4;
#endif

    CLogSocket(std::uint64_t s, LogPacketFunc* aPacketTable);
    ~CLogSocket() override;

    L2_DECLARE_OBJECT_TYPE(CLogSocket)

    void Send(const char* format, ...) override;
    bool CheckPendingWrite() override;

protected:
    void OnRead() override;
    void OnClose() override;

    // The suffix begins at +0xa8 in CacheD and +0xb0 in L2NPC.
    std::uint64_t sock;
    std::int32_t threadCount;
    std::uint32_t threadPadding;
    LogPacketFunc* packetTable;
    SocketMode mode;
    std::int32_t packetLen;

private:
    friend bool LogSendMsgPacket(CLogSocket* socket, const std::uint8_t* packet);
    friend bool LogCheckStatus(CLogSocket* socket, const std::uint8_t* packet);
    friend bool LogTestPacket(CLogSocket* socket, const std::uint8_t* packet);
};


using CLogPacket = CPacketEx<CLogPacketBuffer, CLogSocket, LogPacketFunc>;

bool LogSendMsgPacket(CLogSocket* socket, const std::uint8_t* packet);
bool LogCheckStatus(CLogSocket* socket, const std::uint8_t* packet);
bool LogTestPacket(CLogSocket* socket, const std::uint8_t* packet);

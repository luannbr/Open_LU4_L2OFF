#pragma once

#include "Shared/src/MemorySub.h"

#include "Protocol.h"
#include "Shared/src/IOC.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>

namespace l2npc {

class CDBNPC;
class CDBNPCSocket;

using CacheDCallback = bool (*)(CDBNPCSocket*, const std::uint8_t*);

using CacheDCallbackMap = FixedSizeCallbackMap<8, CacheDCallback>;

extern CacheDCallbackMap gPacketTableFromCache;


// CDBNPCSocket derives directly from CIOSocketEx<CIOBufferEx<20480>> in the
// shipping executable. Unlike CServerSocket it is not a CSocket.
class CDBNPCSocket : public CIOSocket {
public:
    // Current EXE 0x004210fc and exact donor PDB:
    // CDBNPCSocket(unsigned __int64).
    explicit CDBNPCSocket(std::uint64_t socket);
    ~CDBNPCSocket() override;

    L2_DECLARE_OBJECT_TYPE(CDBNPCSocket)

    void Send(const char* format, ...) override;
    void SendV(const char* format, va_list arguments) override;
    void OnRead() override;
    void OnClose() override;

protected:
    // Exact adjacent-PDB names and current target suffix at +0xb0..+0xcf.
    std::uint64_t sock;
    std::int32_t threadCount;
    CacheDCallback* packetTable;
    SocketMode mode;
    std::int32_t packetLen;
};


using CDBNPCPacket = CPacketEx<DefaultIOBuffer, CDBNPCSocket, CacheDCallback>;

// DbNpcSocket.cpp exports these as free C callbacks taking
// (CDBNPCSocket*, const unsigned char*), exactly as in the target PDB.
bool DummyPacket(CDBNPCSocket* socket, const std::uint8_t* packet);
bool ReplyLoadNpcInfo(CDBNPCSocket* socket, const std::uint8_t* packet);
bool NpcLoadLottoInfo(CDBNPCSocket* socket, const std::uint8_t* packet);
bool NpcUpdateLottoGame(CDBNPCSocket* socket, const std::uint8_t* packet);
bool ReplyLoadNpcInfoEx(CDBNPCSocket* socket, const std::uint8_t* packet);
bool LoadDBSavingMap(CDBNPCSocket* socket, const std::uint8_t* packet);
bool GetEventRankerInfo(CDBNPCSocket* socket, const std::uint8_t* packet);
bool GiveEventData(CDBNPCSocket* socket, const std::uint8_t* packet);
bool ReplyCheckKamarokaReward(CDBNPCSocket* socket, const std::uint8_t* packet);

} // namespace l2npc

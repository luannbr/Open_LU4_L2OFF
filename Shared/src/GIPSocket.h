#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/TemplateHelper.h"

#include <cstdarg>
#include <cstdint>

class CGIPSocket;

using GIPPacketCallback = bool (*)(CGIPSocket*, const unsigned char*);

bool DummyGIPPacket(CGIPSocket* socket, const unsigned char* packet);
bool GIPResultPacket(CGIPSocket* socket, const unsigned char* packet);

using GIPCallbackMap = FixedSizeCallbackMap<3, GIPPacketCallback>;

// Shared/GIPSocket.cpp source names recovered from the symbol-complete donor
// PDB and fixed by the current constructor/Bind/data xrefs.
// Protocols 0 and 1 remain dummy entries. Protocol 2 is
// MSG_SET_GAMESERVER_INFO_RESULT.
extern GIPCallbackMap g_PacketTableFromGIP;
extern GIPPacketCallback* GIPPacketTable;


// Shipping owner: Shared/GIPSocket.{h,cpp}. Cached.exe instantiates a
// synchronous CIOBufferEx<8192> socket here. The portable CIOSocket base
// keeps the same framing, callback and queued-write behavior while its native
// descriptor remains in the common Shared/Portable transport registry.
class CGIPSocket final : public CIOSocketEx<IOBuffer8k> {
public:
    CGIPSocket(std::uint64_t socket, GIPPacketCallback* packetTable);
    ~CGIPSocket() override;

#if defined(L2_BUILD_L2SERVER)
    L2_DECLARE_OBJECT_TYPE(CGIPSocket)
#endif

    void Send(const char* format, ...) override;
    void SendV(const char* format, va_list arguments) override;
    bool CheckPendingWrite() override;

protected:
    void OnClose() override;
    void OnRead() override;

private:
    // Current target tail begins at +0xa8 and makes CGIPSocket 0xc8 bytes.
    // The portable generic CIOSocket host base has an ABI-only tail. These
    // source members nevertheless keep the original order and meaning.
    std::uint64_t m_Socket;
    int m_threadCount;
    std::uint32_t m_targetAlignmentPadding;
    GIPPacketCallback* m_packetTable;
    SocketMode m_mode;
    int m_packetLength;
};

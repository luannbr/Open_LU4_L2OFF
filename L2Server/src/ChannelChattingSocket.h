#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>

class CChannelChattingSocket;

using ChannelChattingPacketCallback = bool (*)(CChannelChattingSocket*, const unsigned char*);

// Current L2Server keeps the exact class-ID/name carrier and callback ABI.
// the matched TotalChecker PDB supplies the eliminated physical declaration.
// No ctor, dtor, vtable or runtime method body is compiled by the current image.
class CChannelChattingSocket : public CIOSocket {
public:
    CChannelChattingSocket(std::uint64_t socket, ChannelChattingPacketCallback* packetTable);
    ~CChannelChattingSocket() override;

    L2_DECLARE_OBJECT_TYPE(CChannelChattingSocket)

    void Send(const char* format, ...) override;
    bool CheckPendingWrite() override;
    bool IsValidSockete();

protected:
    void OnRead() override;
    void OnClose() override;

    std::uint64_t sock; // +0xb0
    std::int32_t threadCount; // +0xb8
    ChannelChattingPacketCallback* packetTable; // +0xc0
    SocketMode mode; // +0xc8
    std::int32_t packetLen; // +0xcc

private:
};

using ChannelChattingPacketCallbackMap = FixedSizeCallbackMap<0x4009, ChannelChattingPacketCallback>;

extern ChannelChattingPacketCallbackMap gPacketTableFromChannelChatting;
extern ChannelChattingPacketCallback* ChannelChattingPacketTable;

bool DummyPacket(CChannelChattingSocket* socket, const unsigned char* packet);
bool ChannelChattingServerVersion(CChannelChattingSocket* socket, const unsigned char* packet);
bool ChannelChattingAuthenticateUser(CChannelChattingSocket* socket, const unsigned char* packet);
bool ChannelChattingAuthenticatePledge(CChannelChattingSocket* socket, const unsigned char* packet);

#pragma once

#include "Platform/WindowsTypes.h"

#include <cstdint>

// Original owner: Src/sendmail.h. L2Authd.pdb type 0x7379, field list
// 0x7378, target size 0x418. This is AuthD's private synchronous SMTP
// client. It is unrelated to Shared/SystemMail.
class SendMail {
public:
    SendMail();
    ~SendMail();

    void SocketClose();
    int Init();
    char* PrintErrorMessage(
        int errorCode,
        char* errorMessage);
    int ConnectMailServer();
    int SendMessageA(
        char* receiver,
        char* mailTitle,
        char* message);
    int PacketHelo();
    int PacketMailFrom();
    int PacketRcptTo();
    int PacketData(
        char* mailTitle,
        char* message);
    int PacketQuit();

private:
    std::uint32_t mSock;
    sockaddr_in mAddr;
    [[maybe_unused]] std::int32_t mMailType;
    char mReceiver[1024];
};

extern SendMail Mailservice;

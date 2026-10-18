#pragma once

#include "Shared/src/Runtime/WindowsTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Original owner: mailreport.h / mailreport.cpp. The x86 PDB layout is 0x14c.
// only std::vector and native socket storage naturally follow the host ABI.
class CMailReport {
public:
    CMailReport();
    ~CMailReport();

    static CMailReport* Instance();
    bool Initialize(
        const char* server,
        std::int16_t port,
        const char* sender,
        const char* receiver,
        const char* appName,
        const char* productVersion,
        const char* appVersion);
    bool SendMail(const char* subject, const char* body);
    void AddAttachment(const char* path);

private:
    void SocketClose();
    bool SocketSend(
        std::uint32_t socket,
        const char* data,
        int length,
        int flags);
    bool SocketRecvLine(
        std::uint32_t socket,
        char* data,
        int length,
        int flags);
    bool GetLocalAddress();
    bool ConnectMailServer();
    bool SendPacket(
        const char* packet,
        const char* expectedReply,
        std::uint32_t* replyCode);
    bool PacketHelo();
    bool PacketMailFrom();
    bool PacketRcptTo();
    bool PacketQuit();
    bool PacketData(const char* subject, const char* body);

    static CMailReport* sInstance;

    sockaddr_in mServerAddr;
    std::uint32_t mSock;
    std::int32_t mSocketTimeout;
    std::array<char, 32> mAppName;
    std::array<char, 32> m_szProductVersion;
    std::array<char, 32> m_szAppVer;
    std::array<char, 64> mSender;
    std::array<char, 64> mReceiver;
    in_addr mLocalIPAddr;
    std::array<char, 64> mLocalHostname;
    std::vector<std::string> mAttachments;
};

extern const char* REPLY_SERVICE_READY;
extern const char* REPLY_MAIL_ACTION_OK;
extern const char* REPLY_ENTER_EMAIL;

extern CMailReport g_reportMail;

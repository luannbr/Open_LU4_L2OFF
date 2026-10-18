#pragma once

#include "Platform/WindowsTypes.h"

#include <string>
#include <vector>

// Original owner: Src/mailreport.h. L2Authd.pdb type 0x857F records a
// 0x138-byte VS2013/x86 object. Native STL containers follow
// the portable host ABI. Product fields and the complete method surface are
// kept in their PDB order.
class CMailReport {
public:
    CMailReport();
    ~CMailReport();

    static CMailReport* Instance();

    bool Initialize(
        const char* server,
        const short port,
        const char* sender,
        const char* receiver,
        const char* applicationName,
        const char* productVersion,
        const char* buildNumber);
    bool SendMail(
        const char* subject,
        const char* message);
    void AddAttachment(const char* filename);
    void ClearAttachments();

private:
    bool GetLocalAddress();
    void SocketClose();
    bool ConnectMailServer();
    bool PacketHelo();
    bool PacketMailFrom();
    bool PacketRcptTo();
    bool PacketData(
        const char* subject,
        const char* message);
    bool PacketQuit();
    bool SendPacket(
        const char* packet,
        const char* expectedReply,
        unsigned long* receivedReply);
    bool SocketSend(
        unsigned int socket,
        const char* buffer,
        int length,
        int flags);
    bool SocketRecvLine(
        unsigned int socket,
        char* buffer,
        int length,
        int flags);

    static CMailReport* sInstance;

    sockaddr_in mServerAddr;                 // target +0x000
    unsigned int mSock;                      // target +0x010
    int mSocketTimeout;                      // target +0x014
    char mAppName[32];                       // target +0x018
    std::string m_buildNumber;               // target +0x038
    std::string m_productVersion;            // target +0x050
    char mSender[64];                        // target +0x068
    char mReceiver[64];                      // target +0x0a8
    in_addr mLocalIPAddr;                    // target +0x0e8
    char mLocalHostname[64];                 // target +0x0ec
    std::vector<std::string> mAttachments;   // target +0x12c
};

// These are writable pointer objects in L2Authd.pdb, not preprocessor
// constants. Their pointees are the three SMTP status prefixes used by the
// selected image.
extern const char* REPLY_SERVICE_READY;
extern const char* REPLY_MAIL_ACTION_OK;
extern const char* REPLY_ENTER_EMAIL;

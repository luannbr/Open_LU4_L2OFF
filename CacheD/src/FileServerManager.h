#pragma once

#include "Shared/src/IOCs.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstdint>
#include <string>

class CSocket;
bool RequestSetPledgeEmblem(CSocket* socket, const unsigned char* packet);

// Current CacheD owner recovered from FileServerManager.cpp. Host std::string
// layouts differ from the shipping MSVC 0x28-byte strings, so this socket
// keeps behavior and field ownership without claiming a false host ABI.
class FileServerSocket {
public:
    FileServerSocket();
    ~FileServerSocket();

    FileServerSocket(const FileServerSocket&) = delete;
    FileServerSocket& operator=(const FileServerSocket&) = delete;

    void SetEmblem(char* emblem, int size);
    void SetHeader();
    void GetResponse(std::string& response);
    void Send();
    bool GetDownloadURL(std::string& response, std::string& downloadUrl);

private:
    friend class FileServerManager;

    std::intptr_t m_socket = -1; // target +0x00
    std::string m_header; // target +0x08
    std::string m_emblem; // target +0x30
    std::string m_unused; // target +0x58
    bool m_connected = false; // target +0x80
};

class FileServerManager {
public:
    FileServerManager();

    bool TestConnection();
    bool Initialize();
    bool RequestRegistEmblem(char* emblem, int size, int userSocketId, int pledgeId, int pledgeSocketId);
    void TimerExpired();

private:
    friend bool ::RequestSetPledgeEmblem(CSocket* socket, const unsigned char* packet);

    // No source identifier survives for the target's 0x10090-byte queue
    // carrier. Offsets are fixed by RequestRegistEmblem/TimerExpired.
    struct PendingRegistration {
        std::array<char, 0x10080> emblem{}; // +0x00000
        int size = 0; // +0x10080
        int userSocketId = 0; // +0x10084
        int pledgeId = 0; // +0x10088
        int pledgeSocketId = 0; // +0x1008c
    };

    std::deque<PendingRegistration*> m_requests; // +0x00
    CIOCriticalSection m_lock; // +0x30
    bool m_ready = false; // +0x58
    std::uint8_t m_targetPadding59[3]{};
    int m_intervalMilliseconds = 1000; // +0x5c
};


// DAT_01c6b1f0 has no kept source spelling. This is its single semantic
// process owner, not an address compatibility alias.
extern FileServerManager g_fileServerManager;

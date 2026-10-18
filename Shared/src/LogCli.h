#pragma once

#include "Shared/src/LogSocket.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

enum LogType : int;

class CLog;
class CLogCli;
unsigned ConnectThread(void* argument);
void ConnectThread_common();
void ConnectThread_mailing();

namespace runtime::log {

struct CLogCliPortableAccess;

struct LogClientThreadIdentity {
    std::uint32_t id = 0;
    void* handle = nullptr;
};

LogClientThreadIdentity StartLogClientConnectionWorker(CLogCli* client);

void ConfigureLogClient(CLogCli& client, std::string host, int port);
void WakeLogClientConnectionWorker(CLogCli& client);
bool WaitForLogClientConnectionRequest(CLogCli& client);
void StartLogClientReceiving(CLogCli& client);
void StopLogClient(CLogCli& client);

} // namespace runtime::log

// Recovered CLogCli connection worker. The target creates this thread in a
// suspended state. CLog::ConnectToLogD wakes it for one connection attempt.
class CLogCli {
public:
    CLogCli();
    ~CLogCli();

    bool Init();
    bool Connect();
    void LogSendMsg(LogType logType, char16_t* timestampedMessage);
    void Enter();
    void Leave();

protected:
    friend struct runtime::log::CLogCliPortableAccess;
    friend unsigned ConnectThread(void* argument);
    friend void ConnectThread_common();
    friend bool LogCheckStatus(CLogSocket* socket, const std::uint8_t* packet);

    void ConfirmConnection();

    // Complete target CLogCli layout: 0x10-byte CSPointer followed by the
    // 0x28-byte Win64 CRITICAL_SECTION at +0x10. Native transport and
    // synchronization live in a separate portable sidecar.
    ::CSPointer<CLogSocket> m_spLogSocket;
    std::array<std::byte, 0x28> m_section;
};


// Exact PDB global from logcli.cpp. In the current EXE it immediately follows
// the 0x1690-byte process CLog object.
extern CLogCli logcli;

#pragma once

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CGIPSocket;
class CGameInfoPipeline;
struct CGameInfoPipelinePortableAccess;
bool GIPResultPacket(CGIPSocket* socket, const unsigned char* packet);

// Original Shared/GIP.cpp free-function owners kept by the
// symbol-complete VS2015 Shared donor and confirmed by the current CacheD
// thread graph.
void GIPConnectThread_common();
void GIPConnectThread_mailing();
unsigned int GIPConnectThread(void* argument);

namespace runtime {

// Cooperative host-process teardown for the native transport sidecar. The
// shipping process reaches the CGameInfoPipeline destructor only during CRT
// termination. Portable console shutdown must stop its worker before the IO
// runtime and socket subsystem disappear.
void StopGameInfoPipeline(CGameInfoPipeline& pipeline);

} // namespace runtime

// Shipping owner: Shared/GIP.{h,cpp}. The Windows-only critical sections,
// thread handle and file handle remain as their exact target-sized fields.
// portable native resources are bound outside this fixed object.
class CGameInfoPipeline {
public:
    CGameInfoPipeline();
    ~CGameInfoPipeline();

    void SetSendable(bool sendable);
    void Init();
    bool ConnectToDaemon();
    void _CreateNewGIPFile(int year, int month, int day, int hour, int halfHour);
    void FlushGIPFile();
    void TryConnectingToDaemon();
    void CheckGIPFile();
    void AddV(const wchar_t* format, va_list arguments);
    void _Add(const wchar_t* format, ...);

private:
    friend class CGIPSocket;
    friend struct CGameInfoPipelinePortableAccess;
    friend void runtime::StopGameInfoPipeline(CGameInfoPipeline&);
    friend bool GIPResultPacket(CGIPSocket*, const unsigned char*);

    CGIPSocket* m_socket; // +0x000
    bool m_connected; // +0x008
    bool m_sendable; // +0x009
    std::array<std::byte, 2> m_padding00a; // +0x00a
    std::uint32_t m_workerThreadId; // +0x00c
    std::uint64_t m_workerThreadHandle; // +0x010
    std::array<std::byte, 0x28> m_connectionLock; // +0x018
    std::uint64_t m_fileHandle; // +0x040
    std::array<char16_t, 256> m_fileName; // +0x048
    std::array<std::byte, 0x28> m_fileLock; // +0x248
    int m_fileYear; // +0x270
    int m_fileMonth; // +0x274
    int m_fileDay; // +0x278
    int m_fileHour; // +0x27c
    int m_fileHalfHour; // +0x280
    std::uint32_t m_failCount; // +0x284
};


// Exact process-global name kept by the symbol-complete Shared donor.
extern CGameInfoPipeline GIP;

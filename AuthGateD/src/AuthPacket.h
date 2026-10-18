#pragma once

#include "IOBuffer.h"
#include "IOObject.h"
#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstdint>

class CAuthSocket;

// AuthGateD.pdb type length 0x1c in the selected x86 ABI.
class CAuthPacket : public CIOObject {
public:
    CAuthPacket();
    ~CAuthPacket() override;

    void Init(
        CAuthSocket* socket,
        std::uint32_t sessionId,
        CIOBuffer* buffer,
        int packetLength);
    static CAuthPacket* Alloc();

    virtual void OnIOCallback(
        int success,
        std::uint32_t transferred,
        _OVERLAPPED* overlapped);

    CAuthSocket* m_pSocket = nullptr;
    std::uint32_t m_sessionId = 0;
    CIOBuffer* m_pBuf = nullptr;
    std::int32_t m_packetLen = 0;
};

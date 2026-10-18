#pragma once

#include "Shared/src/IOC.h"

#include <cstdint>


// Shared/IOCPMessage: the completion-key object posted to the process IOCP.
// The portable transport changes only PostQueuedCompletionStatus itself.
class CIOCPMessage : public CIOObject {
public:
    ~CIOCPMessage() override = default;

    virtual void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override;
    virtual void OnReceive(int argument) = 0;

    // PDB: void CIOCPMessage::Post(int). The signed source type is forwarded
    // to the DWORD completion field without changing its bit pattern.
    void Post(int transferred);
};

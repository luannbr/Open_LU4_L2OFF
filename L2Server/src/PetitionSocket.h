#pragma once

#include "Shared/src/IOC.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CPetitionSocket;

using PetitionCallback = bool (*)(CPetitionSocket*, const std::uint8_t*);

inline constexpr std::size_t kPetitionPacketTableSize = 150;
inline constexpr std::uint8_t kPetitionPacketBaseOpcode = 101;

extern std::array<PetitionCallback, kPetitionPacketTableSize> petitionPacketTable;

bool PetitionDummyPacket(CPetitionSocket* socket, const std::uint8_t* packet);
bool ToSendPacketError(CPetitionSocket* socket, const std::uint8_t* packet);
bool PetitionServerVersion(CPetitionSocket* socket, const std::uint8_t* packet);
bool PetitionAcceptWorld(CPetitionSocket* socket, const std::uint8_t* packet);
bool ExternalNotify(CPetitionSocket* socket, const std::uint8_t* packet);

class CPetitionSocket final : public CIOSocket {
public:
    CPetitionSocket(std::uint64_t nativeSocket, PetitionCallback* callbacks);
    ~CPetitionSocket() override;

    L2_DECLARE_OBJECT_TYPE(CPetitionSocket)

    void Send(const char* format, ...) override;

protected:
    void OnRead() override;
    void OnClose() override;

    std::uint64_t sock; // +0xb0
    std::int32_t threadCount; // +0xb8
    std::uint32_t alignmentBC; // +0xbc
    PetitionCallback* packetTable; // +0xc0
    SocketMode mode; // +0xc8
    std::int32_t packetLen; // +0xcc
};

using CPetitionPacketBase = CPacketEx<DefaultIOBuffer, CPetitionSocket, PetitionCallback>;

class CPetitionPacket final : public CPetitionPacketBase {
public:
    CPetitionPacket() = default;
    ~CPetitionPacket() override = default;
};

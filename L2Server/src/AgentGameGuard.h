#pragma once

#include "Shared/src/MemorySub.h"

#include "Agent.h"
#include "External/GameGuardServerLibCompat.h"
#include "Portable/ProcessRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CDBSocket;

bool LoadSelectedCharacterPacket(CDBSocket* socket, const std::uint8_t* packet);

class GameGuardAgent final : public Agent {
public:
    L2_DECLARE_OBJECT_REGISTRATION(GameGuardAgent)

    ~GameGuardAgent() override;

    L2_DECLARE_OBJECT_SINGLE_ALLOCATOR_LITERAL(GameGuardAgent)

    L2_DECLARE_OBJECT_RELEASE(GameGuardAgent)

    bool QueryFirstImpl(PacketList::iterator current) override;
    bool ResponseFirstImpl(const unsigned char** packet) override;
    bool QuerySecondImpl() override;
    bool TimeCheckImpl() override;
    bool ResponseSecond(const unsigned char** packet) override;

    static void CheckGameGuardInvalidVersion(User* user, std::uint32_t reason);

    std::uint32_t m_lastRequestTick; // +0x28
    std::uint32_t m_lastResponseTick; // +0x2c
    std::uint32_t m_noResponceCount; // +0x30

private:
    friend class User;
    friend bool LoadSelectedCharacterPacket(CDBSocket* socket, const std::uint8_t* packet);

    explicit GameGuardAgent(Agent* previous);

    std::array<std::byte, 0x04> m_alignment0034To0038; // +0x34..+0x37
    CCSAuth2 m_csa; // +0x38..+0x8af
};

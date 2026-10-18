#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class User;

enum AgentType : std::int32_t {
    INSPECTOR = 1,
    GAME_GUARD = 2,
    NEW_INSPECTOR = 3,
};


class Agent : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(Agent)

    ~Agent() override;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    using PacketList = std::vector<CPacketHelperEx<20480>*>;

    bool QueryFirst(const PacketList& packets, PacketList::iterator current);
    bool ResponseFirst(const unsigned char** packet);
    bool QuerySecond();
    bool TimeCheck();

    virtual bool QueryFirstImpl(PacketList::iterator current) = 0;
    virtual bool ResponseFirstImpl(const unsigned char** packet) = 0;
    virtual bool QuerySecondImpl() = 0;
    virtual bool TimeCheckImpl() = 0;
    virtual bool ResponseSecond(const unsigned char** packet) = 0;

    User* GetUser() const noexcept { return myUser; }

protected:
    Agent(Agent* previous, AgentType type);
    Agent(User* user, AgentType type);

    Agent* nextAgent; // +0x18
    User* myUser; // +0x20
};

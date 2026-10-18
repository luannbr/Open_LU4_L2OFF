#include "Agent.h"
#include "Shared/src/MemorySub.h"

#include "User.h"

#include "L2Core/src/inc/Guard.h"


Agent::Agent(Agent* previous, AgentType type) : nextAgent(previous), myUser(previous->myUser) {
    L2_GUARD_FUNCTION();
    myUser->m_AgentMap.emplace(type, this);
}

Agent::Agent(User* user, AgentType type) : nextAgent(nullptr), myUser(user) {
    L2_GUARD_FUNCTION();
    myUser->m_AgentMap.emplace(type, this);
}

Agent::~Agent() {
    L2_GUARD_FUNCTION();
    if (nextAgent != nullptr) {
        L2_RELEASE(nextAgent, ObjectRefType::CreateDelete);
    }
}

L2_DEFINE_OBJECT_RELEASE(Agent)

bool Agent::QueryFirst(const PacketList& packets, PacketList::iterator current) {
    // Current L2Server.exe 0x00450dd4, original owner Agent.cpp.
    L2_GUARD_FUNCTION();
    if (!QueryFirstImpl(current)) {
        return false;
    }

    auto nextPacket = current;
    ++nextPacket;
    return nextAgent == nullptr || nextPacket == packets.end() || nextAgent->QueryFirst(packets, nextPacket);
}

bool Agent::ResponseFirst(const unsigned char** packet) {
    // Current L2Server.exe 0x00450c38, original owner Agent.cpp.
    L2_GUARD_FUNCTION();
    if (!ResponseFirstImpl(packet)) {
        return false;
    }
    return nextAgent == nullptr || nextAgent->ResponseFirst(packet);
}

bool Agent::QuerySecond() {
    // Current L2Server.exe 0x00450b74, original owner Agent.cpp.
    L2_GUARD_FUNCTION();
    if (!QuerySecondImpl()) {
        return false;
    }
    return nextAgent == nullptr || nextAgent->QuerySecond();
}

bool Agent::TimeCheck() {
    // Current L2Server.exe 0x00450d10, original owner Agent.cpp.
    L2_GUARD_FUNCTION();
    if (!TimeCheckImpl()) {
        return false;
    }
    return nextAgent == nullptr || nextAgent->TimeCheck();
}

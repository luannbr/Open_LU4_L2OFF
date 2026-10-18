#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>


// L2NPC.pdb: TimeAttackEventRoom is the process-lifetime singleton that
// owns event-room party assignments. The target key type is its custom
// std::CDualKey<int,int>, CDualKey preserves the same member names and
// lexicographic ordering without extending the host's namespace std.
class TimeAttackEventRoom : public Singleton<TimeAttackEventRoom> {
public:
    TimeAttackEventRoom() = default;
    ~TimeAttackEventRoom() override = default;

    bool AddParty(int roomNumber, int partType, int partySid);
    int Clear(int roomNumber, int partType);
    int GetParty(int roomNumber, int partType);

private:
    using key_type = std::CDualKey<int, int>;
    using map_type = std::map<key_type, int>;

    CIOCriticalSection mLock;
    map_type mEventRoomMap;
};


namespace l2npc {

using ListenerListType = std::vector<int>;


// SSQ_NpcServer.cpp process registry used by OnSSQSysEvent. The shipping
// implementation owns a map<int, vector<int>> behind CIOCriticalSection.
class GlobalEventListener {
public:
    int GetListenerList(ListenerListType& result, int eventType);
    int Register(int eventType, int npcSmartId);

private:
    using map_type = std::map<int, ListenerListType>;
    using value_type = map_type::value_type;

    CIOCriticalSection mLock;
    map_type mEventListenerMap;
};


// Exact process-global object from SSQ_NpcServer.cpp.
extern GlobalEventListener g_EventListener;

} // namespace l2npc

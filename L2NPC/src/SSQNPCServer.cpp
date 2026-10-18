#include "SSQNPCServer.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/ThreadManager.h"


bool TimeAttackEventRoom::AddParty(int roomNumber, int partType, int partySid) {
    L2_GUARD_FUNCTION();
    bool inserted;
    {
        L2_GUARD(mLock);
        const CDualKey<int, int> key(roomNumber, partType);
        const auto found = mEventRoomMap.find(key);
        inserted = found == mEventRoomMap.end();
        if (inserted) {
            // The current body performs a second lookup through operator[],
            // default-constructs the mapped int, and only then assigns the SID.
            mEventRoomMap[key] = partySid;
        }
    }
    return inserted;
}

int TimeAttackEventRoom::Clear(int roomNumber, int partType) {
    L2_GUARD_FUNCTION();
    int partySid;
    {
        L2_GUARD(mLock);
        const CDualKey<int, int> key(roomNumber, partType);
        const auto found = mEventRoomMap.find(key);
        partySid = -1;
        if (found != mEventRoomMap.end()) {
            partySid = found->second;
            mEventRoomMap.erase(key);
        }
    }
    return partySid;
}

int TimeAttackEventRoom::GetParty(int roomNumber, int partType) {
    L2_GUARD_FUNCTION();
    int partySid;
    {
        L2_GUARD(mLock);
        const CDualKey<int, int> key(roomNumber, partType);
        const auto found = mEventRoomMap.find(key);
        partySid = found == mEventRoomMap.end() ? -1 : mEventRoomMap[key];
    }
    return partySid;
}


namespace l2npc {

GlobalEventListener g_EventListener;

int GlobalEventListener::GetListenerList(ListenerListType& result, int eventType) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(mLock);
        const auto listener = mEventListenerMap.find(eventType);
        if (listener != mEventListenerMap.end()) {
            result = listener->second;
        }
    }
    return static_cast<int>(result.size());
}

int GlobalEventListener::Register(int eventType, int npcSmartId) {
    // Current L2NPC.exe 0x00538420 performs an explicit find. For a new
    // event it fills a temporary listener vector before inserting the map
    // node. It does not create an empty node through operator[].
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(mLock);
        const auto listener = mEventListenerMap.find(eventType);
        if (listener == mEventListenerMap.end()) {
            ListenerListType listeners;
            listeners.push_back(npcSmartId);
            mEventListenerMap.insert(value_type(eventType, listeners));
        } else {
            listener->second.push_back(npcSmartId);
        }
    }
    return 0;
}

} // namespace l2npc

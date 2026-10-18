#include "PartyRoomRegistry.h"

#include "PartyMatchingWaitingRoom.h"
#include "PartyRoomInfo.h"
#include "PartyRoomListFilter.h"
#include "User.h"


#include <algorithm>
#include <utility>

class AutoFinder {
public:
    explicit AutoFinder(CPartyRoomListFilter& filter) : m_filter(filter) {}

    bool operator()(CPartyRoomRegistry::PartyRoomPair pair);

private:
    CPartyRoomListFilter& m_filter;
};


bool AutoFinder::operator()(CPartyRoomRegistry::PartyRoomPair pair) {
    L2_GUARD_FUNCTION();
    CSPointer<CPartyRoom> room = pair.second;
    CPartyRoomRegistry::PartyRoomList rooms;
    const bool listed = CPartyRoomRegistry::inst().List(m_filter, rooms);
    if (!listed) {
        return room->CanJoin(m_filter.GetUser()) && m_filter.IsFilteredCondition(room);
    }

    CPartyRoom* const rawRoom = room.get();
    return room->CanJoin(m_filter.GetUser()) &&
        std::find_if(rooms.begin(), rooms.end(), [rawRoom](const CSPointer<CPartyRoom>& listedRoom) {
            return listedRoom.get() == rawRoom;
        }) != rooms.end();
}

CPartyRoomRegistry::CPartyRoomRegistry() {}

CPartyRoomRegistry::~CPartyRoomRegistry() {
    // Current L2Server.exe 0x0094c088 clears the three registries inside the
    // guarded body. RWLock and map storage teardown follows automatically.
    L2_GUARD_FUNCTION();
    m_roomIdMap.clear();
    m_roomMasterIdMap.clear();
    m_roomMemberIdMap.clear();
}

CPartyRoomRegistry& CPartyRoomRegistry::inst() {
    static CPartyRoomRegistry registry;
    return registry;
}

CMPCCRoomRegistry& CMPCCRoomRegistry::inst() {
    static CMPCCRoomRegistry registry;
    return registry;
}

int CPartyRoomRegistry::CountMember() {
    L2_GUARD_FUNCTION();
    return static_cast<int>(m_roomMemberIdMap.size());
}

bool CPartyRoomRegistry::IsEmptyRegisterdRoom() {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);
    return m_roomIdMap.empty();
}

CSPointer<CPartyRoom> CPartyRoomRegistry::FindByIdInternal(int roomId) {
    L2_GUARD_FUNCTION();
    const auto found = m_roomIdMap.find(roomId);
    return found == m_roomIdMap.end() ? CSPointer<CPartyRoom>() : found->second;
}

CSPointer<CPartyRoom> CPartyRoomRegistry::GetFirstRoomInternal() {
    return m_roomIdMap.empty() ? CSPointer<CPartyRoom>() : m_roomIdMap.begin()->second;
}

CSPointer<CPartyRoom> CPartyRoomRegistry::FindById(int roomId) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);
    return FindByIdInternal(roomId);
}

CSPointer<CPartyRoom> CPartyRoomRegistry::GetFirstRoom() {
    L2_READ_GUARD(m_lock);
    return GetFirstRoomInternal();
}

CSPointer<CPartyRoom> CPartyRoomRegistry::FindByMember(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);
    const auto found = m_roomMemberIdMap.find(static_cast<int>(user->Id()));
    return found == m_roomMemberIdMap.end() ? CSPointer<CPartyRoom>() : FindByIdInternal(found->second);
}

CSPointer<CPartyRoom> CPartyRoomRegistry::FindByMaster(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);
    const auto found = m_roomMasterIdMap.find(static_cast<int>(user->Id()));
    return found == m_roomMasterIdMap.end() ? CSPointer<CPartyRoom>() : FindByIdInternal(found->second);
}

bool CPartyRoomRegistry::CanRegister(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (FindByMember(user)) {
        return false;
    }
    return user->GetParty() == nullptr || user->IsPartyMaster();
}

bool CMPCCRoomRegistry::CanRegister(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    return !FindByMember(user) && user->IsMPCCMaster();
}

void CPartyRoomRegistry::RegisterPartyRoomMemberImpl(CSPointer<User> user, CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    const int userId = static_cast<int>(user->Id());
    int& registeredRoomId = m_roomMemberIdMap[userId];
    registeredRoomId = room->GetPartyRoomInfo().GetId();
}

void CPartyRoomRegistry::UnregisterPartyRoomMemberImpl(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    m_roomMemberIdMap.erase(static_cast<int>(user->Id()));
    g_PartyMatchingWaitingRoom.EnterWaitingRoom(user.get());
}

void CPartyRoomRegistry::RegisterPartyRoomMember(CSPointer<User> user, CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    RegisterPartyRoomMemberImpl(std::move(user), std::move(room));
}

void CPartyRoomRegistry::UnregisterPartyRoomMember(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    UnregisterPartyRoomMemberImpl(std::move(user));
}

void CPartyRoomRegistry::RegisterMemberList(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    for (const CSPointer<CRoomMember>& member : members) {
        if (!member) {
            continue;
        }

        CSPointer<User> existingUser = member->GetUser();
        if (!existingUser) {
            continue;
        }

        bool unavailable = false;
        {
            CSPointer<User> stateUser = member->GetUser();
            unavailable = stateUser->IsDeleting() || stateUser->m_Deleted != 0;
        }
        if (!unavailable) {
            CSPointer<CPartyRoom> roomArgument = room;
            CSPointer<User> userArgument = member->GetUser();
            RegisterPartyRoomMemberImpl(std::move(userArgument), std::move(roomArgument));
        }
    }
}

void CPartyRoomRegistry::UnregisterMemberList(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    for (const CSPointer<CRoomMember>& member : members) {
        if (!member) {
            continue;
        }

        CSPointer<User> existingUser = member->GetUser();
        if (!existingUser) {
            continue;
        }

        bool unavailable = false;
        {
            CSPointer<User> stateUser = member->GetUser();
            unavailable = stateUser->IsDeleting() || stateUser->m_Deleted != 0;
        }
        if (!unavailable) {
            CSPointer<User> userArgument = member->GetUser();
            UnregisterPartyRoomMemberImpl(std::move(userArgument));
        }
    }
}

bool CPartyRoomRegistry::Register(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    CSPointer<User> master = room->GetPartyRoomInfo().GetMasterUser();
    if (!CanRegister(master)) {
        return false;
    }

    L2_WRITE_GUARD(m_lock);
    const int roomId = room->GetPartyRoomInfo().GetId();
    CSPointer<CPartyRoom>& registeredRoom = m_roomIdMap[roomId];
    registeredRoom = room;
    const int masterId = static_cast<int>(master->Id());
    int& registeredMasterRoomId = m_roomMasterIdMap[masterId];
    registeredMasterRoomId = room->GetPartyRoomInfo().GetId();
    RegisterMemberList(std::move(room));
    return true;
}

bool CPartyRoomRegistry::Replace(CSPointer<User> oldMaster, CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    const CSPointer<User> newMaster = room->GetPartyRoomInfo().GetMasterUser();
    m_roomMasterIdMap.erase(static_cast<int>(oldMaster->Id()));
    m_roomMasterIdMap[static_cast<int>(newMaster->Id())] = room->GetPartyRoomInfo().GetId();
    return true;
}

void CPartyRoomRegistry::Unregister(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    if (!room) {
        return;
    }
    CPartyRoomHistoryManager::inst().AddHistory(room->GetPartyRoomInfo());

    L2_WRITE_GUARD(m_lock);
    const CSPointer<User> master = room->GetPartyRoomInfo().GetMasterUser();
    m_roomIdMap.erase(room->GetPartyRoomInfo().GetId());
    m_roomMasterIdMap.erase(static_cast<int>(master->Id()));
    UnregisterMemberList(std::move(room));
}

bool CPartyRoomRegistry::GetRoomList(PartyRoomList& rooms) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);
    for (const auto& entry : m_roomIdMap) {
        rooms.push_back(entry.second);
    }
    return true;
}

bool CPartyRoomRegistry::List(CPartyRoomListFilter& filter, PartyRoomList& rooms) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);
    rooms.reserve(15);
    int matchingIndex = 0;
    for (const auto& entry : m_roomIdMap) {
        if (filter.IsFilteredCondition(entry.second)) {
            ++matchingIndex;
            if (filter.IsInPage(matchingIndex)) {
                rooms.push_back(entry.second);
            }
        }
        if (rooms.size() > 15) {
            break;
        }
    }
    return true;
}

CSPointer<CPartyRoom> CPartyRoomRegistry::FindByAuto(CSPointer<User> user, CPartyRoomListFilter& filter) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);
    if (m_roomMemberIdMap.find(static_cast<int>(user->Id())) != m_roomMemberIdMap.end()) {
        return {};
    }

    PartyRoomMapReverseIterType candidate = std::find_if(m_roomIdMap.rbegin(), m_roomIdMap.rend(), AutoFinder(filter));
    if (candidate == m_roomIdMap.rend()) {
        return {};
    }
    return candidate->second;
}

bool CPartyRoomRegistry::IsPartyRoomMaster(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    const CSPointer<CPartyRoom> room = CPartyRoomRegistry::inst().FindByMaster(user);
    return room && room->IsPartyRoomMaster(std::move(user));
}

bool CPartyRoomRegistry::IsPartyRoomMember(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    const CSPointer<CPartyRoom> room = CPartyRoomRegistry::inst().FindByMember(user);
    return room && room->IsPartyRoomMember(std::move(user));
}

bool CMPCCRoomRegistry::IsPartyRoomMember(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    const CSPointer<CPartyRoom> room = CMPCCRoomRegistry::inst().FindByMember(user);
    return room && room->IsPartyRoomMember(std::move(user));
}

bool CPartyRoomRegistry::DoesShowPartyWantedMessage(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    return user->GetPM_WindowOpen() && (IsPartyRoomMaster(user) || (user->GetParty() && IsPartyRoomMember(std::move(user))));
}

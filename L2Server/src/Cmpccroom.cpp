#include "Cmpccroom.h"

#include "MultiPartyCommandChannel.h"
#include "Party.h"
#include "User.h"


#include <utility>

CMPCCRoom::CMPCCRoom(CMPCCRoomInfo& info) {
    UpdateInfo(info);
    m_MPCCRoomInfo.SetNewId();
}

CMPCCRoom::~CMPCCRoom() = default;

CPartyRoomInfo& CMPCCRoom::GetPartyRoomInfo() { return m_MPCCRoomInfo; }

CMPCCRoom* CMPCCRoom::CastCMPCCRoom() { return this; }

bool CMPCCRoom::IsMPCCRRoom() { return true; }

bool CMPCCRoom::IsPartyMember(CSPointer<User> user) { return m_MPCCRoomInfo.IsMPCCPartyMember(std::move(user)); }

bool CMPCCRoom::CanJoin(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    return user && CheckAvailableMemberLimit() && CheckAvailableLevel(user) && !IsPartyRoomMember(std::move(user));
}

void CMPCCRoom::GetPartyList(std::vector<CParty*>& parties) {
    L2_GUARD_FUNCTION();
    const CSPointer<CMultiPartyCommandChannel> channel = m_MPCCRoomInfo.GetMPCC();
    if (channel) {
        channel->GetAllParty(parties);
    }
}

bool CMPCCRoom::Join(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user || !CanJoin(user)) {
        return false;
    }

    L2_WRITE_GUARD(m_lock);
    if (FindWaitingMember(user) != m_waitingMemberList.end()) {
        return false;
    }
    m_waitingMemberList.push_back(
        user->IsPartyMaster() ? CRoomMemberFactory::MakeMPCCPartyMaster(std::move(user))
                              : CRoomMemberFactory::MakeMPCCIndividual(std::move(user))
    );
    return true;
}

bool CMPCCRoom::UpdateInfo(CPartyRoomInfo& info) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    m_MPCCRoomInfo = *info.CastCMPCCRoomInfo();
    return true;
}

bool CMPCCRoom::WithdrawInternal(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    const auto found = FindWaitingMember(user);
    if (found == m_waitingMemberList.end()) {
        return IsPartyMember(std::move(user));
    }
    m_waitingMemberList.erase(found);
    return true;
}

void CMPCCRoom::GetPartyMember(RoomMemberList& members) {
    L2_GUARD_FUNCTION();
    if (DoesPartyDismissed()) {
        return;
    }

    std::vector<CParty*> parties;
    GetPartyList(parties);
    const CSPointer<User> roomMaster = m_MPCCRoomInfo.GetMasterUser();
    for (CParty* const party : parties) {
        if (!party || party->GetMaster() == roomMaster.get()) {
            continue;
        }
        User* const master = party->GetMaster();
        if (master) {
            members.push_back(
                CRoomMemberFactory::MakeMPCCMemberPartyMaster(CSPointer<User>(master, ObjectRefType::FunctionChain))
            );
        }
    }
}

void CMPCCRoom::GetPartyMemberWithPM_WindowOpen(RoomMemberList& members) {
    L2_GUARD_FUNCTION();
    if (DoesPartyDismissed()) {
        return;
    }

    std::vector<CParty*> parties;
    GetPartyList(parties);
    for (CParty* const party : parties) {
        User* const master = party ? party->GetMaster() : nullptr;
        if (master && master->GetPM_WindowOpen()) {
            members.push_back(
                CRoomMemberFactory::MakeMPCCMemberPartyMaster(CSPointer<User>(master, ObjectRefType::FunctionChain))
            );
        }
    }
}

void CMPCCRoom::GetPartyMasterList(RoomMemberList& members) {
    L2_GUARD_FUNCTION();
    if (DoesPartyDismissed()) {
        return;
    }

    std::vector<CParty*> parties;
    GetPartyList(parties);
    for (CParty* const party : parties) {
        User* const master = party ? party->GetMaster() : nullptr;
        if (master) {
            members.push_back(
                CRoomMemberFactory::MakeMPCCMemberPartyMaster(CSPointer<User>(master, ObjectRefType::FunctionChain))
            );
        }
    }
}

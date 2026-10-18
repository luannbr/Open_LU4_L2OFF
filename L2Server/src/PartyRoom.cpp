#include "PartyRoom.h"
#include "Shared/src/MemorySub.h"

#include "Party.h"
#include "User.h"


#include <algorithm>
#include <utility>


L2_DEFINE_OBJECT_CLASS_ID(CPartyRoom)

L2_DEFINE_OBJECT_CLASS_NAME(CPartyRoom, m_pClassName)

L2_DEFINE_OBJECT_ALLOCATOR(CPartyRoom, __GetClassName())

L2_DEFINE_OBJECT_NEW(CPartyRoom)

CPartyRoom::CPartyRoom() :
    m_doesPartyDismissed(false),
    m_alignmentPaddingC9{} {}

CPartyRoom::CPartyRoom(CPartyRoomInfo& info) : CPartyRoom() {
    UpdateInfo(info);
    m_partyRoomInfo.SetNewId();
}

CPartyRoom::~CPartyRoom() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    m_waitingMemberList.clear();
}

L2_DEFINE_OBJECT_RELEASE(CPartyRoom)

CPartyRoomInfo& CPartyRoom::GetPartyRoomInfo() { return m_partyRoomInfo; }

bool CPartyRoom::CheckAvailableMemberLimit() {
    L2_GUARD_FUNCTION();
    return GetMemberCount() < GetPartyRoomInfo().GetMaxMemberLimit();
}

void CPartyRoom::SetPartyDismissed(bool value) { m_doesPartyDismissed = value; }

bool CPartyRoom::DoesPartyDismissed() const { return m_doesPartyDismissed; }

bool CPartyRoom::UpdateInfo(CPartyRoomInfo& info) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    GetPartyRoomInfo() = info;
    return true;
}

bool CPartyRoom::ReplaceMaster(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    WithdrawInternal(user);
    GetPartyRoomInfo().SetMaster(std::move(user));
    return true;
}

bool CPartyRoom::Withdraw(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    return WithdrawInternal(std::move(user));
}

int CPartyRoom::GetMemberCount() {
    L2_GUARD_FUNCTION();
    const CSPointer<User> master = GetPartyRoomInfo().GetMasterUser();
    if (!master) {
        return 0;
    }

    CParty* const party = master->GetParty();
    const int partyMemberCount = party ? party->GetMemberCount() : 1;
    return partyMemberCount + static_cast<int>(m_waitingMemberList.size());
}

bool CPartyRoom::CheckAvailableLevel(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    return GetPartyRoomInfo().IsBetweenLevelLimit(std::move(user));
}

bool CPartyRoom::IsPartyRoomMaster(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    return user && GetPartyRoomInfo().IsMaster(std::move(user));
}

bool CPartyRoom::IsPartyMember(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    return user && !m_doesPartyDismissed && GetPartyRoomInfo().IsPartyMember(std::move(user));
}

CPartyRoom::PartyRoomMemberIterType CPartyRoom::FindWaitingMember(CSPointer<User> user) {
    return std::find_if(
        m_waitingMemberList.begin(), m_waitingMemberList.end(),
        [rawUser = user.get()](const CSPointer<CRoomMember>& member) { return member && member->GetUser().get() == rawUser; }
    );
}

CSPointer<CRoomMember> CPartyRoom::GetMember(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (IsPartyRoomMaster(user)) {
        return GetPartyRoomInfo().GetMaster();
    }
    if (IsPartyMember(user)) {
        return CRoomMemberFactory::MakePartyMember(std::move(user));
    }

    L2_READ_GUARD(m_lock);
    const auto found = FindWaitingMember(std::move(user));
    return found == m_waitingMemberList.end() ? CSPointer<CRoomMember>() : *found;
}

bool CPartyRoom::IsPartyWaitingMember(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);
    return FindWaitingMember(std::move(user)) != m_waitingMemberList.end();
}

bool CPartyRoom::IsPartyRoomMember(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    return IsPartyRoomMaster(user) || IsPartyMember(user) || IsPartyWaitingMember(std::move(user));
}

bool CPartyRoom::CanJoin(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    return user && CheckAvailableMemberLimit() && CheckAvailableLevel(user) && !IsPartyRoomMember(std::move(user));
}

void CPartyRoom::GetPartyMasterList(RoomMemberList&) { L2_GUARD_FUNCTION(); }

bool CPartyRoom::WithdrawInternal(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    const auto found = FindWaitingMember(user);
    if (found == m_waitingMemberList.end()) {
        return IsPartyMember(std::move(user));
    }
    m_waitingMemberList.erase(found);
    return true;
}

bool CPartyRoom::Join(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user || !CanJoin(user)) {
        return false;
    }

    L2_WRITE_GUARD(m_lock);
    if (FindWaitingMember(user) != m_waitingMemberList.end()) {
        return false;
    }
    m_waitingMemberList.push_back(CRoomMemberFactory::MakePartyWaitingMember(std::move(user)));
    return true;
}

bool CPartyRoom::DismissParty() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    CParty* const party = m_partyRoomInfo.GetParty();
    if (!party) {
        return false;
    }

    std::vector<User*> partyMembers;
    party->GetAllMember(partyMembers);
    for (User* const member : partyMembers) {
        if (!member) {
            continue;
        }
        CSPointer<User> memberPointer(member, ObjectRefType::FunctionChain);
        if (!IsPartyRoomMaster(memberPointer)) {
            m_waitingMemberList.push_back(CRoomMemberFactory::MakePartyWaitingMember(std::move(memberPointer)));
        }
    }
    m_doesPartyDismissed = true;
    return true;
}

void CPartyRoom::GetPartyMaster(RoomMemberList& members) {
    L2_GUARD_FUNCTION();
    if (m_doesPartyDismissed) {
        return;
    }
    const CSPointer<CRoomMember> master = GetPartyRoomInfo().GetMaster();
    if (master) {
        members.push_back(master);
    }
}

void CPartyRoom::GetPartyMember(RoomMemberList& members) {
    L2_GUARD_FUNCTION();
    if (m_doesPartyDismissed) {
        return;
    }
    CParty* const party = GetPartyRoomInfo().GetParty();
    if (!party) {
        return;
    }

    std::vector<User*> partyMembers;
    party->GetAllMember(partyMembers);
    const CSPointer<User> roomMaster = GetPartyRoomInfo().GetMasterUser();
    for (User* const member : partyMembers) {
        if (!member || member == roomMaster.get()) {
            continue;
        }
        CSPointer<User> memberPointer(member, ObjectRefType::FunctionChain);
        members.push_back(
            IsMPCCRRoom() ? CRoomMemberFactory::MakeMPCCPartyMaster(std::move(memberPointer))
                          : CRoomMemberFactory::MakePartyMember(std::move(memberPointer))
        );
    }
}

void CPartyRoom::GetPartyMemberWithPM_WindowOpen(RoomMemberList& members) {
    L2_GUARD_FUNCTION();
    if (m_doesPartyDismissed) {
        return;
    }
    CParty* const party = GetPartyRoomInfo().GetParty();
    if (!party) {
        return;
    }

    std::vector<User*> partyMembers;
    party->GetAllMember(partyMembers);
    const CSPointer<User> roomMaster = GetPartyRoomInfo().GetMasterUser();
    for (User* const member : partyMembers) {
        if (!member || member == roomMaster.get() || !member->GetPM_WindowOpen()) {
            continue;
        }
        CSPointer<User> memberPointer(member, ObjectRefType::FunctionChain);
        members.push_back(CRoomMemberFactory::MakePartyMember(std::move(memberPointer)));
    }
}

void CPartyRoom::GetPartyWaitingMember(RoomMemberList& members) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);
    for (const CSPointer<CRoomMember>& member : m_waitingMemberList) {
        members.push_back(member);
    }
}

void CPartyRoom::GetMemberList(RoomMemberList& members) {
    L2_GUARD_FUNCTION();
    GetPartyMaster(members);
    GetPartyMember(members);
    GetPartyWaitingMember(members);
}

void CPartyRoom::GetMemberListWithPM_WindowOpen(RoomMemberList& members) {
    L2_GUARD_FUNCTION();
    GetPartyMaster(members);
    GetPartyMemberWithPM_WindowOpen(members);
    GetPartyWaitingMember(members);
}

bool CPartyRoom::IsMPCCRRoom() { return false; }

CMPCCRoom* CPartyRoom::CastCMPCCRoom() { return nullptr; }

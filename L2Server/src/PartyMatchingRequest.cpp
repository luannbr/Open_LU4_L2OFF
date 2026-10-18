#include "PartyMatchingRequest.h"
#include "CursedWeapon.h"
#include "MultiPartyCommandChannel.h"
#include "MultiPartyCommandChannelRegistry.h"
#include "Party.h"
#include "PartyMatchingResponse.h"
#include "PartyMatchingWaitingRoom.h"
#include "PartyRoomMember.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"

#include "Shared/src/IOC.h"
#include "Shared/src/LogFormatter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <utility>

namespace {

std::u16string FilterChatLogText(const char16_t* message) {
    std::u16string filtered;
    if (!message) {
        return filtered;
    }

    filtered.reserve(std::min<std::size_t>(target_utf16::Length(message), 1027));
    for (std::size_t index = 0; message[index] != u'\0' && filtered.size() < 1027; ++index) {
        char16_t value = message[index];
        if (value == u',') {
            filtered.push_back(u' ');
            continue;
        }
        if (value != u'\b') {
            filtered.push_back(value);
            continue;
        }

        bool copy = false;
        while (message[++index] != u'\0' && message[index] != u'\b') {
            if (message[index] == u'\x1b') {
                copy = !copy;
            } else if (copy && filtered.size() < 1027) {
                filtered.push_back(message[index]);
            }
        }
        if (message[index] == u'\0') {
            break;
        }
    }
    return filtered;
}

void WritePartyMatchingChatLog(User& user, const char16_t* message, int type) {
    const FVector position = user.GetPos();
    const std::u16string filtered = FilterChatLogText(message);
    CLogFormatter formatter;
    // formatter.WriteChatLog(
    //     type,
    //     static_cast<std::int64_t>(user.GetDBID()),
    //     0,
    //     static_cast<std::int64_t>(position.x),
    //     static_cast<std::int64_t>(position.y),
    //     static_cast<std::int64_t>(position.z),
    //     const_cast<wchar_t*>(filtered.c_str()),
    //     const_cast<wchar_t*>(user.GetName()),
    //     const_cast<wchar_t*>(L""))
    formatter.WriteChatLog(
        type, static_cast<std::int64_t>(user.GetDBID()), 0, static_cast<std::int64_t>(position.x),
        static_cast<std::int64_t>(position.y), static_cast<std::int64_t>(position.z), filtered, user.GetName(),
        u""
    );
}

} // namespace

// Exact current helper at 00937f34/00937fac. It writes the
// completion timestamp in the destructor so every exit path is throttled in
// the same way as the target.
class UserRequestTimeSetter {
public:
    UserRequestTimeSetter(CSPointer<User> user, int flag) : m_user(std::move(user)), m_flag(flag) {}

    virtual ~UserRequestTimeSetter() {
        if (!m_user) {
            return;
        }
        const auto updateRequestTime = [&] {
            m_user->m_currentRequestTime = runtime::GetTickCount();
            m_user->m_currentRequestTimeFlag = m_flag;
        };
        if (CYieldLock* const lock = m_user->m_lock) {
            L2_GUARD(*lock);
            updateRequestTime();
        } else {
            updateRequestTime();
        }
    }

private:
    CSPointer<User> m_user;
    int m_flag;
    [[maybe_unused]] std::byte m_padding1c[4]{};
};


CPartyMatchingRequest::CPartyMatchingRequest() = default;
CPartyMatchingRequest::~CPartyMatchingRequest() = default;

bool CPartyMatchingRequest::CreateRoom(CPartyRoomInfo& roomInfo) {
    L2_GUARD_FUNCTION();
    return roomInfo.CreateRoom(*this);
}

bool CPartyMatchingRequest::ManageRoom(CPartyRoomInfo& roomInfo) {
    L2_GUARD_FUNCTION();
    if (!roomInfo.IsCorrectLevelLimit()) {
        return false;
    }
    return roomInfo.IsForCreatingNewPartyRoom() ? CreateRoom(roomInfo) : UpdateRoom(roomInfo);
}

bool CPartyMatchingRequest::UpdateRoom(CPartyRoomInfo& roomInfo) {
    L2_GUARD_FUNCTION();
    CPartyRoomRegistry& registry = roomInfo.IsCMPCCRoomInfo() ? static_cast<CPartyRoomRegistry&>(CMPCCRoomRegistry::inst())
                                                              : CPartyRoomRegistry::inst();
    CSPointer<CPartyRoom> room = registry.FindById(roomInfo.GetId());
    if (!room || !room->UpdateInfo(roomInfo)) {
        return false;
    }
    ResponseOnUpdateRoom(std::move(room));
    return true;
}

void CPartyMatchingRequest::ResponseOnSystemMessage(CSPointer<User> user, int messageNo) {
    L2_GUARD_FUNCTION();
    CPartyMatchingResponse response(std::move(user));
    CPartyRoomSystemMessageResponse component(messageNo);
    response.Respond(component);
}

void CPartyMatchingRequest::ResponseOnPartyMatchingWindowStatusChange(CSPointer<User> user, bool open) {
    L2_GUARD_FUNCTION();
    CPartyMatchingResponse response(std::move(user));
    CPartyMatchingWindowsStatusResponse component(open);
    response.Respond(component);
}

void CPartyMatchingRequest::ResponseOnListRoom(CPartyRoomListFilter& filter, CPartyRoomRegistry::PartyRoomList& rooms) {
    L2_GUARD_FUNCTION();
    CPartyMatchingResponse response(filter.GetUser());
    CPartyRoomListResponse component(filter, rooms);
    response.Respond(component);
}

void CPartyMatchingRequest::ResponseOnCreateRoom(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    if (!room) {
        return;
    }

    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    CSPointer<User> master = room->GetPartyRoomInfo().GetMasterUser();
    if (!master) {
        return;
    }
    g_PartyMatchingWaitingRoom.LeaveWaitingRoom(master.get());

    CPartyMatchingResponse response(master);
    CPartyRoomInfoResponse info(room->GetPartyRoomInfo());
    CPartyRoomMemberListResponse memberList(members, room->IsMPCCRRoom());
    CPartyRoomSystemMessageResponse message(
        room->IsMPCCRRoom() ? SystemMessageId::COMMAND_CHANNEL_MATCHING_ROOM_CREATED : SystemMessageId::PARTY_ROOM_CREATED
    );
    CPartyMatchingWindowsStatusResponse window(true);
    CPartyRoomCreatedLogger logger(room);
    response.Respond(info);
    response.Respond(memberList);
    response.Respond(message);
    response.Respond(window);
    response.Respond(logger);
}

void CPartyMatchingRequest::ResponseOnUpdateRoom(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    if (!room) {
        return;
    }
    CPartyRoom::RoomMemberList members;
    room->GetMemberListWithPM_WindowOpen(members);
    CPartyMatchingResponseToAllMember response(members);
    CPartyRoomInfoResponse info(room->GetPartyRoomInfo());
    CPartyRoomSystemMessageResponse message(
        room->IsMPCCRRoom() ? SystemMessageId::COMMAND_CHANNEL_MATCHING_ROOM_INFORMATION_UPDATED
                            : SystemMessageId::PARTY_ROOM_INFORMATION_UPDATED
    );
    response.Respond(info);
    response.Respond(message);
}

void CPartyMatchingRequest::ResponseOnMemberJoin(CSPointer<CPartyRoom> room, CSPointer<CRoomMember> member) {
    L2_GUARD_FUNCTION();
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    {
        GuardHelper responseGuard(L"ResponseOnMemberJoin_1", CThreadManager::GetThreadID());
        g_PartyMatchingWaitingRoom.LeaveWaitingRoom(member->GetUser().get());
        CPartyMatchingResponse joined(member->GetUser());
        CPartyRoomInfoResponse info(room->GetPartyRoomInfo());
        joined.Respond(info);
        CPartyRoomMemberListResponse memberList(members, room->IsMPCCRRoom());
        joined.Respond(memberList);
        CPartyMatchingWindowsStatusResponse window(true);
        joined.Respond(window);
    }
    {
        GuardHelper responseGuard(L"ResponseOnMemberJoin_2", CThreadManager::GetThreadID());
        CPartyMatchingResponseToAllMember all(members);
        CPartyRoomJoinedMemberResponse joinedMember(member);
        all.Respond(joinedMember);
    }
}

void CPartyMatchingRequest::ResponseOnMemberDirectJoin(CSPointer<CPartyRoom> room, CSPointer<CRoomMember> member) {
    L2_GUARD_FUNCTION();
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    CPartyMatchingResponseToAllMember all(members);
    CPartyRoomJoinedMemberResponse joined(member);
    all.Respond(joined);
}

void CPartyMatchingRequest::ResponseOnWithdraw(CSPointer<CPartyRoom> room, CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!room || !user) {
        return;
    }
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);

    CPartyMatchingResponse withdrawn(user);
    CPartyRoomDismissedResponse dismissed(room->IsMPCCRRoom());
    CPartyMatchingWindowsStatusResponse window(false);
    withdrawn.Respond(dismissed);
    withdrawn.Respond(window);

    CPartyMatchingResponseToAllMember all(members);
    CPartyRoomWithdrawedMemberResponse component(std::move(user), room->IsMPCCRRoom());
    all.Respond(component);
}

void CPartyMatchingRequest::ResponseOnSay(CSPointer<CPartyRoom> room, CSPointer<User> user, const char16_t* message) {
    L2_GUARD_FUNCTION();
    if (!room || !user) {
        return;
    }
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    CPartyMatchingResponseToAllMember all(members);
    if (room->IsMPCCRRoom()) {
        CMPCCRoomChatResponse component(std::move(user), message);
        all.Respond(component);
    } else {
        CPartyRoomChatResponse component(std::move(user), message);
        all.Respond(component);
    }
}

void CPartyMatchingRequest::ResponseOnMemberStateChange(CSPointer<CRoomMember> member, CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    if (!member || !room) {
        return;
    }
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    CSPointer<User> changedUser = member->GetUser();
    if (changedUser) {
        CPartyMatchingResponse changed(changedUser);
        CPartyRoomInfoResponse info(room->GetPartyRoomInfo());
        CPartyRoomMemberListResponse memberList(members, room->IsMPCCRRoom());
        changed.Respond(info);
        changed.Respond(memberList);
    }
    CPartyMatchingResponseToAllMember all(members);
    CPartyRoomMemberStateChangedResponse state(std::move(member));
    all.Respond(state);
}

void CPartyMatchingRequest::ResponseOnSystemMessageToAllMember(CSPointer<CPartyRoom> room, int messageNo) {
    L2_GUARD_FUNCTION();
    if (!room) {
        return;
    }
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    CPartyMatchingResponseToAllMember all(members);
    CPartyRoomSystemMessageResponse message(messageNo);
    all.Respond(message);
}

void CPartyMatchingRequest::ResponseOnDismissParty(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    if (!room) {
        return;
    }
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    CPartyMatchingResponseToAllMember all(members);
    CPartyRoomInfoResponse info(room->GetPartyRoomInfo());
    CPartyRoomMemberListResponse list(members, room->IsMPCCRRoom());
    CPartyMatchingWindowsStatusResponse window(true);
    all.Respond(info);
    all.Respond(list);
    all.Respond(window);
}

void CPartyMatchingRequest::ResponseOnDismissRoom(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    if (!room) {
        return;
    }
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    CPartyMatchingResponseToAllMember all(members);
    CPartyRoomDismissedResponse dismissed(room->IsMPCCRRoom());
    CPartyRoomSystemMessageResponse message(
        room->IsMPCCRRoom() ? SystemMessageId::COMMAND_CHANNEL_MATCHING_ROOM_DISBANDED
                            : SystemMessageId::PARTY_ROOM_DISBANDED
    );
    CPartyMatchingWindowsStatusResponse window(false);
    CPartyRoomDeletedLogger logger(room);
    all.Respond(dismissed);
    all.Respond(message);
    all.Respond(window);
    all.Respond(logger);
}

void CPartyMatchingRequest::ResponseOnChangeLocation(CSPointer<CPartyRoom> room, CSPointer<CRoomMember> member) {
    L2_GUARD_FUNCTION();
    if (!room || !member) {
        return;
    }
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    CPartyMatchingResponseToAllMember all(members);
    CPartyRoomMemberStateChangedResponse state(std::move(member));
    all.Respond(state);
}

void CPartyMatchingRequest::ResponseOnInspect(CSPointer<CPartyRoom> room, CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!room || !user) {
        return;
    }
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    CPartyMatchingResponse response(std::move(user));
    CPartyRoomInspecter inspecter(members);
    response.Respond(inspecter);
}

void CPartyMatchingRequest::ResponseOnViewRoom(CSPointer<CPartyRoom> room, CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!room || !user) {
        return;
    }
    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    CPartyMatchingResponse response(std::move(user));
    CPartyRoomInfoResponse info(room->GetPartyRoomInfo());
    CPartyRoomMemberListResponse memberList(members, room->IsMPCCRRoom());
    CPartyMatchingWindowsStatusResponse window(true);
    response.Respond(info);
    response.Respond(memberList);
    response.Respond(window);
}

void CPartyMatchingRequest::ResponseOnListPartyMaster(CSPointer<CPartyRoom> room, CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!room || !user) {
        return;
    }
    CPartyRoom::RoomMemberList members;
    room->GetPartyMasterList(members);
    CPartyMatchingResponse response(std::move(user));
    CPartyMPCCPartyMasterListResponse list(members);
    response.Respond(list);
}

CSPointer<CPartyRoom> CPartyMatchingRequest::JoinRoomInternal(
    CSPointer<User> user, int roomId, CPartyRoomListFilter& filter
) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return {};
    }

    L2_WRITE_GUARD(user->m_partyMatchingLock);
    CPartyRoomRegistry& registry =
        filter.IsMPCCRoomFilter() ? static_cast<CPartyRoomRegistry&>(CMPCCRoomRegistry::inst()) : CPartyRoomRegistry::inst();
    if (registry.IsPartyRoomMember(user)) {
        return {};
    }

    CSPointer<CPartyRoom> room = roomId == 0 ? registry.FindByAuto(user, filter) : registry.FindById(roomId);
    if (!room || !room->Join(user)) {
        return {};
    }
    registry.RegisterPartyRoomMember(user, room);
    return room;
}

bool CPartyMatchingRequest::JoinRoom(CSPointer<User> user, int roomId, CPartyRoomListFilter& filter) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    if (user->m_currentRequestTimeFlag == 0 && runtime::GetTickCount() - user->m_currentRequestTime < 3000U) {
        return false;
    }

    UserRequestTimeSetter requestTime(user, 0);
    if (!filter.IsMPCCRoomFilter()) {
        if (user->GetParty()) {
            ResponseOnSystemMessage(user, SystemMessageId::CANNOT_ENTER_PARTY_ROOM);
            return false;
        }
        CSPointer<CPartyRoom> room = JoinRoomInternal(user, roomId, filter);
        if (!room) {
            ResponseOnSystemMessage(user, SystemMessageId::CANNOT_ENTER_PARTY_ROOM_REQUIREMENTS_NOT_MET);
            return false;
        }
        ResponseOnMemberJoin(room, CRoomMemberFactory::MakePartyWaitingMember(user));
        return true;
    }

    CParty* const party = user->GetParty();
    if (party && (!user->IsPartyMaster() || user->IsMPCCPartyMaster())) {
        ResponseOnSystemMessage(user, SystemMessageId::CANNOT_ENTER_PARTY_ROOM);
        return false;
    }
    CSPointer<CPartyRoom> room = JoinRoomInternal(user, roomId, filter);
    if (!room) {
        ResponseOnSystemMessage(user, SystemMessageId::CANNOT_ENTER_COMMAND_CHANNEL_MATCHING_ROOM_REQUIREMENTS_NOT_MET);
        return false;
    }
    ResponseOnMemberJoin(
        room, party ? CRoomMemberFactory::MakeMPCCPartyMaster(user) : CRoomMemberFactory::MakeMPCCIndividual(user)
    );
    return true;
}

bool CPartyMatchingRequest::WithdrawRoomInternal(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    CPartyRoomRegistry& registry = CPartyRoomRegistry::inst();
    CSPointer<CPartyRoom> room = registry.FindByMember(user);
    if (!room || !room->Withdraw(user)) {
        return false;
    }
    ResponseOnWithdraw(room, user);
    registry.UnregisterPartyRoomMember(user);
    return true;
}

bool CPartyMatchingRequest::WithdrawMPCCRoomInternal(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    CPartyRoomRegistry& registry = CMPCCRoomRegistry::inst();
    CSPointer<CPartyRoom> room = registry.FindByMember(user);
    if (!room || !room->Withdraw(user)) {
        return false;
    }
    ResponseOnWithdraw(room, user);
    registry.UnregisterPartyRoomMember(user);
    return true;
}

bool CPartyMatchingRequest::OustFromRoom(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    CPartyRoomRegistry& registry = CPartyRoomRegistry::inst();
    if (registry.IsPartyRoomMaster(user)) {
        return false;
    }
    if (user->GetParty() && registry.IsPartyRoomMember(user)) {
        CSPointer<CPartyRoom> room = registry.FindByMember(user);
        if (room) {
            ResponseOnSystemMessage(
                room->GetPartyRoomInfo().GetMasterUser(), SystemMessageId::CANNOT_FORCE_DISMISS_PARTY_MEMBER
            );
        }
        return false;
    }
    if (!WithdrawRoomInternal(user)) {
        return false;
    }
    ResponseOnSystemMessage(user, SystemMessageId::EXPELLED_FROM_PARTY_ROOM);
    return true;
}

bool CPartyMatchingRequest::OustFromMPCCRoom(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    CPartyRoomRegistry& registry = CMPCCRoomRegistry::inst();
    if (registry.IsPartyRoomMaster(user)) {
        return false;
    }
    if (CMultiPartyCommandChannelRegistry::IsMPCCMemberPartyMaster(user.get()) && registry.IsPartyRoomMember(user)) {
        CSPointer<CPartyRoom> room = registry.FindByMember(user);
        if (room) {
            ResponseOnSystemMessage(
                room->GetPartyRoomInfo().GetMasterUser(), SystemMessageId::CANNOT_FORCE_DISMISS_PARTY_MEMBER
            );
        }
        return false;
    }
    if (!WithdrawMPCCRoomInternal(user)) {
        return false;
    }
    ResponseOnSystemMessage(user, SystemMessageId::EXPELLED_FROM_COMMAND_CHANNEL_MATCHING_ROOM);
    return true;
}

bool CPartyMatchingRequest::SayInRoom(CSPointer<User> user, const char16_t* message) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    CSPointer<CPartyRoom> room = CPartyRoomRegistry::inst().FindByMember(user);
    if (!room || !room->IsPartyRoomMember(user)) {
        return false;
    }
    WritePartyMatchingChatLog(*user.get(), message, 0x0e);
    ResponseOnSay(room, user, message);
    return true;
}

bool CPartyMatchingRequest::SayInMPCCRoom(CSPointer<User> user, const char16_t* message) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    CSPointer<CPartyRoom> room = CMPCCRoomRegistry::inst().FindByMember(user);
    if (!room || !room->IsPartyRoomMember(user)) {
        return false;
    }
    WritePartyMatchingChatLog(*user.get(), message, 0x15);
    ResponseOnSay(room, user, message);
    return true;
}

bool CPartyMatchingRequest::OnJoinParty(CSPointer<User> partyMaster, CSPointer<User> joinedUser) {
    L2_GUARD_FUNCTION();
    if (!partyMaster || !joinedUser) {
        return false;
    }
    CPartyRoomRegistry& registry = CPartyRoomRegistry::inst();
    CSPointer<CPartyRoom> room = registry.FindByMaster(partyMaster);
    if (!room) {
        WithdrawRoomInternal(partyMaster);
        WithdrawRoomInternal(joinedUser);
        return false;
    }
    if (!room->IsPartyWaitingMember(joinedUser)) {
        WithdrawRoomInternal(joinedUser);
        registry.RegisterPartyRoomMember(joinedUser, room);
        ResponseOnMemberDirectJoin(room, CRoomMemberFactory::MakePartyMember(joinedUser));
        return true;
    }
    if (!room->Withdraw(joinedUser)) {
        return false;
    }
    ResponseOnMemberStateChange(CRoomMemberFactory::MakePartyMember(joinedUser), room);
    return true;
}

bool CPartyMatchingRequest::OnJoinMPCC(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user || !user->IsPartyMaster()) {
        return false;
    }
    CMultiPartyCommandChannel* const channel = user->GetMPCC();
    User* const channelMaster = channel ? channel->GetMaster() : nullptr;
    if (!channelMaster) {
        return false;
    }
    CSPointer<User> master(channelMaster, ObjectRefType::FunctionChain);
    CPartyRoomRegistry& registry = CMPCCRoomRegistry::inst();
    CSPointer<CPartyRoom> masterRoom = registry.FindByMaster(master);
    CSPointer<CPartyRoom> currentRoom = registry.FindByMember(user);
    if (!masterRoom || !currentRoom) {
        return false;
    }
    if (masterRoom.get() == currentRoom.get()) {
        ResponseOnMemberStateChange(CRoomMemberFactory::MakeMPCCMemberPartyMaster(user), masterRoom);
        return true;
    }
    WithdrawMPCCRoomInternal(user);
    registry.RegisterPartyRoomMember(user, masterRoom);
    ResponseOnMemberDirectJoin(masterRoom, CRoomMemberFactory::MakeMPCCMemberPartyMaster(user));
    return true;
}

bool CPartyMatchingRequest::OnWithdrawParty(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    return WithdrawRoomInternal(std::move(user));
}

bool CPartyMatchingRequest::OnWithdrawMPCC(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    return WithdrawMPCCRoomInternal(std::move(user));
}

bool CPartyMatchingRequest::OnDismissPartyInternal(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    if (!room || !room->DismissParty()) {
        return false;
    }
    ResponseOnDismissParty(room);
    room->SetPartyDismissed(false);
    return true;
}

bool CPartyMatchingRequest::OnDismissParty(CParty* party) {
    L2_GUARD_FUNCTION();
    if (!party) {
        return false;
    }
    CPartyRoomRegistry& registry = CPartyRoomRegistry::inst();
    if (User* const master = party->GetMaster()) {
        CSPointer<User> masterUser(master, ObjectRefType::FunctionChain);
        return OnDismissPartyInternal(registry.FindByMaster(std::move(masterUser)));
    }

    std::vector<User*> users;
    party->GetAllMember(users);
    for (User* const user : users) {
        if (!user) {
            continue;
        }
        CSPointer<User> member(user, ObjectRefType::FunctionChain);
        CSPointer<CPartyRoom> room = registry.FindByMaster(member);
        if (room) {
            return OnDismissPartyInternal(std::move(room));
        }
    }
    return false;
}

bool CPartyMatchingRequest::OnChangeLocation(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    CSPointer<CPartyRoom> room = CPartyRoomRegistry::inst().FindByMember(user);
    if (!room) {
        return false;
    }
    CSPointer<CRoomMember> member = room->GetMember(user);
    if (!member) {
        return false;
    }
    ResponseOnChangeLocation(room, std::move(member));
    return true;
}

void CPartyMatchingRequest::InspectPartyRoom(CSPointer<User> user, int roomId) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }
    CSPointer<CPartyRoom> room = CPartyRoomRegistry::inst().FindById(roomId);
    if (room) {
        ResponseOnInspect(std::move(room), std::move(user));
    }
}

bool CPartyMatchingRequest::DismissRoomInternal(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    if (!room) {
        return false;
    }
    ResponseOnDismissRoom(room);
    CPartyRoomRegistry::inst().Unregister(std::move(room));
    return true;
}

bool CPartyMatchingRequest::DismissMPCCRoomInternal(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    if (!room) {
        return false;
    }
    ResponseOnDismissRoom(room);
    CMPCCRoomRegistry::inst().Unregister(std::move(room));
    return true;
}

void CPartyMatchingRequest::OnListPartyMaster(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }
    CSPointer<CPartyRoom> room = CMPCCRoomRegistry::inst().FindByMember(user);
    if (room) {
        ResponseOnListPartyMaster(std::move(room), std::move(user));
    }
}

bool CPartyMatchingRequest::WithdrawRoom(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    CPartyRoomRegistry& registry = CPartyRoomRegistry::inst();
    if (registry.IsPartyRoomMaster(user)) {
        return false;
    }
    if (user->GetParty() && registry.IsPartyRoomMember(user)) {
        ResponseOnPartyMatchingWindowStatusChange(user, false);
        return true;
    }
    if (!WithdrawRoomInternal(user)) {
        return false;
    }
    ResponseOnSystemMessage(user, SystemMessageId::LEFT_PARTY_ROOM);
    return true;
}

bool CPartyMatchingRequest::DismissRoomOnly(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    CPartyRoomRegistry& registry = CPartyRoomRegistry::inst();
    CSPointer<CPartyRoom> room = registry.FindByMaster(user);
    return room && room->IsPartyRoomMaster(user) ? DismissRoomInternal(std::move(room)) : false;
}

bool CPartyMatchingRequest::DismissMPCCRoomOnly(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    CPartyRoomRegistry& registry = CMPCCRoomRegistry::inst();
    CSPointer<CPartyRoom> room = registry.FindByMaster(user);
    return room && room->IsPartyRoomMaster(user) ? DismissMPCCRoomInternal(std::move(room)) : false;
}

bool CPartyMatchingRequest::ListRoom(CPartyRoomListFilter& filter) {
    L2_GUARD_FUNCTION();
    (void)CCursedWeaponMgr::inst();
    CSPointer<User> user = filter.GetUser();
    if (!user || user->m_nCursedWeaponClassId > 0) {
        return false;
    }
    if (user->m_currentRequestTimeFlag == 1 && runtime::GetTickCount() - user->m_currentRequestTime < 1000U) {
        return false;
    }
    UserRequestTimeSetter requestTime(user, 1);

    CPartyRoomRegistry& normalRegistry = CPartyRoomRegistry::inst();
    CSPointer<CPartyRoom> room = normalRegistry.FindByMember(user);
    if (room) {
        const bool allowed = room->IsPartyRoomMaster(user) || (user->GetParty() && room->IsPartyRoomMember(user));
        if (!allowed) {
            ResponseOnSystemMessage(user, SystemMessageId::PARTY_ROOM_LIST_REQUIRES_NO_PARTY_OR_PARTY_LEADERSHIP);
            return false;
        }
        ResponseOnViewRoom(std::move(room), std::move(user));
        return true;
    }

    CPartyRoomRegistry& mpccRegistry = CMPCCRoomRegistry::inst();
    room = mpccRegistry.FindByMember(user);
    if (room) {
        const bool allowed = room->IsPartyRoomMaster(user) || (user->IsPartyMaster() && room->IsPartyMember(user));
        if (!allowed) {
            ResponseOnSystemMessage(user, SystemMessageId::PARTY_ROOM_LIST_REQUIRES_NO_PARTY_OR_PARTY_LEADERSHIP);
            return false;
        }
        ResponseOnViewRoom(std::move(room), std::move(user));
        return true;
    }

    if (CMultiPartyCommandChannelRegistry::IsMPCCMaster(user.get())) {
        CSPointer<CMultiPartyCommandChannel> channel = CMultiPartyCommandChannelRegistry::FindByMember(user.get());
        User* const master = channel ? channel->GetMaster() : nullptr;
        CParty* const party = master ? master->GetParty() : nullptr;
        if (!master || !party) {
            return false;
        }
        CSPointer<User> masterUser(master, ObjectRefType::FunctionChain);
        CMPCCRoomInfo info(0, masterUser, 1, 120, 350, static_cast<int>(party->GetRoutingType()), master->GetName());
        return ManageRoom(info);
    }
    if (CMultiPartyCommandChannelRegistry::IsMPCCPartyMember(user.get())) {
        ResponseOnSystemMessage(user, SystemMessageId::COMMAND_CHANNEL_PARTY_MEMBER_CANNOT_USE_MATCHING_WINDOW);
        return false;
    }
    if (user->GetParty() && !user->IsPartyMaster()) {
        ResponseOnSystemMessage(user, SystemMessageId::PARTY_ROOM_LIST_REQUIRES_NO_PARTY_OR_PARTY_LEADERSHIP);
        return false;
    }

    CPartyRoomRegistry::PartyRoomList rooms;
    CPartyRoomRegistry& registry = filter.IsMPCCRoomFilter() ? mpccRegistry : normalRegistry;
    if (!registry.List(filter, rooms)) {
        return false;
    }
    if (filter.GetPage() > 1 && rooms.empty()) {
        filter.SetPage(filter.GetPage() - 1);
        registry.List(filter, rooms);
    }
    if (!filter.IsMPCCRoomFilter()) {
        g_PartyMatchingWaitingRoom.EnterWaitingRoom(user.get());
    }
    ResponseOnListRoom(filter, rooms);
    return true;
}

bool CPartyMatchingRequest::DismissRoom(int roomId) {
    L2_GUARD_FUNCTION();
    return DismissRoomInternal(CPartyRoomRegistry::inst().FindById(roomId));
}

bool CPartyMatchingRequest::DismissMPCCRoom(int roomId) {
    L2_GUARD_FUNCTION();
    return DismissMPCCRoomInternal(CMPCCRoomRegistry::inst().FindById(roomId));
}

bool CPartyMatchingRequest::DismissRoom(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!DismissRoomOnly(user)) {
        return false;
    }
    CPartyRoomListFilter filter(user, 1, ALL_AREA, ALL_LEVEL, false);
    return ListRoom(filter);
}

bool CPartyMatchingRequest::DismissMPCCRoom(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    return DismissMPCCRoomOnly(std::move(user));
}

bool CPartyMatchingRequest::OnLeaveWorld(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    if (user->IsMPCCMaster()) {
        return DismissMPCCRoom(user);
    }
    if (user->IsMPCCPartyMaster()) {
        return WithdrawMPCCRoomInternal(user);
    }
    if (CMPCCRoomRegistry::inst().IsPartyRoomMember(user)) {
        return WithdrawMPCCRoomInternal(user);
    }
    if (DismissRoomOnly(user)) {
        return true;
    }
    return WithdrawRoomInternal(std::move(user));
}

bool CPartyMatchingRequest::OnReplacePartyMaster(CSPointer<User> oldMaster, CSPointer<User> newMaster) {
    L2_GUARD_FUNCTION();
    if (!oldMaster || !newMaster) {
        return false;
    }
    bool changed = false;
    CPartyRoomRegistry& registry = CPartyRoomRegistry::inst();
    CSPointer<CPartyRoom> room = registry.FindByMaster(oldMaster);
    if (room && room->ReplaceMaster(newMaster)) {
        registry.Replace(oldMaster, room);
        ResponseOnMemberStateChange(CRoomMemberFactory::MakePartyMember(oldMaster), room);
        ResponseOnMemberStateChange(CRoomMemberFactory::MakePartyRoomMaster(newMaster), room);
        ResponseOnPartyMatchingWindowStatusChange(newMaster, true);
        ResponseOnSystemMessageToAllMember(room, SystemMessageId::PARTY_ROOM_LEADER_CHANGED);
        changed = true;
    }

    CSPointer<CPartyRoom> mpccRoom = CMPCCRoomRegistry::inst().FindByMember(oldMaster);
    if (mpccRoom) {
        DismissMPCCRoom(mpccRoom->GetPartyRoomInfo().GetId());
        changed = true;
    }
    return changed;
}

bool CPartyMatchingRequest::WithdrawMPCCRoom(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }
    CPartyRoomRegistry& registry = CMPCCRoomRegistry::inst();
    if (registry.IsPartyRoomMaster(user)) {
        DismissMPCCRoom(user);
        return true;
    }
    if (CMultiPartyCommandChannelRegistry::IsMPCCMemberPartyMaster(user.get()) && registry.IsPartyRoomMember(user)) {
        ResponseOnPartyMatchingWindowStatusChange(user, false);
        return true;
    }
    if (!WithdrawMPCCRoomInternal(user)) {
        return false;
    }
    ResponseOnSystemMessage(user, SystemMessageId::LEFT_COMMAND_CHANNEL_MATCHING_ROOM);
    return true;
}

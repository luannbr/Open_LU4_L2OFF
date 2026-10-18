#pragma once

#include "PartyRoomInfo.h"
#include "PartyRoomListFilter.h"
#include "PartyRoomRegistry.h"

#include "Shared/src/TemplateHelper.h"

#include <cstddef>

class CParty;
class CRoomMember;
class User;

// Exact current PartyMatchingRequest.cpp singleton owner. The class has no
// state beyond Singleton's primary vptr. All mutable state belongs to User,
// the room registries, and the room objects themselves.
class CPartyMatchingRequest final : public Singleton<CPartyMatchingRequest> {
public:
    CPartyMatchingRequest();
    ~CPartyMatchingRequest() override;

    bool CreateRoom(CPartyRoomInfo& roomInfo);
    bool ManageRoom(CPartyRoomInfo& roomInfo);
    bool UpdateRoom(CPartyRoomInfo& roomInfo);

    bool JoinRoom(CSPointer<User> user, int roomId, CPartyRoomListFilter& filter);
    bool WithdrawRoom(CSPointer<User> user);
    bool WithdrawMPCCRoom(CSPointer<User> user);
    bool OustFromRoom(CSPointer<User> user);
    bool OustFromMPCCRoom(CSPointer<User> user);
    bool SayInRoom(CSPointer<User> user, const char16_t* message);
    bool SayInMPCCRoom(CSPointer<User> user, const char16_t* message);

    bool OnJoinParty(CSPointer<User> user, CSPointer<User> partyMaster);
    bool OnJoinMPCC(CSPointer<User> user);
    bool OnWithdrawParty(CSPointer<User> user);
    bool OnWithdrawMPCC(CSPointer<User> user);
    bool OnDismissParty(CParty* party);
    bool OnChangeLocation(CSPointer<User> user);
    void InspectPartyRoom(CSPointer<User> user, int roomId);
    void OnListPartyMaster(CSPointer<User> user);
    bool OnLeaveWorld(CSPointer<User> user);
    bool OnReplacePartyMaster(CSPointer<User> oldMaster, CSPointer<User> newMaster);

    bool ListRoom(CPartyRoomListFilter& filter);
    bool DismissRoom(int roomId);
    bool DismissMPCCRoom(int roomId);
    bool DismissRoom(CSPointer<User> user);
    bool DismissMPCCRoom(CSPointer<User> user);
    bool DismissRoomOnly(CSPointer<User> user);
    bool DismissMPCCRoomOnly(CSPointer<User> user);

    void ResponseOnSystemMessage(CSPointer<User> user, int messageNo);
    void ResponseOnPartyMatchingWindowStatusChange(CSPointer<User> user, bool open);
    void ResponseOnListRoom(CPartyRoomListFilter& filter, CPartyRoomRegistry::PartyRoomList& rooms);
    void ResponseOnCreateRoom(CSPointer<CPartyRoom> room);
    void ResponseOnUpdateRoom(CSPointer<CPartyRoom> room);
    void ResponseOnMemberJoin(CSPointer<CPartyRoom> room, CSPointer<CRoomMember> member);
    void ResponseOnMemberDirectJoin(CSPointer<CPartyRoom> room, CSPointer<CRoomMember> member);
    void ResponseOnWithdraw(CSPointer<CPartyRoom> room, CSPointer<User> user);
    void ResponseOnSay(CSPointer<CPartyRoom> room, CSPointer<User> user, const char16_t* message);
    void ResponseOnMemberStateChange(CSPointer<CRoomMember> member, CSPointer<CPartyRoom> room);
    void ResponseOnSystemMessageToAllMember(CSPointer<CPartyRoom> room, int messageNo);
    void ResponseOnDismissParty(CSPointer<CPartyRoom> room);
    void ResponseOnDismissRoom(CSPointer<CPartyRoom> room);
    void ResponseOnChangeLocation(CSPointer<CPartyRoom> room, CSPointer<CRoomMember> member);
    void ResponseOnInspect(CSPointer<CPartyRoom> room, CSPointer<User> user);
    void ResponseOnViewRoom(CSPointer<CPartyRoom> room, CSPointer<User> user);
    void ResponseOnListPartyMaster(CSPointer<CPartyRoom> room, CSPointer<User> user);

private:
    CSPointer<CPartyRoom> JoinRoomInternal(CSPointer<User> user, int roomId, CPartyRoomListFilter& filter);
    bool WithdrawRoomInternal(CSPointer<User> user);
    bool WithdrawMPCCRoomInternal(CSPointer<User> user);
    bool DismissRoomInternal(CSPointer<CPartyRoom> room);
    bool DismissMPCCRoomInternal(CSPointer<CPartyRoom> room);
    bool OnDismissPartyInternal(CSPointer<CPartyRoom> room);
};

#pragma once

#include "PartyRoom.h"
#include "PartyRoomListFilter.h"
#include "PartyRoomRegistry.h"

#include "Shared/src/PacketHelper.h"

#include <cstddef>

class User;

class CPartyMatchingResponseComponent {
public:
    virtual bool Respond(CSPointer<User> user) = 0;
    virtual ~CPartyMatchingResponseComponent();
};

class CPartyMatchingResponse {
public:
    explicit CPartyMatchingResponse(CSPointer<User> user);
    void Respond(CPartyMatchingResponseComponent& component);

private:
    CSPointer<User> m_user;
};

class CPartyMatchingResponseToAllMember {
public:
    explicit CPartyMatchingResponseToAllMember(CPartyRoom::RoomMemberList& members);
    void Respond(CPartyMatchingResponseComponent& component);

private:
    CPartyRoom::RoomMemberList& m_memberList;
};

class CPartyRoomInfoResponse final : public CPartyMatchingResponseComponent {
public:
    explicit CPartyRoomInfoResponse(CPartyRoomInfo& info);
    bool Respond(CSPointer<User> user) override;

private:
    CPartyRoomInfo& m_info;
};

class CPartyRoomSystemMessageResponse final : public CPartyMatchingResponseComponent {
public:
    explicit CPartyRoomSystemMessageResponse(int messageNo);
    bool Respond(CSPointer<User> user) override;

private:
    int m_messageNo;
};

class CPartyMatchingWindowsStatusResponse final : public CPartyMatchingResponseComponent {
public:
    explicit CPartyMatchingWindowsStatusResponse(bool open);
    bool Respond(CSPointer<User> user) override;

private:
    bool m_open;
};

class CPartyRoomCreatedLogger final : public CPartyMatchingResponseComponent {
public:
    explicit CPartyRoomCreatedLogger(CSPointer<CPartyRoom> room);
    bool Respond(CSPointer<User> user) override;

private:
    CSPointer<CPartyRoom> m_partyRoom;
};

class CPartyRoomDeletedLogger final : public CPartyMatchingResponseComponent {
public:
    explicit CPartyRoomDeletedLogger(CSPointer<CPartyRoom> room);
    bool Respond(CSPointer<User> user) override;

private:
    CSPointer<CPartyRoom> m_partyRoom;
};

class CPartyRoomJoinedMemberResponse final : public CPartyMatchingResponseComponent {
public:
    explicit CPartyRoomJoinedMemberResponse(CSPointer<CRoomMember> joinedUser);
    bool Respond(CSPointer<User> user) override;

private:
    CSPointer<CRoomMember> m_joinedUser;
};

class CPartyRoomDismissedResponse final : public CPartyMatchingResponseComponent {
public:
    explicit CPartyRoomDismissedResponse(bool mpccRoom);
    bool Respond(CSPointer<User> user) override;

private:
    bool m_isMPCCRoom;
};

class CPartyRoomWithdrawedMemberResponse final : public CPartyMatchingResponseComponent {
public:
    CPartyRoomWithdrawedMemberResponse(CSPointer<User> withdrawnUser, bool mpccRoom);
    bool Respond(CSPointer<User> user) override;

private:
    CSPointer<User> m_withdrawedUser;
    bool m_bIsMPCCRoom;
};

class CPartyRoomChatResponse final : public CPartyMatchingResponseComponent {
public:
    CPartyRoomChatResponse(CSPointer<User> talker, const char16_t* message);
    bool Respond(CSPointer<User> user) override;

private:
    CSPointer<User> m_talker;
    const char16_t* m_msg;
};

class CMPCCRoomChatResponse final : public CPartyMatchingResponseComponent {
public:
    CMPCCRoomChatResponse(CSPointer<User> talker, const char16_t* message);
    bool Respond(CSPointer<User> user) override;

private:
    CSPointer<User> m_talker;
    const char16_t* m_msg;
};

class CPartyRoomMemberStateChangedResponse final : public CPartyMatchingResponseComponent {
public:
    explicit CPartyRoomMemberStateChangedResponse(CSPointer<CRoomMember> member);
    bool Respond(CSPointer<User> user) override;

private:
    CSPointer<CRoomMember> m_member;
};

class CPartyRoomInspecter final : public CPartyMatchingResponseComponent {
public:
    explicit CPartyRoomInspecter(CPartyRoom::RoomMemberList& members);
    bool Respond(CSPointer<User> user) override;

private:
    CPartyRoom::RoomMemberList& m_memberList;
};

class CPartyRoomMemberListPacketMaker {
public:
    explicit CPartyRoomMemberListPacketMaker(CPartyRoom::RoomMemberList& members);
    void Make(CPacketHelperEx<20480>& packet);

private:
    CPartyRoom::RoomMemberList& m_memberList;
};

class CPartyRoomListPacketMaker {
public:
    explicit CPartyRoomListPacketMaker(CPartyRoomRegistry::PartyRoomList& rooms);
    void Make(CPacketHelperEx<20480>& packet);

private:
    CPartyRoomRegistry::PartyRoomList& m_roomList;
};

class CPartyMPCCPartyMasterListPacketMaker {
public:
    explicit CPartyMPCCPartyMasterListPacketMaker(CPartyRoom::RoomMemberList& members);
    void Make(CPacketHelperEx<20480>& packet);

private:
    CPartyRoom::RoomMemberList& m_memberList;
};

class CPartyRoomMemberListResponse final : public CPartyMatchingResponseComponent {
public:
    CPartyRoomMemberListResponse(CPartyRoom::RoomMemberList& members, bool mpccRoom);
    bool Respond(CSPointer<User> user) override;

private:
    CSPointer<CRoomMember> GetMember(CSPointer<User> user);

    CPartyRoom::RoomMemberList& m_memberList;
    CPacketHelperEx<20480> m_packetHelper;
    bool m_bIsMPCCRoom;
};

class CPartyRoomListResponse final : public CPartyMatchingResponseComponent {
public:
    CPartyRoomListResponse(CPartyRoomListFilter& filter, CPartyRoomRegistry::PartyRoomList& rooms);
    bool Respond(CSPointer<User> user) override;

private:
    CPartyRoomListFilter& m_filter;
    CPartyRoomRegistry::PartyRoomList& m_partyRoomList;
    CPacketHelperEx<20480> m_packetHelper;
};

class CPartyMPCCPartyMasterListResponse final : public CPartyMatchingResponseComponent {
public:
    explicit CPartyMPCCPartyMasterListResponse(CPartyRoom::RoomMemberList& members);
    bool Respond(CSPointer<User> user) override;

private:
    CPartyRoom::RoomMemberList& m_memberList;
    CPacketHelperEx<20480> m_packetHelper;
};

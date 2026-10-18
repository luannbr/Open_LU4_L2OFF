#pragma once

#include "PartyRoom.h"

#include <cstddef>

class CParty;

// Exact current Cmpccroom.cpp derived-room envelope. Current CPartyRoom is
// 0xd0 and the embedded CMPCCRoomInfo is 0x58, yielding 0x128.
class CMPCCRoom final : public CPartyRoom {
public:
    explicit CMPCCRoom(CMPCCRoomInfo& info);
    ~CMPCCRoom() override;

    bool Join(CSPointer<User> user) override;
    bool UpdateInfo(CPartyRoomInfo& info) override;
    bool CanJoin(CSPointer<User> user) override;
    CPartyRoomInfo& GetPartyRoomInfo() override;
    virtual CMPCCRoom* CastCMPCCRoom();
    virtual bool IsPartyMember(CSPointer<User> user);
    bool IsMPCCRRoom() override;
    void GetPartyMasterList(RoomMemberList& members) override;
    bool WithdrawInternal(CSPointer<User> user) override;


protected:
    void GetPartyMember(RoomMemberList& members) override;
    void GetPartyMemberWithPM_WindowOpen(RoomMemberList& members) override;

private:
    void GetPartyList(std::vector<CParty*>& parties);

    CMPCCRoomInfo m_MPCCRoomInfo;
};

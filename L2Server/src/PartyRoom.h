#pragma once

#include "PartyRoomInfo.h"
#include "PartyRoomMember.h"

#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>

class CMPCCRoom;
class User;

// Exact current L2Server PartyRoom.cpp owner. Current xvector is 0x20,
// which moves the lock to +0x90 and makes the physical class 0xd0 bytes.
class CPartyRoom : public MemoryObject {
public:
    using RoomMemberList = std::vector<CSPointer<CRoomMember>>;
    using PartyRoomMemberIterType = RoomMemberList::iterator;

    L2_DECLARE_OBJECT_REGISTRATION(CPartyRoom)

    CPartyRoom();
    explicit CPartyRoom(CPartyRoomInfo& info);
    ~CPartyRoom() override;

    CPartyRoom(const CPartyRoom&) = delete;
    CPartyRoom& operator=(const CPartyRoom&) = delete;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    virtual bool Join(CSPointer<User> user);
    virtual bool Withdraw(CSPointer<User> user);
    virtual bool DismissParty();
    virtual bool UpdateInfo(CPartyRoomInfo& info);
    bool ReplaceMaster(CSPointer<User> user);
    virtual bool CanJoin(CSPointer<User> user);
    virtual int GetMemberCount();

    void GetMemberList(RoomMemberList& members);
    void GetMemberListWithPM_WindowOpen(RoomMemberList& members);
    CSPointer<CRoomMember> GetMember(CSPointer<User> user);

    virtual CPartyRoomInfo& GetPartyRoomInfo();
    bool IsPartyRoomMember(CSPointer<User> user);
    bool IsPartyRoomMaster(CSPointer<User> user);
    bool IsPartyWaitingMember(CSPointer<User> user);
    bool IsPartyMember(CSPointer<User> user);
    bool CheckAvailableMemberLimit();
    void SetPartyDismissed(bool value);
    CMPCCRoom* CastCMPCCRoom();
    virtual bool IsMPCCRRoom();
    virtual void GetPartyMasterList(RoomMemberList& members);

    virtual bool WithdrawInternal(CSPointer<User> user);


protected:
    bool CheckAvailableLevel(CSPointer<User> user);
    virtual void GetPartyMaster(RoomMemberList& members);
    virtual void GetPartyMember(RoomMemberList& members);
    void GetPartyWaitingMember(RoomMemberList& members);
    virtual void GetPartyMemberWithPM_WindowOpen(RoomMemberList& members);
    bool DoesPartyDismissed() const;

    PartyRoomMemberIterType FindWaitingMember(CSPointer<User> user);

    CPartyRoomInfo m_partyRoomInfo;
    RoomMemberList m_waitingMemberList;
    RWLock m_lock;
    bool m_doesPartyDismissed;
    [[maybe_unused]] std::byte m_alignmentPaddingC9[7];

private:
    inline static const wchar_t* m_pClassName = L"CPartyRoom";
};

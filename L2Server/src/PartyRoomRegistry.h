#pragma once

#include "PartyRoom.h"

#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>

class CPartyRoomListFilter;

class CPartyRoomRegistry {
public:
    using PartyRoomList = std::vector<CSPointer<CPartyRoom>>;
    using PartyRoomMap = ::std::map<int, CSPointer<CPartyRoom>>;
    using PartyRoomIdMap = ::std::map<int, int>;
    using PartyRoomPair = std::pair<int, CSPointer<CPartyRoom>>;

    CPartyRoomRegistry();
    virtual ~CPartyRoomRegistry();

    CPartyRoomRegistry(const CPartyRoomRegistry&) = delete;
    CPartyRoomRegistry& operator=(const CPartyRoomRegistry&) = delete;

    static CPartyRoomRegistry& inst();

    bool Register(CSPointer<CPartyRoom> room);
    bool Replace(CSPointer<User> oldMaster, CSPointer<CPartyRoom> room);
    void Unregister(CSPointer<CPartyRoom> room);
    void RegisterPartyRoomMember(CSPointer<User> user, CSPointer<CPartyRoom> room);
    void UnregisterPartyRoomMember(CSPointer<User> user);

    CSPointer<CPartyRoom> FindByMember(CSPointer<User> user);
    CSPointer<CPartyRoom> FindByMaster(CSPointer<User> user);
    CSPointer<CPartyRoom> FindById(int roomId);
    CSPointer<CPartyRoom> FindByAuto(CSPointer<User> user, CPartyRoomListFilter& filter);
    CSPointer<CPartyRoom> GetFirstRoom();

    bool List(CPartyRoomListFilter& filter, PartyRoomList& rooms);
    bool IsPartyRoomMaster(CSPointer<User> user);
    virtual bool IsPartyRoomMember(CSPointer<User> user);
    bool DoesShowPartyWantedMessage(CSPointer<User> user);
    int CountMember();
    bool IsEmptyRegisterdRoom();
    bool GetRoomList(PartyRoomList& rooms);


protected:
    using PartyRoomMapReverseIterType = PartyRoomMap::reverse_iterator;
    using PartyRoomMapIterType = PartyRoomMap::iterator;
    using PartyRoomIdMapIterType = PartyRoomIdMap::iterator;

    virtual bool CanRegister(CSPointer<User> user);
    void RegisterMemberList(CSPointer<CPartyRoom> room);
    void UnregisterMemberList(CSPointer<CPartyRoom> room);
    void RegisterPartyRoomMemberImpl(CSPointer<User> user, CSPointer<CPartyRoom> room);
    void UnregisterPartyRoomMemberImpl(CSPointer<User> user);
    CSPointer<CPartyRoom> FindByIdInternal(int roomId);
    CSPointer<CPartyRoom> GetFirstRoomInternal();

    PartyRoomMap m_roomIdMap;
    PartyRoomIdMap m_roomMasterIdMap;
    PartyRoomIdMap m_roomMemberIdMap;
    RWLock m_lock;
};

class CMPCCRoomRegistry final : public CPartyRoomRegistry {
public:
    CMPCCRoomRegistry() = default;
    ~CMPCCRoomRegistry() override = default;

    static CMPCCRoomRegistry& inst();
    bool CanRegister(CSPointer<User> user) override;
    bool IsPartyRoomMember(CSPointer<User> user) override;
};

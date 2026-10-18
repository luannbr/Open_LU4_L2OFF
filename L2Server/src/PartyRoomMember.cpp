#include "PartyRoomMember.h"
#include "Shared/src/MemorySub.h"

#include "User.h"


#include <utility>


L2_DEFINE_OBJECT_CLASS_ID(CRoomMember)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CRoomMember)

L2_DEFINE_OBJECT_ALLOCATOR(CRoomMember, __GetClassName())

L2_DEFINE_OBJECT_NEW(CRoomMember)

CRoomMember::CRoomMember(CSPointer<User> user) : m_user(user.get(), ObjectRefType::ClassMember) {}

CRoomMember::~CRoomMember() = default;

L2_DEFINE_OBJECT_RELEASE(CRoomMember)

CSPointer<User> CRoomMember::GetUser() const { return m_user; }

void CRoomMember::SetUser(CSPointer<User> user) { m_user.SetObject(user.get(), ObjectRefType::ClassMember); }

CPartyWaitingMember::CPartyWaitingMember(CSPointer<User> user) : CRoomMember(std::move(user)) {}

CRoomMember::RoomMemberType CPartyWaitingMember::GetMemberType() const { return WAITING; }

const char16_t* CPartyWaitingMember::GetMemberTypeStr() const { return u"Waiting"; }

CPartyRoomMaster::CPartyRoomMaster(CSPointer<User> user) : CRoomMember(std::move(user)) {}

CRoomMember::RoomMemberType CPartyRoomMaster::GetMemberType() const { return MASTER; }

const char16_t* CPartyRoomMaster::GetMemberTypeStr() const { return u"Master"; }

CPartyMember::CPartyMember(CSPointer<User> user) : CRoomMember(std::move(user)) {}

CRoomMember::RoomMemberType CPartyMember::GetMemberType() const { return PARTY_MEMBER; }

const char16_t* CPartyMember::GetMemberTypeStr() const { return u"Party"; }

CMPCCMaster::CMPCCMaster(CSPointer<User> user) : CRoomMember(std::move(user)) {}

CRoomMember::RoomMemberType CMPCCMaster::GetMemberType() const { return MPCC_MASTER; }

const char16_t* CMPCCMaster::GetMemberTypeStr() const { return u"MPCCMaster"; }

CMPCCMemberPartyMaster::CMPCCMemberPartyMaster(CSPointer<User> user) : CRoomMember(std::move(user)) {}

CRoomMember::RoomMemberType CMPCCMemberPartyMaster::GetMemberType() const { return MPCC_MEMBER_PARTY_MASTER; }

const char16_t* CMPCCMemberPartyMaster::GetMemberTypeStr() const { return u"MPCCMemberPartyMaster"; }

CMPCCPartyMaster::CMPCCPartyMaster(CSPointer<User> user) : CRoomMember(std::move(user)) {}

CRoomMember::RoomMemberType CMPCCPartyMaster::GetMemberType() const { return MPCC_PARTY_MASTER; }

const char16_t* CMPCCPartyMaster::GetMemberTypeStr() const { return u"MPCCPartyMaster"; }

CMPCCIndividual::CMPCCIndividual(CSPointer<User> user) : CRoomMember(std::move(user)) {}

CRoomMember::RoomMemberType CMPCCIndividual::GetMemberType() const { return MPCC_INDIVIDUAL; }

const char16_t* CMPCCIndividual::GetMemberTypeStr() const { return u"MPCCIndividual"; }

CSPointer<CRoomMember> CRoomMemberFactory::MakePartyRoomMaster(CSPointer<User> user) {
    return GetMember(new CPartyRoomMaster(std::move(user)));
}

CSPointer<CRoomMember> CRoomMemberFactory::MakePartyWaitingMember(CSPointer<User> user) {
    return GetMember(new CPartyWaitingMember(std::move(user)));
}

CSPointer<CRoomMember> CRoomMemberFactory::MakePartyMember(CSPointer<User> user) {
    return GetMember(new CPartyMember(std::move(user)));
}

CSPointer<CRoomMember> CRoomMemberFactory::MakeMPCCMaster(CSPointer<User> user) {
    return GetMember(new CMPCCMaster(std::move(user)));
}

CSPointer<CRoomMember> CRoomMemberFactory::MakeMPCCMemberPartyMaster(CSPointer<User> user) {
    return GetMember(new CMPCCMemberPartyMaster(std::move(user)));
}

CSPointer<CRoomMember> CRoomMemberFactory::MakeMPCCPartyMaster(CSPointer<User> user) {
    return GetMember(new CMPCCPartyMaster(std::move(user)));
}

CSPointer<CRoomMember> CRoomMemberFactory::MakeMPCCIndividual(CSPointer<User> user) {
    return GetMember(new CMPCCIndividual(std::move(user)));
}

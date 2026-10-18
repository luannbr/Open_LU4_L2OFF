#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>

class User;

class CRoomMember : public MemoryObject {
public:
    enum RoomMemberType : std::int32_t {
        WAITING = 0,
        MASTER = 1,
        PARTY_MEMBER = 2,
        MPCC_MASTER = 3,
        MPCC_MEMBER_PARTY_MASTER = 4,
        MPCC_PARTY_MASTER = 5,
        MPCC_INDIVIDUAL = 6,
    };

    explicit CRoomMember(CSPointer<User> user);
    ~CRoomMember() override;

    L2_DECLARE_OBJECT_REGISTRATION(CRoomMember)

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    CSPointer<User> GetUser() const;
    void SetUser(CSPointer<User> user);

    virtual RoomMemberType GetMemberType() const = 0;
    virtual const char16_t* GetMemberTypeStr() const = 0;


private:
    CSPointer<User> m_user;
};

class CPartyWaitingMember final : public CRoomMember {
public:
    explicit CPartyWaitingMember(CSPointer<User> user);
    RoomMemberType GetMemberType() const override;
    const char16_t* GetMemberTypeStr() const override;
};

class CPartyRoomMaster final : public CRoomMember {
public:
    explicit CPartyRoomMaster(CSPointer<User> user);
    RoomMemberType GetMemberType() const override;
    const char16_t* GetMemberTypeStr() const override;
};

class CPartyMember final : public CRoomMember {
public:
    explicit CPartyMember(CSPointer<User> user);
    RoomMemberType GetMemberType() const override;
    const char16_t* GetMemberTypeStr() const override;
};

class CMPCCMaster final : public CRoomMember {
public:
    explicit CMPCCMaster(CSPointer<User> user);
    RoomMemberType GetMemberType() const override;
    const char16_t* GetMemberTypeStr() const override;
};

class CMPCCMemberPartyMaster final : public CRoomMember {
public:
    explicit CMPCCMemberPartyMaster(CSPointer<User> user);
    RoomMemberType GetMemberType() const override;
    const char16_t* GetMemberTypeStr() const override;
};

class CMPCCPartyMaster final : public CRoomMember {
public:
    explicit CMPCCPartyMaster(CSPointer<User> user);
    RoomMemberType GetMemberType() const override;
    const char16_t* GetMemberTypeStr() const override;
};

class CMPCCIndividual final : public CRoomMember {
public:
    explicit CMPCCIndividual(CSPointer<User> user);
    RoomMemberType GetMemberType() const override;
    const char16_t* GetMemberTypeStr() const override;
};

class CRoomMemberFactory {
public:
    static CSPointer<CRoomMember> MakePartyRoomMaster(CSPointer<User> user);
    static CSPointer<CRoomMember> MakePartyWaitingMember(CSPointer<User> user);
    static CSPointer<CRoomMember> MakePartyMember(CSPointer<User> user);
    static CSPointer<CRoomMember> MakeMPCCMaster(CSPointer<User> user);
    static CSPointer<CRoomMember> MakeMPCCMemberPartyMaster(CSPointer<User> user);
    static CSPointer<CRoomMember> MakeMPCCPartyMaster(CSPointer<User> user);
    static CSPointer<CRoomMember> MakeMPCCIndividual(CSPointer<User> user);

private:
    static CSPointer<CRoomMember> GetMember(CRoomMember* member) {
        CSPointer<CRoomMember> result(member, ObjectRefType::FunctionChain);
        L2_RELEASE(member, ObjectRefType::CreateDelete);
        return result;
    }
};

#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

template <std::size_t Capacity>
class CPacketHelperEx;

class User;

struct FriendDetailInfo {
    int nCharDBID;
    int nPledgeDBID;
    int nAllianceDBID;
    char cBirthMonth;
    char cBirthDay;
    std::int32_t tLastLogoutTime;
};

static_assert(std::is_aggregate_v<FriendDetailInfo>);
static_assert(std::is_trivially_default_constructible_v<FriendDetailInfo>);
static_assert(std::is_trivially_copyable_v<FriendDetailInfo>);
static_assert(std::is_trivially_destructible_v<FriendDetailInfo>);

struct FriendInfo {
    std::int32_t nCharDBID; // +0x00
    std::array<std::byte, 0x04> m_alignment0004To0008; // +0x04..+0x07
    std::u16string wName; // +0x08..+0x2f
    std::int16_t nLevel; // +0x30..+0x31
    std::int16_t nClass; // +0x32..+0x33
    std::array<std::byte, 0x04> m_alignment0034To0038; // +0x34..+0x37
    std::u16string szFriendMemo; // +0x38..+0x5f
};


// The current image uses four consecutive 0x20-byte MSVC map owners where
// the 2015 donor used 0x10-byte owners. Current constructors, lookups and the
// donor field list fix their order and access boundaries.
class CFriends : public MemoryObject {
public:
    // No current compiled copy body or caller. Donor declaration only.
    CFriends(const CFriends& other);
    explicit CFriends(const std::vector<FriendInfo, std::allocator<FriendInfo>>& friends);

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    CFriends* AddRef(const char* callerFile, int callerLine, ObjectRefType type) override;
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    ~CFriends() override;

    int GetFriendCount();
    void RemoveFriend(unsigned int characterId);
    bool HasFriend(const char16_t* name);
    bool HasFriend(unsigned int characterId);
    unsigned int GetFriendDbId(const char16_t* name);
    void GetFriendName(unsigned int characterId, char16_t* name, int nameCapacity);
    void ChangeFriendName(unsigned int characterId, const char16_t* name);
    void UpdateFriendMemo(unsigned int characterId, const char16_t* memo);
    bool HasFriendDetail(unsigned int characterId);
    void SetDetail(unsigned int characterId, const FriendDetailInfo& detailInfo);
    void AddFriend(unsigned int characterId, const FriendInfo& friendInfo);
    void RemoveFriendDetail(unsigned int characterId);
    void UpdateFriendLevel(unsigned int characterId, int level);
    void UpdateFriendClass(unsigned int characterId, int classId);
    void MakeFriendDetailInfo(int characterId, CPacketHelperEx<20480>& packet);
    void MakeFriendDetailInfo_Internal(
        FriendInfo& friendInfo, FriendDetailInfo& detailInfo, int characterId, CPacketHelperEx<20480>& packet
    );
    void BuildFriendDBIDList(std::vector<int>& characterIds);
    void AddWhisperFriend(unsigned int characterId, const char16_t* name);
    bool HasWhisperFriend(unsigned int characterId);
    void MakeL2FriendList(CPacketHelperEx<20480>& packet, int characterId);
    void MakeFriendList(
        CPacketHelperEx<20480>& packet,
        std::vector<std::u16string>& onlineFriends,
        std::vector<std::u16string>& offlineFriends
    );
    void SendLogonMsgToFriend(User* user);
    void SendLogoffMsgToFriend(User* user);
    void InformLevelChangedInfoToFriends(User* user);
    void InformClassChangedInfoToFriends(User* user);

    RWLock m_lockMap; // +0x18..+0x4f
    std::map<unsigned int, FriendInfo> m_mapFriends; // +0x50..+0x6f
    std::map<std::u16string, unsigned int, runtime::CStringLess<std::u16string>> m_mapFriendsRev; // +0x70..+0x8f
    std::map<unsigned int, FriendDetailInfo> m_mapFriendsDetail; // +0x90..+0xaf

protected:
    std::map<unsigned int, FriendInfo> m_mapWhisperFriends; // +0xb0..+0xcf

private:
    L2_DECLARE_OBJECT_REGISTRATION(CFriends)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CFriends)
};

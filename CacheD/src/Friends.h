#pragma once

#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

template <std::size_t Capacity>
class CPacketHelperEx;

using FriendMemoMap = std::map<unsigned int, std::u16string>;
using FriendIdSet = std::set<unsigned int>;

class CFriends;
using CFriendsSP = CSPointer<CFriends>;

// Shipping owner: CacheD/Friends.{h,cpp}. Current Cached.exe fixes the
// complete 0x70-byte object as MemoryObject + RWLock + friend-id/memo xmap.
class CFriends : public MemoryObject {
public:
    explicit CFriends(const FriendMemoMap& friends);
    ~CFriends() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void GetFriendCharIdList(FriendIdSet& friendIds);
    void MakePacket(CPacketHelperEx<20480>& packet);
    void MakeDetailInfoPacket(unsigned int friendCharacterId, CPacketHelperEx<20480>& packet);
    void UpdateFriendMemo(unsigned int friendCharacterId, const wchar_t* memo);

    // The usable PDB has no names for current 0x004d62c4/0x004d634c.
    // These semantic names describe their exact map operations.
    void AddFriend(unsigned int friendCharacterId);
    void RemoveFriend(unsigned int friendCharacterId);

private:
    L2_DECLARE_OBJECT_REGISTRATION(CFriends)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CFriends)

    RWLock m_lock;
    FriendMemoMap m_friends;
};

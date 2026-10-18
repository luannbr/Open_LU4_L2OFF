#pragma once

#include "Friends.h"

#include "Shared/src/ThreadSafeLibrary.h"

using CFriendsMap = CObjectMap<CFriends, 1, 5, RWLock, int>;

// Shipping owner: CacheD/FriendsDb.{h,cpp}. The current 2018 class has no
// observed instance state. Both methods operate through CUserDb, request TLS
// and DBConn. The global object is the one used by primary opcodes 143/144.
// its original data identifier does not survive, so g_blockCharDb is an
// semantic spelling.
class CBlockCharDB {
public:
    int AddBlockChar(unsigned int characterId, wchar_t* blockedCharacterName);
    int DeleteBlockChar(unsigned int characterId, wchar_t* blockedCharacterName);
    int UpdateBlockMemo(int characterId, int blockedCharacterId, const wchar_t* memo);
};

extern CBlockCharDB g_blockCharDb;

// Shipping owner: CacheD/FriendsDb.{h,cpp}. The current object contains a
// vptr followed by the one-bucket Shared object map at +0x08.
class CFriendsDb {
public:
    CFriendsDb();
    virtual ~CFriendsDb();

    void DeleteFriends(unsigned int characterId, const FriendIdSet& friendIds);
    bool EstablishFriendship(unsigned int characterId, unsigned int friendCharacterId);
    bool BreakFriendship(unsigned int characterId, unsigned int friendCharacterId);
    bool UpdateFriendMemo(int characterId, int friendCharacterId, const wchar_t* memo);
    void GetFriendIdList(unsigned int characterId, FriendIdSet& friendIds);
    void MakeFriendListPacket(unsigned int characterId, CPacketHelperEx<20480>& packet);
    void MakeFriendDetailPacket(unsigned int characterId, unsigned int friendCharacterId, CPacketHelperEx<20480>& packet);

private:
    CFriendsSP CreateFriends(unsigned int characterId, const FriendMemoMap& friends);
    CFriendsSP LoadFriends(unsigned int characterId);
    CFriendsSP GetFriends(unsigned int characterId);

    CFriendsMap m_friends;
};

extern CFriendsDb g_friendsDb;

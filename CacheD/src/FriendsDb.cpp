#include "Shared/src/Log.h"

#include "FriendsDb.h"

#include "Cached.h"
#include "UserDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/UtilsString.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <string>
#include <string_view>

CBlockCharDB g_blockCharDb;
CFriendsDb g_friendsDb;

int CBlockCharDB::AddBlockChar(unsigned int characterId, wchar_t* blockedCharacterName) {
    // Current Cached.exe 0x004d6f54 / FriendsDb.cpp:265.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, DBID[U,%u]", "FriendsDb.cpp", 0x109, characterId)
        // Log.Add(LOG_ERROR,
        //     "[block list] Cannot block '%s' for character %u: the character could not be loaded. No block-list update submitted.",
        //     blockedCharacterName ? WideToUtf8(blockedCharacterName).c_str() : "<missing>", characterId)
        L2_LOG_ERROR(Log,
            "[block list] Cannot block '%s' for character %u: the character could not be loaded; no block-list update submitted.",
            blockedCharacterName ? WideToUtf8(blockedCharacterName).c_str() : "<missing>", characterId);
        return 0;
    }

    CUserSP blockedUser = g_userDb.GetUser(WideToUtf16(blockedCharacterName).c_str(), true);
    if (!blockedUser)
        return 0;

    int sourceCharacterId = static_cast<int>(user->GetId());
    int blockedCharacterId = static_cast<int>(blockedUser->GetId());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddBlockList (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&sourceCharacterId);
    connection.BindParameter(&blockedCharacterId);
    if (!connection.ExecutePrepared(statement))
        return 0;
    return blockedCharacterId;
}

int CBlockCharDB::DeleteBlockChar(unsigned int characterId, wchar_t* blockedCharacterName) {
    // Current Cached.exe 0x004d72e8 / FriendsDb.cpp:343.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, DBID[U,%u]", "FriendsDb.cpp", 0x157, characterId)
        // Log.Add(LOG_ERROR,
        //     "[block list] Cannot unblock '%s' for character %u: the character could not be loaded. No block-list update submitted.",
        //     blockedCharacterName ? WideToUtf8(blockedCharacterName).c_str() : "<missing>", characterId)
        L2_LOG_ERROR(Log,
            "[block list] Cannot unblock '%s' for character %u: the character could not be loaded; no block-list update submitted.",
            blockedCharacterName ? WideToUtf8(blockedCharacterName).c_str() : "<missing>", characterId);
        return 0;
    }

    CUserSP blockedUser = g_userDb.GetUser(WideToUtf16(blockedCharacterName).c_str(), true);
    if (!blockedUser)
        return -1;

    int sourceCharacterId = static_cast<int>(user->GetId());
    int blockedCharacterId = static_cast<int>(blockedUser->GetId());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* deleteStatement = L"{Call lin_DeleteBlockList (?, ?)}";
    connection.Prepare(deleteStatement);
    connection.BindParameter(&sourceCharacterId);
    connection.BindParameter(&blockedCharacterId);
    if (!connection.ExecutePrepared(deleteStatement))
        return 0;

    if (GetCacheDConfiguration().blockPenaltyCount > 0) {
        int point = 0;
        int pointType = 9;
        connection.UnBind();
        connection.Bind(&point);
        constexpr const wchar_t* loadPointStatement = L"{Call lin_LoadUserPoint (?, ?)}";
        connection.Prepare(loadPointStatement);
        connection.BindParameter(&blockedCharacterId);
        connection.BindParameter(&pointType);
        if (connection.ExecutePrepared(loadPointStatement)) {
            // The target ignores Fetch's boolean result and tests the bound
            // zero-initialized value after making the call.
            connection.Fetch();
            if (point > 0) {
                connection.UnBind();
                --point;
                constexpr const wchar_t* savePointStatement = L"{Call lin_SaveUserPoint (?, ?, ?)}";
                connection.Prepare(savePointStatement);
                connection.BindParameter(&blockedCharacterId);
                connection.BindParameter(&pointType);
                connection.BindParameter(&point);
                connection.ExecutePrepared(savePointStatement);
            }
        }
    }

    return blockedCharacterId;
}

int CBlockCharDB::UpdateBlockMemo(int characterId, int blockedCharacterId, const wchar_t* memo) {
    // Current Cached.exe 0x004d77d4 / FriendsDb.cpp:424.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, DBID[U,%d]", "FriendsDb.cpp", 0x1a8, characterId)
        // Log.Add(LOG_ERROR,
        //     "[block list] Cannot update the memo for blocked character %d: owner character %d could not be loaded. No memo update submitted.",
        //     blockedCharacterId, characterId)
        L2_LOG_ERROR(Log,
            "[block list] Cannot update the memo for blocked character %d: owner character %d could not be loaded; no memo update submitted.",
            blockedCharacterId, characterId);
        return 0;
    }

    CUserSP blockedUser = g_userDb.GetUser(blockedCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!blockedUser)
        return 0;

    const std::wstring escapedMemo = Utils::EscapeSqlApostrophes(memo == nullptr ? L"" : memo);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateBlockMemo (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&blockedCharacterId);
    connection.BindParameter(escapedMemo.c_str());
    if (!connection.ExecutePrepared(statement))
        return 0;
    return static_cast<int>(blockedUser->GetId());
}

CFriendsDb::CFriendsDb() = default;

// Current scalar deleting destructor 0x004d7dd8 destroys the one-bucket
// CFriendsMap at +0x08 before optional object deletion.
CFriendsDb::~CFriendsDb() = default;

CFriendsSP CFriendsDb::CreateFriends(unsigned int characterId, const FriendMemoMap& friends) {
    L2_GUARD_FUNCTION();

    CFriendsSP result = m_friends.FindObject(static_cast<int>(characterId));
    if (result)
        return result;

    CFriends* created = new CFriends(friends);
    m_friends.Insert(static_cast<int>(characterId), created);
    result.SetObject(created, ObjectRefType::FunctionChain);
    L2_RELEASE(created, ObjectRefType::CreateDelete);
    return result;
}

CFriendsSP CFriendsDb::LoadFriends(unsigned int characterId) {
    L2_GUARD_FUNCTION();

    int friendCharacterId = 0;
    std::array<char16_t, 51> memo{};
    std::int64_t memoIndicator = -1;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&friendCharacterId);
    connection.BindNullable(memo.data(), 0x66, &memoIndicator);
    constexpr const wchar_t* statement = L"{Call lin_LoadFriends (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement))
        return {};

    FriendMemoMap friends;
    while (connection.Fetch()) {
        friends[static_cast<unsigned int>(friendCharacterId)] = Utils::UnescapeSqlApostrophes(memo.data());
    }
    return CreateFriends(characterId, friends);
}

CFriendsSP CFriendsDb::GetFriends(unsigned int characterId) {
    // Current Cached.exe 0x004d80f8.
    CFriendsSP friends = m_friends.FindObject(static_cast<int>(characterId));
    return friends ? friends : LoadFriends(characterId);
}

bool CFriendsDb::EstablishFriendship(unsigned int characterId, unsigned int friendCharacterId) {
    // Current Cached.exe 0x004d8778.
    L2_GUARD_FUNCTION();

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_EstablishFriendship (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&friendCharacterId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || result != 1) {
        return false;
    }

    CFriendsSP friends = GetFriends(characterId);
    if (friends) {
        CUserSP friendUser =
            g_userDb.GetUser(static_cast<int>(friendCharacterId), true, false, runtime::process::CurrentDatabaseId());
        if (friendUser) {
            friends->AddFriend(friendCharacterId);
        }
    }

    CFriendsSP reverseFriends = GetFriends(friendCharacterId);
    if (reverseFriends) {
        CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            reverseFriends->AddFriend(characterId);
        }
    }
    return true;
}

bool CFriendsDb::BreakFriendship(unsigned int characterId, unsigned int friendCharacterId) {
    // Current Cached.exe 0x004d8ad0.
    L2_GUARD_FUNCTION();

    unsigned int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_BreakFriendship (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&friendCharacterId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || result != 1) {
        return false;
    }

    CFriendsSP friends = GetFriends(characterId);
    if (friends) {
        friends->RemoveFriend(friendCharacterId);
    }
    CFriendsSP reverseFriends = GetFriends(friendCharacterId);
    if (reverseFriends) {
        reverseFriends->RemoveFriend(characterId);
    }
    return true;
}

bool CFriendsDb::UpdateFriendMemo(int characterId, int friendCharacterId, const wchar_t* memo) {
    // Current Cached.exe 0x004d81a0 / FriendsDb.cpp:224.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, DBID[U,%d]", "FriendsDb.cpp", 0xe0, characterId)
        // Log.Add(LOG_ERROR,
        //     "[friends] Cannot update the memo for friend %d: owner character %d could not be loaded. No memo update submitted.",
        //     friendCharacterId, characterId)
        L2_LOG_ERROR(Log,
            "[friends] Cannot update the memo for friend %d: owner character %d could not be loaded; no memo update submitted.",
            friendCharacterId, characterId);
        return false;
    }

    CUserSP friendUser = g_userDb.GetUser(friendCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!friendUser)
        return false;

    const std::wstring escapedMemo = Utils::EscapeSqlApostrophes(memo == nullptr ? L"" : memo);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateFriendMemo (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&friendCharacterId);
    connection.BindParameter(escapedMemo.c_str());
    if (!connection.ExecutePrepared(statement))
        return false;

    CFriendsSP friends = GetFriends(static_cast<unsigned int>(characterId));
    if (!friends)
        return false;
    friends->UpdateFriendMemo(static_cast<unsigned int>(friendCharacterId), memo == nullptr ? L"" : memo);
    return true;
}

void CFriendsDb::DeleteFriends(unsigned int characterId, const FriendIdSet& friendIds) {
    L2_GUARD_FUNCTION();

    // The target removes this in-memory owner before attempting SQL and does
    // not restore it when lin_DeleteFriends fails.
    m_friends.DeleteObject(static_cast<int>(characterId));

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteFriends (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement))
        return;

    for (const unsigned int friendCharacterId : friendIds) {
        CFriendsSP friends = GetFriends(friendCharacterId);
        if (friends)
            friends->RemoveFriend(characterId);
    }
}

void CFriendsDb::GetFriendIdList(unsigned int characterId, FriendIdSet& friendIds) {
    // Current Cached.exe 0x004d8ed8 obtains the same kept lazy CFriends
    // owner and delegates to CFriends::GetFriendCharIdList when non-null.
    L2_GUARD_FUNCTION();
    CFriendsSP friends = GetFriends(characterId);
    if (friends)
        friends->GetFriendCharIdList(friendIds);
}

void CFriendsDb::MakeFriendListPacket(unsigned int characterId, CPacketHelperEx<20480>& packet) {
    // Current Cached.exe 0x004d8578.
    L2_GUARD_FUNCTION();
    CFriendsSP friends = GetFriends(characterId);
    if (friends)
        friends->MakePacket(packet);
}

void CFriendsDb::MakeFriendDetailPacket(
    unsigned int characterId, unsigned int friendCharacterId, CPacketHelperEx<20480>& packet
) {
    // Current Cached.exe 0x004d8670 delegates through the same lazy CFriends
    // owner as the complete list path.
    L2_GUARD_FUNCTION();
    CFriendsSP friends = GetFriends(characterId);
    if (friends) {
        friends->MakeDetailInfoPacket(friendCharacterId, packet);
    }
}

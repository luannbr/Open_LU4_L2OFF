#include "Friends.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/TemplateHelper.h"

#include "User.h"
#include "UserDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/Runtime/Time32.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <optional>


L2_DEFINE_OBJECT_CLASS_ID(CFriends)

L2_DEFINE_OBJECT_CLASS_NAME(CFriends, m_pClassName_CFriends)

L2_DEFINE_OBJECT_NEW(CFriends)

// Current allocator 0x004d675c publishes 0x70. CRT initializer
// 0x006a2e6c is the fourteenth assignment from MemoryObject's counter.
L2_DEFINE_OBJECT_ALLOCATOR(CFriends, m_pClassName_CFriends)

L2_DEFINE_OBJECT_RELEASE(CFriends)

CFriends::CFriends(const FriendMemoMap& friends) : m_friends(friends) {
    // Current constructor 0x004d65bc copies every xmap node after creating
    // the RWLock at +0x18 and the xmap at +0x50.
}

// Current Cached.exe complete destructor 0x004d63f4.
CFriends::~CFriends() = default;

void CFriends::AddFriend(unsigned int friendCharacterId) {
    // Current unnamed 0x004d62c4 inserts a default-constructed empty memo.
    {
        L2_WRITE_GUARD(m_lock);
        m_friends.emplace(friendCharacterId, std::u16string{});
    }
}

void CFriends::RemoveFriend(unsigned int friendCharacterId) {
    // Current unnamed 0x004d634c erases the key if it is present.
    {
        L2_WRITE_GUARD(m_lock);
        m_friends.erase(friendCharacterId);
    }
}

void CFriends::GetFriendCharIdList(FriendIdSet& friendIds) {
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_lock);
        for (const auto& entry : m_friends) {
            friendIds.insert(entry.first);
        }
    }
}

void CFriends::MakePacket(CPacketHelperEx<20480>& packet) {
    // Current Cached.exe 0x004d5c98. The database result order and the
    // nested packet shape are fixed by the paired current
    // L2Server ReplyLoadFriendList decoder.
    L2_GUARD_FUNCTION();

    L2_READ_GUARD(m_lock);
    for (const auto& entry : m_friends) {
        const unsigned int friendCharacterId = entry.first;
        std::array<char16_t, 50> characterName{};
        short level = 0;
        short characterClass = 0;

        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(characterName.data(), 0x64);
        connection.Bind(&level);
        connection.Bind(&characterClass);
        constexpr const wchar_t* statement = L"{Call lin_LoadFriendInfo(?)}";
        connection.Prepare(statement);
        connection.BindParameter(&friendCharacterId);
        if (connection.ExecutePrepared(statement) && connection.Fetch()) {
            packet.Assemble(
                "dShhS", static_cast<int>(friendCharacterId), characterName.data(), static_cast<int>(level),
                static_cast<int>(characterClass), entry.second.c_str()
            );
        }

        if (packet.GetAssembledCount() > 0x7f) {
            break;
        }
    }
}

void CFriends::MakeDetailInfoPacket(unsigned int friendCharacterId, CPacketHelperEx<20480>& packet) {
    // Current Cached.exe 0x004d53e8. The friend map membership check and the
    // entire user lookup/assembly remain under the source object's read lock.
    L2_GUARD_FUNCTION();

    std::optional<CUserSP> userStorage;
    {
        L2_READ_GUARD(m_lock);
        if (m_friends.find(friendCharacterId) == m_friends.end()) {
            return;
        }

        auto& user = userStorage.emplace(g_userDb.GetUser(
            static_cast<int>(friendCharacterId), true, true, runtime::process::CurrentDatabaseId()));
        if (user) {
            const SYSTEMTIME* const birthday = user->GetBirthday();
            packet.Assemble(
                "dddccd", static_cast<int>(friendCharacterId), static_cast<int>(user->GetPledgeID()), user->GetAllianceID(),
                static_cast<int>(birthday->wMonth), static_cast<int>(birthday->wDay),
                runtime::crt::SystemTimeToTime32(user->m_lastLogoutTime)
            );
        }
    }
}

void CFriends::UpdateFriendMemo(unsigned int friendCharacterId, const wchar_t* memo) {
    L2_GUARD_FUNCTION();
    std::optional<FriendMemoMap::iterator> foundStorage;
    {
        L2_WRITE_GUARD(m_lock);
        const auto& found = foundStorage.emplace(m_friends.find(friendCharacterId));
        if (found != m_friends.end()) {
            // Current 0x004d58c0 calls wcslen(memo) unconditionally before
            // assigning the target xstring. A null memo is therefore not a
            // supported direct-call contract.
            const std::u16string targetMemo = WideToUtf16(memo);
            found->second.assign(std::u16string_view(targetMemo));
        }
    }
}

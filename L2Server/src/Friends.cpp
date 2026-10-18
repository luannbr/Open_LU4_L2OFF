#include "Friends.h"
#include "Shared/src/MemorySub.h"

#include <optional>
#include "Alliance.h"
#include "CConfig.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

namespace {

template <std::size_t Capacity>
void CopyTargetWide(const char16_t* source, std::array<char16_t, Capacity>& destination) {
    destination.fill(char16_t{});
    if (source == nullptr) {
        return;
    }

    std::size_t count = 0;
    while (count + 1 < destination.size() && source[count] != char16_t{}) {
        ++count;
    }
    std::copy_n(source, count, destination.data());
}

} // namespace

// static_assert(L2ServerMemory::kCFriendsClassId == 122)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CFriends) == 122);

L2_DEFINE_OBJECT_CLASS_ID(CFriends)

L2_DEFINE_OBJECT_CLASS_NAME(CFriends, m_pClassName_CFriends)

L2_DEFINE_OBJECT_ALLOCATOR(CFriends, m_pClassName_CFriends)

L2_DEFINE_OBJECT_NEW(CFriends)

CFriends* CFriends::AddRef(const char*, int, ObjectRefType) {
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);
    return this;
}

L2_DEFINE_OBJECT_RELEASE(CFriends)

CFriends::CFriends(const std::vector<FriendInfo, std::allocator<FriendInfo>>& friends) {
    for (const FriendInfo& friendInfo : friends) {
        const unsigned int characterId = static_cast<unsigned int>(friendInfo.nCharDBID);
        m_mapFriends.emplace(characterId, friendInfo);
        m_mapFriendsRev.emplace(friendInfo.wName, characterId);
    }
}

CFriends::~CFriends() = default;

int CFriends::GetFriendCount() {
    const int count = [&] {
        L2_READ_GUARD(m_lockMap);
        const int count = static_cast<int>(m_mapFriends.size());
        return count;
    }();
    return count;
}

void CFriends::RemoveFriend(unsigned int characterId) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockMap);
        const auto position = m_mapFriends.find(characterId);
        if (position != m_mapFriends.end()) {
            m_mapFriendsRev.erase(position->second.wName);
            m_mapFriends.erase(position);
        }
    }
}

bool CFriends::HasFriend(const char16_t* name) {
    std::optional<std::u16string> targetNameStorage;
    const bool found = [&] {
        L2_READ_GUARD(m_lockMap);
        const std::u16string& targetName = targetNameStorage.emplace(Utf16Text(name).String());
        const bool found = m_mapFriendsRev.find(targetName) != m_mapFriendsRev.end();
        return found;
    }();
    return found;
}

bool CFriends::HasFriend(unsigned int characterId) {
    const bool found = [&] {
        L2_READ_GUARD(m_lockMap);
        const bool found = m_mapFriends.find(characterId) != m_mapFriends.end();
        return found;
    }();
    return found;
}

unsigned int CFriends::GetFriendDbId(const char16_t* name) {
    std::optional<std::u16string> targetNameStorage;
    const unsigned int characterId = [&] {
        L2_READ_GUARD(m_lockMap);
        const std::u16string& targetName = targetNameStorage.emplace(Utf16Text(name).String());
        const auto position = m_mapFriendsRev.find(targetName);
        const unsigned int characterId = position == m_mapFriendsRev.end() ? 0U : position->second;
        return characterId;
    }();
    return characterId;
}

void CFriends::GetFriendName(unsigned int characterId, char16_t* name, int nameCapacity) {
    L2_GUARD_FUNCTION();

    name[0] = u'\0';
    {
        L2_READ_GUARD(m_lockMap);
        const auto position = m_mapFriends.find(characterId);
        if (position != m_mapFriends.end() && position->second.wName.size() < static_cast<std::size_t>(nameCapacity)) {
            std::copy_n(position->second.wName.c_str(), position->second.wName.size() + 1, name);
        }
    }
}

void CFriends::ChangeFriendName(unsigned int characterId, const char16_t* name) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockMap);
        const auto position = m_mapFriends.find(characterId);
        if (position != m_mapFriends.end()) {
            const auto oldName = m_mapFriendsRev.find(position->second.wName);
            if (oldName != m_mapFriendsRev.end()) {
                m_mapFriendsRev.erase(oldName);
            }

            std::u16string targetName = Utf16Text(name).String();
            position->second.wName = targetName;
            m_mapFriendsRev.emplace(std::move(targetName), characterId);
        }
    }
}

void CFriends::UpdateFriendMemo(unsigned int characterId, const char16_t* memo) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockMap);
        const auto position = m_mapFriends.find(characterId);
        if (position != m_mapFriends.end()) {
            position->second.szFriendMemo.assign(std::u16string_view(memo));
        }
    }
}

void CFriends::SetDetail(unsigned int characterId, const FriendDetailInfo& detailInfo) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockMap);
        if (m_mapFriends.find(characterId) != m_mapFriends.end() &&
            m_mapFriendsDetail.find(characterId) == m_mapFriendsDetail.end()) {
            m_mapFriendsDetail.emplace(characterId, detailInfo);
        }
    }
}

void CFriends::AddFriend(unsigned int characterId, const FriendInfo& friendInfo) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockMap);
        FriendInfo& destination = m_mapFriends[characterId];
        destination.nCharDBID = friendInfo.nCharDBID;
        destination.wName = friendInfo.wName;
        destination.nLevel = friendInfo.nLevel;
        destination.nClass = friendInfo.nClass;
        destination.szFriendMemo = friendInfo.szFriendMemo;
        m_mapFriendsRev[friendInfo.wName] = characterId;
    }
}

void CFriends::RemoveFriendDetail(unsigned int characterId) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockMap);
        const auto position = m_mapFriendsDetail.find(characterId);
        if (position != m_mapFriendsDetail.end()) {
            m_mapFriendsDetail.erase(position);
        }
    }
}

void CFriends::UpdateFriendLevel(unsigned int characterId, int level) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockMap);
        const auto position = m_mapFriends.find(characterId);
        if (position != m_mapFriends.end()) {
            position->second.nLevel = static_cast<std::int16_t>(level);
        }
    }
}

void CFriends::UpdateFriendClass(unsigned int characterId, int classId) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockMap);
        const auto position = m_mapFriends.find(characterId);
        if (position != m_mapFriends.end()) {
            position->second.nClass = static_cast<std::int16_t>(classId);
        }
    }
}

void CFriends::BuildFriendDBIDList(std::vector<int>& characterIds) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockMap);
        for (const auto& entry : m_mapFriends) {
            characterIds.push_back(static_cast<int>(entry.first));
        }
    }
}

void CFriends::AddWhisperFriend(unsigned int characterId, const char16_t* name) {
    {
        L2_WRITE_GUARD(m_lockMap);
        m_mapWhisperFriends[characterId].wName = name;
    }
}

bool CFriends::HasWhisperFriend(unsigned int characterId) {
    const bool found = [&] {
        L2_READ_GUARD(m_lockMap);
        const bool found = m_mapWhisperFriends.find(characterId) != m_mapWhisperFriends.end();
        return found;
    }();
    return found;
}

void CFriends::MakeL2FriendList(CPacketHelperEx<20480>& packet, int characterId) {
    L2_GUARD_FUNCTION();

    {
        L2_READ_GUARD(m_lockMap);
        for (auto& entry : m_mapFriends) {
            FriendInfo& friendInfo = entry.second;
            int status = 0;
            std::uint32_t serverId = 0;

            CSPointer<User> user =
                g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(entry.first)));
            if (user) {
                status = 1;
                serverId = user->GetServerId();
                friendInfo.nLevel = static_cast<std::int16_t>(user->Level());
                friendInfo.nClass = static_cast<std::int16_t>(user->m_data->m_nClass);
            }
            if (static_cast<int>(entry.first) == characterId) {
                status += 2;
            }

            packet.Assemble(
                "dSddddS", static_cast<int>(entry.first), friendInfo.wName.c_str(), status, serverId,
                static_cast<int>(friendInfo.nLevel), static_cast<int>(friendInfo.nClass), friendInfo.szFriendMemo.c_str()
            );
        }
    }
}

void CFriends::MakeFriendList(
    CPacketHelperEx<20480>& packet, std::vector<std::u16string>& onlineFriends, std::vector<std::u16string>& offlineFriends
) {
    L2_GUARD_FUNCTION();

    {
        L2_READ_GUARD(m_lockMap);
        for (auto& entry : m_mapFriends) {
            FriendInfo& friendInfo = entry.second;
            int online = 0;
            std::uint32_t serverId = 0;

            CSPointer<User> user =
                g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(entry.first)));
            if (user) {
                online = 1;
                serverId = user->GetServerId();
                friendInfo.nLevel = static_cast<std::int16_t>(user->Level());
                friendInfo.nClass = static_cast<std::int16_t>(user->m_data->m_nClass);
                onlineFriends.push_back(friendInfo.wName);
            } else {
                offlineFriends.push_back(friendInfo.wName);
            }

            if (packet.Assemble(
                    "dSdddd", static_cast<int>(entry.first), friendInfo.wName.c_str(), online, serverId,
                    static_cast<int>(friendInfo.nClass), static_cast<int>(friendInfo.nLevel)
                ) == 0) {
                break;
            }
        }
    }
}

void CFriends::SendLogonMsgToFriend(User* user) {
    L2_GUARD_FUNCTION();

    {
        L2_READ_GUARD(m_lockMap);
        for (const auto& entry : m_mapFriends) {
            CSPointer<User> friendUser =
                g_UserDBIDMap.FindObject(std::CDualKey<int, int>(user->m_data->m_nWorldId, static_cast<int>(entry.first)));
            if (friendUser) {
                friendUser->GetSocket()->SendSystemMessage_Friend(SystemMessageId::FRIEND_LOGGED_IN, user->GetName());
                const std::u16string targetName = std::u16string(user->GetName());
                friendUser->GetSocket()->Send("cdS", 0x59, 1, targetName.c_str());
            }
        }
    }
}

void CFriends::SendLogoffMsgToFriend(User* user) {
    L2_GUARD_FUNCTION();

    {
        L2_READ_GUARD(m_lockMap);
        for (const auto& entry : m_mapFriends) {
            CSPointer<User> friendUser =
                g_UserDBIDMap.FindObject(std::CDualKey<int, int>(user->m_data->m_nWorldId, static_cast<int>(entry.first)));
            if (friendUser) {
                friendUser->GetSocket()->SendSystemMessage_Friend(SystemMessageId::FRIEND_LOGGED_OUT, user->GetName());
                const std::int32_t now = runtime::crt::Time32Now();
                const std::u16string targetName = std::u16string(user->GetName());
                friendUser->GetSocket()->Send("cdSd", 0x59, 0, targetName.c_str(), now);
            }
        }
    }
}

void CFriends::InformLevelChangedInfoToFriends(User* user) {
    L2_GUARD_FUNCTION();

    {
        L2_READ_GUARD(m_lockMap);
        for (const auto& entry : m_mapFriends) {
            CSPointer<User> friendUser =
                g_UserDBIDMap.FindObject(std::CDualKey<int, int>(user->m_data->m_nWorldId, static_cast<int>(entry.first)));
            if (friendUser) {
                friendUser->SetNeedUpdateFriendsInfo(
                    FriendsEnum::FRIEND_LEVEL_CHANGED, user->m_data->m_nWorldId, user->Id()
                );
            }
        }
    }
}

void CFriends::InformClassChangedInfoToFriends(User* user) {
    L2_GUARD_FUNCTION();

    {
        L2_READ_GUARD(m_lockMap);
        for (const auto& entry : m_mapFriends) {
            CSPointer<User> friendUser =
                g_UserDBIDMap.FindObject(std::CDualKey<int, int>(user->m_data->m_nWorldId, static_cast<int>(entry.first)));
            if (friendUser) {
                friendUser->SetNeedUpdateFriendsInfo(
                    FriendsEnum::FRIEND_CLASS_CHANGED, user->m_data->m_nWorldId, user->Id()
                );
            }
        }
    }
}

bool CFriends::HasFriendDetail(unsigned int characterId) {
    const bool found = [&] {
        L2_READ_GUARD(m_lockMap);
        const bool found = m_mapFriendsDetail.find(characterId) != m_mapFriendsDetail.end();
        return found;
    }();
    return found;
}

void CFriends::MakeFriendDetailInfo(int characterId, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lockMap);
        const auto friendPosition = m_mapFriends.find(static_cast<unsigned int>(characterId));
        if (friendPosition != m_mapFriends.end()) {
            const auto detailPosition = m_mapFriendsDetail.find(static_cast<unsigned int>(characterId));
            if (detailPosition != m_mapFriendsDetail.end()) {
                MakeFriendDetailInfo_Internal(friendPosition->second, detailPosition->second, characterId, packet);
            }
        }
    }
}

void CFriends::MakeFriendDetailInfo_Internal(
    FriendInfo& friendInfo, FriendDetailInfo& detailInfo, int characterId, CPacketHelperEx<20480>& packet
) {
    int online = 0;
    std::uint32_t userServerId = 0;
    const std::int32_t now = runtime::crt::Time32Now();
    int elapsed = runtime::Win64CvttDoubleToInt32(
        std::difftime(static_cast<std::time_t>(now), static_cast<std::time_t>(detailInfo.tLastLogoutTime))
    );

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterId));
    if (user) {
        online = 1;
        userServerId = user->GetServerId();
        elapsed = -1;
        friendInfo.nLevel = static_cast<std::int16_t>(user->Level());
        friendInfo.nClass = static_cast<std::int16_t>(user->m_data->m_nClass);
        detailInfo.nPledgeDBID = static_cast<int>(user->m_data->m_nPledgeID);
        detailInfo.nAllianceDBID = user->GetAllianceDbId();
    }

    std::uint32_t pledgeServerId = 0;
    int pledgeCrestId = 0;
    std::array<char16_t, 24> pledgeName{};
    int allianceCrestId = 0;
    std::array<char16_t, 24> allianceName{};

    CPledge* pledge = nullptr;
    {
        CSPointer<CPledge> foundPledge = CPledge::FindPledge(g_nWorldId, detailInfo.nPledgeDBID);
        pledge = foundPledge.get();
    }
    if (pledge != nullptr) {
        pledgeServerId = pledge->Id();
        pledgeCrestId = pledge->GetFieldValue(0xd);
        CopyTargetWide(pledge->GetPledgeName(), pledgeName);

        CAlliance* const alliance = pledge->GetAlliance();
        if (alliance != nullptr) {
            allianceCrestId = static_cast<int>(alliance->m_data->m_nCrestId);
            CopyTargetWide(alliance->m_data->m_sAllianceName, allianceName);
        }
    }

    packet.Assemble(
        "dSddhhddSddSccdS", characterId, friendInfo.wName.c_str(), online, userServerId, static_cast<int>(friendInfo.nLevel),
        static_cast<int>(friendInfo.nClass), pledgeServerId, pledgeCrestId, pledgeName.data(), detailInfo.nAllianceDBID,
        allianceCrestId, allianceName.data(), static_cast<int>(static_cast<std::int8_t>(detailInfo.cBirthMonth)),
        static_cast<int>(static_cast<std::int8_t>(detailInfo.cBirthDay)), elapsed, friendInfo.szFriendMemo.c_str()
    );
}

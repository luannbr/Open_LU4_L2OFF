#include "PartyMatchingWaitingRoom.h"

#include "PartyRoomRegistry.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <string>

namespace {

template <typename TContainer>
auto FindUserId(TContainer& container, const User* user) {
    return std::find(container.begin(), container.end(), static_cast<std::int32_t>(user->Id()));
}

} // namespace

CPartyMatchingWaitingRoom g_PartyMatchingWaitingRoom;

CPartyMatchingWaitingRoomListFilter::CPartyMatchingWaitingRoomListFilter(
    User* const user,
    const int page,
    const int lowerLevelLimit,
    const int upperLevelLimit,
    const PartyMatchingRoomType roomType,
    std::vector<int>& classList,
    const SharedUtilsWideChar* const characterName
) :
    m_pUser(user),
    m_nPage(page),
    m_nLowerLevelLimit(0),
    m_nUpperLevelLimit(0),
    m_eRoomType(roomType),
    m_lstClass(classList) {
    InitLevelLimits(lowerLevelLimit, upperLevelLimit);
    SetCharName(characterName);
}

bool CPartyMatchingWaitingRoomListFilter::IsBetweenLevelLimit(User* const user) {
    // Current L2Server 0x00944aa0.
    L2_GUARD_FUNCTION();
    const int level = user->m_data->m_nLevel;
    return m_nLowerLevelLimit <= level && level <= m_nUpperLevelLimit;
}

bool CPartyMatchingWaitingRoomListFilter::HasCharacterName(User* const user) {
    // Current L2Server 0x00944b58.
    L2_GUARD_FUNCTION();
    if (m_sCharName[0] == SharedUtilsWideChar{}) {
        return true;
    }

    const std::size_t prefixLength = std::char_traits<SharedUtilsWideChar>::length(m_sCharName);
    const std::u16string userName = std::u16string(user->GetName());
    return userName.size() >= prefixLength &&
        std::char_traits<char16_t>::compare(reinterpret_cast<const char16_t*>(m_sCharName), userName.data(), prefixLength) ==
        0;
}

int CPartyMatchingWaitingRoomListFilter::GetCountPerPage() {
    // Current L2Server 0x00944c40.
    L2_GUARD_FUNCTION();
    if (m_pUser == nullptr || m_pUser->m_nPartyWaitingRoomLimitCount < 1) {
        return 0x40;
    }
    return m_pUser->m_nPartyWaitingRoomLimitCount;
}

bool CPartyMatchingWaitingRoomListFilter::IsInPage(const int index) {
    // Current L2Server 0x00944ce0.
    L2_GUARD_FUNCTION();
    const int countPerPage = GetCountPerPage();
    const int firstIndex = runtime::AddInt32Wrapping(
        runtime::MultiplyInt32Wrapping(countPerPage, runtime::AddInt32Wrapping(m_nPage, -1)), 1
    );
    if (firstIndex <= index) {
        const int lastIndex = runtime::MultiplyInt32Wrapping(GetCountPerPage(), m_nPage);
        if (index <= lastIndex) {
            return true;
        }
    }
    return false;
}

void CPartyMatchingWaitingRoomListFilter::SetCharName(const SharedUtilsWideChar* const characterName) {
    // Current L2Server 0x00944da8.
    L2_GUARD_FUNCTION();
    const std::size_t copyLength = std::min<std::size_t>(std::char_traits<SharedUtilsWideChar>::length(characterName), 24);
    std::memcpy(m_sCharName, characterName, copyLength * sizeof(SharedUtilsWideChar));
    m_sCharName[copyLength] = SharedUtilsWideChar{};
    m_sCharName[24] = SharedUtilsWideChar{};
}

bool CPartyMatchingWaitingRoomListFilter::IsInClass(User* const user) {
    // Current L2Server 0x00944e60.
    L2_GUARD_FUNCTION();
    if (m_lstClass.empty()) {
        return true;
    }
    return std::find(m_lstClass.begin(), m_lstClass.end(), user->m_data->m_nClass) != m_lstClass.end();
}

User* CPartyMatchingWaitingRoomListFilter::GetUser() { return m_pUser; }

PartyMatchingRoomType CPartyMatchingWaitingRoomListFilter::GetRoomType() { return m_eRoomType; }

void CPartyMatchingWaitingRoomListFilter::SetLowerLevelLimit(const int lowerLevelLimit) {
    m_nLowerLevelLimit = static_cast<std::uint32_t>(lowerLevelLimit) - 1U < 120U ? lowerLevelLimit : 1;
}

void CPartyMatchingWaitingRoomListFilter::SetUpperLevelLimit(const int upperLevelLimit) {
    m_nUpperLevelLimit = static_cast<std::uint32_t>(upperLevelLimit) - 1U < 120U ? upperLevelLimit : 120;
}

void CPartyMatchingWaitingRoomListFilter::CheckLevelLimits() {
    if (m_nUpperLevelLimit < m_nLowerLevelLimit) {
        m_nLowerLevelLimit = 1;
    }
}

void CPartyMatchingWaitingRoomListFilter::SetClass(std::vector<int>& classList) { m_lstClass = classList; }

void CPartyMatchingWaitingRoomListFilter::InitLevelLimits(const int lowerLevelLimit, const int upperLevelLimit) {
    // Current L2Server 0x00945abc.
    L2_GUARD_FUNCTION();
    if (m_eRoomType == PARTY_ROOM) {
        if (m_pUser != nullptr) {
            CSPointer<CPartyRoom> room =
                CPartyRoomRegistry::inst().FindByMember(CSPointer<User>(m_pUser, ObjectRefType::FunctionChain));
            if (!room) {
                SetLowerLevelLimit(1);
                SetUpperLevelLimit(120);
            } else {
                CPartyRoomInfo& roomInfo = room->GetPartyRoomInfo();
                SetLowerLevelLimit(roomInfo.GetLowerLevelLimit());
                SetUpperLevelLimit(roomInfo.GetUpperLevelLimit());
            }
        }
    } else if (m_eRoomType == WAITING_ROOM) {
        SetLowerLevelLimit(lowerLevelLimit);
        SetUpperLevelLimit(upperLevelLimit);
    }
    CheckLevelLimits();
}

CPartyMatchingWaitingRoom::~CPartyMatchingWaitingRoom() = default;

bool CPartyMatchingWaitingRoom::IsInWaitingRoom(User* user) {
    L2_GUARD_FUNCTION();
    const bool found = [&] {
        L2_GUARD(m_lock);
        const bool found = FindUserId(m_waitingUserSidList, user) != m_waitingUserSidList.end();
        return found;
    }();
    return found;
}

bool CPartyMatchingWaitingRoom::_IsInvitable(User* user) {
    L2_GUARD_FUNCTION();
    return FindUserId(m_nonInvitableUserSidList, user) == m_nonInvitableUserSidList.end();
}

bool CPartyMatchingWaitingRoom::SetInvitable(User* user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }

    {
        L2_GUARD(m_lock);
        const auto found = FindUserId(m_nonInvitableUserSidList, user);
        if (found == m_nonInvitableUserSidList.end()) {
            return false;
        }
        m_nonInvitableUserSidList.erase(found);
    }
    return true;
}

bool CPartyMatchingWaitingRoom::IsInvitable(User* user) {
    L2_GUARD_FUNCTION();
    const bool invitable = [&] {
        L2_GUARD(m_lock);
        const bool invitable = _IsInvitable(user);
        return invitable;
    }();
    return invitable;
}

bool CPartyMatchingWaitingRoom::LeaveWaitingRoom(User* user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }

    const bool removed = [&] {
        L2_GUARD(m_lock);
        const auto found = FindUserId(m_waitingUserSidList, user);
        const bool removed = found != m_waitingUserSidList.end();
        if (removed) {
            m_waitingUserSidList.erase(found);
        }
        return removed;
    }();

    if (removed) {
        SetInvitable(user);
    }
    return removed;
}

bool CPartyMatchingWaitingRoom::EnterWaitingRoom(User* user) {
    L2_GUARD_FUNCTION();
    if (!user || user->GetParty() || IsInWaitingRoom(user) || user->m_data->m_bGMInvisible ||
        user->m_data->m_nBuilder >= 2) {
        return false;
    }

    {
        L2_GUARD(m_lock);
        m_waitingUserSidList.push_back(static_cast<std::int32_t>(user->Id()));
    }
    return true;
}

bool CPartyMatchingWaitingRoom::SetNonInvitable(User* user) {
    L2_GUARD_FUNCTION();
    if (!user || !IsInWaitingRoom(user) || !IsInvitable(user)) {
        return false;
    }

    {
        L2_GUARD(m_lock);
        m_nonInvitableUserSidList.push_back(static_cast<std::int32_t>(user->Id()));
    }
    return true;
}

bool CPartyMatchingWaitingRoom::ListWaitingRoom(CPartyMatchingWaitingRoomListFilter& filter) {
    // Current L2Server 0x009454a8.
    L2_GUARD_FUNCTION();

    std::array<char, 0x1000> packet{};
    User* const requester = filter.GetUser();
    int totalCount = 0;
    std::uint32_t listedCount = 0;
    std::uint32_t packetLength = 0;

    {
        L2_GUARD(m_lock);
        for (auto position = m_waitingUserSidList.end(); position != m_waitingUserSidList.begin();) {
            --position;
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(*position));
            User* listedUser = nullptr;
            if (creature != nullptr && creature->IsPC()) {
                listedUser = creature->CastUser();
            }
            if (listedUser == nullptr || !filter.IsBetweenLevelLimit(listedUser) || !filter.IsInClass(listedUser) ||
                !filter.HasCharacterName(listedUser) || !_IsInvitable(listedUser)) {
                continue;
            }

            totalCount = runtime::AddInt32Wrapping(totalCount, 1);
            if (!filter.IsInPage(totalCount) || listedCount >= static_cast<std::uint32_t>(filter.GetCountPerPage())) {
                continue;
            }

            auto* const sharedData = listedUser->m_data;
            const int bbsLocation = listedUser->GetBBSLocation();
            const char16_t* const hostName = listedUser->GetName();
            const std::u16string targetName = std::u16string(hostName);
            const int assembledUser = ::Assemble(
                packet.data() + packetLength, 0x1000 - runtime::Int32FromBits(packetLength), "Sddd", targetName.c_str(),
                sharedData->m_nClass, sharedData->m_nLevel, bbsLocation
            );
            const std::uint32_t userEnd = packetLength + static_cast<std::uint32_t>(assembledUser);

            std::uint32_t nextListedCount = listedCount;
            {
                User::InZoneRestrictionMap restrictions;
                listedUser->GetAllInZoneRestriction(restrictions, nullptr);
                const int assembledCount = ::Assemble(
                    packet.data() + userEnd, 0x1000 - runtime::Int32FromBits(userEnd), "d",
                    static_cast<std::uint32_t>(restrictions.size())
                );
                packetLength = userEnd + static_cast<std::uint32_t>(assembledCount);
                for (const auto& restriction : restrictions) {
                    const int assembledRestriction = ::Assemble(
                        packet.data() + packetLength, 0x1000 - runtime::Int32FromBits(packetLength), "d",
                        restriction.first
                    );
                    packetLength += static_cast<std::uint32_t>(assembledRestriction);
                }
                nextListedCount = listedCount + 1U;
            }
            listedCount = nextListedCount;
        }
    }

    requester->GetSocket()->Send(
        "chddb", 0xfe, 0x36, totalCount, listedCount, runtime::Int32FromBits(packetLength), packet.data()
    );
    return true;
}

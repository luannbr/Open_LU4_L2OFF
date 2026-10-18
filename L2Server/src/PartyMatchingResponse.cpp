#include "PartyMatchingResponse.h"
#include "Party.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "User.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace {

using InZoneRestrictionMap = ::std::map<int, int>;


std::wstring AccountName(const User& user) {
    std::size_t length = 0;
    while (length < user.m_sAccountName.size() && user.m_sAccountName[length] != u'\0') {
        ++length;
    }
    return Utf16ToWide(std::u16string_view(user.m_sAccountName.data(), length));
}

} // namespace

CPartyMatchingResponseComponent::~CPartyMatchingResponseComponent() = default;

CPartyMatchingResponse::CPartyMatchingResponse(CSPointer<User> user) : m_user(user) {}

void CPartyMatchingResponse::Respond(CPartyMatchingResponseComponent& component) {
    L2_GUARD_FUNCTION();
    if (m_user) {
        component.Respond(m_user);
    }
}

CPartyMatchingResponseToAllMember::CPartyMatchingResponseToAllMember(CPartyRoom::RoomMemberList& members) :
    m_memberList(members) {}

void CPartyMatchingResponseToAllMember::Respond(CPartyMatchingResponseComponent& component) {
    L2_GUARD_FUNCTION();
    for (const CSPointer<CRoomMember>& member : m_memberList) {
        if (!member) {
            continue;
        }
        const CSPointer<User> user = member->GetUser();
        if (user) {
            component.Respond(user);
        }
    }
}

CPartyRoomInfoResponse::CPartyRoomInfoResponse(CPartyRoomInfo& info) : m_info(info) {}

bool CPartyRoomInfoResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return true;
    }
    CUserSocket* const socket = user->GetSocket();
    if (!socket) {
        return true;
    }

    if (!m_info.IsCMPCCRoomInfo()) {
        socket->Send(
            "cddddddSh", 0x9d, m_info.GetId(), m_info.GetMaxMemberLimit(), m_info.GetUpperLevelLimit(),
            m_info.GetLowerLevelLimit(), static_cast<int>(m_info.GetPartyRoutingType()), m_info.GetLocation(),
            m_info.GetTitle().c_str()
        );
    } else {
        socket->Send(
            "chddddddSh", 0xfe, 0x9c, m_info.GetId(), m_info.GetMaxMemberLimit(), m_info.GetUpperLevelLimit(),
            m_info.GetLowerLevelLimit(), static_cast<int>(m_info.GetPartyRoutingType()), m_info.GetLocation(),
            m_info.GetTitle().c_str()
        );
    }
    return true;
}

CPartyRoomSystemMessageResponse::CPartyRoomSystemMessageResponse(const int messageNo) : m_messageNo(messageNo) {}

bool CPartyRoomSystemMessageResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (user && user->GetSocket()) {
        user->GetSocket()->SendSystemMessage(static_cast<unsigned int>(m_messageNo));
    }
    return true;
}

CPartyMatchingWindowsStatusResponse::CPartyMatchingWindowsStatusResponse(const bool open) : m_open(open) {}

bool CPartyMatchingWindowsStatusResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (user) {
        user->SetPM_WindowOpen(m_open);
        user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_PARTYMATCHING});
    }
    return true;
}

CPartyRoomCreatedLogger::CPartyRoomCreatedLogger(CSPointer<CPartyRoom> room) : m_partyRoom(std::move(room)) {}

bool CPartyRoomCreatedLogger::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!m_partyRoom || !user || !user->m_data) {
        return true;
    }

    CPartyRoomInfo& info = m_partyRoom->GetPartyRoomInfo();
    const std::u16string targetTitle = ReplaceStr(info.GetTitle(), u",", u" ");
    const std::wstring title = Utf16ToWide(targetTitle);
    const FVector position = user->GetPos();
    const std::wstring accountName = AccountName(*user.get());
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
    //     L"%s,%s,,,,",
    //     static_cast<std::int64_t>(0x358),
    //     static_cast<std::int64_t>(user->GetAccountID()),
    //     static_cast<std::int64_t>(user->GetDBID()),
    //     static_cast<std::int64_t>(position.x),
    //     static_cast<std::int64_t>(position.y),
    //     static_cast<std::int64_t>(position.z),
    //     title.c_str(),
    //     static_cast<std::int64_t>(user->m_data->m_Race),
    //     static_cast<std::int64_t>(user->m_data->m_Sex),
    //     static_cast<std::int64_t>(user->m_data->m_nClass),
    //     static_cast<std::int64_t>(user->m_data->m_nLevel),
    //     static_cast<std::int64_t>(info.GetId()),
    //     static_cast<std::int64_t>(info.GetPartyRoutingType()),
    //     static_cast<std::int64_t>(info.GetMaxMemberLimit()),
    //     static_cast<std::int64_t>(m_partyRoom->GetMemberCount()),
    //     user->GetName(),
    //     accountName.c_str())
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        u"%s,%s,,,,",
        static_cast<std::int64_t>(0x358), static_cast<std::int64_t>(user->GetAccountID()),
        static_cast<std::int64_t>(user->GetDBID()), static_cast<std::int64_t>(position.x),
        static_cast<std::int64_t>(position.y), static_cast<std::int64_t>(position.z), title.c_str(),
        static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
        static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
        static_cast<std::int64_t>(info.GetId()), static_cast<std::int64_t>(info.GetPartyRoutingType()),
        static_cast<std::int64_t>(info.GetMaxMemberLimit()), static_cast<std::int64_t>(m_partyRoom->GetMemberCount()),
        user->GetName(), accountName.c_str()
    );
    return true;
}

CPartyRoomDeletedLogger::CPartyRoomDeletedLogger(CSPointer<CPartyRoom> room) : m_partyRoom(std::move(room)) {}

bool CPartyRoomDeletedLogger::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!m_partyRoom || !user) {
        return true;
    }
    CPartyRoomInfo& info = m_partyRoom->GetPartyRoomInfo();
    CLogFormatter formatter;
    formatter.WriteLog1_1(
        0x1faa, user.get(), nullptr, nullptr, nullptr, nullptr, info.GetId(), info.GetPartyRoutingType(),
        info.GetMaxMemberLimit(), m_partyRoom->GetMemberCount(), 0, 0, 0
    );
    return true;
}

CPartyRoomJoinedMemberResponse::CPartyRoomJoinedMemberResponse(CSPointer<CRoomMember> joinedUser) :
    m_joinedUser(joinedUser) {}

bool CPartyRoomJoinedMemberResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    {
        GuardHelper responseGuard(L"Respons_1", CThreadManager::GetThreadID());
        if (!m_joinedUser) {
            return false;
        }
    }

    const CSPointer<User> joinedUser = m_joinedUser->GetUser();
    {
        GuardHelper responseGuard(L"Respons_2", CThreadManager::GetThreadID());
        if (!joinedUser || !user) {
            return false;
        }
    }

    {
        GuardHelper responseGuard(L"Respons_3", CThreadManager::GetThreadID());
        if (user->Id() != joinedUser->Id()) {
            GuardHelper sendGuard(L"Respons_4", CThreadManager::GetThreadID());
            if (m_joinedUser->GetMemberType() < 3) {
                std::array<char, 0x5000> packet{};
                std::uint32_t packetLength = 0;
                InZoneRestrictionMap restrictions;
                joinedUser->GetAllInZoneRestriction(restrictions, nullptr);
                const int restrictionCount = static_cast<int>(restrictions.size());
                for (const auto& restriction : restrictions) {
                    packetLength += static_cast<std::uint32_t>(::Assemble(
                        packet.data() + packetLength, 0x5000 - static_cast<int>(packetLength), "d", restriction.first
                    ));
                }

                user->GetSocket()->Send(
                    "chddSdddddb", 0xfe, 10, 0, joinedUser->Id(), joinedUser->GetName(),
                    static_cast<int>(joinedUser->m_data->m_nClass), joinedUser->m_data->m_nLevel,
                    joinedUser->GetBBSLocation(), m_joinedUser->GetMemberType(), restrictionCount,
                    static_cast<int>(packetLength), packet.data()
                );
                user->GetSocket()->Send(
                    "chccS", 0x62, SystemMessageId::CHARACTER_ENTERED_PARTY_ROOM, 1, 0x0c, joinedUser->GetName()
                );
            } else {
                user->GetSocket()->Send(
                    "chddSdddd", 0xfe, 0x9f, 0, joinedUser->Id(), joinedUser->GetName(),
                    static_cast<int>(joinedUser->m_data->m_nClass), joinedUser->m_data->m_nLevel,
                    joinedUser->GetBBSLocation(), m_joinedUser->GetMemberType()
                );
                user->GetSocket()->Send(
                    "chccS", 0x62, SystemMessageId::CHARACTER_ENTERED_COMMAND_CHANNEL_MATCHING_ROOM, 1, 0x0c,
                    joinedUser->GetName()
                );
            }
        }
    }
    return true;
}

CPartyRoomDismissedResponse::CPartyRoomDismissedResponse(const bool mpccRoom) : m_isMPCCRoom(mpccRoom) {}

bool CPartyRoomDismissedResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (user && user->GetSocket()) {
        user->GetSocket()->Send("ch", 0xfe, m_isMPCCRoom ? 0x9e : 9);
    }
    return true;
}

CPartyRoomWithdrawedMemberResponse::CPartyRoomWithdrawedMemberResponse(CSPointer<User> withdrawnUser, const bool mpccRoom) :
    m_withdrawedUser(std::move(withdrawnUser)), m_bIsMPCCRoom(mpccRoom) {}

bool CPartyRoomWithdrawedMemberResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!m_withdrawedUser || !user || m_withdrawedUser->Id() == user->Id() || !m_withdrawedUser->m_data ||
        !user->GetSocket()) {
        return true;
    }

    if (!m_bIsMPCCRoom) {
        const auto memberInfoName = std::u16string(m_withdrawedUser->GetName());
        user->GetSocket()->Send(
            "chddSddddd", 0xfe, 10, 2, m_withdrawedUser->Id(), memberInfoName.c_str(),
            static_cast<int>(m_withdrawedUser->m_data->m_nClass), m_withdrawedUser->m_data->m_nLevel, 0, 0, 0
        );
        const auto systemMessageName = std::u16string(m_withdrawedUser->GetName());
        user->GetSocket()->Send(
            "chccS", 0x62, SystemMessageId::CHARACTER_LEFT_PARTY_ROOM, 1, 0x0c, systemMessageName.c_str()
        );
    } else {
        const auto memberInfoName = std::u16string(m_withdrawedUser->GetName());
        user->GetSocket()->Send(
            "chddSdddd", 0xfe, 0x9f, 2, m_withdrawedUser->Id(), memberInfoName.c_str(),
            static_cast<int>(m_withdrawedUser->m_data->m_nClass), m_withdrawedUser->m_data->m_nLevel, 0, 0
        );
        const auto systemMessageName = std::u16string(m_withdrawedUser->GetName());
        user->GetSocket()->Send(
            "chccS", 0x62, SystemMessageId::CHARACTER_LEFT_COMMAND_CHANNEL_MATCHING_ROOM, 1, 0x0c, systemMessageName.c_str()
        );
    }
    return true;
}

CPartyRoomChatResponse::CPartyRoomChatResponse(CSPointer<User> talker, const char16_t* message) :
    m_talker(std::move(talker)), m_msg(message) {}

bool CPartyRoomChatResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (m_talker && user) {
        user->Hear(m_talker.get(), m_talker->GetName(), m_msg, 0x0e);
    }
    return true;
}

CMPCCRoomChatResponse::CMPCCRoomChatResponse(CSPointer<User> talker, const char16_t* message) :
    m_talker(std::move(talker)), m_msg(message) {}

bool CMPCCRoomChatResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (m_talker && user) {
        user->Hear(m_talker.get(), m_talker->GetName(), m_msg, 0x15);
    }
    return true;
}

CPartyRoomMemberStateChangedResponse::CPartyRoomMemberStateChangedResponse(CSPointer<CRoomMember> member) :
    m_member(std::move(member)) {}

bool CPartyRoomMemberStateChangedResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    const CSPointer<User> member = m_member->GetUser();
    if (m_member->GetMemberType() < 3) {
        std::array<char, 0x5000> packet{};
        std::uint32_t packetLength = 0;
        InZoneRestrictionMap restrictions;
        member->GetAllInZoneRestriction(restrictions, nullptr);
        const int restrictionCount = static_cast<int>(restrictions.size());
        for (const auto& restriction : restrictions) {
            packetLength += static_cast<std::uint32_t>(
                ::Assemble(packet.data() + packetLength, 0x5000 - static_cast<int>(packetLength), "d", restriction.first)
            );
        }

        user->GetSocket()->Send(
            "chddSdddddb", 0xfe, 10, 1, member->Id(), member->GetName(), static_cast<int>(member->m_data->m_nClass),
            member->m_data->m_nLevel, member->GetBBSLocation(), m_member->GetMemberType(), restrictionCount,
            static_cast<int>(packetLength), packet.data()
        );
    } else {
        user->GetSocket()->Send(
            "chddSdddd", 0xfe, 0x9f, 1, member->Id(), member->GetName(), static_cast<int>(member->m_data->m_nClass),
            member->m_data->m_nLevel, member->GetBBSLocation(), m_member->GetMemberType()
        );
    }
    return true;
}

CPartyRoomInspecter::CPartyRoomInspecter(CPartyRoom::RoomMemberList& members) : m_memberList(members) {}

bool CPartyRoomInspecter::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    std::u16string members;
    for (const CSPointer<CRoomMember>& member : m_memberList) {
        if (!member) {
            continue;
        }
        const CSPointer<User> roomUser = member->GetUser();
        if (!roomUser) {
            continue;
        }
        members.append(roomUser->GetName());
        members.append(u"(");
        members.append(member->GetMemberTypeStr());
        members.append(u") ");
    }
    if (user && user->GetSocket()) {
        user->GetSocket()->SendSystemMessage(u"SYS", members);
    }
    return true;
}

CPartyRoomMemberListPacketMaker::CPartyRoomMemberListPacketMaker(CPartyRoom::RoomMemberList& members) :
    m_memberList(members) {}

void CPartyRoomMemberListPacketMaker::Make(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    for (const CSPointer<CRoomMember>& member : m_memberList) {
        if (!member) {
            continue;
        }
        const CSPointer<User> user = member->GetUser();
        if (!user) {
            continue;
        }

        const int userId = user->Id();
        auto* const userData = user->m_data;
        const int memberType = static_cast<int>(member->GetMemberType());
        const int bbsLocation = user->GetBBSLocation();
        const auto name = std::u16string(user->GetName());
        packet.Assemble(
            "dSdddd", userId, name.c_str(), static_cast<int>(userData->m_nClass), userData->m_nLevel, bbsLocation, memberType
        );
        if (member->GetMemberType() == 0 || member->GetMemberType() == 1 || member->GetMemberType() == 2) {
            InZoneRestrictionMap restrictions;
            user->GetAllInZoneRestriction(restrictions, nullptr);
            packet.Assemble("d", static_cast<int>(restrictions.size()));
            for (const auto& restriction : restrictions) {
                packet.Assemble("d", restriction.first);
            }
        }
    }
}

CPartyRoomListPacketMaker::CPartyRoomListPacketMaker(CPartyRoomRegistry::PartyRoomList& rooms) : m_roomList(rooms) {}

void CPartyRoomListPacketMaker::Make(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    for (const CSPointer<CPartyRoom>& room : m_roomList) {
        if (!room) {
            continue;
        }
        CPartyRoomInfo& info = room->GetPartyRoomInfo();
        const CSPointer<User> master = info.GetMasterUser();
        if (!master) {
            continue;
        }

        if (!info.IsCMPCCRoomInfo()) {
            {
                const CSPointer<User> packetMaster = info.GetMasterUser();
                const auto* const title = info.GetTitle().c_str();
                const int memberCount = room->GetMemberCount();
                const auto masterName = std::u16string(packetMaster->GetName());
                const int maxMemberLimit = info.GetMaxMemberLimit();
                const int lowerLevelLimit = info.GetLowerLevelLimit();
                const int upperLevelLimit = info.GetUpperLevelLimit();
                const int location = info.GetLocation();
                const int id = info.GetId();
                packet.Assemble(
                    "dSddddSd", id, title, location, upperLevelLimit, lowerLevelLimit, maxMemberLimit, masterName.c_str(),
                    memberCount
                );
            }

            CPartyRoom::RoomMemberList members;
            room->GetMemberList(members);
            for (const CSPointer<CRoomMember>& member : members) {
                if (!member) {
                    continue;
                }
                const CSPointer<User> roomUser = member->GetUser();
                if (!roomUser) {
                    continue;
                }
                const auto name = std::u16string(roomUser->GetName());
                packet.Assemble("dS", static_cast<int>(roomUser->m_data->m_nClass), name.c_str());
            }
        } else {
            const CSPointer<User> packetMaster = info.GetMasterUser();
            const auto* const title = info.GetTitle().c_str();
            const auto masterName = std::u16string(packetMaster->GetName());
            const int maxMemberLimit = info.GetMaxMemberLimit();
            const int memberCount = room->GetMemberCount();
            const int lowerLevelLimit = info.GetLowerLevelLimit();
            const int upperLevelLimit = info.GetUpperLevelLimit();
            const int location = info.GetLocation();
            const int id = info.GetId();
            packet.Assemble(
                "dSdddddS", id, title, location, upperLevelLimit, lowerLevelLimit, memberCount, maxMemberLimit,
                masterName.c_str()
            );
        }
    }
}

CPartyMPCCPartyMasterListPacketMaker::CPartyMPCCPartyMasterListPacketMaker(CPartyRoom::RoomMemberList& members) :
    m_memberList(members) {}

void CPartyMPCCPartyMasterListPacketMaker::Make(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    for (const CSPointer<CRoomMember>& member : m_memberList) {
        if (!member) {
            continue;
        }
        const CSPointer<User> user = member->GetUser();
        if (user) {
            const auto name = std::u16string(user->GetName());
            packet.Assemble("S", name.c_str());
        }
    }
}

CPartyRoomMemberListResponse::CPartyRoomMemberListResponse(CPartyRoom::RoomMemberList& members, const bool mpccRoom) :
    m_memberList(members), m_packetHelper(), m_bIsMPCCRoom(mpccRoom) {
    CPartyRoomMemberListPacketMaker(m_memberList).Make(m_packetHelper);
}

CSPointer<CRoomMember> CPartyRoomMemberListResponse::GetMember(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    const auto found = std::find_if(m_memberList.begin(), m_memberList.end(), [&user](const CSPointer<CRoomMember>& member) {
        return member && member->GetUser().get() == user.get();
    });
    return found == m_memberList.end() ? CSPointer<CRoomMember>() : *found;
}

bool CPartyRoomMemberListResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    const CSPointer<CRoomMember> member = GetMember(user);
    if (member && user && user->GetSocket()) {
        user->GetSocket()->Send(
            "chddb", 0xfe, m_bIsMPCCRoom ? 0xa0 : 8, static_cast<int>(member->GetMemberType()),
            static_cast<int>(m_memberList.size()), m_packetHelper.GetLength(), m_packetHelper.GetPacket()
        );
    }
    return true;
}

CPartyRoomListResponse::CPartyRoomListResponse(CPartyRoomListFilter& filter, CPartyRoomRegistry::PartyRoomList& rooms) :
    m_filter(filter), m_partyRoomList(rooms), m_packetHelper() {
    CPartyRoomListPacketMaker(m_partyRoomList).Make(m_packetHelper);
}

bool CPartyRoomListResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user || !user->GetSocket()) {
        return true;
    }
    const int page = std::max(m_filter.GetPage(), 1);
    CParty::Stat& statistics = CParty::Stat::inst();
    if (!m_filter.IsMPCCRoomFilter()) {
        user->GetSocket()->Send(
            "cddbdd", 0x9c, page, static_cast<int>(m_partyRoomList.size()), m_packetHelper.GetLength(),
            m_packetHelper.GetPacket(), statistics.GetTotalPartyCount(), statistics.GetTotalPartyMemberCount()
        );
    } else {
        user->GetSocket()->Send(
            "chddbdd", 0xfe, 0x9d, page, static_cast<int>(m_partyRoomList.size()), m_packetHelper.GetLength(),
            m_packetHelper.GetPacket(), statistics.GetTotalPartyCount(), statistics.GetTotalPartyMemberCount()
        );
    }
    return true;
}

CPartyMPCCPartyMasterListResponse::CPartyMPCCPartyMasterListResponse(CPartyRoom::RoomMemberList& members) :
    m_memberList(members), m_packetHelper() {
    CPartyMPCCPartyMasterListPacketMaker(m_memberList).Make(m_packetHelper);
}

bool CPartyMPCCPartyMasterListResponse::Respond(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (user && user->GetSocket()) {
        user->GetSocket()->Send(
            "chdb", 0xfe, 0xa3, static_cast<int>(m_memberList.size()), m_packetHelper.GetLength(), m_packetHelper.GetPacket()
        );
    }
    return true;
}

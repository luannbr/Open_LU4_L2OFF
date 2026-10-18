#include "OlympiadServiceProxy.h"
#include "Alliance.h"
#include "Area.h"
#include "Auth.h"
#include "CConfig.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "HtmlGenerator.h"
#include "NoblessSystem.h"
#include "PVPMatch.h"
#include "Pledge.h"
#include "PledgeGameSystem.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include "Shared/src/DateTime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace {

namespace CheckRegistrationResult {

enum Value : int {
    Success = 0,
    NotAlive = 1,
    NotActive = 2,
    NotEnoughSpaceOfInventory = 3,
    InTraining = 4,
    InvalidArgument = 5,
    SubJobCharacter = 6,
    NotEnoughLevel = 7,
    InvalidClass = 8,
    InPledgeGame = 9,
    InPvPMatch = 10,
    InFortressSiege = 11,
    InFishing = 12,
    InBeautyShop = 13,
    NoPoint = 14,
    CursedUser = 15,
    LimitGameRule = 16,
    LimitTotal = 17,
    NoNoblessInfo = 18,
    LimitEtc = 19,
    InRaid = 20,
};

} // namespace CheckRegistrationResult

GameValidCondition ToGameValidCondition(const CheckRegistrationResult::Value result) {
    // Current L2Server 0x009175ac.
    switch (result) {
        case CheckRegistrationResult::Success:
            return GVC_NONE;
        case CheckRegistrationResult::NotAlive:
            return GVC_ALIVE;
        case CheckRegistrationResult::NotActive:
            return GVC_ACTIVE;
        case CheckRegistrationResult::NotEnoughSpaceOfInventory:
            return GVC_INVENTORY;
        case CheckRegistrationResult::InTraining:
            return GVC_NOT_TRAINING;
        case CheckRegistrationResult::SubJobCharacter:
            return GVC_SUBJOB;
        case CheckRegistrationResult::InFishing:
            return GVC_FISHING;
        case CheckRegistrationResult::CursedUser:
            return GVC_NOT_CURSED;
        default:
            return GVC_VALID_TEAM;
    }
}

void SendOCA_PREPARE(const int userDatabaseId, const GameValidCondition condition) {
    // Current L2Server 0x00917edc.
    L2_GUARD_FUNCTION();
    olympiad::OCA_PREPARE packet{};
    packet.nUserDBID = userDatabaseId;
    packet.nGameValidCondition = static_cast<int>(condition);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OCA_PREPARE sent: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x551,
    //     userDatabaseId,
    //     static_cast<int>(condition))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OCA_PREPARE sent: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x551,
    //     userDatabaseId,
    //     static_cast<int>(condition))
    L2_LOG_INFO(
        Log, "Sending olympiad preparation response for character database ID %d with validation condition %d.",
        userDatabaseId, static_cast<int>(condition)
    );
    WOlympiadNetworkPeer::inst().SendPacket(OCA_PREPARE, packet);
}

CheckRegistrationResult::Value CheckRegistrationWithoutInventory(CSPointer<User> user, const GameRuleType) {
    // Current L2Server 0x0091da94. The current body does not use gameRule.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x66,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x66,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad registration check rejected: player is unavailable.");
        return CheckRegistrationResult::InvalidArgument;
    }

    CUserSocket* const socket = user->GetSocket();
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x67,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x67,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        L2_LOG_WARN(
            Log, "Olympiad registration check rejected for character database ID %u: player socket is unavailable.",
            static_cast<unsigned int>(user->GetDBID())
        );
        return CheckRegistrationResult::InvalidArgument;
    }
    if (socket->GetRaidStatus() != URS_NONE) {
        return CheckRegistrationResult::InRaid;
    }
    if (user->m_data->m_bIsAlive == 0) {
        return CheckRegistrationResult::NotAlive;
    }
    if (!user->m_data->m_bIsActive) {
        return CheckRegistrationResult::NotActive;
    }
    if (user->m_data->m_TrainingState == 1) {
        return CheckRegistrationResult::InTraining;
    }

    (void)CCursedWeaponMgr::inst();
    if (user->m_nCursedWeaponClassId > 0) {
        return CheckRegistrationResult::CursedUser;
    }
    if (user->GetBeautyShop()) {
        return CheckRegistrationResult::InBeautyShop;
    }
    if (CAreaDB::inst().IsFishingZone(user->GetPos(), 0)) {
        return CheckRegistrationResult::InFishing;
    }
    if (user->m_data->m_nSubJobId != 0 || user->m_data->m_IsDualClass) {
        return CheckRegistrationResult::SubJobCharacter;
    }
    if (user->m_data->m_nNoblessType != 1 && user->m_data->m_nNoblessType != 2) {
        return CheckRegistrationResult::NoNoblessInfo;
    }
    if (!WOlympiadDefaultSetting::inst().IsAllowedLevel(user->m_data->m_nLevel)) {
        return CheckRegistrationResult::NotEnoughLevel;
    }
    if (!WOlympiadStuff::IsAllowedClass(user->m_data->m_nClass)) {
        return CheckRegistrationResult::InvalidClass;
    }
    if (g_PledgeGameSystem.IsRegisteredUser(user.get()) || user->m_PledgeGameUser.IsRegistered() ||
        user->m_PledgeGameUser.IsPlayer()) {
        return CheckRegistrationResult::InPledgeGame;
    }
    if (user->IsInPVPMatchExcept(PVPMatchType::OLYMPIAD, PVPMatchPlayerState::NONE)) {
        return CheckRegistrationResult::InPvPMatch;
    }
    if (user->HasFlagOfSiege()) {
        return CheckRegistrationResult::InFortressSiege;
    }
    if (!user->GetNoblessInfo()) {
        return CheckRegistrationResult::NoNoblessInfo;
    }
    return CheckRegistrationResult::Success;
}

CheckRegistrationResult::Value CheckRegistration(CSPointer<User> user, const GameRuleType gameRule) {
    // Current L2Server 0x0091e2b8.
    L2_GUARD_FUNCTION();
    const CheckRegistrationResult::Value result = CheckRegistrationWithoutInventory(user, gameRule);
    if (result != CheckRegistrationResult::Success) {
        return result;
    }
    return WOlympiadStuff::HasEnoughSpaceInInventory(user) ? CheckRegistrationResult::Success
                                                           : CheckRegistrationResult::NotEnoughSpaceOfInventory;
}

std::u16string ToTargetUtf16(const wchar_t* value) {
    std::u16string result;
    if (!value) {
        return result;
    }
    while (*value != L'\0') {
        const std::uint32_t codePoint = static_cast<std::uint32_t>(*value++);
        if (codePoint <= 0xffffU) {
            result.push_back(static_cast<char16_t>(codePoint));
        } else {
            const std::uint32_t adjusted = codePoint - 0x10000U;
            result.push_back(static_cast<char16_t>(0xd800U + (adjusted >> 10U)));
            result.push_back(static_cast<char16_t>(0xdc00U + (adjusted & 0x3ffU)));
        }
    }
    return result;
}

template <std::size_t Capacity>
void CopyTargetUtf16(char16_t (&destination)[Capacity], std::u16string_view source) {
    static_assert(Capacity > 0);
    const std::size_t count = std::min(source.size(), Capacity - 1U);
    std::fill_n(destination, Capacity, u'\0');
    std::copy_n(source.data(), count, destination);
}

void GetPledgeInfo(CSPointer<User> user, int* pledgeDatabaseId, std::u16string* pledgeName, int* pledgeCrestId) {
    *pledgeDatabaseId = 0;
    *pledgeName = std::u16string();
    *pledgeCrestId = 0;
    if (!user || user->m_data->m_nPledgeID == 0) {
        return;
    }

    CSPointer<CPledge> pledge = CPledge::FindPledge(user->m_data->m_nWorldId, static_cast<int>(user->m_data->m_nPledgeID));
    if (!pledge) {
        return;
    }
    *pledgeDatabaseId = static_cast<int>(user->m_data->m_nPledgeID);
    const std::u16string targetName = std::u16string(pledge->GetPledgeName());
    *pledgeName = targetName.c_str();
    *pledgeCrestId = static_cast<int>(pledge->GetCrestId());
}

void GetAllianceInfo(CSPointer<User> user, int* allianceDatabaseId, std::u16string* allianceName, int* allianceCrestId) {
    *allianceDatabaseId = 0;
    *allianceName = std::u16string();
    *allianceCrestId = 0;
    if (!user) {
        return;
    }
    if (user->GetAllianceDbId() == 0) {
        return;
    }

    CSPointer<CAlliance> alliance =
        g_AllianceDbIdMap.FindObject(std::CDualKey<int, int>(user->m_data->m_nWorldId, user->GetAllianceDbId()));
    if (!alliance) {
        return;
    }
    *allianceDatabaseId = user->GetAllianceDbId();
    *allianceName = std::u16string(alliance->m_data->m_sAllianceName);
    *allianceCrestId = static_cast<int>(alliance->m_data->m_nCrestId);
}

void SendErrorMssage(CSPointer<User> user, short messageId) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x1c5,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x1c5,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(
            Log, "Cannot deliver olympiad error message %u: player is unavailable.",
            static_cast<unsigned int>(static_cast<unsigned short>(messageId))
        );
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x1c6,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x1c6,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(
            Log, "Cannot deliver olympiad error message %u to character database ID %u: player socket is unavailable.",
            static_cast<unsigned int>(static_cast<unsigned short>(messageId)), static_cast<unsigned int>(user->GetDBID())
        );
        return;
    }
    socket->SendSystemMessage_Char(static_cast<unsigned short>(messageId), user->GetName());
}

void HandleCheckRegisterationResult(CSPointer<User> user, const CheckRegistrationResult::Value result) {
    // Current L2Server 0x00918514.
    switch (result) {
        case CheckRegistrationResult::InvalidClass:
        case CheckRegistrationResult::NoNoblessInfo:
            SendErrorMssage(user, 0x5dd);
            return;
        case CheckRegistrationResult::InPledgeGame:
            SendErrorMssage(user, 0xdc1);
            return;
        case CheckRegistrationResult::InFortressSiege:
            SendErrorMssage(user, 0xada);
            return;
        case CheckRegistrationResult::InFishing:
            SendErrorMssage(user, 0x1078);
            return;
        case CheckRegistrationResult::InBeautyShop:
            SendErrorMssage(user, 0x101c);
            return;
        case CheckRegistrationResult::NoPoint:
            SendErrorMssage(user, 0xb7d);
            return;
        case CheckRegistrationResult::LimitGameRule:
            SendErrorMssage(user, 0xc99);
            return;
        case CheckRegistrationResult::LimitTotal:
            SendErrorMssage(user, 0xc98);
            return;
        case CheckRegistrationResult::InRaid:
            SendErrorMssage(user, 0xfcf);
            return;
        default:
            return;
    }
}

} // namespace

WOlympiadServiceNode::WOlympiadServiceNode(int worldId) : m_idWorld(worldId) {}

void WOlympiadServiceNode::Clear() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_Members.clear();
}

void WOlympiadServiceNode::Disconnect(int userDatabaseId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_Members.erase(userDatabaseId);
}

bool WOlympiadServiceNode::Connect(CSPointer<User> user) {
    // Current L2Server 0x0091b0d4.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0xe9,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0xe9,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad connection request rejected: player is unavailable.");
        return false;
    }

    olympiad::PkOlympiadAuthInfo authInfo{};
    {
        L2_WRITE_GUARD(AccountAuthInfoLock);
        const auto account = g_AccountAuthInfoMap.find(user->GetAccountID());
        if (account == g_AccountAuthInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]No account info : %d",
            //     L"OlympiadServiceProxy.cpp",
            //     0xf7,
            //     user->GetAccountID())
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]No account info : %d",
            //     L"OlympiadServiceProxy.cpp",
            //     0xf7,
            //     user->GetAccountID())
            L2_LOG_WARN(
                Log,
                "Olympiad connection request rejected for character database ID %u: authentication data for account ID %u "
                "is missing.",
                static_cast<unsigned int>(user->GetDBID()), static_cast<unsigned int>(user->GetAccountID())
            );
            return false;
        }
        WOlympiadStuff::Convert(&account->second, &authInfo);
    }

    authInfo.nAccountID = static_cast<int>(user->GetAccountID());
    CopyTargetUtf16(authInfo.sAccountName, std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size()));
    const std::u16string characterName = std::u16string(user->GetName());
    CopyTargetUtf16(authInfo.sCharName, std::u16string_view(characterName));
    authInfo.cCountryCode = static_cast<char>(user->m_data->m_CountryCode);

    authInfo.nPremiumLevel = user->GetSocket()->m_iPremiumLevel;
    authInfo.nX = static_cast<int>(user->GetPos().x);
    authInfo.nY = static_cast<int>(user->GetPos().y);
    authInfo.nZ = static_cast<int>(user->GetPos().z);
    authInfo.nDormantType = user->GetSocket()->GetDormantType();
    authInfo.tDormantMarkingTime = user->GetSocket()->GetDormantMarkingTime();

    if (!WOlympiadNetworkPeer::inst().IsAvailable()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Peer is not available: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x10e,
        //     m_idWorld)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Peer is not available: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x10e,
        //     m_idWorld)
        L2_LOG_WARN(Log, "Olympiad connection request to world %d rejected: network peer is unavailable.", m_idWorld);
        return false;
    }

    {
        L2_GUARD(m_Lock);
        if (m_Members.count(static_cast<int>(user->GetDBID())) != 0U) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]user already connected to world: %d, %d",
            //     L"OlympiadServiceProxy.cpp",
            //     0x117,
            //     m_idWorld,
            //     static_cast<int>(user->GetDBID()))
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]user already connected to world: %d, %d",
            //     L"OlympiadServiceProxy.cpp",
            //     0x117,
            //     m_idWorld,
            //     static_cast<int>(user->GetDBID()))
            L2_LOG_WARN(
                Log, "Olympiad connection request rejected: character database ID %u is already registered with world %d.",
                static_cast<unsigned int>(user->GetDBID()), m_idWorld
            );
            return false;
        }
    }

    olympiad::OCQ_CONNECT packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.nServerID = static_cast<int>(user->GetServerId());
    packet.authInfo = authInfo;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OCQ_CONNECT sent: %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x121,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OCQ_CONNECT sent: %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x121,
    //     packet.nUserDBID)
    L2_LOG_INFO(
        Log, "Sending olympiad connection request for character database ID %d to world %d.", packet.nUserDBID, m_idWorld
    );
    if (!WOlympiadNetworkPeer::inst().SendPacket(OCS_PROTOCOL::OCQ_CONNECT, packet)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Send is failed: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x125,
        //     m_idWorld)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Send is failed: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x125,
        //     m_idWorld)
        L2_LOG_ERROR(
            Log, "Olympiad connection request could not be sent for character database ID %d to world %d.", packet.nUserDBID,
            m_idWorld
        );
        return false;
    }

    user->ProcessMyParty(true);
    user->GetSocket()->AddRaidStatus(URS_REQUEST_RAID);
    user->m_RaidInfo.m_nRaidWorldId = m_idWorld;
    user->m_bIsSendUserInfoCompleted = false;
    user->m_bIsSendFactionInfoCompleted = false;
    {
        L2_GUARD(m_Lock);
        m_Members[static_cast<int>(user->GetDBID())] = user->GetServerId();
    }
    return true;
}

WOlympiadServiceProxy::WOlympiadServiceProxy() :
    IWOlympiadNetworkPeerCallback(),
    m_pServiceNode(nullptr),
    m_MatchResultCache{},
    m_tSeasonStart(0),
    m_nNominateSecond(0),
    m_nSeasonStartYear(0),
    m_nSeasonStartMonth(0),
    m_bMatchOpen(false) {}

WOlympiadServiceProxy::~WOlympiadServiceProxy() {
    m_Members.clear();
    delete m_pServiceNode;
    m_pServiceNode = nullptr;
}

void WOlympiadServiceProxy::OnCloseHost() { OnHostDisconnected(); }

bool WOlympiadServiceProxy::Init() {
    L2_GUARD_FUNCTION();
    delete m_pServiceNode;
    m_pServiceNode = new WOlympiadServiceNode(WOlympiadServiceConfig::inst().m_Server.idWorld);
    return true;
}

bool WOlympiadServiceProxy::IsMember(int userDatabaseId) const {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    return m_Members.find(userDatabaseId) != m_Members.end();
}

bool WOlympiadServiceProxy::IsPossibleToSetHero() const {
    L2_GUARD_FUNCTION();
    const int elapsed = static_cast<int>(std::time(nullptr)) - m_tSeasonStart;
    return elapsed > 300 && elapsed + 300 < m_nNominateSecond;
}

void WOlympiadServiceProxy::OnNotifyNoblessInfo(const olympiad::OSN_NOBLESS_INFO& packet) {
    L2_GUARD_FUNCTION();
    NoblessSystem::inst().SetNoblessInfoFromOlympiad(
        packet.nUserDBID, packet.nHeroType, packet.nWinCount, packet.nPreviousPoint, packet.nOlympiadPoint,
        packet.nMatchCount, packet.nOlympiadWinCount, packet.nOlympiadLoseCount, packet.nOlympiadTradePoint,
        packet.bNewSeason
    );
}

void WOlympiadServiceProxy::RequestClassRankByServer() {
    L2_GUARD_FUNCTION();
    olympiad::OCQ_CLASS_RANK_BY_SERVER packet{};
    packet.nReserved = 0;
    if (!Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCQ_CLASS_RANK_BY_SERVER, packet)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x502)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x502)
        L2_LOG_ERROR(Log, "Olympiad class-ranking request could not be sent.");
    }
}

void WOlympiadServiceProxy::UpdatePledgeInfo(int pledgeDatabaseId, int crestDatabaseId) {
    L2_GUARD_FUNCTION();
    olympiad::OCN_UPDATE_PLEDGE_INFO packet{};
    packet.nPledgeDBID = pledgeDatabaseId;
    packet.nCrestDBID = crestDatabaseId;
    Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCN_UPDATE_PLEDGE_INFO, packet);
}

bool WOlympiadServiceProxy::SendMatchListToUser(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3b4,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3b4,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad match-list request rejected: player is unavailable.");
        return false;
    }
    olympiad::OCQ_MATCH_LIST packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    const bool sent = Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCQ_MATCH_LIST, packet);
    if (!sent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3bb)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3bb)
        L2_LOG_ERROR(Log, "Olympiad match-list request could not be sent for character database ID %d.", packet.nUserDBID);
    }
    return sent;
}

bool WOlympiadServiceProxy::SendHeroListToUser(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3c9,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3c9,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad hero-list request rejected: player is unavailable.");
        return false;
    }
    olympiad::OCQ_HERO_LIST packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    const bool sent = Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCQ_HERO_LIST, packet);
    if (!sent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3d0)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3d0)
        L2_LOG_ERROR(Log, "Olympiad hero-list request could not be sent for character database ID %d.", packet.nUserDBID);
    }
    return sent;
}

bool WOlympiadServiceProxy::SendHeroMatchResult(CSPointer<User> user, const int classType, const int page) {
    // Current L2Server 0x00919afc.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x41f,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x41f,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad hero match-results request rejected: player is unavailable.");
        return false;
    }

    if (user->GetSocket() == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x420,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x420,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        L2_LOG_WARN(
            Log, "Olympiad hero match-results request rejected for character database ID %u: socket is unavailable.",
            static_cast<unsigned int>(user->GetDBID())
        );
        return false;
    }

    {
        L2_GUARD(m_MatchResultCache.m_Lock);
        const auto position = m_MatchResultCache.m_heroMatchResultMap.find(std::make_pair(classType, page));
        if (position != m_MatchResultCache.m_heroMatchResultMap.end()) {
            std::u16string html = position->second->Generate();
            CNpcHtmlSender(user->GetSocket()).SendHtml(html);
            return true;
        }
    }

    olympiad::OCQ_HERO_MATCH_RESULT packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.nClassType = classType;
    packet.nPage = page;
    if (!WOlympiadNetworkPeer::inst().SendPacket(OCS_PROTOCOL::OCQ_HERO_MATCH_RESULT, packet)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x437)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x437)
        L2_LOG_ERROR(
            Log, "Olympiad hero match-results request could not be sent for character database ID %d (class %d, page %d).",
            packet.nUserDBID, classType, page
        );
        return false;
    }
    return true;
}

bool WOlympiadServiceProxy::RegisterUser(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3dd,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3dd,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad registration request rejected: player is unavailable.");
        return false;
    }

    olympiad::OCQ_REGISTER packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.nClass = user->m_data->m_nClass;
    packet.sCharName = user->GetName();
    GetPledgeInfo(user, &packet.nPledgeDBID, &packet.sPledgeName, &packet.nPledgeCrestID);
    GetAllianceInfo(user, &packet.nAllianceDBID, &packet.sAllianceName, &packet.nAllianceCrestID);
    if (WOlympiadNetworkPeer::inst().SendPacket(OCS_PROTOCOL::OCQ_REGISTER, packet)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
    //     L"OlympiadServiceProxy.cpp",
    //     1000)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
    //     L"OlympiadServiceProxy.cpp",
    //     1000)
    L2_LOG_ERROR(Log, "Olympiad registration request could not be sent for character database ID %d.", packet.nUserDBID);
    return false;
}

void WOlympiadServiceProxy::RequestNoblessInfo(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3f5,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3f5,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Olympiad noblesse-info request rejected: player is unavailable.");
        return;
    }
    if (!WOlympiadStuff::IsAllowedUser(user)) {
        return;
    }
    olympiad::OCN_NOBLESS_INFO packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCN_NOBLESS_INFO, packet);
}

bool WOlympiadServiceProxy::SendMatchCountToUser(CSPointer<User> user, CSPointer<User> target) {
    // Current L2Server 0x00918b20.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4b2,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4b2,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad match-count request rejected: requesting player is unavailable.");
        return false;
    }
    if (!target) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4b3,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4b3,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(
            Log, "Olympiad match-count request rejected for character database ID %u: target player is unavailable.",
            static_cast<unsigned int>(user->GetDBID())
        );
        return false;
    }

    olympiad::OCQ_MATCH_COUNT packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.nTargetWorldID = target->m_data->m_nWorldId;
    packet.nTargetUserDBID = static_cast<int>(target->GetDBID());
    const bool sent = Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCQ_MATCH_COUNT, packet);
    if (!sent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4bc)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4bc)
        L2_LOG_ERROR(
            Log,
            "Olympiad match-count request could not be sent for character database ID %d (target character %d, target world "
            "%d).",
            packet.nUserDBID, packet.nTargetUserDBID, packet.nTargetWorldID
        );
    }
    return sent;
}

void WOlympiadServiceProxy::RequestLastMatchResult(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x47d,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x47d,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Olympiad last-match result request rejected: player is unavailable.");
        return;
    }
    olympiad::OCN_LAST_MATCH_RESULT packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCN_LAST_MATCH_RESULT, packet);
}

bool WOlympiadServiceProxy::ResetOlympiadPoint(CSPointer<User> user, int point) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x48e,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x48e,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad point-reset request rejected: player is unavailable (requested points %d).", point);
        return false;
    }
    olympiad::OCQ_RESET_OLYMPIAD_POINT packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.nPoint = std::max(0, point);
    return Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCQ_RESET_OLYMPIAD_POINT, packet);
}

bool WOlympiadServiceProxy::DeleteTradePoint(CSPointer<User> user, int tradePoint, std::uint32_t npcServerId) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x618,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x618,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(
            Log, "Olympiad trade-point deduction rejected: player is unavailable (points %d, NPC server ID %u).", tradePoint,
            static_cast<unsigned int>(npcServerId)
        );
        return false;
    }

    olympiad::OCQ_DELETE_TRADE_POINT packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.sCharName = user->GetName();
    packet.nAccountID = static_cast<int>(user->GetAccountID());
    packet.sAccountName = std::u16string(user->m_sAccountName.data());
    packet.nTradePoint = tradePoint;
    packet.nNPCSID = static_cast<int>(npcServerId);
    return Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCQ_DELETE_TRADE_POINT, packet);
}

bool WOlympiadServiceProxy::SendOlympiadRecordTo(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4ca,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4ca,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad record request rejected: player is unavailable.");
        return false;
    }
    olympiad::OCQ_OLYMPIAD_RECORD packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    const bool sent = Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCQ_OLYMPIAD_RECORD, packet);
    if (!sent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4d1)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4d1)
        L2_LOG_ERROR(Log, "Olympiad record request could not be sent for character database ID %d.", packet.nUserDBID);
    }
    return sent;
}

bool WOlympiadServiceProxy::SendOlympiadInfoTo(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4de,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x4de,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Cannot send olympiad information: player is unavailable.");
        return false;
    }
    CUserSocket* const socket = user->GetSocket();
    if (!socket) {
        return false;
    }
    protocol::S_EX_OLYMPIAD_INFO packet{};
    packet.bOpen = m_bMatchOpen;
    socket->Send(S_EX, S_EX_OLYMPIAD_INFO, packet);
    return true;
}

bool WOlympiadServiceProxy::SendDebugStringTo(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x50f,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x50f,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad debug-information request rejected: player is unavailable.");
        return false;
    }
    olympiad::OCQ_DEBUG_STRING packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    const bool sent = Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCQ_DEBUG_STRING, packet);
    if (!sent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x516)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x516)
        L2_LOG_ERROR(
            Log, "Olympiad debug-information request could not be sent for character database ID %d.", packet.nUserDBID
        );
    }
    return sent;
}

void WOlympiadServiceProxy::OnOlympiadRecord(const olympiad::OSA_OLYMPIAD_RECORD& source) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = WOlympiadStuff::FindUserByKey(std::CDualKey<int, int>(g_nWorldId, source.nUserDBID));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x5a9)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x5a9)
        L2_LOG_WARN(Log, "Olympiad record response ignored: character database ID %d is unavailable.", source.nUserDBID);
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (!socket) {
        return;
    }
    protocol::S_EX_OLYMPIAD_RECORD packet{};
    packet.nPoint = source.nPoint;
    packet.nWinCount = source.nWinCount;
    packet.nLoseCount = source.nLoseCount;
    packet.nMatchCount = source.nMatchCount;
    packet.nPrevClassType = source.nPrevClassType;
    packet.nPrevRank = source.nPrevRank;
    packet.nPrevRankCount = source.nPrevRankCount;
    packet.nPrevClassRank = source.nPrevClassRank;
    packet.nPrevClassRankCount = source.nPrevClassRankCount;
    packet.nPrevClassRankByServer = source.nPrevClassRankByServer;
    packet.nPrevClassRankByServerCount = source.nPrevClassRankByServerCount;
    packet.nPrevPoint = source.nPrevPoint;
    packet.nPrevWinCount = source.nPrevWinCount;
    packet.nPrevLoseCount = source.nPrevLoseCount;
    packet.nPrevGrade = source.nPrevGrade;
    packet.nSeasonYear = m_nSeasonStartYear;
    packet.nSeasonMonth = m_nSeasonStartMonth;
    packet.bMatchOpen = m_bMatchOpen;
    socket->Send(S_EX, S_EX_OLYMPIAD_RECORD, packet);
}

bool WOlympiadServiceProxy::BroadcastOlympiadInfo() {
    L2_GUARD_FUNCTION();
    protocol::S_EX_OLYMPIAD_INFO packet{};
    packet.bOpen = m_bMatchOpen;
    WOlympiadServiceProxyUtil::BroadcastPacket(S_EX_OLYMPIAD_INFO, packet);
    return true;
}

bool WOlympiadServiceProxy::SetHeroType(
    CSPointer<User> user, const int heroType, const std::uint32_t npcServerId, const bool byBuilder
) const {
    // Current L2Server 0x0091aa98.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_SET_HERO_TYPE packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.nHeroType = heroType;
    packet.nNPCSID = static_cast<int>(npcServerId);
    packet.sCharName = user->GetName();
    GetPledgeInfo(user, &packet.nPledgeDBID, &packet.sPledgeName, &packet.nPledgeCrestID);
    GetAllianceInfo(user, &packet.nAllianceDBID, &packet.sAllianceName, &packet.nAllianceCrestID);
    packet.bByBuilder = byBuilder;
    return Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCQ_SET_HERO_TYPE, packet);
}

void WOlympiadServiceProxy::OnOlympiadMatchingInfo(const olympiad::OSN_OLYMPIAD_MATCHING_INFO& packet) {
    L2_GUARD_FUNCTION();
    m_bMatchOpen = packet.bMatchOpen;
    BroadcastOlympiadInfo();
}

void WOlympiadServiceProxy::OnSetOlympiadTerm(int seasonStart, int nominateSecond) {
    m_tSeasonStart = seasonStart;
    m_nNominateSecond = nominateSecond;
    const L2DateTime start = L2DateTime::FromTime_t(seasonStart);
    m_nSeasonStartYear = start.m_nYear;
    m_nSeasonStartMonth = start.m_nMonth;
}

void WOlympiadServiceProxy::OnHostDisconnected() {
    L2_GUARD_FUNCTION();
    UnregisterAllMember();
    SharedOlympiadInfo::GetInfo()->m_bHostConnected = false;
    m_bMatchOpen = false;
    BroadcastOlympiadInfo();
}

void WOlympiadServiceProxy::UnregisterAllMember() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const auto& member : m_Members) {
        CSPointer<User> user = g_UserMap.FindObject(member.second);
        if (!user) {
            continue;
        }
        user->m_data->m_PVPMatchType = PVPMatchType::NONE;
        user->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::NONE;
        user->m_data->SetOlympidApplicant(false);
    }
    m_Members.clear();
    if (m_pServiceNode) {
        m_pServiceNode->Clear();
    }
}

void WOlympiadServiceProxy::RemoveMemberByID(int userDatabaseId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_Members.erase(userDatabaseId);
    if (m_pServiceNode) {
        m_pServiceNode->Disconnect(userDatabaseId);
    }
}

bool WOlympiadServiceProxy::ConnectGame(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user || !m_pServiceNode) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     user ? 0x17a : 0x179,
        //     user ? L"WO_ASSERT_NOT_NULL_RETURN" : L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     user ? 0x17a : 0x179,
        //     user ? L"WO_ASSERT_NOT_NULL_RETURN" : L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(
            Log, "Olympiad game connection rejected: player available %s, service node available %s.", user ? "yes" : "no",
            m_pServiceNode ? "yes" : "no"
        );
        return false;
    }
    return m_pServiceNode->Connect(user);
}

void WOlympiadServiceProxy::RegisterMember(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x274,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x274,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Cannot register olympiad membership: player is unavailable.");
        return;
    }
    L2_GUARD(m_Lock);
    if (m_Members.find(static_cast<int>(user->GetDBID())) == m_Members.end()) {
        m_Members[static_cast<int>(user->GetDBID())] = user->GetServerId();
    }
}

void WOlympiadServiceProxy::UnregisterMember(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x2e6,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x2e6,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Cannot unregister olympiad membership: player is unavailable.");
        return;
    }

    if (!IsMember(static_cast<int>(user->GetDBID())) && user->m_data->IsOlympiadApplicant()) {
        return;
    }

    user->m_data->m_PVPMatchType = PVPMatchType::NONE;
    user->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::NONE;
    user->m_data->SetOlympidApplicant(false);
    RemoveMemberByID(static_cast<int>(user->GetDBID()));
    WOlympiadStuff::Write_LOG_REMOVE_OLYMPIAD_ENTRY_InPeer(user);
}

bool WOlympiadServiceProxy::StopMatchMaking(CSPointer<User> user, const bool byDisconnect) {
    // Current L2Server 0x0091cd1c.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x255,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x255,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad matchmaking cancellation rejected: player is unavailable.");
        return false;
    }

    olympiad::OCQ_STOP packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.bByDisconnect = byDisconnect;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OCQ_STOP sent: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x25b,
    //     packet.nUserDBID,
    //     byDisconnect)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OCQ_STOP sent: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x25b,
    //     packet.nUserDBID,
    //     byDisconnect)
    L2_LOG_INFO(
        Log, "Sending olympiad matchmaking cancellation for character database ID %d (disconnect %s).", packet.nUserDBID,
        byDisconnect ? "yes" : "no"
    );

    if (WOlympiadNetworkPeer::inst().SendPacket(OCS_PROTOCOL::OCQ_STOP, packet)) {
        if (byDisconnect) {
            UnregisterMember(user);
        }
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]OCQ_STOP failed to send: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x25f,
    //     packet.nUserDBID,
    //     byDisconnect)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d]OCQ_STOP failed to send: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x25f,
    //     packet.nUserDBID,
    //     byDisconnect)
    L2_LOG_ERROR(
        Log,
        "Olympiad matchmaking cancellation could not be sent for character database ID %d (disconnect %s); removing local "
        "membership.",
        packet.nUserDBID, byDisconnect ? "yes" : "no"
    );
    UnregisterMember(user);
    return false;
}

bool WOlympiadServiceProxy::StartMatchMaking(CSPointer<User> user) {
    // Current L2Server 0x0091e41c.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x211,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x211,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad matchmaking request rejected: player is unavailable.");
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x214,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x214,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        L2_LOG_WARN(
            Log, "Olympiad matchmaking request rejected for character database ID %u: socket is unavailable.",
            static_cast<unsigned int>(user->GetDBID())
        );
        return false;
    }

    if (!WOlympiadNetworkPeer::inst().IsAvailable()) {
        socket->SendSystemMessage(u"SYS", u"Peer is not available");
        return false;
    }

    if (IsMember(static_cast<int>(user->GetDBID())) || user->m_data->IsOlympiadApplicant() ||
        !user->CanRegisterAtOlympiad()) {
        SendErrorMssage(user, 0x5de);
        return false;
    }

    const CheckRegistrationResult::Value result = CheckRegistration(user, GRT_CLASSLESS);
    if (result != CheckRegistrationResult::Success) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Failed to register: %d, %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x229,
        //     static_cast<int>(user->GetDBID()),
        //     static_cast<int>(result))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Failed to register: %d, %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x229,
        //     static_cast<int>(user->GetDBID()),
        //     static_cast<int>(result))
        L2_LOG_WARN(
            Log, "Olympiad matchmaking eligibility check failed for character database ID %u: result %d.",
            static_cast<unsigned int>(user->GetDBID()), static_cast<int>(result)
        );
        HandleCheckRegisterationResult(user, result);
        return false;
    }

    olympiad::OCQ_START packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.Application.sCharName = std::u16string(user->GetName());
    packet.Application.cGameRule = static_cast<char>(GRT_CLASSLESS);
    packet.Application.nAccountID = static_cast<int>(user->GetAccountID());
    packet.Application.sAccountName = std::u16string(user->m_sAccountName.data());
    packet.Application.nClassType = static_cast<int>(user->m_data->m_nClass);
    packet.Application.nLevel = user->m_data->m_nLevel;
    packet.Application.nRace = static_cast<int>(user->m_data->m_Race);
    packet.Application.nSex = static_cast<int>(user->m_data->m_Sex);

    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OCQ_START sent: %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x23b,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OCQ_START sent: %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x23b,
    //     packet.nUserDBID)
    L2_LOG_INFO(Log, "Sending olympiad matchmaking request for character database ID %d.", packet.nUserDBID);
    if (!WOlympiadNetworkPeer::inst().SendPacket(OCS_PROTOCOL::OCQ_START, packet)) {
        socket->SendSystemMessage(u"SYS", u"Peer is not available");
        return false;
    }

    RegisterMember(user);
    user->m_data->SetOlympidApplicant(true);
    user->m_data->m_PVPMatchType = PVPMatchType::OLYMPIAD;
    user->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::WAITING;
    WOlympiadStuff::Write_LOG_ADD_OLYMPIAD_ENTRY_InPeer(user, GRT_CLASSLESS);
    return true;
}

bool WOlympiadServiceProxy::HandleCommand(const int userDatabaseId, const std::vector<std::u16string>& params) {
    // Current L2Server 0x0091eb28.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_BCMD packet{};
    packet.nUserDBID = userDatabaseId;
    for (const std::u16string& value : params) {
        packet.vParams.push_back(value);
    }

    if (!Singleton<WOlympiadNetworkPeer>::inst().SendPacket(OCS_PROTOCOL::OCQ_BCMD, packet)) {
        return false;
    }
    if (!params.empty() && runtime::StringView(params.data()[0]) == u"reset") {
        UnregisterAllMember();
    }
    return true;
}

void WOlympiadServiceProxy::ReloadUserIfReturnedCompletely(CSPointer<User> user) {
    // Current L2Server 0x0091c140.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x324,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x324,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Character reload after olympiad return cannot continue: player is unavailable.");
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (!socket) {
        return;
    }

    {
        L2_GUARD(*user->m_lock);
        if (!socket->IsCompletelyReturned()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Invalid Raid User Socket Closed: %d, %d",
            //     L"OlympiadServiceProxy.cpp",
            //     0x332,
            //     user->m_RaidInfo.m_nRaidWorldId,
            //     user->GetDBID())
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Invalid Raid User Socket Closed: %d, %d",
            //     L"OlympiadServiceProxy.cpp",
            //     0x332,
            //     user->m_RaidInfo.m_nRaidWorldId,
            //     user->GetDBID())
            L2_LOG_ERROR(
                Log,
                "Closing player connection: olympiad return is incomplete for character database ID %u (raid world %d).",
                static_cast<unsigned int>(user->GetDBID()), user->m_RaidInfo.m_nRaidWorldId
            );
            socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            return;
        }

        if (!socket->SetReloadState()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]SetReloadState failed and Socket Closed: %d, %d",
            //     L"OlympiadServiceProxy.cpp",
            //     0x339,
            //     user->m_RaidInfo.m_nRaidWorldId,
            //     user->GetDBID())
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]SetReloadState failed and Socket Closed: %d, %d",
            //     L"OlympiadServiceProxy.cpp",
            //     0x339,
            //     user->m_RaidInfo.m_nRaidWorldId,
            //     user->GetDBID())
            L2_LOG_ERROR(
                Log,
                "Closing player connection: reload-state transition failed after olympiad return for character database ID "
                "%u (raid world %d).",
                static_cast<unsigned int>(user->GetDBID()), user->m_RaidInfo.m_nRaidWorldId
            );
            socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            return;
        }

        socket->SetRaidStatus(URS_NONE);
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]User will be reloaded: %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x341,
    //     user->GetDBID())
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]User will be reloaded: %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x341,
    //     user->GetDBID())
    L2_LOG_INFO(
        Log, "Preparing character reload after olympiad return for database ID %u.",
        static_cast<unsigned int>(user->GetDBID())
    );
    UnregisterMember(user);
    socket->IncUserCount(false);
    const FVector origin = user->m_RaidInfo.m_OriginPos;
    db.ReloadCharacter(*user.get(), static_cast<int>(origin.x), static_cast<int>(origin.y), static_cast<int>(origin.z));
}

void WOlympiadServiceProxy::RestartUserIfCompletely(CSPointer<User> user) {
    // Current L2Server 0x0091d448.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x5e2,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x5e2,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Olympiad restart cannot continue: player is unavailable.");
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x5e8)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x5e8)
        L2_LOG_WARN(
            Log, "Olympiad restart cannot continue for character database ID %u: socket is unavailable.",
            static_cast<unsigned int>(user->GetDBID())
        );
        return;
    }

    {
        L2_GUARD(*user->m_lock);
        if (!socket->IsReadyToRestart()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Is not ready for restarting: %d, %d",
            //     L"OlympiadServiceProxy.cpp",
            //     0x5f1,
            //     static_cast<int>(user->GetDBID()),
            //     static_cast<int>(socket->GetRaidStatus()))
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Is not ready for restarting: %d, %d",
            //     L"OlympiadServiceProxy.cpp",
            //     0x5f1,
            //     static_cast<int>(user->GetDBID()),
            //     static_cast<int>(socket->GetRaidStatus()))
            L2_LOG_WARN(
                Log, "Olympiad restart not ready for character database ID %u: raid status %d.",
                static_cast<unsigned int>(user->GetDBID()), static_cast<int>(socket->GetRaidStatus())
            );
            return;
        }

        socket->SetRaidStatus(URS_NONE);
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]S_EX_RESTART_RESPONSE sent: %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x5f8,
    //     static_cast<int>(user->GetDBID()))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]S_EX_RESTART_RESPONSE sent: %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x5f8,
    //     static_cast<int>(user->GetDBID()))
    L2_LOG_INFO(
        Log, "Sending olympiad restart response for character database ID %u.", static_cast<unsigned int>(user->GetDBID())
    );
    socket->Send("ch", 0xfe, S_EX_RESTART_RESPONSE);
    if (static_cast<int>(socket->GetAccountID()) > 0) {
        g_UserSocketMapEnterChar.DeleteObject(
            static_cast<int>(socket->GetAccountID()), const_cast<char*>("OlympiadServiceProxy.cpp"), 0x600
        );
    }
    UnregisterMember(user);
    user->SendLogout();
    socket->m_nCloseReason = SCR_RAID_QUIT;
    {
        L2_GUARD(socket->mLock);
        socket->UnBindUser();
    }
    socket->IncUserCount(false);
    db.RequestLoadChar(user->GetAccountID(), socket);
}

void WOlympiadServiceProxy::OnConnectResult(int userDatabaseId, bool success, bool asPlayer) {
    // Current L2Server 0x0091c900.
    L2_GUARD_FUNCTION();
    if (!m_pServiceNode) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x190,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x190,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(
            Log, "Olympiad connection result ignored for character database ID %d: service node is unavailable.",
            userDatabaseId
        );
        return;
    }

    CSPointer<User> user = WOlympiadStuff::FindUserByKey(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x196,
        //     userDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x196,
        //     userDatabaseId)
        L2_LOG_WARN(Log, "Olympiad connection result ignored: character database ID %d is unavailable.", userDatabaseId);
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Invalid socket: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x19e,
        //     userDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Invalid socket: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x19e,
        //     userDatabaseId)
        L2_LOG_WARN(
            Log, "Olympiad connection result ignored for character database ID %d: socket is unavailable.", userDatabaseId
        );
        return;
    }

    if (!success) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]S_EX_RAID_RESERVE_RESULT sent: %d, 0",
        //     L"OlympiadServiceProxy.cpp",
        //     0x1a4,
        //     userDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]S_EX_RAID_RESERVE_RESULT sent: %d, 0",
        //     L"OlympiadServiceProxy.cpp",
        //     0x1a4,
        //     userDatabaseId)
        L2_LOG_WARN(
            Log,
            "Olympiad connection declined for character database ID %d; sending failure response and removing local "
            "membership.",
            userDatabaseId
        );
        socket->Send("chd", 0xfe, 0xb7, 0);
        socket->SendSystemMessage(u"SYS", u"Olympiad server is not available");
        socket->SetRaidStatus(URS_NONE);
        user->m_RaidInfo.m_nRaidWorldId = 0;
        UnregisterMember(user);
        return;
    }

    user->DespawnAllSummon(true, false);
    user->TryDismounting(true, false);
    if (asPlayer) {
        user->DispelByBuffProtectionFlag(2);
    }
    user->DespawnPetByOlympiadInPeer();
}

bool WOlympiadServiceProxy::ObserveMatch(CSPointer<User> user, const int fieldId) {
    // Current L2Server 0x0091ed2c.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x36d,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x36d,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Olympiad observation rejected: player is unavailable (field %d).", fieldId);
        return false;
    }

    if (user->GetSocket() == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x36e,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x36e,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        L2_LOG_WARN(
            Log, "Olympiad observation rejected for character database ID %u: socket is unavailable.",
            static_cast<unsigned int>(user->GetDBID())
        );
        return false;
    }

    if (static_cast<unsigned int>(fieldId) >= static_cast<unsigned int>(SharedOlympiadInfo::FIELD_COUNT)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid field id: %d, %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x372,
        //     user->GetDBID(),
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid field id: %d, %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x372,
        //     user->GetDBID(),
        //     fieldId)
        L2_LOG_WARN(
            Log, "Olympiad observation rejected for character database ID %u: field %d is outside 0..%d.",
            static_cast<unsigned int>(user->GetDBID()), fieldId, static_cast<int>(SharedOlympiadInfo::FIELD_COUNT) - 1
        );
        return false;
    }

    if (!WOlympiadNetworkPeer::inst().IsAvailable()) {
        user->GetSocket()->SendSystemMessage(u"SYS", u"Peer is not available");
        return false;
    }

    if (IsMember(static_cast<int>(user->GetDBID())) || user->m_data->IsOlympiadApplicant() ||
        !user->CanRegisterAtOlympiad()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_OLYMPIAD_WHILE_REGISTERED);
        return false;
    }

    if (user->GetSocket()->GetRaidStatus() != URS_NONE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]User is in raid: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x388,
        //     user->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]User is in raid: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x388,
        //     user->GetDBID())
        L2_LOG_WARN(
            Log, "Olympiad observation rejected for character database ID %u: raid status is active (%d).",
            static_cast<unsigned int>(user->GetDBID()), static_cast<int>(user->GetSocket()->GetRaidStatus())
        );
        return false;
    }

    (void)CCursedWeaponMgr::inst();
    if (user->m_nCursedWeaponClassId > 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]cursed user: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x38f,
        //     user->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]cursed user: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x38f,
        //     user->GetDBID())
        L2_LOG_WARN(
            Log, "Olympiad observation rejected for character database ID %u: cursed weapon class %d is active.",
            static_cast<unsigned int>(user->GetDBID()), user->m_nCursedWeaponClassId
        );
        return false;
    }

    if (g_PledgeGameSystem.IsRegisteredUser(user.get())) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_OLYMPIAD_WHILE_REGISTERED);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]in pledge game: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x397,
        //     user->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]in pledge game: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x397,
        //     user->GetDBID())
        L2_LOG_WARN(
            Log, "Olympiad observation rejected for character database ID %u: player is registered in a clan game.",
            static_cast<unsigned int>(user->GetDBID())
        );
        return false;
    }

    olympiad::OCQ_OBSERVE packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.nFieldID = fieldId;
    packet.nAccountID = static_cast<int>(user->GetAccountID());
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OCQ_OBSERVE sent: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x3a0,
    //     packet.nUserDBID,
    //     packet.nFieldID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OCQ_OBSERVE sent: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x3a0,
    //     packet.nUserDBID,
    //     packet.nFieldID)
    L2_LOG_INFO(
        Log, "Sending olympiad observation request for character database ID %d, field %d.", packet.nUserDBID,
        packet.nFieldID
    );
    if (!WOlympiadNetworkPeer::inst().SendPacket(OCS_PROTOCOL::OCQ_OBSERVE, packet)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3a4)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x3a4)
        L2_LOG_ERROR(
            Log, "Olympiad observation request could not be sent for character database ID %d, field %d.", packet.nUserDBID,
            packet.nFieldID
        );
        return false;
    }

    RegisterMember(user);
    user->m_data->SetOlympidApplicant(true);
    return true;
}

void WOlympiadServiceProxy::OnPrepareAsPlayer(const int userDatabaseId) const {
    // Current L2Server 0x0091f424.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = WOlympiadStuff::FindUserByKey(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x582,
        //     userDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x582,
        //     userDatabaseId)
        L2_LOG_WARN(
            Log,
            "Olympiad preparation cannot validate character database ID %d: player is unavailable; replying with team "
            "validation failure.",
            userDatabaseId
        );
        SendOCA_PREPARE(userDatabaseId, GVC_VALID_TEAM);
        return;
    }

    const CheckRegistrationResult::Value result = CheckRegistrationWithoutInventory(user, GRT_CLASSLESS);
    if (result == CheckRegistrationResult::Success) {
        SendOCA_PREPARE(userDatabaseId, GVC_NONE);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Check failed: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x58b,
    //     userDatabaseId,
    //     static_cast<int>(result))
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Check failed: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x58b,
    //     userDatabaseId,
    //     static_cast<int>(result))
    L2_LOG_WARN(
        Log, "Olympiad preparation check failed for character database ID %d: registration result %d.", userDatabaseId,
        static_cast<int>(result)
    );
    SendOCA_PREPARE(userDatabaseId, ToGameValidCondition(result));
}

void WOlympiadServiceProxy::OnHeroMatchResult(const olympiad::OSA_HERO_MATCH_RESULT& packet) {
    // Current L2Server 0x0091f77c.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = WOlympiadStuff::FindUserByKey(std::CDualKey<int, int>(g_nWorldId, packet.nUserDBID));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     1099)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     1099)
        L2_LOG_WARN(
            Log, "Olympiad hero match-results response ignored: character database ID %d is unavailable.", packet.nUserDBID
        );
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x453)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x453)
        L2_LOG_WARN(
            Log,
            "Olympiad hero match-results response cannot be delivered to character database ID %d: socket is unavailable.",
            packet.nUserDBID
        );
        return;
    }

    const char16_t* const templateFileName = g_nCountryCode == 5 ? u"hero_match_thailand.htm" : u"hero_match.htm";
    // The current body leaves this process-owned builder
    // allocated after sending the generated page.
    CHtmlGenerator* const generator = new CHtmlGenerator(templateFileName);

    std::u16string value = FormatString(u"%d", packet.nWinCount);
    generator->AddValue(u"total_win", value);
    value = FormatString(u"%d", packet.nDrawCount);
    generator->AddValue(u"total_draw", value);
    value = FormatString(u"%d", packet.nLoseCount);
    generator->AddValue(u"total_lose", value);

    int gameRule = 0;
    for (const olympiad::PkHeroMatchResult& result : packet.vList) {
        const std::tm* const matchTime = runtime::crt::LocalTime32(result.nMatchTime);

        if (gameRule != result.nGameRule) {
            std::u16string groupTemplate = generator->GetTemplet(result.nGameRule + 5);
            CLocalHtmlGenerator groupGenerator(groupTemplate);
            generator->AddValue(u"MATCH_LIST", groupGenerator);
            gameRule = result.nGameRule;
        }

        std::u16string matchTemplate = generator->GetTemplet(result.nTempleteNumber);
        CLocalHtmlGenerator matchGenerator(matchTemplate);

        value = FormatString(u"%04d", matchTime->tm_year + 1900);
        matchGenerator.AddValue(u"year", value);
        value = FormatString(u"%02d", matchTime->tm_mon + 1);
        matchGenerator.AddValue(u"month", value);
        value = FormatString(u"%02d", matchTime->tm_mday);
        matchGenerator.AddValue(u"day", value);
        value = FormatString(u"%02d", matchTime->tm_hour);
        matchGenerator.AddValue(u"hour", value);
        value = FormatString(u"%02d", matchTime->tm_min);
        matchGenerator.AddValue(u"min", value);

        std::u16string opponent = result.sRivalName;
        matchGenerator.AddValue(u"opponent", opponent);
        value = FormatString(u"%d", result.nRivalClassID);
        matchGenerator.AddValue(u"classID", value);
        value = FormatString(u"%d", result.nElapsedTime / 60);
        matchGenerator.AddValue(u"time_min", value);
        value = FormatString(u"%02d", ((result.nElapsedTime % 60) / 10) * 10);
        matchGenerator.AddValue(u"time_sec", value);
        value = FormatString(u"%d", result.nWinCount);
        matchGenerator.AddValue(u"win", value);
        value = FormatString(u"%d", result.nDrawCount);
        matchGenerator.AddValue(u"draw", value);
        value = FormatString(u"%d", result.nLoseCount);
        matchGenerator.AddValue(u"lose", value);
        generator->AddValue(u"MATCH_LIST", matchGenerator);
    }

    if (packet.nPage > 1) {
        std::u16string previousTemplate = generator->GetTemplet(4);
        CLocalHtmlGenerator previousGenerator(previousTemplate);
        value = FormatString(u"bypass _match?class=%d&page=%d", packet.nClassType, packet.nPage - 1);
        previousGenerator.AddValue(u"action", value);
        generator->AddValue(u"PREV", previousGenerator);
    }

    if (packet.bHasNextPage) {
        std::u16string nextTemplate = generator->GetTemplet(5);
        CLocalHtmlGenerator nextGenerator(nextTemplate);
        value = FormatString(u"bypass _match?class=%d&page=%d", packet.nClassType, packet.nPage + 1);
        nextGenerator.AddValue(u"action", value);
        generator->AddValue(u"NEXT", nextGenerator);
    }

    std::u16string html = generator->Generate();
    CNpcHtmlSender(socket).SendHtml(html);
}

void WOlympiadServiceProxy::OnStartResult(const olympiad::OSA_START& packet) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = WOlympiadStuff::FindUserByKey(std::CDualKey<int, int>(g_nWorldId, packet.nUserDBID));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x28b,
        //     packet.nUserDBID)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d",
        //     L"OlympiadServiceProxy.cpp",
        //     0x28b,
        //     packet.nUserDBID)
        L2_LOG_WARN(
            Log, "Olympiad matchmaking response has no player for character database ID %d; removing local membership.",
            packet.nUserDBID
        );
        RemoveMemberByID(packet.nUserDBID);
        return;
    }
    if (packet.cResult == 0) {
        user->SendSystemMessage(SystemMessageId::WORLD_OLYMPIAD_WAITING_LIST_REGISTERED);
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]Start failed: %d, %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x294,
    //     packet.nUserDBID,
    //     static_cast<int>(packet.cResult),
    //     packet.nMessageNo)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d]Start failed: %d, %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x294,
    //     packet.nUserDBID,
    //     static_cast<int>(packet.cResult),
    //     packet.nMessageNo)
    L2_LOG_WARN(
        Log, "Olympiad matchmaking request declined for character database ID %d: result %d, response message ID %d.",
        packet.nUserDBID, static_cast<int>(packet.cResult), packet.nMessageNo
    );
    if (packet.cResult == 2) {
        SendErrorMssage(user, 0x5de);
        return;
    }
    SendErrorMssage(user, static_cast<short>(packet.nMessageNo));
    UnregisterMember(user);
}

void WOlympiadServiceProxy::OnStopResult(const olympiad::OSA_STOP& packet) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = WOlympiadStuff::FindUserByKey(std::CDualKey<int, int>(g_nWorldId, packet.nUserDBID));
    if (!user) {
        RemoveMemberByID(packet.nUserDBID);
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (!socket) {
        return;
    }
    if (packet.cResult == 0) {
        socket->SendSystemMessage(SystemMessageId::REMOVED_FROM_OLYMPIAD_WAITING_LIST);
        UnregisterMember(user);
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]OnStopReult failed: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x2c5,
    //     packet.nUserDBID,
    //     static_cast<int>(packet.cResult))
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]OnStopReult failed: %d, %d",
    //     L"OlympiadServiceProxy.cpp",
    //     0x2c5,
    //     packet.nUserDBID,
    //     static_cast<int>(packet.cResult))
    L2_LOG_WARN(
        Log, "Olympiad matchmaking cancellation declined for character database ID %d: result %d.", packet.nUserDBID,
        static_cast<int>(packet.cResult)
    );
    if (packet.cResult == 1) {
        socket->SendSystemMessage(SystemMessageId::NOT_REGISTERED_FOR_OLYMPIAD);
    } else if (packet.cResult == 2) {
        socket->SendSystemMessage(SystemMessageId::WAITING_FOR_MATCH_START);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x2d2)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x2d2)
        L2_LOG_ERROR(
            Log,
            "Olympiad cancellation response has unsupported result %d for character database ID %d; no client error message "
            "is defined.",
            static_cast<int>(packet.cResult), packet.nUserDBID
        );
    }
}

bool WOlympiadServiceProxyUtil::IsHero(const int characterDatabaseId) {
    L2_GUARD_FUNCTION();
    const SharedNoblessInfo* const info = Singleton<NoblessSystem>::inst().GetNoblessInfo(characterDatabaseId);
    return info != nullptr && HeroType_IsHero(info->nHeroType);
}

void WOlympiadServiceProxyUtil::ResetRankInfo(ClassType classType) {
    L2_GUARD_FUNCTION();
    SharedOlympiadInfo* const info = SharedOlympiadInfo::GetInfo();
    const int classIndex = OlympiadDefine::GetHeroClassIndex(classType);
    if (classIndex != -1) {
        info->arrRankingCount[classIndex] = 0;
    }
}

void WOlympiadServiceProxyUtil::AddRankInfo(ClassType classType, int ranking, const wchar_t* name, int point) {
    L2_GUARD_FUNCTION();
    const std::u16string targetName = ToTargetUtf16(name);
    static_assert(sizeof(SharedUtilsWideChar) == sizeof(char16_t));
    SharedOlympiadInfo::GetInfo()->AddRankInfo(
        classType, ranking, name != nullptr ? reinterpret_cast<const SharedUtilsWideChar*>(targetName.c_str()) : nullptr,
        point
    );
}

void WOlympiadServiceProxyUtil::RegisterToOlympiad(const CSPointer<User> user) {
    // Current L2Server 0x0091f5fc.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x67e,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadServiceProxy.cpp",
        //     0x67e,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Olympiad registration helper cannot continue: player is unavailable.");
        return;
    }

    if (Singleton<WOlympiadServiceConfig>::inst().IsHost()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x684)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadServiceProxy.cpp",
        //     0x684)
        L2_LOG_WARN(
            Log,
            "Olympiad registration helper rejected character database ID %u: this operation is unavailable in host mode.",
            static_cast<unsigned int>(user->GetDBID())
        );
        return;
    }

    if (WOlympiadStuff::IsAllowedUser(user)) {
        WOlympiadServiceProxy::inst().RegisterUser(user);
    }
}

void WOlympiadServiceProxyUtil::RegisterToOlympiad(const int userDatabaseId) {
    // Current L2Server 0x00920038.
    L2_GUARD_FUNCTION();
    const CSPointer<User> user = WOlympiadStuff::FindUserByKey(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
    if (user) {
        RegisterToOlympiad(user);
    }
}

void WOlympiadServiceProxyUtil::BroadcastPacket(S_EX_PROTOCOL protocol, const network::IPacket& packet) {
    L2_GUARD_FUNCTION();
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        if (user->IsInRealWorld()) {
            user->GetSocket()->Send(S_EX, protocol, const_cast<network::IPacket&>(packet));
        }
    });
}

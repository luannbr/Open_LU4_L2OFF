#include "UserPacketEx.h"
#include "Agit.h"
#include "AgitAuctionPacketHandler.h"
#include "AirShip.h"
#include "Auction.h"
#include "BeautyManager.h"
#include "BlockUpset.h"
#include "BotChecker.h"
#include "BuilderCmdHandler.h"
#include "CConfig.h"
#include "CWorld.h"
#include "Captcha.h"
#include "CardCollectSystem.h"
#include "Castle.h"
#include "Cleft.h"
#include "CommissionTrade.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "DBPacketEx.h"
#include "DecoNpcData.h"
#include "Dominion.h"
#include "DuelUserInfo.h"
#include "DynamicQuest.h"
#include "EventCampaign.h"
#include "EventMatch.h"
#include "FieldCycle.h"
#include "FlyMove.h"
#include "Fortress.h"
#include "Friends.h"
#include "GameEventSystem.h"
#include "HTMLCache.h"
#include "IPBlock.h"
#include "InstantAgit.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "L2Packet.h"
#include "LuckyGameSystem.h"
#include "Manor.h"
#include "Mentor.h"
#include "MultiPartyCommandChannelManager.h"
#include "MultiPartyCommandChannelRegistry.h"
#include "NPC.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "OlympiadService.h"
#include "OlympiadServiceProxy.h"
#include "OwnThing.h"
#include "PVPMatch.h"
#include "Party.h"
#include "PartyMatchingRequest.h"
#include "PartyMatchingWaitingRoom.h"
#include "PartyRoomInfo.h"
#include "PartyRoomListFilter.h"
#include "PartyRoomRegistry.h"
#include "PetitionCli.h"
#include "Pledge.h"
#include "PledgeContribution.h"
#include "PledgeFunction.h"
#include "PledgeGameSystem.h"
#include "PledgeItem.h"
#include "PledgeJoinSystem.h"
#include "PledgeLevel.h"
#include "PledgeMastery.h"
#include "PledgeRaidNetwork.h"
#include "PledgeSkill.h"
#include "PledgeStuff.h"
#include "Post.h"
#include "PremiumService.h"
#include "Protocol/src/SystemMessageIds.h"
#include "QueueTicket.h"
#include "Raid.h"
#include "ReplyChecker.h"
#include "Residence.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shuttle.h"
#include "SkillEnchant.h"
#include "User.h"
#include "UserBookmark.h"
#include "UserSocket.h"

#include "Auth.h"
#include "BRShopServer.h"
#include "Default.h"
#include "Duel.h"
#include "External/NCGuardServerLibCompat.h"
#include "Fishing.h"
#include "OlympiadStuff.h"
#include "Pet.h"
#include "Portable/ClientProtocolRuntime.h"
#include "VariationDB.h"
#include "VipAttendance.h"

#include "Skill.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/CategoryDataContent.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/PledgeAnnounce.h"
#include "Shared/src/PledgeAnnounceRegistry.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/proto/DBPacket.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>

namespace {

template <typename Packet>
bool UserPacketParser(CSocket* baseSocket, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket != nullptr ? socket->GetUser() : nullptr;
    if (user == nullptr) {
        return true;
    }

    Packet packet{};
    network::Stream stream(const_cast<std::uint8_t*>(source), 0x4000);
    if (!packet.Decode(&stream)) {
        runtime::process::ReportCallStack(2, false);
        return true;
    }
    return UserPacketHandle(socket, packet);
}

namespace CheckPledgeRecruiteeResult {

enum Value : std::int32_t {
    Success = 0,
    InvalidArgument = 1,
    InTraining = 2,
    AlreadyMember = 3,
    TimePenalty = 4,
    FreeAccountInTaiwan = 5,
    AlreadyGraduatedAcademy = 6,
    CannotJoinAcademy = 7,
};

} // namespace CheckPledgeRecruiteeResult

namespace CheckPledgeRecruitingResult {

enum Value : std::int32_t {
    Success = 0,
    InvalidArgument = 1,
    WaitingToDismiss = 2,
    CannotDoPledgeWork = 3,
    HaveNoPledge = 4,
    PledgeIsFull = 5,
    TimePenalty = 6,
};

} // namespace CheckPledgeRecruitingResult

bool CanJoinAcademy(CSPointer<User> user) {
    // Current anonymous UserPacketEx.cpp owner at 0x00becce8.
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }

    CSharedCreatureData* const data = user->m_data;
    if (data->m_nSubJob1Class != -1 || data->m_nSubJob2Class != -1 || data->m_nSubJob3Class != -1) {
        for (const int level : data->m_SubJobLevel) {
            if (level > 84) {
                return false;
            }
        }

        const std::array<int, 4> subJobClasses{
            data->m_nSubJob0Class, data->m_nSubJob1Class, data->m_nSubJob2Class, data->m_nSubJob3Class};
        for (const int classId : subJobClasses) {
            if (classId > 0 && !User::CanJoinAcademyClass(static_cast<ClassType>(classId))) {
                return false;
            }
        }
    }

    return data->m_nLevel < 85 && User::CanJoinAcademyClass(data->m_nClass);
}

CheckPledgeRecruiteeResult::Value CheckPledgeRecruitee(CSPointer<User> user, const PledgeType pledgeType) {
    // Current anonymous UserPacketEx.cpp owner at 0x00bed0e8.
    L2_GUARD_FUNCTION();
    if (!user) {
        return CheckPledgeRecruiteeResult::InvalidArgument;
    }
    if (user->m_data->m_TrainingState == 1) {
        return CheckPledgeRecruiteeResult::InTraining;
    }
    if (user->m_data->m_nPledgeID != 0) {
        return CheckPledgeRecruiteeResult::AlreadyMember;
    }
    if (!user->CanJoinPledge()) {
        return CheckPledgeRecruiteeResult::TimePenalty;
    }
    if (g_nCountryCode == 3 && user->GetSocket() != nullptr && user->GetSocket()->m_bEconomicRestriction) {
        return CheckPledgeRecruiteeResult::FreeAccountInTaiwan;
    }
    if (pledgeType == ACADEMY) {
        if (user->m_data->m_nAcademyPledgeDBID != 0) {
            return CheckPledgeRecruiteeResult::AlreadyGraduatedAcademy;
        }
        if (!CanJoinAcademy(user)) {
            return CheckPledgeRecruiteeResult::CannotJoinAcademy;
        }
    }
    return CheckPledgeRecruiteeResult::Success;
}

CheckPledgeRecruitingResult::Value CheckPledgeRecruiting(CSPointer<CPledge> pledge, const PledgeType pledgeType) {
    // Current anonymous UserPacketEx.cpp owner at 0x00bed440.
    L2_GUARD_FUNCTION();
    if (!pledge) {
        return CheckPledgeRecruitingResult::InvalidArgument;
    }
    if (pledge->IsWaitingToDismiss()) {
        return CheckPledgeRecruitingResult::WaitingToDismiss;
    }
    if (!pledge->CanDoPledgeWork(0x78)) {
        return CheckPledgeRecruitingResult::CannotDoPledgeWork;
    }
    if (!pledge->HasPledgeType(pledgeType)) {
        return CheckPledgeRecruitingResult::HaveNoPledge;
    }
    if (!pledge->CanJoinMoreMember(pledgeType)) {
        return CheckPledgeRecruitingResult::PledgeIsFull;
    }
    if (!pledge->CanAcceptNewMember()) {
        return CheckPledgeRecruitingResult::TimePenalty;
    }
    return CheckPledgeRecruitingResult::Success;
}

std::tm* TargetLocalTime32ForUserPacket(const std::int32_t value) noexcept {
    static thread_local std::tm result{};
    const std::time_t input = static_cast<std::time_t>(value);
#if defined(_WIN32)
    return ::localtime_s(&result, &input) == 0 ? &result : nullptr;
#else
    return ::localtime_r(&input, &result);
#endif
}

void EndResidenceDecoNpcJob(CResidence* const residence, const int line) {
    if (residence != nullptr && !residence->m_decoNpcJobLock.EndJob()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Failed to end job at %s(%d)",
        //     L"UserPacketEx.cpp",
        //     line)
        // L2_LOG_ERROR(Log,
        //     L"Failed to end job at %s(%d)",
        //     L"UserPacketEx.cpp",
        //     line)
        L2_LOG_ERROR(
            Log, "Residence decorative NPC job could not be ended: residence=%d, caller marker=%d",
            residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0, line
        );
    }
}

bool CheckResidenceDomain(const CResidence* const residence, const CDecoNpcData* const data) {
    // Current L2Server 0x00be8e28.
    L2_GUARD_FUNCTION();
    if (residence == nullptr || residence->m_data == nullptr || data == nullptr) {
        return false;
    }
    for (int index = 0; index < residence->m_data->m_nResidenceDomainCount; ++index) {
        if (data->HasResidenceDomain(residence->m_data->m_aResidenceDomains[index])) {
            return true;
        }
    }
    return false;
}

bool CheckTypeOverlapping(const CResidence* const residence, const CDecoNpcData* const data) {
    // Current L2Server 0x00c07b88 returns true when no duplicate exists.
    L2_GUARD_FUNCTION();
    if (residence == nullptr || residence->m_data == nullptr || data == nullptr) {
        return false;
    }
    for (int slot = 0; slot < residence->m_data->m_nDecoNpcSlotCount; ++slot) {
        const int placedId = residence->m_data->m_aDecoNpc[slot].nCID;
        if (placedId == 0) {
            continue;
        }
        const CDecoNpcData* const placed = CDecoNpcDB::Inst().FindDecoNpc(static_cast<unsigned int>(placedId));
        if (placed != nullptr && placed->m_nFactionType == data->m_nFactionType && placed->m_nSubType == data->m_nSubType) {
            return false;
        }
    }
    return true;
}

void SendDecoNpcInfoError(CSPointer<User> user) {
    // Current anonymous UserPacketEx.cpp owner at 0x00bf9ec0.
    if (!user) {
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return;
    }
    protocol::S_EX_DECO_NPC_INFO response{};
    response.nAgitCID = -1;
    socket->Send(S_EX, S_EX_DECO_NPC_INFO, response);
}

void SendInstantAgitDecoNpcInfo(CSPointer<User> user) {
    // Current anonymous UserPacketEx.cpp owner at 0x00bf9fc0.
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return;
    }

    CInstantAgit* const agit = CInstantAgitManager::inst().GetInstantAgit(static_cast<int>(user->m_data->m_nPledgeID));
    if (agit == nullptr) {
        socket->SendSystemMessage(u"SYS", u"invalid agit");
        SendDecoNpcInfoError(user);
        return;
    }
    if (!user->HavePledgePower(PP_CONTROL_AGIT_FUNC)) {
        socket->SendSystemMessage(u"SYS", u"no right");
        SendDecoNpcInfoError(user);
        return;
    }
    agit->SendDecoNpcInfo(user);
}

void SetDecoNpc(CSPointer<User> user, const int agitId, const int decoId, const int slot) {
    // Current anonymous UserPacketEx.cpp owner at 0x00c07ce4.
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(agitId);
    if (residence == nullptr) {
        socket->SendSystemMessage(u"SYS", u"invalid agit");
        return;
    }
    if (!ResidenceUtil::HasRightToSetDecoNpc(residence, user)) {
        socket->SendSystemMessage(u"SYS", u"no right");
        return;
    }

    JobTransactionLong jobType = 0x3b;
    if (!residence->m_decoNpcJobLock.BeginJob(&jobType)) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_SYSTEM_ERROR);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC][NO_ERROR] To begin deco npc job is failed at %s : %d, %d",
        //     L"`anonymous-namespace'::SetDecoNpc",
        //     residence->m_data != nullptr
        //         ? residence->m_data->m_nResidenceID
        //         : 0,
        //     static_cast<int>(jobType))
        // L2_LOG_ERROR(Log,
        //     L"[DECONPC][NO_ERROR] To begin deco npc job is failed at %s : %d, %d",
        //     L"`anonymous-namespace'::SetDecoNpc",
        //     residence->m_data != nullptr
        //         ? residence->m_data->m_nResidenceID
        //         : 0,
        //     static_cast<int>(jobType))
        L2_LOG_ERROR(
            Log,
            "Decorative NPC placement rejected: residence job could not begin, residence=%d, slot=%d, decoration=%d, job=%d",
            agitId, slot, decoId, static_cast<int>(jobType)
        );
        return;
    }

    constexpr int kSourceLine = 0x2eab;
    CResidenceData* const residenceData = residence->m_data;
    const auto rejectText = [&](const char16_t* const message) {
        socket->SendSystemMessage(u"SYS", message);
        EndResidenceDecoNpcJob(residence, kSourceLine);
    };
    if (residenceData == nullptr || slot < 0 || slot >= residenceData->m_nDecoNpcSlotCount) {
        rejectText(u"invalid agit slot");
        return;
    }
    if (residenceData->m_aDecoNpc[slot].nCID != 0) {
        socket->SendSystemMessage(SystemMessageId::REMOVE_EXISTING_EXPERT_BEFORE_PLACEMENT);
        EndResidenceDecoNpcJob(residence, kSourceLine);
        return;
    }

    const CDecoNpcData* const data = CDecoNpcDB::Inst().FindDecoNpc(static_cast<unsigned int>(decoId));
    if (data == nullptr) {
        rejectText(u"invalid deco id");
        return;
    }
    if (data->m_nPeriod < 1) {
        rejectText(u"invalid period");
        return;
    }
    if (!data->HasResidenceGrade(residenceData->m_nResidenceGrade)) {
        rejectText(u"invalid grade");
        return;
    }
    if (!CheckResidenceDomain(residence, data)) {
        rejectText(u"invalid domain");
        return;
    }
    if (!DecoNpcUtil::IsRightSlot(data, slot)) {
        rejectText(u"invalid slot for a faction delegation");
        return;
    }
    if (!CheckTypeOverlapping(residence, data)) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_PLACE_SAME_CLASS_EXPERTS_OF_SAME_FACTION_TOGETHER);
        EndResidenceDecoNpcJob(residence, kSourceLine);
        return;
    }

    const std::int32_t currentTime = runtime::crt::Time32Now();
    std::tm* const localTime = TargetLocalTime32ForUserPacket(currentTime);
    if (localTime == nullptr) {
        EndResidenceDecoNpcJob(residence, kSourceLine);
        return;
    }
    const std::int32_t expireTime =
        currentTime + (((data->m_nPeriod * 1440 - localTime->tm_min) + 60) * 60 - localTime->tm_sec);
    const FVector position = user->GetPos();
    CSetResidenceDecoNpc request(
        static_cast<int>(user->m_nId), static_cast<int>(user->GetDBID()), static_cast<int>(user->GetAccountID()), position,
        static_cast<int>(user->m_data->m_nPledgeID), residenceData->m_nResidenceID, slot, decoId, expireTime,
        static_cast<int>(data->m_nCostAdena), data->GetFirstCostItemCID(),
        static_cast<std::int64_t>(data->GetFirstCostItemCount()), 0
    );
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x3b), static_cast<int>(sizeof(request)), request);
}

void RemoveDecoNpc(CSPointer<User> user, const int agitId, const int slot) {
    // Current anonymous UserPacketEx.cpp owner at 0x00c075cc.
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(agitId);
    if (residence == nullptr) {
        socket->SendSystemMessage(u"SYS", u"invalid agit");
        return;
    }
    if (!ResidenceUtil::HasRightToSetDecoNpc(residence, user)) {
        socket->SendSystemMessage(u"SYS", u"no right");
        return;
    }

    JobTransactionLong jobType = 0x3c;
    if (!residence->m_decoNpcJobLock.BeginJob(&jobType)) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_SYSTEM_ERROR);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC][NO_ERROR] To begin deco npc job is failed at %s : %d, %d",
        //     L"`anonymous-namespace'::RemoveDecoNpc",
        //     residence->m_data != nullptr
        //         ? residence->m_data->m_nResidenceID
        //         : 0,
        //     static_cast<int>(jobType))
        // L2_LOG_ERROR(Log,
        //     L"[DECONPC][NO_ERROR] To begin deco npc job is failed at %s : %d, %d",
        //     L"`anonymous-namespace'::RemoveDecoNpc",
        //     residence->m_data != nullptr
        //         ? residence->m_data->m_nResidenceID
        //         : 0,
        //     static_cast<int>(jobType))
        L2_LOG_ERROR(
            Log, "Decorative NPC removal rejected: residence job could not begin, residence=%d, slot=%d, job=%d", agitId,
            slot, static_cast<int>(jobType)
        );
        return;
    }

    constexpr int kSourceLine = 0x2e07;
    CResidenceData* const residenceData = residence->m_data;
    const auto rejectText = [&](const char16_t* const message) {
        socket->SendSystemMessage(u"SYS", message);
        EndResidenceDecoNpcJob(residence, kSourceLine);
    };
    if (residenceData == nullptr || slot < 0 || slot >= residenceData->m_nDecoNpcSlotCount) {
        rejectText(u"invalid agit slot");
        return;
    }
    const int decoId = residenceData->m_aDecoNpc[slot].nCID;
    if (decoId == 0) {
        rejectText(u"no deco at agit slot");
        return;
    }
    const CDecoNpcData* const data = CDecoNpcDB::Inst().FindDecoNpc(static_cast<unsigned int>(decoId));
    if (data == nullptr) {
        rejectText(u"invalid deco id of placed deco npc");
        return;
    }
    if (!data->m_isChangeable) {
        socket->SendSystemMessage(SystemMessageId::EXPERT_CANNOT_BE_REMOVED);
        EndResidenceDecoNpcJob(residence, kSourceLine);
        return;
    }

    const FVector position = user->GetPos();
    CResetResidenceDecoNpc request(
        static_cast<int>(user->m_nId), static_cast<int>(user->GetDBID()), static_cast<int>(user->GetAccountID()), position,
        static_cast<int>(user->m_data->m_nPledgeID), residenceData->m_nResidenceID, slot
    );
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x3c), static_cast<int>(sizeof(request)), request);
}

bool HandleConnectToWOlympiad(
    CUserSocket* const socket, const int worldId, const int characterId, const std::uint32_t accountId
) {
    // Current anonymous UserPacketEx.cpp owner at 0x00bf939c.
    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserPacketEx.cpp",
        //     0x1578,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserPacketEx.cpp",
        //     0x1578,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        L2_LOG_ERROR(
            Log, "World Olympiad connection rejected: client socket is null, world=%d, character ID=%d", worldId, characterId
        );
        return false;
    }

    const olympiad::PkOlympiadAuthInfo* const authInfo =
        WOlympiadService::inst().FindAuthInfo(std::CDualKey<int, int>(worldId, characterId));
    if (authInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Invalid connect: %d, %d",
        //     L"UserPacketEx.cpp",
        //     0x157f,
        //     worldId,
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Invalid connect: %d, %d",
        //     L"UserPacketEx.cpp",
        //     0x157f,
        //     worldId,
        //     characterId)
        L2_LOG_ERROR(
            Log, "World Olympiad connection rejected: authorization information is unavailable, world=%d, character ID=%d",
            worldId, characterId
        );
        return false;
    }

    socket->m_CountryCode = static_cast<std::uint8_t>(authInfo->cCountryCode);
    socket->m_iPremiumLevel = authInfo->nPremiumLevel;
    socket->SetDormantMarkingTime(authInfo->tDormantMarkingTime, authInfo->nDormantType);

    std::optional<std::wstring> existingAccountNameStorage;
    const bool accountRegistered = [&] {
        L2_GUARD(accountMapSect);
        const auto existing = accountMap.find(accountId);
        if (existing != accountMap.end()) {
            CUserSocket* const existingSocket = existing->second;
            existingAccountNameStorage.emplace(
                Utf16ToWide(existingSocket->GetAccountName(), Utf16WidePolicy::PreserveInvalidOnWindows)
            );
            const std::wstring& existingAccountName = *existingAccountNameStorage;
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]try dual login. account: %s, %d, %d",
            //     L"UserPacketEx.cpp",
            //     0x158f,
            //     existingAccountName.c_str(),
            //     static_cast<int>(existingSocket->GetAccountID()),
            //     existingSocket->GetSocketUseTime())
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]try dual login. account: %s, %d, %d",
            //     L"UserPacketEx.cpp",
            //     0x158f,
            //     existingAccountName.c_str(),
            //     static_cast<int>(existingSocket->GetAccountID()),
            //     existingSocket->GetSocketUseTime())
            L2_LOG_ERROR(
                Log,
                "World Olympiad connection rejected: account already has a connection, account=%s, account ID=%u, socket "
                "use time=%d",
                ::WideToUtf8(existingAccountName).c_str(), static_cast<unsigned>(existingSocket->GetAccountID()),
                existingSocket->GetSocketUseTime()
            );
            return false;
        }

        accountMap.emplace(accountId, socket);
        L2_ADD_REF(socket, ObjectRefType::AccountMap);
        socket->SetAccountID(accountId);
        return true;
    }();
    if (!accountRegistered) {
        return false;
    }

    socket->SetAccountID(static_cast<std::uint32_t>(authInfo->nAccountID));
    socket->SetAccountName(
        const_cast<SharedUtilsWideChar*>(reinterpret_cast<const SharedUtilsWideChar*>(authInfo->sAccountName))
    );

    stAccountAuthInfo accountAuthInfo;
    WOlympiadStuff::Convert(authInfo, &accountAuthInfo);
    socket->SetPCCafeCode(accountAuthInfo.sPCCafeCode);
    {
        L2_WRITE_GUARD(AccountAuthInfoLock);
        g_AccountAuthInfoMap[static_cast<std::uint32_t>(authInfo->nAccountID)] = accountAuthInfo;
    }

    db.LoadSelectedCharacter(worldId, characterId, socket, ALT_FORCE, authInfo->nX, authInfo->nY, authInfo->nZ, true);
    socket->m_nCloseReason = SCR_NORMAL;
    return true;
}

} // namespace

// Current UserPacketEx.cpp global at 0x2106ac60. The constructor compiled at
// 0x00bf7c48 proves all 352 callbacks, registration flags and packet-name
// buffers are created, with DummyUserPacketEx as the default callback.
UserPacketExCallbackMap g_UserPacketEx(&DummyUserPacketEx, u"C_EX_PROTOCOL");

namespace {

L2_BIND_PACKET(kBindCExDummy, g_UserPacketEx, 0x000, u"C_EX_DUMMY", &DummyUserPacketEx);
// Current EX12 initializers 0x00dc6d8c, 0x00dc834c, 0x00dc85ec,
// 0x00dc87fc, 0x00dc92dc, 0x00dc93fc, 0x00dc942c and 0x00dc97bc.
L2_BIND_PACKET(kBindCExNCGuard, g_UserPacketEx, 0x0c8, u"C_EX_NCGUARD", &UserPacketEx::RequestOnReceiveNCGuardClientData);
L2_BIND_PACKET(
    kBindCExUserHardwareInfo, g_UserPacketEx, 0x0ae, u"C_EX_USER_HARDWARE_INFO", &UserPacketEx::UserHardwareInfoPacket
);
L2_BIND_PACKET(kBindCExSysString, g_UserPacketEx, 0x0c3, u"C_EX_SYS_STRING", &UserPacketEx::RequestSysString);
L2_BIND_PACKET(kBindCExQueueTicket, g_UserPacketEx, 0x121, u"C_EX_QUEUETICKET", &UserPacketEx::QueueTicketPacket);
L2_BIND_PACKET(kBindCExClientIni, g_UserPacketEx, 0x104, u"C_EX_CLIENT_INI", &UserPacketEx::RequestSaveClientINI);
L2_BIND_PACKET(kBindCExCaptchaAnswer, g_UserPacketEx, 0x10f, u"C_EX_CAPTCHA_ANSWER", &UserPacketEx::ReqeustCaptchaAnswer);
L2_BIND_PACKET(
    kBindCExRefreshCaptchaImage, g_UserPacketEx, 0x110, u"C_EX_REFRESH_CAPTCHA_IMAGE", &UserPacketEx::RefreshCaptchaImage
);
L2_BIND_PACKET(
    kBindCExSsoAuthNTokenReq, g_UserPacketEx, 0x125, u"C_EX_SSO_AUTHNTOKEN_REQ", &UserPacketEx::RequestSSOAuthNToken
);
// Current EX14 initializers 0x00dc6aec, 0x00dc6e4c, 0x00dc6f6c,
// 0x00dc714c and 0x00dc7bcc.
L2_BIND_PACKET(kBindCExChangeName, g_UserPacketEx, 0x038, u"C_EX_CHANGE_NAME", &RequestChangeCharName);
L2_BIND_PACKET(
    kBindCExPrivateStoreWholeSetMsg,
    g_UserPacketEx,
    0x047,
    u"C_EX_PRIVATE_STORE_WHOLE_SET_MSG",
    &RequestPrivateStoreWholeSetMsg
);
L2_BIND_PACKET(kBindCExDispel, g_UserPacketEx, 0x048, u"C_EX_DISPEL", &UserPacketEx::DispelPacket);
L2_BIND_PACKET(kBindCExUserBookmark, g_UserPacketEx, 0x04e, u"C_EX_USER_BOOKMARK", &CUserBookmarkHandler::UserPacketHandler);
L2_BIND_PACKET(kBindCExEndScenePlayer, g_UserPacketEx, 0x058, u"C_EX_END_SCENE_PLAYER", &UserPacketEx::EndScenePlayer);
L2_BIND_PACKET(kBindCExStopScenePlayer, g_UserPacketEx, 0x090, u"C_EX_STOP_SCENE_PLAYER", &UserPacketEx::StopScenePlayer);
// Current EX02 initializers 0x00dc633c and 0x00dc636c.
L2_BIND_PACKET(kBindCExAskJoinMPCC, g_UserPacketEx, 0x006, u"C_EX_ASK_JOIN_MPCC", &AskJoinMPCC);
L2_BIND_PACKET(kBindCExAcceptJoinMPCC, g_UserPacketEx, 0x007, u"C_EX_ACCEPT_JOIN_MPCC", &AcceptJoinMPCC);
// Current EX02 initializer 0x00dc639c.
L2_BIND_PACKET(kBindCExOustFromMPCC, g_UserPacketEx, 0x008, u"C_EX_OUST_FROM_MPCC", &OustFromMPCC);
// Current EX04 initializer 0x00dc618c.
L2_BIND_PACKET(kBindCExAutoSoulshot, g_UserPacketEx, 0x00d, u"C_EX_AUTO_SOULSHOT", &AutoSoulshotEXPacket);
// Current EX04 initializers 0x00dc61bc, 0x00dc61ec and 0x00dc621c.
L2_BIND_PACKET(kBindCExEnchantSkillInfo, g_UserPacketEx, 0x00e, u"C_EX_ENCHANT_SKILL_INFO", &EnchantSkillInfo);
L2_BIND_PACKET(
    kBindCExEnchantSkillInfoDetail, g_UserPacketEx, 0x043, u"C_EX_ENCHANT_SKILL_INFO_DETAIL", &EnchantSkillInfoDetail
);
L2_BIND_PACKET(kBindCExRequestEnchantSkill, g_UserPacketEx, 0x00f, u"C_EX_REQ_ENCHANT_SKILL", &RequestEnchantSkill);
// Current Variation initializers 0x00dc68ac..0x00dc699c.
L2_BIND_PACKET(
    kBindCExTryToPutItemForVariationMake,
    g_UserPacketEx,
    0x026,
    u"C_EX_TRY_TO_PUT_ITEM_FOR_VARIATION_MAKE",
    &UserPacketEx::TryToPutItemForVariationMakePacket
);
L2_BIND_PACKET(
    kBindCExTryToPutIntensiveForVariationMake,
    g_UserPacketEx,
    0x027,
    u"C_EX_TRY_TO_PUT_INTENSIVE_FOR_VARIATION_MAKE",
    &UserPacketEx::TryToPutIntensiveForVariationMakePacket
);
L2_BIND_PACKET(
    kBindCExTryToPutCommissionForVariationMake,
    g_UserPacketEx,
    0x028,
    u"C_EX_TRY_TO_PUT_COMMISSION_FOR_VARIATION_MAKE",
    &UserPacketEx::TryToPutCommissionForVariationMakePacket
);
L2_BIND_PACKET(
    kBindCExTryToMakeVariation, g_UserPacketEx, 0x03e, u"C_EX_TRY_TO_MAKE_VARIATION", &UserPacketEx::TryToMakeVariationPacket
);
L2_BIND_PACKET(
    kBindCExTryToPutItemForVariationCancel,
    g_UserPacketEx,
    0x03f,
    u"C_EX_TRY_TO_PUT_ITEM_FOR_VARIATION_CANCEL",
    &TryToPutItemForVariationCancelPacket
);
L2_BIND_PACKET(
    kBindCExClickVariationCancelButton,
    g_UserPacketEx,
    0x040,
    u"C_EX_CLICK_VARIATION_CANCEL_BUTTON",
    &ClickVariationCancelButtonPacket
);
// Current EX04 initializers 0x00dc6f9c and 0x00dc82bc.
L2_BIND_PACKET(
    kBindCExWithdrawPremiumItem, g_UserPacketEx, 0x04f, u"C_EX_WITHDRAW_PREMIUM_ITEM", &UserPacketEx::WithdrawPremiumItem
);
L2_BIND_PACKET(
    kBindCExRequestUseGoodsInventoryItem,
    g_UserPacketEx,
    0x0ab,
    u"C_EX_REQUEST_USE_GOODS_IVENTORY_ITEM",
    &RequestRetrieveGoodsInventoryItem
);
// Current EX04 initializer 0x00dc933c.
L2_BIND_PACKET(
    kBindCExRequestVipAttendanceItemList,
    g_UserPacketEx,
    0x106,
    u"C_EX_REQUEST_VIP_ATTENDANCE_ITEMLIST",
    &UserPacketEx::RequestVipAttendanceItemList
);
// Current EX04 initializer 0x00dc6d2c.
L2_BIND_PACKET(
    kBindCExChangeInventorySlot,
    g_UserPacketEx,
    0x024,
    u"C_EX_CHANGE_INVENTORY_SLOT",
    &UserPacketEx::RequestChangeInventorySlot
);
// Current EX04 initializers 0x00dc6e7c, 0x00dc6eac, 0x00dc8cac and
// 0x00dc8cdc.
L2_BIND_PACKET(
    kBindCExTryToPutEnchantTargetItem,
    g_UserPacketEx,
    0x049,
    u"C_EX_TRY_TO_PUT_ENCHANT_TARGET_ITEM",
    &UserPacketEx::TryToPutEnchantTargetItem
);
L2_BIND_PACKET(
    kBindCExTryToPutEnchantSupportItem,
    g_UserPacketEx,
    0x04a,
    u"C_EX_TRY_TO_PUT_ENCHANT_SUPPORT_ITEM",
    &UserPacketEx::TryToPutEnchantSupportItem
);
L2_BIND_PACKET(
    kBindCExEnchantScrollItemAdd, g_UserPacketEx, 0x0e3, u"C_EX_ENCHANT_SCROLL_ITEM_ADD", &UserPacketEx::EnchantScrollItemAdd
);
L2_BIND_PACKET(
    kBindCExEnchantSupportItemRemove,
    g_UserPacketEx,
    0x0e4,
    u"C_EX_ENCHANT_SUPPORT_ITEM_REMOVE",
    &UserPacketEx::EnchantSupportItemRemove
);
// Current EX04 initializers 0x00dc828c, 0x00dc83ac and 0x00dc86dc.
L2_BIND_PACKET(
    kBindCExRequestGoodsInventoryInfo,
    g_UserPacketEx,
    0x0aa,
    u"C_EX_REQUEST_GOODS_INVENTORY_INFO",
    &RequestInquiryGoodsInventory
);
L2_BIND_PACKET(
    kBindCExChangeAttributeItem,
    g_UserPacketEx,
    0x0b0,
    u"C_EX_CHANGE_ATTRIBUTE_ITEM",
    &UserPacketEx::ChangeAttributeItemPacket
);
L2_BIND_PACKET(
    kBindCExRequestShapeShifting,
    g_UserPacketEx,
    0x0c7,
    u"C_EX_REQUEST_SHAPE_SHIFTING",
    &UserPacketEx::OnRequestShapeShifting
);
// Current EX04 initializers 0x00dc864c and 0x00dc867c.
L2_BIND_PACKET(
    kBindCExTryToPutShapeShiftingTargetItem,
    g_UserPacketEx,
    0x0c4,
    u"C_EX_TRY_TO_PUT_SHAPE_SHIFTING_TARGET_ITEM",
    &UserPacketEx::TryToPutShapeShiftingTargetItem
);
L2_BIND_PACKET(
    kBindCExTryToPutShapeShiftingExtractionItem,
    g_UserPacketEx,
    0x0c5,
    u"C_EX_TRY_TO_PUT_SHAPE_SHIFTING_EXTRACTION_ITEM",
    &UserPacketEx::TryToPutShapeShiftingExtractionItem
);
// Current EX04 initializers 0x00dc6c6c and 0x00dc7b6c.
L2_BIND_PACKET(kBindCExRequestItemLink, g_UserPacketEx, 0x01e, u"C_EX_RQ_ITEMLINK", &RequestItemLink);
L2_BIND_PACKET(
    kBindCExRequestCrystalItemInfo,
    g_UserPacketEx,
    0x08e,
    u"C_EX_REQUEST_CRYSTALITEM_INFO",
    &UserPacketEx::RequestCrystalItemInfo
);
// Current EX02 initializers 0x00dc60cc, 0x00dc60fc, 0x00dc612c,
// 0x00dc615c, 0x00dc648c, 0x00dc64bc, 0x00dc64ec, 0x00dc67bc,
// 0x00dc71ac, 0x00dc71dc, 0x00dc720c, 0x00dc723c, 0x00dc726c, 0x00dc729c,
// 0x00dc72fc and 0x00dc966c.
L2_BIND_PACKET(kBindCExOustFromPartyRoom, g_UserPacketEx, 0x009, u"C_EX_OUST_FROM_PARTY_ROOM", &ReplyOustFromPartyRoom);
L2_BIND_PACKET(kBindCExDismissPartyRoom, g_UserPacketEx, 0x00a, u"C_EX_DISMISS_PARTY_ROOM", &ReplyDismissPartyRoom);
L2_BIND_PACKET(kBindCExWithdrawPartyRoom, g_UserPacketEx, 0x00b, u"C_EX_WITHDRAW_PARTY_ROOM", &ReplyWithdrawPartyRoom);
L2_BIND_PACKET(
    kBindCExHandOverPartyMaster, g_UserPacketEx, 0x00c, u"C_EX_HAND_OVER_PARTY_MASTER", &ReplyHandOverPartyMaster
);
L2_BIND_PACKET(
    kBindCExMPCCShowPartyMembersInfo, g_UserPacketEx, 0x02d, u"C_EX_MPCC_SHOW_PARTY_MEMBERS_INFO", &MPCCShowPartyMembersInfo
);
L2_BIND_PACKET(kBindCExAskJoinPartyRoom, g_UserPacketEx, 0x02f, u"C_EX_ASK_JOIN_PARTY_ROOM", &AskJoinPartyRoomPacket);
L2_BIND_PACKET(
    kBindCExAnswerJoinPartyRoom, g_UserPacketEx, 0x030, u"C_EX_ANSWER_JOIN_PARTY_ROOM", &AnswerJoinPartyRoomPacket
);
L2_BIND_PACKET(
    kBindCExListPartyMatchingWaitingRoom,
    g_UserPacketEx,
    0x031,
    u"C_EX_LIST_PARTY_MATCHING_WAITING_ROOM",
    &ListPartyMatchingWaitingRoomPacket
);
// Current EX04 initializer 0x00dc69cc.
L2_BIND_PACKET(
    kBindCExChooseInventoryAttributeItem,
    g_UserPacketEx,
    0x032,
    u"C_EX_CHOOSE_INVENTORY_ATTRIBUTE_ITEM",
    &ChooseInventoryAttributeItem
);
L2_BIND_PACKET(
    kBindCExListMPCCWaiting, g_UserPacketEx, 0x05a, u"C_EX_LIST_MPCC_WAITING", &UserPacketEx::ReplyListMPCCWaitingPacket
);
L2_BIND_PACKET(kBindCExManageMPCCRoom, g_UserPacketEx, 0x05b, u"C_EX_MANAGE_MPCC_ROOM", &UserPacketEx::ReplyManageMPCCRoom);
L2_BIND_PACKET(kBindCExJoinMPCCRoom, g_UserPacketEx, 0x05c, u"C_EX_JOIN_MPCC_ROOM", &UserPacketEx::ReplyJoinMPCCRoom);
L2_BIND_PACKET(kBindCExOustFromMPCCRoom, g_UserPacketEx, 0x05d, u"C_EX_OUST_FROM_MPCC_ROOM", &ReplyOustFromMPCCRoom);
L2_BIND_PACKET(kBindCExDismissMPCCRoom, g_UserPacketEx, 0x05e, u"C_EX_DISMISS_MPCC_ROOM", &ReplyDismissMPCCRoom);
L2_BIND_PACKET(kBindCExWithdrawMPCCRoom, g_UserPacketEx, 0x05f, u"C_EX_WITHDRAW_MPCC_ROOM", &ReplyWithdrawMPCCRoom);
L2_BIND_PACKET(kBindCExMPCCPartyMasterList, g_UserPacketEx, 0x061, u"C_EX_MPCC_PARTYMASTER_LIST", &ReplyMPCCPartyMasterList);
L2_BIND_PACKET(
    kBindCExPartyMatchingRoomHistory,
    g_UserPacketEx,
    0x11c,
    u"C_EX_PARTY_MATCHING_ROOM_HISTORY",
    &UserPacketEx::RequestPartyMatchingRoomHistory
);
// Current EX10 initializers 0x00dc624c, 0x00dc627c, 0x00dc62ac and 0x00dc62dc.
L2_BIND_PACKET(kBindCExReqManorList, g_UserPacketEx, 0x001, u"C_EX_REQ_MANOR_LIST", &CManorPacketHandler::RequestManorList);
L2_BIND_PACKET(
    kBindCExProcureCropList, g_UserPacketEx, 0x002, u"C_EX_PROCURE_CROP_LIST", &CManorPacketHandler::ProcureCropList
);
L2_BIND_PACKET(kBindCExSetSeed, g_UserPacketEx, 0x003, u"C_EX_SET_SEED", &CManorPacketHandler::SetSeed);
L2_BIND_PACKET(kBindCExSetCrop, g_UserPacketEx, 0x004, u"C_EX_SET_CROP", &CManorPacketHandler::SetCrop);
// Current EX07 initializers 0x00dc67ec, 0x00dc681c and 0x00dc684c.
L2_BIND_PACKET(kBindCExDuelAskStart, g_UserPacketEx, 0x01b, u"C_EX_DUEL_ASK_START", &RequestDuelAskStart);
L2_BIND_PACKET(kBindCExDuelAcceptStart, g_UserPacketEx, 0x01c, u"C_EX_DUEL_ACCEPT_START", &RequestDuelAcceptStart);
L2_BIND_PACKET(kBindCExDuelSurrender, g_UserPacketEx, 0x042, u"C_EX_DUEL_SURRENDER", &RequestDuelSurrender);
// Current EX05 initializer 0x00dc687c.
L2_BIND_PACKET(
    kBindCExMagicSkillUseGround, g_UserPacketEx, 0x041, u"C_EX_MAGIC_SKILL_USE_GROUND", &RequestMagicSkillUseGround
);
// Current EX07 initializers 0x00dc630c and 0x00dc642c.
L2_BIND_PACKET(kBindCExWriteHeroWords, g_UserPacketEx, 0x005, u"C_EX_WRITE_HERO_WORDS", &WriteHeroWords);
L2_BIND_PACKET(
    kBindCExOlympiadObserverEnd, g_UserPacketEx, 0x029, u"C_EX_OLYMPIAD_OBSERVER_END", &Handle_C_EX_OLYMPIAD_OBSERVER_END
);
// Current EX07 initializers 0x00dc75cc, 0x00dc75fc and 0x00dc98ac.
L2_BIND_PACKET(
    kBindCExConnectToRaidServer, g_UserPacketEx, 0x070, u"C_EX_CONNECT_TO_RAID_SERVER", &UserPacketEx::ConnectToRaidServer
);
L2_BIND_PACKET(kBindCExReturnFromRaid, g_UserPacketEx, 0x071, u"C_EX_RETURN_FROM_RAID", &UserPacketEx::ReturnFromRaid);
L2_BIND_PACKET(
    kBindCExRaidServerInfo, g_UserPacketEx, 0x12a, u"C_EX_RAID_SERVER_INFO", &UserPacketEx::RequestRaidServerInfo
);
// Current EX07 initializers 0x00dc768c and 0x00dc79bc.
L2_BIND_PACKET(kBindCExEventMatch, g_UserPacketEx, 0x074, u"C_EX_EVENT_MATCH", &UserPacketEx::EventMatch);
// Current EX02 initializers 0x00dc76bc and 0x00dc76ec.
L2_BIND_PACKET(
    kBindCExPartyLootingModify, g_UserPacketEx, 0x075, u"C_EX_PARTY_LOOTING_MODIFY", &UserPacketEx::PartyLootingModify
);
L2_BIND_PACKET(
    kBindCExPartyLootingModifyAgreement,
    g_UserPacketEx,
    0x076,
    u"C_EX_PARTY_LOOTING_MODIFY_AGREEMENT",
    &UserPacketEx::PartyLootingModifyAgreement
);
L2_BIND_PACKET(kBindCExGfxOlympiad, g_UserPacketEx, 0x085, u"C_EX_GFX_OLYMPIAD", &Handle_C_EX_GFX_OLYMPIAD);
// Current EX08 initializers 0x00dc80ac, 0x00dc80dc and 0x00dc810c.
L2_BIND_PACKET(
    kBindCExSecondPasswordCheck, g_UserPacketEx, 0x0a6, u"C_EX_2ND_PASSWORD_CHECK", &UserPacketEx::Request2ndPasswordCheck
);
L2_BIND_PACKET(
    kBindCExSecondPasswordVerify, g_UserPacketEx, 0x0a7, u"C_EX_2ND_PASSWORD_VERIFY", &UserPacketEx::Request2ndPasswordVerify
);
L2_BIND_PACKET(
    kBindCExSecondPasswordReq, g_UserPacketEx, 0x0a8, u"C_EX_2ND_PASSWORD_REQ", &UserPacketEx::Request2ndPasswordReq
);
// Current EX08 initializer 0x00dc813c.
L2_BIND_PACKET(kBindCExCheckCharName, g_UserPacketEx, 0x0a9, u"C_EX_CHECK_CHAR_NAME", &UserPacketEx::CheckCharName);
// Current EX07 initializers 0x00dc6b1c and 0x00dc6b4c.
L2_BIND_PACKET(kBindCExShowCastleInfo, g_UserPacketEx, 0x039, u"C_EX_SHOW_CASTLE_INFO", &RequestShowCastleInfo);
L2_BIND_PACKET(kBindCExShowFortressInfo, g_UserPacketEx, 0x03a, u"C_EX_SHOW_FORTRESS_INFO", &RequestShowFortressInfo);
// Current EX10 initializer 0x00dc6b7c.
L2_BIND_PACKET(kBindCExShowAgitInfo, g_UserPacketEx, 0x03b, u"C_EX_SHOW_AGIT_INFO", &RequestShowAgitInfo);
// Current EX07 initializers 0x00dc6bac and 0x00dc6dbc.
L2_BIND_PACKET(
    kBindCExShowFortressSiegeInfo, g_UserPacketEx, 0x03c, u"C_EX_SHOW_FORTRESS_SIEGE_INFO", &RequestShowFortressSiegeInfo
);
L2_BIND_PACKET(kBindCExShowFortressMapInfo, g_UserPacketEx, 0x045, u"C_EX_SHOW_FORTRESS_MAP_INFO", &RequestFortressMapInfo);
// Current EX10 initializer 0x00dc987c.
L2_BIND_PACKET(
    kBindCExShowAgitSiegeInfo, g_UserPacketEx, 0x12b, u"C_EX_SHOW_AGIT_SIEGE_INFO", &UserPacketEx::RequestShowAgitSiegeInfo
);
// Current EX01 initializers 0x00dc63fc and 0x00dc63cc.
L2_BIND_PACKET(kBindCExPledgeEmblem, g_UserPacketEx, 0x010, u"C_EX_PLEDGE_EMBLEM", &PledgeEmblem);
L2_BIND_PACKET(kBindCExSetPledgeEmblem, g_UserPacketEx, 0x011, u"C_EX_SET_PLEDGE_EMBLEM", &SetPledgeEmblem);
// Current EX01 pledge initializers 0x00dc657c, 0x00dc65ac,
// 0x00dc65dc, 0x00dc660c and 0x00dc663c.
L2_BIND_PACKET(kBindCExSetAcademyMaster, g_UserPacketEx, 0x012, u"C_EX_SET_ACADEMY_MASTER", &SetAcademyMaster);
L2_BIND_PACKET(
    kBindCExPledgePowerGradeList, g_UserPacketEx, 0x013, u"C_EX_PLEDGE_POWER_GRADE_LIST", &RequestPledgePowerList
);
L2_BIND_PACKET(kBindCExViewPledgePower, g_UserPacketEx, 0x014, u"C_EX_VIEW_PLEDGE_POWER", &RequestViewPledgePower);
L2_BIND_PACKET(
    kBindCExSetPledgePowerGrade, g_UserPacketEx, 0x015, u"C_EX_SET_PLEDGE_POWER_GRADE", &RequestSetPledgePowerGrade
);
L2_BIND_PACKET(
    kBindCExViewPledgeMemberInfo, g_UserPacketEx, 0x016, u"C_EX_VIEW_PLEDGE_MEMBER_INFO", &RequestViewPledgeMemberInfo
);
// Current EX01 initializer 0x00dc666c.
L2_BIND_PACKET(kBindCExViewPledgeWarList, g_UserPacketEx, 0x017, u"C_EX_VIEW_PLEDGE_WARLIST", &RequestViewPledgeWarList);
L2_BIND_PACKET(kBindCExCursedWeaponList, g_UserPacketEx, 0x02a, u"C_EX_CURSED_WEAPON_LIST", &RequestCursedWeaponList);
L2_BIND_PACKET(
    kBindCExExistingCursedWeaponLocation,
    g_UserPacketEx,
    0x02b,
    u"C_EX_EXISTING_CURSED_WEAPON_LOCATION",
    &RequestExistingCursedWeaponLocation
);
// Current EX01 initializer 0x00dc678c.
L2_BIND_PACKET(
    kBindCExReorganizePledgeMember, g_UserPacketEx, 0x02c, u"C_EX_REORGANIZE_PLEDGE_MEMBER", &RequestReorganizePledgeMember
);
L2_BIND_PACKET(kBindCExGetBossRecord, g_UserPacketEx, 0x03d, u"C_EX_GET_BOSS_RECORD", &GetBossRecord);
L2_BIND_PACKET(kBindCExFishRanking, g_UserPacketEx, 0x018, u"C_EX_FISH_RANKING", &RequsetViewFishingChampionshipRanking);
L2_BIND_PACKET(
    kBindCExRequestAntiFreeServer, g_UserPacketEx, 0x044, u"C_EX_REQUEST_ANTI_FREE_SERVER", &RequestAntiFreeServer
);
L2_BIND_PACKET(kBindCExDynamicQuest, g_UserPacketEx, 0x093, u"C_EX_DYNAMIC_QUEST", &DynamicQuestManager::UserPacketHandler);
// Current EX09 initializer 0x00dc711c.
L2_BIND_PACKET(kBindCExBlockUpsetEnter, g_UserPacketEx, 0x057, u"C_EX_BLOCK_UPSET_ENTER", &UserPacketEx::BlockUpsetEnter);
// Current EX09 initializer 0x00dc717c.
L2_BIND_PACKET(kBindCExBlockUpsetVote, g_UserPacketEx, 0x059, u"C_EX_BLOCK_UPSET_VOTE", &UserPacketEx::BlockUpsetVote);
// Current EX09 initializer 0x00dc771c.
L2_BIND_PACKET(
    kBindCExAnswerCoupleAction, g_UserPacketEx, 0x077, u"C_EX_ANSWER_COUPLE_ACTION", &UserPacketEx::AnswerCoupleAction
);
L2_BIND_PACKET(
    kBindCExBrLoadEventTopRankersReq,
    g_UserPacketEx,
    0x078,
    u"C_EX_BR_LOAD_EVENT_TOP_RANKERS_REQ",
    &RequestBrLoadEventTopRankers
);
// Current EX09 initializer 0x00dc777c.
L2_BIND_PACKET(kBindCExAskMyMembership, g_UserPacketEx, 0x079, u"C_EX_ASK_MY_MEMBERSHIP", &UserPacketEx::AnswerMyMembership);
// Current EX09 initializer 0x00dc77dc.
L2_BIND_PACKET(kBindCExVoteSystem, g_UserPacketEx, 0x07b, u"C_EX_VOTE_SYSTEM", &UserPacketEx::RequestVoteSystem);
// Current EX09 initializer 0x00dc7c8c.
L2_BIND_PACKET(kBindCExFriendDetailInfo, g_UserPacketEx, 0x094, u"C_EX_FRIEND_DETAIL_INFO", &UserPacketEx::FriendDetailInfo);
// Current EX09 initializer 0x00dc7cbc.
L2_BIND_PACKET(kBindCExUpdateFriendMemo, g_UserPacketEx, 0x095, u"C_EX_UPDATE_FRIEND_MEMO", &UserPacketEx::FriendUpdateMemo);
// Current EX09 initializer 0x00dc7cec.
L2_BIND_PACKET(kBindCExUpdateBlockMemo, g_UserPacketEx, 0x096, u"C_EX_UPDATE_BLOCK_MEMO", &UserPacketEx::BlockUpdateMemo);
// Current EX09 initializer 0x00dc7c2c.
L2_BIND_PACKET(
    kBindCExSurrenderPledgeWar, g_UserPacketEx, 0x092, u"C_EX_SURRENDER_PLEDGE_WAR", &UserPacketEx::SurrenderPledgeWar
);
L2_BIND_PACKET(kBindCExNotifyPlayStart, g_UserPacketEx, 0x0ac, u"C_EX_NOTIFY_PLAY_START", &UserPacketEx::NotifyPlayStart);
L2_BIND_PACKET(
    kBindCExUserInterfaceInfo, g_UserPacketEx, 0x0af, u"C_EX_USER_INTERFACE_INFO", &UserPacketEx::UserInterfaceInfoPacket
);
L2_BIND_PACKET(
    kBindCExExecutedUiEventsCount,
    g_UserPacketEx,
    0x103,
    u"C_EX_EXECUTED_UIEVENTS_COUNT",
    &UserPacketEx::ExecutedUIEventsCount
);
// Current EX01 initializer 0x00dc945c.
L2_BIND_PACKET(
    kBindCExPledgeSignin, g_UserPacketEx, 0x111, u"C_EX_PLEDGE_SIGNIN", &UserPacketEx::Request_C_EX_PLEDGE_SIGNIN
);
L2_BIND_PACKET(kBindCExPCCafeCouponUse, g_UserPacketEx, 0x019, u"C_EX_PCCAFE_COUPON_USE", &RequestPCCafeCouponUse);
// Current EX11 initializer 0x00dc66fc.
L2_BIND_PACKET(kBindCExOrcMove, g_UserPacketEx, 0x01a, u"C_EX_ORC_MOVE", &ResponseNewInspectorPacket);
L2_BIND_PACKET(
    kBindCExCancelEnchantItem, g_UserPacketEx, 0x04b, u"C_EX_CANCEL_ENCHANT_ITEM", &UserPacketEx::CancelEnchantItem
);
// Current EX15 initializer 0x00dc762c.
L2_BIND_PACKET(kBindCExRefundReq, g_UserPacketEx, 0x072, u"C_EX_REFUND_REQ", &UserPacketEx::RefundListReq);
// Current EX15 initializers 0x00dc8d3c..0x00dc8d9c.
L2_BIND_PACKET(
    kBindCExDivideAdenaStart, g_UserPacketEx, 0x0e6, u"C_EX_DIVIDE_ADENA_START", &UserPacketEx::RequestDivideAdenaStart
);
L2_BIND_PACKET(
    kBindCExDivideAdenaCancel, g_UserPacketEx, 0x0e7, u"C_EX_DIVIDE_ADENA_CANCEL", &UserPacketEx::RequestDivideAdenaCancel
);
L2_BIND_PACKET(kBindCExDivideAdena, g_UserPacketEx, 0x0e8, u"C_EX_DIVIDE_ADENA", &UserPacketEx::RequestDivideAdena);
L2_BIND_PACKET(
    kBindCExBuySellUiCloseReq, g_UserPacketEx, 0x073, u"C_EX_BUY_SELL_UI_CLOSE_REQ", &UserPacketEx::BuySellUICloseReq
);
// Current EX11 initializers 0x00dc780c..0x00dc789c.
L2_BIND_PACKET(kBindCExGetOnShuttle, g_UserPacketEx, 0x07c, u"C_EX_GETON_SHUTTLE", &UserPacketEx::GetOnShuttlePacket);
L2_BIND_PACKET(kBindCExGetOffShuttle, g_UserPacketEx, 0x07d, u"C_EX_GETOFF_SHUTTLE", &UserPacketEx::GetOffShuttlePacket);
L2_BIND_PACKET(
    kBindCExMoveToLocationInShuttle,
    g_UserPacketEx,
    0x07e,
    u"C_EX_MOVE_TO_LOCATION_IN_SHUTTLE",
    &UserPacketEx::MoveToLocationInShuttlePacket
);
L2_BIND_PACKET(
    kBindCExCannotMoveAnyMoreInShuttle,
    g_UserPacketEx,
    0x07f,
    u"C_EX_CAN_NOT_MOVE_ANYMORE_IN_SHUTTLE",
    &UserPacketEx::CannotMoveAnyMoreInShuttlePacket
);
// Current EX11 initializer 0x00dc78cc.
L2_BIND_PACKET(kBindCExAgitAuctionCmd, g_UserPacketEx, 0x080, u"C_EX_AGITAUCTION_CMD", &UserPacketEx::AgitAuctionCmdPacket);
L2_BIND_PACKET(kBindCExAddPostFriend, g_UserPacketEx, 0x081, u"C_EX_ADD_POST_FRIEND", &UserPacketEx::AddPostFriend);
L2_BIND_PACKET(kBindCExDeletePostFriend, g_UserPacketEx, 0x082, u"C_EX_DELETE_POST_FRIEND", &UserPacketEx::DeletePostFriend);
L2_BIND_PACKET(
    kBindCExBrSetLectureMarkReq, g_UserPacketEx, 0x08d, u"C_EX_BR_SET_LECTURE_MARK_REQ", &RequestBrSetLectureMark
);
// Current EX09 initializers 0x00dc816c and 0x00dc819c.
L2_BIND_PACKET(kBindCExMentorAdd, g_UserPacketEx, 0x0b4, u"C_EX_MENTOR_ADD", &UserPacketEx::AnswerAddMentee);
L2_BIND_PACKET(kBindCExMentorCancel, g_UserPacketEx, 0x0b5, u"C_EX_MENTOR_CANCEL", &UserPacketEx::RequestCancelMentor);
L2_BIND_PACKET(kBindCExMentorList, g_UserPacketEx, 0x0b6, u"C_EX_MENTOR_LIST", &UserPacketEx::RequestMentorList);
L2_BIND_PACKET(
    kBindCExMenteeWaitingList, g_UserPacketEx, 0x0b8, u"C_EX_MENTEE_WAITING_LIST", &UserPacketEx::MenteeWaitingListPacket
);
// Current EX01 initializer 0x00dc825c.
L2_BIND_PACKET(
    kBindCExJoinPledgeByName, g_UserPacketEx, 0x0b9, u"C_EX_JOIN_PLEDGE_BY_NAME", &UserPacketEx::JoinPledgeByNamePacket
);
L2_BIND_PACKET(
    kBindCExInzoneWaitingTime, g_UserPacketEx, 0x0ba, u"C_EX_INZONE_WAITING_TIME", &UserPacketEx::InzoneWaitngTime
);
L2_BIND_PACKET(
    kBindCExCancelShapeShifting, g_UserPacketEx, 0x0c6, u"C_EX_CANCEL_SHAPE_SHIFTING", &UserPacketEx::CancelShapeShifting
);
L2_BIND_PACKET(
    kBindCExJoinCuriousHouse, g_UserPacketEx, 0x0bb, u"C_EX_JOIN_CURIOUS_HOUSE", &UserPacketEx::RequestJoinPledgeGame
);
L2_BIND_PACKET(
    kBindCExCancelCuriousHouse,
    g_UserPacketEx,
    0x0bc,
    u"C_EX_CANCEL_CURIOUS_HOUSE",
    &UserPacketEx::RequestCancelJoinPledgeGame
);
L2_BIND_PACKET(
    kBindCExLeaveCuriousHouse, g_UserPacketEx, 0x0bd, u"C_EX_LEAVE_CURIOUS_HOUSE", &UserPacketEx::RequestLeavePledgeGame
);
L2_BIND_PACKET(
    kBindCExObserveListCuriousHouse,
    g_UserPacketEx,
    0x0be,
    u"C_EX_OBSERVE_LIST_CURIOUS_HOUSE",
    &UserPacketEx::RequestObserveListPledgeGame
);
L2_BIND_PACKET(
    kBindCExObserveCuriousHouse,
    g_UserPacketEx,
    0x0bf,
    u"C_EX_OBSERVE_CURIOUS_HOUSE",
    &UserPacketEx::RequestObservePledgeGame
);
L2_BIND_PACKET(
    kBindCExExitObserveCuriousHouse,
    g_UserPacketEx,
    0x0c0,
    u"C_EX_EXIT_OBSERVE_CURIOUS_HOUSE",
    &UserPacketEx::RequestExitObservePledgeGame
);
L2_BIND_PACKET(
    kBindCExReqCuriousHouseHtml, g_UserPacketEx, 0x0c1, u"C_EX_REQ_CURIOUS_HOUSE_HTML", &UserPacketEx::RequestPledgeGameHTML
);
L2_BIND_PACKET(
    kBindCExReqCuriousHouseRecord,
    g_UserPacketEx,
    0x0c2,
    u"C_EX_REQ_CURIOUS_HOUSE_RECORD",
    &UserPacketEx::RequestPledgeGameRecord
);
L2_BIND_PACKET(kBindCExLoadUiSetting, g_UserPacketEx, 0x021, u"C_EX_LOAD_UI_SETTING", &RequestLoadUI);
// Current EX08 initializer 0x00dc6ccc.
L2_BIND_PACKET(kBindCExSaveUiSetting, g_UserPacketEx, 0x022, u"C_EX_SAVE_UI_SETTING", &RequestSaveUI);
L2_BIND_PACKET(
    kBindCExExitPartyMatchingWaitingRoom,
    g_UserPacketEx,
    0x025,
    u"C_EX_EXIT_PARTY_MATCHING_WAITING_ROOM",
    &ExitPartyMatchingWaitingRoomPacket
);
L2_BIND_PACKET(
    kBindCExBrMinigameLoadScoresReq, g_UserPacketEx, 0x08b, u"C_EX_BR_MINIGAME_LOAD_SCORES_REQ", &RequestBrMinigameLoadScores
);
L2_BIND_PACKET(
    kBindCExPledgeRecruitInfo, g_UserPacketEx, 0x0d3, u"C_EX_PLEDGE_RECRUIT_INFO", &UserPacketEx::RequestPledgeRecruitInfo
);
L2_BIND_PACKET(
    kBindCExPledgeRecruitBoardSearch,
    g_UserPacketEx,
    0x0d4,
    u"C_EX_PLEDGE_RECRUIT_BOARD_SEARCH",
    &UserPacketEx::RequestSearchPledgeRecruitBoard
);
L2_BIND_PACKET(
    kBindCExPledgeRecruitBoardApply,
    g_UserPacketEx,
    0x0d5,
    u"C_EX_PLEDGE_RECRUIT_BOARD_APPLY",
    &UserPacketEx::RequestApplyPledgeRecruitBoard
);
L2_BIND_PACKET(
    kBindCExPledgeRecruitBoardDetail,
    g_UserPacketEx,
    0x0d6,
    u"C_EX_PLEDGE_RECRUIT_BOARD_DETAIL",
    &UserPacketEx::RequestPledgeOnPledgeRecruitBoard
);
L2_BIND_PACKET(
    kBindCExPledgeWaitingListApply,
    g_UserPacketEx,
    0x0d7,
    u"C_EX_PLEDGE_WAITING_LIST_APPLY",
    &UserPacketEx::RequestApplyOnPledgeRecruit
);
L2_BIND_PACKET(
    kBindCExPledgeWaitingListApplied,
    g_UserPacketEx,
    0x0d8,
    u"C_EX_PLEDGE_WAITING_LIST_APPLIED",
    &UserPacketEx::RequestApplyInfoOnPledgeWaitingList
);
L2_BIND_PACKET(
    kBindCExPledgeWaitingList,
    g_UserPacketEx,
    0x0d9,
    u"C_EX_PLEDGE_WAITING_LIST",
    &UserPacketEx::RequestPledgeWaitingUserList
);
L2_BIND_PACKET(
    kBindCExPledgeWaitingUser,
    g_UserPacketEx,
    0x0da,
    u"C_EX_PLEDGE_WAITING_USER",
    &UserPacketEx::RequestUserOnPledgeWaitingList
);
L2_BIND_PACKET(
    kBindCExPledgeWaitingUserAccept,
    g_UserPacketEx,
    0x0db,
    u"C_EX_PLEDGE_WAITING_USER_ACCEPT",
    &UserPacketEx::RequestAcceptPledgeWaitingUser
);
L2_BIND_PACKET(
    kBindCExPledgeDraftListSearch,
    g_UserPacketEx,
    0x0dc,
    u"C_EX_PLEDGE_DRAFT_LIST_SEARCH",
    &UserPacketEx::RequestSearchPledgeDraftUserList
);
L2_BIND_PACKET(
    kBindCExPledgeDraftListApply,
    g_UserPacketEx,
    0x0dd,
    u"C_EX_PLEDGE_DRAFT_LIST_APPLY",
    &UserPacketEx::RequestApplyPledgeDraftUserList
);
L2_BIND_PACKET(
    kBindCExPledgeRecruitApplyInfo,
    g_UserPacketEx,
    0x0de,
    u"C_EX_PLEDGE_RECRUIT_APPLY_INFO",
    &UserPacketEx::RequestPledgeRecruitApplyInfo
);
L2_BIND_PACKET(
    kBindCExPledgeJoinSystem, g_UserPacketEx, 0x0df, u"C_EX_PLEDGE_JOIN_SYS", &UserPacketEx::RequestPledgeJoinRemainTime
);
L2_BIND_PACKET(
    kBindCExResponseWebPetitionAlarm,
    g_UserPacketEx,
    0x0e0,
    u"C_EX_RESPONSE_WEB_PETITION_ALARM",
    &UserPacketEx::ResponsePetitionExternalNotify
);
L2_BIND_PACKET(kBindCExStopMove, g_UserPacketEx, 0x0ed, u"C_EX_STOP_MOVE", &UserPacketEx::RequestStopMove);
L2_BIND_PACKET(kBindCExCharacterBack, g_UserPacketEx, 0x033, u"C_EX_CHARACTER_BACK", &RequestCharacterBackToLobby);
L2_BIND_PACKET(
    kBindCExRequestPvpMatchRecord, g_UserPacketEx, 0x046, u"C_EX_REQUEST_PVPMATCH_RECORD", &RequestPVPMatchRecord
);
L2_BIND_PACKET(
    kBindCExChangeNicknameColor, g_UserPacketEx, 0x04c, u"C_EX_CHANGE_NICKNAME_COLOR", &UserPacketEx::ChangeNicknameColor
);
L2_BIND_PACKET(
    kBindCExRequestResetNickname, g_UserPacketEx, 0x04d, u"C_EX_REQUEST_RESET_NICKNAME", &UserPacketEx::ResetNicknameColor
);
L2_BIND_PACKET(
    kBindCExStartRequestPvpMatchRank,
    g_UserPacketEx,
    0x051,
    u"C_EX_START_REQUEST_PVPMATCH_CC_RANK",
    &UserPacketEx::StartRequestPVPMatchRank
);
L2_BIND_PACKET(
    kBindCExStopRequestPvpMatchRank,
    g_UserPacketEx,
    0x052,
    u"C_EX_STOP_REQUEST_PVPMATCH_CC_RANK",
    &UserPacketEx::StopRequestPVPMatchRank
);
// Current EX06 initializers 0x00dc708c and 0x00dc70bc.
L2_BIND_PACKET(
    kBindCExRequestRegisterDominion,
    g_UserPacketEx,
    0x054,
    u"C_EX_REQUEST_REGISTER_DOMINION",
    &UserPacketEx::RequestRegisterDominion
);
L2_BIND_PACKET(
    kBindCExRequestDominionInfo, g_UserPacketEx, 0x055, u"C_EX_REQUEST_DOMINION_INFO", &UserPacketEx::RequestDominionInfo
);
// Current EX06 initializers 0x00dc753c, 0x00dc756c and 0x00dc759c.
L2_BIND_PACKET(
    kBindCExRequestShowPetition, g_UserPacketEx, 0x06d, u"C_EX_REQUEST_SHOW_PETITION", &UserPacketEx::RequestShowPetition
);
L2_BIND_PACKET(
    kBindCExRequestShowStepTwo, g_UserPacketEx, 0x06e, u"C_EX_REQUEST_SHOWSTEP_TWO", &UserPacketEx::RequestShowStepTwo
);
L2_BIND_PACKET(
    kBindCExRequestShowStepThree, g_UserPacketEx, 0x06f, u"C_EX_REQUEST_SHOWSTEP_THREE", &UserPacketEx::RequestShowStepThree
);
L2_BIND_PACKET(
    kBindCExChangeAttributeCancel,
    g_UserPacketEx,
    0x0b2,
    u"C_EX_CHANGE_ATTRIBUTE_CANCEL",
    &UserPacketEx::ChangeAttributeCancelPacket
);
L2_BIND_PACKET(
    kBindCExRequestCrystalItemCancel,
    g_UserPacketEx,
    0x08f,
    u"C_EX_REQUEST_CRYSTALITEM_CANCEL",
    &UserPacketEx::RequestCrystalItemCancel
);
// Current EX06 registration slice: each name/opcode/handler tuple is fixed
// by the pinned image's static initializer table.
L2_BIND_PACKET(
    kBindCExQuestNpcLogList, g_UserPacketEx, 0x07a, u"C_EX_QUEST_NPC_LOG_LIST", &UserPacketEx::RequestQuestNPCLogList
);
L2_BIND_PACKET(
    kBindCExRequestWebSessionId, g_UserPacketEx, 0x0a5, u"C_EX_REQUEST_WEB_SESSION_ID", &UserPacketEx::RequestWebSessionID
);
L2_BIND_PACKET(
    kBindCExRequestChangeAttribute,
    g_UserPacketEx,
    0x0b1,
    u"C_EX_REQUEST_CHANGE_ATTRIBUTE",
    &UserPacketEx::RequestChangeAttributePacket
);
L2_BIND_PACKET(kBindCExRequestMentorAdd, g_UserPacketEx, 0x0b7, u"C_EX_REQUEST_MENTOR_ADD", &UserPacketEx::RequestAddMentee);
L2_BIND_PACKET(
    kBindCExRequestKalieToken, g_UserPacketEx, 0x0c9, u"C_EX_REQUEST_KALIE_TOKEN", &UserPacketEx::RequestKalieToken
);
L2_BIND_PACKET(
    kBindCExRequestShowRegistBeauty,
    g_UserPacketEx,
    0x0ca,
    u"C_EX_REQUEST_SHOW_REGIST_BEAUTY",
    &UserPacketEx::RequestShowRegistBeauty
);
L2_BIND_PACKET(
    kBindCExRequestRegistBeauty, g_UserPacketEx, 0x0cb, u"C_EX_REQUEST_REGIST_BEAUTY", &UserPacketEx::RequestRegistBeauty
);
L2_BIND_PACKET(
    kBindCExRequestShowResetBeauty,
    g_UserPacketEx,
    0x0cc,
    u"C_EX_REQUEST_SHOW_RESET_BEAUTY",
    &UserPacketEx::RequestShowResetBeauty
);
L2_BIND_PACKET(
    kBindCExRequestResetBeauty, g_UserPacketEx, 0x0cd, u"C_EX_REQUEST_RESET_BEAUTY", &UserPacketEx::RequestResetBeauty
);
L2_BIND_PACKET(
    kBindCExPccafeOpenWithoutNpc,
    g_UserPacketEx,
    0x0f0,
    u"C_EX_PCCAFE_REQUEST_OPEN_WINDOW_WITHOUT_NPC",
    &UserPacketEx::ShowPCCafeHTML
);
L2_BIND_PACKET(
    kBindCExRequestCardRewardList,
    g_UserPacketEx,
    0x0fb,
    u"C_EX_REQUEST_CARD_REWARD_LIST",
    &UserPacketEx::RequestShowCardCollectSystem
);
L2_BIND_PACKET(
    kBindCExRequestAttendanceReward,
    g_UserPacketEx,
    0x0fd,
    u"C_EX_REQUEST_ACCOUNT_ATTENDANCE_REWARD",
    &UserPacketEx::RequestAttendanceStampReward
);
L2_BIND_PACKET(
    kBindCExRequestAutoFishing, g_UserPacketEx, 0x105, u"C_EX_REQUEST_AUTOFISH", &UserPacketEx::RequestAutoFishing
);
L2_BIND_PACKET(
    kBindCExRequestVipAttendanceCheck,
    g_UserPacketEx,
    0x107,
    u"C_EX_REQUEST_VIP_ATTENDANCE_CHECK",
    &UserPacketEx::RequestVipAttendanceCheck
);
L2_BIND_PACKET(
    kBindCExRequestBalthusToken, g_UserPacketEx, 0x11b, u"C_EX_REQUEST_BALTHUS_TOKEN", &UserPacketEx::RequestBalthusToken
);
L2_BIND_PACKET(
    kBindCExRequestAccountAttendanceInfo,
    g_UserPacketEx,
    0x0fc,
    u"C_EX_REQUEST_ACCOUNT_ATTENDANCE_INFO",
    &UserPacketEx::RequestAttendanceStamp
);
L2_BIND_PACKET(
    kBindCExMonsterBookOpen, g_UserPacketEx, 0x12d, u"C_EX_MONSTER_BOOK_OPEN", &UserPacketEx::RequestOpenMonsterBook
);
L2_BIND_PACKET(
    kBindCExMonsterBookClose, g_UserPacketEx, 0x12e, u"C_EX_MONSTER_BOOK_CLOSE", &UserPacketEx::RequestCloseMonsterBook
);
L2_BIND_PACKET(
    kBindCExMonsterBookReward,
    g_UserPacketEx,
    0x12f,
    u"C_EX_REQ_MONSTER_BOOK_REWARD",
    &UserPacketEx::RequestMonsterBookReward
);
L2_BIND_PACKET(kBindCExEnchantClose, g_UserPacketEx, 0x0f8, u"C_EX_ENCHANT_CLOSE", &UserPacketEx::EnchantClose);
L2_BIND_PACKET(kBindCExSetTutorial, g_UserPacketEx, 0x01d, u"C_EX_SET_TUTORIAL", &RequestSetTutorial);
// Current static initializers 0x00dc6c0c, 0x00dc6c3c,
// 0x00dc6a2c, and 0x00dc6a5c.
L2_BIND_PACKET(
    kBindCExCannotMoveAnymoreInAirShip,
    g_UserPacketEx,
    0x01f,
    u"C_EX_CAN_NOT_MOVE_ANYMORE_IN_AIRSHIP",
    &CannotMoveAnymoreInAirShipPacket
);
L2_BIND_PACKET(
    kBindCExMoveToLocationInAirShip,
    g_UserPacketEx,
    0x020,
    u"C_EX_MOVE_TO_LOCATION_IN_AIRSHIP",
    &MoveToLocationInAirShipPacket
);
// Current static initializer 0x00dc6cfc.
L2_BIND_PACKET(
    kBindCExRequestBaseAttributeCancel,
    g_UserPacketEx,
    0x023,
    u"C_EX_REQUEST_BASE_ATTRIBUTE_CANCEL",
    &RequestBaseAttributeCancel
);
L2_BIND_PACKET(
    kBindCExCannotAirShipMoveAnymore,
    g_UserPacketEx,
    0x034,
    u"C_EX_CANNOT_AIRSHIP_MOVE_ANYMORE",
    &CannotAirShipMoveAnymorePacket
);
L2_BIND_PACKET(
    kBindCExMoveToLocationAirShip, g_UserPacketEx, 0x035, u"C_EX_MOVE_TO_LOCATION_AIRSHIP", &MoveToLocationAirShipPacket
);
// Current static initializers 0x00dc7bfc and 0x00dc831c.
L2_BIND_PACKET(kBindCExFlyMove, g_UserPacketEx, 0x091, u"C_EX_FLY_MOVE", &UserPacketEx::FlyMove);
L2_BIND_PACKET(kBindCExFlyMoveStart, g_UserPacketEx, 0x0ad, u"C_EX_FLY_MOVE_START", &UserPacketEx::FlyMoveStart);
// Current static initializer 0x00dc91bc.
L2_BIND_PACKET(kBindCExTarget, g_UserPacketEx, 0x0fe, u"C_EX_TARGET", &UserPacketEx::RequestChangeTarget);
L2_BIND_PACKET(
    kBindCExBrMinigameInsertScoreReq,
    g_UserPacketEx,
    0x08c,
    u"C_EX_BR_MINIGAME_INSERT_SCORE_REQ",
    &RequestBrMinigameInsertScore
);
L2_BIND_PACKET(kBindCExSeedPhase, g_UserPacketEx, 0x060, u"C_EX_SEED_PHASE", &ReplySeedPhase);
L2_BIND_PACKET(
    kBindCExRequestPostItemList, g_UserPacketEx, 0x062, u"C_EX_REQUEST_POST_ITEM_LIST", &UserPacketEx::RequestPostItemList
);
L2_BIND_PACKET(kBindCExSendPost, g_UserPacketEx, 0x063, u"C_EX_SEND_POST", &UserPacketEx::WritePost);
L2_BIND_PACKET(
    kBindCExRequestReceivedPostList,
    g_UserPacketEx,
    0x064,
    u"C_EX_REQUEST_RECEIVED_POST_LIST",
    &UserPacketEx::RequestReceivedPostList
);
L2_BIND_PACKET(
    kBindCExDeleteReceivedPost, g_UserPacketEx, 0x065, u"C_EX_DELETE_RECEIVED_POST", &UserPacketEx::DeleteReceivedPost
);
L2_BIND_PACKET(
    kBindCExRequestReceivedPost, g_UserPacketEx, 0x066, u"C_EX_REQUEST_RECEIVED_POST", &UserPacketEx::RequestReceivedPost
);
L2_BIND_PACKET(kBindCExReceivePost, g_UserPacketEx, 0x067, u"C_EX_RECEIVE_POST", &UserPacketEx::ReceivePost);
L2_BIND_PACKET(kBindCExRejectPost, g_UserPacketEx, 0x068, u"C_EX_REJECT_POST", &UserPacketEx::RejectPost);
L2_BIND_PACKET(
    kBindCExRequestSentPostList, g_UserPacketEx, 0x069, u"C_EX_REQUEST_SENT_POST_LIST", &UserPacketEx::RequestSentPostList
);
L2_BIND_PACKET(kBindCExDeleteSentPost, g_UserPacketEx, 0x06a, u"C_EX_DELETE_SENT_POST", &UserPacketEx::DeleteSentPost);
L2_BIND_PACKET(kBindCExRequestSentPost, g_UserPacketEx, 0x06b, u"C_EX_REQUEST_SENT_POST", &UserPacketEx::RequestSentPost);
L2_BIND_PACKET(kBindCExCancelSendPost, g_UserPacketEx, 0x06c, u"C_EX_CANCEL_SEND_POST", &UserPacketEx::CancelSendPost);
L2_BIND_PACKET(kBindCExShowPostFriend, g_UserPacketEx, 0x083, u"C_EX_SHOW_POST_FRIEND", &UserPacketEx::ShowPostFriend);
L2_BIND_PACKET(
    kBindCExFriendListForPostbox, g_UserPacketEx, 0x084, u"C_EX_FRIEND_LIST_FOR_POSTBOX", &UserPacketEx::SendFriendList
);
L2_BIND_PACKET(kBindCExCheckSpeedHack, g_UserPacketEx, 0x0ce, u"C_EX_CHECK_SPEEDHACK", &UserPacketEx::OnCheckSpeedHack);
L2_BIND_PACKET(
    kBindCExNotifyExitBeautyShop,
    g_UserPacketEx,
    0x0e1,
    u"C_EX_NOTIFY_EXIT_BEAUTYSHOP",
    &UserPacketEx::RequestNotifyExitBeautyShop
);
L2_BIND_PACKET(kBindCExAbilityWndOpen, g_UserPacketEx, 0x0ee, u"C_EX_ABILITY_WND_OPEN", &UserPacketEx::AbilityWndOpen);
L2_BIND_PACKET(kBindCExAbilityWndClose, g_UserPacketEx, 0x0ef, u"C_EX_ABILITY_WND_CLOSE", &UserPacketEx::AbilityWndClose);
L2_BIND_PACKET(
    kBindCExSelectedQuestZoneId,
    g_UserPacketEx,
    0x0ff,
    u"C_EX_SELECTED_QUEST_ZONEID",
    &UserPacketEx::RequestSetSelectedQuestZondID
);
L2_BIND_PACKET(kBindCExInteractModify, g_UserPacketEx, 0x15e, u"C_EX_INTERACT_MODIFY", &UserPacketEx::RequestInteractModify);
// Current EX04 initializer 0x00dca26c.
L2_BIND_PACKET(
    kBindCExTryEnchantArtifact, g_UserPacketEx, 0x15f, u"C_EX_TRY_ENCHANT_ARTIFACT", &UserPacketEx::RequestTryEnchantArtifact
);
L2_BIND_PACKET(
    kBindCExPledgeContributionRank,
    g_UserPacketEx,
    0x13e,
    u"C_EX_PLEDGE_CONTRIBUTION_RANK",
    &UserPacketParser<protocol::C_EX_PLEDGE_CONTRIBUTION_RANK>
);
L2_BIND_PACKET(
    kBindCExPledgeContributionInfo,
    g_UserPacketEx,
    0x13f,
    u"C_EX_PLEDGE_CONTRIBUTION_INFO",
    &UserPacketParser<protocol::C_EX_PLEDGE_CONTRIBUTION_INFO>
);
L2_BIND_PACKET(
    kBindCExPledgeContributionReward,
    g_UserPacketEx,
    0x140,
    u"C_EX_PLEDGE_CONTRIBUTION_REWARD",
    &UserPacketParser<protocol::C_EX_PLEDGE_CONTRIBUTION_REWARD>
);
L2_BIND_PACKET(
    kBindCExPledgeLevelUp, g_UserPacketEx, 0x141, u"C_EX_PLEDGE_LEVEL_UP", &UserPacketParser<protocol::C_EX_PLEDGE_LEVEL_UP>
);
L2_BIND_PACKET(
    kBindCExPledgeMissionReward,
    g_UserPacketEx,
    0x143,
    u"C_EX_PLEDGE_MISSION_REWARD",
    &UserPacketEx::RequestPledgeMissionReward
);
L2_BIND_PACKET(
    kBindCExPledgeMasteryInfo,
    g_UserPacketEx,
    0x144,
    u"C_EX_PLEDGE_MASTERY_INFO",
    &UserPacketParser<protocol::C_EX_PLEDGE_MASTERY_INFO>
);
L2_BIND_PACKET(
    kBindCExPledgeMasterySet,
    g_UserPacketEx,
    0x145,
    u"C_EX_PLEDGE_MASTERY_SET",
    &UserPacketParser<protocol::C_EX_PLEDGE_MASTERY_SET>
);
L2_BIND_PACKET(
    kBindCExPledgeMasteryReset,
    g_UserPacketEx,
    0x146,
    u"C_EX_PLEDGE_MASTERY_RESET",
    &UserPacketParser<protocol::C_EX_PLEDGE_MASTERY_RESET>
);
L2_BIND_PACKET(
    kBindCExPledgeSkillInfo, g_UserPacketEx, 0x147, u"C_EX_PLEDGE_SKILL_INFO", &UserPacketParser<protocol::C_EX_PLEDGE_SKILL_INFO>
);
L2_BIND_PACKET(
    kBindCExPledgeSkillActivate,
    g_UserPacketEx,
    0x148,
    u"C_EX_PLEDGE_SKILL_ACTIVATE",
    &UserPacketParser<protocol::C_EX_PLEDGE_SKILL_ACTIVATE>
);
L2_BIND_PACKET(
    kBindCExPledgeBonusUiOpen, g_UserPacketEx, 0x122, u"C_EX_PLEDGE_BONUS_UI_OPEN", &UserPacketEx::RequesPledgeBonusUIOpen
);
L2_BIND_PACKET(
    kBindCExPledgeBonusRewardList,
    g_UserPacketEx,
    0x123,
    u"C_EX_PLEDGE_BONUS_REWARD_LIST",
    &UserPacketEx::RequesPledgeBonusRewardList
);
L2_BIND_PACKET(
    kBindCExPledgeBonusReceiveReward,
    g_UserPacketEx,
    0x124,
    u"C_EX_PLEDGE_BONUS_RECEIVE_REWARD",
    &UserPacketEx::RequesPledgeBonusReceiveReward
);
// Current EX08 initializer 0x00dc87cc.
L2_BIND_PACKET(
    kBindCExQueueTicketLogin, g_UserPacketEx, 0x126, u"C_EX_QUEUETICKET_LOGIN", &UserPacketEx::QueueTicketLoginPacket
);
// Current EX09 initializer 0x00dc97ec.
L2_BIND_PACKET(
    kBindCExBlockDetailInfo, g_UserPacketEx, 0x127, u"C_EX_BLOCK_DETAIL_INFO", &UserPacketEx::RequestBlockDetailInfo
);
L2_BIND_PACKET(
    kBindCExPledgeMissionInfo, g_UserPacketEx, 0x142, u"C_EX_PLEDGE_MISSION_INFO", &UserPacketEx::RequestPledgeMissionInfo
);
L2_BIND_PACKET(
    kBindCExPledgeItemList, g_UserPacketEx, 0x149, u"C_EX_PLEDGE_ITEM_LIST", &UserPacketParser<protocol::C_EX_PLEDGE_ITEM_LIST>
);
L2_BIND_PACKET(
    kBindCExPledgeItemActivate,
    g_UserPacketEx,
    0x14a,
    u"C_EX_PLEDGE_ITEM_ACTIVATE",
    &UserPacketParser<protocol::C_EX_PLEDGE_ITEM_ACTIVATE>
);
L2_BIND_PACKET(
    kBindCExPledgeAnnounce, g_UserPacketEx, 0x14b, u"C_EX_PLEDGE_ANNOUNCE", &UserPacketParser<protocol::C_EX_PLEDGE_ANNOUNCE>
);
L2_BIND_PACKET(
    kBindCExPledgeAnnounceSet,
    g_UserPacketEx,
    0x14c,
    u"C_EX_PLEDGE_ANNOUNCE_SET",
    &UserPacketParser<protocol::C_EX_PLEDGE_ANNOUNCE_SET>
);
// Current EX01 initializer 0x00dc9f6c.
L2_BIND_PACKET(kBindCExCreatePledge, g_UserPacketEx, 0x14d, u"C_EX_CREATE_PLEDGE", &UserPacketEx::RequestCreatePledge);
L2_BIND_PACKET(
    kBindCExPledgeItemInfo, g_UserPacketEx, 0x14e, u"C_EX_PLEDGE_ITEM_INFO", &UserPacketParser<protocol::C_EX_PLEDGE_ITEM_INFO>
);
L2_BIND_PACKET(
    kBindCExPledgeItemBuy, g_UserPacketEx, 0x14f, u"C_EX_PLEDGE_ITEM_BUY", &UserPacketParser<protocol::C_EX_PLEDGE_ITEM_BUY>
);
// Current EX05 initializers 0x00dc7efc, 0x00dc7f2c.
L2_BIND_PACKET(
    kBindCExCallToChangeClass, g_UserPacketEx, 0x0a1, u"C_EX_CALL_TO_CHANGE_CLASS", &UserPacketEx::CallToChangeClass
);
L2_BIND_PACKET(
    kBindCExChangeToAwakenedClass,
    g_UserPacketEx,
    0x0a2,
    u"C_EX_CHANGE_TO_AWAKENED_CLASS",
    &UserPacketEx::ChangeToAwakenedClass
);
// Current EX05 initializers 0x00dc8dcc, 0x00dc8dfc, 0x00dc8e2c.
L2_BIND_PACKET(
    kBindCExAcquirePotentialSkill,
    g_UserPacketEx,
    0x0e9,
    u"C_EX_ACQUIRE_POTENTIAL_SKILL",
    &UserPacketEx::RequestAcquirePotentailSkill
);
L2_BIND_PACKET(
    kBindCExRequestPotentialSkillList,
    g_UserPacketEx,
    0x0ea,
    u"C_EX_REQUEST_POTENTIAL_SKILL_LIST",
    &UserPacketEx::RequestPotentialSkillList
);
L2_BIND_PACKET(
    kBindCExResetPotentialSkill,
    g_UserPacketEx,
    0x0eb,
    u"C_EX_RESET_POTENTIAL_SKILL",
    &UserPacketEx::RequestResetPotentialSkill
);
// Current EX04 initializers 0x00dc8fdc, 0x00dc900c, 0x00dc903c,
// 0x00dc906c.
L2_BIND_PACKET(kBindCExEnchantOne, g_UserPacketEx, 0x0f4, u"C_EX_ENCHANT_ONE", &UserPacketEx::EnchantOne);
L2_BIND_PACKET(kBindCExEnchantOneRemove, g_UserPacketEx, 0x0f5, u"C_EX_ENCHANT_ONE_REMOVE", &UserPacketEx::EnchantOneRemove);
L2_BIND_PACKET(kBindCExEnchantTwo, g_UserPacketEx, 0x0f6, u"C_EX_ENCHANT_TWO", &UserPacketEx::EnchantTwo);
L2_BIND_PACKET(kBindCExEnchantTwoRemove, g_UserPacketEx, 0x0f7, u"C_EX_ENCHANT_TWO_REMOVE", &UserPacketEx::EnchantTwoRemove);
// Current EX04 initializer 0x00dc909c.
L2_BIND_PACKET(kBindCExEnchantTry, g_UserPacketEx, 0x0f9, u"C_EX_ENCHANT_TRY", &UserPacketEx::EnchantTry);
L2_BIND_PACKET(
    kBindCExEnchantRetryToPutItems,
    g_UserPacketEx,
    0x0fa,
    u"C_EX_ENCHANT_RETRY_TO_PUT_ITEMS",
    &UserPacketEx::EnchantRetryToPutItems
);
// Current EX05 initializer 0x00dc921c.
L2_BIND_PACKET(
    kBindCExAlchemySkillList, g_UserPacketEx, 0x100, u"C_EX_ALCHEMY_SKILL_LIST", &UserPacketEx::RequestAlchemySkillList
);
// Current EX15 initializer 0x00dc924c.
L2_BIND_PACKET(kBindCExTryMixCube, g_UserPacketEx, 0x101, u"C_EX_TRY_MIX_CUBE", &UserPacketEx::TryMixCube);
// Current EX06 initializer 0x00dc927c.
L2_BIND_PACKET(
    kBindCExAlchemyConversion,
    g_UserPacketEx,
    0x102,
    u"C_REQUEST_ALCHEMY_CONVERSION",
    &UserPacketEx::RequestAlchemyConversion
);
// Current EX04 initializer 0x00dc939c.
L2_BIND_PACKET(kBindCExTryEnsoul, g_UserPacketEx, 0x108, u"C_EX_TRY_ENSOUL", &UserPacketEx::RequestTryEnsoul);
L2_BIND_PACKET(kBindCExDecoNpcInfo, g_UserPacketEx, 0x117, u"C_EX_DECO_NPC_INFO", &UserPacketEx::Request_C_EX_DECO_NPC_INFO);
L2_BIND_PACKET(kBindCExDecoNpcSet, g_UserPacketEx, 0x118, u"C_EX_DECO_NPC_SET", &UserPacketEx::Request_C_EX_DECO_NPC_SET);
L2_BIND_PACKET(kBindCExFactionInfo, g_UserPacketEx, 0x119, u"C_EX_FACTION_INFO", &UserPacketEx::RequestFactionInfo);
// Current EX04 initializer 0x00dc981c.
L2_BIND_PACKET(
    kBindCExTryEnsoulExtraction, g_UserPacketEx, 0x128, u"C_EX_TRY_ENSOUL_EXTRACTION", &UserPacketEx::RequestEnsoulExtraction
);
L2_BIND_PACKET(
    kBindCExRaidBossSpawnInfo, g_UserPacketEx, 0x129, u"C_EX_RAID_BOSS_SPAWN_INFO", &UserPacketEx::RequestRaidBossSpawnInfo
);
L2_BIND_PACKET(
    kBindCExUpgradeSystemRequest, g_UserPacketEx, 0x136, u"C_EX_UPGRADE_SYSTEM_REQUEST", &UserPacketEx::RequestUpgradeSystem
);
L2_BIND_PACKET(
    kBindCExRequestLockedItem, g_UserPacketEx, 0x159, u"C_EX_REQUEST_LOCKED_ITEM", &UserPacketEx::RequestLockedItem
);
L2_BIND_PACKET(
    kBindCExRequestUnlockedItem, g_UserPacketEx, 0x15a, u"C_EX_REQUEST_UNLOCKED_ITEM", &UserPacketEx::RequestUnLockedItem
);
L2_BIND_PACKET(
    kBindCExLockedItemCancel, g_UserPacketEx, 0x15b, u"C_EX_LOCKED_ITEM_CANCEL", &UserPacketEx::RequestLockedItemCancel
);
L2_BIND_PACKET(
    kBindCExUnlockedItemCancel, g_UserPacketEx, 0x15c, u"C_EX_UNLOCKED_ITEM_CANCEL", &UserPacketEx::RequestUnLockedItemCancel
);
// Current EX03 initializers 0x00dc6a8c, 0x00dc6abc and 0x00dc98dc.
L2_BIND_PACKET(kBindCExItemAuctionBid, g_UserPacketEx, 0x036, u"C_EX_ITEM_AUCTION_BID", &RequestItemAuctionBid);
L2_BIND_PACKET(kBindCExItemAuctionInfo, g_UserPacketEx, 0x037, u"C_EX_ITEM_AUCTION_INFO", &RequestItemAuctionInfo);
L2_BIND_PACKET(
    kBindCExItemAuctionStatus, g_UserPacketEx, 0x12c, u"C_EX_ITEM_AUCTION_STATUS", &UserPacketEx::RequestItemAuctionStatus
);
// Current EX03 cash-shop initializers 0x00dc79ec..0x00dc7aac,
// 0x00dc861c and 0x00dc88ec..0x00dc894c.
L2_BIND_PACKET(kBindCExBrGamePointReq, g_UserPacketEx, 0x086, u"C_EX_BR_GAME_POINT_REQ", &RequestBrGamePoint);
L2_BIND_PACKET(kBindCExBrProductListReq, g_UserPacketEx, 0x087, u"C_EX_BR_PRODUCT_LIST_REQ", &RequestBrProductList);
L2_BIND_PACKET(kBindCExBrProductInfoReq, g_UserPacketEx, 0x088, u"C_EX_BR_PRODUCT_INFO_REQ", &RequestBrProductInfo);
L2_BIND_PACKET(kBindCExBrBuyProductReq, g_UserPacketEx, 0x089, u"C_EX_BR_BUY_PRODUCT_REQ", &RequestBrBuyProduct);
L2_BIND_PACKET(kBindCExBrRecentProductReq, g_UserPacketEx, 0x08a, u"C_EX_BR_RECENT_PRODUCT_REQ", &RequestBrRecentProduct);
L2_BIND_PACKET(
    kBindCExBrBuyProductGiftReq,
    g_UserPacketEx,
    0x0b3,
    u"C_EX_BR_BUY_PRODUCT_GIFT_REQ",
    &UserPacketEx::RequestBrBuyProductForGift
);
L2_BIND_PACKET(
    kBindCExBrAddInterestedProduct,
    g_UserPacketEx,
    0x0cf,
    u"C_EX_BR_ADD_INTERESTED_PRODUCT",
    &UserPacketEx::RequestBrAddInterestedProduct
);
L2_BIND_PACKET(
    kBindCExBrDeleteInterestedProduct,
    g_UserPacketEx,
    0x0d0,
    u"C_EX_BR_DELETE_INTERESTED_PRODUCT",
    &UserPacketEx::RequestBrDeleteInterestedProduct
);
L2_BIND_PACKET(
    kBindCExBrExistNewProductReq,
    g_UserPacketEx,
    0x0d1,
    u"C_EX_BR_EXIST_NEW_PRODUCT_REQ",
    &UserPacketEx::RequestBrExistNewProduct
);
// Current EX03 commission initializers 0x00dc7d4c..0x00dc7ecc.
L2_BIND_PACKET(
    kBindCExRequestCommissionItemList,
    g_UserPacketEx,
    0x098,
    u"C_EX_REQUEST_COMMISSION_ITEM_LIST",
    &UserPacketEx::RequestCommissionItemList
);
L2_BIND_PACKET(
    kBindCExRequestCommissionInfo,
    g_UserPacketEx,
    0x099,
    u"C_EX_REQUEST_COMMISSION_INFO",
    &UserPacketEx::RequestCommissionInfo
);
L2_BIND_PACKET(
    kBindCExRequestCommissionRegister,
    g_UserPacketEx,
    0x09a,
    u"C_EX_REQUEST_COMMISSION_REGISTER",
    &UserPacketEx::RequestCommissionRegister
);
L2_BIND_PACKET(
    kBindCExRequestCommissionCancel,
    g_UserPacketEx,
    0x09b,
    u"C_EX_REQUEST_COMMISSION_CANCEL",
    &UserPacketEx::RequestCommissionCancel
);
L2_BIND_PACKET(
    kBindCExRequestCommissionDelete,
    g_UserPacketEx,
    0x09c,
    u"C_EX_REQUEST_COMMISSION_DELETE",
    &UserPacketEx::RequestCommissionDelete
);
L2_BIND_PACKET(
    kBindCExRequestCommissionSearch,
    g_UserPacketEx,
    0x09d,
    u"C_EX_REQUEST_COMMISSION_SEARCH",
    &UserPacketEx::RequestCommissionSearch
);
L2_BIND_PACKET(
    kBindCExRequestCommissionBuyInfo,
    g_UserPacketEx,
    0x09e,
    u"C_EX_REQUEST_COMMISSION_BUY_INFO",
    &UserPacketEx::RequestCommissionBuyInfo
);
L2_BIND_PACKET(
    kBindCExRequestCommissionBuyItem,
    g_UserPacketEx,
    0x09f,
    u"C_EX_REQUEST_COMMISSION_BUY_ITEM",
    &UserPacketEx::RequestCommissionBuyItem
);
L2_BIND_PACKET(
    kBindCExRequestCommissionRegisteredItem,
    g_UserPacketEx,
    0x0a0,
    u"C_EX_REQUEST_COMMISSION_REGISTERED_ITEM",
    &UserPacketEx::RequestCommissionRegisteredItem
);

// Current EX13 initializers and exact named dispatch roots.
L2_BIND_PACKET(
    kBindCExNotifyStartMinigame,
    g_UserPacketEx,
    0x053,
    u"C_EX_NOTIFY_START_MINIGAME",
    &UserPacketEx::NotifyStartMinigamePacket
);
L2_BIND_PACKET(kBindCExCleftEnter, g_UserPacketEx, 0x056, u"C_EX_CLEFT_ENTER", &UserPacketEx::CleftEnter);
L2_BIND_PACKET(
    kBindCExEventCampaignInfo, g_UserPacketEx, 0x0d2, u"C_EX_EVENT_CAMPAIGN_INFO", &CEventCampaignManager::UserPacketHandler
);
L2_BIND_PACKET(
    kBindCExRegisterXmasWishCard,
    g_UserPacketEx,
    0x0e2,
    u"C_EX_EVENT_REGISTER_XMAS_WISHCARD",
    &UserPacketEx::RequestRegisterXmasWish
);
L2_BIND_PACKET(
    kBindCExSelectCardReward,
    g_UserPacketEx,
    0x0e5,
    u"C_EX_SELECT_CARD_REWARD",
    &UserPacketEx::RequestRewardCardCollectSystem
);
L2_BIND_PACKET(
    kBindCExStartLuckyGame, g_UserPacketEx, 0x0f1, u"C_EX_START_LUCKY_GAME", &UserPacketEx::RequestStartLuckyGame
);
L2_BIND_PACKET(
    kBindCExBettingLuckyGame, g_UserPacketEx, 0x0f2, u"C_EX_BETTING_LUCKY_GAME", &UserPacketEx::RequestBettingLuckyGame
);
L2_BIND_PACKET(
    kBindCExTrainingZoneLeaving,
    g_UserPacketEx,
    0x0f3,
    u"C_EX_TRAININGZONE_LEAVING",
    &UserPacketEx::RequestTrainingZoneLeaving
);
L2_BIND_PACKET(kBindCExToDoList, g_UserPacketEx, 0x11e, u"C_EX_TODOLIST", &UserPacketEx::RequesToDoList);
L2_BIND_PACKET(kBindCExToDoListHtml, g_UserPacketEx, 0x11f, u"C_EX_TODOLIST_HTML", &UserPacketEx::RequesToDoListHtml);
L2_BIND_PACKET(
    kBindCExOneDayReceiveReward,
    g_UserPacketEx,
    0x120,
    u"C_EX_ONE_DAY_RECEIVE_REWARD",
    &UserPacketEx::RequestOneDayReceiveReward
);
L2_BIND_PACKET(
    kBindCExCardUpdownPickNumber,
    g_UserPacketEx,
    0x137,
    u"C_EX_CARD_UPDOWN_PICK_NUMB",
    &UserPacketEx::RequestPickCardUpdownGame
);
L2_BIND_PACKET(
    kBindCExCardUpdownGameReward,
    g_UserPacketEx,
    0x138,
    u"C_EX_CARD_UPDOWN_GAME_REWARD_REQUEST",
    &UserPacketEx::RequestCardUpdownGameReward
);
L2_BIND_PACKET(
    kBindCExCardUpdownGameRetry,
    g_UserPacketEx,
    0x139,
    u"C_EX_CARD_UPDOWN_GAME_RETRY",
    &UserPacketEx::RequestRetryCardUpdownGame
);

// Current UserPacketEx.cpp static registrations. These ten slots are
// routed to the common rejection handler at 0x00be97b8. They
// do not have packet-specific parsers in the current product.
L2_BIND_PACKET(kBindCExOlympiadMatchList, g_UserPacketEx, 0x02e, u"C_EX_OLYMPIAD_MATCH_LIST", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExJump, g_UserPacketEx, 0x050, u"C_EX_JUMP", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExLoadInzonePartyHistory, g_UserPacketEx, 0x097, u"C_EX_LOAD_INZONE_PARTY_HISTORY", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExNotUsed163, g_UserPacketEx, 0x0a3, u"C_EX_NOT_USED_163", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExNotUsed164, g_UserPacketEx, 0x0a4, u"C_EX_NOT_USED_164", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExChangePotentialPoint, g_UserPacketEx, 0x0ec, u"C_EX_CHANGE_POTENTIAL_POINT", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExCastlewarSeasonReward, g_UserPacketEx, 0x109, u"C_EX_CASTLEWAR_SEASON_REWARD", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExBrVipProductListReq, g_UserPacketEx, 0x10a, u"C_EX_BR_VIP_PRODUCT_LIST_REQ", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExRequestLuckyGameInfo, g_UserPacketEx, 0x10b, u"C_EX_REQUEST_LUCKY_GAME_INFO", &DummyUserPacketEx);
L2_BIND_PACKET(
    kBindCExRequestLuckyGameItemList, g_UserPacketEx, 0x10c, u"C_EX_REQUEST_LUCKY_GAME_ITEMLIST", &DummyUserPacketEx
);
L2_BIND_PACKET(kBindCExRequestLuckyGameBonus, g_UserPacketEx, 0x10d, u"C_EX_REQUEST_LUCKY_GAME_BONUS", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExVipInfo, g_UserPacketEx, 0x10e, u"C_EX_VIP_INFO", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExRequestMatchArena, g_UserPacketEx, 0x112, u"C_EX_REQUEST_MATCH_ARENA", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExConfirmMatchArena, g_UserPacketEx, 0x113, u"C_EX_CONFIRM_MATCH_ARENA", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExCancelMatchArena, g_UserPacketEx, 0x114, u"C_EX_CANCEL_MATCH_ARENA", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExChangeClassArena, g_UserPacketEx, 0x115, u"C_EX_CHANGE_CLASS_ARENA", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExConfirmClassArena, g_UserPacketEx, 0x116, u"C_EX_CONFIRM_CLASS_ARENA", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExExitArena, g_UserPacketEx, 0x11a, u"C_EX_EXIT_ARENA", &DummyUserPacketEx);
L2_BIND_PACKET(
    kBindCExArenaCustomNotification, g_UserPacketEx, 0x11d, u"C_EX_ARENA_CUSTOM_NOTIFICATION", &DummyUserPacketEx
);
L2_BIND_PACKET(kBindCExMatchgroup, g_UserPacketEx, 0x130, u"C_EX_MATCHGROUP", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExMatchgroupAsk, g_UserPacketEx, 0x131, u"C_EX_MATCHGROUP_ASK", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExMatchgroupAnswer, g_UserPacketEx, 0x132, u"C_EX_MATCHGROUP_ANSWER", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExMatchgroupWithdraw, g_UserPacketEx, 0x133, u"C_EX_MATCHGROUP_WITHDRAW", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExMatchgroupOust, g_UserPacketEx, 0x134, u"C_EX_MATCHGROUP_OUST", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExMatchgroupChangeMaster, g_UserPacketEx, 0x135, u"C_EX_MATCHGROUP_CHANGE_MASTER", &DummyUserPacketEx);
// Current EX08 initializer 0x00dc9b7c.
L2_BIND_PACKET(
    kBindCExCardUpdownGameQuit,
    g_UserPacketEx,
    0x13a,
    u"C_EX_CARD_UPDOWN_GAME_QUIT",
    &UserPacketEx::RequestQuitCardUpdownGame
);
L2_BIND_PACKET(kBindCExArenaRankAll, g_UserPacketEx, 0x13b, u"C_EX_ARENA_RANK_ALL", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExArenaMyRank, g_UserPacketEx, 0x13c, u"C_EX_ARENA_MYRANK", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExSwapAgathionSlotItems, g_UserPacketEx, 0x13d, u"C_EX_SWAP_AGATHION_SLOT_ITEMS", &DummyUserPacketEx);
L2_BIND_PACKET(kBindCExElementalSpiritInfo, g_UserPacketEx, 0x150, u"C_EX_ELEMENTAL_SPIRIT_INFO", &DummyUserPacketEx);
L2_BIND_PACKET(
    kBindCExElementalSpiritExtractInfo, g_UserPacketEx, 0x151, u"C_EX_ELEMENTAL_SPIRIT_EXTRACT_INFO", &DummyUserPacketEx
);
L2_BIND_PACKET(kBindCExElementalSpiritExtract, g_UserPacketEx, 0x152, u"C_EX_ELEMENTAL_SPIRIT_EXTRACT", &DummyUserPacketEx);
L2_BIND_PACKET(
    kBindCExElementalSpiritEvolutionInfo, g_UserPacketEx, 0x153, u"C_EX_ELEMENTAL_SPIRIT_EVOLUTION_INFO", &DummyUserPacketEx
);
L2_BIND_PACKET(
    kBindCExElementalSpiritEvolution, g_UserPacketEx, 0x154, u"C_EX_ELEMENTAL_SPIRIT_EVOLUTION", &DummyUserPacketEx
);
L2_BIND_PACKET(
    kBindCExElementalSpiritSetTalent, g_UserPacketEx, 0x155, u"C_EX_ELEMENTAL_SPIRIT_SET_TALENT", &DummyUserPacketEx
);
L2_BIND_PACKET(
    kBindCExElementalSpiritInitTalent, g_UserPacketEx, 0x156, u"C_EX_ELEMENTAL_SPIRIT_INIT_TALENT", &DummyUserPacketEx
);
L2_BIND_PACKET(
    kBindCExElementalSpiritAbsorbInfo, g_UserPacketEx, 0x157, u"C_EX_ELEMENTAL_SPIRIT_ABSORB_INFO", &DummyUserPacketEx
);
L2_BIND_PACKET(kBindCExElementalSpiritAbsorb, g_UserPacketEx, 0x158, u"C_EX_ELEMENTAL_SPIRIT_ABSORB", &DummyUserPacketEx);
L2_BIND_PACKET(
    kBindCExElementalSpiritChangeType, g_UserPacketEx, 0x15d, u"C_EX_ELEMENTAL_SPIRIT_CHANGE_TYPE", &DummyUserPacketEx
);

} // namespace

bool SetPledgeEmblem(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be0704, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t pieceIndex = 0;
    std::int32_t totalFileSize = 0;
    std::int32_t pieceSize = 0;
    const std::uint8_t* const pieceData = ::Disassemble(packet, "ddd", &pieceIndex, &totalFileSize, &pieceSize);
    // Log.Add(
    //     LOG_DEBUG,
    //     "C_EX_SET_PLEDGE_EMBLEM %d %d %d",
    //     pieceIndex,
    //     totalFileSize,
    //     pieceSize)
    L2_LOG_DEBUG(Log, "C_EX_SET_PLEDGE_EMBLEM %d %d %d", pieceIndex, totalFileSize, pieceSize);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !user->HavePledgePower(PP_SET_CREST)) {
        socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        return false;
    }
    if (!pledge->GetFunction()->IsEnable(PledgeFunctionType::CREST)) {
        socket->SendSystemMessage(SystemMessageId::CLAN_LEVEL_TOO_LOW_TO_REGISTER_CREST);
        return false;
    }
    if (pledge->IsWaitingToDismiss()) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_CHANGE_CLAN_CREST_DURING_DISSOLUTION_GRACE_PERIOD);
        return false;
    }
    if (static_cast<std::uint32_t>(pieceSize) < 0x3801U) {
        db.RequestSetPledgeEmblem(user, pledge, pieceIndex, totalFileSize, pieceSize, pieceData, false);
    }
    return false;
}

bool PledgeEmblem(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be08c0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::uint32_t emblemId = 0;
    std::uint32_t pledgeServerId = 0;
    ::Disassemble(packet, "dd", &emblemId, &pledgeServerId);
    return user->OnPledgeEmblem(emblemId, pledgeServerId);
}

bool SetAcademyMaster(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be0ad8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int setMaster = 0;
    std::array<char16_t, 25> masterName{};
    std::array<char16_t, 25> academyMemberName{};
    ::Disassemble(packet, "dSS", &setMaster, 0x32, masterName.data(), 0x32, academyMemberName.data());
    std::wstring hostMasterName = Utf16ToWide(masterName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    std::wstring hostAcademyMemberName = Utf16ToWide(academyMemberName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !pledge->IsPledgeTypeMember(hostAcademyMemberName.data(), ACADEMY)) {
        return false;
    }

    if (setMaster == 1) {
        if (pledge->IsPledgeTypeMember(hostMasterName.data(), ACADEMY)) {
            return false;
        }

        const std::uint32_t academyMemberDatabaseId = pledge->GetMemberDbId(hostAcademyMemberName.data());
        if (pledge->HasAcademyMaster(academyMemberDatabaseId)) {
            pledge->ViewPledgeMemberInfo(user, hostMasterName.data());
            return false;
        }

        const std::uint32_t masterDatabaseId = pledge->GetMemberDbId(hostMasterName.data());
        if (pledge->HasAcademyStudent(masterDatabaseId)) {
            pledge->ViewPledgeMemberInfo(user, hostMasterName.data());
            return false;
        }
        if (!pledge->CanBeAcademyMaster(masterDatabaseId)) {
            user->SendSystemMessage(SystemMessageId::ACADEMY_SPONSOR_REQUIRES_AWAKENING_AND_LEVEL_85);
            return false;
        }

        int transaction = 0;
        if (!pledge->BeginTransaction(&transaction)) {
            socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
            return false;
        }
        const std::uint32_t requestMasterDatabaseId = pledge->GetMemberDbId(hostMasterName.data());
        const std::uint32_t requestAcademyMemberDatabaseId = pledge->GetMemberDbId(hostAcademyMemberName.data());
        const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
        db.RequestSetAcademyMaster(user, pledgeDatabaseId, requestAcademyMemberDatabaseId, requestMasterDatabaseId);
        return false;
    }

    const std::uint32_t academyMemberDatabaseId = pledge->GetMemberDbId(hostAcademyMemberName.data());
    if (!pledge->HasAcademyMaster(academyMemberDatabaseId)) {
        pledge->ViewPledgeMemberInfo(user, hostMasterName.data());
        return false;
    }

    int transaction = 0;
    if (!pledge->BeginTransaction(&transaction)) {
        socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        return false;
    }
    const std::uint32_t requestAcademyMemberDatabaseId = pledge->GetMemberDbId(hostAcademyMemberName.data());
    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    db.RequestSetAcademyMaster(user, pledgeDatabaseId, requestAcademyMemberDatabaseId, 0);
    return false;
}

bool RequestPledgePowerList(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00be0da0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr) {
        pledge->SendPledgPowerListPacket(user);
    }
    return false;
}

bool RequestViewPledgePower(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be0e74, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    PledgeType pledgeType;
    std::array<char16_t, 25> characterName{};
    ::Disassemble(packet, "dS", &pledgeType, 0x32, characterName.data());
    std::wstring hostCharacterName = Utf16ToWide(characterName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr) {
        pledge->ViewPledgPowerPacket(user, pledgeType, hostCharacterName.data());
    }
    return false;
}

bool RequestSetPledgePowerGrade(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf5a00, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<char16_t, 25> characterName{};
    int gradeId;
    ::Disassemble(packet, "Sd", 0x32, characterName.data(), &gradeId);
    std::wstring hostCharacterName = Utf16ToWide(characterName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return false;
    }
    if (!user->IsPledgeMaster() && !user->HavePledgePower(PP_MANAGE_GRADE)) {
        socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        return false;
    }

    const int previousGradeId = pledge->GetPledgePowerGrade(hostCharacterName.data());
    const std::uint32_t characterDatabaseId = pledge->SetPledgePowerGrade(hostCharacterName.data(), gradeId);
    if (characterDatabaseId == 0) {
        return false;
    }

    CSPointer<User> member =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(characterDatabaseId)));
    if (member) {
        member->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(member->m_data->m_nGradeId, gradeId, 0x46);
        member->GetSocket()->Send("chd", 0xfe, 0x43, pledge->GetPowerGrade(gradeId));
    }

    user->SendSystemMessage(SystemMessageId::TARGET_PRIVILEGES_CHANGED);
    pledge->ViewPledgeMemberInfo(hostCharacterName.data());
    db.RequestSetPledgePowerGrade(characterDatabaseId, gradeId);

    const char16_t* memberAccountName = member ? member->m_sAccountName.data() : u"";
    const std::int64_t memberAccountId = member ? member->GetAccountID() : 0;
    const std::u16string actorName = std::u16string(user->GetName());
    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    const std::u16string pledgeName = std::u16string(pledge->GetPledgeName());
    const std::uint32_t actorAccountId = user->GetAccountID();
    const std::uint32_t actorDatabaseId = user->GetDBID();
    CLogFormatter formatter;
    formatter.WriteLog(
        0x120, actorDatabaseId, actorAccountId, characterDatabaseId, memberAccountId, 0, 0, 0, nullptr, pledgeName.c_str(),
        nullptr, pledgeDatabaseId, previousGradeId, gradeId, 0, 0, 0, 0, 0, 0, 0, actorName.c_str(),
        user->m_sAccountName.data(), characterName.data(), memberAccountName, 0
    );
    return false;
}

bool RequestViewPledgeMemberInfo(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be0f94, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    PledgeType pledgeType;
    std::array<char16_t, 25> characterName{};
    ::Disassemble(packet, "dS", &pledgeType, 0x32, characterName.data());
    std::wstring hostCharacterName = Utf16ToWide(characterName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr) {
        pledge->ViewPledgeMemberInfo(user, pledgeType, hostCharacterName.data());
    }
    return false;
}

bool RequestViewPledgeWarList(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be1134, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::uint32_t page;
    int listCondition;
    ::Disassemble(packet, "dd", &page, &listCondition);
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        socket->SendSystemMessage(SystemMessageId::NOT_IN_CLAN);
    } else {
        pledge->SendViewPledgeWarList(socket, page, listCondition);
    }
    return false;
}

bool RequestReorganizePledgeMember(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be1324, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !user->m_data->m_bPledgeMaster) {
        socket->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        return false;
    }

    int action = 0;
    std::array<char16_t, 25> firstCharacterName{};
    PledgeType pledgeType = MASTER_PLEDGE;
    std::array<char16_t, 25> secondCharacterName{};
    ::Disassemble(packet, "dSdS", &action, 0x32, firstCharacterName.data(), &pledgeType, 0x32, secondCharacterName.data());
    std::wstring hostFirstCharacterName = Utf16ToWide(firstCharacterName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    std::wstring hostSecondCharacterName =
        Utf16ToWide(secondCharacterName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    const std::uint32_t firstCharacterDatabaseId = pledge->GetMemberDbId(hostFirstCharacterName.data());
    const std::uint32_t secondCharacterDatabaseId = pledge->GetMemberDbId(hostSecondCharacterName.data());

    if (!user->IsPledgeMaster() && !user->HavePledgePower(PP_MANAGE_GRADE)) {
        user->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        return false;
    }
    if (action != 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CHANGE_CLAN_MEMBER_DIVISION_REQUIREMENTS_NOT_MET);
        return false;
    }

    const bool canReorganize = pledgeType == SUB_1 ? pledge->CanAppointElite(static_cast<int>(firstCharacterDatabaseId))
                                                   : pledge->CanDismissElite(static_cast<int>(firstCharacterDatabaseId));
    if (!canReorganize) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CHANGE_CLAN_MEMBER_DIVISION_REQUIREMENTS_NOT_MET);
        return false;
    }

    user->SendSystemMessage(SystemMessageId::TARGET_DIVISION_CHANGED);
    int transaction = 0;
    if (!pledge->BeginTransaction(&transaction)) {
        socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        return false;
    }
    db.RequestReorganizePledgeMember(
        user, pledge, action, firstCharacterDatabaseId, pledgeType == SUB_1 ? SUB_1 : MASTER_PLEDGE,
        secondCharacterDatabaseId
    );
    return false;
}

bool DummyUserPacketEx(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00be97b8.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    if (baseSocket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "DummyUserPacketEx called in UserPacketEx")
        // L2_LOG_ERROR(Log,
        //     "DummyUserPacketEx called in UserPacketEx")
        L2_LOG_ERROR(Log, "Unsupported extended client packet handler invoked with a null socket");
        return true;
    }

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    const int packetId = socket->GetLastPacketNo();
    const int extendedPacketId = socket->GetLastEXPacketNo();
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "DummyUserPacketEx called in UserPacketEx C[%d] EX[%d]",
        //     packetId,
        //     extendedPacketId)
        // L2_LOG_ERROR(Log,
        //     "DummyUserPacketEx called in UserPacketEx C[%d] EX[%d]",
        //     packetId,
        //     extendedPacketId)
        L2_LOG_ERROR(
            Log, "Unsupported extended client packet without a bound character: opcode=%d, extended opcode=%d", packetId,
            extendedPacketId
        );
        return true;
    }

    const std::string accountName = Utf16ToUtf8(user->m_sAccountName.data());
    // Log.Add(
    //     LOG_ERROR,
    //     L"DummyUserPacketEx called in UserPacketEx C[%d] EX[%d] "
    //     L"Account[%s:%d] User[%s:%d]",
    //     packetId,
    //     extendedPacketId,
    //     accountName.c_str(),
    //     static_cast<int>(user->GetAccountID()),
    //     user->GetName(),
    //     static_cast<int>(user->GetDBID()))
    // L2_LOG_ERROR(Log,
    //     L"DummyUserPacketEx called in UserPacketEx C[%d] EX[%d] "
    //     L"Account[%s:%d] User[%s:%d]",
    //     packetId,
    //     extendedPacketId,
    //     accountName.c_str(),
    //     static_cast<int>(user->GetAccountID()),
    //     user->GetName(),
    //     static_cast<int>(user->GetDBID()))
    // L2_LOG_ERROR(Log,
    //     "Unsupported extended client packet: opcode=%d, extended opcode=%d, account=%s, account ID=%u, character=%s,
    //     character ID=%u", packetId, extendedPacketId, ::WideToUtf8(accountName).c_str(),
    //     static_cast<unsigned>(user->GetAccountID()), ::WideToUtf8(user->GetName()).c_str(),
    //     static_cast<unsigned>(user->GetDBID()))
    L2_LOG_ERROR(
        Log,
        "Unsupported extended client packet: opcode=%d, extended opcode=%d, account=%s, account ID=%u, character=%s, "
        "character ID=%u",
        packetId, extendedPacketId, accountName.c_str(), static_cast<unsigned>(user->GetAccountID()),
        ::Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID())
    );
    return true;
}

bool AskJoinMPCC(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bf55fc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x367)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x367)
        L2_LOG_ERROR(Log, "Command channel invitation rejected: no character is bound to the client socket.");
        return true;
    }

    CParty* const party = user->GetParty();
    if (party != nullptr && party->IsLootingTypeModifyInProgress()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_MAKE_ONE_TO_ONE_REQUEST_DURING_PARTY_LOOT_CHANGE);
        return false;
    }

    std::array<char16_t, 32> targetName;
    ::Disassemble(packet, "S", 0x32, targetName.data());
    std::array<SharedUtilsWideChar, 28> lowerName;
    std::memset(lowerName.data(), 0, 0x32);
    ::ToLowerCase(reinterpret_cast<const SharedUtilsWideChar*>(targetName.data()), lowerName.data());

    User* target = nullptr;
    {
        const std::u16string hostLowerNameUtf16(reinterpret_cast<const char16_t*>(lowerName.data()));
        CSPointer<User> targetReference = GetUserFromNameMap(g_nWorldId, hostLowerNameUtf16);
        target = targetReference.get();
    }
    user->AskJoinMPCC(target, MCT_NORMAL_SKILL);
    return false;
}

bool AcceptJoinMPCC(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00be05fc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x390)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x390)
        L2_LOG_ERROR(Log, "Command channel invitation response rejected: no character is bound to the client socket.");
        return true;
    }

    int response;
    int createType = 0;
    ::Disassemble(packet, "dd", &response, &createType);
    return user->OnAcceptJoinMPCC(response, createType);
}

bool OustFromMPCC(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bf57ac, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x3ec)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x3ec)
        L2_LOG_ERROR(Log, "Command channel member removal rejected: no character is bound to the client socket.");
        return true;
    }

    std::array<char16_t, 32> targetName;
    ::Disassemble(packet, "S", 0x32, targetName.data());
    std::array<SharedUtilsWideChar, 28> lowerName;
    std::memset(lowerName.data(), 0, 0x32);
    ::ToLowerCase(reinterpret_cast<const SharedUtilsWideChar*>(targetName.data()), lowerName.data());

    const std::u16string hostLowerNameUtf16(reinterpret_cast<const char16_t*>(lowerName.data()));
    CSPointer<User> targetReference = GetUserFromNameMap(g_nWorldId, hostLowerNameUtf16);
    if (targetReference) {
        CParty* const targetParty = targetReference->GetParty();
        if (targetParty != nullptr) {
            User* const targetMaster = targetParty->GetMaster();
            if (targetMaster != nullptr) {
                CMultiPartyCommandChannelManager manager;
                CSPointer<User> targetMasterReference(targetMaster, ObjectRefType::FunctionChain);
                CSPointer<User> channelOwnerReference(user, ObjectRefType::FunctionChain);
                manager.Oust(std::move(channelOwnerReference), std::move(targetMasterReference));
            }
        }
    }
    return false;
}

bool EnchantSkillInfo(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bfc180, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int skillId;
    int skillLevelAndSubLevel;
    ::Disassemble(packet, "dd", &skillId, &skillLevelAndSubLevel);
    const SkillEnchantCommandType command = SECT_NORMAL;
    SkillEnchantOperator* const enchant = Singleton<SkillEnchantFactory>::inst().CreateObject(command);
    if (enchant == nullptr) {
        return true;
    }
    enchant->SendEnchantSkillInfo(user, socket, skillId, skillLevelAndSubLevel, false);
    return false;
}

bool EnchantSkillInfoDetail(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bfc28c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    SkillEnchantCommandType command;
    int skillId;
    int skillLevelAndSubLevel;
    ::Disassemble(packet, "ddd", &command, &skillId, &skillLevelAndSubLevel);
    SkillEnchantOperator* const enchant = Singleton<SkillEnchantFactory>::inst().CreateObject(command);
    if (enchant == nullptr) {
        return true;
    }
    enchant->SendEnchantSkillInfo(user, socket, skillId, skillLevelAndSubLevel, true);
    return false;
}

bool RequestEnchantSkill(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bfc3a8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::CANNOT_ENCHANT_SKILLS_BEFORE_AWAKENED_CLASS_DIVERSIFICATION);
        return false;
    }

    SkillEnchantCommandType command;
    int skillId;
    int skillLevelAndSubLevel;
    ::Disassemble(packet, "ddd", &command, &skillId, &skillLevelAndSubLevel);
    SkillEnchantOperator* const enchant = Singleton<SkillEnchantFactory>::inst().CreateObject(command);
    if (enchant == nullptr) {
        return true;
    }
    enchant->Operate(user, skillId, skillLevelAndSubLevel);
    return false;
}

bool ReplyOustFromPartyRoom(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bed730, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1a7)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1a7)
        L2_LOG_ERROR(Log, "Party room expulsion rejected: no character is bound to the client socket.");
        return true;
    }

    std::uint32_t targetServerId = 0;
    ::Disassemble(packet, "d", &targetServerId);
    CCreature* const target = g_CreatureSmartIdManager.GetObjectW(targetServerId);
    if (target != nullptr && target->IsPC()) {
        CPartyMatchingRequest::inst().OustFromRoom(CSPointer<User>(target->CastUser(), ObjectRefType::FunctionChain));
    }
    return false;
}

bool ReplyOustFromMPCCRoom(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bed8a4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1c3)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1c3)
        L2_LOG_ERROR(Log, "Command channel room expulsion rejected: no character is bound to the client socket.");
        return true;
    }

    std::uint32_t targetServerId = 0;
    ::Disassemble(packet, "d", &targetServerId);
    CCreature* const target = g_CreatureSmartIdManager.GetObjectW(targetServerId);
    if (target != nullptr && target->IsPC()) {
        CPartyMatchingRequest::inst().OustFromMPCCRoom(CSPointer<User>(target->CastUser(), ObjectRefType::FunctionChain));
    }
    return false;
}

bool ReplyWithdrawPartyRoom(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00beda18, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1df)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1df)
        L2_LOG_ERROR(Log, "Party room withdrawal rejected: no character is bound to the client socket.");
        return true;
    }

    std::int32_t reserved = 0;
    ::Disassemble(packet, "d", &reserved);
    CPartyMatchingRequest::inst().WithdrawRoom(CSPointer<User>(user, ObjectRefType::FunctionChain));
    return false;
}

bool ReplyWithdrawMPCCRoom(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bedb4c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1f5)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1f5)
        L2_LOG_ERROR(Log, "Command channel room withdrawal rejected: no character is bound to the client socket.");
        return true;
    }

    std::int32_t reserved = 0;
    ::Disassemble(packet, "d", &reserved);
    CPartyMatchingRequest::inst().WithdrawMPCCRoom(CSPointer<User>(user, ObjectRefType::FunctionChain));
    return false;
}

bool ReplyDismissPartyRoom(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bedc80, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x20b)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x20b)
        L2_LOG_ERROR(Log, "Party room dismissal rejected: no character is bound to the client socket.");
        return true;
    }

    CPartyMatchingRequest::inst().DismissRoom(CSPointer<User>(user, ObjectRefType::FunctionChain));
    return false;
}

bool ReplyDismissMPCCRoom(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bedd90, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x21d)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x21d)
        L2_LOG_ERROR(Log, "Command channel room dismissal rejected: no character is bound to the client socket.");
        return true;
    }

    CPartyMatchingRequest::inst().DismissMPCCRoom(CSPointer<User>(user, ObjectRefType::FunctionChain));
    return false;
}

bool ReplyMPCCPartyMasterList(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bef82c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x126b)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x126b)
        L2_LOG_ERROR(Log, "Command channel party leader list request rejected: no character is bound to the client socket.");
        return true;
    }

    CPartyMatchingRequest::inst().OnListPartyMaster(CSPointer<User>(user, ObjectRefType::FunctionChain));
    return false;
}

bool ReplyHandOverPartyMaster(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bf52f4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x22f)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x22f)
        L2_LOG_ERROR(Log, "Party leadership transfer rejected: no character is bound to the client socket.");
        return true;
    }
    if (!user->IsPartyMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_PARTY_LEADER_CAN_TRANSFER_LEADERSHIP);
        return false;
    }

    std::array<char16_t, 32> targetName;
    ::Disassemble(packet, "S", 0x32, targetName.data());
    std::array<SharedUtilsWideChar, 28> lowerName;
    std::memset(lowerName.data(), 0, 0x32);
    ::ToLowerCase(reinterpret_cast<const SharedUtilsWideChar*>(targetName.data()), lowerName.data());
    const std::u16string hostLowerNameUtf16(reinterpret_cast<const char16_t*>(lowerName.data()));
    CSPointer<User> target = GetUserFromNameMap(user->m_data->m_nWorldId, hostLowerNameUtf16);
    if (!target) {
        socket->SendSystemMessage(SystemMessageId::SELECT_NEW_PARTY_LEADER);
        return false;
    }
    if (user->m_nId == target->m_nId) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_TRANSFER_PARTY_LEADERSHIP_TO_SELF);
        return false;
    }
    if (CReplyChecker::IsUserWaitingReply(user)) {
        socket->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
        return false;
    }

    CParty* const party = user->GetParty();
    if (party != nullptr) {
        if (!party->IsMember(target->m_nId)) {
            socket->SendSystemMessage(SystemMessageId::CAN_ONLY_TRANSFER_LEADERSHIP_TO_PARTY_MEMBER);
        } else if (!party->HandOverMaster(target.get())) {
            socket->SendSystemMessage(SystemMessageId::PARTY_LEADERSHIP_TRANSFER_FAILED);
        }
    }
    return false;
}

bool MPCCShowPartyMembersInfo(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bedea0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::uint32_t targetServerId = 0;
    ::Disassemble(packet, "d", &targetServerId);
    CCreature* const targetCreature = g_CreatureSmartIdManager.GetObjectW(targetServerId);
    User* target = nullptr;
    if (targetCreature != nullptr && targetCreature->IsPC()) {
        target = targetCreature->CastUser();
    }
    if (target == nullptr) {
        return false;
    }

    CMultiPartyCommandChannel* const channel = user->GetMPCC();
    CParty* const targetParty = target->GetParty();
    if (targetParty != nullptr && channel != nullptr) {
        const CSPointer<CMultiPartyCommandChannel> targetChannel =
            CMultiPartyCommandChannelRegistry::FindByMemberParty(targetParty);
        if (targetChannel && channel->m_nId == targetChannel->m_nId) {
            channel->ShowPartyMembersInfo(user, targetParty);
        }
    }
    return false;
}

bool AskJoinPartyRoomPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c04e04, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsBlockAll()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::BLOCK_ALL_ENABLED);
        return false;
    }
    if (!user->m_data->m_bCanJoinParty || !user->IsPartyJoinable()) {
        return false;
    }

    std::array<char16_t, 32> targetName;
    ::Disassemble(packet, "S", 0x32, targetName.data());
    std::array<SharedUtilsWideChar, 28> lowerName;
    std::memset(lowerName.data(), 0, 0x32);
    ::ToLowerCase(reinterpret_cast<const SharedUtilsWideChar*>(targetName.data()), lowerName.data());

    User* target = nullptr;
    {
        const std::u16string hostLowerNameUtf16(reinterpret_cast<const char16_t*>(lowerName.data()));
        CSPointer<User> targetReference = GetUserFromNameMap(g_nWorldId, hostLowerNameUtf16);
        target = targetReference.get();
    }

    const CSPointer<CPartyRoom> room =
        CPartyRoomRegistry::inst().FindByMember(CSPointer<User>(user, ObjectRefType::FunctionChain));
    if (target == nullptr || !room) {
        return false;
    }

    if (!user->CheckCreatePartyMatchLevelLimit() || !g_PartyMatchingWaitingRoom.IsInWaitingRoom(target) ||
        target->m_data->m_nPartyId != 0) {
        const std::u16string messageName = std::u16string(target->GetName());
        user->GetSocket()->Send(
            "chccS", 0x62, SystemMessageId::CHARACTER_CANNOT_BE_INVITED_TO_PARTY_ROOM, 1, 0x0c, messageName.c_str()
        );
        return false;
    }
    if (!room->CheckAvailableMemberLimit()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_INVITE_CHARACTER_PARTY_ROOM_FULL);
        return false;
    }
    if (!room->CanJoin(CSPointer<User>(target, ObjectRefType::FunctionChain))) {
        const std::u16string messageName = std::u16string(target->GetName());
        user->GetSocket()->Send(
            "chccS", 0x62, SystemMessageId::CHARACTER_DOES_NOT_MEET_PARTY_ROOM_SETTINGS, 1, 0x0c, messageName.c_str()
        );
        return false;
    }
    if (target->m_data->m_bGMInvisible) {
        return false;
    }
    if (target->IsOlympiadPlaying()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_INVITE_WHILE_REFUSING_INVITATIONS);
        return false;
    }
    if (target->m_PledgeGameUser.IsPlaying()) {
        const std::u16string messageName = std::u16string(target->GetName());
        user->GetSocket()->Send(
            "chccS", 0x62, SystemMessageId::CHARACTER_CANNOT_BE_INVITED_TO_PARTY_ROOM, 1, 0x0c, messageName.c_str()
        );
        return false;
    }
    if (user->m_nId == target->m_nId) {
        return false;
    }
    if (CReplyChecker::IsUserWaitingReply(user)) {
        socket->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
        return false;
    }
    if (target->IsBlockAll()) {
        user->GetSocket()->SendSystemMessage_Char(SystemMessageId::TARGET_BLOCKS_ALL, target->GetName());
        return false;
    }
    if (target->IsInBlockList(*user)) {
        user->GetSocket()->SendSystemMessage_Char(SystemMessageId::CHARACTER_BLOCKED_YOU, target->GetName());
        return false;
    }

    static_cast<void>(CCursedWeaponMgr::inst());
    if (target->m_nCursedWeaponClassId > 0) {
        return false;
    }
    if (CPVPMatchSystem::inst().IsCrataeCubeUser(*user) || CPVPMatchSystem::inst().IsCrataeCubeUser(*target)) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_FORM_PARTY_IN_THIS_AREA);
        return false;
    }
    if (target->m_data->m_TrainingState == 1) {
        return false;
    }

    if (!CReplyChecker::AttachReplyChecker(10, user, target, 0x3c)) {
        const std::u16string messageName = std::u16string(target->GetName());
        socket->Send("chccS", 0x62, SystemMessageId::INVITED_CHARACTER_BUSY, 1, 0x0c, messageName.c_str());
        return false;
    }

    CUserSocket* const targetSocket = target->GetSocket();
    if (targetSocket != nullptr) {
        std::u16string& requestTitle = room->GetPartyRoomInfo().GetTitle();
        const std::u16string requestUserName = std::u16string(user->GetName());
        targetSocket->Send("chSS", 0xfe, 0x35, requestUserName.c_str(), requestTitle.c_str());

        std::u16string& messageTitle = room->GetPartyRoomInfo().GetTitle();
        const std::u16string messageUserName = std::u16string(user->GetName());
        targetSocket->Send(
            "chccScS", 0x62, SystemMessageId::PARTY_ROOM_INVITATION_RECEIVED, 2, 0x0c, messageUserName.c_str(), 0,
            messageTitle.c_str()
        );
    }
    return false;
}

bool AnswerJoinPartyRoomPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c0589c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const replier = socket->GetUser();
    if (replier == nullptr) {
        return true;
    }
    if (!replier->m_data->m_bCanJoinParty || !replier->IsPartyJoinable()) {
        return false;
    }

    int answer;
    ::Disassemble(packet, "d", &answer);
    User* requester = nullptr;
    {
        CSPointer<User> requesterReference = CReplyChecker::GetRequesterFromReplier(replier);
        requester = requesterReference.get();
    }
    if (requester != nullptr) {
        static_cast<void>(CCursedWeaponMgr::inst());
        if (replier->m_nCursedWeaponClassId > 0) {
            return false;
        }

        if (answer == 0) {
            requester->GetSocket()->SendSystemMessage(SystemMessageId::PARTY_MATCHING_INVITATION_NOT_ACCEPTED);
        } else {
            const CSPointer<CPartyRoom> room =
                CPartyRoomRegistry::inst().FindByMember(CSPointer<User>(requester, ObjectRefType::FunctionChain));
            if (room && room->CanJoin(CSPointer<User>(replier, ObjectRefType::FunctionChain)) &&
                replier->m_data->m_nPartyId == 0 && g_PartyMatchingWaitingRoom.IsInWaitingRoom(replier)) {
                g_PartyMatchingWaitingRoom.LeaveWaitingRoom(replier);
                CPartyRoomListFilter filter(
                    CSPointer<User>(replier, ObjectRefType::FunctionChain), 1, ALL_AREA, ALL_LEVEL, false
                );
                CPartyMatchingRequest::inst().JoinRoom(
                    CSPointer<User>(replier, ObjectRefType::FunctionChain), room->GetPartyRoomInfo().GetId(), filter
                );
            }
        }
    }
    CReplyChecker::FinishWaitingReplyByReplier(replier);
    return false;
}

bool ListPartyMatchingWaitingRoomPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c073f4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t page = 0;
    std::int32_t lowerLevelLimit = 0;
    std::int32_t upperLevelLimit = 0;
    std::int32_t classGroup = 0;
    const std::uint8_t* const cursor = ::Disassemble(packet, "dddd", &page, &lowerLevelLimit, &upperLevelLimit, &classGroup);

    std::vector<int> classList;
    CategoryContent::ClassRole::inst().GatherClassList(classGroup, classList);

    std::array<char16_t, 24> characterName;
    ::Disassemble(cursor, "S", 0x30, characterName.data());
    CPartyMatchingWaitingRoomListFilter filter(
        user, page, lowerLevelLimit, upperLevelLimit, WAITING_ROOM, classList,
        reinterpret_cast<const SharedUtilsWideChar*>(characterName.data())
    );
    g_PartyMatchingWaitingRoom.ListWaitingRoom(filter);
    return false;
}

bool RequsetViewFishingChampionshipRanking(CSocket*, const unsigned char*) {
    // Current L2Server 0x00be10b4 is guard + false.
    L2_GUARD_FUNCTION();
    return false;
}

bool GetBossRecord(CSocket*, const unsigned char*) {
    // Current L2Server 0x00be0a58 is guard + false.
    L2_GUARD_FUNCTION();
    return false;
}

bool RequestAntiFreeServer(CSocket*, const unsigned char*) {
    // Current L2Server 0x00be253c is guard + true.
    L2_GUARD_FUNCTION();
    return true;
}

bool ExitPartyMatchingWaitingRoomPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be0994.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        g_PartyMatchingWaitingRoom.LeaveWaitingRoom(user);
    }
    return user == nullptr;
}

bool RequestLoadUI(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be21f8.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        db.RequestLoadUI(*user);
    }
    return user == nullptr;
}

bool RequestSaveUI(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00be22bc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::uint32_t binarySize = 0;
    std::array<unsigned char, 8000> binary{};
    const unsigned char* const cursor = ::Disassemble(packet, "d", &binarySize);
    if (binarySize <= binary.size()) {
        ::Disassemble(cursor, "b", static_cast<int>(binarySize), binary.data());
        db.RequestSaveUI(*user, static_cast<int>(binarySize), binary.data());
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[RequestSaveUI] nBinarySize:%d, MAX_UI_SETTING_LENGTH:%d",
        //     runtime::Int32FromBits(binarySize),
        //     8000)
        // L2_LOG_ERROR(Log,
        //     L"[RequestSaveUI] nBinarySize:%d, MAX_UI_SETTING_LENGTH:%d",
        //     runtime::Int32FromBits(binarySize),
        //     8000)
        L2_LOG_ERROR(
            Log, "UI settings save rejected: payload size=%u exceeds limit=%zu, character ID=%u",
            static_cast<unsigned>(binarySize), binary.size(), static_cast<unsigned>(user->GetDBID())
        );
    }
    return false;
}

bool RequestBrMinigameLoadScores(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be3f90.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        db.RequestLoadMinigameScores(user);
    }
    return user == nullptr;
}

bool RequestBrLoadEventTopRankers(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c08ab8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int eventId = 0;
    int requestedRound = 0;
    int unused = 0;
    ::Disassemble(packet, "ddd", &eventId, &requestedRound, &unused);
    static_cast<void>(unused);
    auto* const event = static_cast<CEvent_World*>(Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_WORLD));
    if (event != nullptr) {
        const int currentRound = event->GetRoundNo(runtime::crt::Time32Now());
        const int relativeRound = runtime::SubtractInt32Wrapping(currentRound, requestedRound);
        if (relativeRound < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"RequestBrLoadEventTopRankers, round no = %d",
            //     relativeRound)
            // L2_LOG_ERROR(Log,
            //     L"RequestBrLoadEventTopRankers, round no = %d",
            //     relativeRound)
            // Original: L2_LOG_ERROR(Log,
            //     "Event ranking request rejected: computed round=%d is negative, current round=%d, requested round=%d,
            //     event=%d", relativeRound, currentRound, requestedRound, eventId)
            L2_LOG_ERROR(
                Log,
                "Ranking request for event %d rejected: computed round %d is negative (current round %d, requested round "
                "%d).",
                eventId, relativeRound, currentRound, requestedRound
            );
        } else {
            db.RequestLoadEventTopRankers(static_cast<int>(user->GetDBID()), eventId, relativeRound);
        }
    }
    return false;
}

bool RequestPrivateStoreWholeSetMsg(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be2780, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const int storeType = user->GetPrivateStoreType();
    if ((storeType == PST_WHOLESALE_OPEN || storeType == PST_WHOLESALE_MANAGE) && !user->IsRiding() && !user->IsNowTrade()) {
        if (user->GetTrade() != nullptr) {
            user->TradeCancel(false);
        }
        if (user->IsItemUsable(0)) {
            user->SetPrivateStoreWholeMsg(packet);
        }
    }
    return false;
}

bool RequestCharacterBackToLobby(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be1b08 always returns false.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    const std::uint32_t accountId = socket->GetAccountID();
    if (accountId != 0) {
        db.RequestLoadChar(accountId, socket);
    }
    return false;
}

bool RequestSetTutorial(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be1e80.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::uint32_t tutorialState = 0;
        ::Disassemble(packet, "d", &tutorialState);
        db.RequestSetTutorial(*user, tutorialState);
    }
    return user == nullptr;
}

bool RequestBrMinigameInsertScore(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be4054.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::int32_t score = 0;
        ::Disassemble(packet, "d", &score);
        db.RequestInsertMinigameScores(user, score);
    }
    return user == nullptr;
}

bool ReplySeedPhase(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be25bc.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        g_FieldCycleSystem.SendMapInfoTo(user);
    }
    return user == nullptr;
}

bool RequestPCCafeCouponUse(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be122c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::array<char16_t, 21> coupon{};
        ::Disassemble(packet, "S", 0x2a, coupon.data());
        user->RequestPCCafeCouponUse(coupon.data());
    }
    return user == nullptr;
}

bool ResponseNewInspectorPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00bf8eac, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const unsigned char* cursor = packet;
    Agent* const agent = user->m_AgentMap[NEW_INSPECTOR];
    if (agent != nullptr) {
        static_cast<void>(agent->ResponseSecond(&cursor));
    }
    return false;
}

bool RequestBrSetLectureMark(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be4134.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    char lectureMark = 0;
    ::Disassemble(packet, "c", &lectureMark);
    if (!g_bUseLecture) {
        user->SendSystemMessage(u"SYS", u"Lecture Not Support, Check Server");
    } else {
        db.RequestSetLectureMark(user, lectureMark);
    }
    return false;
}

bool RequestCursedWeaponList(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00bfd0d4.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_nWorldId == g_nWorldId) {
        CCursedWeaponMgr::inst().SendCursedWeaponList(socket);
    }
    return false;
}

bool RequestExistingCursedWeaponLocation(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00bfd1b0.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_nWorldId == g_nWorldId) {
        CCursedWeaponMgr::inst().SendExistingCursedWeapon(socket);
    }
    return false;
}

bool User::OnAcceptJoinMPCC(const int response, const int createType) {
    // Current L2Server 0x00bfcbd4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    CSPointer<User> requester = CReplyChecker::GetRequesterFromReplier(this);
    if (!requester) {
        SendSystemMessage(SystemMessageId::COMMAND_CHANNEL_INVITATION_USER_NOT_FOUND);
        CReplyChecker::FinishWaitingReplyByReplier(this);
        return false;
    }

    static_cast<void>(CCursedWeaponMgr::inst());
    if (m_nCursedWeaponClassId > 0) {
        if (createType == MCT_ITEM) {
            requester->SetPendingUseETCItem(nullptr, PUIT_MPCC_CREATE);
        }
        return false;
    }
    static_cast<void>(CCursedWeaponMgr::inst());
    if (requester->m_nCursedWeaponClassId > 0) {
        if (createType == MCT_ITEM) {
            requester->SetPendingUseETCItem(nullptr, PUIT_MPCC_CREATE);
        }
        return false;
    }

    if (response == 1) {
        CMultiPartyCommandChannelManager manager;
        if (CMultiPartyCommandChannelRegistry::IsChannelOwner(requester->GetParty())) {
            manager.Join(requester, CSPointer<User>(this, ObjectRefType::FunctionChain));
            CPartyMatchingRequest::inst().OnJoinMPCC(CSPointer<User>(this, ObjectRefType::FunctionChain));
        } else if (createType != MCT_ITEM) {
            manager.Open(requester, MCT_NORMAL_SKILL);
            manager.Join(requester, CSPointer<User>(this, ObjectRefType::FunctionChain));
            CPartyMatchingRequest::inst().OnJoinMPCC(CSPointer<User>(this, ObjectRefType::FunctionChain));
        } else {
            requester->CreateMPCCWithItem(this);
        }
    } else {
        if (createType == MCT_ITEM) {
            requester->SetPendingUseETCItem(nullptr, PUIT_MPCC_CREATE);
        }
        requester->GetSocket()->SendSystemMessage_S(SystemMessageId::COMMAND_CHANNEL_INVITATION_DECLINED, GetName());
    }

    CReplyChecker::FinishWaitingReplyByReplier(this);
    return false;
}

bool RequestDuelAskStart(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be15e4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!user->IsPC()) {
        return false;
    }

    std::array<char16_t, 25> targetName;
    int duelType = 0;
    ::Disassemble(packet, "Sd", 0x32, targetName.data(), &duelType);
    const std::wstring hostTargetName = Utf16ToWide(targetName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    return user->OnDuelAskStart(hostTargetName.c_str(), static_cast<DuelType>(duelType));
}

bool RequestDuelAcceptStart(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be16fc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!user->IsPC()) {
        return false;
    }

    int duelType;
    int reply;
    int result;
    ::Disassemble(packet, "ddd", &duelType, &reply, &result);
    return user->OnDuelAcceptStart(static_cast<DuelType>(duelType), reply, result);
}

bool RequestMagicSkillUseGround(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be1804, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int x;
    int y;
    int z;
    int skillId;
    int forceAttack;
    char stand;
    ::Disassemble(packet, "dddddc", &x, &y, &z, &skillId, &forceAttack, &stand);

    user->m_NonTargetSkillPos = FVector(x, y, z);
    static const FVector nullPos;
    if (!nullPos.IsEqual(user->m_NonTargetSkillPos)) {
        if (user->m_SkillDurationMod.m_nFearCount < 1) {
            static_cast<void>(user->OnMagicSkillUsePacket(skillId, forceAttack == 1, stand == 1));
        } else {
            socket->Send("cd", 0x1f, 2);
        }
    }
    return false;
}

bool RequestDuelSurrender(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00be9ac0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsInDuel() && user->m_DuelUserInfo.IsInIndividualDuel() && user->m_DuelUserInfo.IsInFightStatus()) {
        CDuelManager::inst().CancelDuel(user, 7);
        User* const opponent = user->m_DuelUserInfo.GetIndividualDuelEnemy();
        if (opponent != nullptr) {
            const int userZ = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
            const int userY = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
            const int userX = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
            const char16_t* const opponentName = opponent->GetName();
            const char16_t* const userName = user->GetName();
            const std::uint32_t opponentAccountId = opponent->GetAccountID();
            const std::uint32_t opponentDbId = opponent->GetDBID();
            const std::uint32_t userAccountId = user->GetAccountID();
            const std::uint32_t userDbId = user->GetDBID();
            const std::wstring userAccountName =
                Utf16ToWide(user->m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
            const std::wstring opponentAccountName =
                Utf16ToWide(opponent->m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
#if defined(_WIN32)
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
                0x377, userDbId, userAccountId, opponentDbId, opponentAccountId, userX, userY, userZ, DT_INDIVIDUAL, 3,
                userName, userAccountName.c_str(), opponentName, opponentAccountName.c_str()
            );
#else
            // Native variadic formatting requires 64-bit operands for every %I64d.
            // Original Windows call is kept above. CSV fields and event code are unchanged.
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
            //     static_cast<long long>(0x377),
            //     static_cast<long long>(userDbId),
            //     static_cast<long long>(userAccountId),
            //     static_cast<long long>(opponentDbId),
            //     static_cast<long long>(opponentAccountId),
            //     static_cast<long long>(userX),
            //     static_cast<long long>(userY),
            //     static_cast<long long>(userZ),
            //     static_cast<long long>(DT_INDIVIDUAL),
            //     static_cast<long long>(3),
            //     userName,
            //     userAccountName.c_str(),
            //     opponentName,
            //     opponentAccountName.c_str())
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
                static_cast<long long>(0x377), static_cast<long long>(userDbId), static_cast<long long>(userAccountId),
                static_cast<long long>(opponentDbId), static_cast<long long>(opponentAccountId),
                static_cast<long long>(userX), static_cast<long long>(userY), static_cast<long long>(userZ),
                static_cast<long long>(DT_INDIVIDUAL), static_cast<long long>(3), userName, userAccountName.c_str(),
                opponentName, opponentAccountName.c_str()
            );
#endif

            const int opponentZ = runtime::Win64CvttDoubleToInt32(opponent->GetPos().z);
            const int opponentY = runtime::Win64CvttDoubleToInt32(opponent->GetPos().y);
            const int opponentX = runtime::Win64CvttDoubleToInt32(opponent->GetPos().x);
            const char16_t* const userTargetName = user->GetName();
            const char16_t* const opponentActorName = opponent->GetName();
            const std::uint32_t userTargetAccountId = user->GetAccountID();
            const std::uint32_t userTargetDbId = user->GetDBID();
            const std::uint32_t opponentActorAccountId = opponent->GetAccountID();
            const std::uint32_t opponentActorDbId = opponent->GetDBID();
#if defined(_WIN32)
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
                0x377, opponentActorDbId, opponentActorAccountId, userTargetDbId, userTargetAccountId, opponentX, opponentY,
                opponentZ, DT_INDIVIDUAL, 0, opponentActorName, opponentAccountName.c_str(), userTargetName,
                userAccountName.c_str()
            );
#else
            // Native variadic formatting requires 64-bit operands for every %I64d.
            // Original Windows call is kept above. CSV fields and event code are unchanged.
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
            //     static_cast<long long>(0x377),
            //     static_cast<long long>(opponentActorDbId),
            //     static_cast<long long>(opponentActorAccountId),
            //     static_cast<long long>(userTargetDbId),
            //     static_cast<long long>(userTargetAccountId),
            //     static_cast<long long>(opponentX),
            //     static_cast<long long>(opponentY),
            //     static_cast<long long>(opponentZ),
            //     static_cast<long long>(DT_INDIVIDUAL),
            //     static_cast<long long>(0),
            //     opponentActorName,
            //     opponentAccountName.c_str(),
            //     userTargetName,
            //     userAccountName.c_str())
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
                static_cast<long long>(0x377), static_cast<long long>(opponentActorDbId),
                static_cast<long long>(opponentActorAccountId), static_cast<long long>(userTargetDbId),
                static_cast<long long>(userTargetAccountId), static_cast<long long>(opponentX),
                static_cast<long long>(opponentY), static_cast<long long>(opponentZ), static_cast<long long>(DT_INDIVIDUAL),
                static_cast<long long>(0), opponentActorName, opponentAccountName.c_str(), userTargetName,
                userAccountName.c_str()
            );
#endif
        }
    }
    return false;
}

bool RequestChangeCharName(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c000d4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    int type = 0;
    std::array<char16_t, 24> targetName{};
    const std::uint8_t* cursor = ::Disassemble(packet, "dS", &type, 0x30, targetName.data());

    if (!g_bMergedServer && !g_bChangeWrongCharName) {
        return true;
    }

    const int previousPacket = socket->GetLastUsePacket();
    if (type == 0 && previousPacket != 0x13 && previousPacket != 0x0c && previousPacket != 0x2b) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Prev Packet ID: [%d]",
        //     L"UserPacketEx.cpp",
        //     0xbac,
        //     previousPacket)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Prev Packet ID: [%d]",
        //     L"UserPacketEx.cpp",
        //     0xbac,
        //     previousPacket)
        L2_LOG_ERROR(
            Log, "Name change request rejected: previous packet=%d is not allowed for character rename", previousPacket
        );
        return true;
    }

    if (!User::CheckCharacterName(targetName.data(), g_nCountryCode, false) ||
        (g_nCountryCode == 8 && !User::CheckCharacterNameDual(targetName.data(), 8, false))) {
        socket->Send("chdd", 0xfe, 0x6a, type, 1);
        return false;
    }

    int characterDatabaseId = 0;
    if (type == 0) {
        std::uint32_t characterIndex = 0;
        ::Disassemble(cursor, "d", &characterIndex);
        if (characterIndex >= 7U) {
            return true;
        }
        characterDatabaseId = socket->m_nCharacterDBIDs[characterIndex];
    } else {
        if (type != 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid type[%d]",
            //     L"UserPacketEx.cpp",
            //     0xbe5,
            //     type)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid type[%d]",
            //     L"UserPacketEx.cpp",
            //     0xbe5,
            //     type)
            L2_LOG_ERROR(Log, "Name change request rejected: unsupported type=%d (expected 0 or 1)", type);
            return false;
        }

        User* const user = socket->GetUser();
        if (user == nullptr) {
            return true;
        }
        if (!TalkChannelCache::inst().mChannels[10].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] user[%s] request variation with invalid talk channel.",
            //     L"UserPacketEx.cpp",
            //     0xbd7,
            //     user->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] user[%s] request variation with invalid talk channel.",
            //     L"UserPacketEx.cpp",
            //     0xbd7,
            //     user->GetName())
            // L2_LOG_ERROR(Log,
            //     "Clan name change request rejected: talk channel validation failed, character=%s, character ID=%u",
            //     ::WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID()))
            L2_LOG_ERROR(
                Log, "Clan name change request rejected: talk channel validation failed, character=%s, character ID=%u",
                ::Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID())
            );
            return false;
        }

        CPledge* const pledge = user->GetPledge();
        CSharedPledgeData* const pledgeData = pledge != nullptr ? pledge->SharedData() : nullptr;
        if (pledgeData == nullptr || pledge->m_bMergedProcessing || pledgeData->m_nPrevServer == 0 ||
            user->GetDBID() != pledge->GetMasterDbId()) {
            return false;
        }

        pledge->m_bMergedProcessing = true;
        characterDatabaseId = static_cast<int>(pledge->GetDbId());
    }

    if (!g_bMergedServer && g_bChangeWrongCharName && type == 0) {
        type = 2;
    }
    db.RequestChangeMergedName(type, characterDatabaseId, targetName.data(), socket);
    return false;
}

namespace UserPacketEx {

bool RequestRegisterDominion(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be9e90, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int dominionId = 0;
    int pledgeRegistration = 0;
    int registerEntry = 0;
    ::Disassemble(packet, "ddd", &dominionId, &pledgeRegistration, &registerEntry);

    CDominion* const dominion = g_ResidenceDB.GetDominion(dominionId);
    if (dominion == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"cannot find dominion - %d : NpcRegisterDominionSiege",
        //     dominionId)
        // L2_LOG_ERROR(Log,
        //     L"cannot find dominion - %d : NpcRegisterDominionSiege",
        //     dominionId)
        L2_LOG_ERROR(
            Log, "Territory war registration request rejected: dominion=%d is unavailable, character ID=%u", dominionId,
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    if (CDominionManager::Inst()->GetState() != DSST_REGISTRATION) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_OUTSIDE_TERRITORY_WAR_REGISTRATION_PERIOD);
        return false;
    }

    if (pledgeRegistration != 0 &&
        (user->GetPledge() == nullptr ||
            (!user->m_data->m_bPledgeMaster && !user->HavePledgePower(PP_REGISTER_CASTLE_WAR)))) {
        socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        return false;
    }

    bool success = false;
    if (registerEntry == 0) {
        if (pledgeRegistration == 0) {
            success = dominion->UnregisterUser(static_cast<int>(user->GetDBID()), true);
            if (success) {
                const int residenceId = dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0;
                CLogFormatter formatter;
                formatter.WriteLog2(0x7e8, user, nullptr, residenceId, 0, 0, 0, 0, 0, 0);
            }
        } else {
            if (user->GetPledge() == nullptr) {
                return false;
            }
            success = dominion->UnregisterPledge(static_cast<int>(user->m_data->m_nPledgeID), true);
            if (success) {
                const int residenceId = dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0;
                CLogFormatter formatter;
                formatter.WriteLog1(
                    0x7e7, user, nullptr, user->GetPledge()->GetPledgeName(), nullptr, nullptr, residenceId,
                    user->m_data->m_nPledgeID, 0, 0, 0, 0, 0
                );
            }
        }
    } else {
        if (user->GetPledge() != nullptr) {
            if (user->GetPledge()->GetCastle() != nullptr) {
                socket->SendSystemMessage(SystemMessageId::TERRITORY_OWNING_CLAN_CANNOT_JOIN_AS_MERCENARY);
                return false;
            }
            if (CDominionManager::Inst()->GetPledgeDominionSiegeID(static_cast<int>(user->m_data->m_nPledgeID)) != 0) {
                socket->SendSystemMessage(SystemMessageId::ALREADY_REGISTERED_FOR_ANOTHER_TERRITORY_WAR);
                return false;
            }
        }

        if (pledgeRegistration == 0) {
            if (CDominionManager::Inst()->GetUserDominionSiegeID(static_cast<int>(user->GetDBID())) != 0) {
                socket->SendSystemMessage(SystemMessageId::ALREADY_REGISTERED_FOR_ANOTHER_TERRITORY_WAR);
                return false;
            }
            success = dominion->RegisterUser(static_cast<int>(user->GetDBID()), true);
            if (success) {
                const int residenceId = dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0;
                CLogFormatter formatter;
                formatter.WriteLog2(0x7e8, user, nullptr, residenceId, 1, 0, 0, 0, 0, 0);
                user->SetDominionRenamedID(residenceId);
            }
        } else {
            if (user->GetPledge() == nullptr) {
                return false;
            }
            success = dominion->RegisterPledge(static_cast<int>(user->m_data->m_nPledgeID), true);
            if (success) {
                const int residenceId = dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0;
                CLogFormatter formatter;
                formatter.WriteLog1(
                    0x7e7, user, nullptr, user->GetPledge()->GetPledgeName(), nullptr, nullptr, residenceId,
                    user->m_data->m_nPledgeID, 1, 0, 0, 0, 0
                );
                user->GetPledge()->SetAllOnlineMemberRenamed(residenceId);
            }
        }
    }

    int pledgeCount = 0;
    int userCount = 0;
    CDominionSiegeEntry* const siegeEntry = dominion->GetSiegeEntry();
    if (siegeEntry != nullptr) {
        pledgeCount = siegeEntry->GetPledgeListSize();
        userCount = siegeEntry->GetUserListSize();
    }
    socket->Send(
        "chdddddd", 0xfe, 0x92, dominionId, pledgeRegistration, registerEntry, success == true, pledgeCount, userCount
    );
    return false;
}

bool RequestDominionInfo(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00bfb39c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }

    std::array<char, 0x5000> dominionPayload{};
    int dominionPayloadSize = 0;
    int dominionCount = 0;
    const CDominionManager::DominionList dominions = CDominionManager::Inst()->GetDominionList();
    for (CDominion* const dominion : dominions) {
        if (dominion == nullptr) {
            // Log.Add(LOG_ERROR, L"Invalid dominion!")
            // L2_LOG_ERROR(Log, L"Invalid dominion!")
            L2_LOG_ERROR(Log, "Territory information entry skipped: dominion list contains a null object.");
            continue;
        }

        CPledge* const owner = dominion->GetResidenceOwnerPledge();
        if (owner == nullptr) {
            continue;
        }

        const CDominion::OwnThingList ownThings = dominion->GetOwnThingList();
        const int residenceId = dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0;
        const char16_t* const residenceName = dominion->m_data != nullptr ? dominion->m_data->m_sResidenceName.data() : u"";
        const std::u16string pledgeName = std::u16string(owner->GetPledgeName());
        int written = ::Assemble(
            dominionPayload.data() + dominionPayloadSize, 0x5000 - dominionPayloadSize, "dSS", residenceId, residenceName,
            pledgeName.c_str()
        );
        if (written > 0) {
            dominionPayloadSize += written;
        }

        std::array<char, 0x5000> ownThingPayload{};
        int ownThingPayloadSize = 0;
        int ownThingCount = 0;
        CYieldLock& ownThingLock = dominion->GetOwnThingLock();
        {
            L2_GUARD(ownThingLock);
            for (const auto& entry : ownThings) {
                if (entry.first == nullptr) {
                    continue;
                }
                written = ::Assemble(
                    ownThingPayload.data() + ownThingPayloadSize, 0x5000 - ownThingPayloadSize, "d",
                    entry.first->GetOriginalResidenceID()
                );
                if (written > 0) {
                    ownThingPayloadSize += written;
                }
                ++ownThingCount;
            }
        }

        if (ownThingCount < 1) {
            written = ::Assemble(dominionPayload.data() + dominionPayloadSize, 0x5000 - dominionPayloadSize, "d", 0);
        } else {
            written = ::Assemble(
                dominionPayload.data() + dominionPayloadSize, 0x5000 - dominionPayloadSize, "db", ownThingCount,
                ownThingPayloadSize, ownThingPayload.data()
            );
        }
        if (written > 0) {
            dominionPayloadSize += written;
        }

        written = ::Assemble(
            dominionPayload.data() + dominionPayloadSize, 0x5000 - dominionPayloadSize, "d",
            CDominionManager::Inst()->GetNextSiegeTime()
        );
        if (written > 0) {
            dominionPayloadSize += written;
        }
        ++dominionCount;
    }

    socket->Send("chdb", 0xfe, 0x93, dominionCount, dominionPayloadSize, dominionPayload.data());

    if (CDominionManager::Inst()->GetState() == DSST_DOMINION_SIEGE) {
        std::array<char, 0x5000> ownThingPayload{};
        int ownThingPayloadSize = 0;
        int ownThingCount = 0;
        for (CDominion* const dominion : dominions) {
            if (dominion == nullptr) {
                // Log.Add(LOG_ERROR, L"Invalid dominion!")
                // Noise: the first traversal of this same dominion list already reports the null entry.
                // L2_LOG_ERROR(Log, L"Invalid dominion!")
                continue;
            }
            COwnThing* const ownThing = dominion->GetOwnThing();
            if (ownThing == nullptr) {
                // Log.Add(LOG_ERROR, L"Invalid ownthing!")
                // L2_LOG_ERROR(Log, L"Invalid ownthing!")
                L2_LOG_ERROR(
                    Log, "Territory ward position entry skipped: dominion=%d has no ward object.",
                    dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0
                );
                continue;
            }

            CYieldLock& ownThingLock = dominion->GetOwnThingLock();
            {
                L2_GUARD(ownThingLock);
                int x = runtime::Win64CvttDoubleToInt32(ownThing->GetPos().x);
                int y = runtime::Win64CvttDoubleToInt32(ownThing->GetPos().y);
                int z = runtime::Win64CvttDoubleToInt32(ownThing->GetPos().z);
                if (ownThing->GetUser() != nullptr) {
                    x = runtime::Win64CvttDoubleToInt32(ownThing->GetUser()->GetPos().x);
                    y = runtime::Win64CvttDoubleToInt32(ownThing->GetUser()->GetPos().y);
                    z = runtime::Win64CvttDoubleToInt32(ownThing->GetUser()->GetPos().z);
                }
                const int residenceId = dominion->m_data != nullptr ? dominion->m_data->m_nResidenceID : 0;
                const int written = ::Assemble(
                    ownThingPayload.data() + ownThingPayloadSize, 0x5000 - ownThingPayloadSize, "dddd", residenceId, x, y, z
                );
                if (written > 0) {
                    ownThingPayloadSize += written;
                }
            }
            ++ownThingCount;
        }
        socket->Send("chdb", 0xfe, 0x94, ownThingCount, ownThingPayloadSize, ownThingPayload.data());
    }
    return false;
}

bool RequestShowPetition(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00be3050, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        petitionCli.ReqShowPetition(user);
    }
    return user == nullptr;
}

bool RequestShowStepTwo(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be3114, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::uint8_t categoryId = 0;
        ::Disassemble(packet, "c", &categoryId);
        petitionCli.ReqShowStepTwo(user, categoryId);
    }
    return user == nullptr;
}

bool RequestShowStepThree(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be31f8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::uint8_t categoryId = 0;
        ::Disassemble(packet, "c", &categoryId);
        petitionCli.ReqShowStepThree(user, categoryId);
    }
    return user == nullptr;
}

} // namespace UserPacketEx

bool RequestShowAgitInfo(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00bf4080, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    if (socket->GetUser() == nullptr) {
        return true;
    }

    std::array<char, 0x5000> buffer;
    int dataSize = 0;
    int agitCount = 0;
    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (!residence->IsWorldAgit()) {
            continue;
        }
        CWorldAgit* const agit = residence->CastWorldAgit();
        if (agit == nullptr) {
            continue;
        }

        int agitStatus = 0;
        if (!agit->IsAuctionAgit()) {
            agitStatus = agit->IsCompetitionAgit() ? 2 : 1;
        }
        CPledge* const owner = agit->GetResidenceOwnerPledge();
        const char16_t* const pledgeName = owner != nullptr ? owner->GetPledgeName() : u"";
        const char16_t* const masterName = owner != nullptr ? owner->GetMasterName() : u"";
        const int written = ::Assemble(
            buffer.data() + dataSize, static_cast<int>(buffer.size()) - dataSize, "dSSd", agit->GetResidenceID(), pledgeName,
            masterName, agitStatus
        );
        ++agitCount;
        if (written > 0) {
            dataSize += written;
        }
    }

    socket->Send("chdb", 0xfe, 0x16, agitCount, dataSize, buffer.data());
    return false;
}

bool RequestShowCastleInfo(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00bf3bb4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    if (socket->GetUser() == nullptr) {
        return true;
    }

    std::array<char, 0x5000> buffer;
    int dataSize = 0;
    int castleCount = 0;
    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (!residence->IsCastle()) {
            continue;
        }
        CCastle* const castle = residence->CastCastle();
        if (castle == nullptr) {
            continue;
        }

        CPledge* const owner = castle->GetResidenceOwnerPledge();
        const char16_t* const pledgeName = owner != nullptr ? owner->GetPledgeName() : u"";
        CResidenceData* const data = castle->m_data;
        const int residenceType = data->m_ResidenceType;
        const int written = ::Assemble(
            buffer.data() + dataSize, static_cast<int>(buffer.size()) - dataSize, "dSddcc", data->m_nResidenceID, pledgeName,
            data->m_TaxInfoData[residenceType].m_nTaxRate, data->m_NextSiege, static_cast<int>(castle->IsUnderSiege()),
            static_cast<int>(static_cast<char>(residenceType))
        );
        ++castleCount;
        if (written > 0) {
            dataSize += written;
        }
    }

    socket->Send("chdb", 0xfe, 0x14, castleCount, dataSize, buffer.data());
    return false;
}

bool RequestShowFortressInfo(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00bf3e2c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    if (socket->GetUser() == nullptr) {
        return true;
    }

    std::array<char, 0x5000> buffer;
    int dataSize = 0;
    int fortressCount = 0;
    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (!residence->IsFortress()) {
            continue;
        }
        CFortress* const fortress = residence->CastFortress();
        if (fortress == nullptr) {
            continue;
        }

        CResidenceData* const data = fortress->m_data;
        const ResidenceState state = data != nullptr ? data->m_ResidenceState : RS_MAX;
        CPledge* const owner = fortress->GetResidenceOwnerPledge();
        const char16_t* const pledgeName = owner != nullptr ? owner->GetPledgeName() : u"";
        int elapsedOwnerSeconds = 0;
        if (owner != nullptr) {
            elapsedOwnerSeconds =
                runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), data->m_tLastOwnerChangedTime);
        }
        const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
        const int written = ::Assemble(
            buffer.data() + dataSize, static_cast<int>(buffer.size()) - dataSize, "dSdd", residenceId, pledgeName,
            static_cast<int>(state == RS_SIEGE), elapsedOwnerSeconds
        );
        ++fortressCount;
        if (written > 0) {
            dataSize += written;
        }
    }

    socket->Send("chdb", 0xfe, 0x15, fortressCount, dataSize, buffer.data());
    return false;
}

bool RequestShowFortressSiegeInfo(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00be1c84, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return false;
    }
    CSharedPledgeData* const pledgeData = pledge->SharedData();
    const int defenceFortressId = static_cast<int>(pledgeData->m_nFortressUnderDefenceSiegeId);
    const int offenceFortressId = static_cast<int>(pledgeData->m_nFortressUnderOffenceSiegeId);
    if (defenceFortressId == 0 && offenceFortressId == 0) {
        return false;
    }
    const int fortressId = offenceFortressId != 0 ? offenceFortressId : defenceFortressId;
    CResidence* const residence = g_ResidenceDB.GetResidence(fortressId);
    if (residence == nullptr || !residence->IsFortress()) {
        return false;
    }
    CFortress* const fortress = residence->CastFortress();
    if (fortress == nullptr) {
        return false;
    }

    CResidenceData* const data = fortress->m_data;
    const ResidenceState state = data != nullptr ? data->m_ResidenceState : RS_MAX;
    if (state != RS_SIEGE) {
        return false;
    }
    const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
    if (static_cast<int>(pledgeData->m_nFortressDbId) != residenceId && fortressId != residenceId) {
        return false;
    }

    const int numBarracks = fortress->GetNumBarracks();
    const int numCaptured = fortress->GetNumBarracksCapturedInSiege();
    const int publishedResidenceId = fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0;
    socket->Send("chddd", 0xfe, 0x17, publishedResidenceId, numBarracks, numCaptured);
    return false;
}

bool RequestFortressMapInfo(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be2680, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int fortressId;
    ::Disassemble(packet, "d", &fortressId);
    CResidence* const residence = g_ResidenceDB.GetResidence(fortressId);
    if (residence != nullptr) {
        CFortress* const fortress = residence->CastFortress();
        if (fortress != nullptr) {
            fortress->SendMapInfo(user);
        }
    }
    return false;
}

bool WriteHeroWords(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be9938, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x347)
        // L2_LOG_ERROR(Log,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x347)
        L2_LOG_ERROR(Log, "Hero message update rejected: no character is bound to the client socket.");
        return true;
    }
    if (!user->IsHero()) {
        return false;
    }

    std::array<char16_t, 65> heroWords{};
    ::Disassemble(packet, "S", 0x40, heroWords.data());
    DxWriteHeroWords request(static_cast<int>(user->GetDBID()), heroWords.data());
    db.RequestTPacketProcess(user->m_data->m_nWorldId, 0x24, sizeof(request), request);
    return false;
}

bool Handle_C_EX_OLYMPIAD_OBSERVER_END(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00bf259c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CSPointer<User> retained(user, ObjectRefType::FunctionChain);
    if (!retained->m_data->m_bGMInvisible) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]too fast observer end, ignore",
        //     L"UserPacketEx.cpp",
        //     0x300c)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]too fast observer end, ignore",
        //     L"UserPacketEx.cpp",
        //     0x300c)
        L2_LOG_WARN(
            Log, "World Olympiad observer exit ignored: required invisibility flag is not set, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
    } else {
        WOlympiadService::inst().StopObserving(retained);
    }
    return false;
}

bool Handle_C_EX_GFX_OLYMPIAD(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00bf2720, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const bool isHost = WOlympiadServiceConfig::inst().IsHost();
    if (isHost) {
        WOlympiadService::inst().SendGameList(CSPointer<User>(user, ObjectRefType::FunctionChain));
    } else {
        WOlympiadServiceProxy::inst().SendMatchListToUser(CSPointer<User>(user, ObjectRefType::FunctionChain));
    }
    return false;
}

int GetLootingTypeStrNum(const ItemRoutingType routingType) {
    // Current L2Server 0x00be3604, original owner UserPacketEx.cpp.
    if (routingType == IRT_PICKER_FIRST) {
        return 0x1e7;
    }
    if (routingType == IRT_RANDOMLY) {
        return 0x1e8;
    }
    if (routingType == IRT_RANDOMLY_INCLUDING_SPOIL) {
        return 0x31e;
    }
    if (routingType != IRT_ORDERLY) {
        if (routingType != IRT_ORDERLY_INCLUDING_SPOIL) {
            return 0;
        }
        return 800;
    }
    return 799;
}

bool AutoSoulshotEXPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be0450, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int itemClassId = 0;
    int activeState = 0;
    int uiSlot = 4;
    ::Disassemble(packet, "ddd", &itemClassId, &activeState, &uiSlot);
    const bool activate = activeState == 1 || activeState == 3;
    if (!g_bUseAutoSoulShotUI) {
        return user->OnExAutoSoulShotPacket(itemClassId, activate);
    }

    // Portable: the LU4 client sends only the item and on/off; the slot follows
    // from the shot kind, as on the official LU4 server.
    if (runtime::client_protocol::UsesLu4Protocol(socket) && static_cast<unsigned int>(uiSlot) >= 4U) {
        CItem* const item = user->m_inv.GetFirstItemByClassID(itemClassId, false, false);
        if (item == nullptr || !item->IsItem()) {
            return false;
        }
        switch (item->m_const->m_enumDefaultAction) {
        case ITEM_ACTION_SOULSHOT:
            uiSlot = E_AUTOSOULSHOT_TYPE_SOULSHOT;
            break;
        case ITEM_ACTION_SPIRITSHOT:
        case ITEM_ACTION_BLESS_SPIRITSHOT:
            uiSlot = E_AUTOSOULSHOT_TYPE_SPIRITSHOT;
            break;
        case ITEM_ACTION_SUMMON_SOULSHOT:
            uiSlot = E_AUTOSOULSHOT_TYPE_SUMMON_SOULSHOT;
            break;
        case ITEM_ACTION_SUMMON_SPIRITSHOT:
            uiSlot = E_AUTOSOULSHOT_TYPE_SUMMON_SPIRITSHOT;
            break;
        default:
            return false;
        }
    }

    if (itemClassId == 0 && static_cast<unsigned int>(uiSlot) < 4U) {
        AutoSoulShotUi& ui = user->m_AutoSoulShotUi[uiSlot];
        itemClassId = ui.m_ItemClassID;
        const int storedActiveState = ui.m_IsActiveState;
        ui.m_ItemClassID = 0;
        // Current 0x00be056d writes the saved value back to the same lane.
        ui.m_IsActiveState = storedActiveState;
        user->GetSocket()->Send("chddd", 0xfe, 0x0c, 0, storedActiveState, uiSlot);
        static_cast<void>(itemClassId);
        return false;
    }

    return user->OnExAutoSoulShotPacket(itemClassId, activate, uiSlot, activeState);
}

bool RequestItemLink(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00bfb1e0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int itemServerId;
    ::Disassemble(packet, "d", &itemServerId);
    CItem* const item = ItemSmartIdManager()->GetObjectW(itemServerId);
    if (item == nullptr) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_DISPLAY_REQUIREMENTS_NOT_MET);
        return false;
    }

    CPacketHelperEx<20480> itemPacket;
    CItemAssembler assembler(*user, *item, false);
    assembler.AssembleItemInfo(itemPacket);
    socket->Send("chb", 0xfe, 0x6d, itemPacket.GetLength(), itemPacket.GetPacket());
    return false;
}

bool RequestInquiryGoodsInventory(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be4dd8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_nWorldId != g_nWorldId) {
        socket->SendSystemMessage(SystemMessageId::FUNCTION_UNAVAILABLE_AFTER_DIMENSIONAL_TRANSFER);
        return false;
    }

    char premium = 0;
    ::Disassemble(packet, "c", &premium);
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
    } else {
        user->RequestInquiryGoodsInventory(premium == 1);
    }
    return false;
}

bool RequestRetrieveGoodsInventoryItem(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be4f08, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    char selector = 0;
    const std::uint8_t* const cursor = ::Disassemble(packet, "c", &selector);
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }

    std::int64_t itemUid = 0;
    if (selector == 1) {
        ::Disassemble(cursor, "Q", &itemUid);
        user->RequestRetrieveGoodsInventory(itemUid);
    } else {
        std::int64_t warehouseNo = 0;
        ::Disassemble(cursor, "QQ", &warehouseNo, &itemUid);
        user->WithdrawPremiumItem(warehouseNo, itemUid);
    }
    return false;
}

bool UserPacketEx::WithdrawPremiumItem(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c0063c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (!TalkChannelCache::inst().mChannels[12].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request withdraw premium item with invalid talk channel.",
        //     L"UserPacketEx.cpp",
        //     0x1028,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] user[%s] request withdraw premium item with invalid talk channel.",
        //     L"UserPacketEx.cpp",
        //     0x1028,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "Premium item withdrawal rejected: talk channel validation failed, character=%s, character ID=%u",
        //     ::WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID()))
        L2_LOG_ERROR(
            Log, "Premium item withdrawal rejected: talk channel validation failed, character=%s, character ID=%u",
            ::Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID())
        );
        user->SendSystemMessage(SystemMessageId::SHARED_DIMENSIONAL_ITEM_RETRIEVAL_FAILED);
        return false;
    }

    std::int64_t warehouseNo = 0;
    std::int64_t amount = 0;
    ::Disassemble(packet, "QQ", &warehouseNo, &amount);
    user->WithdrawPremiumItem(warehouseNo, amount);
    return false;
}

bool IsValidCommissionItem(User* const user, CItem* const item) {
    // Current L2Server 0x00be19cc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        return false;
    }
    if (item == nullptr) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }
    if (item->m_itemOption.itemOptionKey.variationOption1 > 0 && item->m_itemOption.itemOptionKey.variationOption2 > 0) {
        user->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
        return false;
    }
    if (item->IsQuestItem()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_QUEST_ITEM);
        return false;
    }
    if (!user->m_inv.Have(item->m_data->index)) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }
    return true;
}

bool IsValidVariationCandidateItem(User* const user, CItem* const item) {
    // Current L2Server 0x00bf45f8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        return false;
    }
    if (item == nullptr) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }
    if (item->m_itemOption.itemOptionKey.variationOption1 >= 1 && item->m_itemOption.itemOptionKey.variationOption2 >= 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_AUGMENT_ALREADY_AUGMENTED_ITEM);
        return false;
    }
    if (item->IsQuestItem()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_QUEST_ITEM);
        return false;
    }
    if (!user->m_inv.Have(item->m_data->index)) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }

    const int itemClassId = item->m_nClassId;
    if (item->m_const->GetCrystalType() != CRT_D && !g_VariationDB.IsExceptionItem(itemClassId) &&
        g_VariationDB.IsValidCandidateItem(static_cast<std::uint32_t>(itemClassId))) {
        return true;
    }
    user->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
    return false;
}

bool IsValidVariationIntensiveItem(User* const user, CItem* const item) {
    // Current L2Server 0x00bf4788, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        return false;
    }
    if (item == nullptr) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }
    if (item->IsQuestItem()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_QUEST_ITEM);
        return false;
    }
    if (item->m_itemOption.itemOptionKey.variationOption1 > 0 && item->m_itemOption.itemOptionKey.variationOption2 > 0) {
        user->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
        return false;
    }
    if (!user->m_inv.Have(item->m_data->index)) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }
    if (!g_VariationDB.HasIntenItem(item->m_nClassId)) {
        user->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
        return false;
    }
    if (!item->CheckItemUseCondition(user)) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_MATERIAL_LEVEL_TOO_HIGH);
        return false;
    }
    return true;
}

bool IsValidVariationCancelCandiateItem(User* const user, CItem* const item) {
    // Current L2Server 0x00bf4920, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        return false;
    }
    if (item == nullptr) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }
    if (item->m_itemOption.itemOptionKey.variationOption1 < 1 || item->m_itemOption.itemOptionKey.variationOption2 < 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_REMOVE_AUGMENTATION_FROM_UNAUGMENTED_ITEM);
        return false;
    }
    if (item->IsQuestItem()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_QUEST_ITEM);
        return false;
    }
    if (!user->m_inv.Have(item->m_data->index)) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }

    const int itemClassId = item->m_nClassId;
    if (item->m_data->GetLocked() == 0 && item->m_const->GetCrystalType() != CRT_D &&
        !g_VariationDB.IsExceptionItem(itemClassId) &&
        g_VariationDB.IsValidCandidateItem(static_cast<std::uint32_t>(itemClassId))) {
        return true;
    }
    user->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
    return false;
}

bool IsValidStateOfUser(User* const user) {
    // Current L2Server 0x00bfeda8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        return false;
    }
    if (!TalkChannelCache::inst().mChannels[8].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request variation with invalid "
        //     L"talk channel.",
        //     L"UserPacketEx.cpp",
        //     0x82b,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] user[%s] request variation with invalid "
        //     L"talk channel.",
        //     L"UserPacketEx.cpp",
        //     0x82b,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "Augmentation request rejected: talk channel validation failed, character=%s, character ID=%u",
        //     ::WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID()))
        L2_LOG_ERROR(
            Log, "Augmentation request rejected: talk channel validation failed, character=%s, character ID=%u",
            ::Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID())
        );
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }
    if (user->GetPrivateStoreType() >= 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_AUGMENT_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }
    if (!user->IsItemUsable(0)) {
        if (!user->IsActive() && user->m_data->m_bIsAlive == 0) {
            user->SendSystemMessage(SystemMessageId::CANNOT_AUGMENT_WHILE_DEAD);
        }
        if (user->IsNowTrade()) {
            user->SendSystemMessage(SystemMessageId::CANNOT_AUGMENT_DURING_TRADE);
        }
        if (!user->m_SkillMod.m_bCanAct) {
            user->SendSystemMessage(SystemMessageId::CANNOT_AUGMENT_WHILE_PETRIFIED);
        }
        return false;
    }
    if (user->IsFrozen(true)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_AUGMENT_WHILE_FROZEN);
        return false;
    }
    if (user->m_pFishingController->m_FishingState != FISHINGS_NONE ||
        user->m_pFishingController->m_bIsAutoFishing == true) {
        user->SendSystemMessage(SystemMessageId::CANNOT_AUGMENT_WHILE_FISHING);
        return false;
    }
    if (user->m_data->m_StopMode != STP_STAND) {
        user->SendSystemMessage(SystemMessageId::CANNOT_AUGMENT_WHILE_SITTING);
        return false;
    }
    return user->m_lWaitVariation != 1;
}

bool TryToPutItemForVariationCancelPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bffb40, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int itemServerId = 0;
    ::Disassemble(packet, "d", &itemServerId);
    CItem* const item = ItemSmartIdManager()->GetObjectW(itemServerId);
    if (item == nullptr) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }

    bool valid = false;
    const ItemOption itemOption(item->m_itemOption);
    const ItemOptionKey optionKey(itemOption.itemOptionKey);
    std::int64_t cancelAdena = 0;
    if (IsValidStateOfUser(user)) {
        valid = IsValidVariationCancelCandiateItem(user, item);
        const CommissionInfo commission = g_VariationDB.GetCommissionInfo(
            static_cast<std::uint32_t>(item->m_nClassId), static_cast<std::uint32_t>(itemOption.intensiveItemClassID)
        );
        cancelAdena = commission.cancelAdena;
    }
    socket->Send(
        "chddddQd", 0xfe, 0x58, static_cast<int>(item->Id()), item->m_nClassId, optionKey.variationOption1,
        optionKey.variationOption2, cancelAdena, valid
    );
    return false;
}

bool ClickVariationCancelButtonPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bffd24, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int itemServerId = 0;
    ::Disassemble(packet, "d", &itemServerId);
    CItem* const item = ItemSmartIdManager()->GetObjectW(itemServerId);
    if (item == nullptr) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }

    const ItemOption itemOption(item->m_itemOption);
    CItem* const adena = user->m_inv.GetFirstItemByClassID(0x39, false, false);
    if (!IsValidStateOfUser(user)) {
        user->GetSocket()->Send("chd", 0xfe, 0x59, 0);
        return false;
    }

    bool valid = IsValidVariationCancelCandiateItem(user, item);
    const CommissionInfo commission = g_VariationDB.GetCommissionInfo(
        static_cast<std::uint32_t>(item->m_nClassId), static_cast<std::uint32_t>(itemOption.intensiveItemClassID)
    );
    if (commission.cancelAdena > 0 && (adena == nullptr || adena->m_data->m_nCount < commission.cancelAdena)) {
        user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
        valid = false;
    }
    if (!valid) {
        user->GetSocket()->Send("chd", 0xfe, 0x59, 0);
        return false;
    }

    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 1) == 1) {
        user->GetSocket()->Send("chd", 0xfe, 0x59, 0);
        return false;
    }

    // Current virtual call passes the literal pending-item slot 0.
    user->SetPendingUseETCItem(item, PUIT_ENCHANT);
    db.RequestCancelVariationItem(item, user, commission.cancelAdena);
    return false;
}

bool ChooseInventoryAttributeItem(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bee034, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    int targetItemServerId = 0;
    std::int64_t attributeCount = 0;
    ::Disassemble(packet, "dQ", &targetItemServerId, &attributeCount);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CItem* const firstTarget = user->m_inv.GetByServerID(targetItemServerId);
    CItem* const firstPending = user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT);
    if (firstTarget != nullptr && CItemRestrict::Inst().CheckItemRestrict(firstTarget->m_data->m_nClassID)) {
        user->SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_ENCHANT);
        socket->Send("chd", 0xfe, 0x62, 2);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]item restrict on. ItemClassID[%d]",
        //     "UserPacketEx.cpp",
        //     0xabb,
        //     firstTarget->m_data->m_nClassID)
        L2_LOG_WARN(
            Log, "Attribute enchant rejected: target item class %d is restricted (character ID %u)",
            firstTarget->m_data->m_nClassID, static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    if (firstPending != nullptr && CItemRestrict::Inst().CheckItemRestrict(firstPending->m_data->m_nClassID)) {
        user->SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_ENCHANT);
        socket->Send("chd", 0xfe, 0x62, 2);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]item restrict on. ItemClassID[%d]",
        //     "UserPacketEx.cpp",
        //     0xac3,
        //     firstPending->m_data->m_nClassID)
        L2_LOG_WARN(
            Log, "Attribute enchant rejected: pending item class %d is restricted (character ID %u)",
            firstPending->m_data->m_nClassID, static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    if (user->GetPrivateStoreType() > 0) {
        user->SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_ENCHANT);
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ADD_ATTRIBUTE_DURING_PRIVATE_STORE_OR_WORKSHOP);
        user->GetSocket()->Send("chd", 0xfe, 0x62, 2);
        return false;
    }

    if (targetItemServerId == -1 && user->CheckAndResetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT)) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::ATTRIBUTE_ENCHANT_ITEM_USE_CANCELLED);
        socket->Send("chd", 0xfe, 0x62, 2);
        return false;
    }

    CItem* const targetItem = user->m_inv.GetByServerID(targetItemServerId);
    if (targetItem == nullptr || !targetItem->IsItem() || targetItem->IsDuration() ||
        static_cast<int>(targetItem->m_const->GetCrystalType()) < 5 ||
        (targetItem->m_data->m_Type == IT_ARMOR && (targetItem->m_data->m_Slot & INT64_C(0x100)) != 0)) {
        L2_LOG_WARN(
            Log, "[attribute enchant] Character %u: target %d rejected (class %d, crystal grade %d, duration item %d).",
            static_cast<unsigned>(user->GetDBID()), targetItemServerId, targetItem ? targetItem->m_nClassId : -1,
            targetItem ? static_cast<int>(targetItem->m_const->GetCrystalType()) : -1,
            targetItem ? static_cast<int>(targetItem->IsDuration()) : -1
        );
        user->SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_ENCHANT);
        socket->Send("chd", 0xfe, 0x62, 2);
        return false;
    }

    if (targetItem->m_data->GetLocked() != 0) {
        user->SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_ENCHANT);
        socket->Send("chd", 0xfe, 0x62, 2);
        user->SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        return false;
    }

    CItem* const pendingItem = user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT);
    if (pendingItem == nullptr || !user->m_inv.Have(pendingItem->m_data->index)) {
        user->SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_ENCHANT);
        socket->Send("chd", 0xfe, 0x62, 2);
        user->GetSocket()->SendSystemMessage(SystemMessageId::ATTRIBUTE_ENCHANT_ITEM_USE_CANCELLED);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Choose inventory item fail. Pending item is null",
        //     "UserPacketEx.cpp",
        //     0xafd)
        L2_LOG_WARN(
            Log,
            "Attribute enchant cancelled: pending item is missing or no longer in inventory (character ID %u, target item "
            "ID %d)",
            static_cast<unsigned>(user->GetDBID()), targetItemServerId
        );
        return false;
    }

    for (const CSkillInfo* const skill : pendingItem->m_ItemSkillList) {
        if (skill->CheckOperateCond(user, targetItem, CSkillInfo::OCT_BOTH) != -2) {
            user->SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_ENCHANT);
            CItemConstInfo* const itemInfo = g_ItemConstInfoDB.GetItemConstInfo(targetItem->m_data->m_nClassID);
            user->GetSocket()->SendSystemMessage(
                itemInfo == nullptr || !itemInfo->HasEquipConditionType(ECT_HERO)
                    ? SystemMessageId::ATTRIBUTE_ENCHANT_ITEM_REQUIREMENTS_NOT_MET
                    : SystemMessageId::CANNOT_ADD_ATTRIBUTE_TO_HERO_ITEM
            );
            socket->Send("chd", 0xfe, 0x62, 2);
            return false;
        }
    }

    CObject* const previousTarget = user->m_spTarget.GetObjectW();
    user->SetSkillTarget(targetItem);
    user->nAttributeCount = attributeCount;
    user->EnchantAttrItem(pendingItem, targetItem);
    user->SetSkillTarget(previousTarget);
    return false;
}

bool CannotMoveAnymoreInAirShipPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00be1f60.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int airShipId;
    int x;
    int y;
    int z;
    int heading;
    Disassemble(packet, "ddddd", &airShipId, &x, &y, &z, &heading);

    if (user->IsInAirShip() && airShipId != 0) {
        user->ValidateLocationInAirShip(
            FVector(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)), heading, airShipId
        );
    }
    user->StopMove();
    return false;
}

bool MoveToLocationInAirShipPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00be20a8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int airShipId;
    int targetX;
    int targetY;
    int targetZ;
    int originX;
    int originY;
    int originZ;
    Disassemble(packet, "ddddddd", &airShipId, &targetX, &targetY, &targetZ, &originX, &originY, &originZ);
    (void)originX;
    (void)originY;
    (void)originZ;

    CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(airShipId));
    if (object == nullptr || !object->IsAirShip()) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
    } else {
        user->MoveToLocationRelative(static_cast<CAirShip*>(object), targetX, targetY, targetZ);
    }
    return false;
}

bool CannotAirShipMoveAnymorePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bee680.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int x;
    int y;
    int z;
    int airShipId;
    Disassemble(packet, "dddd", &x, &y, &z, &airShipId);

    CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(static_cast<std::uint32_t>(airShipId));
    if (airShip != nullptr) {
        airShip->OnCannotMoveAnymorePacket(x, y, z);
    }
    return false;
}

bool MoveToLocationAirShipPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00be1bc0.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        (void)user->OnMoveToLocationAirShipPacket(packet);
    }
    return user == nullptr;
}

bool RequestPVPMatchRecord(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bf9188.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    CPVPMatchSystem::inst().SendCurrentRecordForUI(user);
    return false;
}

namespace UserPacketEx {

bool AgitAuctionCmdPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be3768, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        int command = 0;
        const std::uint8_t* const commandPacket = ::Disassemble(packet, "d", &command);
        CAgitAuctionPacketHandler handler(socket);
        handler.Process(command, commandPacket);
    }
    return user == nullptr;
}

bool RequestOnReceiveNCGuardClientData(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be2420.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (g_bInitedNCGuard) {
        int size = 0;
        const std::uint8_t* const data = ::Disassemble(packet, "d", &size);
        if (size > 0) {
            const NCGuardServerResult result =
                OnReceivedNCGuardClientMsg(user->GetAccountID(), data, static_cast<std::uint32_t>(size));
            if (result != 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"OnReceiveNCGuardCallback failed[%d]",
                //     result)
                L2_LOG_ERROR(
                    Log, "NCGuard client message processing failed: result=%d, account ID=%u", static_cast<int>(result),
                    static_cast<unsigned>(user->GetAccountID())
                );
            }
        }
    }
    return false;
}

bool UserHardwareInfoPacket(CSocket* const /*baseSocket*/, const std::uint8_t* const packet) {
    // Current L2Server 0x00be5198.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 20> hardwareId;
    std::memset(hardwareId.data(), 0, 0x24);
    const std::uint8_t* cursor = ::Disassemble(packet, "S", 0x24, hardwareId.data());

    int hardware1;
    int hardware2;
    int hardware3;
    int hardware4;
    cursor = ::Disassemble(cursor, "dddd", &hardware1, &hardware2, &hardware3, &hardware4);

    int hardware5;
    int hardware6;
    cursor = ::Disassemble(cursor, "dd", &hardware5, &hardware6);

    std::array<char16_t, 56> processorName;
    std::memset(processorName.data(), 0, 100);
    int processor1;
    int processor2;
    int processor3;
    int processor4;
    cursor = ::Disassemble(cursor, "Sdddd", 100, processorName.data(), &processor1, &processor2, &processor3, &processor4);

    int memory1;
    int memory2;
    int memory3;
    cursor = ::Disassemble(cursor, "ddd", &memory1, &memory2, &memory3);

    std::array<char16_t, 56> deviceName;
    std::array<char16_t, 56> driverName;
    std::memset(deviceName.data(), 0, 100);
    std::memset(driverName.data(), 0, 100);
    int device1;
    int device2;
    int device3;
    ::Disassemble(cursor, "dddSS", &device1, &device2, &device3, 100, deviceName.data(), 100, driverName.data());

    CLogFormatter formatter;
    formatter.WriteLog(
        0x5e0, 0, 0, 0, 0, 0, 0, 0, hardwareId.data(), processorName.data(), nullptr, 1, processor1, processor2, processor3,
        processor4, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, 0
    );
    formatter.WriteLog(
        0x5e0, 0, 0, 0, 0, 0, 0, 0, hardwareId.data(), nullptr, nullptr, 2, memory1, memory2, memory3, 0, 0, 0, 0, 0, 0,
        nullptr, nullptr, nullptr, nullptr, 0
    );
    formatter.WriteLog(
        0x5e0, 0, 0, 0, 0, 0, 0, 0, hardwareId.data(), nullptr, nullptr, 3, hardware1, hardware2, hardware3, hardware4, 0, 0,
        0, 0, 0, nullptr, nullptr, nullptr, nullptr, 0
    );
    formatter.WriteLog(
        0x5e0, 0, 0, 0, 0, 0, 0, 0, hardwareId.data(), deviceName.data(), driverName.data(), 4, hardware5, hardware6,
        device3, device1, device2, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, 0
    );
    return false;
}

bool RequestSysString(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be6770.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int stringNumber = 0;
    std::array<char16_t, 25> value{};
    ::Disassemble(packet, "dS", &stringNumber, 0x32, value.data());
    g_ObjectDB.SetSysString(user->m_data->m_CountryCode, stringNumber, value.data());
    return false;
}

bool RequestSaveClientINI(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf8b94.
    L2_GUARD_FUNCTION();

    if (!g_bProcessClientINI) {
        return false;
    }

    CUserSocket* const socket = baseSocket != nullptr ? baseSocket->CastUserSocket() : nullptr;
    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]UserSocket is NULL!",
        //     L"UserPacketEx.cpp",
        //     0x2cb6)
        L2_LOG_ERROR(Log, "Client INI save request rejected: client socket is unavailable");
        return false;
    }

    network::Stream stream(const_cast<std::uint8_t*>(packet), 0x4000);
    protocol::CS_ClientINI request{};
    if (!request.Decode(&stream)) {
        User* const user = socket->GetUser();
        if (user == nullptr) {
            const std::string accountName = Utf16ToUtf8(socket->GetAccountName());
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] RequestSaveClientINI decode fail account[%s]",
            //     L"UserPacketEx.cpp",
            //     0x2cad,
            //     accountName.c_str())
            L2_LOG_WARN(
                Log, "Client INI save request could not be decoded, account=%s", accountName.c_str()
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] RequestSaveClientINI decode fail user[%s]",
            //     L"UserPacketEx.cpp",
            //     0x2ca9,
            //     user->GetName())
            // L2_LOG_WARN(Log,
            //     "Client INI save request could not be decoded, character=%s",
            //     ::WideToUtf8(user->GetName()).c_str())
            L2_LOG_WARN(
                Log, "Client INI save request could not be decoded, character=%s", ::Utf16ToUtf8(user->GetName()).c_str()
            );
        }
        return false;
    }

    socket->SaveINI(static_cast<int>(request.cType), request.strIniStr.c_str());
    return false;
}

bool ReqeustCaptchaAnswer(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be8c3c. Preserve the shipping spelling.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int64_t transactionId = 0;
    int answer = 0;
    ::Disassemble(packet, "Qd", &transactionId, &answer);
    if (g_CaptchaManager.ConfirmAnswer(transactionId, answer) == CAPTCHA::ACR_SUCCESS) {
        user->ConfirmCaptchaAnswerSuccess();
    } else {
        user->ConfirmCaptchaAnswerFail();
    }
    return false;
}

bool RefreshCaptchaImage(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be8d38.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int64_t transactionId = 0;
    ::Disassemble(packet, "Q", &transactionId);
    g_CaptchaManager.RefreshCaptchaImage(user->GetAccountID(), transactionId);
    return false;
}

bool QueueTicketPacket(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00bf4adc.
    L2_GUARD_FUNCTION();

    if (!g_bQueueTicketEnabled || baseSocket == nullptr) {
        return true;
    }

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    const std::uint32_t accountId = socket->GetQueueTicketAccountID();
    if (accountId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [SHANGHAI_QUEUETICKET] Invalid "
        //     L"QueueTicketAccountID in socket %d != %d",
        //     L"UserPacketEx.cpp",
        //     0x2b3f,
        //     accountId,
        //     accountId)
        L2_LOG_ERROR(Log, "Queue ticket request rejected: socket has no queue account ID");
        return true;
    }

    char status = 0;
    {
        L2_READ_GUARD(AccountAuthInfoLock);
        const auto account = g_AccountAuthInfoMap.find(accountId);
        if (account == g_AccountAuthInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d],[SHANGHAI_QUEUETICKET], account not found... "
            //     L"account_id(%d)",
            //     L"UserPacketEx.cpp",
            //     0x2b4b,
            //     accountId)
            L2_LOG_ERROR(
                Log, "Queue ticket request rejected: account authorization entry is missing, account ID=%u",
                static_cast<unsigned>(accountId)
            );
            return true;
        }
        if (account->second.WaiterStatus == WAITER_NO) {
            status = 1;
        } else if (account->second.WaiterStatus == WAITER_VIPPASS) {
            status = 3;
        } else if (account->second.WaiterStatus == WAITER_QUEUETICKET) {
            status = 2;
        }
    }

    if (!g_bQueueTicketVipWait) {
        {
            L2_READ_GUARD(g_AccountVIPLevelByDBLock);
            const auto vip = g_AccountVIPLevelByDBMap.find(accountId);
            if (vip != g_AccountVIPLevelByDBMap.end() && vip->second.m_bIsLoadedByCacheD && vip->second.m_cVIPLevel != 0 &&
                g_nQueueTicketPassVipLevel <= static_cast<int>(vip->second.m_cVIPLevel)) {
                status = 3;
                vip->second.m_bIsLoadedByCacheD = false;
            }
        }
    }

    int waiterCount = 0;
    authd.AuthWaitUserReq(static_cast<int>(accountId));
    if (status == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d],[SHANGHAI_QUEUETICKET],queue ticket system error %d",
        //     L"UserPacketEx.cpp",
        //     0x2bc5,
        //     accountId)
        L2_LOG_ERROR(
            Log, "Queue ticket request has no valid admission status, account ID=%u", static_cast<unsigned>(accountId)
        );
    } else if (status == 1 || status == 3) {
        QueueTicket::Inst()->CancleWaiter(static_cast<int>(accountId));
        socket->SetPassQueueTicket(true);
    } else if (status == 2) {
        if (!QueueTicket::Inst()->AddWaiter(static_cast<int>(accountId), socket)) {
            status = 0;
        } else if (g_bQueueTicketNormalWait) {
            waiterCount = QueueTicket::Inst()->GetWaiterCount(static_cast<int>(accountId));
        } else {
            if (g_nServerUserCount <= g_nServerUserCountLimit - 1 &&
                QueueTicket::Inst()->AbleEntrance(static_cast<int>(accountId))) {
                int forceCount = g_nServerUserCountLimit - g_nServerUserCount;
                if (forceCount > 10) {
                    forceCount = 10;
                }
                QueueTicket::Inst()->EntranceForce(forceCount);
                status = 1;
                socket->SetPassQueueTicket(true);
            } else {
                waiterCount = QueueTicket::Inst()->GetWaiterCount(static_cast<int>(accountId));
                if (waiterCount == 0) {
                    // Log.Add(
                    //     LOG_NORMAL,
                    //     L"[SHANGHAI_QUEUETICKET],entranced by force %d, "
                    //     L"players %d/%d",
                    //     accountId,
                    //     g_nServerUserCount,
                    //     g_nServerUserCountLimit)
                    L2_LOG_INFO(
                        Log, "Queue ticket waiter count is zero; skipping queue response, account ID=%u, players=%d/%d",
                        static_cast<unsigned>(accountId), g_nServerUserCount, g_nServerUserCountLimit
                    );
                    return false;
                }
            }
        }
    }

    socket->Send("chcd", 0xfe, 0x1ad, status, waiterCount);
    return false;
}

bool RequestSSOAuthNToken(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00be8f0c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (!authd.AuthRequestAuthNToken(user->GetAccountID())) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][STEAM] AUTHNTOKEN Request Fail!! "
        //     L"(In AuthRequestAuthNToken())",
        //     L"UserPacketEx.cpp",
        //     0x3149)
        L2_LOG_ERROR(
            Log, "SSO authentication token request failed; sending failure response, account ID=%u",
            static_cast<unsigned>(user->GetAccountID())
        );
        user->GetSocket()->Send("chcb", 0xfe, 0x1b2, 0, 0, 0);
    }
    return false;
}

bool FlyMove(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bf8698.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (user->m_bFlyMoving) {
        int nodeId = 0;
        ::Disassemble(packet, "d", &nodeId);

        CFlyMoveManager& manager = CFlyMoveManager::Inst();
        const int pathId = user->m_FlyMoveInfo.path_id;
        const int currentNodeId = user->m_FlyMoveInfo.node_id;
        if (nodeId == -1) {
            manager.Land(user, pathId, currentNodeId);
        } else if (manager.IsRelatedNode(pathId, currentNodeId, nodeId)) {
            user->m_nPrevNode = currentNodeId;
            user->m_FlyMoveInfo.node_id = nodeId;
            manager.FlyMove(user, pathId, nodeId, false);
        }
    }
    return false;
}

bool FlyMoveStart(CSocket* baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00be50dc.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->StartFlyMove();
    }
    return user == nullptr;
}

bool RequestChangeTarget(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00be87a0.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int targetId = 0;
    int requestType = 0;
    ::Disassemble(packet, "dh", &targetId, &requestType);
    if (targetId == -1) {
        // Current MSVC evaluates the name getter before the DBID getter.
        const char16_t* const userName = user->GetName();
        const int userDbId = static_cast<int>(user->GetDBID());
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid TargetID(-1) : User DBID[%d], User Name[%s]",
        //     userDbId,
        //     userName)
        // L2_LOG_WARN(Log,
        //     "Target change rejected: target ID=-1, character ID=%d, character=%s",
        //     userDbId,
        //     ::WideToUtf8(userName).c_str())
        L2_LOG_WARN(
            Log, "Target change rejected: target ID=-1, character ID=%d, character=%s", userDbId,
            ::Utf16ToUtf8(userName).c_str()
        );
        return false;
    }

    CObject* const target = CObject::GetObjectW(static_cast<std::uint32_t>(targetId));
    if (requestType == 1 && user->m_SkillDurationMod.m_nBlockCanControllCount != 0) {
        socket->Send("cd", 0xb9, 0);
    } else if (target != nullptr) {
        user->ChangeTarget(target, CTR_BY_CONTEXT_MENU);
    }
    return false;
}

} // namespace UserPacketEx

bool RequestBaseAttributeCancel(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0045c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (user->m_spTalkTarget.GetObjectW() == nullptr) {
        user->LogAbnormalAction(AUA_INVALID_NPC_OR_CHANNEL, false, 0, 0, 0, 0);
        return true;
    }

    if (!TalkChannelCache::inst().mChannels[11].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        user->LogAbnormalAction(AUA_INVALID_NPC_OR_CHANNEL, false, 0, 0, 0, 0);
        return false;
    }

    int itemServerId = 0;
    int attribute = 0;
    ::Disassemble(packet, "dd", &itemServerId, &attribute);
    if (attribute < 0 || attribute > 5) {
        const char16_t* const userName = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid attribute %d requested by user %s",
        //     attribute,
        //     userName)
        // L2_LOG_WARN(Log,
        //     "Attribute cancellation requested with invalid attribute=%d (expected 0..5), character=%s, item ID=%d",
        //     attribute,
        //     ::WideToUtf8(userName).c_str(), itemServerId)
        L2_LOG_WARN(
            Log, "Attribute cancellation requested with invalid attribute=%d (expected 0..5), character=%s, item ID=%d",
            attribute, ::Utf16ToUtf8(userName).c_str(), itemServerId
        );
    }

    CItem* const item = user->m_inv.GetByServerID(itemServerId);
    static_cast<void>(user->BaseAttributeCancel(item, static_cast<AttributeTypeEnum>(attribute)));
    return false;
}

namespace UserPacketEx {

bool GetOnShuttlePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bfd37c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int shuttleId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    ::Disassemble(packet, "dddd", &shuttleId, &x, &y, &z);

    CShuttle* const shuttle = CShuttleManager::Inst().GetShuttle(shuttleId);
    if (shuttle == nullptr) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }

    if (user->HavePet() || user->HaveSummon()) {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            user, summon, summon->OnTeleport(); summon->SetMyShuttleID(static_cast<unsigned int>(shuttleId));
            static_cast<void>(CWorld::Inst().GetOnShuttle(
                summon, shuttle, FVector(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z))
            ));
        );
    }

    user->SetMyShuttleID(static_cast<unsigned int>(shuttleId));
    static_cast<void>(CWorld::Inst().GetOnShuttle(
        user, shuttle, FVector(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z))
    ));
    return false;
}

bool GetOffShuttlePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bfd6e4.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int shuttleId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    ::Disassemble(packet, "dddd", &shuttleId, &x, &y, &z);

    CShuttle* const shuttle = CShuttleManager::Inst().GetShuttle(shuttleId);
    if (shuttle == nullptr) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }

    int adjustedX = x;
    int adjustedY = y;
    const int queryZ = runtime::AddInt32Wrapping(z, 0x20);
    int groundZ = CWorld::Inst().GetGeo()->FindGround(adjustedX, adjustedY, queryZ);
    bool foundGround = runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(groundZ, z)) < 0x20;
    if (!foundGround) {
        groundZ = CWorld::Inst().GetGeo()->FindGround(runtime::AddInt32Wrapping(x, 0x10), y, queryZ);
        if (runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(groundZ, z)) < 0x20) {
            adjustedX = runtime::AddInt32Wrapping(x, 0x10);
            foundGround = true;
        }
    }
    if (!foundGround) {
        groundZ = CWorld::Inst().GetGeo()->FindGround(runtime::AddInt32Wrapping(x, -0x10), y, queryZ);
        if (runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(groundZ, z)) < 0x20) {
            adjustedX = runtime::AddInt32Wrapping(x, -0x10);
            foundGround = true;
        }
    }
    if (!foundGround) {
        groundZ = CWorld::Inst().GetGeo()->FindGround(x, runtime::AddInt32Wrapping(y, 0x10), queryZ);
        if (runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(groundZ, z)) < 0x20) {
            adjustedY = runtime::AddInt32Wrapping(y, 0x10);
            foundGround = true;
        }
    }
    if (!foundGround) {
        groundZ = CWorld::Inst().GetGeo()->FindGround(x, runtime::AddInt32Wrapping(y, -0x10), queryZ);
        if (runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(groundZ, z)) < 0x20) {
            adjustedY = runtime::AddInt32Wrapping(y, -0x10);
            foundGround = true;
        }
    }

    FVector destination(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
    if (foundGround) {
        destination = FVector(static_cast<double>(adjustedX), static_cast<double>(adjustedY), static_cast<double>(groundZ));
    }

    user->StopMove();
    user->SetMyShuttleID(0);
    static_cast<void>(CWorld::Inst().GetOffShuttle(user, shuttle, destination, true));

    if (user->HavePet() || user->HaveSummon()) {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            user, summon, summon->OnTeleport(); summon->SetMyShuttleID(0);
            static_cast<void>(CWorld::Inst().GetOffShuttle(summon, shuttle, destination, true));
        );
    }
    return false;
}

bool MoveToLocationInShuttlePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bfdbd0.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int shuttleId = 0;
    int targetX = 0;
    int targetY = 0;
    int targetZ = 0;
    int originX = 0;
    int originY = 0;
    int originZ = 0;
    ::Disassemble(packet, "ddddddd", &shuttleId, &targetX, &targetY, &targetZ, &originX, &originY, &originZ);
    static_cast<void>(originX);
    static_cast<void>(originY);
    static_cast<void>(originZ);

    CShuttle* const shuttle = CShuttleManager::Inst().GetShuttle(shuttleId);
    if (shuttle == nullptr) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }

    const double radius = shuttle->m_const->m_dRadius;
    const double negativeX = 0.0 - static_cast<double>(targetX);
    const double negativeY = 0.0 - static_cast<double>(targetY);
    if (radius * radius < negativeX * negativeX + negativeY * negativeY) {
        // user->ActionFailed(1)
        user->ActionFailed(AF_MOVE);
        return false;
    }

    user->MoveToLocationRelative(shuttle, targetX, targetY, targetZ);
    return false;
}

bool CannotMoveAnyMoreInShuttlePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bfddb8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int shuttleId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int heading = 0;
    ::Disassemble(packet, "ddddd", &shuttleId, &x, &y, &z, &heading);

    CShuttle* const shuttle = CShuttleManager::Inst().GetShuttle(shuttleId);
    const int shuttleObjectId = shuttle == nullptr ? 0 : static_cast<int>(shuttle->Id());
    if (user->IsInShuttle()) {
        user->ValidateLocationInShuttle(
            FVector(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)), heading, shuttleObjectId
        );
    }
    user->StopMove();
    return false;
}

bool TryToPutItemForVariationMakePacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bfefec, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int itemServerId = 0;
    ::Disassemble(packet, "d", &itemServerId);
    CItem* const item = ItemSmartIdManager()->GetObjectW(itemServerId);
    if (item == nullptr) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }
    if (item->m_data->GetLocked() != 0) {
        user->SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        return false;
    }

    bool valid = false;
    if (IsValidStateOfUser(user)) {
        valid = IsValidVariationCandidateItem(user, item);
    }
    socket->Send("chddd", 0xfe, 0x54, static_cast<int>(item->Id()), item->m_nClassId, valid);
    return false;
}

bool TryToPutIntensiveForVariationMakePacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bff16c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int candidateServerId = 0;
    int intensiveServerId = 0;
    ::Disassemble(packet, "dd", &candidateServerId, &intensiveServerId);
    CItem* const candidate = ItemSmartIdManager()->GetObjectW(candidateServerId);
    CItem* const intensive = ItemSmartIdManager()->GetObjectW(intensiveServerId);
    if (candidate == nullptr || intensive == nullptr) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }

    if (!IsValidStateOfUser(user)) {
        socket->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
        return false;
    }

    const bool valid = IsValidVariationCandidateItem(user, candidate) && IsValidVariationIntensiveItem(user, intensive);
    const CommissionInfo commission = g_VariationDB.GetCommissionInfo(
        static_cast<std::uint32_t>(candidate->m_nClassId), static_cast<std::uint32_t>(intensive->m_nClassId)
    );
    if (commission.cancelAdena == 0 && commission.itemClassID == 0 && commission.itemCount == 0) {
        socket->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
        return false;
    }

    socket->Send(
        "chdddQd", 0xfe, 0x55, static_cast<int>(intensive->Id()), intensive->m_nClassId, commission.itemClassID,
        commission.itemCount, valid
    );
    return false;
}

bool TryToPutCommissionForVariationMakePacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bff368, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int candidateServerId = 0;
    int intensiveServerId = 0;
    int commissionItemServerId = 0;
    std::int64_t amount = 0;
    ::Disassemble(packet, "dddQ", &candidateServerId, &intensiveServerId, &commissionItemServerId, &amount);

    CItem* const candidate = ItemSmartIdManager()->GetObjectW(candidateServerId);
    CItem* const intensive = ItemSmartIdManager()->GetObjectW(intensiveServerId);
    CItem* commissionItem = nullptr;
    if (amount > 0) {
        commissionItem = ItemSmartIdManager()->GetObjectW(commissionItemServerId);
    }
    if (candidate == nullptr || intensive == nullptr || (amount > 0 && commissionItem == nullptr) || amount < 0) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }

    bool valid = false;
    CommissionInfo commission;
    std::int64_t remaining = 0;
    if (IsValidStateOfUser(user)) {
        valid = IsValidVariationCandidateItem(user, candidate) && IsValidVariationIntensiveItem(user, intensive) &&
            (amount == 0 || IsValidCommissionItem(user, commissionItem));
        commission = g_VariationDB.GetCommissionInfo(
            static_cast<std::uint32_t>(candidate->m_nClassId), static_cast<std::uint32_t>(intensive->m_nClassId)
        );
        if (amount > 0 && commissionItem->m_nClassId != commission.itemClassID) {
            user->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
            valid = false;
        }
        remaining = runtime::SubtractInt64Wrapping(commission.itemCount, amount);
        if (remaining != 0) {
            user->SendSystemMessage(SystemMessageId::INVALID_GEMSTONE_QUANTITY);
            valid = false;
        }
    }

    socket->Send(
        "chddQQd", 0xfe, 0x56, amount > 0 ? static_cast<int>(commissionItem->Id()) : 0,
        amount > 0 ? commissionItem->m_nClassId : 0, amount, remaining, valid
    );
    return false;
}

bool TryToMakeVariationPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bff630, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int candidateServerId = 0;
    int intensiveServerId = 0;
    int commissionItemServerId = 0;
    std::int64_t amount = 0;
    ::Disassemble(packet, "dddQ", &candidateServerId, &intensiveServerId, &commissionItemServerId, &amount);

    CItem* const candidate = ItemSmartIdManager()->GetObjectW(candidateServerId);
    CItem* const intensive = ItemSmartIdManager()->GetObjectW(intensiveServerId);
    CItem* commissionItem = nullptr;
    if (amount > 0) {
        commissionItem = ItemSmartIdManager()->GetObjectW(commissionItemServerId);
    }
    if (candidate == nullptr || intensive == nullptr || (amount > 0 && commissionItem == nullptr) || amount < 0) {
        user->SendSystemMessage(SystemMessageId::AUGMENTATION_FAILED_INVALID_PROCEDURE);
        return false;
    }

    const int candidateClassId = candidate->m_data->m_nClassID;
    if (CItemRestrict::Inst().CheckItemRestrict(candidateClassId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d][NO_ERROR]Item Restrict on. "
        //     L"ItemClassID[%d]",
        //     "UserPacketEx.cpp",
        //     0x9ee,
        //     candidateClassId)
        L2_LOG_WARN(
            Log, "Augmentation rejected: target item class %d is restricted, character ID=%u", candidateClassId,
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    if (candidate->m_data->GetLocked() != 0) {
        user->SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        return false;
    }

    const int intensiveClassId = intensive->m_data->m_nClassID;
    if (CItemRestrict::Inst().CheckItemRestrict(intensiveClassId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d][NO_ERROR]Item Restrict on. "
        //     L"ItemClassID[%d]",
        //     "UserPacketEx.cpp",
        //     0x9fa,
        //     intensiveClassId)
        L2_LOG_WARN(
            Log, "Augmentation rejected: intensive item class %d is restricted, character ID=%u", intensiveClassId,
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    if (commissionItem != nullptr) {
        const int commissionClassId = commissionItem->m_data->m_nClassID;
        if (CItemRestrict::Inst().CheckItemRestrict(commissionClassId)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d][NO_ERROR]Item Restrict on. "
            //     L"ItemClassID[%d]",
            //     "UserPacketEx.cpp",
            //     0xa00,
            //     commissionClassId)
            L2_LOG_WARN(
                Log, "Augmentation rejected: commission item class %d is restricted, character ID=%u", commissionClassId,
                static_cast<unsigned>(user->GetDBID())
            );
            return false;
        }
    }

    if (!IsValidStateOfUser(user)) {
        user->GetSocket()->Send("chddd", 0xfe, 0x57, 0, 0, 0);
        return false;
    }

    bool valid = IsValidVariationCandidateItem(user, candidate) && IsValidVariationIntensiveItem(user, intensive) &&
        (amount == 0 || IsValidCommissionItem(user, commissionItem));
    const CommissionInfo commission = g_VariationDB.GetCommissionInfo(
        static_cast<std::uint32_t>(candidate->m_nClassId), static_cast<std::uint32_t>(intensive->m_nClassId)
    );
    if (amount > 0 && commissionItem->m_nClassId != commission.itemClassID) {
        user->SendSystemMessage(SystemMessageId::UNSUITABLE_ITEM);
        valid = false;
    }
    if (commission.itemCount != amount) {
        user->SendSystemMessage(SystemMessageId::INVALID_GEMSTONE_QUANTITY);
        valid = false;
    }
    if (!valid) {
        user->GetSocket()->Send("chddd", 0xfe, 0x57, 0, 0, 0);
        return false;
    }

    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 1) == 1) {
        user->GetSocket()->Send("chddd", 0xfe, 0x57, 0, 0, 0);
        return false;
    }

    // Current virtual call passes the literal pending-item slot 0.
    user->SetPendingUseETCItem(candidate, PUIT_ENCHANT);
    const VariationKey::WeaponType weaponType = VariationKey::GetWeaponType(candidate);
    const VariationKey variationKey(intensive->m_nClassId, weaponType);
    const ItemOptionKey optionKey = g_VariationDB.GetItemOptionKeyOnCreate(variationKey);
    if (optionKey.variationOption1 < 1 || optionKey.variationOption2 < 1) {
        user->GetSocket()->Send("chddd", 0xfe, 0x57, 0, 0, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
        return false;
    }

    db.RequestMakeVariationItem(candidate, intensive, commissionItem, amount, user, optionKey);
    return false;
}

bool RequestCrystalItemInfo(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00bfacec, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int itemServerId = 0;
    std::int64_t amount = 0;
    ::Disassemble(packet, "dQ", &itemServerId, &amount);

    CItem* item = user->m_inv.GetByServerID(itemServerId);
    if (item == nullptr) {
        CPet* const pet = user->GetPet();
        if (pet != nullptr) {
            item = pet->m_inv.GetByServerID(itemServerId);
        }
    }
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]What a wonderful world(3). item server "
        //     L"id[%d], amount[%I64d] username[%s]",
        //     "UserPacketEx.cpp",
        //     0x1a53,
        //     itemServerId,
        //     amount,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Crystallization info request rejected: item not found in character or pet inventory, item ID=%d, amount=%lld,
        //     character=%s", itemServerId, static_cast<long long>(amount),
        //     ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_WARN(
            Log,
            "Crystallization info request rejected: item not found in character or pet inventory, item ID=%d, amount=%lld, "
            "character=%s",
            itemServerId, static_cast<long long>(amount), ::Utf16ToUtf8(user->GetName()).c_str()
        );
        socket->Send("ch", 0xfe, 0xe2);
        return true;
    }

    if (!user->CheckCrystalItem(item, runtime::Int32FromBits(static_cast<std::uint32_t>(amount)))) {
        user->CheckAndResetPendingUseETCItem(PUIT_CRYSTAL);
        socket->Send("ch", 0xfe, 0xe2);
        return false;
    }
    if (!user->CheckAndSetPendingUseETCItem(item, PUIT_CRYSTAL)) {
        user->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
        user->CheckAndResetPendingUseETCItem(PUIT_CRYSTAL);
        socket->Send("ch", 0xfe, 0xe2);
        return false;
    }

    const int crystalType = static_cast<int>(item->m_const->GetCrystalType());
    const std::int64_t ownedCrystalAmount = user->m_inv.GetItemAmount(CrystalClassByCrystalType[crystalType], 0, true);
    const std::int64_t producedCrystalAmount = item->GetCrystalizeCount(1);
    if (runtime::AddInt64Wrapping(ownedCrystalAmount, producedCrystalAmount) >= INT64_C(1000000000000)) {
        user->CheckAndResetPendingUseETCItem(PUIT_CRYSTAL);
        user->SendSystemMessage(SystemMessageId::CRYSTALLIZATION_QUANTITY_LIMIT_REACHED);
        socket->Send("ch", 0xfe, 0xe2);
        return false;
    }

    std::array<char, 0x5000> itemPacket;
    const int firstLength = ::Assemble(
        itemPacket.data(), static_cast<int>(itemPacket.size()), "dQf", CrystalClassByCrystalType[crystalType],
        producedCrystalAmount, 100.0
    );
    int packetLength = firstLength > 0 ? firstLength : 0;

    std::vector<CrystalItemStruct> crystalItems;
    CItemConstInfo* const itemInfo = g_ItemConstInfoDB.GetItemConstInfo(item->GetClassId());
    if (itemInfo != nullptr) {
        itemInfo->GetCrystalItemList(crystalItems);
        for (const CrystalItemStruct& crystalItem : crystalItems) {
            const int assembled = ::Assemble(
                itemPacket.data() + packetLength, static_cast<int>(itemPacket.size()) - packetLength, "dQf",
                crystalItem.ItemID, crystalItem.CrystalCount, crystalItem.Prob
            );
            if (assembled > 0) {
                packetLength = runtime::AddInt32Wrapping(packetLength, assembled);
            }
        }
    }

    const int itemCount =
        runtime::AddInt32Wrapping(runtime::Int32FromBits(static_cast<std::uint32_t>(crystalItems.size())), 1);
    socket->Send("chdb", 0xfe, 0xe1, itemCount, packetLength, itemPacket.data());
    return false;
}

bool ReplyListMPCCWaitingPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bef25c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_nWorldId != g_nWorldId) {
        socket->SendSystemMessage(SystemMessageId::FUNCTION_UNAVAILABLE_AFTER_DIMENSIONAL_TRANSFER);
        return false;
    }
    if (!user->m_data->m_bCanJoinParty) {
        user->GetSocket()->Send("cdd", 0x3a, 0, 0);
        return false;
    }
    if (!user->IsPartyJoinable()) {
        return false;
    }

    int page = 0;
    int location = 0;
    int level = 0;
    ::Disassemble(packet, "ddd", &page, &location, &level);
    CPartyRoomListFilter filter(
        CSPointer<User>(user, ObjectRefType::FunctionChain), page, static_cast<LocationFilter>(location),
        static_cast<LevelFilter>(level), true
    );
    CPartyMatchingRequest::inst().ListRoom(filter);
    return false;
}

bool ReplyManageMPCCRoom(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bef444, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x121f)
        L2_LOG_ERROR(Log, "Command channel room management request rejected: socket has no character");
        return true;
    }
    if (!user->IsMPCCMaster() || !user->IsPartyJoinable()) {
        return false;
    }

    int roomId = 0;
    int maxMember = 0;
    int lowerLevel = 0;
    int upperLevel = 0;
    int routing = 0;
    std::array<char16_t, 32> title;
    ::Disassemble(packet, "dddddS", &roomId, &maxMember, &lowerLevel, &upperLevel, &routing, 0x40, title.data());

    CParty* const party = user->GetParty();
    if (party != nullptr) {
        routing = static_cast<int>(party->GetRoutingType());
    }
    CMPCCRoomInfo roomInfo(
        roomId, CSPointer<User>(user, ObjectRefType::FunctionChain), lowerLevel, upperLevel, maxMember, routing, title.data()
    );
    CPartyMatchingRequest::inst().ManageRoom(roomInfo);
    return false;
}

bool ReplyJoinMPCCRoom(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bef63c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x124c)
        L2_LOG_ERROR(Log, "Command channel room join request rejected: socket has no character");
        return true;
    }
    if (!user->IsPartyJoinable()) {
        return false;
    }

    int roomId = 0;
    int location = 0;
    int level = 0;
    int page = 0;
    ::Disassemble(packet, "dddd", &roomId, &location, &level, &page);
    CPartyRoomListFilter filter(
        CSPointer<User>(user, ObjectRefType::FunctionChain), page, static_cast<LocationFilter>(location),
        static_cast<LevelFilter>(level), true
    );
    CPartyMatchingRequest::inst().JoinRoom(CSPointer<User>(user, ObjectRefType::FunctionChain), roomId, filter);
    return false;
}

bool RequestPartyMatchingRoomHistory(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf390c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CSPointer<User> userReference(user, ObjectRefType::FunctionChain);
    if (user->m_currentRequestTimeFlag == 2 && runtime::GetTickCount() - user->m_currentRequestTime < 2000U) {
        return false;
    }
    CPartyRoomHistoryManager::inst().SendHistory(userReference);
    return false;
}

bool EventMatch(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be34f8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1654)
        L2_LOG_ERROR(Log, "Event match request ignored: socket has no character");
        return false;
    }

    int matchId = 0;
    int action = 0;
    ::Disassemble(packet, "dd", &matchId, &action);
    if (action == 0) {
        CEventMatchUserCommandHandler::RemoveObserver(matchId, user);
    }
    return false;
}

bool PartyLootingModify(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c05c30, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const CSPointer<CPartyRoom> room =
        CPartyRoomRegistry::inst().FindByMember(CSPointer<User>(user, ObjectRefType::FunctionChain));
    if (!room) {
        CParty* const party = user->GetParty();
        if (party == nullptr || party->GetMaster() != user || party->IsLootingTypeModifyInProgress()) {
            return false;
        }
        if (CReplyChecker::IsUserWaitingReply(user)) {
            socket->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
            return false;
        }

        std::vector<User*> members;
        party->GetAllMember(members);
        int routingType = 0;
        ::Disassemble(packet, "d", &routingType);
        for (User* const member : members) {
            if (member != user) {
                CUserSocket* const memberSocket = member->GetSocket();
                const char16_t* const userName = user->GetName();
                memberSocket->Send("chSd", 0xfe, 0xc0, userName, routingType);
            }
        }
        const ItemRoutingType requestedRoutingType = static_cast<ItemRoutingType>(routingType);
        socket->SendSystemMessage_i(
            SystemMessageId::PARTY_LOOT_CHANGE_APPROVAL_REQUESTED, GetLootingTypeStrNum(requestedRoutingType)
        );
        party->StartModifyLootingType(requestedRoutingType);
        return false;
    }

    if (!room->IsPartyRoomMaster(CSPointer<User>(user, ObjectRefType::FunctionChain))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] [%d] Invalid user [PartyLootingModify]",
        //     L"UserPacketEx.cpp",
        //     0x1686)
        L2_LOG_WARN(
            Log, "Party loot change rejected: requester is not the room leader, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    CParty* const party = user->GetParty();
    if (party != nullptr && party->GetMaster() != user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] [%d] Invalid user [PartyLootingModify]",
        //     L"UserPacketEx.cpp",
        //     0x168f)
        L2_LOG_WARN(
            Log, "Party loot change rejected: requester is not the party leader, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    if (CReplyChecker::IsUserWaitingReply(user)) {
        socket->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
        return false;
    }

    int routingType = 0;
    ::Disassemble(packet, "d", &routingType);
    const ItemRoutingType requestedRoutingType = static_cast<ItemRoutingType>(routingType);
    if (party == nullptr) {
        CPartyRoom::RoomMemberList members;
        room->GetMemberList(members);
        for (const CSPointer<CRoomMember>& member : members) {
            if (room->IsPartyRoomMaster(member->GetUser())) {
                user->m_ItemRoutingType = requestedRoutingType;
                socket->Send("chdd", 0xfe, 0xc1, 1, routingType);
                socket->SendSystemMessage_i(
                    SystemMessageId::PARTY_LOOT_METHOD_CHANGED, GetLootingTypeStrNum(requestedRoutingType)
                );
            } else {
                {
                    const CSPointer<User> memberUser = member->GetUser();
                    CUserSocket* const memberSocket = memberUser->GetSocket();
                    memberSocket->Send("chdd", 0xfe, 0xc1, 2, routingType);
                }
                {
                    const CSPointer<User> memberUser = member->GetUser();
                    const int routingMessage = GetLootingTypeStrNum(requestedRoutingType);
                    CUserSocket* const memberSocket = memberUser->GetSocket();
                    memberSocket->SendSystemMessage_i(SystemMessageId::PARTY_LOOT_METHOD_CHANGED, routingMessage);
                }
            }
        }
        return false;
    }

    if (party->IsLootingTypeModifyInProgress()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] [%d] Modifying a looting type is already in progress "
        //     L"[PartyLootingModify]",
        //     L"UserPacketEx.cpp",
        //     0x16ba)
        L2_LOG_WARN(
            Log, "Party loot change rejected: another loot change is in progress, character ID=%u, requested loot type=%d",
            static_cast<unsigned>(user->GetDBID()), routingType
        );
        return false;
    }

    CPartyRoom::RoomMemberList members;
    room->GetMemberList(members);
    for (const CSPointer<CRoomMember>& member : members) {
        // Current releases this first member reference before comparing the
        // borrowed raw pointer with the requester.
        User* const memberUser = member->GetUser().get();
        if (memberUser == user) {
            socket->SendSystemMessage_i(
                SystemMessageId::PARTY_LOOT_CHANGE_APPROVAL_REQUESTED, GetLootingTypeStrNum(requestedRoutingType)
            );
            continue;
        }

        bool sameParty = false;
        {
            const CSPointer<User> memberReference = member->GetUser();
            sameParty = user->m_nPartySid != 0 && memberReference->m_nPartySid == user->m_nPartySid;
        }
        if (sameParty) {
            const CSPointer<User> memberReference = member->GetUser();
            CUserSocket* const memberSocket = memberReference->GetSocket();
            const char16_t* const userName = user->GetName();
            memberSocket->Send("chSd", 0xfe, 0xc0, userName, routingType);
        }
    }
    party->StartModifyLootingType(requestedRoutingType);
    return false;
}

bool PartyLootingModifyAgreement(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c06778, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CParty* const party = user->GetParty();
    if (party == nullptr || !party->IsLootingTypeModifyInProgress()) {
        return false;
    }

    int agreement = 0;
    ::Disassemble(packet, "d", &agreement);
    if (agreement == 0) {
        party->CompleteModifyLootingType();
        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* const member : members) {
            CUserSocket* const memberSocket = member->GetSocket();
            memberSocket->Send("chdd", 0xfe, 0xc1, 0, 0);
            CUserSocket* const messageSocket = member->GetSocket();
            messageSocket->SendSystemMessage(SystemMessageId::PARTY_LOOT_CHANGE_CANCELLED);
        }
        return false;
    }

    party->AcceptModifyLootingType(user);
    if (!party->CheckAcceptStatus()) {
        return false;
    }

    const CSPointer<CPartyRoom> room =
        CPartyRoomRegistry::inst().FindByMember(CSPointer<User>(user, ObjectRefType::FunctionChain));
    party->SetRoutingType(party->ReservedItemLootingType());
    if (!room) {
        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* const member : members) {
            member->m_ItemRoutingType = party->ReservedItemLootingType();

            CUserSocket* const memberSocket = member->GetSocket();
            const ItemRoutingType routingType = party->ReservedItemLootingType();
            memberSocket->Send("chdd", 0xfe, 0xc1, 1, routingType);

            const int routingMessage = GetLootingTypeStrNum(party->ReservedItemLootingType());
            CUserSocket* const messageSocket = member->GetSocket();
            messageSocket->SendSystemMessage_i(SystemMessageId::PARTY_LOOT_METHOD_CHANGED, routingMessage);
        }
    } else {
        CPartyRoom::RoomMemberList members;
        room->GetMemberList(members);
        for (const CSPointer<CRoomMember>& member : members) {
            bool isPartyMember = false;
            {
                const CSPointer<User> memberReference = member->GetUser();
                isPartyMember = party->IsMember(memberReference->GetServerId());
            }
            if (!isPartyMember) {
                {
                    const CSPointer<User> memberReference = member->GetUser();
                    CUserSocket* const memberSocket = memberReference->GetSocket();
                    const ItemRoutingType routingType = party->ReservedItemLootingType();
                    memberSocket->Send("chdd", 0xfe, 0xc1, 2, routingType);
                }
                {
                    const CSPointer<User> memberReference = member->GetUser();
                    const int routingMessage = GetLootingTypeStrNum(party->ReservedItemLootingType());
                    CUserSocket* const messageSocket = memberReference->GetSocket();
                    messageSocket->SendSystemMessage_i(SystemMessageId::PARTY_LOOT_METHOD_CHANGED, routingMessage);
                }
            } else {
                {
                    const CSPointer<User> memberReference = member->GetUser();
                    memberReference->m_ItemRoutingType = party->ReservedItemLootingType();
                }
                {
                    const CSPointer<User> memberReference = member->GetUser();
                    CUserSocket* const memberSocket = memberReference->GetSocket();
                    const ItemRoutingType routingType = party->ReservedItemLootingType();
                    memberSocket->Send("chdd", 0xfe, 0xc1, 1, routingType);
                }
                {
                    const CSPointer<User> memberReference = member->GetUser();
                    const int routingMessage = GetLootingTypeStrNum(party->ReservedItemLootingType());
                    CUserSocket* const messageSocket = memberReference->GetSocket();
                    messageSocket->SendSystemMessage_i(SystemMessageId::PARTY_LOOT_METHOD_CHANGED, routingMessage);
                }
            }
        }
    }
    party->CompleteModifyLootingType();
    return false;
}

bool FriendDetailInfo(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf37c4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> characterName{};
    ::Disassemble(packet, "S", 0x32, characterName.data());

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CSPointer<CFriends> friends = user->GetFriends();
    if (friends) {
        const unsigned int characterId = friends->GetFriendDbId(characterName.data());
        user->SendFriendDetailInfo(static_cast<int>(characterId), false);
    }
    return false;
}

bool FriendUpdateMemo(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be4404, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> characterName{};
    std::array<char16_t, 51> memo{};
    ::Disassemble(packet, "SS", 0x32, characterName.data(), 0x66, memo.data());

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->OnRequestUpdateFriendMemo(characterName.data(), memo.data());
    }
    return user == nullptr;
}

bool BlockUpdateMemo(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be452c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> characterName{};
    std::array<char16_t, 51> memo{};
    ::Disassemble(packet, "SS", 0x32, characterName.data(), 0x66, memo.data());

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->OnRequestUpdateBlockMemo(characterName.data(), memo.data());
    }
    return user == nullptr;
}

bool RequestBlockDetailInfo(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf2834, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> characterName{};
    ::Disassemble(packet, "S", 0x32, characterName.data());

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const int characterId = user->GetBlockCharDBID(characterName.data());
    if (characterId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] You don't exist My BlockList[%s]",
        //     L"UserPacketEx.cpp",
        //     0x3167,
        //     hostCharacterName.c_str())
        L2_LOG_WARN(
            Log, u"Block detail request rejected: character=%s is not in the block list, requester character ID=%u",
            characterName.data(), static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    const std::u16string memo = user->GetBlockCharInfo(characterId, false);
    userSocket->Send("chSS", 0xfe, 0xef, characterName.data(), memo.c_str());
    return false;
}

bool BlockUpsetEnter(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bea584, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int stage = 0;
    int team = 0;
    ::Disassemble(packet, "dd", &stage, &team);
    CBlockUpsetManager& manager = Singleton<CBlockUpsetManager>::inst();
    if (team == -1) {
        manager.RemoveBlockUpsetWaitingPlayer(stage, static_cast<int>(user->m_nId));
    } else {
        manager.AddBlockUpsetWaitingPlayer(stage, static_cast<BlueRedTeam::Enum>(team != 0), *user);
    }
    return false;
}

bool BlockUpsetVote(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bea680, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int stage = 0;
    int vote = 0;
    ::Disassemble(packet, "dd", &stage, &vote);
    Singleton<CBlockUpsetManager>::inst().VoteToStart(stage, static_cast<int>(user->m_nId), vote == 1);
    return false;
}

bool RequestAddMentee(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf6cec, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<char16_t, 28> targetName{};
    ::Disassemble(packet, "S", 0x32, targetName.data());
    std::array<SharedUtilsWideChar, 28> lowerName{};
    std::memset(lowerName.data(), 0, 0x32);
    ::ToLowerCase(reinterpret_cast<const SharedUtilsWideChar*>(targetName.data()), lowerName.data());

    const std::u16string hostLowerNameUtf16(reinterpret_cast<const char16_t*>(lowerName.data()));
    CSPointer<User> targetReference = GetUserFromNameMap(g_nWorldId, hostLowerNameUtf16);
    User* const target = targetReference.get();

    int reason = 0;
    if (!user->CanBeMentor(reason)) {
        if (reason < 1 || reason > 9) {
            reason = 1;
        }
        constexpr std::array<unsigned int, 10> messages{
            0,
            SystemMessageId::MENTOR_MENTEE_LIMIT,
            SystemMessageId::MUST_AWAKEN_TO_BECOME_MENTOR,
            SystemMessageId::MUST_AWAKEN_TO_BECOME_MENTOR,
            SystemMessageId::NEW_MENTEE_REUSE_TIME_REMAINING,
            SystemMessageId::TARGET_CANNOT_FORM_MORE_MENTORING_RELATIONSHIPS,
            0,
            0,
            SystemMessageId::PREMIUM_ACCOUNT_REQUIRED_TO_ADD_MENTEE,
            0,
        };
        if (reason == 4) {
            const int remaining = user->GetMentorPenaltyEndTimeLeft();
            const int days = remaining / 0x15180;
            const int hours = (remaining % 0x15180) / 0xe10;
            const int minutes = (remaining % 0xe10) / 0x3c;
            socket->Send(
                "chccccccc", 0x62, messages[static_cast<std::size_t>(reason)], 3, 0x14,
                static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(days))), 0x14,
                static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(hours))), 0x14,
                static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(minutes)))
            );
        } else {
            socket->SendSystemMessage(messages[static_cast<std::size_t>(reason)]);
        }
        return false;
    }

    if (user->IsBlockAll()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::BLOCK_ALL_ENABLED);
        return false;
    }
    if (user->m_data->m_TrainingState == 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }
    if (target == nullptr) {
        socket->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        return false;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(target->GetClass()))) {
        user->GetSocket()->SendSystemMessage_Char(
            SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_TO_NAMED_CHARACTER, target->GetName()
        );
        return false;
    }
    if (target->IsBlockAll()) {
        socket->SendSystemMessage_S(SystemMessageId::TARGET_BLOCKS_ALL, target->GetName());
        return false;
    }
    if (target->IsInBlockList(*user)) {
        socket->SendSystemMessage_S(SystemMessageId::CHARACTER_BLOCKED_YOU, target->GetName());
        return false;
    }
    if (target->m_data->m_TrainingState == 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SEND_REQUEST_TO_CHARACTER_IN_TRAINING_CAMP);
        return false;
    }

    reason = 0;
    if (!target->CanBeMentee(reason)) {
        constexpr std::array<unsigned int, 9> messages{
            0,
            SystemMessageId::TARGET_ALREADY_HAS_MENTOR,
            SystemMessageId::TARGET_LEVEL_TOO_HIGH_TO_BECOME_MENTEE,
            0,
            0,
            SystemMessageId::TARGET_CANNOT_FORM_MORE_MENTORING_RELATIONSHIPS,
            SystemMessageId::TARGET_LACKS_ITEM_REQUIRED_TO_BECOME_MENTEE,
            SystemMessageId::MENTEE_MUST_BE_ON_MAIN_CLASS,
            SystemMessageId::TARGET_CAN_NO_LONGER_BE_MENTEE,
        };
        const unsigned int message = (reason >= 0 && static_cast<std::size_t>(reason) < messages.size())
            ? messages[static_cast<std::size_t>(reason)]
            : 0;
        if (static_cast<int>(message) > 0) {
            socket->SendSystemMessage_S(message, targetName.data());
        }
        return false;
    }
    if (target->m_data->m_bGMInvisible) {
        return false;
    }
    if (user->m_nId == target->m_nId) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_BE_OWN_MENTEE);
        return false;
    }
    if (CReplyChecker::IsUserWaitingReply(user)) {
        socket->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
        return false;
    }
    if (!CReplyChecker::AttachReplyChecker(0x0d, user, target, 0x3c)) {
        socket->SendSystemMessage_S(SystemMessageId::INVITED_CHARACTER_BUSY, target->GetName());
        return false;
    }

    CUserSocket* const targetSocket = target->GetSocket();
    if (targetSocket != nullptr) {
        targetSocket->Send(
            "chSdd", 0xfe, 0x11c, user->GetName(), user->m_data->m_nSubJob0Class, user->m_data->m_MainClassLevel
        );
        socket->SendSystemMessage_S(SystemMessageId::MENTEE_OFFER_SENT, target->GetName());
    }
    return false;
}

bool AnswerAddMentee(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf7564, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int result = 0;
    std::array<char16_t, 32> targetName;
    ::Disassemble(packet, "dS", &result, 0x32, targetName.data());

    std::array<SharedUtilsWideChar, 28> lowerName;
    std::memset(lowerName.data(), 0, 0x32);
    ::ToLowerCase(reinterpret_cast<const SharedUtilsWideChar*>(targetName.data()), lowerName.data());

    User* target = nullptr;
    {
        const std::u16string hostLowerNameUtf16(reinterpret_cast<const char16_t*>(lowerName.data()));
        CSPointer<User> targetReference = GetUserFromNameMap(g_nWorldId, hostLowerNameUtf16);
        target = targetReference.get();
    }

    if (result == 0) {
        if (target != nullptr) {
            const char16_t* const requesterName = user->GetName();
            CUserSocket* const targetSocket = target->GetSocket();
            targetSocket->SendSystemMessage_S(SystemMessageId::MENTORING_OFFER_DECLINED, requesterName);
        }
    } else {
        if (target != nullptr) {
            target->AddMentee(user);
        }
        if (result < 1 && target != nullptr) {
            const char16_t* const requesterName = user->GetName();
            CUserSocket* const targetSocket = target->GetSocket();
            targetSocket->SendSystemMessage_S(SystemMessageId::MENTORING_OFFER_DECLINED, requesterName);
        }
    }

    CReplyChecker::FinishWaitingReplyByReplier(user);
    return false;
}

bool RequestCancelMentor(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bea770, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int type;
    const std::uint8_t* const cursor = ::Disassemble(packet, "d", &type);
    std::array<SharedUtilsWideChar, 28> lowerName;
    std::memset(lowerName.data(), 0, 0x32);
    std::array<char16_t, 32> targetName;
    int targetDatabaseId = 0;
    int endType;
    if (type == 0) {
        endType = 3;
        if (user->IsAlreadyHaveMentor()) {
#if defined(_WIN32)
            static_assert(sizeof(wchar_t) == sizeof(char16_t));
            const char16_t* const source = reinterpret_cast<const char16_t*>(user->m_Mentee.GetMentorName());
#else
            const std::u16string sourceStorage = ::WideToUtf16(std::wstring_view(user->m_Mentee.GetMentorName()));
            const char16_t* const source = sourceStorage.c_str();
#endif
            std::size_t length = 0;
            while (length < 25 && source[length] != u'\0') {
                ++length;
            }
            if (length >= 25) {
                targetName[0] = u'\0';
                runtime::process::HandleInvalidParameter(L"wcscpy_s");
            } else {
                std::copy_n(source, length + 1, targetName.data());
            }
            ::ToLowerCase(reinterpret_cast<const SharedUtilsWideChar*>(targetName.data()), lowerName.data());
        }
    } else {
        endType = 2;
        ::Disassemble(cursor, "S", 0x32, targetName.data());
        ::ToLowerCase(reinterpret_cast<const SharedUtilsWideChar*>(targetName.data()), lowerName.data());
        std::wstring hostLowerName = Utf16ToWide(lowerName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
        targetDatabaseId = user->GetMenteeDBId(hostLowerName.data());
    }
    user->EndMentoring(targetDatabaseId, endType);
    return false;
}

bool RequestVoteSystem(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be3690, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int targetServerId;
    ::Disassemble(packet, "d", &targetServerId);
    user->OnRequestVoteSystem(targetServerId);
    return false;
}

bool AnswerCoupleAction(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf0818, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x175d)
        L2_LOG_ERROR(Log, "Couple action reply ignored: socket has no character");
        return false;
    }

    user->m_bAskCoupleAction = false;
    int actionId;
    int answer;
    int targetServerId;
    ::Disassemble(packet, "ddd", &actionId, &answer, &targetServerId);

    const CSPointer<User> requester = CReplyChecker::GetRequesterFromReplier(user);
    if (requester) {
        if (targetServerId == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"CoupleActionPacket's TargetServerID is Zero")
            L2_LOG_WARN(
                Log, "Couple action reply has no target ID, action=%d, character ID=%u", actionId,
                static_cast<unsigned>(user->GetDBID())
            );
        } else {
            CCreature* const targetCreature = g_CreatureSmartIdManager.GetObjectW(targetServerId);
            User* const target = targetCreature != nullptr && targetCreature->IsPC() ? targetCreature->CastUser() : nullptr;
            if (target == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"CoupleActionPacket's TargetServerID InValid")
                L2_LOG_WARN(
                    Log,
                    "Couple action reply target is unavailable or is not a player, target ID=%d, action=%d, character ID=%u",
                    targetServerId, actionId, static_cast<unsigned>(user->GetDBID())
                );
            } else {
                target->m_bAskCoupleAction = false;
                if (answer == 1) {
                    const FVector userPosition = user->GetPos();
                    const FVector targetPosition = target->GetPos();
                    const double targetDirection =
                        std::atan2(targetPosition.y - userPosition.y, targetPosition.x - userPosition.x);
                    user->m_data->SetDir(
                        FAngle{runtime::Win64CvttDoubleToInt32(targetDirection * 65535.0 / 6.283185307179586)}
                    );
                    const double userDirection =
                        std::atan2(userPosition.y - targetPosition.y, userPosition.x - targetPosition.x);
                    target->m_data->SetDir(
                        FAngle{runtime::Win64CvttDoubleToInt32(userDirection * 65535.0 / 6.283185307179586)}
                    );

                    CUserSocket* const targetSocket = target->GetSocket();
                    const char16_t* const userName = user->GetName();
                    targetSocket->SendSystemMessage_S(SystemMessageId::CHARACTER_ACCEPTED_COUPLE_ACTION, userName);
                    user->OnCoupleActionPacket(actionId, targetServerId);
                } else if (answer == -1) {
                    CUserSocket* const targetSocket = target->GetSocket();
                    const char16_t* const userName = user->GetName();
                    targetSocket->SendSystemMessage_S(SystemMessageId::CHARACTER_REFUSES_COUPLE_ACTIONS, userName);
                } else {
                    CUserSocket* const targetSocket = target->GetSocket();
                    targetSocket->SendSystemMessage(SystemMessageId::COUPLE_ACTION_REFUSED);
                }
            }
        }
    }
    CReplyChecker::FinishWaitingReplyByReplier(user);
    return false;
}

bool AnswerMyMembership(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00c07084, original owner UserPacketEx.cpp.
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const CSPointer<CPartyRoom> room =
        CPartyRoomRegistry::inst().FindByMember(CSPointer<User>(user, ObjectRefType::FunctionChain));
    if (!room) {
        CParty* const party = user->GetParty();
        if (party == nullptr) {
            // Current format has a second d but supplies only this first one.
            // the trailing variadic lane remains stack residue.
            socket->Send("chdd", 0xfe, 0xc4, 0);
        } else if (party->GetMaster() == user) {
            socket->Send("chdd", 0xfe, 0xc4, 1);
        } else {
            socket->Send("chdd", 0xfe, 0xc4, 2);
        }
        return false;
    }

    CParty* const party = user->GetParty();
    if (party == nullptr) {
        if (room->IsPartyRoomMaster(CSPointer<User>(user, ObjectRefType::FunctionChain))) {
            socket->Send("chdd", 0xfe, 0xc4, 4);
        } else {
            socket->Send("chdd", 0xfe, 0xc4, 6);
        }
        return false;
    }

    if (party->GetMaster() == user && room->IsPartyRoomMaster(CSPointer<User>(user, ObjectRefType::FunctionChain))) {
        socket->Send("chdd", 0xfe, 0xc4, 3);
        return false;
    }

    // Current repeats this condition after the failed party-master branch.
    if (room->IsPartyRoomMaster(CSPointer<User>(user, ObjectRefType::FunctionChain))) {
        socket->Send("chdd", 0xfe, 0xc4, 4);
    } else {
        socket->Send("chdd", 0xfe, 0xc4, 5);
    }
    return false;
}

bool Request2ndPasswordCheck(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00be4848, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    const int accountId = runtime::Int32FromBits(socket->GetAccountID());
    if (accountId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try hack ? account[%d]",
        //     "UserPacketEx.cpp",
        //     0x1f36,
        //     accountId)
        L2_LOG_WARN(Log, "Secondary password status request rejected: socket has no account ID");
        return true;
    }

    if (!g_bCheckSecondaryAuth || socket->m_BuilderSetting.m_bNoSeondaryAuth == true) {
        socket->SetSecondaryAuthState(CUserSocket::SAS_NOT_NEED);
        socket->Send("chdd", 0xfe, 0x105, 2, 0);
    } else if (socket->GetSecondaryAuthState() == CUserSocket::SAS_NOT_NEED ||
        socket->GetSecondaryAuthState() == CUserSocket::SAS_COMPLETED) {
        socket->Send("chdd", 0xfe, 0x105, 2, 0);
    } else {
        auto* const emptyPassword = const_cast<char16_t*>(u"");
        authd.AuthRequest2ndPassword(4, accountId, 0, emptyPassword, emptyPassword);
    }
    return false;
}

bool Request2ndPasswordVerify(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be49cc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);

    std::array<char16_t, 9> password;
    ::Disassemble(packet, "S", 0x12, password.data());

    const int accountId = runtime::Int32FromBits(socket->GetAccountID());
    if (accountId != 0) {
        const int ipAddress = runtime::Int32FromBits(socket->GetIPAddressUINT());
        authd.AuthRequest2ndPassword(3, accountId, ipAddress, password.data(), const_cast<char16_t*>(u""));
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try hack ? account[%d]",
        //     "UserPacketEx.cpp",
        //     0x1f60,
        //     accountId)
        L2_LOG_WARN(Log, "Secondary password verification rejected: socket has no account ID");
    }
    return accountId == 0;
}

bool Request2ndPasswordReq(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be4af4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);

    char requestType = 0;
    std::array<char16_t, 9> password{};
    std::array<char16_t, 9> newPassword{};
    const std::uint8_t* const cursor = ::Disassemble(packet, "cS", &requestType, 0x12, password.data());
    if (requestType == 2) {
        ::Disassemble(cursor, "S", 0x12, newPassword.data());
    }

    const int accountId = runtime::Int32FromBits(socket->GetAccountID());
    if (accountId != 0) {
        const int ipAddress = runtime::Int32FromBits(socket->GetIPAddressUINT());
        authd.AuthRequest2ndPassword(requestType, accountId, ipAddress, password.data(), newPassword.data());
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try hack ? account[%d]",
        //     "UserPacketEx.cpp",
        //     0x1f84,
        //     accountId)
        L2_LOG_WARN(
            Log, "Secondary password operation rejected: socket has no account ID, request type=%d",
            static_cast<int>(requestType)
        );
    }
    return accountId == 0;
}

bool CheckCharName(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be4c58, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);

    std::array<char16_t, 25> characterName{};
    ::Disassemble(packet, "S", 0x32, characterName.data());

    if (!User::CheckCharacterName(characterName.data(), g_nCountryCode, false)) {
        socket->Send("chd", 0xfe, 0x10b, 4);
        return false;
    }
    if (g_nCountryCode == 8 && !User::CheckCharacterNameDual(characterName.data(), 8, false)) {
        socket->Send("chd", 0xfe, 0x10b, 7);
        return false;
    }

    db.RequestCheckCharName(
        runtime::Int32FromBits(socket->GetServerId()), runtime::Int32FromBits(socket->GetAccountID()),
        characterName.data()
    );
    return false;
}

bool QueueTicketLoginPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bfe044, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    if (!g_bQueueTicketEnabled) {
        return true;
    }

    std::int32_t value1;
    std::int32_t value2;
    std::int64_t macAddress = 0;
    std::uint32_t accountId = 0;
    std::int32_t value5;
    std::int32_t value6;
    std::uint32_t oneTimeKey = 0;
    std::int32_t value8;
    ::Disassemble(packet, "ddQddddd", &value1, &value2, &macAddress, &accountId, &value5, &value6, &oneTimeKey, &value8);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    if (socket == nullptr) {
        return true;
    }
    if (socket->m_nQueueTicketAccountID != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [SHANGHAI_QUEUETICKET] "
        //     L"Invalid QueueTicketAccountID %d",
        //     L"UserPacketEx.cpp",
        //     0x2ad3,
        //     runtime::Int32FromBits(
        //         socket->m_nQueueTicketAccountID))
        L2_LOG_WARN(
            Log, "Queue login rejected: socket already has queue account ID=%u, requested account ID=%u",
            static_cast<unsigned>(socket->m_nQueueTicketAccountID), static_cast<unsigned>(accountId)
        );
        return true;
    }

    socket->SetMacAddress(macAddress);
    if (g_nIPBlock != 0) {
        const ::in_addr address = socket->GetAddress();
        const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&address.s_addr);
        IPAddress ipAddress(bytes[0], bytes[1], bytes[2], bytes[3]);
        if (IPBlockManager::inst().IsBlockedIP(ipAddress)) {
            socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            return true;
        }
    }

    if (CBotBlockManager::inst().IsBotUser(accountId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"BotUser Try login. C_LOGIN. account[%d]",
        //     runtime::Int32FromBits(accountId))
        L2_LOG_WARN(
            Log, "Queue login rejected: account is on the bot block list, account ID=%u", static_cast<unsigned>(accountId)
        );
        return true;
    }

    stAccountAuthInfo authInfo;
    {
        L2_READ_GUARD(AccountAuthInfoLock);
        const auto account = g_AccountAuthInfoMap.find(accountId);
        if (account == g_AccountAuthInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] account not found... account_id(%d)",
            //     "UserPacketEx.cpp",
            //     0x2af5,
            //     runtime::Int32FromBits(accountId))
            L2_LOG_ERROR(
                Log, "Queue login rejected: account authorization entry is missing, account ID=%u",
                static_cast<unsigned>(accountId)
            );
            return true;
        }
        std::memcpy(&authInfo, &account->second, sizeof(authInfo));
    }

    const auto* const expectedAddress = reinterpret_cast<const std::uint8_t*>(&authInfo.client_addr.s_addr);
    bool addressMatches = false;
    const ::in_addr address1 = socket->GetAddress();
    const auto* const address1Bytes = reinterpret_cast<const std::uint8_t*>(&address1.s_addr);
    if (expectedAddress[0] == address1Bytes[0]) {
        const ::in_addr address2 = socket->GetAddress();
        const auto* const address2Bytes = reinterpret_cast<const std::uint8_t*>(&address2.s_addr);
        if (expectedAddress[1] == address2Bytes[1]) {
            const ::in_addr address3 = socket->GetAddress();
            const auto* const address3Bytes = reinterpret_cast<const std::uint8_t*>(&address3.s_addr);
            if (expectedAddress[2] == address3Bytes[2]) {
                const ::in_addr address4 = socket->GetAddress();
                const auto* const address4Bytes = reinterpret_cast<const std::uint8_t*>(&address4.s_addr);
                addressMatches = expectedAddress[3] == address4Bytes[3];
            }
        }
    }

    if (addressMatches) {
        const std::uint32_t storedOneTimeKey = [&] {
            L2_WRITE_GUARD(KeyLock);
            return g_OneTimeKeyMap[accountId];
        }();
        if (storedOneTimeKey == oneTimeKey) {
            socket->m_nQueueTicketAccountID = accountId;
            socket->Send("chc", 0xfe, 0x1b3, 1);
            return false;
        }
    } else {
        std::array<char16_t, 20> expectedAddressString{};
        FormatUtf16To(
            expectedAddressString.data(), 0x13, u"%d.%d.%d.%d", static_cast<int>(expectedAddress[0]),
            static_cast<int>(expectedAddress[1]), static_cast<int>(expectedAddress[2]), static_cast<int>(expectedAddress[3])
        );
        const std::u16string socketAddressString = socket->GetIPAddressString();
        Log.Add(
            LOG_BOTH, u"%I64d,,%I64d,,,,,,%s,%s,,,,,,,,,,,,,%s,,,", static_cast<std::int64_t>(0x36f),
            static_cast<std::int64_t>(runtime::Int32FromBits(accountId)), expectedAddressString.data(),
            socketAddressString.c_str(), u"QueueTicket"
        );
    }
    return true;
}

bool RequestQuitCardUpdownGame(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00bf3340, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    CSPointer<User> user(socket->GetUser(), ObjectRefType::FunctionChain);
    if (user) {
        user->QuitCardUpDownGame();
    }
    return !user;
}

bool ConnectToRaidServer(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf96fc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);

    int worldId = 0;
    int characterId = 0;
    std::uint32_t accountId = 0;
    ::Disassemble(packet, "ddd", &worldId, &characterId, &accountId);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]C_EX_CONNECT_TO_RAID_SERVER recv: %d, %d",
    //     L"UserPacketEx.cpp",
    //     0x15c4,
    //     worldId,
    //     characterId)
    L2_LOG_DEBUG(
        Log, "Raid server connection requested: world=%d, character ID=%d, account ID=%u", worldId, characterId,
        static_cast<unsigned>(accountId)
    );

    static_cast<void>(WOlympiadService::inst());
    if (PledgeRaidNetworkUtil::IsHost()) {
        if (HandleConnectToWOlympiad(socket, worldId, characterId, accountId)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Succeed to handle connect: %d, %d, %d",
            //     L"UserPacketEx.cpp",
            //     0x15ce,
            //     worldId,
            //     characterId,
            //     accountId)
            L2_LOG_INFO(
                Log, "World Olympiad connection request accepted: world=%d, character ID=%d, account ID=%u", worldId,
                characterId, static_cast<unsigned>(accountId)
            );
            return false;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Failed to handle connect: %d, %d, %d",
        //     L"UserPacketEx.cpp",
        //     0x15ca,
        //     worldId,
        //     characterId,
        //     accountId)
        L2_LOG_ERROR(
            Log, "World Olympiad connection request failed: world=%d, character ID=%d, account ID=%u", worldId, characterId,
            static_cast<unsigned>(accountId)
        );
        return true;
    }

    RaidReserverInfo* const reservation =
        RaidServerManager::inst().GetRaidCandidateInfo(RaidServerManager::RaidKey(worldId, characterId));
    if (reservation == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Not reserved char[%d:%d] request to connect to raid server",
        //     L"UserPacketEx.cpp",
        //     0x15d8,
        //     worldId,
        //     characterId)
        L2_LOG_WARN(
            Log, "Raid server connection rejected: character reservation is missing, world=%d, character ID=%d", worldId,
            characterId
        );
        return true;
    }

    socket->m_CountryCode = reservation->GetLanguageType();
    socket->m_iPremiumLevel = reservation->GetPremiumLevel();
    socket->SetDormantMarkingTime(reservation->GetDormantMarkingTime(), reservation->GetDormantType());

    std::optional<std::wstring> existingAccountNameStorage;
    const bool accountRegistered = [&] {
        L2_GUARD(accountMapSect);
        const auto existing = accountMap.find(accountId);
        if (existing != accountMap.end()) {
            CUserSocket* const existingSocket = existing->second;
            existingAccountNameStorage.emplace(
                Utf16ToWide(existingSocket->GetAccountName(), Utf16WidePolicy::PreserveInvalidOnWindows)
            );
            const std::wstring& existingAccountName = *existingAccountNameStorage;
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] try dual login. account [%s:%d] used[%d]",
            //     L"UserPacketEx.cpp",
            //     0x15ea,
            //     existingAccountName.c_str(),
            //     static_cast<int>(existingSocket->GetAccountID()),
            //     existingSocket->GetSocketUseTime())
            L2_LOG_WARN(
                Log,
                "Raid server connection rejected: account already has a socket, account=%s, account ID=%u, socket use "
                "time=%d",
                ::WideToUtf8(existingAccountName).c_str(), static_cast<unsigned>(existingSocket->GetAccountID()),
                existingSocket->GetSocketUseTime()
            );
            return false;
        }

        accountMap.emplace(accountId, socket);
        L2_ADD_REF(socket, ObjectRefType::AccountMap);
        socket->SetAccountID(accountId);
        return true;
    }();
    if (!accountRegistered) {
        return true;
    }

    socket->SetAccountID(static_cast<std::uint32_t>(reservation->GetAccountId()));
    socket->SetAccountName(reservation->GetAccountName());
    const stAccountAuthInfo accountAuthInfo = reservation->GetAuthInfo();
    socket->SetPCCafeCode(accountAuthInfo.sPCCafeCode);
    {
        L2_WRITE_GUARD(AccountAuthInfoLock);
        g_AccountAuthInfoMap[static_cast<std::uint32_t>(reservation->GetAccountId())] = accountAuthInfo;
    }

    const FVector destinationForZ = reservation->GetDestPos();
    const FVector destinationForY = reservation->GetDestPos();
    const FVector destinationForX = reservation->GetDestPos();
    db.LoadSelectedCharacter(
        worldId, characterId, socket, ALT_FORCE, runtime::Win64CvttDoubleToInt32(destinationForX.x),
        runtime::Win64CvttDoubleToInt32(destinationForY.y), runtime::Win64CvttDoubleToInt32(destinationForZ.z), true
    );
    socket->m_nCloseReason = SCR_NORMAL;
    return false;
}

bool ReturnFromRaid(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c08808, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr || user->m_data->m_nWorldId != g_nWorldId || user->m_RaidInfo.m_nRaidWorldId < 1) {
        return true;
    }

    int returnType = 0;
    ::Disassemble(packet, "d", &returnType);
    const bool returningFromHost = WOlympiadServiceConfig::inst().IsReturningFromHost(user->m_RaidInfo.m_nRaidWorldId);
    if (!returningFromHost) {
        if (returnType == 0) {
            socket->AddRaidStatus(URS_CLIENT_RETURN);
            RaidClientManager::inst().ReloadUserIfReturnedCompletely(*user);
        } else if (returnType == 1) {
            socket->AddRaidStatus(URS_CLIENT_RESTART);
            RaidClientManager::inst().RestartUserIfCompletely(*user);
        }
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]C_EX_RETURN_FROM_RAID recv: %d, %d",
    //     L"UserPacketEx.cpp",
    //     0x161a,
    //     static_cast<int>(user->GetDBID()),
    //     returnType)
    L2_LOG_DEBUG(
        Log, "Return from raid requested: character ID=%u, return type=%d", static_cast<unsigned>(user->GetDBID()),
        returnType
    );
    if (returnType == 0) {
        socket->AddRaidStatus(URS_CLIENT_RETURN);
        WOlympiadServiceProxy::inst().ReloadUserIfReturnedCompletely(CSPointer<User>(user, ObjectRefType::FunctionChain));
        return false;
    }
    if (returnType == 1) {
        socket->AddRaidStatus(URS_CLIENT_RESTART);
        WOlympiadServiceProxy::inst().RestartUserIfCompletely(CSPointer<User>(user, ObjectRefType::FunctionChain));
        return false;
    }
    if (returnType == 2) {
        // The current image keeps AL=1 on this branch.
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
    //     L"UserPacketEx.cpp",
    //     0x162e)
    L2_LOG_WARN(
        Log, "Return from raid request ignored: unsupported return type=%d (expected 0..2), character ID=%u", returnType,
        static_cast<unsigned>(user->GetDBID())
    );
    return false;
}

bool RequestRaidServerInfo(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00bf42f0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    protocol::S_EX_RAID_SERVER_INFO response{};
    if (!g_bWorldMapTestEnable) {
        for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
            CResidence* const residence = entry.second;
            if (!residence->IsCastle()) {
                continue;
            }

            const std::int32_t nextSiege = residence->m_data != nullptr ? residence->m_data->m_NextSiege : 0;
            const std::int32_t now = runtime::crt::Time32Now();
            const std::tm* const local = runtime::crt::LocalTime32(now);
            const std::int32_t remaining = runtime::SubtractInt32Wrapping(nextSiege, now);
            if (local->tm_wday == 6 && remaining < 90000 && local->tm_hour > 18 && local->tm_hour < 22 &&
                local->tm_min >= 0) {
                response.cDimensionRaid = 1;
            } else if (local->tm_wday == 0 && remaining > 7200 && local->tm_hour > 19 && local->tm_hour < 22 &&
                local->tm_min >= 0) {
                response.cDimensionCastleSiege = 1;
            }
            break;
        }
    } else {
        response.cDimensionRaid = static_cast<char>(g_bDimensionRaidEnable);
        response.cDimensionCastleSiege = static_cast<char>(g_bDimensionCastleSiegeEnable);
    }

    user->GetSocket()->Send(S_EX, S_EX_RAID_SERVER_INFO, response);
    return false;
}

bool SurrenderPledgeWar(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be42f8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<char16_t, 28> pledgeName;
    ::Disassemble(packet, "S", 0x32, pledgeName.data());
    std::wstring hostPledgeName = Utf16ToWide(pledgeName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    if (user->CanStopPledgeWar(hostPledgeName.data())) {
        if (user->GetPledge() != nullptr) {
            user->GetPledge()->EndTransaction();
        }
    }
    return false;
}

bool JoinPledgeByNamePacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf7704, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<char16_t, 25> targetName{};
    PledgeType pledgeType = MASTER_PLEDGE;
    ::Disassemble(packet, "Sd", 0x32, targetName.data(), &pledgeType);
    if (pledgeType != MASTER_PLEDGE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserPacketEx.cpp",
        //     0x2201)
        L2_LOG_WARN(
            Log, "Clan join by name rejected: unsupported clan unit type=%d, expected=%d, character ID=%u",
            static_cast<int>(pledgeType), static_cast<int>(MASTER_PLEDGE), static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    const std::u16string hostTargetNameUtf16(targetName.data());
    CSPointer<User> target = GetUserFromNameMap(g_nWorldId, hostTargetNameUtf16);
    if (!target) {
        socket->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        socket->Send("cd", 0x2d, 0);
        return false;
    }
    return socket->DoJoinPledge(user, target.get(), pledgeType);
}

bool Request_C_EX_PLEDGE_SIGNIN(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf1ba8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const rawUser = socket->GetUser();
    if (rawUser == nullptr) {
        return true;
    }

    std::uint32_t pledgeServerId = 0;
    ::Disassemble(packet, "d", &pledgeServerId);
    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);
    CSPointer<CPledge> pledge(CPledge::GetObjectW(pledgeServerId), ObjectRefType::FunctionChain);
    if (!pledge) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_JOIN_DISSOLVED_CLAN);
        return false;
    }

    if (pledge->m_nRecruitType != 1) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::ATTEMPTING_TO_JOIN_CLAN);
        pledge->SetPledgeWorkDone();
        return false;
    }

    const PledgeType pledgeType = pledge->m_recruitPledgeType;
    const CheckPledgeRecruitingResult::Value recruitingResult = CheckPledgeRecruiting(pledge, pledgeType);
    if (recruitingResult == CheckPledgeRecruitingResult::Success) {
        const CheckPledgeRecruiteeResult::Value recruiteeResult = CheckPledgeRecruitee(user, pledgeType);
        switch (recruiteeResult) {
            case CheckPledgeRecruiteeResult::Success: {
                int transaction = 0;
                if (pledge->BeginTransaction(&transaction)) {
                    db.RequestSigninPledge(user, pledge, pledgeType);
                    return false;
                }
                socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
                break;
            }
            case CheckPledgeRecruiteeResult::InTraining:
                socket->SendSystemMessage(SystemMessageId::CANNOT_JOIN_CLAN_WHILE_IN_TRAINING_CAMP);
                break;
            case CheckPledgeRecruiteeResult::AlreadyMember:
                socket->SendSystemMessage(SystemMessageId::ALREADY_IN_CLAN);
                break;
            case CheckPledgeRecruiteeResult::TimePenalty:
                if (BP::_PLEDGE_WITHDRAW_PENALTY_TIMEOUT < 0x15180) {
                    socket->Send(
                        "chccd", 0x62, SystemMessageId::CANNOT_JOIN_CLAN_BEFORE_WITHDRAWAL_DELAY_EXPIRES, 1, 1,
                        BP::_PLEDGE_WITHDRAW_PENALTY_TIMEOUT / 60
                    );
                } else {
                    socket->SendSystemMessage(SystemMessageId::CANNOT_JOIN_CLAN_DURING_WITHDRAWAL_RESTRICTION);
                }
                break;
            case CheckPledgeRecruiteeResult::FreeAccountInTaiwan:
                socket->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
                break;
            case CheckPledgeRecruiteeResult::AlreadyGraduatedAcademy:
                socket->SendSystemMessage(SystemMessageId::CANNOT_REJOIN_CLAN_ACADEMY_ALREADY_GRADUATED);
                break;
            case CheckPledgeRecruiteeResult::CannotJoinAcademy:
                socket->SendSystemMessage(SystemMessageId::CLAN_ACADEMY_MEMBERSHIP_REQUIREMENTS);
                break;
            default:
                // Log.Add(
                //     LOG_ERROR,
                //     L"Failed to check to join pledge : %d",
                //     static_cast<int>(recruiteeResult))
                L2_LOG_ERROR(
                    Log, "Clan sign-in failed: unhandled applicant check result=%d, clan server ID=%d, character ID=%u",
                    static_cast<int>(recruiteeResult), pledgeServerId, static_cast<unsigned>(user->GetDBID())
                );
                socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
                break;
        }
    } else {
        switch (recruitingResult) {
            case CheckPledgeRecruitingResult::WaitingToDismiss:
                socket->SendSystemMessage_S(
                    SystemMessageId::CANNOT_JOIN_CLAN_DISSOLUTION_REQUESTED, std::u16string(pledge->GetPledgeName()).c_str()
                );
                socket->Send("cd", 0x2d, 0);
                break;
            case CheckPledgeRecruitingResult::CannotDoPledgeWork:
                socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
                socket->Send("cd", 0x2d, 0);
                break;
            case CheckPledgeRecruitingResult::PledgeIsFull:
                socket->SendSystemMessage(SystemMessageId::CANNOT_JOIN_CLAN_FULL);
                break;
            case CheckPledgeRecruitingResult::TimePenalty:
                socket->SendSystemMessage_S(
                    SystemMessageId::TARGET_CLAN_CANNOT_RECRUIT_DURING_EXPULSION_RESTRICTION,
                    std::u16string(pledge->GetPledgeName()).c_str()
                );
                break;
            default:
                // Log.Add(
                //     LOG_ERROR,
                //     L"Failed to check pledge recruiting : %d",
                //     static_cast<int>(recruitingResult))
                L2_LOG_ERROR(
                    Log, "Clan sign-in failed: unhandled recruiting check result=%d, clan server ID=%d, character ID=%u",
                    static_cast<int>(recruitingResult), pledgeServerId, static_cast<unsigned>(user->GetDBID())
                );
                socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
                break;
        }
    }

    pledge->SetPledgeWorkDone();
    return false;
}

bool CallToChangeClass(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be4654, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t request = 0;
    ::Disassemble(packet, "d", &request);
    if (!g_bDualBillingSystem || request != 1) {
        g_npcserver.Send("cd", 0xb0, static_cast<int>(user->Id()));
    } else {
        static_cast<void>(user->Restart(RESTART_VILLAGE, 0));
        user->SendUSMEventPacket(0x0d);
    }
    return false;
}

bool ChangeToAwakenedClass(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c03a4c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t request = 0;
    ::Disassemble(packet, "d", &request);

    const int awakenClassId = user->m_nAwakenClassID;
    const int initialClassType = GetInitialClassType(awakenClassId, user->m_data->m_Race);
    if (initialClassType == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid class[%d] at file[%s], line[%d]",
        //     awakenClassId,
        //     L"UserPacketEx.cpp",
        //     0x1edc)
        L2_LOG_ERROR(
            Log, "Awakening class change rejected: initial class lookup failed, class=%d, race=%d, character ID=%u",
            awakenClassId, static_cast<int>(user->m_data->m_Race), static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    if ((awakenClassId >= 0x8b && awakenClassId <= 0x92) || awakenClassId == 0xbe || awakenClassId == 0xbf) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Change Class!! (To Awakened Class [%d])",
        //     L"UserPacketEx.cpp",
        //     0x1ee4,
        //     awakenClassId)
        L2_LOG_WARN(
            Log, "Awakening class change rejected: target class=%d is disallowed by this handler, character ID=%u",
            awakenClassId, static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    if (!user->CanChangeToAwakenClass(initialClassType)) {
        return false;
    }

    if (user->m_data->m_nSubJobId == 0) {
        CSPointer<User> retainedUser(user, ObjectRefType::FunctionChain);
        if (WOlympiadStuff::IsAllowedUser(retainedUser) && !WOlympiadServiceProxy::inst().IsPossibleToSetHero()) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::HERO_SELECTION_IN_PROGRESS);
            return false;
        }
        // if (user->m_data->m_nHeroType != 0) {
        if (user->m_data->m_nHeroType != HERO_NONE) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_AWAKEN_WHILE_HERO_OR_HERO_PENDING);
            return false;
        }
    }

    if (TalkChannelCache::inst().mChannels[15].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        g_npcserver.Send("cdddd", 0xb1, request, static_cast<int>(user->Id()), user->m_nAwakenNpcID, awakenClassId);
    }
    return false;
}

bool RequestAcquirePotentailSkill(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be76b8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const bool acquired = runtime::raw_atomic::CompareExchangeI32(&user->m_nChangeSubjobFlag, 1, 0) == 0;
    if (acquired) {
        if (!user->CheckAbilityCond()) {
            runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
        } else if (!user->OnRequestAbilitySkillPacket(packet)) {
            user->SendAbilitySkillList(0);
            runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
        }
    }
    return false;
}

bool RequestPotentialSkillList(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00be77bc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    user->SendAbilitySkillList(1);
    return false;
}

bool RequestResetPotentialSkill(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be787c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const bool acquired = runtime::raw_atomic::CompareExchangeI32(&user->m_nChangeSubjobFlag, 1, 0) == 0;
    if (acquired) {
        if (!user->CheckAbilityCond()) {
            runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
        } else if (!user->OnRequestResetAbilitySkillPacket(packet)) {
            user->SendAbilitySkillList(0);
            runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
        }
    }
    return false;
}

bool RequestAlchemySkillList(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00be89f0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_Race == RT_ERTHEIA) {
        return user->SendAlchemySkillList();
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"none ertheia race request alchemy skill list. race:[%d]",
    //     static_cast<int>(user->m_data->m_Race))
    L2_LOG_WARN(
        Log, "Alchemy skill list request rejected: character is not Ertheia, race=%d, character ID=%u",
        static_cast<int>(user->m_data->m_Race), static_cast<unsigned>(user->GetDBID())
    );
    return false;
}

bool TryMixCube(CSocket* const baseSocket, const std::uint8_t* const source) {
    // Current L2Server 0x00bf9bfc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    protocol::C_AlchemyTryMixCube request{};
    network::Stream stream(const_cast<std::uint8_t*>(source), 0x4000);
    if (request.Decode(&stream)) {
        user->TryRollyCube(request.itemInfoList);
    }
    return false;
}

bool RequestAlchemyConversion(CSocket* const baseSocket, const std::uint8_t* const source) {
    // Current L2Server 0x00bf9d58, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    protocol::C_AlchemyConversion request{};
    network::Stream stream(const_cast<std::uint8_t*>(source), 0x4000);
    if (request.Decode(&stream)) {
        user->TryAlchemyConversion(request.info.nSkillId, request.info.nLev, request.recipeList, request.nTryCount);
    }
    return false;
}

bool RequestTryEnsoul(CSocket* const baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c044d8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int targetItemSid = 0;
    char count = 0;
    packet = ::Disassemble(packet, "dc", &targetItemSid, &count);

    std::vector<EnsoulTryData> ensoulTryData;
    for (int index = 0; index < static_cast<int>(count); ++index) {
        char slotType = 0;
        char slotIndex = 0;
        int stoneItemSid = 0;
        int optionId = 0;
        packet = ::Disassemble(packet, "ccdd", &slotType, &slotIndex, &stoneItemSid, &optionId);
        ensoulTryData.emplace_back(
            static_cast<EnsoulItem::EnsoulItemSlotType>(static_cast<int>(slotType)), static_cast<int>(slotIndex),
            stoneItemSid, optionId
        );
    }

    if (!TalkChannelCache::inst().mChannels[20].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request ensoul with invalid talk channel.",
        //     L"UserPacketEx.cpp",
        //     0x2d29,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Soul crystal application rejected: talk channel validation failed, character=%s, item ID=%d",
        //     ::WideToUtf8(user->GetName()).c_str(), targetItemSid)
        L2_LOG_WARN(
            Log, "Soul crystal application rejected: talk channel validation failed, character=%s, item ID=%d",
            ::Utf16ToUtf8(user->GetName()).c_str(), targetItemSid
        );
        user->SendSystemMessage(SystemMessageId::SOUL_CRYSTAL_APPLICATION_FAILED_INVALID_PROCEDURE);
        user->GetSocket()->Send("chc", 0xfe, 0x181, 0);
    } else if (!user->OnEnsoulTry(targetItemSid, ensoulTryData)) {
        user->GetSocket()->Send("chc", 0xfe, 0x181, 0);
    }
    return false;
}

bool RequestEnsoulExtraction(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c0470c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int targetItemSid = 0;
    char slotType = 0;
    char slotIndex = 0;
    ::Disassemble(packet, "dcc", &targetItemSid, &slotType, &slotIndex);

    if (!TalkChannelCache::inst().mChannels[20].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request ensoul extraction with invalid talk channel.",
        //     L"UserPacketEx.cpp",
        //     0x3185,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Soul crystal extraction rejected: talk channel validation failed, character=%s, item ID=%d",
        //     ::WideToUtf8(user->GetName()).c_str(), targetItemSid)
        L2_LOG_WARN(
            Log, "Soul crystal extraction rejected: talk channel validation failed, character=%s, item ID=%d",
            ::Utf16ToUtf8(user->GetName()).c_str(), targetItemSid
        );
        user->SendSystemMessage(SystemMessageId::SOUL_CRYSTAL_APPLICATION_FAILED_INVALID_PROCEDURE);
        user->GetSocket()->Send("chc", 0xfe, 0x1b5, 0);
    } else if (!user->OnEnsoulExtractionTry(
                   targetItemSid, static_cast<EnsoulItem::EnsoulItemSlotType>(static_cast<int>(slotType)),
                   static_cast<int>(slotIndex)
               )) {
        user->GetSocket()->Send("chc", 0xfe, 0x1b5, 0);
    }
    return false;
}

bool RequestCreatePledge(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c04c80, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (!TalkChannelCache::inst().mChannels[24].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        return false;
    }

    CCreature* const target = g_CreatureSmartIdManager.GetObjectW(user->m_spTalkTarget.Sid());
    CNPC* const npc = target != nullptr && target->IsNPC() ? target->CastNPC() : nullptr;
    if (npc != nullptr) {
        std::array<char16_t, 25> pledgeName{};
        ::Disassemble(packet, "S", 0x32, pledgeName.data());
        CPledge::CreatePledgeByNpc(npc, user, pledgeName.data());
    }
    return false;
}

bool DispelPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be28a0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int targetServerId = 0;
    int skillId = 0;
    std::uint32_t packedSkillLevel = 0;
    ::Disassemble(packet, "ddd", &targetServerId, &skillId, &packedSkillLevel);
    user->DispelBuff(
        targetServerId, skillId, static_cast<int>(packedSkillLevel & 0xffffU),
        static_cast<std::int32_t>(packedSkillLevel) >> 16
    );
    return false;
}

bool EndScenePlayer(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bef108, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int sceneId = 0;
    ::Disassemble(packet, "d", &sceneId);
    if (user->m_SceneNpcId != -1) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(user->m_SceneNpcId));
        CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
        if (npc != nullptr) {
            g_npcserver.Send("cddd", 0xad, user->m_SceneNpcId, user->Id(), sceneId);
        }
    }
    user->StopScenePlay(sceneId);
    return false;
}

bool StopScenePlayer(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00bf63b0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const int sceneId = user->m_SceneId;
    if (user->IsMPCCMember()) {
        CMultiPartyCommandChannel* const channel = user->GetMPCC();
        if (channel != nullptr) {
            std::vector<User*> members;
            channel->GetAllMember(members);
            for (User* const member : members) {
                if (member == nullptr || member->m_SceneId != sceneId) {
                    continue;
                }
                if (member->m_SceneNpcId != -1) {
                    CCreature* const creature =
                        g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(member->m_SceneNpcId));
                    CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
                    if (npc != nullptr) {
                        g_npcserver.Send("cddd", 0xad, member->m_SceneNpcId, member->Id(), sceneId);
                    }
                }
                member->StopScenePlay(sceneId);
                member->GetSocket()->Send("chd", 0xfe, 0xe7, sceneId);
            }
        }
    } else if (user->m_data->m_nPartyId == 0) {
        if (user->m_SceneNpcId != -1) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(user->m_SceneNpcId));
            CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
            if (npc != nullptr) {
                g_npcserver.Send("cddd", 0xad, user->m_SceneNpcId, user->Id(), sceneId);
            }
        }
        user->StopScenePlay(sceneId);
        user->GetSocket()->Send("chd", 0xfe, 0xe7, sceneId);
    } else {
        CParty* const party = user->GetParty();
        if (party != nullptr) {
            std::vector<User*> members;
            party->GetAllMember(members);
            for (User* const member : members) {
                if (member == nullptr || member->m_SceneId != sceneId) {
                    continue;
                }
                if (member->m_SceneNpcId != -1) {
                    CCreature* const creature =
                        g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(member->m_SceneNpcId));
                    CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
                    if (npc != nullptr) {
                        g_npcserver.Send("cddd", 0xad, member->m_SceneNpcId, member->Id(), sceneId);
                    }
                }
                member->StopScenePlay(sceneId);
                member->GetSocket()->Send("chd", 0xfe, 0xe7, sceneId);
            }
        }
    }
    return false;
}

bool NotifyPlayStart(CSocket*, const unsigned char*) {
    // Current L2Server 0x00be505c is guard + false.
    L2_GUARD_FUNCTION();
    return false;
}

bool UserInterfaceInfoPacket(CSocket*, const unsigned char*) {
    // Current L2Server 0x00be56dc is guard + false.
    L2_GUARD_FUNCTION();
    return false;
}

bool ExecutedUIEventsCount(CSocket*, const unsigned char*) {
    // Current L2Server 0x00be8adc is guard + false.
    L2_GUARD_FUNCTION();
    return false;
}

bool CancelEnchantItem(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be29a4.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->SetPendingUseETCItem(nullptr, PUIT_ENCHANT);
        user->SetPendingUseETCItem(nullptr, PUIT_INC_ENCHANT_PROP);
        user->InitAllEnchantBonusOptions();
        socket->Send(
            "cddQdddd", 0x87,
            // 2,
            IER_CANCELED, 0, std::int64_t{0}, 0, 0, 0, 0
        );
    }
    return user == nullptr;
}

bool RefundListReq(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be32dc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    if (::g_nCountryCode == 1) {
        return true;
    }

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x154d)
        L2_LOG_ERROR(Log, "Refund list request rejected: socket has no character");
        return true;
    }
    return user->OnRefundPacket(packet);
}

bool RequestDivideAdenaStart(CSocket* baseSocket, const unsigned char*) {
    // Current L2Server 0x00be71d4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CMultiPartyCommandChannel* const channel = user->GetMPCC();
    CParty* const party = user->GetParty();
    if (channel == nullptr) {
        if (party == nullptr) {
            user->SendSystemMessage(SystemMessageId::REQUIRES_COMMAND_CHANNEL_OR_PARTY);
            return false;
        }
        if (party->m_data->m_OnlineMaster.id != static_cast<std::uint32_t>(user->m_data->m_nId)) {
            user->SendSystemMessage(SystemMessageId::REQUIRES_PARTY_LEADER);
            return false;
        }
    } else {
        if (channel->IsMaster(static_cast<int>(user->m_nId))) {
            channel->DivideAdenaStart();
            return false;
        }
        if (party->m_data->m_OnlineMaster.id != static_cast<std::uint32_t>(user->m_data->m_nId)) {
            user->SendSystemMessage(SystemMessageId::REQUIRES_COMMAND_CHANNEL_OR_PARTY_LEADER);
            return false;
        }
    }
    party->DivideAdenaStart();
    return false;
}

bool RequestDivideAdenaCancel(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be7338, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    DivideType::Enum divideType = DivideType::DIT_MAX;
    ::Disassemble(packet, "d", &divideType);

    CMultiPartyCommandChannel* const channel = user->GetMPCC();
    CParty* party = user->GetParty();
    if (channel == nullptr) {
        // Preserve the current extra second GetParty call.
        party = user->GetParty();
        if (party == nullptr) {
            user->SendSystemMessage(SystemMessageId::REQUIRES_COMMAND_CHANNEL_OR_PARTY);
            return false;
        }
        if (party->m_data->m_OnlineMaster.id != static_cast<std::uint32_t>(user->m_data->m_nId)) {
            user->SendSystemMessage(SystemMessageId::REQUIRES_PARTY_LEADER);
            return false;
        }
    } else {
        if (channel->IsMaster(static_cast<int>(user->m_nId))) {
            channel->DivideAdenaCancel(divideType);
            return false;
        }
        if (party->m_data->m_OnlineMaster.id != static_cast<std::uint32_t>(user->m_data->m_nId)) {
            user->SendSystemMessage(SystemMessageId::REQUIRES_COMMAND_CHANNEL_OR_PARTY_LEADER);
            return false;
        }
    }
    party->DivideAdenaCancel(divideType);
    return false;
}

bool RequestDivideAdena(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be74d8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int adenaItemSid = 0;
    std::int64_t totalAmount = 0;
    ::Disassemble(packet, "dQ", &adenaItemSid, &totalAmount);

    CItem* const adena = user->m_inv.GetFirstItemByClassID(0x39, false, false);
    if (adena == nullptr || adena->m_data->m_nCount < totalAmount) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_NOT_ENOUGH_ADENA);
        return false;
    }
    if (static_cast<int>(adena->Id()) != adenaItemSid) {
        socket->SendSystemMessage(SystemMessageId::ONLY_ADENA_CAN_BE_DISTRIBUTED);
        return false;
    }

    CMultiPartyCommandChannel* const channel = user->GetMPCC();
    CParty* const party = user->GetParty();
    if (channel == nullptr) {
        if (party == nullptr) {
            user->SendSystemMessage(SystemMessageId::REQUIRES_COMMAND_CHANNEL_OR_PARTY);
            return false;
        }
        if (party->m_data->m_OnlineMaster.id != static_cast<std::uint32_t>(user->m_data->m_nId)) {
            user->SendSystemMessage(SystemMessageId::REQUIRES_PARTY_LEADER);
            return false;
        }
    } else {
        if (channel->IsMaster(static_cast<int>(user->m_nId))) {
            channel->DivideAdena(totalAmount);
            return false;
        }
        if (party->m_data->m_OnlineMaster.id != static_cast<std::uint32_t>(user->m_data->m_nId)) {
            user->SendSystemMessage(SystemMessageId::REQUIRES_COMMAND_CHANNEL_OR_PARTY_LEADER);
            return false;
        }
    }
    party->DivideAdena(totalAmount);
    return false;
}

bool BuySellUICloseReq(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be33d4.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        user->m_bOpenedNpcStoreUI = false;
        user->SendItemList(false, true, nullptr);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x155e)
        L2_LOG_ERROR(Log, "NPC store close request rejected: socket has no character");
    }
    return user == nullptr;
}

bool AddPostFriend(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be3864.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<char16_t, 25> targetName{};
    ::Disassemble(packet, "S", 0x32, targetName.data());
    if (std::u16string_view(targetName.data()) == std::u16string_view(user->GetName())) {
        socket->Send("chSd", 0xfe, 0xd3, targetName.data(), -2);
        return false;
    }

    const int result = user->m_pPostInfo->GetPostFriend().CheckAddable(targetName.data());
    if (result == 1) {
        db.RequestAddPostFriend(*user, targetName.data());
    } else {
        socket->Send("chSd", 0xfe, 0xd3, targetName.data(), result);
    }
    return false;
}

bool DeletePostFriend(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be3a0c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<char16_t, 25> targetName{};
    ::Disassemble(packet, "S", 0x32, targetName.data());
    CPostFriend& postFriend = user->m_pPostInfo->GetPostFriend();
    if (postFriend.CheckNameExist(targetName.data())) {
        db.RequestDeletePostFriend(*user, targetName.data());
        postFriend.DeletePostFriend(targetName.data());
        user->GetSocket()->Send(
            "chccS", 0x62, SystemMessageId::MAIL_RECIPIENT_REMOVED_FROM_CONTACT_LIST, 1, 0x0c, targetName.data()
        );
    }
    return false;
}

bool RequestMentorList(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be59fc.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_nWorldId == g_nWorldId) {
        user->SendMentorList();
    } else {
        socket->SendSystemMessage(SystemMessageId::FUNCTION_UNAVAILABLE_AFTER_DIMENSIONAL_TRANSFER);
    }
    return false;
}

bool MenteeWaitingListPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be5bbc.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::int32_t page = 0;
        std::int32_t levelMin = 0;
        std::int32_t levelMax = 0;
        ::Disassemble(packet, "ddd", &page, &levelMin, &levelMax);
        CMenteeWaitingListFilter filter(user, page, levelMin, levelMax);
        g_MenteeWaitingList.SendWaitingList(filter);
    }
    return user == nullptr;
}

bool InzoneWaitngTime(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be5ae0 (original spelling kept).
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        char sendPacket = 1;
        ::Disassemble(packet, "c", &sendPacket);
        user->InstantZoneWaitingTime(sendPacket != 0);
    }
    return user == nullptr;
}

bool CancelShapeShifting(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be5f10.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
        user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
        socket->Send(
            "chdddd", 0xfe, 300,
            // 2,
            ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
        );
    }
    return user == nullptr;
}

bool RequestJoinPledgeGame(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be60f8.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        g_PledgeGameSystem.RegisterEntry(user);
    }
    return user == nullptr;
}

bool RequestCancelJoinPledgeGame(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be61bc.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    CPledgeGameField* const field = user->m_PledgeGameUser.GetField();
    if (field == nullptr || !field->m_bIsUnRegist) {
        g_PledgeGameSystem.LeaveGame(user, false);
    }
    return false;
}

bool RequestLeavePledgeGame(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be629c.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    CPledgeGameField* const field = user->m_PledgeGameUser.GetField();
    if (field == nullptr || !field->m_bIsUnRegist) {
        g_PledgeGameSystem.LeaveGame(user, false);
    }
    return false;
}

bool RequestObserveListPledgeGame(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be637c.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        g_PledgeGameSystem.RequestObserverList(user);
    }
    return user == nullptr;
}

bool RequestObservePledgeGame(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be6440.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::int32_t fieldId = 0;
        ::Disassemble(packet, "d", &fieldId);
        g_PledgeGameSystem.RegisterObserverEntry(user, fieldId);
    }
    return user == nullptr;
}

bool RequestExitObservePledgeGame(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be6524.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        g_PledgeGameSystem.RegisterExitObserverEntry(user);
    }
    return user == nullptr;
}

bool RequestPledgeGameHTML(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be65e8.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        g_PledgeGameSystem.PledgeGameHTML(user);
    }
    return user == nullptr;
}

bool RequestPledgeGameRecord(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be66ac.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        g_PledgeGameSystem.SendRecordToUser(user);
    }
    return user == nullptr;
}

bool RequestPledgeRecruitInfo(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00bf137c.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }

    std::uint32_t pledgeServerId = 0;
    ::Disassemble(packet, "d", &pledgeServerId);
    CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
    if (pledge != nullptr) {
        CPacketHelperEx<20480> subPledgeNames;
        const int subPledgeCount = pledge->GatherAllSubPledgeName(subPledgeNames);
        const std::u16string pledgeName = std::u16string(pledge->GetPledgeName());
        const std::u16string masterName = std::u16string(pledge->GetMasterName());
        socket->Send(
            "chSSdddb", 0xfe, 0x13f, pledgeName.c_str(), masterName.c_str(), pledge->GetPledgeLevel(),
            pledge->GetMemberCount(), subPledgeCount, subPledgeNames.GetLength(), subPledgeNames.GetPacket()
        );
    }
    return false;
}

bool RequestSearchPledgeRecruitBoard(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00bea8e8.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int minimumLevel = 0;
    int maximumLevel = 0;
    int nameTarget = 0;
    std::array<char16_t, 25> name{};
    int pledgeType = 0;
    int sortCondition = 0;
    int orderCondition = 0;
    int searchCondition = 0;
    ::Disassemble(
        packet, "dddSdddd", &minimumLevel, &maximumLevel, &nameTarget, 0x32, name.data(), &pledgeType, &sortCondition,
        &orderCondition, &searchCondition
    );
    if (searchCondition != 0 && searchCondition != 1) {
        searchCondition = -1;
    }
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }
    if (user->m_PledgeSearchDelay == 1) {
        // Log.Add(
        //     LOG_DEBUG,
        //     L"Can't response RequestSearchPledgeRecruitBoard click too fast")
        L2_LOG_DEBUG(
            Log, "Clan recruitment search ignored: search delay flag is set, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    runtime::raw_atomic::ExchangeI32(&user->m_PledgeSearchDelay, 1);
    user->m_nPledgeJoinSearchTickCount = 0;

    const char16_t* pledgeName = nullptr;
    const char16_t* masterName = nullptr;
    if (nameTarget == 0) {
        masterName = name.data();
    } else if (nameTarget == 1) {
        pledgeName = name.data();
    } else {
        return false;
    }
    CPledgeJoinSystem::Inst()->RequestSearchPledgeRecruitBoard(
        user, pledgeName, minimumLevel, maximumLevel, masterName, pledgeType, sortCondition, orderCondition, searchCondition
    );
    return false;
}

bool RequestApplyPledgeRecruitBoard(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00beab3c.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int action = 0;
    int recruitType = 0;
    std::array<char16_t, 21> title{};
    std::array<char16_t, 257> description{};
    int sortCondition = 0;
    int pledgeType = 0;
    ::Disassemble(
        packet, "ddSSdd", &action, &recruitType, 0x2a, title.data(), 0x202, description.data(), &sortCondition, &pledgeType
    );
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }
    if (sortCondition != 0 && sortCondition != 1) {
        sortCondition = -1;
    }
    if (sortCondition == -1) {
        return false;
    }
    if (pledgeType != MASTER_PLEDGE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserPacketEx.cpp",
        //     0x245a)
        L2_LOG_WARN(
            Log, "Clan recruitment board request rejected: unsupported clan unit type=%d, expected=%d, character ID=%u",
            pledgeType, static_cast<int>(MASTER_PLEDGE), static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    if (user->m_PledgeIODelay == 1) {
        // Log.Add(
        //     LOG_DEBUG,
        //     L"Can't response RequestApplyPledgeRecruitBoard click too fast")
        L2_LOG_DEBUG(
            Log, "Clan recruitment board request ignored: clan I/O delay flag is set, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    runtime::raw_atomic::ExchangeI32(&user->m_PledgeIODelay, 1);
    user->m_nPledgeJoinIOTickCount = 0;

    CPledgeJoinSystem* const system = CPledgeJoinSystem::Inst();
    if (action == 0) {
        system->RequestDeletePledgeRecruitBoard(user, false);
    } else if (action == 1) {
        system->RequestWritePledgeRecruitBoard(
            user, recruitType, title.data(), description.data(), sortCondition, static_cast<PledgeType>(pledgeType)
        );
    } else {
        system->RequestModifyPledgeRecruitBoard(
            user, recruitType, title.data(), description.data(), sortCondition, static_cast<PledgeType>(pledgeType)
        );
    }
    return false;
}

bool RequestPledgeOnPledgeRecruitBoard(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00beae20.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::uint32_t pledgeServerId = 0;
    ::Disassemble(packet, "d", &pledgeServerId);
    if (user->m_data->m_nWorldId == g_nWorldId) {
        CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
        if (pledge != nullptr) {
            CPledgeJoinSystem::Inst()->RequestPledgeOnPledgeRecruitBoard(user, static_cast<int>(pledge->GetDbId()));
        }
    }
    return false;
}

bool RequestApplyOnPledgeRecruit(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00beaf60.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int action = 0;
    std::uint32_t pledgeServerId = 0;
    std::array<char16_t, 51> comment{};
    ::Disassemble(packet, "ddS", &action, &pledgeServerId, 0x66, comment.data());
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }
    if (user->m_PledgeIODelay == 1) {
        // Log.Add(
        //     LOG_DEBUG,
        //     L"Can't response RequestApplyOnPledgeRecruit click too fast")
        L2_LOG_DEBUG(
            Log, "Clan recruitment application ignored: clan I/O delay flag is set, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    runtime::raw_atomic::ExchangeI32(&user->m_PledgeIODelay, 1);
    user->m_nPledgeJoinIOTickCount = 0;
    if (user->GetPledge() != nullptr) {
        return false;
    }
    CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
    if (pledge == nullptr || pledge->GetStatus() == 3) {
        return false;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }

    CPledgeJoinSystem* const system = CPledgeJoinSystem::Inst();
    if (action == 0) {
        system->RequestDeletePledgeWaitingUserList(user, false);
    } else if (action == 1) {
        system->RequestWritePledgeWaitingUserList(user, static_cast<int>(pledge->GetDbId()), comment.data());
    }
    return false;
}

bool RequestApplyInfoOnPledgeWaitingList(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00be6dcc.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_nWorldId == g_nWorldId) {
        db.RequestApplyInfoOnPledgeWaitingList(user);
    }
    return false;
}

bool RequestPledgeWaitingUserList(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00beb1ac.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::uint32_t pledgeServerId = 0;
    ::Disassemble(packet, "d", &pledgeServerId);
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }
    if (user->m_WaitListDelay == 1) {
        // Log.Add(
        //     LOG_DEBUG,
        //     L"Can't response RequestPledgeWaitingUserList click too fast")
        L2_LOG_DEBUG(
            Log, "Clan waiting-list request ignored: waiting-list delay flag is set, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    runtime::raw_atomic::ExchangeI32(&user->m_WaitListDelay, 1);
    user->m_nWaitListTickCount = 0;
    CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
    if (pledge != nullptr) {
        CPledgeJoinSystem::Inst()->RequestPledgeWaitingUserList(user, static_cast<int>(pledge->GetDbId()));
    }
    return false;
}

bool RequestUserOnPledgeWaitingList(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00beb328.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::uint32_t pledgeServerId = 0;
    int characterId = 0;
    ::Disassemble(packet, "dd", &pledgeServerId, &characterId);
    if (user->m_data->m_nWorldId == g_nWorldId) {
        CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
        if (pledge != nullptr) {
            CPledgeJoinSystem::Inst()->RequestUserOnPledgeWaitingList(
                user, static_cast<int>(pledge->GetDbId()), characterId
            );
        }
    }
    return false;
}

bool RequestAcceptPledgeWaitingUser(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00bf78cc.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int action = 0;
    int characterId = 0;
    int pledgeType = 0;
    ::Disassemble(packet, "ddd", &action, &characterId, &pledgeType);
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }
    if (pledgeType != MASTER_PLEDGE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserPacketEx.cpp",
        //     0x2560)
        L2_LOG_WARN(
            Log,
            "Clan waiting-list decision rejected: unsupported clan unit type=%d, expected=%d, applicant character ID=%d",
            pledgeType, static_cast<int>(MASTER_PLEDGE), characterId
        );
        return false;
    }
    if (user->m_PledgeIODelay == 1) {
        // Log.Add(
        //     LOG_DEBUG,
        //     L"Can't response RequestAcceptPledgeWaitingUser, click too fast")
        L2_LOG_DEBUG(
            Log, "Clan waiting-list decision ignored: clan I/O delay flag is set, applicant character ID=%d", characterId
        );
        return false;
    }
    runtime::raw_atomic::ExchangeI32(&user->m_PledgeIODelay, 1);
    user->m_nPledgeJoinIOTickCount = 0;

    const std::CDualKey<int, int> databaseKey(g_nWorldId, characterId);
    if (action == 0) {
        CPledgeJoinSystem::Inst()->RequestRejectPledgeWiatingUser(user, user->m_data->m_nWorldId, characterId);
        CSPointer<User> rejected = g_UserDBIDMap.FindObject(databaseKey);
        if (rejected) {
            db.RequestPledgeRecruitApplyInfo(rejected.get());
        }
        return false;
    }

    CSPointer<User> databaseUser = g_UserDBIDMap.FindObject(databaseKey);
    if (!databaseUser) {
        socket->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        socket->Send("cd", 0x2d, 0);
        return false;
    }
    CSPointer<User> liveUser = g_UserMap.FindObject(static_cast<int>(databaseUser->Id()));
    if (!liveUser) {
        socket->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        socket->Send("cd", 0x2d, 0);
        return false;
    }
    socket->DoJoinPledge(user, liveUser.get(), static_cast<PledgeType>(pledgeType));
    return false;
}

bool RequestSearchPledgeDraftUserList(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00beb464.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int minimumLevel = 0;
    int maximumLevel = 0;
    int classGroup = 0;
    std::array<char16_t, 25> name{};
    int sortCondition = 0;
    int orderCondition = 0;
    ::Disassemble(
        packet, "dddSdd", &minimumLevel, &maximumLevel, &classGroup, 0x32, name.data(), &sortCondition, &orderCondition
    );
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }
    if (user->m_DraftSearchDelay == 1) {
        // Log.Add(
        //     LOG_DEBUG,
        //     L"Can't response RequestSearchPledgeDraftUserList, click too fast")
        L2_LOG_DEBUG(
            Log, "Clan draft search ignored: draft search delay flag is set, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    runtime::raw_atomic::ExchangeI32(&user->m_DraftSearchDelay, 1);
    user->m_nDraftSearchTickCount = 0;
    CPledgeJoinSystem::Inst()->RequestPledgeDraftUserList(
        user, minimumLevel, maximumLevel, classGroup, name.data(), sortCondition, orderCondition
    );
    return false;
}

bool RequestApplyPledgeDraftUserList(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00beb620.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int action = 0;
    int value = 0;
    ::Disassemble(packet, "dd", &action, &value);
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }
    if (user->m_PledgeIODelay == 1) {
        // Log.Add(
        //     LOG_DEBUG,
        //     L"Can't response RequestApplyPledgeDraftUserList, click too fast")
        L2_LOG_DEBUG(
            Log, "Clan draft list update ignored: clan I/O delay flag is set, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    runtime::raw_atomic::ExchangeI32(&user->m_PledgeIODelay, 1);
    user->m_nPledgeJoinIOTickCount = 0;
    if (user->GetPledge() != nullptr) {
        return false;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }
    if (action == 0) {
        CPledgeJoinSystem::Inst()->RequestDeleteDraftUserList(user);
    } else {
        CPledgeJoinSystem::Inst()->RequestWriteDraftUserList(user, value);
    }
    return false;
}

bool RequestPledgeJoinRemainTime(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00beb7fc.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        CPledgeJoinSystem::Inst()->RequestPledgeJoinRemainTime(user);
    }
    return user == nullptr;
}

bool RequestPledgeRecruitApplyInfo(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be6ea4.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        db.RequestPledgeRecruitApplyInfo(user);
    }
    return user == nullptr;
}

bool ResponsePetitionExternalNotify(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be6f68.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        db.RequestDeletePetitionExternalNotify(*user);
    }
    return user == nullptr;
}

bool RequestStopMove(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be7980.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->StopMove();
    }
    return user == nullptr;
}

bool ChangeNicknameColor(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be2abc.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        int selectedColorIndex = 0;
        int itemClassId = 0;
        std::array<char16_t, 25> nickname{};
        ::Disassemble(packet, "dSd", &selectedColorIndex, 0x32, nickname.data(), &itemClassId);
        user->SetNicknameAndColor(selectedColorIndex, nickname.data(), itemClassId);
    }
    return user == nullptr;
}

bool ResetNicknameColor(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be2bdc.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->ResetNicknameAndColor();
    }
    return user == nullptr;
}

bool StartRequestPVPMatchRank(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00bf927c.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    user->m_bReceiveUserPVPMatchRecord = true;
    CPVPMatch* const match = CPVPMatchSystem::inst().GetMatchByUser(*user);
    if (match != nullptr) {
        match->SendCurrentRecordForUIToOneUser(static_cast<std::uint32_t>(user->m_data->m_nDBID), 1);
    }
    return false;
}

bool StopRequestPVPMatchRank(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be2c98.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->m_bReceiveUserPVPMatchRecord = false;
    }
    return user == nullptr;
}

bool ChangeAttributeCancelPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be593c.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->CheckAndResetPendingUseETCItem(PUIT_ATTRIBUTE_CHANGE);
    }
    return user == nullptr;
}

bool RequestCrystalItemCancel(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be4238.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->SetPendingUseETCItem(nullptr, PUIT_CRYSTAL);
    }
    return user == nullptr;
}

bool ChangeAttributeItemPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be575c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    int groupId = 0;
    int targetItemServerId = 0;
    ::Disassemble(packet, "dd", &groupId, &targetItemServerId);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    return user == nullptr ? true : user->OnChangeAttributeItemPacket(groupId, targetItemServerId);
}

bool OnRequestShapeShifting(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be6018, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int targetItemServerId = 0;
    ::Disassemble(packet, "d", &targetItemServerId);
    return user->OnRequestShapeShifting(targetItemServerId);
}

bool TryToPutShapeShiftingTargetItem(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00bf10c8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int targetItemServerId = 0;
    ::Disassemble(packet, "d", &targetItemServerId);
    CItem* const shapeItem = user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING);
    if (shapeItem == nullptr || !user->m_inv.Have(shapeItem->m_data->index)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ShapeShifting][%S][%d]Try to put shape shifting "
        //     L"target item fail. Source item[%d] is null. user[%s]",
        //     "UserPacketEx.cpp",
        //     0x2229,
        //     shapeItem != nullptr
        //         ? shapeItem->m_data->m_nClassID
        //         : 0,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Appearance target selection rejected: source item is missing or no longer in inventory, source class=%d,
        //     character=%s", shapeItem != nullptr
        //         ? shapeItem->m_data->m_nClassID
        //         : 0,
        //     ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_WARN(
            Log,
            "Appearance target selection rejected: source item is missing or no longer in inventory, source class=%d, "
            "character=%s",
            shapeItem != nullptr ? shapeItem->m_data->m_nClassID : 0, ::Utf16ToUtf8(user->GetName()).c_str()
        );
        // socket->Send("chdQ", 0xfe, 0x12a, 0, INT64_C(0))
        socket->Send("chdQ", 0xfe, 0x12a, ShapeShiftingTargetItemPutResult::SSTIPR_FAIL, INT64_C(0));
        return false;
    }

    CItem* const targetItem = user->m_inv.GetByServerID(targetItemServerId);
    if (targetItem == nullptr || !targetItem->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ShapeShifting][%S][%d]Try to put shape shifting "
        //     L"target item fail. Target item[%d] is invalid. user[%s]",
        //     "UserPacketEx.cpp",
        //     0x2231,
        //     targetItem != nullptr
        //         ? targetItem->m_data->m_nClassID
        //         : 0,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Appearance target selection rejected: target item is missing or invalid, target class=%d, character=%s",
        //     targetItem != nullptr
        //         ? targetItem->m_data->m_nClassID
        //         : 0,
        //     ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_WARN(
            Log, "Appearance target selection rejected: target item is missing or invalid, target class=%d, character=%s",
            targetItem != nullptr ? targetItem->m_data->m_nClassID : 0, ::Utf16ToUtf8(user->GetName()).c_str()
        );
        // socket->Send("chdQ", 0xfe, 0x12a, 0, INT64_C(0))
        socket->Send("chdQ", 0xfe, 0x12a, ShapeShiftingTargetItemPutResult::SSTIPR_FAIL, INT64_C(0));
        return false;
    }
    if (targetItem->m_data->GetLocked() != 0) {
        user->SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        // socket->Send("chdQ", 0xfe, 0x12a, 0, INT64_C(0))
        socket->Send("chdQ", 0xfe, 0x12a, ShapeShiftingTargetItemPutResult::SSTIPR_FAIL, INT64_C(0));
        return false;
    }

    CItem* const extractionItem = user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING_EXTRACTION);
    if (!targetItem->IsShapeShiftableTargetItem(user, shapeItem, extractionItem)) {
        // socket->Send("chdQ", 0xfe, 0x12a, 0, INT64_C(0))
        socket->Send("chdQ", 0xfe, 0x12a, ShapeShiftingTargetItemPutResult::SSTIPR_FAIL, INT64_C(0));
        return false;
    }
    for (const CSkillInfo* const skill : shapeItem->m_ItemSkillList) {
        if (skill->CheckOperateCond(user, targetItem, CSkillInfo::OCT_BOTH) != -2) {
            user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
            socket->Send(
                "chdQ", 0xfe, 0x12a,
                // 0,
                ShapeShiftingTargetItemPutResult::SSTIPR_FAIL, INT64_C(0)
            );
            return false;
        }
    }

    socket->Send(
        "chdQ", 0xfe, 0x12a,
        // 1,
        ShapeShiftingTargetItemPutResult::SSTIPR_SUCCESS, user->CalcShapeShiftingFee(shapeItem, targetItem)
    );
    return false;
}

bool TryToPutShapeShiftingExtractionItem(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be5cbc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int targetItemServerId = 0;
    int extractionItemServerId = 0;
    ::Disassemble(packet, "dd", &targetItemServerId, &extractionItemServerId);
    CItem* const targetItem = user->m_inv.GetByServerID(targetItemServerId);
    if (targetItem == nullptr || !targetItem->IsItem()) {
        user->SendSystemMessage(SystemMessageId::SELECT_TARGET_ITEM_FOR_APPEARANCE_MODIFICATION_FIRST);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ShapeShifting][%S][%d]Try to put shape shifting "
        //     L"target item fail. Target item is invalid.",
        //     "UserPacketEx.cpp",
        //     0x2279)
        L2_LOG_WARN(
            Log,
            "Appearance extraction item selection rejected: target item is missing or invalid, target ID=%d, character "
            "ID=%u",
            targetItemServerId, static_cast<unsigned>(user->GetDBID())
        );
        // socket->Send("chd", 0xfe, 299, 0)
        socket->Send("chd", 0xfe, 299, ShapeShiftingExtractionItemPutResult::SSEIPR_FAIL);
        return false;
    }

    CItem* const extractionItem = user->m_inv.GetByServerID(extractionItemServerId);
    if (extractionItem == nullptr || !extractionItem->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ShapeShifting][%S][%d]Try to put shape shifting "
        //     L"extraction item fail. Extraction item is invalid.",
        //     "UserPacketEx.cpp",
        //     0x2281)
        L2_LOG_WARN(
            Log,
            "Appearance extraction item selection rejected: extraction item is missing or invalid, extraction item ID=%d, "
            "character ID=%u",
            extractionItemServerId, static_cast<unsigned>(user->GetDBID())
        );
        // socket->Send("chd", 0xfe, 299, 0)
        socket->Send("chd", 0xfe, 299, ShapeShiftingExtractionItemPutResult::SSEIPR_FAIL);
        return false;
    }
    if (!extractionItem->IsShapeShiftableExtractionItem(user, targetItem)) {
        // socket->Send("chd", 0xfe, 299, 0)
        socket->Send("chd", 0xfe, 299, ShapeShiftingExtractionItemPutResult::SSEIPR_FAIL);
        return false;
    }

    CItem* const shapeItem = user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING);
    if (shapeItem == nullptr || !user->m_inv.Have(shapeItem->m_data->index)) {
        user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ShapeShifting][%S][%d]Try to put shape shifting "
        //     L"extraction item fail. Source item is null",
        //     "UserPacketEx.cpp",
        //     0x2291)
        L2_LOG_WARN(
            Log,
            "Appearance extraction item selection rejected: source item is missing or no longer in inventory, character "
            "ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        // socket->Send("chd", 0xfe, 299, 0)
        socket->Send("chd", 0xfe, 299, ShapeShiftingExtractionItemPutResult::SSEIPR_FAIL);
        return false;
    }

    user->SetPendingUseETCItem(extractionItem, PUIT_SHAPE_SHIFTING_EXTRACTION);
    // socket->Send("chd", 0xfe, 299, 1)
    socket->Send("chd", 0xfe, 299, ShapeShiftingExtractionItemPutResult::SSEIPR_SUCCESS);
    return false;
}

bool TryToPutEnchantTargetItem(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00bee788, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    int targetItemServerId = 0;
    ::Disassemble(packet, "d", &targetItemServerId);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CItem* const targetItem = user->m_inv.GetByServerID(targetItemServerId);
    if (targetItem == nullptr || !targetItem->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try to put enchant target item fail. "
        //     L"Target item[%d] is invalid. user[%s]",
        //     "UserPacketEx.cpp",
        //     0x0e81,
        //     targetItem != nullptr
        //         ? targetItem->m_data->m_nClassID
        //         : 0,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Enchant target selection rejected: target item is missing or invalid, target class=%d, character=%s",
        //     targetItem != nullptr
        //         ? targetItem->m_data->m_nClassID
        //         : 0,
        //     ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_WARN(
            Log, "Enchant target selection rejected: target item is missing or invalid, target class=%d, character=%s",
            targetItem != nullptr ? targetItem->m_data->m_nClassID : 0, ::Utf16ToUtf8(user->GetName()).c_str()
        );
        socket->SendSystemMessage(SystemMessageId::ENCHANT_SCROLL_USE_CANCELLED);
        socket->Send("chd", 0xfe, 0x82, 0);
        return false;
    }
    if (!targetItem->IsEnchantableItem()) {
        user->SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_MEET_ENCHANT_SCROLL_REQUIREMENTS);
        socket->Send("chd", 0xfe, 0x82, 0);
        return false;
    }
    if (targetItem->m_data->GetLocked() != 0) {
        user->SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        socket->Send("chd", 0xfe, 0x82, 0);
        return false;
    }

    CItem* const scrollItem = user->GetPendingUseETCItem(PUIT_ENCHANT);
    if (scrollItem == nullptr || !user->m_inv.Have(scrollItem->m_data->index)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try to put enchant target item fail. "
        //     L"Pending item[%d] is null. user[%s]",
        //     "UserPacketEx.cpp",
        //     0x0e96,
        //     scrollItem != nullptr
        //         ? scrollItem->m_data->m_nClassID
        //         : 0,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Enchant target selection rejected: pending scroll is missing or no longer in inventory, scroll class=%d,
        //     character=%s", scrollItem != nullptr
        //         ? scrollItem->m_data->m_nClassID
        //         : 0,
        //     ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_WARN(
            Log,
            "Enchant target selection rejected: pending scroll is missing or no longer in inventory, scroll class=%d, "
            "character=%s",
            scrollItem != nullptr ? scrollItem->m_data->m_nClassID : 0, ::Utf16ToUtf8(user->GetName()).c_str()
        );
        socket->SendSystemMessage(SystemMessageId::ENCHANT_SCROLL_USE_CANCELLED);
        socket->Send("chd", 0xfe, 0x82, 0);
        return false;
    }
    for (const CSkillInfo* const skill : scrollItem->m_ItemSkillList) {
        if (skill->CheckOperateCond(user, targetItem, CSkillInfo::OCT_BOTH) != -2) {
            user->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
            socket->Send("chd", 0xfe, 0x82, 0);
            return false;
        }
    }

    CItem* const supportItem = user->GetPendingUseETCItem(PUIT_INC_ENCHANT_PROP);
    if (supportItem != nullptr) {
        for (const CSkillInfo* const skill : supportItem->m_ItemSkillList) {
            if (skill->CheckOperateCond(user, targetItem, CSkillInfo::OCT_BOTH) != -2) {
                user->SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_MEET_ENCHANT_SUPPORT_STONE_REQUIREMENTS);
                socket->Send("chd", 0xfe, 0x82, 0);
                return false;
            }
        }
    }

    socket->Send("chd", 0xfe, 0x82, 1);
    return false;
}

bool TryToPutEnchantSupportItem(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00beea78, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    int supportItemServerId = 0;
    int targetItemServerId = 0;
    ::Disassemble(packet, "dd", &supportItemServerId, &targetItemServerId);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CItem* const targetItem = user->m_inv.GetByServerID(targetItemServerId);
    if (targetItem == nullptr || !targetItem->IsItem() || targetItem->IsDuration()) {
        socket->Send("chd", 0xfe, 0x83, 0);
        user->SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_MEET_ENCHANT_SCROLL_REQUIREMENTS);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try to put enchant target item fail. "
        //     L"Target item is invalid.",
        //     "UserPacketEx.cpp",
        //     0x0efa)
        L2_LOG_WARN(
            Log,
            "Enchant support selection rejected: target item is missing, invalid or temporary, target ID=%d, character "
            "ID=%u",
            targetItemServerId, static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    CItem* const scrollItem = user->GetPendingUseETCItem(PUIT_ENCHANT);
    if (scrollItem == nullptr || !user->m_inv.Have(scrollItem->m_data->index)) {
        socket->Send("chd", 0xfe, 0x83, 0);
        socket->SendSystemMessage(SystemMessageId::ENCHANT_SCROLL_USE_CANCELLED);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try to put enchant target item fail. "
        //     L"Pending item is null",
        //     "UserPacketEx.cpp",
        //     0x0f14)
        L2_LOG_WARN(
            Log, "Enchant support selection rejected: pending scroll is missing or no longer in inventory, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    for (const CSkillInfo* const skill : scrollItem->m_ItemSkillList) {
        if (skill->CheckOperateCond(user, targetItem, CSkillInfo::OCT_BOTH) != -2) {
            socket->Send("chd", 0xfe, 0x83, 0);
            user->SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_MEET_ENCHANT_SCROLL_REQUIREMENTS);
            return false;
        }
    }

    CItem* const supportItem = user->m_inv.GetByServerID(supportItemServerId);
    bool compatible = false;
    if (supportItem != nullptr) {
        const int supportType = supportItem->m_const->m_EtcitemType;
        const bool supportTypeAllowed = supportType == EIT_INC_PROP_ENCHT_WP || supportType == EIT_INC_PROP_ENCHT_AM ||
            supportType == EIT_BLESS_INC_PROP_ENCHT_WP || supportType == EIT_BLESS_INC_PROP_ENCHT_AM ||
            supportType == EIT_MULTI_INC_PROB_ENCHT_WP || supportType == EIT_MULTI_INC_PROB_ENCHT_AM ||
            supportType == EIT_INC_PROP_ENCHT_AG || supportType == EIT_BLESS_INC_PROP_ENCHT_AG ||
            supportType == EIT_MULTI_INC_PROB_ENCHT_AG;
        switch (supportTypeAllowed ? scrollItem->m_const->m_EtcitemType : EIT_MAX) {
            case EIT_ENCHT_WP:
                compatible = supportType == EIT_INC_PROP_ENCHT_WP;
                break;
            case EIT_ENCHT_AM:
                compatible = supportType == EIT_INC_PROP_ENCHT_AM;
                break;
            case EIT_BLESS_ENCHT_WP:
                compatible = supportType == EIT_BLESS_INC_PROP_ENCHT_WP;
                break;
            case EIT_BLESS_ENCHT_AM:
                compatible = supportType == EIT_BLESS_INC_PROP_ENCHT_AM;
                break;
            case EIT_MULTI_ENCHT_WP:
                compatible = supportType == EIT_MULTI_INC_PROB_ENCHT_WP;
                break;
            case EIT_MULTI_ENCHT_AM:
                compatible = supportType == EIT_MULTI_INC_PROB_ENCHT_AM;
                break;
            case EIT_ENCHT_AG:
                compatible = supportType == EIT_INC_PROP_ENCHT_AG;
                break;
            case EIT_BLESS_ENCHT_AG:
                compatible = supportType == EIT_BLESS_INC_PROP_ENCHT_AG;
                break;
            case EIT_MULTI_ENCHT_AG:
                compatible = supportType == EIT_MULTI_INC_PROB_ENCHT_AG;
                break;
            default:
                break;
        }
    }
    if (!compatible) {
        socket->Send("chd", 0xfe, 0x83, 0);
        user->SendSystemMessage(SystemMessageId::INVALID_ENCHANT_SUPPORT_STONE);
        return false;
    }

    for (const CSkillInfo* const skill : supportItem->m_ItemSkillList) {
        if (skill->CheckOperateCond(user, targetItem, CSkillInfo::OCT_BOTH) != -2) {
            socket->Send("chd", 0xfe, 0x83, 0);
            user->SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_MEET_ENCHANT_SUPPORT_STONE_REQUIREMENTS);
            return false;
        }
    }

    user->SetPendingUseETCItem(nullptr, PUIT_INC_ENCHANT_PROP);
    if (user->CheckAndSetPendingUseETCItem(supportItem, PUIT_INC_ENCHANT_PROP)) {
        socket->Send("chd", 0xfe, 0x83, 1);
        return false;
    }
    user->InitAllEnchantBonusOptions();
    socket->Send("chd", 0xfe, 0x83, 0);
    user->SendSystemMessage(SystemMessageId::ENCHANT_SUPPORT_STONE_REGISTRATION_FAILED);
    return false;
}

bool EnchantScrollItemAdd(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00bf154c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int scrollItemServerId = 0;
    int targetItemServerId = 0;
    ::Disassemble(packet, "dd", &scrollItemServerId, &targetItemServerId);
    CItem* const scrollItem = user->m_inv.GetByServerID(scrollItemServerId);
    CItem* const targetItem = user->m_inv.GetByServerID(targetItemServerId);
    if (scrollItem == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try to put scroll item fail. "
        //     L"Pending scroll[%d] is null. user[%s]",
        //     "UserPacketEx.cpp",
        //     0x2677,
        //     0,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Enchant scroll selection rejected: scroll is not in inventory, scroll ID=%d, character=%s",
        //     scrollItemServerId, ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_WARN(
            Log, "Enchant scroll selection rejected: scroll is not in inventory, scroll ID=%d, character=%s",
            scrollItemServerId, ::Utf16ToUtf8(user->GetName()).c_str()
        );
        socket->SendSystemMessage(SystemMessageId::ENCHANT_SCROLL_USE_CANCELLED);
        socket->Send("chd", 0xfe, 0x152, 0);
        return false;
    }

    if (targetItem != nullptr) {
        if (!targetItem->IsEnchantableItem()) {
            user->SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_MEET_ENCHANT_SCROLL_REQUIREMENTS);
            socket->Send("chd", 0xfe, 0x152, 0);
            return false;
        }
        for (const CSkillInfo* const skill : scrollItem->m_ItemSkillList) {
            if (skill->CheckOperateCond(user, targetItem, CSkillInfo::OCT_BOTH) != -2) {
                socket->SendSystemMessage(SystemMessageId::ITEM_REQUIREMENTS_NOT_MET);
                socket->Send("chd", 0xfe, 0x152, 0);
                return false;
            }
        }
    }

    CItem* const supportItem = user->GetPendingUseETCItem(PUIT_INC_ENCHANT_PROP);
    if (supportItem != nullptr) {
        bool checkedType = true;
        bool compatible = false;
        bool resetBeforeMessage = false;
        switch (scrollItem->m_const->m_EtcitemType) {
            case EIT_ENCHT_WP:
                compatible = supportItem->m_const->m_EtcitemType == EIT_INC_PROP_ENCHT_WP;
                resetBeforeMessage = true;
                break;
            case EIT_ENCHT_AM:
                compatible = supportItem->m_const->m_EtcitemType == EIT_INC_PROP_ENCHT_AM;
                resetBeforeMessage = true;
                break;
            case EIT_BLESS_ENCHT_WP:
                compatible = supportItem->m_const->m_EtcitemType == EIT_BLESS_INC_PROP_ENCHT_WP;
                resetBeforeMessage = true;
                break;
            case EIT_BLESS_ENCHT_AM:
                compatible = supportItem->m_const->m_EtcitemType == EIT_BLESS_INC_PROP_ENCHT_AM;
                break;
            case EIT_MULTI_ENCHT_WP:
                compatible = supportItem->m_const->m_EtcitemType == EIT_MULTI_INC_PROB_ENCHT_WP;
                break;
            case EIT_MULTI_ENCHT_AM:
                compatible = supportItem->m_const->m_EtcitemType == EIT_MULTI_INC_PROB_ENCHT_AM;
                break;
            case EIT_ENCHT_AG:
                compatible = supportItem->m_const->m_EtcitemType == EIT_INC_PROP_ENCHT_AG;
                resetBeforeMessage = true;
                break;
            case EIT_BLESS_ENCHT_AG:
                compatible = supportItem->m_const->m_EtcitemType == EIT_BLESS_INC_PROP_ENCHT_AG;
                break;
            case EIT_MULTI_ENCHT_AG:
                compatible = supportItem->m_const->m_EtcitemType == EIT_MULTI_INC_PROB_ENCHT_AG;
                break;
            default:
                checkedType = false;
                break;
        }

        if (checkedType && !compatible) {
            socket->Send("ch", 0xfe, 0x153);
            if (resetBeforeMessage) {
                user->SetPendingUseETCItem(nullptr, PUIT_INC_ENCHANT_PROP);
                user->SendSystemMessage(SystemMessageId::INVALID_ENCHANT_SUPPORT_STONE);
            } else {
                user->SendSystemMessage(SystemMessageId::INVALID_ENCHANT_SUPPORT_STONE);
                user->SetPendingUseETCItem(nullptr, PUIT_INC_ENCHANT_PROP);
            }
        }
    }

    socket->Send("chdd", 0xfe, 0x152, 1, scrollItem->m_data->m_nClassID);
    user->SetPendingUseETCItem(scrollItem, PUIT_ENCHANT);
    return false;
}

bool EnchantSupportItemRemove(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be70e8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->SetPendingUseETCItem(nullptr, PUIT_INC_ENCHANT_PROP);
        user->InitAllEnchantBonusOptions();
        socket->Send("ch", 0xfe, 0x153, std::int64_t{-2});
    }
    return user == nullptr;
}

bool EnchantClose(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be7c9c.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->CheckAndResetPendingUseETCItem(PUIT_ONE);
        user->CheckAndResetPendingUseETCItem(PUIT_TWO);
    }
    return user == nullptr;
}

bool RequestAttendanceStamp(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be8268.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->RequestAccountStamp(0, true, true);
    }
    return user == nullptr;
}

bool RequestVipAttendanceItemList(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bfc6ec, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (g_bUseVipAttendance && VipAttendanceSystem::Inst().CheckDaysAndReset(user)) {
        VipAttendanceSystem::Inst().SendVipAttendanceItemList(user);
    }
    return false;
}

bool RequestChangeInventorySlot(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf8f84, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int requestCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &requestCount);
    const int artifactLimit = user->GetArtifactInventoryLimit();
    const int inventoryLimit = user->GetInventoryLimit();
    if (requestCount <= runtime::AddInt32Wrapping(artifactLimit, inventoryLimit)) {
        std::map<int, int> slotIndices;
        int index = 0;
        while (index < requestCount) {
            int itemServerId = 0;
            int slotIndex = 0;
            cursor = ::Disassemble(cursor, "dd", &itemServerId, &slotIndex);
            slotIndices.emplace(itemServerId, slotIndex);
            index = runtime::AddInt32Wrapping(index, 1);
        }
        user->ChangeInventorySlot(slotIndices);
        db.RequestSaveInventorySlotIndex(user);
    }
    return false;
}

bool RequestOpenMonsterBook(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be9048.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr && user->IsActive()) {
        user->RequestOpenMonsterBook();
    }
    return user == nullptr;
}

bool RequestCloseMonsterBook(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be9118.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr && user->IsActive()) {
        user->RequestCloseMonsterBook();
    }
    return user == nullptr;
}

bool RequestMonsterBookReward(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be91e8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::int16_t cardId = 0;
        ::Disassemble(packet, "h", &cardId);
        user->TryGetMonsterBookReward(static_cast<int>(cardId));
    }
    return user == nullptr;
}

bool RequestPostItemList(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bfa9d0 always returns false.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x127e)
        L2_LOG_ERROR(Log, "Mail attachment list request ignored: socket has no character");
        return false;
    }

    CPackageItemPacketManager packetManager;
    {
        L2_READ_GUARD(user->m_inv);
        for (CSharedItemData* data = user->m_inv.GetFirst(); data != nullptr; data = CItemFactory::Inst().GetNext(data)) {
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(data->index);
            if (!item || !item->IsTradable(user, false)) {
                continue;
            }

            packetManager.CheckLimitPackageItemPacketDataSize();
            CItemAssembler itemAssembler(*user, *item.get(), false);
            const int assembled = packetManager.PackageAssembleItem(&itemAssembler);
            if (assembled < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Assembe Error at file[%s], line[%d], "
                //     L"assemble result = [%d], total length = [%d]",
                //     L"UserPacketEx.cpp",
                //     0x1296,
                //     assembled,
                //     packetManager.GetTotalPackageItemLength())
                L2_LOG_ERROR(
                    Log, "Mail attachment list item encoding failed: result=%d, total length=%d", assembled,
                    packetManager.GetTotalPackageItemLength()
                );
            } else {
                packetManager.SetAssembleItemLengthPerItem(assembled);
                packetManager.IncreasePackageItemCount();
            }
        }
    }

    socket->Send(
        "chcd", static_cast<int>(S_EX), static_cast<int>(S_EX_REPLY_POST_ITEM_LIST), 1,
        packetManager.GetTotalPackageItemCount()
    );
    packetManager.SendPacket_PackageItemData(socket, S_EX, S_EX_REPLY_POST_ITEM_LIST, PackageItemPacket::PIPT_ITEM_INFO);
    return false;
}

bool WritePost(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bf7dec always returns false.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x12ba)
        L2_LOG_ERROR(Log, "Mail send request ignored: socket has no character");
        return false;
    }

    std::array<char16_t, 25> receiverBuffer{};
    std::array<char16_t, 61> subjectBuffer{};
    std::array<char16_t, 1001> bodyBuffer{};
    int type = 0;
    int itemCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "SdSSd", 0x32, receiverBuffer.data(), &type, 0x7a, subjectBuffer.data(), 0x7d2, bodyBuffer.data(), &itemCount
    );
    if (itemCount > 8) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user %s attempted change post-trade packet. count %d > %d",
        //     user->GetName(),
        //     itemCount,
        //     8)
        // L2_LOG_WARN(Log,
        //     "Mail send request rejected: attachment count=%d exceeds limit=8, character=%s",
        //     itemCount, ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_WARN(
            Log, "Mail send request rejected: attachment count=%d exceeds limit=8, character=%s", itemCount,
            ::Utf16ToUtf8(user->GetName()).c_str()
        );
        return false;
    }

    std::u16string receiverTarget(receiverBuffer.data());
    if (receiverTarget.empty()) {
        // Log.Add(LOG_ERROR, L"[Post Error] Sender Name Is NULL.")
        L2_LOG_WARN(
            Log, "Mail send request rejected: recipient name is empty, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    receiverTarget = TrimLR(receiverTarget);
    const std::u16string& receiver = receiverTarget;
    const std::u16string_view subject(subjectBuffer.data());
    const std::u16string_view body(bodyBuffer.data());

    std::array<CPost::ItemPair, 8> items{};
    for (int index = 0; index < itemCount; ++index) {
        int itemSid = 0;
        std::int64_t amount = 0;
        cursor = ::Disassemble(cursor, "dQ", &itemSid, &amount);
        CItem* const item = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(itemSid));
        if (!item) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Write Post. Item does not exist, ItemSID[%d], "
            //     L"UserDBID[%d], UserName[%s]",
            //     itemSid,
            //     user->GetDBID(),
            //     user->GetName())
            // L2_LOG_WARN(Log,
            //     "Mail send request rejected: attachment item is unavailable, item ID=%d, character ID=%u, character=%s",
            //     itemSid,
            //     static_cast<unsigned>(user->GetDBID()),
            //     ::WideToUtf8(user->GetName()).c_str())
            L2_LOG_WARN(
                Log, "Mail send request rejected: attachment item is unavailable, item ID=%d, character ID=%u, character=%s",
                itemSid, static_cast<unsigned>(user->GetDBID()), ::Utf16ToUtf8(user->GetName()).c_str()
            );
            socket->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_WITH_UNSUITABLE_ITEM);
            return false;
        }

        const int itemClassId = item->m_data->m_nClassID;
        if (CItemRestrict::Inst().CheckItemRestrict(itemClassId)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Write Post. Item Restrict On. ClassID[%d]",
            //     itemClassId)
            L2_LOG_WARN(
                Log, "Mail send request rejected: attachment item class=%d is restricted, character ID=%u", itemClassId,
                static_cast<unsigned>(user->GetDBID())
            );
            socket->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_WITH_UNSUITABLE_ITEM);
            return false;
        }
        items[static_cast<std::size_t>(index)].db_id = static_cast<int>(item->GetDBID());
        items[static_cast<std::size_t>(index)].amount = amount;
    }
    if (itemCount > 0 && !F2PConfig::CanItemAttatchedPostUser(user)) {
        socket->SendSystemMessage(SystemMessageId::FREE_USER_CANNOT_ATTACH_ITEMS_OR_ADENA_TO_MAIL);
        return false;
    }

    std::int64_t money = 0;
    ::Disassemble(cursor, "Q", &money);
    if (type == 0 && money != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user %s sent normal mail with trade money.",
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Normal mail request contains payment amount=%lld. Resetting payment to zero, character=%s",
        //     static_cast<long long>(money), ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_WARN(
            Log, "Normal mail request contains payment amount=%lld; resetting payment to zero, character=%s",
            static_cast<long long>(money), ::Utf16ToUtf8(user->GetName()).c_str()
        );
        money = 0;
    }
    if (user->m_pPostInfo->GetSentListSize() > 240) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_OWN_MAILBOX_LIMIT_EXCEEDED);
        return false;
    }
    if (std::u16string_view(user->GetName()) == receiver) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_TO_SELF);
        return false;
    }
    if (User::IsInFifthClassGroup(user->m_data->m_nClass)) {
        socket->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }

    CSPointer<User> recipient = GetUserFromNameMap(g_nWorldId, receiver);
    if (recipient && User::IsInFifthClassGroup(recipient->m_data->m_nClass)) {
        socket->SendSystemMessage_Char(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_TO_NAMED_CHARACTER, recipient->GetName());
        return false;
    }

    std::int64_t fee = 0;
    std::array<ItemConsumeType, 8> consumeTypes{};
    if (PostUtil::CheckWritePost(baseSocket, user, itemCount, items.data(), money, fee, consumeTypes.data())) {
        if (user->m_pPostInfo->CheckSendingDelay() && user->m_pPostInfo->TryWrite()) {
            std::array<char, 0x400> itemPacket{};
            int itemPacketSize = 0;
            for (int index = 0; index < itemCount; ++index) {
                const CPost::ItemPair& item = items[static_cast<std::size_t>(index)];
                itemPacketSize += ::Assemble(
                    itemPacket.data() + itemPacketSize, static_cast<int>(itemPacket.size()) - itemPacketSize, "dQd",
                    item.db_id, item.amount, static_cast<int>(consumeTypes[static_cast<std::size_t>(index)])
                );
            }
            db.RequestWritePost(
                *user, receiver.data(), type, subject.data(), body.data(), itemCount, itemPacketSize, itemPacket.data(),
                money, itemCount > 0, fee
            );
            g_npcserver.Send("cdddd", 0xc6, user->GetServerId(), 2, 0, 0);
            return false;
        }

        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        socket->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_BEFORE_SEND_DELAY_EXPIRES);
    }
    return false;
}

bool DeleteReceivedPost(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bef93c always returns false.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x136e)
        L2_LOG_ERROR(Log, "Received mail deletion ignored: socket has no character");
        return false;
    }

    int requestedCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &requestedCount);
    const int availableCount = user->m_pPostInfo->GetReceivedListSize();
    if (availableCount < requestedCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid mail count for user %s, %d > %d",
        //     user->GetName(),
        //     requestedCount,
        //     availableCount)
        // L2_LOG_WARN(Log,
        //     "Received mail deletion rejected: character=%s, requested count=%d exceeds available count=%d",
        //     ::WideToUtf8(user->GetName()).c_str(),
        //     requestedCount,
        //     availableCount)
        L2_LOG_WARN(
            Log, "Received mail deletion rejected: character=%s, requested count=%d exceeds available count=%d",
            ::Utf16ToUtf8(user->GetName()).c_str(), requestedCount, availableCount
        );
        return false;
    }

    std::array<char, 0x5000> deletePacket{};
    int deletePacketSize = 0;
    int validCount = 0;
    for (int index = 0; index < requestedCount; ++index) {
        int mailId = 0;
        cursor = ::Disassemble(cursor, "d", &mailId);
        CSPointer<CPost> post = user->m_pPostInfo->FindReceivedPostWithRef(mailId);
        if (!post || post->GetProcessType() < Post::ProcessReceived) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"No mail exists for user %s, mail %d. "
            //     L"[DeleteReceivedPost]",
            //     user->GetName(),
            //     mailId)
            // L2_LOG_WARN(Log,
            //     "Received mail deletion skipped: mail is missing or has not reached received state, character=%s, mail
            //     ID=%d",
            //     ::WideToUtf8(user->GetName()).c_str(),
            //     mailId)
            L2_LOG_WARN(
                Log,
                "Received mail deletion skipped: mail is missing or has not reached received state, character=%s, mail "
                "ID=%d",
                ::Utf16ToUtf8(user->GetName()).c_str(), mailId
            );
            continue;
        }

        ++validCount;
        const int assembled = ::Assemble(
            deletePacket.data() + deletePacketSize, static_cast<int>(deletePacket.size()) - deletePacketSize, "dd", mailId,
            post->GetNextJobTime()
        );
        if (assembled > 0) {
            deletePacketSize += assembled;
        }
    }
    if (validCount > 0) {
        db.RequestDeletePost(validCount, deletePacketSize, deletePacket.data(), 2, 0, user->GetDBID(), 1);
    }
    return false;
}

bool RequestReceivedPost(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00befbd8 always returns false.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x13a8)
        L2_LOG_ERROR(Log, "Received mail open request ignored: socket has no character");
        return false;
    }
    if (!user->m_pPostInfo->CheckPacketReceivedDelay()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[RequestReceivedPost][delay][%s][%d]",
        //     user->GetName(),
        //     0x13af)
        // L2_LOG_DEBUG(Log,
        //     "Received mail open request ignored: packet delay check failed, character=%s",
        //     ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_DEBUG(
            Log, "Received mail open request ignored: packet delay check failed, character=%s",
            ::Utf16ToUtf8(user->GetName()).c_str()
        );
        return false;
    }
    if (g_nCountryCode == 1 && user->GetSocket() != nullptr && user->GetSocket()->m_bEconomicRestriction) {
        user->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        return false;
    }

    int mailId = 0;
    ::Disassemble(packet, "d", &mailId);
    CSPointer<CPost> post = user->m_pPostInfo->FindReceivedPostWithRef(mailId);
    if (!post) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No mail exists for user %s, mail %d. "
        //     L"[RequestReceivedPost]",
        //     user->GetName(),
        //     mailId)
        // L2_LOG_WARN(Log,
        //     "Received mail open request rejected: mail is missing, character=%s, mail ID=%d",
        //     ::WideToUtf8(user->GetName()).c_str(),
        //     mailId)
        L2_LOG_WARN(
            Log, "Received mail open request rejected: mail is missing, character=%s, mail ID=%d",
            ::Utf16ToUtf8(user->GetName()).c_str(), mailId
        );
    } else {
        db.RequestOpenPost(mailId, 1, post->GetNextJobTime());
    }
    return false;
}

bool ReceivePost(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bf5de0 always returns false.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x13d8)
        L2_LOG_ERROR(Log, "Mail collection request ignored: socket has no character");
        return false;
    }
    if (!user->m_pPostInfo->CheckPacketReceivedDelay()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ReceivePost][delay][%s][%d]",
        //     user->GetName(),
        //     0x13df)
        // L2_LOG_DEBUG(Log,
        //     "Mail collection request ignored: packet delay check failed, character=%s",
        //     ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_DEBUG(
            Log, "Mail collection request ignored: packet delay check failed, character=%s",
            ::Utf16ToUtf8(user->GetName()).c_str()
        );
        return false;
    }

    int mailId = 0;
    ::Disassemble(packet, "d", &mailId);
    CSPointer<CPost> post = user->m_pPostInfo->FindReceivedPostWithRef(mailId);
    if (!post) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No mail exists for user %s, mail %d. [ReceivePost]",
        //     user->GetName(),
        //     mailId)
        // L2_LOG_WARN(Log,
        //     "Mail collection rejected: mail is missing, character=%s, mail ID=%d",
        //     ::WideToUtf8(user->GetName()).c_str(),
        //     mailId)
        L2_LOG_WARN(
            Log, "Mail collection rejected: mail is missing, character=%s, mail ID=%d",
            ::Utf16ToUtf8(user->GetName()).c_str(), mailId
        );
        return false;
    }

    CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, post->GetSenderId()));
    if (sender && sender->GetSocket() != nullptr && sender->GetSocket()->GetRaidStatus() != URS_NONE) {
        user->SendSystemMessage(SystemMessageId::MAIL_SENDER_CANNOT_RECEIVE_PAYMENT_DURING_DIMENSIONAL_TRANSFER);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR]Sender is in Raidserver")
        L2_LOG_WARN(Log, "Mail collection rejected: sender has active raid status, mail ID=%d", mailId);
        return false;
    }
    if (!user->m_pPostInfo->TryReceivePost(mailId)) {
        return false;
    }

    const int itemCount = user->m_pPostInfo->GetItemCount();
    PostItemInfo* const items = user->m_pPostInfo->GetItemInfo();
    const std::int64_t tradeMoney = user->m_pPostInfo->GetTradeMoney();
    std::array<char, 0x5000> itemPacket{};
    int itemPacketSize = 0;
    for (int index = 0; index < itemCount; ++index) {
        const PostItemInfo& itemInfo = items[static_cast<std::size_t>(index)];
        ItemConsumeType consumeType = ICT_NORMAL;
        CItem* const item = static_cast<CItem*>(g_ObjectDB.GetObjectW(itemInfo.class_id, false));
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"No Item For GetConsumeType %d [ReceivePost]",
            //     itemInfo.class_id)
            L2_LOG_ERROR(
                Log, "Mail collection item template is missing; using normal consume type, item class=%d, mail ID=%d",
                itemInfo.class_id, mailId
            );
        } else {
            consumeType = item->m_data->m_ItemConsumeType;
        }
        itemPacketSize += ::Assemble(
            itemPacket.data() + itemPacketSize, static_cast<int>(itemPacket.size()) - itemPacketSize, "dQd", itemInfo.db_id,
            itemInfo.amount, static_cast<int>(consumeType)
        );
    }

    if (!PostUtil::CheckReceivePost(baseSocket, user, itemCount, items, tradeMoney, post->IsReturnable())) {
        user->m_pPostInfo->CancelTryReceivePost();
        return false;
    }
    if (!post->StartDB()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"already Started db job. [receivepost] %d",
        //     mailId)
        L2_LOG_WARN(Log, "Mail collection cancelled: database job is already active, mail ID=%d", mailId);
        user->m_pPostInfo->CancelTryReceivePost();
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        return false;
    }

    db.RequestReceivePost(
        mailId, post->GetNextJobTime(), post->GetSenderId(), post->GetReceiverId(), post->GetSenderType(), itemCount,
        itemPacketSize, itemPacket.data(), tradeMoney
    );
    return false;
}

bool RejectPost(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00befdf0 always returns false.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1436)
        L2_LOG_ERROR(Log, "Mail rejection request ignored: socket has no character");
        return false;
    }

    int mailId = 0;
    ::Disassemble(packet, "d", &mailId);
    CSPointer<CPost> post = user->m_pPostInfo->FindReceivedPostWithRef(mailId);
    if (!post) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No mail exists for user %s, mail %d. [RejectPost]",
        //     user->GetName(),
        //     mailId)
        // L2_LOG_WARN(Log,
        //     "Mail rejection request ignored: mail is missing, character=%s, mail ID=%d",
        //     ::WideToUtf8(user->GetName()).c_str(),
        //     mailId)
        L2_LOG_WARN(
            Log, "Mail rejection request ignored: mail is missing, character=%s, mail ID=%d",
            ::Utf16ToUtf8(user->GetName()).c_str(), mailId
        );
    } else if (post->IsReturnable()) {
        if (!post->StartDB()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"already Started db job. [rejectpost] %d",
            //     mailId)
            L2_LOG_WARN(Log, "Mail rejection request ignored: database job is already active, mail ID=%d", mailId);
        } else {
            db.RequestRejectPost(mailId, post->GetNextJobTime(), 1);
        }
    }
    return false;
}

bool DeleteSentPost(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bf002c always returns false.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1471)
        L2_LOG_ERROR(Log, "Sent mail deletion ignored: socket has no character");
        return false;
    }

    int requestedCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &requestedCount);
    const int availableCount = user->m_pPostInfo->GetSentListSize();
    if (availableCount < requestedCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid mail count for user %s, %d > %d",
        //     user->GetName(),
        //     requestedCount,
        //     availableCount)
        // L2_LOG_WARN(Log,
        //     "Sent mail deletion rejected: character=%s, requested count=%d exceeds available count=%d",
        //     ::WideToUtf8(user->GetName()).c_str(),
        //     requestedCount,
        //     availableCount)
        L2_LOG_WARN(
            Log, "Sent mail deletion rejected: character=%s, requested count=%d exceeds available count=%d",
            ::Utf16ToUtf8(user->GetName()).c_str(), requestedCount, availableCount
        );
        return false;
    }

    std::array<char, 0x5000> deletePacket{};
    int deletePacketSize = 0;
    int validCount = 0;
    for (int index = 0; index < requestedCount; ++index) {
        int mailId = 0;
        cursor = ::Disassemble(cursor, "d", &mailId);
        CSPointer<CPost> post = user->m_pPostInfo->FindSentPostWithRef(mailId);
        if (!post) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"No mail exists for user %s, mail %d. "
            //     L"[DeleteSentPost]",
            //     user->GetName(),
            //     mailId)
            // L2_LOG_WARN(Log,
            //     "Sent mail deletion skipped: mail is missing, character=%s, mail ID=%d",
            //     ::WideToUtf8(user->GetName()).c_str(),
            //     mailId)
            L2_LOG_WARN(
                Log, "Sent mail deletion skipped: mail is missing, character=%s, mail ID=%d",
                ::Utf16ToUtf8(user->GetName()).c_str(), mailId
            );
            continue;
        }

        ++validCount;
        const int assembled = ::Assemble(
            deletePacket.data() + deletePacketSize, static_cast<int>(deletePacket.size()) - deletePacketSize, "dd", mailId,
            post->GetNextJobTime()
        );
        if (assembled > 0) {
            deletePacketSize += assembled;
        }
    }
    if (validCount > 0) {
        db.RequestDeletePost(validCount, deletePacketSize, deletePacket.data(), 1, user->GetDBID(), 0, 1);
    }
    return false;
}

bool RequestSentPost(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bf02c0 always returns false.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x14ac)
        L2_LOG_ERROR(Log, "Sent mail open request ignored: socket has no character");
        return false;
    }

    int mailId = 0;
    ::Disassemble(packet, "d", &mailId);
    CSPointer<CPost> post = user->m_pPostInfo->FindSentPostWithRef(mailId);
    if (!post) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No mail exists for user %s, mail %d. [RequestSentPost]",
        //     user->GetName(),
        //     mailId)
        // L2_LOG_WARN(Log,
        //     "Sent mail open request rejected: mail is missing, character=%s, mail ID=%d",
        //     ::WideToUtf8(user->GetName()).c_str(),
        //     mailId)
        L2_LOG_WARN(
            Log, "Sent mail open request rejected: mail is missing, character=%s, mail ID=%d",
            ::Utf16ToUtf8(user->GetName()).c_str(), mailId
        );
    } else {
        db.RequestOpenPost(mailId, 0, post->GetNextJobTime());
    }
    return false;
}

bool CancelSendPost(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bf0448 always returns false.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x14c9)
        L2_LOG_ERROR(Log, "Mail cancellation request ignored: socket has no character");
        return false;
    }

    int mailId = 0;
    ::Disassemble(packet, "d", &mailId);
    CSPointer<CPost> post = user->m_pPostInfo->FindSentPostWithRef(mailId);
    if (!post || !user->m_pPostInfo->TryCancelPost(mailId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No mail exists for user %s, mail %d. [CancelSendPost]",
        //     user->GetName(),
        //     mailId)
        // L2_LOG_WARN(Log,
        //     "Mail cancellation rejected: mail is missing or cancellation could not start, character=%s, mail ID=%d",
        //     ::WideToUtf8(user->GetName()).c_str(),
        //     mailId)
        L2_LOG_WARN(
            Log, "Mail cancellation rejected: mail is missing or cancellation could not start, character=%s, mail ID=%d",
            ::Utf16ToUtf8(user->GetName()).c_str(), mailId
        );
        return false;
    }

    const int itemCount = user->m_pPostInfo->GetItemCount();
    PostItemInfo* const items = user->m_pPostInfo->GetItemInfo();
    if (!PostUtil::CheckCancelPost(baseSocket, user, itemCount, items, 0)) {
        user->m_pPostInfo->CancelTryCancelPost();
        return false;
    }
    if (!post->StartDB()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"already Started db job. [cancelpost] %d",
        //     mailId)
        L2_LOG_WARN(Log, "Mail cancellation stopped: database job is already active, mail ID=%d", mailId);
        user->m_pPostInfo->CancelTryCancelPost();
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        return false;
    }

    std::array<char, 0x5000> itemPacket{};
    int itemPacketSize = 0;
    ItemConsumeType consumeType = ICT_NORMAL;
    for (int index = 0; index < itemCount; ++index) {
        const PostItemInfo& itemInfo = items[static_cast<std::size_t>(index)];
        CItem* const item = static_cast<CItem*>(g_ObjectDB.GetObjectW(itemInfo.class_id, false));
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"No Item For GetConsumeType %d [CancelSendPost]",
            //     itemInfo.class_id)
            L2_LOG_ERROR(
                Log, "Mail cancellation item template is missing; retaining consume type=%d, item class=%d, mail ID=%d",
                static_cast<int>(consumeType), itemInfo.class_id, mailId
            );
        } else {
            consumeType = item->m_data->m_ItemConsumeType;
        }
        itemPacketSize += ::Assemble(
            itemPacket.data() + itemPacketSize, static_cast<int>(itemPacket.size()) - itemPacketSize, "dQd", itemInfo.db_id,
            itemInfo.amount, static_cast<int>(consumeType)
        );
    }

    db.RequestCancelPost(
        mailId, post->GetNextJobTime(), user->GetDBID(), post->GetReceiverId(), itemCount, itemPacketSize, itemPacket.data()
    );
    return false;
}

bool SendFriendList(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bfc4ec returns true only for a null user.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CSPointer<CFriends> friends = user->GetFriends();
    if (friends) {
        CPacketHelperEx<20480> friendPacket;
        std::vector<std::u16string> onlineFriends;
        std::vector<std::u16string> offlineFriends;
        friends->MakeFriendList(friendPacket, onlineFriends, offlineFriends);
        socket->Send("cdb", 0x58, friendPacket.GetAssembledCount(), friendPacket.GetLength(), friendPacket.GetPacket());
    }
    return false;
}

bool RequestReceivedPostList(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be2e3c always returns false.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x1354)
        L2_LOG_ERROR(Log, "Received mail list request ignored: socket has no character");
    } else if (user->m_data->m_nWorldId == g_nWorldId) {
        if (user->m_pPostInfo->CheckReceivedListDelay()) {
            db.RequestReceivedPostList(*user);
        }
    } else {
        socket->SendSystemMessage(SystemMessageId::FUNCTION_UNAVAILABLE_AFTER_DIMENSIONAL_TRANSFER);
    }
    return false;
}

bool RequestSentPostList(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be2f58 always returns false.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user null at file[%s], line[%d]",
        //     L"UserPacketEx.cpp",
        //     0x145e)
        L2_LOG_ERROR(Log, "Sent mail list request ignored: socket has no character");
    } else if (user->m_pPostInfo->CheckSentListDelay()) {
        db.RequestSentPostList(*user);
    }
    return false;
}

bool ShowPostFriend(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be3b90.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->m_pPostInfo->GetPostFriend().LoadAndSendPostFriend(*user);
    }
    return user == nullptr;
}

bool OnCheckSpeedHack(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be697c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t suspected = 0;
    ::Disassemble(packet, "d", &suspected);
    if (suspected != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Suspected a SPEED_HACK_USER[%s]",
        //     L"UserPacketEx.cpp",
        //     0x239b,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Client reported suspected speed hack, flag=%d, character=%s",
        //     suspected, ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_WARN(
            Log, "Client reported suspected speed hack, flag=%d, character=%s", suspected,
            ::Utf16ToUtf8(user->GetName()).c_str()
        );
    }
    return false;
}

bool RequestNotifyExitBeautyShop(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be702c.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->SetBeautyShop(false);
    }
    return user == nullptr;
}

bool AbilityWndOpen(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be7a3c.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        runtime::raw_atomic::ExchangeI32(&user->m_nAbilityClass, user->m_data->m_nSubJobId);
    }
    return user == nullptr;
}

bool AbilityWndClose(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be7b00.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        runtime::raw_atomic::ExchangeI32(&user->m_nAbilityClass, -1);
    }
    return user == nullptr;
}

bool RequestSetSelectedQuestZondID(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be8914. The exported source spelling is ZondID.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::int32_t zoneId = 0;
        ::Disassemble(packet, "d", &zoneId);
        user->SetSelectedQuestZoneID(zoneId);
    }
    return user == nullptr;
}

bool RequestInteractModify(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be92d4 sign-extends both decoded bytes.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::int8_t type = 0;
        std::int8_t value = 0;
        ::Disassemble(packet, "cc", &type, &value);
        user->SetInteractRejectType(static_cast<int>(type), static_cast<int>(value));
    }
    return user == nullptr;
}

bool RequestTryEnchantArtifact(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be93a8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (!user->TryEnchantArtifact(packet)) {
        socket->Send(
            "chddddd", 0xfe, 0x201,
            // 2,
            AER_CONDITION_UNSATISFIED, 0, 0, 0, 0
        );
    }
    return false;
}

bool RequestPledgeMissionReward(CSocket* baseSocket, const unsigned char* source) {
    // Current L2Server.exe 0x00bf3548.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CSPointer<User> retained(user, ObjectRefType::FunctionChain);
    protocol::C_EX_PLEDGE_MISSION_REWARD packet{};
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    if (packet.Decode(&stream)) {
        retained->PledgeMissionReward(packet.nMissionId_);
    }
    return false;
}

bool RequesPledgeBonusUIOpen(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00beb8f0.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!g_bUsePledgeBonus) {
        return false;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return false;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    pledge->ProcessBonusPoint(user, 0, 0, static_cast<int>(pledge->GetPledgeLevel()), now);

    const PledgeBonusInfo& access = pledge->m_PledgeBonus.m_PledgeBonus[0];
    const PledgeBonusInfo& hunt = pledge->m_PledgeBonus.m_PledgeBonus[1];
    const int accessAvailable = access.m_nTodayRewardLev > 0 &&
        user->CanResetTimeStamp(PledgeBonus::ACCESS_REWARD_RECEIVE, runtime::crt::Time32Now());
    const int huntAvailable = hunt.m_nTodayRewardLev > 0 &&
        user->CanResetTimeStamp(PledgeBonus::HUNT_REWARD_RECEIVE, runtime::crt::Time32Now());
    CPledgeBonusSetting& setting = CPledgeBonusSetting::Inst();
    socket->Send(
        "chdddccdddcc", 0xfe, 0x1ae, access.m_nMaxBonusPoint, access.m_nCurrentBonusPoint,
        setting.GetAccessRewardSkill(access.m_nTodayRewardLev, 0),
        static_cast<int>(static_cast<char>(access.m_nTodayRewardLev)), accessAvailable, hunt.m_nMaxBonusPoint,
        hunt.m_nCurrentBonusPoint, setting.GetHuntRewardItem(hunt.m_nTodayRewardLev),
        static_cast<int>(static_cast<char>(hunt.m_nTodayRewardLev)), huntAvailable
    );
    return false;
}

bool RequesPledgeBonusRewardList(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bebb90.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->GetPledge() == nullptr) {
        return false;
    }

    CPledgeBonusSetting& setting = CPledgeBonusSetting::Inst();
    socket->Send(
        "chdddddddd", 0xfe, 0x1af, setting.GetAccessRewardSkill(1, 0), setting.GetAccessRewardSkill(2, 0),
        setting.GetAccessRewardSkill(3, 0), setting.GetAccessRewardSkill(4, 0), setting.GetHuntRewardItem(1),
        setting.GetHuntRewardItem(2), setting.GetHuntRewardItem(3), setting.GetHuntRewardItem(4)
    );
    return false;
}

bool RequesPledgeBonusReceiveReward(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bfb9ac.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    char rewardType = 0;
    ::Disassemble(packet, "c", &rewardType);
    CPledge* const pledge = user->GetPledge();
    const std::int32_t now = runtime::crt::Time32Now();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [PledgeBonus] No Pledge CharID[%d]",
        //     L"UserPacketEx.cpp",
        //     0x3134,
        //     static_cast<int>(user->GetDBID()))
        L2_LOG_WARN(
            Log, "Clan bonus reward rejected: character has no clan, character ID=%u, reward type=%d",
            static_cast<unsigned>(user->GetDBID()), static_cast<int>(rewardType)
        );
        return false;
    }

    CPledgeBonusSetting& setting = CPledgeBonusSetting::Inst();
    if (rewardType == 0) {
        if (!user->CanResetTimeStamp(PledgeBonus::ACCESS_REWARD_RECEIVE, now)) {
            user->SendSystemMessage(SystemMessageId::PRODUCT_ALREADY_RECEIVED);
        } else {
            const int rewardLevel = pledge->ProcessBonusPoint(user, 0, 0, static_cast<int>(pledge->GetPledgeLevel()), now);
            if (static_cast<unsigned int>(rewardLevel - 1) >= 4U) {
                user->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
            } else {
                const int rewardId = setting.GetAccessRewardSkill(rewardLevel, 0);
                const CSkillKey skillKey(
                    rewardId, setting.GetAccessRewardSkill(rewardLevel, 1), setting.GetAccessRewardSkill(rewardLevel, 2)
                );
                const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(skillKey, nullptr);
                if (skill != nullptr) {
                    skill->ActivateSkill(user, user, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
                    user->tPledgeBonusTimeStamp[PledgeBonus::ACCESS_REWARD_RECEIVE] = now;
                    CLogFormatter formatter;
                    formatter.WriteLog1_1(
                        0x817, user, nullptr, pledge->GetPledgeName(), nullptr, nullptr, pledge->GetDbId(),
                        pledge->GetPledgeLevel(), 0, rewardId, 0, 0, 0
                    );
                    db.RequestUpdateUserPledgeBonusReward(user, 1, now);
                }
            }
        }
    } else if (rewardType == 1) {
        if (!user->CanResetTimeStamp(PledgeBonus::HUNT_REWARD_RECEIVE, now)) {
            user->SendSystemMessage(SystemMessageId::PRODUCT_ALREADY_RECEIVED);
        } else {
            const int rewardLevel = pledge->ProcessBonusPoint(user, 1, 0, static_cast<int>(pledge->GetPledgeLevel()), now);
            if (static_cast<unsigned int>(rewardLevel - 1) >= 4U) {
                user->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
            } else {
                const int rewardId = setting.GetHuntRewardItem(rewardLevel);
                CObject* const object = g_ObjectDB.GetObjectW(rewardId, false);
                CItem* const item = object != nullptr && object->IsItem() ? static_cast<CItem*>(object) : nullptr;
                if (item == nullptr) {
                    socket->SendSystemMessage(u"SYS", u"Try to give Invalid Item");
                } else {
                    const int itemWeight = item->m_data->m_ItemConsumeType == ICT_ASSET ? 0 : item->m_const->m_nWeight;
                    const bool inventoryFull = static_cast<float>(user->GetInventoryLimit()) * 0.9F <
                            static_cast<float>(user->m_inv.GetInventoryCount() + 1) ||
                        static_cast<float>(static_cast<int>(user->GetWeightLimit(69000.0))) * 0.8F <
                            static_cast<float>(runtime::AddInt32Wrapping(user->m_inv.GetCarryingWeight(), itemWeight));
                    if (inventoryFull) {
                        user->SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_CLAN_BONUS_REWARD_INVENTORY_LIMIT_EXCEEDED);
                    } else {
                        std::optional<InventoryParams> parametersStorage;
                        {
                            L2_GUARD(user->m_ItemManipulationLock);
                            InventoryParams& parameters = parametersStorage.emplace();
                            parameters.nItemClassID = rewardId;
                            parameters.nCount = 1;
                            user->AddItemToInventory(parameters);
                        }
                        user->tPledgeBonusTimeStamp[PledgeBonus::HUNT_REWARD_RECEIVE] = now;
                        CLogFormatter formatter;
                        formatter.WriteLog1_1(
                            0x817, user, nullptr, pledge->GetPledgeName(), nullptr, nullptr, pledge->GetDbId(),
                            pledge->GetPledgeLevel(), 1, rewardId, 0, 0, 0
                        );
                        db.RequestUpdateUserPledgeBonusReward(user, 2, now);
                    }
                }
            }
        }
    }

    const PledgeBonusInfo& access = pledge->m_PledgeBonus.m_PledgeBonus[0];
    const PledgeBonusInfo& hunt = pledge->m_PledgeBonus.m_PledgeBonus[1];
    const int accessAvailable = access.m_nTodayRewardLev > 0 &&
        user->CanResetTimeStamp(PledgeBonus::ACCESS_REWARD_RECEIVE, runtime::crt::Time32Now());
    const int huntAvailable = hunt.m_nTodayRewardLev > 0 &&
        user->CanResetTimeStamp(PledgeBonus::HUNT_REWARD_RECEIVE, runtime::crt::Time32Now());
    socket->Send(
        "chdddccdddcc", 0xfe, 0x1ae, access.m_nMaxBonusPoint, access.m_nCurrentBonusPoint,
        setting.GetAccessRewardSkill(access.m_nTodayRewardLev, 0),
        static_cast<int>(static_cast<char>(access.m_nTodayRewardLev)), accessAvailable, hunt.m_nMaxBonusPoint,
        hunt.m_nCurrentBonusPoint, setting.GetHuntRewardItem(hunt.m_nTodayRewardLev),
        static_cast<int>(static_cast<char>(hunt.m_nTodayRewardLev)), huntAvailable
    );
    return false;
}

bool RequestPledgeMissionInfo(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bf3444.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    CSPointer<User> retained(user, ObjectRefType::FunctionChain);
    retained->PledgeMissionSendToUserIfChanged();
    return false;
}

bool EnchantOne(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be7d6c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int itemServerId = 0;
    ::Disassemble(packet, "d", &itemServerId);
    socket->Send("ch", S_EX, user->OnEnchantOne(itemServerId) ? S_EX_ENCHANT_ONE_OK : S_EX_ENCHANT_ONE_FAIL);
    return false;
}

bool EnchantOneRemove(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be7e7c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int itemServerId = 0;
    ::Disassemble(packet, "d", &itemServerId);
    socket->Send(
        "ch", S_EX, user->OnEnchantOneRemove(itemServerId) ? S_EX_ENCHANT_ONE_REMOVE_OK : S_EX_ENCHANT_ONE_REMOVE_FAIL
    );
    return false;
}

bool EnchantTwo(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be7f8c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int itemServerId = 0;
    ::Disassemble(packet, "d", &itemServerId);
    socket->Send("ch", S_EX, user->OnEnchantTwo(itemServerId) ? S_EX_ENCHANT_TWO_OK : S_EX_ENCHANT_TWO_FAIL);
    return false;
}

bool EnchantTwoRemove(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be809c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int itemServerId = 0;
    ::Disassemble(packet, "d", &itemServerId);
    socket->Send(
        "ch", S_EX, user->OnEnchantTwoRemove(itemServerId) ? S_EX_ENCHANT_TWO_REMOVE_OK : S_EX_ENCHANT_TWO_REMOVE_FAIL
    );
    return false;
}

bool EnchantTry(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00be81ac, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    static_cast<void>(user->OnEnchantTry());
    return false;
}

bool EnchantRetryToPutItems(CSocket* baseSocket, const unsigned char* source) {
    // Current L2Server 0x00bf1a6c.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    protocol::C_EX_ENCHANT_RETRY_TO_PUT_ITEMS_INFO packet{};
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    if (!packet.Decode(&stream)) {
        return false;
    }

    if (user->OnEnchantRetryToPutItems(packet)) {
        socket->Send("ch", S_EX, S_EX_ENCHANT_RETRY_TO_PUT_ITEMS_OK);
    } else {
        socket->Send("ch", S_EX, S_EX_ENCHANT_RETRY_TO_PUT_ITEMS_FAIL);
    }
    return false;
}

bool Request_C_EX_DECO_NPC_INFO(CSocket* baseSocket, const unsigned char* source) {
    // Current L2Server 0x00bfa1e8.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const rawUser = socket->GetUser();
    if (rawUser == nullptr) {
        return true;
    }

    protocol::C_EX_DECO_NPC_INFO request{};
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    if (!request.Decode(&stream)) {
        return false;
    }

    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);
    if (InstantAgitUtil::IsInInstantAgit(user)) {
        SendInstantAgitDecoNpcInfo(user);
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(request.nAgitCID);
    if (residence == nullptr || residence->m_data == nullptr) {
        socket->SendSystemMessage(u"SYS", u"invalid agit");
        SendDecoNpcInfoError(user);
        return false;
    }
    if (!ResidenceUtil::HasRightToSetDecoNpc(residence, user)) {
        socket->SendSystemMessage(u"SYS", u"no right");
        SendDecoNpcInfoError(user);
        return false;
    }

    protocol::S_EX_DECO_NPC_INFO response{};
    response.nAgitCID = request.nAgitCID;
    const std::int32_t currentTime = runtime::crt::Time32Now();
    for (int slot = 0; slot < residence->m_data->m_nDecoNpcSlotCount; ++slot) {
        const auto& npc = residence->m_data->m_aDecoNpc[slot];
        protocol::DecoNpcInfo info{};
        info.cSlot = static_cast<char>(slot);
        info.nDecoCID = npc.nCID;
        info.nExpire = npc.nCID == 0 ? 0 : npc.tExpire - currentTime;
        response.vDecoNpcs.push_back(info);
    }
    response.nResidenceGrade = static_cast<char>(residence->m_data->m_nResidenceGrade);
    for (int index = 0; index < residence->m_data->m_nResidenceDomainCount; ++index) {
        response.vResidenceDomains.push_back(static_cast<char>(residence->m_data->m_aResidenceDomains[index]));
    }
    socket->Send(S_EX, S_EX_DECO_NPC_INFO, response);
    return false;
}

bool Request_C_EX_DECO_NPC_SET(CSocket* baseSocket, const unsigned char* source) {
    // Current L2Server 0x00c08508.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const rawUser = socket->GetUser();
    if (rawUser == nullptr) {
        return true;
    }

    protocol::C_EX_DECO_NPC_SET request{};
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    if (!request.Decode(&stream)) {
        return false;
    }

    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);
    auto& channel = TalkChannelCache::inst().mChannels[21];
    if (!channel.IsValidChannel(user->Id(), channel.Get(user->Id()), 0xfa, 0)) {
        user->SendSystemMessage(SystemMessageId::EXPERT_PLACEMENT_OR_REMOVAL_FAILED_INVALID_PROCEDURE);
        return false;
    }

    CSPointer<CPledge> pledge(user->GetPledge(), ObjectRefType::FunctionChain);
    if (!pledge) {
        socket->SendSystemMessage(u"SYS", u"no pledge");
        return false;
    }

    const bool set = request.nDecoCID != 0;
    if (InstantAgitUtil::IsInInstantAgit(user)) {
        if (set) {
            CInstantAgitManager::inst().TryAddInstantAgitDecoNpc(user, request.nDecoCID, static_cast<int>(request.cSlot));
        } else {
            CInstantAgitManager::inst().TryRemoveInstantAgitDecoNpc(user, static_cast<int>(request.cSlot));
        }
    } else if (set) {
        SetDecoNpc(user, request.nAgitCID, request.nDecoCID, static_cast<int>(request.cSlot));
    } else {
        RemoveDecoNpc(user, request.nAgitCID, static_cast<int>(request.cSlot));
    }
    return false;
}

bool RequestFactionInfo(CSocket* baseSocket, const unsigned char* source) {
    // Current L2Server 0x00bf245c does not require the
    // requesting socket to own the queried character.
    L2_GUARD_FUNCTION();
    static_cast<void>(baseSocket);

    protocol::C_EX_FACTION_INFO request{};
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    if (!request.Decode(&stream)) {
        return false;
    }

    CSPointer<User> user = g_UserMap.FindObject(request.nUserID);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"C_EX_FACTION_INFO - UserPacketEx::RequestFactionInfo() InValid UserID")
        L2_LOG_WARN(
            Log, "Faction info request rejected: requested user ID=%u is unavailable", static_cast<unsigned>(request.nUserID)
        );
        return false;
    }
    user->SendFactionInfoList(static_cast<int>(request.nRequestType));
    return false;
}

bool RequestRaidBossSpawnInfo(CSocket* baseSocket, const unsigned char* source) {
    // Current L2Server 0x00bfa5d4.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    protocol::C_EX_RAID_BOSS_SPAWN_INFO request{};
    protocol::S_EX_RAID_BOSS_SPAWN_INFO response{};
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    if (!request.Decode(&stream)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RequestRaidBossSpawnInfo Decode failed User[%s:%d]",
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_WARN(Log,
        //     "Raid boss spawn info request could not be decoded, character=%s, character ID=%u",
        //     ::WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID()))
        L2_LOG_WARN(
            Log, "Raid boss spawn info request could not be decoded, character=%s, character ID=%u",
            ::Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    if (request.vRaidNpcIds.size() >= 101) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Request NPC id Count over 100 User[%s:%d]",
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_WARN(Log,
        //     "Raid boss spawn info request rejected: NPC count=%zu exceeds limit=100, character=%s, character ID=%u",
        //     request.vRaidNpcIds.size(), ::WideToUtf8(user->GetName()).c_str(),
        //     static_cast<unsigned>(user->GetDBID()))
        L2_LOG_WARN(
            Log, "Raid boss spawn info request rejected: NPC count=%zu exceeds limit=100, character=%s, character ID=%u",
            request.vRaidNpcIds.size(), ::Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    for (const std::int32_t npcId : request.vRaidNpcIds) {
        if (static_cast<std::uint32_t>(npcId) - 1U < 999999U) {
            if (g_UniqNPCDB.GetBossNPC(0, npcId + 1000000) != nullptr) {
                response.vRaidSpawnNpcIds.push_back(npcId);
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Invalid NPC Id  %d [%s:%d]",
            //     npcId,
            //     user->GetName(),
            //     static_cast<int>(user->GetDBID()))
            // L2_LOG_WARN(Log,
            //     "Raid boss spawn info skipped invalid NPC ID=%d (expected 1..999999), character=%s, character ID=%u",
            //     npcId,
            //     ::WideToUtf8(user->GetName()).c_str(),
            //     static_cast<unsigned>(user->GetDBID()))
            L2_LOG_WARN(
                Log, "Raid boss spawn info skipped invalid NPC ID=%d (expected 1..999999), character=%s, character ID=%u",
                npcId, ::Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID())
            );
        }
    }
    socket->Send(S_EX, S_EX_RAID_BOSS_SPAWN_INFO, response);
    return false;
}

bool RequestUpgradeSystem(CSocket* baseSocket, const unsigned char* source) {
    // Current L2Server 0x00bf29fc.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const rawUser = socket->GetUser();
    if (rawUser == nullptr) {
        return true;
    }

    protocol::C_EX_UPGRADE_SYSTEM_REQUEST request{};
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    if (!request.Decode(&stream)) {
        return false;
    }
    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);
    user->RequestUpgradeSystem(request);
    return false;
}

bool RequestLockedItem(CSocket* baseSocket, const unsigned char* source) {
    // Current L2Server 0x00bf2b4c.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const rawUser = socket->GetUser();
    if (rawUser == nullptr) {
        return true;
    }

    protocol::C_EX_REQUEST_LOCKED_ITEM request{};
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    if (!request.Decode(&stream)) {
        return false;
    }
    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);
    user->RequestLockedItem(request);
    return false;
}

bool RequestUnLockedItem(CSocket* baseSocket, const unsigned char* source) {
    // Current L2Server 0x00bf2c9c.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const rawUser = socket->GetUser();
    if (rawUser == nullptr) {
        return true;
    }

    protocol::C_EX_REQUEST_UNLOCKED_ITEM request{};
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    if (!request.Decode(&stream)) {
        return false;
    }
    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);
    user->RequestUnLockedItem(request);
    return false;
}

bool RequestLockedItemCancel(CSocket* baseSocket, const unsigned char* source) {
    // Current L2Server 0x00bf2dec.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const rawUser = socket->GetUser();
    if (rawUser == nullptr) {
        return true;
    }

    protocol::C_EX_LOCKED_ITEM_CANCEL request{};
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    if (!request.Decode(&stream)) {
        return false;
    }
    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);
    user->SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
    protocol::S_EX_LOCKED_RESULT response{};
    response.lock = 0;
    response.result = 0;
    socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
    return false;
}

bool RequestUnLockedItemCancel(CSocket* baseSocket, const unsigned char* source) {
    // Current L2Server 0x00bf2f80.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const rawUser = socket->GetUser();
    if (rawUser == nullptr) {
        return true;
    }

    protocol::C_EX_UNLOCKED_ITEM_CANCEL request{};
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    if (!request.Decode(&stream)) {
        return false;
    }
    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);
    user->SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
    protocol::S_EX_LOCKED_RESULT response{};
    response.lock = 1;
    response.result = 0;
    socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
    return false;
}

bool NotifyStartMinigamePacket(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00be2d54.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        CLogFormatter formatter;
        formatter.WriteLog2_1(0x7c, user, nullptr, 0, 0, 0, 0, 0, 0, 0);
    }
    return user == nullptr;
}

bool CleftEnter(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bea488.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t team = 0;
    ::Disassemble(packet, "d", &team);
    CCleft& cleft = Singleton<CCleft>::inst();
    if (team == -1) {
        cleft.RemoveCleftPlayer(static_cast<int>(user->Id()));
    } else {
        cleft.AddCleftPlayer(static_cast<BlueRedTeam::Enum>(team != 0), *user);
    }
    return false;
}

bool RequestRegisterXmasWish(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c08c04.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<char16_t, 256> wish{};
    ::Disassemble(packet, "S", 0x200, wish.data());
    CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_ONETIME);
    auto* const oneTimeEvent = static_cast<CEvent_OneTimeEvent*>(event);
    if (oneTimeEvent == nullptr) {
        return false;
    }

    const XmasWishCardResult::Enum result = oneTimeEvent->IsUserReceived(static_cast<int>(user->GetDBID()));
    const auto hasCapacity = [user]() {
        return static_cast<double>(user->m_inv.GetCarryingWeight()) / user->CarryWeight() < 0.8 &&
            user->m_inv.GetTotalCount() < user->GetInventoryLimit();
    };

    if (result == XmasWishCardResult::XWCR_UNKNOWN) {
        if (hasCapacity()) {
            user->SendDB("chdS", 0xea, 0x170, user->GetDBID(), wish.data());
            return false;
        }

        if (!hasCapacity()) {
            user->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_WISH_OR_RECEIVE_REWARD_INVENTORY_RESTRICTION);
            socket->Send("chd", 0xfe, 0x14c, 1);
        } else {
            user->SendSystemMessage(SystemMessageId::WISH_REGISTRATION_FAILED);
        }
    } else if (result == XmasWishCardResult::XWCR_ALREADY_RECEIVED) {
        user->SendSystemMessage(SystemMessageId::WISH_ALREADY_REGISTERED_AND_REWARDED);
        socket->Send("chd", 0xfe, 0x14c, 2);
    } else {
        user->SendSystemMessage(SystemMessageId::WISH_REGISTRATION_FAILED);
    }
    return false;
}

bool RequestRewardCardCollectSystem(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bfdf60.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::int32_t id = 0;
        ::Disassemble(packet, "d", &id);
        Singleton<CCardCollectSystem>::inst().CheckAndReward(*user, id);
    }
    return user == nullptr;
}

bool RequestStartLuckyGame(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c03cf8.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t gameType = 0;
    ::Disassemble(packet, "d", &gameType);
    if (TalkChannelCache::inst().mChannels[19].IsValidChannel(user->Id(), user->m_spTalkTarget.GetId(), 0xfa, 0)) {
        CObject* const target = user->m_spTalkTarget.GetObjectW();
        if (target != nullptr) {
            if (!user->IsTalkableDistance(static_cast<CWorldObject*>(target))) {
                CUserSocket* const userSocket = user->GetSocket();
                if (userSocket != nullptr) {
                    userSocket->SendSystemMessage(SystemMessageId::NPC_TOO_FAR);
                }
                // Log.Add(
                //     LOG_ERROR,
                //     L"showLuckyGame Error too distant")
                L2_LOG_WARN(
                    Log, "Lucky Game start rejected: NPC is outside talking distance, character ID=%u",
                    static_cast<unsigned>(user->GetDBID())
                );
            } else {
                user->SendStartLuckyGame(gameType);
            }
        }
    }
    return false;
}

bool RequestBettingLuckyGame(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c03e60.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t gameType = 0;
    std::int32_t ticketCount = 0;
    ::Disassemble(packet, "dd", &gameType, &ticketCount);

    const auto sendFullResult = [user, gameType](const int result, const int rewardIndex, const int rewardCount) {
        user->GetSocket()->Send("chdddd", 0xfe, 0x161, result, gameType, rewardIndex, rewardCount);
    };
    const auto sendShortResult = [user, gameType](const int result) {
        user->GetSocket()->Send("chdd", 0xfe, 0x161, result, gameType);
    };

    if (!TalkChannelCache::inst().mChannels[19].IsValidChannel(user->Id(), user->m_spTalkTarget.GetId(), 0xfa, 0)) {
        sendFullResult(-3, -1, 0);
        return false;
    }

    CObject* const target = user->m_spTalkTarget.GetObjectW();
    if (target == nullptr) {
        sendFullResult(-3, -1, 0);
        return false;
    }
    if (!user->IsTalkableDistance(static_cast<CWorldObject*>(target))) {
        CUserSocket* const userSocket = user->GetSocket();
        if (userSocket != nullptr) {
            userSocket->SendSystemMessage(SystemMessageId::NPC_TOO_FAR);
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR]showLuckyGame Error too distant")
        L2_LOG_WARN(
            Log, "Lucky Game bet rejected: NPC is outside talking distance, character ID=%u",
            static_cast<unsigned>(user->GetDBID())
        );
        sendFullResult(-3, -1, 0);
        return false;
    }

    if (ticketCount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][LuckyGame] Ticket count is not positive number (%d)")
        L2_LOG_WARN(
            Log, "Lucky Game bet rejected: ticket count=%d must be positive, character ID=%u", ticketCount,
            static_cast<unsigned>(user->GetDBID())
        );
        sendFullResult(0, 0, 0);
        return false;
    }

    LuckyGameSystem::BettingType bettingType = LuckyGameSystem::BT_NONE;
    int ticketClassId = 0;
    if (gameType == 1) {
        bettingType = LuckyGameSystem::BT_COMMON;
        ticketClassId = 0x5cd7;
    } else if (gameType == 2) {
        bettingType = LuckyGameSystem::BT_PREMIUM;
        ticketClassId = 0x5cd8;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][%s][%d][LuckyGame] no game type (%d)",
        //     L"UserPacketEx.cpp",
        //     0x28e1,
        //     gameType)
        L2_LOG_WARN(
            Log, "Lucky Game bet rejected: unsupported game type=%d (expected 1 or 2), character ID=%u", gameType,
            static_cast<unsigned>(user->GetDBID())
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][LuckyGame] Fail by Ticket ")
        // Noise: the preceding warning already identifies the unsupported game type.
        // L2_LOG_ERROR(Log,
        //     L"[ERROR][LuckyGame] Fail by Ticket ")
        sendFullResult(-1, 0, 0);
        return false;
    }

    if (!user->m_inv.HaveItemByClassId(ticketClassId, ticketCount, false, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][LuckyGame] Fail by Ticket ")
        L2_LOG_WARN(
            Log, "Lucky Game bet rejected: required tickets are unavailable, ticket class=%d, count=%d, character ID=%u",
            ticketClassId, ticketCount, static_cast<unsigned>(user->GetDBID())
        );
        sendFullResult(-1, 0, 0);
        return false;
    }

    if (user->m_data->m_nNumInventory < 0) {
        sendFullResult(-2, 0, 0);
        return false;
    }
    if (user->GetInventoryLimit() < user->m_data->m_nNumInventory + ticketCount || user->m_inv.GetCarryingWeight() < 0 ||
        user->CastCreature()->m_nCurrentWeight > 2) {
        user->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        sendShortResult(-2);
        return false;
    }

    if (ticketCount < 0x33) {
        if (ticketCount < 1) {
            sendShortResult(-4);
            return false;
        }

        int result = 0;
        CLuckyGameSystem* const system = CLuckyGameSystem::Inst();
        if (system->AcquirePickLuckyGameFlag()) {
            L2_GUARD(*system, (scoped_lock_detail::AdoptOwnerMethod<&CLuckyGameSystem::ReleasePickLuckyGameFlag>{}));
            if (!system->GetLuckyGameResult(user, bettingType, ticketCount, result)) {
                sendFullResult(result, 0, 0);
            }
            return false;
        }

        user->SendSystemMessage(SystemMessageId::OPERATION_NOT_PROCESSED_TRY_AGAIN);
        sendFullResult(result, 0, 0);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][LuckyGame] too many turn (%d) ",
        //     ticketCount)
        L2_LOG_WARN(
            Log, "Lucky Game bet rejected: ticket count=%d exceeds limit=50, character ID=%u", ticketCount,
            static_cast<unsigned>(user->GetDBID())
        );
        user->SendSystemMessage(SystemMessageId::WISH_REGISTRATION_FAILED);
        sendShortResult(-4);
    }
    return false;
}

bool RequestTrainingZoneLeaving(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00be7bbc.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_TrainingState == 1) {
        user->EndTraining(TrainingZone::BY_USER, runtime::crt::Time32Now());
    }
    return false;
}

bool RequesToDoList(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be832c.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (g_L2ServerType != L2ServerType::WORLDSERVER && g_bUseOneDayReward) {
        std::int8_t type = 0;
        std::int8_t entering = 0;
        ::Disassemble(packet, "cc", &type, &entering);
        user->SendToDoList(static_cast<E_TODOLIST_TYPE>(type), entering != 0);
    }
    return false;
}

bool RequesToDoListHtml(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be8430.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (g_bUseOneDayReward) {
        std::int8_t type = 0;
        std::array<char16_t, 200> name{};
        ::Disassemble(packet, "cS", &type, 400, name.data());
        const std::wstring hostName = Utf16ToWide(std::u16string_view(name.data()));
        user->SendToDoListHtml(static_cast<E_TODOLIST_TYPE>(type), hostName.c_str());
    }
    return false;
}

bool RequestOneDayReceiveReward(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be8554.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (g_L2ServerType != L2ServerType::WORLDSERVER && g_bUseOneDayReward) {
        std::int16_t databaseId = 0;
        ::Disassemble(packet, "h", &databaseId);
        if (!g_ToDoListManager.IsOneDayRewardData(databaseId) || !user->m_bLoadDBOneDayReward) {
            user->SendSystemMessage(SystemMessageId::TRY_AGAIN_LATER);
        } else if (runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 1) != 1) {
            return user->OnRequestOneDayReceiveReward(databaseId);
        } else {
            user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        }
    }
    return false;
}

bool RequestPickCardUpdownGame(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf3114.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    CSPointer<User> user(socket->GetUser(), ObjectRefType::FunctionChain);
    if (user) {
        std::int16_t card = 0;
        ::Disassemble(packet, "h", &card);
        user->PickCardOnCardUpDownGame(card);
    }
    return !user;
}

bool RequestCardUpdownGameReward(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00bf323c.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    CSPointer<User> user(socket->GetUser(), ObjectRefType::FunctionChain);
    if (user) {
        user->TryGetCardUpdownGameReward();
    }
    return !user;
}

bool RequestRetryCardUpdownGame(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00bfec24.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    CSPointer<User> user(socket->GetUser(), ObjectRefType::FunctionChain);
    if (!user) {
        return true;
    }

    CObject* const target = user->m_spTalkTarget.GetObjectW();
    CardUpDownGame::GameData* const data = CardUpDownGame::Script::inst().GetLatestGameData();
    if (target != nullptr && data != nullptr) {
        if (target->GetClassId() == data->m_nNpcId && user->GetInZoneID() == target->GetInZoneID()) {
            user->ShowCardUpdownGame(static_cast<int>(target->Id()), data->m_nId);
        } else {
            user->QuitCardUpDownGame();
        }
    }
    return false;
}

bool RequestQuestNPCLogList(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be363c, original owner UserPacketEx.cpp.
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    std::int32_t questId = 0;
    ::Disassemble(packet, "d", &questId);
    user->SendQuestNPCLogList(questId, nullptr);
    return false;
}

bool RequestWebSessionID(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00be4770, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    const in_addr address = socket->GetAddress();
    authd.RequestWebSessionID(static_cast<int>(user->GetAccountID()), static_cast<int>(address.s_addr));
    return false;
}

bool RequestChangeAttributePacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be5838, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    std::int32_t first = 0;
    std::int32_t second = 0;
    std::int32_t third = -1;
    ::Disassemble(packet, "ddd", &first, &second, &third);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    return user == nullptr ? true : user->OnRequestChangeAttributePacket(first, second, third);
}

bool RequestKalieToken(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00be6890, original owner UserPacketEx.cpp.
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    static_cast<void>(user->OnRequestKalieToken());
    return false;
}

bool RequestBalthusToken(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00be68bc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        static_cast<void>(user->OnRequestBalthusToken());
    }
    return user == nullptr;
}

bool RequestShowCardCollectSystem(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00be86bc, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const std::uint32_t now = runtime::GetTickCount();
    if (user->m_nSendShowCardCollectSystemTime == 0 ||
        runtime::Int32FromBits(now - static_cast<std::uint32_t>(user->m_nSendShowCardCollectSystemTime)) > 499) {
        user->m_nSendShowCardCollectSystemTime = runtime::Int32FromBits(now);
        user->OnShowCardCollectSystem();
    }
    return false;
}

bool RequestAutoFishing(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be8b5c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        std::int8_t start = 0;
        ::Disassemble(packet, "c", &start);
        user->OnRequestAutoFishing(start == 1);
    }
    return user == nullptr;
}

bool ShowPCCafeHTML(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00bf18a0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const wchar_t* const fileName = g_nCountryCode == 8 || g_nCountryCode == 9 || g_nCountryCode == 12
        ? L"pccafe_item002_ru.htm"
        : L"pccafe_item002.htm";
    std::u16string html;
    if (!g_HTMLCache.GetHTMLFile(WideToUtf16(fileName).c_str(), html, user->m_data->m_CountryCode)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No fileName UserName[%s], fileName[%s]",
        //     user->GetName(),
        //     fileName)
        // L2_LOG_ERROR(Log,
        //     "PC cafe HTML lookup failed: character=%s, file=%s, country=%d",
        //     ::WideToUtf8(user->GetName()).c_str(), ::WideToUtf8(fileName).c_str(),
        //     static_cast<int>(user->m_data->m_CountryCode))
        L2_LOG_ERROR(
            Log, "PC cafe HTML lookup failed: character=%s, file=%s, country=%d", ::Utf16ToUtf8(user->GetName()).c_str(),
            ::WideToUtf8(fileName).c_str(), static_cast<int>(user->m_data->m_CountryCode)
        );
    } else {
        const std::u16string hostHtml(runtime::StringView(html));
        user->ShowHTML(WideToUtf16(fileName).c_str(), hostHtml.c_str(), 0, HTML::LAUNCH, false);
    }
    return false;
}

bool RequestShowRegistBeauty(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c01c98, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t state = Beauty::STATEMAX;
    ::Disassemble(packet, "d", &state);
    if (static_cast<std::uint32_t>(state) >= Beauty::STATEMAX) {
        return false;
    }
    if (!g_npcserver.IsConnect()) {
        socket->SendSystemMessage(SystemMessageId::BEAUTY_SHOP_UNAVAILABLE_NPC_SERVER_DOWN);
        return false;
    }
    if (!TalkChannelCache::inst().mChannels[17].IsValidChannel(user->Id(), user->m_spTalkTarget.GetId(), 0xfa, 0) ||
        user->m_BeautyDelay == 1) {
        return false;
    }

    runtime::raw_atomic::ExchangeI32(&user->m_BeautyDelay, 1);
    user->m_nBeautyTickCount = 0;
    if (user->m_BeautyPurchase.m_nLoaded == 1) {
        CBeautyManager::Inst()->GetDataList(user->GetServerId(), static_cast<Beauty::BeautyState>(state));
    } else {
        CBeautyManager::Inst()->RequestBeautyPurchaseList(*user, static_cast<char>(state));
    }
    return false;
}

bool RequestRegistBeauty(CSocket* const baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c01e90, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const auto sendResult = [&](const int result) {
        const int hairShape = user->m_data->m_nHairShapeEx != 0 ? user->m_data->m_nHairShapeEx : user->m_data->m_nHairShape;
        const int hairColor = user->m_data->m_nHairColorEx != 0 ? user->m_data->m_nHairColorEx : user->m_data->m_nHairColor;
        const int face = user->m_data->m_nFaceEx != 0 ? user->m_data->m_nFaceEx : user->m_data->m_nFace;
        const std::int64_t beautyCoin = user->m_inv.GetItemAmount(0x8dd4, 0, true);
        const std::int64_t adena = user->m_inv.GetAdenaAmount();
        socket->Send("chQQddddd", 0xfe, 0x136, adena, beautyCoin, 0, result, face, hairColor, hairShape);
    };
    const auto refreshHair = [&]() { CBeautyManager::Inst()->GetDataList(user->GetServerId(), Beauty::HAIR); };

    if (!g_npcserver.IsConnect()) {
        // sendResult(-1)
        sendResult(Beautyshop::BSRT_NPCSERVER_DISCONNECTED);
        refreshHair();
        return false;
    }
    if (!TalkChannelCache::inst().mChannels[17].IsValidChannel(user->Id(), user->m_spTalkTarget.GetId(), 0xfa, 0)) {
        // sendResult(0)
        sendResult(Beautyshop::BSRT_FAIL);
        refreshHair();
        return false;
    }
    if (user->m_BeautyPurchase.m_nLoaded != 1) {
        // sendResult(0)
        sendResult(Beautyshop::BSRT_FAIL);
        refreshHair();
        return false;
    }

    std::int32_t face = -1;
    std::int32_t hairShape = -1;
    std::int32_t hairColor = -1;
    // Current 0x00c02373 previews the three fields without advancing r15.
    // the per-state loop at 0x00c024de reads the same packet from its start.
    ::Disassemble(packet, "ddd", &face, &hairShape, &hairColor);
    if (runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(face, hairShape), hairColor) == -3) {
        user->SendSystemMessage(SystemMessageId::STYLE_CHANGE_FAILED);
        // sendResult(0)
        sendResult(Beautyshop::BSRT_FAIL);
        refreshHair();
        return false;
    }
    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) == 1) {
        user->SendSystemMessage(SystemMessageId::FINISH_PREVIOUS_OPERATION_BEFORE_RETRYING);
        // sendResult(0)
        sendResult(Beautyshop::BSRT_FAIL);
        refreshHair();
        return false;
    }

    CPacketHelperEx<20480> boughtItems;
    CPacketHelperEx<20480> ownedItems;
    std::int64_t adenaPrice = 0;
    std::int64_t beautyCoinPrice = 0;
    int colorPrimary = -1;
    for (int state = Beauty::HAIR; state < Beauty::STATEMAX; ++state) {
        std::int32_t secondary = -1;
        packet = ::Disassemble(packet, "d", &secondary);
        // Current 0x00c024f0..0x00c0250c keeps unchanged selections in
        // the owned section without looking them up or purchasing them.
        if (secondary == -1) {
            ownedItems.Assemble("dd", state, secondary);
            continue;
        }
        const int primary = state == Beauty::COLOR ? colorPrimary : 0;
        if (state == Beauty::HAIR) {
            colorPrimary = secondary;
        }
        if (user->HaveBeautyItem(state, primary, secondary)) {
            ownedItems.Assemble("dd", state, secondary);
            continue;
        }
        if (!CBeautyManager::Inst()->BuyBeautyItem(
                static_cast<Beauty::BeautyState>(state), primary, secondary, static_cast<int>(user->GetDBID()), boughtItems,
                adenaPrice, beautyCoinPrice, 0
            )) {
            // sendResult(0)
            sendResult(Beautyshop::BSRT_FAIL);
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            refreshHair();
            return false;
        }
    }

    CPacketHelperEx<20480> request;
    if (ownedItems.GetAssembledCount() == 0) {
        request.Assemble("d", 0);
    } else {
        request.Assemble("db", ownedItems.GetAssembledCount(), ownedItems.GetLength(), ownedItems.GetPacket());
    }
    // if (ownedItems.GetAssembledCount() == 0) {
    if (boughtItems.GetAssembledCount() == 0) {
        request.Assemble("d", 0);
    } else {
        // The adjacent current 0x00c02b1c call uses the same "db" contract.
        // request.Assemble(
        //     "ddb",
        //     ownedItems.GetAssembledCount(),
        //     ownedItems.GetLength(),
        //     ownedItems.GetPacket())
        // request.Assemble("db", ownedItems.GetAssembledCount(), ownedItems.GetLength(), ownedItems.GetPacket())
        request.Assemble("db", boughtItems.GetAssembledCount(), boughtItems.GetLength(), boughtItems.GetPacket());
    }
    db.RequestBuyBeautyItem(user, 0, adenaPrice, beautyCoinPrice, request);
    return false;
}

bool RequestShowResetBeauty(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00c02e20, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!g_npcserver.IsConnect()) {
        socket->SendSystemMessage(SystemMessageId::BEAUTY_SHOP_UNAVAILABLE_NPC_SERVER_DOWN);
        return false;
    }
    if (TalkChannelCache::inst().mChannels[17].IsValidChannel(user->Id(), user->m_spTalkTarget.GetId(), 0xfa, 0)) {
        const std::int64_t beautyCoin = user->m_inv.GetItemAmount(0x8dd4, 0, true);
        const std::int64_t adena = user->m_inv.GetAdenaAmount();
        socket->Send(
            "chQQddd", 0xfe, 0x137, adena, beautyCoin, user->m_data->m_nFace, user->m_data->m_nHairColor,
            user->m_data->m_nHairShape
        );
    }
    return false;
}

bool RequestResetBeauty(CSocket* const baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c02fc0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const auto sendResult = [&](const int result) {
        const int hairShape = user->m_data->m_nHairShapeEx != 0 ? user->m_data->m_nHairShapeEx : user->m_data->m_nHairShape;
        const int hairColor = user->m_data->m_nHairColorEx != 0 ? user->m_data->m_nHairColorEx : user->m_data->m_nHairColor;
        const int face = user->m_data->m_nFaceEx != 0 ? user->m_data->m_nFaceEx : user->m_data->m_nFace;
        const std::int64_t beautyCoin = user->m_inv.GetItemAmount(0x8dd4, 0, true);
        const std::int64_t adena = user->m_inv.GetAdenaAmount();
        socket->Send("chQQddddd", 0xfe, 0x136, adena, beautyCoin, 1, result, face, hairColor, hairShape);
    };

    std::int32_t face = -1;
    std::int32_t hairShape = -1;
    std::int32_t hairColor = -1;
    // Current 0x00c030ec likewise leaves r14 at the packet start for the
    // per-state reset loop at 0x00c033e0.
    ::Disassemble(packet, "ddd", &face, &hairShape, &hairColor);
    if (runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(face, hairShape), hairColor) == -3) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_STYLE_RESTORATION_FAILED);
        // sendResult(0)
        sendResult(Beautyshop::BSRT_FAIL);
        return false;
    }
    if (!g_npcserver.IsConnect()) {
        // sendResult(-1)
        sendResult(Beautyshop::BSRT_NPCSERVER_DISCONNECTED);
        return false;
    }
    if (!TalkChannelCache::inst().mChannels[17].IsValidChannel(user->Id(), user->m_spTalkTarget.GetId(), 0xfa, 0)) {
        // sendResult(0)
        sendResult(Beautyshop::BSRT_FAIL);
        return false;
    }
    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) == 1) {
        user->SendSystemMessage(SystemMessageId::FINISH_PREVIOUS_OPERATION_BEFORE_RETRYING);
        // sendResult(0)
        sendResult(Beautyshop::BSRT_FAIL);
        return false;
    }

    CPacketHelperEx<20480> request;
    std::int64_t adenaPrice = 0;
    std::int64_t unusedBeautyCoinPrice = 0;
    int colorPrimary = -1;
    for (int state = Beauty::HAIR; state < Beauty::STATEMAX; ++state) {
        std::int32_t secondary = -1;
        packet = ::Disassemble(packet, "d", &secondary);
        const int primary = state == Beauty::COLOR ? colorPrimary : 0;
        if (state == Beauty::HAIR) {
            colorPrimary = secondary;
        }
        if (!CBeautyManager::Inst()->ResetBeautyItem(
                static_cast<Beauty::BeautyState>(state), primary, secondary, static_cast<int>(user->GetDBID()), request,
                adenaPrice, unusedBeautyCoinPrice, 0
            )) {
            // sendResult(0)
            sendResult(Beautyshop::BSRT_FAIL);
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            return false;
        }
    }
    db.RequestResetBeautyItem(user, 0, adenaPrice, request);
    return false;
}

bool RequestAttendanceStampReward(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c08e88, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int8_t stamp = 0;
    ::Disassemble(packet, "c", &stamp);
    CEvent* const baseEvent = Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_ACCOUNT_EVENT);
    auto* const event = static_cast<CEvent_AccountEvent*>(baseEvent);
    if (event == nullptr || !event->InEventTime()) {
        return false;
    }
    const auto reward = event->m_mapAttendReward.find(static_cast<int>(stamp));
    if (reward == event->m_mapAttendReward.end()) {
        return false;
    }

    if (static_cast<double>(user->m_data->m_nWeightPoint) <= user->GetWeightLimit(69000.0) * 0.800000011920929 &&
        static_cast<float>(user->m_inv.GetTotalCount()) <= static_cast<float>(user->GetInventoryLimit()) * 0.8F) {
        user->GiveItemOnAccount(
            0, 0, 0, reward->second.nItemClassId, reward->second.nAmount, 0, static_cast<int>(stamp),
            const_cast<wchar_t*>(L"NoEvent"), 0, event->m_EventPeriod.m_nStartTime
        );
    } else {
        user->SendSystemMessage(SystemMessageId::INVENTORY_WEIGHT_AND_SLOT_USAGE_MUST_BE_BELOW_80_PERCENT);
    }
    return false;
}

bool RequestVipAttendanceCheck(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00bfc844, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (g_bUseVipAttendance && socket->CheckAttendanceTime(true) && user->m_data->m_bIsActive) {
        if (user->m_data->m_nLevel < g_nAttendanceMinimumLev) {
            user->SendSystemMessage(SystemMessageId::ATTENDANCE_REWARD_LEVEL_REQUIREMENTS_NOT_MET);
            return false;
        }

        VipAttendanceSystem& attendance = VipAttendanceSystem::Inst();
        const unsigned int accountId = user->GetAccountID();
        if (!attendance.AcquireVIPAttendanceFlag(accountId)) {
            user->SendSystemMessage(SystemMessageId::OPERATION_NOT_PROCESSED_TRY_AGAIN);
            socket->Send("chccdc", 0xfe, 0x17f, -3, 0, 0, 0);
            return false;
        }

        if (attendance.IsReceptableReward(user) && attendance.CheckDaysAndReset(user)) {
            const int result = user->OnRequestVipAttendanceCheck();
            if (result == 1) {
                return false;
            }
            socket->Send("chccdd", 0xfe, 0x17f, static_cast<int>(static_cast<std::int8_t>(result)), 0, 0, 0);
        }
        attendance.ReleaseVIPAttendanceFlag(accountId);
    }
    return false;
}

bool RequestShowAgitSiegeInfo(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00c04908, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    protocol::S_EX_SHOW_AGIT_SIEGE_INFO response;
    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (!residence->IsWorldAgit()) {
            continue;
        }
        CWorldAgit* const agit = residence->CastWorldAgit();
        if (agit == nullptr || agit->IsAuctionAgit()) {
            continue;
        }

        protocol::AgitSiegeInfo info{};
        info.nAgitId = agit->GetResidenceID();
        if (agit->IsBaseCampAgit()) {
            info.cAgitType = 1;
        } else if (agit->IsTeamBattleAgit()) {
            info.cAgitType = 2;
        } else {
            info.cAgitType = static_cast<char>(agit->IsCompetitionAgit() ? 3 : 1);
        }

        CPledge* const owner = agit->GetResidenceOwnerPledge();
        if (owner == nullptr) {
            info.wstrOwnerPledgeName.assign(u"");
            info.wstrOwnerPledgeMasterName.assign(u"");
        } else {
            const std::u16string pledgeName = std::u16string(owner->GetPledgeName());
            const std::u16string masterName = std::u16string(owner->GetMasterName());
            info.wstrOwnerPledgeName.assign(std::u16string_view(pledgeName));
            info.wstrOwnerPledgeMasterName.assign(std::u16string_view(masterName));
        }
        info.nSiegeDate = agit->m_data != nullptr ? agit->m_data->m_NextSiege : 0;
        info.cSiegeState = static_cast<char>(agit->IsUnderSiege());
        response.vAgitInfos.push_back(info);
    }

    user->GetSocket()->Send(S_EX, S_EX_SHOW_AGIT_SIEGE_INFO, response);
    return false;
}

} // namespace UserPacketEx

namespace {

bool IsCommissionTalkChannelValid(User* const user) {
    return TalkChannelCache::inst().mChannels[13].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0);
}

void SendCommissionBareFailure(CSocket* const socket) { socket->Send("ch", 0xfe, 0x112); }

} // namespace

bool UserPacketEx::RequestCommissionItemList(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00c007bc.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!IsCommissionTalkChannelValid(user)) {
        SendCommissionBareFailure(baseSocket);
        return false;
    }
    if (!F2PConfig::CanRegisterCommission(user)) {
        baseSocket->Send("ch", 0xfe, 0x149);
        return false;
    }
    CCommissionTradeManager::Inst().RequestCommissionTradeItemList(user);
    return false;
}

bool UserPacketEx::RequestCommissionInfo(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c00934.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!IsCommissionTalkChannelValid(user)) {
        SendCommissionBareFailure(baseSocket);
        return false;
    }

    int itemServerId = 0;
    ::Disassemble(packet, "d", &itemServerId);
    CCommissionTradeManager::Inst().RequestCommissionTradeRegTotalCount(static_cast<int>(user->GetDBID()), itemServerId);
    return false;
}

bool UserPacketEx::RequestCommissionRegister(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c00aac.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!IsCommissionTalkChannelValid(user)) {
        SendCommissionBareFailure(baseSocket);
        return false;
    }
    if (!F2PConfig::CanRegisterCommission(user)) {
        baseSocket->Send("ch", 0xfe, 0x149);
        return false;
    }

    int itemServerId = 0;
    std::array<SharedUtilsWideChar, 104> itemName;
    itemName[0] = SharedUtilsWideChar{};
    std::memset(itemName.data() + 1, 0, 198);
    std::int64_t itemPrice = 0;
    std::int64_t itemAmount = 0;
    CommissionExpired::Enum expiredType = CommissionExpired::EpInvalid;
    CommissionDiscountInfo::Enum discountType = CommissionDiscountInfo::CDI_DEFAULT;
    CommissionItemClassId::Enum premiumItemClassId = CommissionItemClassId::CICI_NONE;
    ::Disassemble(
        packet, "dSQQddd", &itemServerId, 200, itemName.data(), &itemPrice, &itemAmount, &expiredType, &discountType,
        &premiumItemClassId
    );

    CItem* const pendingItem = user->GetPendingUseETCItem(PUIT_COMMISSION);
    const auto fail = [&](const int systemMessage, const bool userSocket, const bool systemFirst) {
        if (systemFirst && systemMessage != 0) {
            user->SendSystemMessage(systemMessage);
        }
        user->CheckAndResetPendingUseETCItem(PUIT_COMMISSION);
        if (!systemFirst && systemMessage != 0) {
            user->SendSystemMessage(systemMessage);
        }
        if (userSocket) {
            user->GetSocket()->Send("chd", 0xfe, 0xf5, 0);
        } else {
            baseSocket->Send("chd", 0xfe, 0xf5, 0);
        }
    };

    const std::int64_t saleTotal = runtime::MultiplyInt64Wrapping(itemPrice, itemAmount);
    if (saleTotal > INT64_C(999999999999) || saleTotal < 1) {
        fail(SystemMessageId::COMMISSION_ADENA_LIMIT_EXCEEDED, true, true);
        return false;
    }
    if (static_cast<std::uint64_t>(itemAmount) - UINT64_C(1) >= UINT64_C(999999)) {
        fail(SystemMessageId::COMMISSION_ITEM_QUANTITY_LIMIT_EXCEEDED, true, true);
        return false;
    }
    if (pendingItem == nullptr) {
        fail(SystemMessageId::FINISH_PREVIOUS_OPERATION_BEFORE_RETRYING, false, false);
        return false;
    }
    if (CItemRestrict::Inst().CheckItemRestrict(pendingItem->m_data->m_nClassID)) {
        fail(0, false, false);
        return false;
    }
    if (user->IsEquipedItem(pendingItem->m_data->index)) {
        fail(SystemMessageId::CANNOT_REGISTER_EQUIPPED_ITEM, false, false);
        return false;
    }
    if (static_cast<int>(pendingItem->m_data->m_nId) != itemServerId) {
        fail(SystemMessageId::ITEM_REGISTRATION_FAILED, true, false);
        return false;
    }

    CItem* const item = user->m_inv.GetByServerID(itemServerId);
    if (item == nullptr) {
        fail(SystemMessageId::CANNOT_REGISTER_MISSING_ITEM, false, false);
        return false;
    }
    const int itemConsumeType = static_cast<int>(item->m_data->m_ItemConsumeType);
    const int enchanted = item->m_data->GetEnchanted();
    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) == 1) {
        fail(SystemMessageId::FINISH_PREVIOUS_OPERATION_BEFORE_RETRYING, false, false);
        return false;
    }

    const Commission::Enum commissionType = item->m_const->GetCommissionItemType();
    CCommissionTradeManager& manager = CCommissionTradeManager::Inst();
    if (manager.SetCommissionJob(commissionType) == 0) {
        const int itemDatabaseId = static_cast<int>(item->GetDBID());
        const int characterId = static_cast<int>(user->GetDBID());
        manager.RequestCommissionTradeRegister(
            CommissionOwner::CoUser, characterId, itemDatabaseId, itemName.data(), itemPrice, itemAmount, itemConsumeType,
            expiredType, commissionType, discountType, premiumItemClassId, enchanted
        );
    } else {
        user->SendSystemMessage(SystemMessageId::SALE_CANCELLATION_FAILED_REQUIREMENTS_NOT_MET);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    }
    return false;
}

bool UserPacketEx::RequestCommissionCancel(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00c010a8.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!IsCommissionTalkChannelValid(user)) {
        SendCommissionBareFailure(baseSocket);
        return false;
    }
    user->SetPendingUseETCItem(nullptr, PUIT_COMMISSION);
    return false;
}

bool UserPacketEx::RequestCommissionDelete(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c011cc.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!IsCommissionTalkChannelValid(user)) {
        SendCommissionBareFailure(baseSocket);
        return false;
    }

    std::int64_t commissionId = 0;
    std::uint32_t commissionTypeValue = 0;
    int decodedItemId = -1;
    ::Disassemble(packet, "Qdd", &commissionId, &commissionTypeValue, &decodedItemId);
    static_cast<void>(decodedItemId);
    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) == 1) {
        user->SendSystemMessage(SystemMessageId::FINISH_PREVIOUS_OPERATION_BEFORE_RETRYING);
        baseSocket->Send("chd", 0xfe, 0xf6, 0);
        return false;
    }

    CCommissionTradeManager& manager = CCommissionTradeManager::Inst();
    const int itemId = manager.GetCommissionItemDBID(commissionId);
    if (commissionTypeValue < 65) {
        const Commission::Enum commissionType = static_cast<Commission::Enum>(commissionTypeValue);
        if (manager.SetCommissionJob(commissionType) == 0) {
            manager.RequestCommissionTradeDelete(
                CommissionOwner::CoUser, commissionId, static_cast<int>(user->GetDBID()), itemId, commissionType
            );
        } else {
            user->SendSystemMessage(SystemMessageId::ITEM_REGISTRATION_FAILED);
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        }
    }
    return false;
}

bool UserPacketEx::RequestCommissionSearch(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c01438, including its two advancing
    // scan indices.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!IsCommissionTalkChannelValid(user)) {
        SendCommissionBareFailure(baseSocket);
        return false;
    }

    int depth = 0;
    Commission::Enum commissionType = Commission::Invalid;
    int itemGeneral = 0;
    CrystalType itemGrade = CRT_INVALID;
    std::array<SharedUtilsWideChar, 104> itemName;
    itemName[0] = SharedUtilsWideChar{};
    std::memset(itemName.data() + 1, 0, 198);
    ::Disassemble(packet, "ddddS", &depth, &commissionType, &itemGeneral, &itemGrade, 200, itemName.data());

    std::size_t pairIndex = 0;
    SharedUtilsWideChar* scan = itemName.data();
    do {
        if (*scan == SharedUtilsWideChar{}) {
            break;
        }
        if (*scan == static_cast<SharedUtilsWideChar>('\'') || *scan == static_cast<SharedUtilsWideChar>(';') ||
            *scan == static_cast<SharedUtilsWideChar>('/') || *scan == static_cast<SharedUtilsWideChar>('*') ||
            (itemName[pairIndex] == static_cast<SharedUtilsWideChar>('-') &&
                itemName[pairIndex + 1] == static_cast<SharedUtilsWideChar>('-'))) {
            baseSocket->Send("chd", 0xfe, 0xf7, -4);
            return false;
        }
        ++scan;
        pairIndex += 2;
    } while (pairIndex < 99);

    if (user->m_CommissionDelay == 1) {
        baseSocket->Send("chd", 0xfe, 0xf7, -3);
        return false;
    }
    runtime::raw_atomic::ExchangeI32(&user->m_CommissionDelay, 1);
    user->m_nCommissionTickCount = 0;
    if (depth != 0 || itemName[0] != SharedUtilsWideChar{}) {
        CCommissionTradeManager::Inst().RequestCommissionTradeSearch(
            static_cast<int>(user->GetDBID()), depth, commissionType, itemGeneral, itemGrade, itemName.data()
        );
    } else {
        user->SendSystemMessage(SystemMessageId::SEARCH_TOO_BROAD);
        baseSocket->Send("chd", 0xfe, 0xf7, -1);
    }
    return false;
}

bool UserPacketEx::RequestCommissionBuyInfo(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c01770.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!IsCommissionTalkChannelValid(user)) {
        SendCommissionBareFailure(baseSocket);
        return false;
    }

    std::int64_t commissionId = 0;
    Commission::Enum commissionType = Commission::Invalid;
    ::Disassemble(packet, "Qd", &commissionId, &commissionType);
    CCommissionTradeManager::Inst().RequestCommissionTradeBuyInfo(
        static_cast<int>(user->GetDBID()), commissionId, commissionType
    );
    return false;
}

bool UserPacketEx::RequestCommissionBuyItem(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c01904.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!IsCommissionTalkChannelValid(user)) {
        SendCommissionBareFailure(baseSocket);
        return false;
    }

    std::int64_t commissionId = 0;
    std::uint32_t commissionTypeValue = UINT32_MAX;
    ::Disassemble(packet, "Qd", &commissionId, &commissionTypeValue);
    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) == 1) {
        user->SendSystemMessage(SystemMessageId::FINISH_PREVIOUS_OPERATION_BEFORE_RETRYING);
        baseSocket->Send("chd", 0xfe, 0xf9, 0);
        return false;
    }

    if (commissionTypeValue < 65) {
        const Commission::Enum commissionType = static_cast<Commission::Enum>(commissionTypeValue);
        CCommissionTradeManager& manager = CCommissionTradeManager::Inst();
        if (manager.SetCommissionJob(commissionType) == 0) {
            manager.RequestCommissionTradeBuyItem(
                static_cast<int>(user->GetDBID()), commissionId, commissionType, CommissionOwner::CoUser
            );
        } else {
            user->SendSystemMessage(SystemMessageId::ITEM_PURCHASE_FAILED);
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        }
    }
    return false;
}

bool UserPacketEx::RequestCommissionRegisteredItem(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00c01b44.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!IsCommissionTalkChannelValid(user)) {
        SendCommissionBareFailure(baseSocket);
        return false;
    }
    CCommissionTradeManager::Inst().RequestCommissionTradeRegisteredList(static_cast<int>(user->GetDBID()));
    return false;
}

bool RequestBrGamePoint(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be3c58, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (!g_PremiumSetting.g_bStandAloneShopTest) {
        g_PremiumSvc.RequestGetGamePoint(static_cast<int>(user->Id()), 0);
    } else {
        user->m_GamePoint = 10000;
        user->GetSocket()->Send("chdQd", 0xfe, 0xd6, static_cast<int>(user->Id()), user->m_GamePoint, 0);
    }
    return false;
}

bool RequestBrProductList(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be3d7c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int listType = 0;
    ::Disassemble(packet, "d", &listType);
    if (listType == 0) {
        const std::int64_t eventCoin = user->m_inv.GetItemAmount(0x5cfd, 0, true);
        const std::int64_t adena = user->m_inv.GetAdenaAmount();
        g_ShopServer.SendProductList(baseSocket, adena, eventCoin);
    } else if (listType == 1) {
        db.ReqeustRecentProduct(user, g_PremiumSetting.purchaseHistoryCount);
    } else if (listType == 2) {
        db.ReqeustInterestedProduct(user);
    }
    return false;
}

bool RequestBrRecentProduct(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be3ec8, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        db.ReqeustRecentProduct(user, g_PremiumSetting.purchaseHistoryCount);
    }
    return user == nullptr;
}

bool RequestBrProductInfo(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf0b34, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int productId = 0;
    ::Disassemble(packet, "d", &productId);
    CProductSP product = g_ShopServer.GetProduct(productId);
    if (product) {
        struct ProductItemInfo {
            std::int32_t itemId;
            std::int32_t amount;
            std::int32_t weight;
            std::int32_t tradable;
        };

        // The current handler leaves lanes belonging to invalid item ids
        // untouched and nevertheless publishes the whole count-sized block.
        std::array<ProductItemInfo, 8> itemInfo;
        const int itemCount = product->GetItemCount();
        for (int index = 0; index < itemCount; ++index) {
            const CProduct::ItemData* const itemData = product->GetItemData(index);
            if (itemData == nullptr) {
                continue;
            }
            CObject* const object = g_ObjectDB.GetObjectW(itemData->typeId, false);
            if (object == nullptr || !object->IsItem()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] GetObject Fail",
                //     "UserPacketEx.cpp",
                //     0x1991)
                L2_LOG_ERROR(
                    Log, "Shop product item template is missing or invalid, product ID=%d, item class=%d, item index=%d",
                    productId, itemData->typeId, index
                );
                continue;
            }

            CItem* const item = static_cast<CItem*>(object);
            ProductItemInfo& output = itemInfo[static_cast<std::size_t>(index)];
            output.itemId = itemData->typeId;
            output.amount = itemData->amount;
            output.weight = static_cast<int>(item->m_data->m_ItemConsumeType) == 3 ? 0 : item->m_const->m_nWeight;
            output.tradable = item->IsTrade() ? 1 : 0;
        }

        std::int64_t adena;
        std::int64_t gamePoint;
        std::int64_t eventCoin;
        {
            L2_READ_GUARD(user->m_inv);
            adena = user->m_inv.GetAdenaAmount();
            gamePoint = user->m_GamePoint;
            eventCoin = user->m_inv.GetItemAmount(0x5cfd, 0, true);
        }

        user->GetSocket()->Send(
            "chdddbQQQ", 0xfe, 0xd8, productId, product->m_price, itemCount, itemCount << 4, itemInfo.data(), adena,
            gamePoint, eventCoin
        );
    }
    return false;
}

bool RequestBrBuyProduct(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf0e8c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int productId = 0;
    int productAmount = 0;
    ::Disassemble(packet, "dd", &productId, &productAmount);
    if (user->IsBuyingProduct()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]: buy product fail, user is already buying state",
        //     L"UserPacketEx.cpp",
        //     0x19c4)
        L2_LOG_WARN(
            Log, "Product purchase rejected: another purchase is in progress, product ID=%d, character ID=%u", productId,
            static_cast<unsigned>(user->GetDBID())
        );
        user->GetSocket()->Send("chd", 0xfe, 0xd9, static_cast<int>(BR_BUY_INVALID_USER_STATE));
        return false;
    }

    const BrBuyProductResultType result = user->CheckBuyProduct(baseSocket, productId, productAmount, false);
    if (result == BR_BUY_SUCCESS) {
        CProductSP product = g_ShopServer.GetProduct(productId);
        if (product) {
            user->SendDB(
                "chdddddc", 0xea, 0x11c, productId, productAmount, static_cast<int>(user->GetDBID()), product->m_limitCount,
                product->m_limitPeriod, 0
            );
        }
    } else {
        user->GetSocket()->Send("chd", 0xfe, 0xd9, static_cast<int>(result));
        user->EndBuyProduct();
    }
    return false;
}

bool UserPacketEx::RequestBrBuyProductForGift(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bf67d4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<SharedUtilsWideChar, 28> receiverName;
    std::array<SharedUtilsWideChar, 64> title;
    std::array<SharedUtilsWideChar, 1004> content;
    std::array<SharedUtilsWideChar, 1008> lowerReceiverName;
    std::memset(receiverName.data(), 0, 0x32);
    std::memset(title.data(), 0, 0x7a);
    std::memset(content.data(), 0, 0x7d2);
    std::memset(lowerReceiverName.data(), 0, 0x7d2);

    int productId = 0;
    int productAmount = 0;
    ::Disassemble(
        packet, "ddSSS", &productId, &productAmount, 0x32, reinterpret_cast<char16_t*>(receiverName.data()), 0x7a,
        reinterpret_cast<char16_t*>(title.data()), 0x7d2, reinterpret_cast<char16_t*>(content.data())
    );
    ::ToLowerCase(receiverName.data(), lowerReceiverName.data());

    if (user->IsBuyingProduct()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]: buy product fail, user is already buying state",
        //     L"UserPacketEx.cpp",
        //     0x209a)
        L2_LOG_WARN(
            Log, "Product gift purchase rejected: another purchase is in progress, product ID=%d, character ID=%u",
            productId, static_cast<unsigned>(user->GetDBID())
        );
        user->GetSocket()->Send("chd", 0xfe, 0x11a, static_cast<int>(BR_BUY_INVALID_USER_STATE));
        return false;
    }
    if (user->m_data->m_nLevel < g_PremiumSetting.giftLevelLimit) {
        user->GetSocket()->Send("chd", 0xfe, 0x11a, static_cast<int>(BR_BUY_LEVEL_LIMIT));
        return false;
    }

    const std::u16string hostLowerReceiverNameUtf16(reinterpret_cast<const char16_t*>(lowerReceiverName.data()));
    CSPointer<User> receiver = GetUserFromNameMap(g_nWorldId, hostLowerReceiverNameUtf16);
    if (receiver && static_cast<int>(user->Id()) == static_cast<int>(receiver->Id())) {
        user->GetSocket()->Send("chd", 0xfe, 0x11a, static_cast<int>(BR_BUY_ME_TO_ME));
        return false;
    }
    if (user->m_pPostInfo->GetSentListSize() >= 0xf1) {
        user->GetSocket()->Send("chd", 0xfe, 0x11a, static_cast<int>(BR_BUY_SEND_POST_COUNT));
        return false;
    }

    const BrBuyProductResultType result = user->CheckBuyProduct(baseSocket, productId, productAmount, true);
    if (result != BR_BUY_SUCCESS) {
        user->GetSocket()->Send("chd", 0xfe, 0x11a, static_cast<int>(result));
        user->EndBuyProduct();
        return false;
    }

    CProductSP product = g_ShopServer.GetProduct(productId);
    if (!product) {
        user->GetSocket()->Send("chd", 0xfe, 0x11a, static_cast<int>(BR_BUY_INVALID_PRODUCT));
        return false;
    }

    user->SendDB(
        "chdddddcSSS", 0xea, 0x11c, productId, productAmount, static_cast<int>(user->GetDBID()), product->m_limitCount,
        product->m_limitPeriod, 1, reinterpret_cast<const char16_t*>(lowerReceiverName.data()),
        reinterpret_cast<const char16_t*>(title.data()), reinterpret_cast<const char16_t*>(content.data())
    );
    return false;
}

bool UserPacketEx::RequestBrAddInterestedProduct(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be6a8c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int productId = 0;
    ::Disassemble(packet, "d", &productId);
    if (!g_PremiumSetting.enable) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RequestBrAddInterestedProduct Error, charID[%d] productID[%d] ",
        //     user->m_data->m_nDBID,
        //     productId)
        L2_LOG_WARN(
            Log, "Product wishlist addition rejected: premium shop is disabled, character ID=%d, product ID=%d",
            user->m_data->m_nDBID, productId
        );
        // Noise: a disabled shop is an expected configuration rejection. The warning identifies the request.
        // runtime::process::ReportCallStack(2, false)
    } else {
        db.ReqeustAddInterestedProduct(user, productId);
    }
    return false;
}

bool UserPacketEx::RequestBrDeleteInterestedProduct(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be6bb0, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int productId = 0;
    ::Disassemble(packet, "d", &productId);
    if (!g_PremiumSetting.enable) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RequestBrDeleteInterestedProduct Error, charID[%d] productID[%d] ",
        //     user->m_data->m_nDBID,
        //     productId)
        L2_LOG_WARN(
            Log, "Product wishlist removal rejected: premium shop is disabled, character ID=%d, product ID=%d",
            user->m_data->m_nDBID, productId
        );
        // Noise: a disabled shop is an expected configuration rejection. The warning identifies the request.
        // runtime::process::ReportCallStack(2, false)
    } else {
        db.ReqeustDeleteInterestedProduct(user, productId);
    }
    return false;
}

bool UserPacketEx::RequestBrExistNewProduct(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00be6cd4, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->GetSocket()->Send("chh", 0xfe, 0x13a, g_ShopServer.ExistNewProduct());
    }
    return user == nullptr;
}

bool RequestItemAuctionBid(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bfff5c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!TalkChannelCache::inst().mChannels[9].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request auction with invalid talk channel.",
        //     L"UserPacketEx.cpp",
        //     0xb6f,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Auction bid rejected: talk channel validation failed, character=%s, character ID=%u",
        //     ::WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID()))
        L2_LOG_WARN(
            Log, "Auction bid rejected: talk channel validation failed, character=%s, character ID=%u",
            ::Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    int auctionDatabaseId = 0;
    std::int64_t biddingPrice = 0;
    ::Disassemble(packet, "dQ", &auctionDatabaseId, &biddingPrice);
    CAuction* const auction = CAuctionManager::inst().GetAuction(auctionDatabaseId);
    if (auction != nullptr) {
        auction->Bid(user, biddingPrice);
    }
    return false;
}

bool RequestItemAuctionInfo(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00bfd28c, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int auctionDatabaseId = 0;
    ::Disassemble(packet, "d", &auctionDatabaseId);
    CAuction* const auction = CAuctionManager::inst().GetAuction(auctionDatabaseId);
    if (auction != nullptr) {
        auction->ShowAuctionUI(user, false);
    }
    return false;
}

bool UserPacketEx::RequestItemAuctionStatus(CSocket* const baseSocket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00bfea18, original owner UserPacketEx.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    protocol::S_EX_ITEM_AUCTION_STATUS response{};
    for (const auto& entry : CAuctionNPCDB::inst().m_mapAuctionNPC) {
        CAuctionNPC* const auctionNpc = entry.second;
        if (auctionNpc == nullptr) {
            continue;
        }

        CAuction* const latest = CAuctionManager::inst().GetLatestAuction(auctionNpc->m_nNPCClassID);
        bool active = latest != nullptr && (latest->m_eStatus == AS_DOING || latest->m_nAuctionDBJobCount > 0);
        response.nPosX = runtime::Win64CvttDoubleToInt32(auctionNpc->GetPos().x);
        response.nPosY = runtime::Win64CvttDoubleToInt32(auctionNpc->GetPos().y);
        response.nPosZ = runtime::Win64CvttDoubleToInt32(auctionNpc->GetPos().z);
        response.nInzoneId = auctionNpc->GetInZoneID();
        response.cAuctionStatus = static_cast<char>(active);
        if (g_bWorldMapTestEnable) {
            response.cAuctionStatus = static_cast<char>(g_bAuctionEnable);
        }
        break;
    }

    user->GetSocket()->Send(S_EX, S_EX_ITEM_AUCTION_STATUS, response);
    return false;
}

namespace protocol {

bool S_EX_OLYMPIAD_INFO::Encode(network::Stream* stream) const { return stream->Encode(&bOpen); }

bool S_EX_OLYMPIAD_INFO::Decode(network::Stream* stream) { return stream->Decode(&bOpen); }

bool S_EX_OLYMPIAD_RECORD::Encode(network::Stream* stream) const {
    return stream->Encode(&nPoint) && stream->Encode(&nWinCount) && stream->Encode(&nLoseCount) &&
        stream->Encode(&nMatchCount) && stream->Encode(&nPrevClassType) && stream->Encode(&nPrevRank) &&
        stream->Encode(&nPrevRankCount) && stream->Encode(&nPrevClassRank) && stream->Encode(&nPrevClassRankCount) &&
        stream->Encode(&nPrevClassRankByServer) && stream->Encode(&nPrevClassRankByServerCount) &&
        stream->Encode(&nPrevPoint) && stream->Encode(&nPrevWinCount) && stream->Encode(&nPrevLoseCount) &&
        stream->Encode(&nPrevGrade) && stream->Encode(&nSeasonYear) && stream->Encode(&nSeasonMonth) &&
        stream->Encode(&bMatchOpen);
}

bool S_EX_OLYMPIAD_RECORD::Decode(network::Stream* stream) {
    return stream->Decode(&nPoint) && stream->Decode(&nWinCount) && stream->Decode(&nLoseCount) &&
        stream->Decode(&nMatchCount) && stream->Decode(&nPrevClassType) && stream->Decode(&nPrevRank) &&
        stream->Decode(&nPrevRankCount) && stream->Decode(&nPrevClassRank) && stream->Decode(&nPrevClassRankCount) &&
        stream->Decode(&nPrevClassRankByServer) && stream->Decode(&nPrevClassRankByServerCount) &&
        stream->Decode(&nPrevPoint) && stream->Decode(&nPrevWinCount) && stream->Decode(&nPrevLoseCount) &&
        stream->Decode(&nPrevGrade) && stream->Decode(&nSeasonYear) && stream->Decode(&nSeasonMonth) &&
        stream->Decode(&bMatchOpen);
}

bool S_EX_OLYMPIAD_MATCH_INFO::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(szTeam1Name), sizeof(szTeam1Name)) &&
        stream->Encode(&nTeam1WinCount) &&
        stream->Encode(reinterpret_cast<const unsigned char*>(szTeam2Name), sizeof(szTeam2Name)) &&
        stream->Encode(&nTeam2WinCount) && stream->Encode(&nCurrentSet) && stream->Encode(&nRemainSecond);
}

bool S_EX_OLYMPIAD_MATCH_INFO::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(szTeam1Name), sizeof(szTeam1Name)) &&
        stream->Decode(&nTeam1WinCount) &&
        stream->Decode(reinterpret_cast<unsigned char*>(szTeam2Name), sizeof(szTeam2Name)) &&
        stream->Decode(&nTeam2WinCount) && stream->Decode(&nCurrentSet) && stream->Decode(&nRemainSecond);
}

bool S_EX_ITEM_ANNOUNCE::Encode(network::Stream* stream) const {
    return stream->Encode(&cType) && stream->Encode(&sUserName) && stream->Encode(&nItemClassID) &&
        stream->Encode(&cEnchantCount) && stream->Encode(&nFromItemClassID);
}

bool S_EX_ITEM_ANNOUNCE::Decode(network::Stream* stream) {
    return stream->Decode(&cType) && stream->Decode(&sUserName) && stream->Decode(&nItemClassID) &&
        stream->Decode(&cEnchantCount) && stream->Decode(&nFromItemClassID);
}

bool S_EX_COMPLETED_DAILY_QUEST_LIST::Encode(network::Stream* stream) const {
    const std::uint32_t questCount = static_cast<std::uint32_t>(vQuestIds.size());
    if (!stream->Encode(&questCount)) {
        return false;
    }
    for (const std::int32_t questId : vQuestIds) {
        if (!stream->Encode(&questId)) {
            return false;
        }
    }
    return true;
}

bool S_EX_COMPLETED_DAILY_QUEST_LIST::Decode(network::Stream* stream) {
    std::int32_t questCount = 0;
    if (!stream->Decode(&questCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < questCount; ++index) {
        std::int32_t questId = 0;
        if (!stream->Decode(&questId)) {
            return false;
        }
        vQuestIds.push_back(questId);
    }
    return true;
}

bool S_EX_COMPLETED_DAILY_QUEST::Encode(network::Stream* stream) const { return stream->Encode(&questId); }

bool S_EX_COMPLETED_DAILY_QUEST::Decode(network::Stream* stream) { return stream->Decode(&questId); }

bool C_EX_DECO_NPC_INFO::Encode(network::Stream* stream) const { return stream->Encode(&nAgitCID); }

bool C_EX_DECO_NPC_INFO::Decode(network::Stream* stream) { return stream->Decode(&nAgitCID); }

bool S_EX_DECO_NPC_INFO::Encode(network::Stream* stream) const {
    const std::uint32_t decoNpcCount = static_cast<std::uint32_t>(vDecoNpcs.size());
    if (!stream->Encode(&nAgitCID) || !stream->Encode(&decoNpcCount)) {
        return false;
    }
    for (const DecoNpcInfo& info : vDecoNpcs) {
        if (!stream->Encode(reinterpret_cast<const unsigned char*>(&info), static_cast<int>(sizeof(info)))) {
            return false;
        }
    }

    const std::uint32_t residenceDomainCount = static_cast<std::uint32_t>(vResidenceDomains.size());
    if (!stream->Encode(&nResidenceGrade) || !stream->Encode(&residenceDomainCount)) {
        return false;
    }
    for (const char residenceDomain : vResidenceDomains) {
        if (!stream->Encode(&residenceDomain)) {
            return false;
        }
    }
    return true;
}

bool S_EX_DECO_NPC_INFO::Decode(network::Stream* stream) {
    std::int32_t decoNpcCount = 0;
    if (!stream->Decode(&nAgitCID) || !stream->Decode(&decoNpcCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < decoNpcCount; ++index) {
        DecoNpcInfo info{};
        if (!stream->Decode(reinterpret_cast<unsigned char*>(&info), static_cast<int>(sizeof(info)))) {
            return false;
        }
        vDecoNpcs.push_back(info);
    }

    std::int32_t residenceDomainCount = 0;
    if (!stream->Decode(&nResidenceGrade) || !stream->Decode(&residenceDomainCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < residenceDomainCount; ++index) {
        char residenceDomain = 0;
        if (!stream->Decode(&residenceDomain)) {
            return false;
        }
        vResidenceDomains.push_back(residenceDomain);
    }
    return true;
}

bool C_EX_DECO_NPC_SET::Encode(network::Stream* stream) const {
    return stream->Encode(&nAgitCID) && stream->Encode(&cSlot) && stream->Encode(&nDecoCID);
}

bool C_EX_DECO_NPC_SET::Decode(network::Stream* stream) {
    return stream->Decode(&nAgitCID) && stream->Decode(&cSlot) && stream->Decode(&nDecoCID);
}

bool S_EX_DECO_NPC_SET::Encode(network::Stream* stream) const {
    return stream->Encode(&cResult) && stream->Encode(&nAgitCID) && stream->Encode(&cSlot) && stream->Encode(&nDecoCID) &&
        stream->Encode(&nExpire);
}

bool S_EX_DECO_NPC_SET::Decode(network::Stream* stream) {
    return stream->Decode(&cResult) && stream->Decode(&nAgitCID) && stream->Decode(&cSlot) && stream->Decode(&nDecoCID) &&
        stream->Decode(&nExpire);
}

bool C_EX_FACTION_INFO::Encode(network::Stream* stream) const {
    return stream->Encode(&nUserID) && stream->Encode(&nRequestType);
}

bool C_EX_FACTION_INFO::Decode(network::Stream* stream) { return stream->Decode(&nUserID) && stream->Decode(&nRequestType); }

bool S_EX_FACTION_INFO::Encode(network::Stream* stream) const {
    const std::uint32_t factionInfoCount = static_cast<std::uint32_t>(lFactionInfoList.size());
    if (!stream->Encode(&nUserID) || !stream->Encode(&nRequestType) || !stream->Encode(&factionInfoCount)) {
        return false;
    }
    for (const FactionInfo& info : lFactionInfoList) {
        if (!stream->Encode(reinterpret_cast<const unsigned char*>(&info), static_cast<int>(sizeof(info)))) {
            return false;
        }
    }
    return true;
}

bool S_EX_FACTION_INFO::Decode(network::Stream* stream) {
    std::int32_t factionInfoCount = 0;
    if (!stream->Decode(&nUserID) || !stream->Decode(&nRequestType) || !stream->Decode(&factionInfoCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < factionInfoCount; ++index) {
        FactionInfo info{};
        if (!stream->Decode(reinterpret_cast<unsigned char*>(&info), static_cast<int>(sizeof(info)))) {
            return false;
        }
        lFactionInfoList.push_back(info);
    }
    return true;
}

bool S_EX_BALTHUS_EVENT::Encode(network::Stream* stream) const {
    return stream->Encode(&nCurrentState) && stream->Encode(&nProgress) && stream->Encode(&nCurrentRewardItem) &&
        stream->Encode(&nRewardTokenCount) && stream->Encode(&nParticipated) && stream->Encode(&bRunning) &&
        stream->Encode(&nTime);
}

bool S_EX_BALTHUS_EVENT::Decode(network::Stream* stream) {
    return stream->Decode(&nCurrentState) && stream->Decode(&nProgress) && stream->Decode(&nCurrentRewardItem) &&
        stream->Decode(&nRewardTokenCount) && stream->Decode(&nParticipated) && stream->Decode(&bRunning) &&
        stream->Decode(&nTime);
}

bool S_EX_FIELD_EVENT_STEP::Encode(network::Stream* stream) const {
    return stream->Encode(&nStep) && stream->Encode(&nRemainSec) && stream->Encode(&nGoalPoint);
}

bool S_EX_FIELD_EVENT_STEP::Decode(network::Stream* stream) {
    return stream->Decode(&nStep) && stream->Decode(&nRemainSec) && stream->Decode(&nGoalPoint);
}

bool S_EX_FIELD_EVENT_POINT::Encode(network::Stream* stream) const { return stream->Encode(&nPoint); }

bool S_EX_FIELD_EVENT_POINT::Decode(network::Stream* stream) { return stream->Decode(&nPoint); }

bool C_EX_ENCHANT_RETRY_TO_PUT_ITEMS_INFO::Encode(network::Stream* stream) const {
    return stream->Encode(&nSlotOneItemServerID) && stream->Encode(&nSlotTwoItemServerID);
}

bool C_EX_ENCHANT_RETRY_TO_PUT_ITEMS_INFO::Decode(network::Stream* stream) {
    return stream->Decode(&nSlotOneItemServerID) && stream->Decode(&nSlotTwoItemServerID);
}

bool C_EX_SWAP_AGATHION_SLOT_ITEMS_INFO::Encode(network::Stream* stream) const {
    return stream->Encode(&SrcSlotBitType) && stream->Encode(&SrcItemID) && stream->Encode(&DestSlotBitType) &&
        stream->Encode(&DestItemID);
}

bool C_EX_SWAP_AGATHION_SLOT_ITEMS_INFO::Decode(network::Stream* stream) {
    return stream->Decode(&SrcSlotBitType) && stream->Decode(&SrcItemID) && stream->Decode(&DestSlotBitType) &&
        stream->Decode(&DestItemID);
}

bool C_EX_RAID_BOSS_SPAWN_INFO::Encode(network::Stream* stream) const {
    const std::uint32_t raidNpcCount = static_cast<std::uint32_t>(vRaidNpcIds.size());
    if (!stream->Encode(&raidNpcCount)) {
        return false;
    }
    for (const std::int32_t raidNpcId : vRaidNpcIds) {
        if (!stream->Encode(&raidNpcId)) {
            return false;
        }
    }
    return true;
}

bool C_EX_RAID_BOSS_SPAWN_INFO::Decode(network::Stream* stream) {
    std::int32_t raidNpcCount = 0;
    if (!stream->Decode(&raidNpcCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < raidNpcCount; ++index) {
        std::int32_t raidNpcId = 0;
        if (!stream->Decode(&raidNpcId)) {
            return false;
        }
        vRaidNpcIds.push_back(raidNpcId);
    }
    return true;
}

bool S_EX_RAID_BOSS_SPAWN_INFO::Encode(network::Stream* stream) const {
    const std::uint32_t raidSpawnNpcCount = static_cast<std::uint32_t>(vRaidSpawnNpcIds.size());
    if (!stream->Encode(&raidSpawnNpcCount)) {
        return false;
    }
    for (const std::int32_t raidSpawnNpcId : vRaidSpawnNpcIds) {
        if (!stream->Encode(&raidSpawnNpcId)) {
            return false;
        }
    }
    return true;
}

bool S_EX_RAID_BOSS_SPAWN_INFO::Decode(network::Stream* stream) {
    std::int32_t raidSpawnNpcCount = 0;
    if (!stream->Decode(&raidSpawnNpcCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < raidSpawnNpcCount; ++index) {
        std::int32_t raidSpawnNpcId = 0;
        if (!stream->Decode(&raidSpawnNpcId)) {
            return false;
        }
        vRaidSpawnNpcIds.push_back(raidSpawnNpcId);
    }
    return true;
}

bool S_EX_RAID_SERVER_INFO::Encode(network::Stream* stream) const {
    return stream->Encode(&cDimensionRaid) && stream->Encode(&cDimensionCastleSiege);
}

bool S_EX_RAID_SERVER_INFO::Decode(network::Stream* stream) {
    return stream->Decode(&cDimensionRaid) && stream->Decode(&cDimensionCastleSiege);
}

bool S_EX_ITEM_AUCTION_STATUS::Encode(network::Stream* stream) const {
    return stream->Encode(&nPosX) && stream->Encode(&nPosY) && stream->Encode(&nPosZ) && stream->Encode(&nInzoneId) &&
        stream->Encode(&cAuctionStatus);
}

bool S_EX_ITEM_AUCTION_STATUS::Decode(network::Stream* stream) {
    return stream->Decode(&nPosX) && stream->Decode(&nPosY) && stream->Decode(&nPosZ) && stream->Decode(&nInzoneId) &&
        stream->Decode(&cAuctionStatus);
}

bool S_EX_ITEM_AUCTION_UPDATED_BIDDING_INFO::Encode(network::Stream* stream) const { return stream->Encode(&nCurrentPrice); }

bool S_EX_ITEM_AUCTION_UPDATED_BIDDING_INFO::Decode(network::Stream* stream) { return stream->Decode(&nCurrentPrice); }

bool S_EX_PRIVATE_STORE_BUYING_RESULT::Encode(network::Stream* stream) const {
    return stream->Encode(&nItemSid) && stream->Encode(&nAmount) &&
        stream->Encode(reinterpret_cast<const unsigned char*>(charName), static_cast<int>(sizeof(charName)));
}

bool S_EX_PRIVATE_STORE_BUYING_RESULT::Decode(network::Stream* stream) {
    return stream->Decode(&nItemSid) && stream->Decode(&nAmount) &&
        stream->Decode(reinterpret_cast<unsigned char*>(charName), static_cast<int>(sizeof(charName)));
}

bool S_EX_PRIVATE_STORE_SELLING_RESULT::Encode(network::Stream* stream) const {
    return stream->Encode(&nItemSid) && stream->Encode(&nAmount) &&
        stream->Encode(reinterpret_cast<const unsigned char*>(charName), static_cast<int>(sizeof(charName)));
}

bool S_EX_PRIVATE_STORE_SELLING_RESULT::Decode(network::Stream* stream) {
    return stream->Decode(&nItemSid) && stream->Decode(&nAmount) &&
        stream->Decode(reinterpret_cast<unsigned char*>(charName), static_cast<int>(sizeof(charName)));
}

bool C_EX_UPGRADE_SYSTEM_REQUEST::Encode(network::Stream* stream) const {
    return stream->Encode(&nItemSid) && stream->Encode(&nUpgradeSystemID);
}

bool C_EX_UPGRADE_SYSTEM_REQUEST::Decode(network::Stream* stream) {
    return stream->Decode(&nItemSid) && stream->Decode(&nUpgradeSystemID);
}

bool S_EX_SHOW_UPGRADE_SYSTEM::Encode(network::Stream* stream) const { return stream->Encode(&nFlag); }

bool S_EX_SHOW_UPGRADE_SYSTEM::Decode(network::Stream* stream) { return stream->Decode(&nFlag); }

bool S_EX_UPGRADE_SYSTEM_RESULT::Encode(network::Stream* stream) const {
    return stream->Encode(&nResult) && stream->Encode(&nID);
}

bool S_EX_UPGRADE_SYSTEM_RESULT::Decode(network::Stream* stream) { return stream->Decode(&nResult) && stream->Decode(&nID); }

bool PkPledgeContributionRankInfo::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool PkPledgeContributionRankInfo::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool C_EX_PLEDGE_CONTRIBUTION_INFO::Encode(network::Stream* stream) const { return stream->Encode(&cReserved); }

bool C_EX_PLEDGE_CONTRIBUTION_INFO::Decode(network::Stream* stream) { return stream->Decode(&cReserved); }

bool C_EX_PLEDGE_CONTRIBUTION_RANK::Encode(network::Stream* stream) const { return stream->Encode(&bCurrent); }

bool C_EX_PLEDGE_CONTRIBUTION_RANK::Decode(network::Stream* stream) { return stream->Decode(&bCurrent); }

bool S_EX_PLEDGE_CONTRIBUTION_INFO::Encode(network::Stream* stream) const {
    return stream->Encode(&nPeriod) && stream->Encode(&nTotal) && stream->Encode(&nTotalForNextReward) &&
        stream->Encode(&nItemClassID) && stream->Encode(&nAmount) && stream->Encode(&nNameValue);
}

bool S_EX_PLEDGE_CONTRIBUTION_INFO::Decode(network::Stream* stream) {
    return stream->Decode(&nPeriod) && stream->Decode(&nTotal) && stream->Decode(&nTotalForNextReward) &&
        stream->Decode(&nItemClassID) && stream->Decode(&nAmount) && stream->Decode(&nNameValue);
}

bool S_EX_PLEDGE_CONTRIBUTION_RANK::Encode(network::Stream* stream) const {
    const std::uint32_t memberCount = static_cast<std::uint32_t>(vMembers.size());
    if (!stream->Encode(&bCurrent) || !stream->Encode(&memberCount)) {
        return false;
    }
    for (const PkPledgeContributionRankInfo& sourceInfo : vMembers) {
        const PkPledgeContributionRankInfo info(sourceInfo);
        if (!info.Encode(stream)) {
            return false;
        }
    }
    return true;
}

bool S_EX_PLEDGE_CONTRIBUTION_RANK::Decode(network::Stream* stream) {
    std::int32_t memberCount = 0;
    if (!stream->Decode(&bCurrent) || !stream->Decode(&memberCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < memberCount; ++index) {
        PkPledgeContributionRankInfo info;
        if (!info.Decode(stream)) {
            return false;
        }
        vMembers.push_back(info);
    }
    return true;
}

bool PkPledgeRaidRankInfo::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool PkPledgeRaidRankInfo::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool S_EX_PLEDGE_RAID_INFO::Encode(network::Stream* stream) const {
    return stream->Encode(&bIsCurrent_) &&
        stream->Encode(reinterpret_cast<const unsigned char*>(szPledgeName_), static_cast<int>(sizeof(szPledgeName_))) &&
        stream->Encode(&nRank_) && stream->Encode(&nTier_) && stream->Encode(&nPoint_) && stream->Encode(&nRewardReason_) &&
        stream->Encode(&nSeason_) && stream->Encode(&hStartTimeYear_) && stream->Encode(&hStartTimeMonth_) &&
        stream->Encode(&hStartTimeDay_) && stream->Encode(&hEndTimeYear_) && stream->Encode(&hEndTimeMonth_) &&
        stream->Encode(&hEndTimeDay_);
}

bool S_EX_PLEDGE_RAID_INFO::Decode(network::Stream* stream) {
    return stream->Decode(&bIsCurrent_) &&
        stream->Decode(reinterpret_cast<unsigned char*>(szPledgeName_), static_cast<int>(sizeof(szPledgeName_))) &&
        stream->Decode(&nRank_) && stream->Decode(&nTier_) && stream->Decode(&nPoint_) && stream->Decode(&nRewardReason_) &&
        stream->Decode(&nSeason_) && stream->Decode(&hStartTimeYear_) && stream->Decode(&hStartTimeMonth_) &&
        stream->Decode(&hStartTimeDay_) && stream->Decode(&hEndTimeYear_) && stream->Decode(&hEndTimeMonth_) &&
        stream->Decode(&hEndTimeDay_);
}

bool S_EX_PLEDGE_RAID_RANK::Encode(network::Stream* stream) const {
    const std::uint32_t rankCount = static_cast<std::uint32_t>(vList_.size());
    if (!stream->Encode(&bIsCurrent_) || !stream->Encode(&rankCount)) {
        return false;
    }
    for (const PkPledgeRaidRankInfo& sourceInfo : vList_) {
        const PkPledgeRaidRankInfo info(sourceInfo);
        if (!info.Encode(stream)) {
            return false;
        }
    }
    return true;
}

bool S_EX_PLEDGE_RAID_RANK::Decode(network::Stream* stream) {
    std::int32_t rankCount = 0;
    if (!stream->Decode(&bIsCurrent_) || !stream->Decode(&rankCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < rankCount; ++index) {
        PkPledgeRaidRankInfo info;
        if (!info.Decode(stream)) {
            return false;
        }
        vList_.push_back(info);
    }
    return true;
}

bool C_EX_PLEDGE_CONTRIBUTION_REWARD::Encode(network::Stream* stream) const { return stream->Encode(&nType); }

bool C_EX_PLEDGE_CONTRIBUTION_REWARD::Decode(network::Stream* stream) { return stream->Decode(&nType); }

bool S_EX_PLEDGE_CONTRIBUTION_REWARD::Encode(network::Stream* stream) const { return stream->Encode(&nResult); }

bool S_EX_PLEDGE_CONTRIBUTION_REWARD::Decode(network::Stream* stream) { return stream->Decode(&nResult); }

bool C_EX_PLEDGE_LEVEL_UP::Encode(network::Stream* stream) const { return stream->Encode(&cReserved); }

bool C_EX_PLEDGE_LEVEL_UP::Decode(network::Stream* stream) { return stream->Decode(&cReserved); }

bool S_EX_PLEDGE_LEVEL_UP::Encode(network::Stream* stream) const { return stream->Encode(&nResult); }

bool S_EX_PLEDGE_LEVEL_UP::Decode(network::Stream* stream) { return stream->Decode(&nResult); }

bool C_EX_PLEDGE_MISSION_REWARD::Encode(network::Stream* stream) const { return stream->Encode(&nMissionId_); }

bool C_EX_PLEDGE_MISSION_REWARD::Decode(network::Stream* stream) { return stream->Decode(&nMissionId_); }

bool S_EX_PLEDGE_MISSION_REWARD_COUNT::Encode(network::Stream* stream) const {
    return stream->Encode(&nCount_) && stream->Encode(&nMaxCount_);
}

bool S_EX_PLEDGE_MISSION_REWARD_COUNT::Decode(network::Stream* stream) {
    return stream->Decode(&nCount_) && stream->Decode(&nMaxCount_);
}

bool S_EX_PLEDGE_SHOW_INFO_UPDATE::Encode(network::Stream* stream) const {
    return stream->Encode(&nPledgeSId) && stream->Encode(&nNextLevelNameValue) && stream->Encode(&nMaster) &&
        stream->Encode(&nElite);
}

bool S_EX_PLEDGE_SHOW_INFO_UPDATE::Decode(network::Stream* stream) {
    return stream->Decode(&nPledgeSId) && stream->Decode(&nNextLevelNameValue) && stream->Decode(&nMaster) &&
        stream->Decode(&nElite);
}

bool PkPledgeMissionInfo::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool PkPledgeMissionInfo::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool S_EX_PLEDGE_MISSION_INFO::Encode(network::Stream* stream) const {
    const std::uint32_t missionInfoCount = static_cast<std::uint32_t>(vList_.size());
    if (!stream->Encode(&missionInfoCount)) {
        return false;
    }
    for (const PkPledgeMissionInfo& sourceInfo : vList_) {
        const PkPledgeMissionInfo info(sourceInfo);
        if (!info.Encode(stream)) {
            return false;
        }
    }
    return true;
}

bool S_EX_PLEDGE_MISSION_INFO::Decode(network::Stream* stream) {
    std::int32_t missionInfoCount = 0;
    if (!stream->Decode(&missionInfoCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < missionInfoCount; ++index) {
        PkPledgeMissionInfo info;
        if (!info.Decode(stream)) {
            return false;
        }
        vList_.push_back(info);
    }
    return true;
}

bool C_EX_PLEDGE_MASTERY_INFO::Encode(network::Stream* stream) const { return stream->Encode(&cReserved); }

bool C_EX_PLEDGE_MASTERY_INFO::Decode(network::Stream* stream) { return stream->Decode(&cReserved); }

bool PkPledgeMasteryInfo::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool PkPledgeMasteryInfo::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool S_EX_PLEDGE_MASTERY_INFO::Encode(network::Stream* stream) const {
    const std::uint32_t masteryCount = static_cast<std::uint32_t>(vMasteries.size());
    if (!stream->Encode(&nTotalPoint) || !stream->Encode(&nMaxPoint) || !stream->Encode(&masteryCount)) {
        return false;
    }
    for (const PkPledgeMasteryInfo& sourceInfo : vMasteries) {
        const PkPledgeMasteryInfo info(sourceInfo);
        if (!info.Encode(stream)) {
            return false;
        }
    }
    return true;
}

bool S_EX_PLEDGE_MASTERY_INFO::Decode(network::Stream* stream) {
    std::int32_t masteryCount = 0;
    if (!stream->Decode(&nTotalPoint) || !stream->Decode(&nMaxPoint) || !stream->Decode(&masteryCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < masteryCount; ++index) {
        PkPledgeMasteryInfo info;
        if (!info.Decode(stream)) {
            return false;
        }
        vMasteries.push_back(info);
    }
    return true;
}

bool C_EX_PLEDGE_MASTERY_SET::Encode(network::Stream* stream) const { return stream->Encode(&nID); }

bool C_EX_PLEDGE_MASTERY_SET::Decode(network::Stream* stream) { return stream->Decode(&nID); }

bool S_EX_PLEDGE_MASTERY_SET::Encode(network::Stream* stream) const { return stream->Encode(&nResult); }

bool S_EX_PLEDGE_MASTERY_SET::Decode(network::Stream* stream) { return stream->Decode(&nResult); }

bool C_EX_PLEDGE_MASTERY_RESET::Encode(network::Stream* stream) const { return stream->Encode(&cReserved); }

bool C_EX_PLEDGE_MASTERY_RESET::Decode(network::Stream* stream) { return stream->Decode(&cReserved); }

bool S_EX_PLEDGE_MASTERY_RESET::Encode(network::Stream* stream) const { return stream->Encode(&nResult); }

bool S_EX_PLEDGE_MASTERY_RESET::Decode(network::Stream* stream) { return stream->Decode(&nResult); }

bool C_EX_PLEDGE_SKILL_INFO::Encode(network::Stream* stream) const {
    return stream->Encode(&nSkillID) && stream->Encode(&nSkillLevel);
}

bool C_EX_PLEDGE_SKILL_INFO::Decode(network::Stream* stream) {
    return stream->Decode(&nSkillID) && stream->Decode(&nSkillLevel);
}

bool S_EX_PLEDGE_SKILL_INFO::Encode(network::Stream* stream) const {
    return stream->Encode(&nSkillID) && stream->Encode(&nSkillLevel) && stream->Encode(&nRemainSecond) &&
        stream->Encode(&nStatus);
}

bool S_EX_PLEDGE_SKILL_INFO::Decode(network::Stream* stream) {
    return stream->Decode(&nSkillID) && stream->Decode(&nSkillLevel) && stream->Decode(&nRemainSecond) &&
        stream->Decode(&nStatus);
}

bool C_EX_PLEDGE_SKILL_ACTIVATE::Encode(network::Stream* stream) const {
    return stream->Encode(&nSkillID) && stream->Encode(&nSkillLevel);
}

bool C_EX_PLEDGE_SKILL_ACTIVATE::Decode(network::Stream* stream) {
    return stream->Decode(&nSkillID) && stream->Decode(&nSkillLevel);
}

bool S_EX_PLEDGE_SKILL_ACTIVATE::Encode(network::Stream* stream) const { return stream->Encode(&nResult); }

bool S_EX_PLEDGE_SKILL_ACTIVATE::Decode(network::Stream* stream) { return stream->Decode(&nResult); }

bool PkPledgeItemInfo::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool PkPledgeItemInfo::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool C_EX_PLEDGE_ITEM_LIST::Encode(network::Stream* stream) const { return stream->Encode(&cReserved); }

bool C_EX_PLEDGE_ITEM_LIST::Decode(network::Stream* stream) { return stream->Decode(&cReserved); }

bool C_EX_PLEDGE_ITEM_INFO::Encode(network::Stream* stream) const { return stream->Encode(&nItemClassID); }

bool C_EX_PLEDGE_ITEM_INFO::Decode(network::Stream* stream) { return stream->Decode(&nItemClassID); }

bool C_EX_PLEDGE_ITEM_BUY::Encode(network::Stream* stream) const {
    return stream->Encode(&nItemClassID) && stream->Encode(&nItemAmount);
}

bool C_EX_PLEDGE_ITEM_BUY::Decode(network::Stream* stream) {
    return stream->Decode(&nItemClassID) && stream->Decode(&nItemAmount);
}

bool S_EX_PLEDGE_ITEM_BUY::Encode(network::Stream* stream) const { return stream->Encode(&nResult); }

bool S_EX_PLEDGE_ITEM_BUY::Decode(network::Stream* stream) { return stream->Decode(&nResult); }

bool C_EX_PLEDGE_ITEM_ACTIVATE::Encode(network::Stream* stream) const { return stream->Encode(&nItemClassId); }

bool C_EX_PLEDGE_ITEM_ACTIVATE::Decode(network::Stream* stream) { return stream->Decode(&nItemClassId); }

bool S_EX_PLEDGE_ITEM_ACTIVATE::Encode(network::Stream* stream) const { return stream->Encode(&nResult); }

bool S_EX_PLEDGE_ITEM_ACTIVATE::Decode(network::Stream* stream) { return stream->Decode(&nResult); }

bool C_EX_PLEDGE_ANNOUNCE::Encode(network::Stream* stream) const { return stream->Encode(&cReserved); }

bool C_EX_PLEDGE_ANNOUNCE::Decode(network::Stream* stream) { return stream->Decode(&cReserved); }

bool S_EX_PLEDGE_ANNOUNCE::Encode(network::Stream* stream) const {
    return stream->Encode(&content) && stream->Encode(&bShow);
}

bool S_EX_PLEDGE_ANNOUNCE::Decode(network::Stream* stream) { return stream->Decode(&content) && stream->Decode(&bShow); }

bool C_EX_PLEDGE_ANNOUNCE_SET::Encode(network::Stream* stream) const {
    return stream->Encode(&content) && stream->Encode(&bShow);
}

bool C_EX_PLEDGE_ANNOUNCE_SET::Decode(network::Stream* stream) { return stream->Decode(&content) && stream->Decode(&bShow); }

bool S_EX_PLEDGE_ANNOUNCE_SET::Encode(network::Stream* stream) const { return stream->Encode(&bSuccess); }

bool S_EX_PLEDGE_ANNOUNCE_SET::Decode(network::Stream* stream) { return stream->Decode(&bSuccess); }

bool AgitSiegeInfo::Encode(network::Stream* stream) const {
    return stream->Encode(&nAgitId) && stream->Encode(&nSiegeDate) && stream->Encode(&wstrOwnerPledgeName) &&
        stream->Encode(&wstrOwnerPledgeMasterName) && stream->Encode(&cAgitType) && stream->Encode(&cSiegeState);
}

bool AgitSiegeInfo::Decode(network::Stream* stream) {
    return stream->Decode(&nAgitId) && stream->Decode(&nSiegeDate) && stream->Decode(&wstrOwnerPledgeName) &&
        stream->Decode(&wstrOwnerPledgeMasterName) && stream->Decode(&cAgitType) && stream->Decode(&cSiegeState);
}

bool S_EX_SHOW_AGIT_SIEGE_INFO::Encode(network::Stream* stream) const {
    const std::uint32_t agitInfoCount = static_cast<std::uint32_t>(vAgitInfos.size());
    if (!stream->Encode(&agitInfoCount)) {
        return false;
    }
    for (const AgitSiegeInfo& sourceInfo : vAgitInfos) {
        const AgitSiegeInfo info(sourceInfo);
        if (!info.Encode(stream)) {
            return false;
        }
    }
    return true;
}

bool S_EX_SHOW_AGIT_SIEGE_INFO::Decode(network::Stream* stream) {
    std::int32_t agitInfoCount = 0;
    if (!stream->Decode(&agitInfoCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < agitInfoCount; ++index) {
        AgitSiegeInfo info;
        if (!info.Decode(stream)) {
            return false;
        }
        vAgitInfos.push_back(info);
    }
    return true;
}

bool C_EX_REQUEST_LOCKED_ITEM::Encode(network::Stream* stream) const { return stream->Encode(&nTargetItemId); }

bool C_EX_REQUEST_LOCKED_ITEM::Decode(network::Stream* stream) { return stream->Decode(&nTargetItemId); }

bool C_EX_REQUEST_UNLOCKED_ITEM::Encode(network::Stream* stream) const { return stream->Encode(&nTargetItemId); }

bool C_EX_REQUEST_UNLOCKED_ITEM::Decode(network::Stream* stream) { return stream->Decode(&nTargetItemId); }

bool C_EX_LOCKED_ITEM_CANCEL::Encode(network::Stream* stream) const { return stream->Encode(&dummy); }

bool C_EX_LOCKED_ITEM_CANCEL::Decode(network::Stream* stream) { return stream->Decode(&dummy); }

bool C_EX_UNLOCKED_ITEM_CANCEL::Encode(network::Stream* stream) const { return stream->Encode(&dummy); }

bool C_EX_UNLOCKED_ITEM_CANCEL::Decode(network::Stream* stream) { return stream->Decode(&dummy); }

bool S_EX_CHOOSE_LOCKED_ITEM::Encode(network::Stream* stream) const { return stream->Encode(&nScrollItemClassID); }

bool S_EX_CHOOSE_LOCKED_ITEM::Decode(network::Stream* stream) { return stream->Decode(&nScrollItemClassID); }

bool S_EX_LOCKED_RESULT::Encode(network::Stream* stream) const { return stream->Encode(&lock) && stream->Encode(&result); }

bool S_EX_LOCKED_RESULT::Decode(network::Stream* stream) { return stream->Decode(&lock) && stream->Decode(&result); }

bool ItemServerInfo::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool ItemServerInfo::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool ItemInfo::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool ItemInfo::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool ItemRollyResultInfo::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool ItemRollyResultInfo::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool AlchemySkillServerInfo::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool AlchemySkillServerInfo::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
}

bool AlchemySkillClientInfo::Encode(network::Stream* stream) const {
    return stream->Encode(&nSkillId) && stream->Encode(&nLev);
}

bool AlchemySkillClientInfo::Decode(network::Stream* stream) { return stream->Decode(&nSkillId) && stream->Decode(&nLev); }

bool C_AlchemyTryMixCube::Encode(network::Stream* stream) const {
    const std::uint32_t itemInfoCount = static_cast<std::uint32_t>(itemInfoList.size());
    if (!stream->Encode(&itemInfoCount)) {
        return false;
    }
    for (const ItemServerInfo& sourceInfo : itemInfoList) {
        const ItemServerInfo info(sourceInfo);
        if (!info.Encode(stream)) {
            return false;
        }
    }
    return true;
}

bool C_AlchemyTryMixCube::Decode(network::Stream* stream) {
    std::int32_t itemInfoCount = 0;
    if (!stream->Decode(&itemInfoCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < itemInfoCount; ++index) {
        ItemServerInfo info;
        if (!info.Decode(stream)) {
            return false;
        }
        itemInfoList.push_back(info);
    }
    return true;
}

bool S_AlchemyTryMixCube::Encode(network::Stream* stream) const {
    const std::uint32_t resultCount = static_cast<std::uint32_t>(resultList.size());
    if (!stream->Encode(&nSuccessInfo) || !stream->Encode(&resultCount)) {
        return false;
    }
    for (const ItemRollyResultInfo& sourceInfo : resultList) {
        const ItemRollyResultInfo info(sourceInfo);
        if (!info.Encode(stream)) {
            return false;
        }
    }
    return true;
}

bool S_AlchemyTryMixCube::Decode(network::Stream* stream) {
    std::int32_t resultCount = 0;
    if (!stream->Decode(&nSuccessInfo) || !stream->Decode(&resultCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < resultCount; ++index) {
        ItemRollyResultInfo info;
        if (!info.Decode(stream)) {
            return false;
        }
        resultList.push_back(info);
    }
    return true;
}

bool S_AlchemySkillList::Encode(network::Stream* stream) const {
    const std::uint32_t skillInfoCount = static_cast<std::uint32_t>(skillInfoList.size());
    if (!stream->Encode(&skillInfoCount)) {
        return false;
    }
    for (const AlchemySkillServerInfo& sourceInfo : skillInfoList) {
        const AlchemySkillServerInfo info(sourceInfo);
        if (!info.Encode(stream)) {
            return false;
        }
    }
    return true;
}

bool S_AlchemySkillList::Decode(network::Stream* stream) {
    std::int32_t skillInfoCount = 0;
    if (!stream->Decode(&skillInfoCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < skillInfoCount; ++index) {
        AlchemySkillServerInfo info;
        if (!info.Decode(stream)) {
            return false;
        }
        skillInfoList.push_back(info);
    }
    return true;
}

bool C_AlchemyConversion::Encode(network::Stream* stream) const {
    if (!stream->Encode(&nTryCount)) {
        return false;
    }

    unsigned char* const frameStart = stream->GetCursor();
    std::int16_t frameSize = 0;
    if (!stream->Encode(&frameSize) || !info.Encode(stream)) {
        return false;
    }
    unsigned char* const frameEnd = stream->GetCursor();
    frameSize = static_cast<std::int16_t>(frameEnd - frameStart);
    stream->SetCursor(frameStart);
    if (!stream->Encode(&frameSize)) {
        return false;
    }
    stream->SetCursor(frameEnd);

    const std::uint32_t recipeCount = static_cast<std::uint32_t>(recipeList.size());
    if (!stream->Encode(&recipeCount)) {
        return false;
    }
    for (const ItemInfo& sourceInfo : recipeList) {
        const ItemInfo recipe(sourceInfo);
        if (!recipe.Encode(stream)) {
            return false;
        }
    }
    return true;
}

bool C_AlchemyConversion::Decode(network::Stream* stream) {
    if (!stream->Decode(&nTryCount)) {
        return false;
    }

    unsigned char* const frameStart = stream->GetCursor();
    std::int16_t frameSize = 0;
    if (!stream->Decode(&frameSize) || !info.Decode(stream)) {
        return false;
    }
    unsigned char* const decodedEnd = stream->GetCursor();
    const std::ptrdiff_t decodedSize = decodedEnd - frameStart;
    if (decodedSize > frameSize) {
        return false;
    }

    unsigned char* const buffer = stream->GetBuffer();
    const std::ptrdiff_t frameOffset = frameStart - buffer;
    const std::ptrdiff_t frameEndOffset = frameOffset + frameSize;
    if (frameEndOffset < 0 || frameEndOffset > stream->GetSize()) {
        return false;
    }
    stream->SetCursor(buffer + frameEndOffset);

    std::int32_t recipeCount = 0;
    if (!stream->Decode(&recipeCount)) {
        return false;
    }
    for (std::int32_t index = 0; index < recipeCount; ++index) {
        ItemInfo recipe;
        if (!recipe.Decode(stream)) {
            return false;
        }
        recipeList.push_back(recipe);
    }
    return true;
}

bool S_AlchemyConversion::Encode(network::Stream* stream) const {
    return stream->Encode(&nSuccessInfo) && stream->Encode(&nSuccessCount) && stream->Encode(&nFailCount);
}

bool S_AlchemyConversion::Decode(network::Stream* stream) {
    return stream->Decode(&nSuccessInfo) && stream->Decode(&nSuccessCount) && stream->Decode(&nFailCount);
}

} // namespace protocol

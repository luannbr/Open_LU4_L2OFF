#include "Shared/src/DXPacket.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"

#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)
#include "Shared/src/Log.h"
#endif

#if defined(L2_BUILD_CACHED)
#include "CAccount.h"
#include "Cached.h"
#include "NewbieDb.h"
#include "NpcDbSocket.h"
#include "Pledge.h"
#include "PledgeContributionInCache.h"
#include "PledgeDb.h"
#include "Shared/src/Runtime/Time32.h"
#include "SevenSignQuestDB.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/LogFormatter.h"
#include "TimeAttackDB.h"
#include "User.h"
#include "UserDb.h"
#include "WareHouse.h"
#endif

#if defined(L2_BUILD_L2SERVER)
#include "Shared/src/UserConst.h"
#include "../../L2Server/src/AccountJobFinishSignal.h"
#include "../../L2Server/src/Agit.h"
#include "../../L2Server/src/Castle.h"
#include "../../L2Server/src/DB.h"
#include "../../L2Server/src/DecoNpcData.h"
#include "../../L2Server/src/Fortress.h"
#include "../../L2Server/src/InstantAgit.h"
#include "../../L2Server/src/Item.h"
#include "../../L2Server/src/MainEvent.h"
#include "../../L2Server/src/MatchingPointDB.h"
#include "../../L2Server/src/NPC.h"
#include "../../L2Server/src/NpcServer.h"
#include "../../L2Server/src/ObjectDB.h"
#include "../../L2Server/src/OlympiadNetwork.h"
#include "../../L2Server/src/OlympiadService.h"
#include "../../L2Server/src/OlympiadStuff.h"
#include "../../L2Server/src/Packet.h"
#include "../../L2Server/src/Pledge.h"
#include "../../L2Server/src/PledgeGameSystem.h"
#include "../../L2Server/src/PledgeStuff.h"
#include "../../L2Server/src/Relay.h"
#include "../../L2Server/src/Residence.h"
#include "../../L2Server/src/SSQ.h"
#include "../../L2Server/src/User.h"
#include "../../L2Server/src/UserSocket.h"
#include "../../L2Server/src/WHeroSystem.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Config.h"
#include "Shared/src/DateTime.h"
#include "Shared/src/GIP.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/QueryPacket.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

constexpr int kMaximumTimeAttackMembers = 9;
constexpr std::size_t kMaximumTimeAttackMemberNameLength = 24;

#if defined(L2_BUILD_CACHED)

constexpr int kNewbieCharacterLevelLimit = 6;
constexpr int kNewbieAccountLevelLimit = 25;
constexpr int kRenewResidenceDecoLogId = 0x10e;
constexpr int kLogBoth = 6;

// Current target global 0x02cefc04. Neither available PDB keeps its
// source identifier, and the target has no reader, but DxLoadPledgeGame still
// performs the write after every database attempt.
std::int32_t g_previousPledgeGameWinnerCharacterDatabaseId = 0;

// Current SQL binding helper 0x0054af44 is the 32-bit DUMMY/enum template,
// not DBConn's ordinary int specialization. The original enum identifier is
// absent from both available PDBs. This private semantic type keeps the
// observable parameter contract without adding public API.
enum class ResidenceDecoActivation : int {
    Inactive = 0,
    Active = 1,
};

void Write_LOG_WOLYMPIAD_SAVE_MATCH_POINT_InHost(
    int worldId, int userDatabaseId, std::int64_t newPoint, std::int64_t oldPoint, int matchId
) {
    // Current anonymous Shared/DXPacket.cpp owner at 0x004ae4ec. The PDB
    // keeps this exact helper name and argument list.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(userDatabaseId, false, false, worldId);
    if (!user) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD]Cannot find user in save match point: "
        //     L"%d, %d, %I64d",
        //     worldId,
        //     userDatabaseId,
        //     newPoint)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[world olympiad] Matching-point audit record omitted: character DB ID %d in world %d was not found. Match
        //     %d, previous points %lld, new points %lld.", userDatabaseId, worldId, matchId, static_cast<long
        //     long>(oldPoint), static_cast<long long>(newPoint))
        L2_LOG_ERROR(
            Log,
            u"[world olympiad] Matching-point audit record omitted: character DB ID %d in world %d was not found; match %d, "
            u"previous points %lld, new points %lld.",
            userDatabaseId, worldId, matchId, static_cast<long long>(oldPoint), static_cast<long long>(newPoint)
        );
        return;
    }

    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fe8, user->GetId(), user->GetAccountID(), 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, user->GetClass(),
        user->GetLevel(), oldPoint, newPoint, matchId, 0, 0, 0, 0, worldId, characterName.c_str(), accountName.c_str(),
        nullptr, nullptr, 0
    );
}

void WriteRenewResidenceDecoAuditLog(
    int pledgeId, const char16_t* pledgeName, int agitId, std::int64_t price, short type, int expire
) {
    // Current 0x004b0aef calls CLog::Add with this exact 26-field format:
    // "%I64d,%I64d,,,,,,,%s,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,,,,,".
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(kRenewResidenceDecoLogId);
    fields[1] = runtime::DecimalUtf16(pledgeId);
    if (pledgeName) {
        std::size_t length = 0;
        while (length < 24 && pledgeName[length] != u'\0') {
            ++length;
        }
        fields[8].assign(pledgeName, pledgeName + length);
    }
    fields[11] = runtime::DecimalUtf16(agitId);
    fields[12] = runtime::DecimalUtf16(pledgeId);
    fields[13] = runtime::DecimalUtf16(price);
    fields[14] = runtime::DecimalUtf16(type);
    fields[15] = runtime::DecimalUtf16(expire);

    std::u16string record;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) {
            record.push_back(u',');
        }
        record.append(fields[index]);
    }
    runtime::process::WriteLogFormatterLine(kLogBoth, record);
}

void WriteDecoNpcCostLog(
    int logId,
    int pledgeId,
    const char16_t* pledgeName,
    int residenceId,
    int agitSlot,
    std::uint32_t itemType,
    int itemCount,
    int expireTime
) {
    // Current anonymous Shared/DXPacket.cpp owner at 0x004aa99c. Its PDB
    // signature has eight arguments and the machine code emits this exact
    // 26-field LogFormatter record.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(logId);
    fields[1] = runtime::DecimalUtf16(pledgeId);
    if (pledgeName) {
        std::size_t length = 0;
        while (length < 24 && pledgeName[length] != u'\0') {
            ++length;
        }
        fields[8].assign(pledgeName, pledgeName + length);
    }
    fields[11] = runtime::DecimalUtf16(residenceId);
    fields[12] = runtime::DecimalUtf16(agitSlot);
    fields[13] = runtime::DecimalUtf16(itemType);
    fields[14] = runtime::DecimalUtf16(itemCount);
    fields[15] = runtime::DecimalUtf16(expireTime);

    std::u16string record;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) {
            record.push_back(u',');
        }
        record.append(fields[index]);
    }
    runtime::process::WriteLogFormatterLine(kLogBoth, record);
}

#endif

void CopyTargetMemberName(char16_t (&destination)[25], const char16_t* source) {
    std::size_t copied = 0;
    while (copied < kMaximumTimeAttackMemberNameLength && source[copied] != u'\0') {
        destination[copied] = source[copied];
        ++copied;
    }
    destination[copied] = u'\0';
    destination[kMaximumTimeAttackMemberNameLength] = u'\0';
}

template <std::size_t Capacity>
void CopyFixedTargetString(char16_t (&destination)[Capacity], const char16_t* source) {
    std::fill(std::begin(destination), std::end(destination), u'\0');
    if (source == nullptr) {
        return;
    }
    std::size_t copied = 0;
    while (copied + 1 < Capacity && source[copied] != u'\0') {
        destination[copied] = source[copied];
        ++copied;
    }
}

template <std::size_t HostCapacity, std::size_t TargetCapacity>
std::array<char16_t, HostCapacity> MakeUtf16ResultBuffer(const char16_t (&source)[TargetCapacity]) {
    std::array<char16_t, HostCapacity> result{};
    const std::u16string_view hostValue(
        source, static_cast<std::size_t>(std::find(std::begin(source), std::end(source), u'\0') - std::begin(source))
    );
    const std::size_t copied = std::min(hostValue.size(), HostCapacity - 1);
    std::copy_n(hostValue.data(), copied, result.data());
    result[copied] = u'\0';
    return result;
}

template <std::size_t TargetCapacity, std::size_t HostCapacity>
void CopyUtf16Result(
    char16_t (&destination)[TargetCapacity], const std::array<char16_t, HostCapacity>& source, std::size_t targetBufferBytes
) {
    const std::size_t targetUnits = std::min(TargetCapacity, targetBufferBytes / sizeof(char16_t));
    if (targetUnits == 0) {
        return;
    }

    const std::u16string_view targetValue(source.data());
    const std::size_t copied = std::min(targetValue.size(), targetUnits - 1);
    std::copy_n(targetValue.data(), copied, destination);
    destination[copied] = u'\0';
}


#if defined(L2_BUILD_L2SERVER)

CSPointer<User> GetUserBySID(int userSid) {
    L2_GUARD_FUNCTION();
    if (userSid < 0) {
        return {};
    }
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    if (creature == nullptr || !creature->IsPC()) {
        return {};
    }
    return CSPointer<User>(creature->CastUser(), ObjectRefType::FunctionChain);
}

void SendSystemMessageItemCID(CSPointer<User> user, unsigned int messageId, int itemClassId) {
    if (!user) {
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket != nullptr) {
        socket->Send("chccd", 0x62, static_cast<int>(messageId), 1, 3, itemClassId);
    }
}

void EndResidenceDecoNpcJob(CResidence* const residence, const int line) {
    if (residence != nullptr && !residence->m_decoNpcJobLock.EndJob()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Failed to end job at %s(%d)",
        //     L"..\\Shared\\DXPacket.cpp",
        //     line)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] Cannot finish the %ls operation for residence %d: no job was tracked.",
        //     line == 0x337 ? L"set" : line == 0x3d5 ? L"reset" : line == 0x44c ? L"expire" : L"decoration",
        //     residence->GetResidenceID())
        L2_LOG_ERROR(
            Log, u"[decoration NPCs] Cannot finish the %ls operation for residence %d: no job was tracked.",
            line == 0x337       ? L"set"
                : line == 0x3d5 ? L"reset"
                : line == 0x44c ? L"expire"
                                : L"decoration",
            residence->GetResidenceID()
        );
    }
}

void DeleteResidenceDecoNpc(const int npcSid) {
    CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(npcSid));
    if (object == nullptr || !object->IsNPC()) {
        return;
    }
    CNPC* const npc = object->CastNPC();
    if (npc != nullptr && !npc->IsDeleting() && npc->m_Deleted == 0) {
        npc->Delete();
    }
}

#endif

} // namespace

#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)

DxPacketCallbackMap g_DxPacketTable(DummyDXPacket, u"DX_PROTOCOL");

bool DummyDXPacket(DxPacketSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyDXPacket called in DX Packet")
    // Noise: All current dispatchers report the decoded protocol ID before calling this fallback. A second context-free
    // record adds no information. Log.Add(
    //     LOG_ERROR,
    //     "[database protocol] Unhandled DX packet, payload ignored.")
    return false;
}

#if defined(L2_BUILD_CACHED)

template <class Packet, int Protocol>
bool TPacketProcess<Packet, Protocol>::DoJob(DxPacketSocket* socket, const std::uint8_t* packet) {
    static_assert(Protocol >= 0 && Protocol < 116);
    Packet* const job = reinterpret_cast<Packet*>(const_cast<std::uint8_t*>(packet));
    if (job->DoCacheDJob()) {
        socket->Send("chb", 0xc0, Protocol, static_cast<int>(sizeof(Packet)), job);
    }
    return false;
}

#elif defined(L2_BUILD_L2SERVER)

template <class Packet, int Protocol>
bool TPacketProcess<Packet, Protocol>::DoJob(DxPacketSocket*, const std::uint8_t* packet) {
    static_assert(Protocol >= 0 && Protocol < 116);
    Packet* const job = reinterpret_cast<Packet*>(const_cast<std::uint8_t*>(packet));
    job->DoServerJob();
    return false;
}

#endif

namespace {

L2_BIND_PACKET(IsRegistered_CNewbieCheck, g_DxPacketTable, 0, TPacketProcess<CNewbieCheck, 0>::DoJob);
L2_BIND_PACKET(IsRegistered_CSetResidenceDeco, g_DxPacketTable, 1, TPacketProcess<CSetResidenceDeco, 1>::DoJob);
L2_BIND_PACKET(IsRegistered_CResetResidenceDeco, g_DxPacketTable, 2, TPacketProcess<CResetResidenceDeco, 2>::DoJob);
L2_BIND_PACKET(IsRegistered_CResetAllResidenceDeco, g_DxPacketTable, 3, TPacketProcess<CResetAllResidenceDeco, 3>::DoJob);
L2_BIND_PACKET(IsRegistered_CRenewResidenceDeco, g_DxPacketTable, 4, TPacketProcess<CRenewResidenceDeco, 4>::DoJob);
L2_BIND_PACKET(IsRegistered_DxChangeSSQPeriod, g_DxPacketTable, 5, TPacketProcess<DxChangeSSQPeriod, 5>::DoJob);
L2_BIND_PACKET(IsRegistered_DxStartSSQAccount, g_DxPacketTable, 6, TPacketProcess<DxStartSSQAccount, 6>::DoJob);
L2_BIND_PACKET(IsRegistered_DxStartSSQSealEffect, g_DxPacketTable, 7, TPacketProcess<DxStartSSQSealEffect, 7>::DoJob);
L2_BIND_PACKET(IsRegistered_DxChangeSSQStatus, g_DxPacketTable, 8, TPacketProcess<DxChangeSSQStatus, 8>::DoJob);
L2_BIND_PACKET(IsRegistered_DxSetTicketBuyCount, g_DxPacketTable, 9, TPacketProcess<DxSetTicketBuyCount, 9>::DoJob);
L2_BIND_PACKET(
    IsRegistered_DxSetSevenSignQuestResult, g_DxPacketTable, 10, TPacketProcess<DxSetSevenSignQuestResult, 10>::DoJob
);
L2_BIND_PACKET(IsRegistered_DxCloseSevenSignQuest, g_DxPacketTable, 11, TPacketProcess<DxCloseSevenSignQuest, 11>::DoJob);
L2_BIND_PACKET(
    IsRegistered_DxAddSevenSignQuestMember, g_DxPacketTable, 12, TPacketProcess<DxAddSevenSignQuestMember, 12>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_DxDeleteDepositedSevenSignQuestItem,
    g_DxPacketTable,
    13,
    TPacketProcess<DxDeleteDepositedSevenSignQuestItem, 13>::DoJob
);
L2_BIND_PACKET(IsRegistered_DxAddTimeAttackRecord, g_DxPacketTable, 14, TPacketProcess<DxAddTimeAttackRecord, 14>::DoJob);
L2_BIND_PACKET(
    IsRegistered_DxChangeSealSelectionCount, g_DxPacketTable, 15, TPacketProcess<DxChangeSealSelectionCount, 15>::DoJob
);
L2_BIND_PACKET(IsRegistered_DxChangeSSQUserPoint, g_DxPacketTable, 16, TPacketProcess<DxChangeSSQUserPoint, 16>::DoJob);
L2_BIND_PACKET(IsRegistered_DxStartQuickCycle, g_DxPacketTable, 17, TPacketProcess<DxStartQuickCycle, 17>::DoJob);
L2_BIND_PACKET(IsRegistered_DxChangeMainEventPoint, g_DxPacketTable, 18, TPacketProcess<DxChangeMainEventPoint, 18>::DoJob);
L2_BIND_PACKET(IsRegistered_DxAddTimeAttackFee, g_DxPacketTable, 19, TPacketProcess<DxAddTimeAttackFee, 19>::DoJob);
L2_BIND_PACKET(
    IsRegistered_DxSetTimeAttackRewardFlag, g_DxPacketTable, 20, TPacketProcess<DxSetTimeAttackRewardFlag, 20>::DoJob
);
L2_BIND_PACKET(IsRegistered_DxChangeSealOwner, g_DxPacketTable, 21, TPacketProcess<DxChangeSealOwner, 21>::DoJob);
L2_BIND_PACKET(IsRegistered_DxChangeSSQWinner, g_DxPacketTable, 22, TPacketProcess<DxChangeSSQWinner, 22>::DoJob);
L2_BIND_PACKET(
    IsRegistered_DxDismissSevenSignMember, g_DxPacketTable, 23, TPacketProcess<DxDismissSevenSignMember, 23>::DoJob
);
L2_BIND_PACKET(IsRegistered_DxChangeCycleMode, g_DxPacketTable, 24, TPacketProcess<DxChangeCycleMode, 24>::DoJob);
L2_BIND_PACKET(
    IsRegistered_DxDepositSevenSignQuestItemEx, g_DxPacketTable, 25, TPacketProcess<DxDepositSevenSignQuestItemEx, 25>::DoJob
);
L2_BIND_PACKET(IsRegistered_DxSaveSiegeKillDeath, g_DxPacketTable, 26, TPacketProcess<DxSaveSiegeKillDeath, 26>::DoJob);
L2_BIND_PACKET(IsRegistered_DxSaveNoblessType, g_DxPacketTable, 27, TPacketProcess<DxSaveNoblessType, 27>::DoJob);
L2_BIND_PACKET(IsRegistered_DxSaveHeroType, g_DxPacketTable, 28, TPacketProcess<DxSaveHeroType, 28>::DoJob);
L2_BIND_PACKET(
    IsRegistered_DxWriteNoblessAchievement, g_DxPacketTable, 29, TPacketProcess<DxWriteNoblessAchievement, 29>::DoJob
);
L2_BIND_PACKET(IsRegistered_DxReloadOlympiadPoint, g_DxPacketTable, 30, TPacketProcess<DxReloadOlympiadPoint, 30>::DoJob);
L2_BIND_PACKET(IsRegistered_DxDeclareWar, g_DxPacketTable, 31, TPacketProcess<DxDeclareWar, 31>::DoJob);
L2_BIND_PACKET(IsRegistered_DxCancelWar, g_DxPacketTable, 32, TPacketProcess<DxCancelWar, 32>::DoJob);
L2_BIND_PACKET(IsRegistered_DxLoadOlympiad, g_DxPacketTable, 33, TPacketProcess<DxLoadOlympiad, 33>::DoJob);
L2_BIND_PACKET(IsRegistered_DxLoadPledgeGame, g_DxPacketTable, 34, TPacketProcess<DxLoadPledgeGame, 34>::DoJob);
L2_BIND_PACKET(IsRegistered_DxSetClass, g_DxPacketTable, 35, TPacketProcess<DxSetClass, 35>::DoJob);
L2_BIND_PACKET(IsRegistered_DxWriteHeroWords, g_DxPacketTable, 36, TPacketProcess<DxWriteHeroWords, 36>::DoJob);
L2_BIND_PACKET(IsRegistered_DxStartOlympiadSeason, g_DxPacketTable, 37, TPacketProcess<DxStartOlympiadSeason, 37>::DoJob);
L2_BIND_PACKET(IsRegistered_DxInitAllOlympiadPoint, g_DxPacketTable, 38, TPacketProcess<DxInitAllOlympiadPoint, 38>::DoJob);
L2_BIND_PACKET(
    IsRegistered_DxAddAllOlympiadBonusPoint, g_DxPacketTable, 39, TPacketProcess<DxAddAllOlympiadBonusPoint, 39>::DoJob
);
L2_BIND_PACKET(IsRegistered_DxNominateHeroes, g_DxPacketTable, 40, TPacketProcess<DxNominateHeroes, 40>::DoJob);
L2_BIND_PACKET(IsRegistered_DxSaveOlympiadRecord, g_DxPacketTable, 41, TPacketProcess<DxSaveOlympiadRecord, 41>::DoJob);
L2_BIND_PACKET(IsRegistered_DxReloadHeroRank, g_DxPacketTable, 42, TPacketProcess<DxReloadHeroRank, 42>::DoJob);
L2_BIND_PACKET(IsRegistered_DxReloadHeroById, g_DxPacketTable, 43, TPacketProcess<DxReloadHeroById, 43>::DoJob);
L2_BIND_PACKET(
    IsRegistered_DxDeletePreviousOlympiadPoint, g_DxPacketTable, 44, TPacketProcess<DxDeletePreviousOlympiadPoint, 44>::DoJob
);
L2_BIND_PACKET(IsRegistered_DxSaveSeasonStartTime, g_DxPacketTable, 45, TPacketProcess<DxSaveSeasonStartTime, 45>::DoJob);
L2_BIND_PACKET(IsRegistered_DxSaveOlympiadTerm, g_DxPacketTable, 46, TPacketProcess<DxSaveOlympiadTerm, 46>::DoJob);
L2_BIND_PACKET(IsRegistered_DxSetHistoryOpen, g_DxPacketTable, 47, TPacketProcess<DxSetHistoryOpen, 47>::DoJob);
L2_BIND_PACKET(
    IsRegistered_DxDeleteOlympiadTradePoint, g_DxPacketTable, 48, TPacketProcess<DxDeleteOlympiadTradePoint, 48>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_DxAddOlympiadTradePoint, g_DxPacketTable, 49, TPacketProcess<DxAddOlympiadTradePoint, 49>::DoJob
);
L2_BIND_PACKET(IsRegistered_CDxSetFortressFacility, g_DxPacketTable, 50, TPacketProcess<CDxSetFortressFacility, 50>::DoJob);
L2_BIND_PACKET(
    IsRegistered_CDxResetFortressFacility, g_DxPacketTable, 51, TPacketProcess<CDxResetFortressFacility, 51>::DoJob
);
L2_BIND_PACKET(IsRegistered_DxAddOlympiadPoint, g_DxPacketTable, 52, TPacketProcess<DxAddOlympiadPoint, 52>::DoJob);
L2_BIND_PACKET(IsRegistered_CApplyInstantAgit, g_DxPacketTable, 53, TPacketProcess<CApplyInstantAgit, 53>::DoJob);
L2_BIND_PACKET(IsRegistered_CAddInstantAgitDeco, g_DxPacketTable, 54, TPacketProcess<CAddInstantAgitDeco, 54>::DoJob);
L2_BIND_PACKET(IsRegistered_CReturnInstantAgit, g_DxPacketTable, 55, TPacketProcess<CReturnInstantAgit, 55>::DoJob);
L2_BIND_PACKET(IsRegistered_DxResetOlympiadPoint, g_DxPacketTable, 56, TPacketProcess<DxResetOlympiadPoint, 56>::DoJob);
L2_BIND_PACKET(IsRegistered_DxFishingChampionShip, g_DxPacketTable, 57, TPacketProcess<DxFishingChampionShip, 57>::DoJob);
L2_BIND_PACKET(IsRegistered_DxMatchingPoint, g_DxPacketTable, 58, TPacketProcess<DxMatchingPoint, 58>::DoJob);
L2_BIND_PACKET(IsRegistered_CSetResidenceDecoNpc, g_DxPacketTable, 59, TPacketProcess<CSetResidenceDecoNpc, 59>::DoJob);
L2_BIND_PACKET(IsRegistered_CResetResidenceDecoNpc, g_DxPacketTable, 60, TPacketProcess<CResetResidenceDecoNpc, 60>::DoJob);
L2_BIND_PACKET(
    IsRegistered_CResetAllResidenceDecoNpc, g_DxPacketTable, 61, TPacketProcess<CResetAllResidenceDecoNpc, 61>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CExpireResidenceDecoNpc, g_DxPacketTable, 62, TPacketProcess<CExpireResidenceDecoNpc, 62>::DoJob
);
L2_BIND_PACKET(IsRegistered_CSetInstantAgitDecoNpc, g_DxPacketTable, 63, TPacketProcess<CSetInstantAgitDecoNpc, 63>::DoJob);
L2_BIND_PACKET(
    IsRegistered_CResetInstantAgitDecoNpc, g_DxPacketTable, 64, TPacketProcess<CResetInstantAgitDecoNpc, 64>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CExpireInstantAgitDecoNpc, g_DxPacketTable, 65, TPacketProcess<CExpireInstantAgitDecoNpc, 65>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_CHECK_PEER, g_DxPacketTable, 66, TPacketProcess<CDX_WOLYMPIAD_CHECK_PEER, 66>::DoJob
);
L2_BIND_PACKET(IsRegistered_CDX_WOLYMPIAD_LOAD, g_DxPacketTable, 67, TPacketProcess<CDX_WOLYMPIAD_LOAD, 67>::DoJob);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_GET_TOP10, g_DxPacketTable, 68, TPacketProcess<CDX_WOLYMPIAD_GET_TOP10, 68>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_GET_PLAYER_COUNT,
    g_DxPacketTable,
    69,
    TPacketProcess<CDX_WOLYMPIAD_GET_PLAYER_COUNT, 69>::DoJob
);
L2_BIND_PACKET(IsRegistered_CDX_WOLYMPIAD_ADD_USER, g_DxPacketTable, 70, TPacketProcess<CDX_WOLYMPIAD_ADD_USER, 70>::DoJob);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_SAVE_TERM, g_DxPacketTable, 71, TPacketProcess<CDX_WOLYMPIAD_SAVE_TERM, 71>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_NOMINATE_HEROES, g_DxPacketTable, 72, TPacketProcess<CDX_WOLYMPIAD_NOMINATE_HEROES, 72>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_RELOAD_HERO_RANK,
    g_DxPacketTable,
    73,
    TPacketProcess<CDX_WOLYMPIAD_RELOAD_HERO_RANK, 73>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_SAVE_MATCH_COUNT,
    g_DxPacketTable,
    74,
    TPacketProcess<CDX_WOLYMPIAD_SAVE_MATCH_COUNT, 74>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_LOG_PENALTY, g_DxPacketTable, 75, TPacketProcess<CDX_WOLYMPIAD_LOG_PENALTY, 75>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_RELOAD_USER, g_DxPacketTable, 76, TPacketProcess<CDX_WOLYMPIAD_RELOAD_USER, 76>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_INIT_ALL_POINT, g_DxPacketTable, 77, TPacketProcess<CDX_WOLYMPIAD_INIT_ALL_POINT, 77>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_ADD_ALL_POINT, g_DxPacketTable, 78, TPacketProcess<CDX_WOLYMPIAD_ADD_ALL_POINT, 78>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_SAVE_RECORD, g_DxPacketTable, 79, TPacketProcess<CDX_WOLYMPIAD_SAVE_RECORD, 79>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_SAVE_SEASON_START_TIME,
    g_DxPacketTable,
    80,
    TPacketProcess<CDX_WOLYMPIAD_SAVE_SEASON_START_TIME, 80>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_RELOAD_HERO_BY_ID,
    g_DxPacketTable,
    81,
    TPacketProcess<CDX_WOLYMPIAD_RELOAD_HERO_BY_ID, 81>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_ADD_POINT, g_DxPacketTable, 82, TPacketProcess<CDX_WOLYMPIAD_ADD_POINT, 82>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_START_SEASON, g_DxPacketTable, 83, TPacketProcess<CDX_WOLYMPIAD_START_SEASON, 83>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_SAVE_HERO_TYPE, g_DxPacketTable, 84, TPacketProcess<CDX_WOLYMPIAD_SAVE_HERO_TYPE, 84>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_DELETE_TRADE_POINT,
    g_DxPacketTable,
    85,
    TPacketProcess<CDX_WOLYMPIAD_DELETE_TRADE_POINT, 85>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_ADD_TRADE_POINT, g_DxPacketTable, 86, TPacketProcess<CDX_WOLYMPIAD_ADD_TRADE_POINT, 86>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_RESET_POINT, g_DxPacketTable, 87, TPacketProcess<CDX_WOLYMPIAD_RESET_POINT, 87>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_MATCHING_POINT, g_DxPacketTable, 88, TPacketProcess<CDX_WOLYMPIAD_MATCHING_POINT, 88>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_MATCHING_POINT_SAVE,
    g_DxPacketTable,
    89,
    TPacketProcess<CDX_WOLYMPIAD_MATCHING_POINT_SAVE, 89>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_MATCHING_POINT_RESET_ALL,
    g_DxPacketTable,
    90,
    TPacketProcess<CDX_WOLYMPIAD_MATCHING_POINT_RESET_ALL, 90>::DoJob
);
L2_BIND_PACKET(IsRegistered_CDX_WOLYMPIAD_RECORD, g_DxPacketTable, 91, TPacketProcess<CDX_WOLYMPIAD_RECORD, 91>::DoJob);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_GENERATE_RECORD_FOR_TEST,
    g_DxPacketTable,
    92,
    TPacketProcess<CDX_WOLYMPIAD_GENERATE_RECORD_FOR_TEST, 92>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10,
    g_DxPacketTable,
    93,
    TPacketProcess<CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10, 93>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_MASTERY_LOAD, g_DxPacketTable, 94, TPacketProcess<CDX_PLEDGE_MASTERY_LOAD, 94>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_CONTRIBUTION_MEMBER_ADD,
    g_DxPacketTable,
    95,
    TPacketProcess<CDX_PLEDGE_CONTRIBUTION_MEMBER_ADD, 95>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_CONTRIBUTION_MEMBER_RESET,
    g_DxPacketTable,
    96,
    TPacketProcess<CDX_PLEDGE_CONTRIBUTION_MEMBER_RESET, 96>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_CONTRIBUTION_MEMBER_TAKE_PERIOD_REWARD,
    g_DxPacketTable,
    97,
    TPacketProcess<CDX_PLEDGE_CONTRIBUTION_MEMBER_TAKE_PERIOD_REWARD, 97>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_CONTRIBUTION_RANK_LOAD,
    g_DxPacketTable,
    98,
    TPacketProcess<CDX_PLEDGE_CONTRIBUTION_RANK_LOAD, 98>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_CONTRIBUTION_RANK_SAVE,
    g_DxPacketTable,
    99,
    TPacketProcess<CDX_PLEDGE_CONTRIBUTION_RANK_SAVE, 99>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_CONTRIBUTION_RANK_DECIDE,
    g_DxPacketTable,
    100,
    TPacketProcess<CDX_PLEDGE_CONTRIBUTION_RANK_DECIDE, 100>::DoJob
);
L2_BIND_PACKET(IsRegistered_CDX_PLEDGE_RAID_LOAD, g_DxPacketTable, 101, TPacketProcess<CDX_PLEDGE_RAID_LOAD, 101>::DoJob);
L2_BIND_PACKET(IsRegistered_CDX_PLEDGE_RAID_SAVE, g_DxPacketTable, 102, TPacketProcess<CDX_PLEDGE_RAID_SAVE, 102>::DoJob);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_RAID_NEXT_SEASON, g_DxPacketTable, 103, TPacketProcess<CDX_PLEDGE_RAID_NEXT_SEASON, 103>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_RAID_RESET_SEASON, g_DxPacketTable, 104, TPacketProcess<CDX_PLEDGE_RAID_RESET_SEASON, 104>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_RAID_GET_INFO, g_DxPacketTable, 105, TPacketProcess<CDX_PLEDGE_RAID_GET_INFO, 105>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_RAID_GET_TOTAL_COUNT,
    g_DxPacketTable,
    106,
    TPacketProcess<CDX_PLEDGE_RAID_GET_TOTAL_COUNT, 106>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_RAID_SET_INFO, g_DxPacketTable, 107, TPacketProcess<CDX_PLEDGE_RAID_SET_INFO, 107>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_RAID_HUNT_NPC_INFO,
    g_DxPacketTable,
    108,
    TPacketProcess<CDX_PLEDGE_RAID_HUNT_NPC_INFO, 108>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_RAID_INZONE_SAVE, g_DxPacketTable, 109, TPacketProcess<CDX_PLEDGE_RAID_INZONE_SAVE, 109>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_RAID_INZONE_DELETE,
    g_DxPacketTable,
    110,
    TPacketProcess<CDX_PLEDGE_RAID_INZONE_DELETE, 110>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_RAID_INZONE_RESET, g_DxPacketTable, 111, TPacketProcess<CDX_PLEDGE_RAID_INZONE_RESET, 111>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_MISSION_SET_COUNT, g_DxPacketTable, 112, TPacketProcess<CDX_PLEDGE_MISSION_SET_COUNT, 112>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_LOAD,
    g_DxPacketTable,
    113,
    TPacketProcess<CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_LOAD, 113>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_ADD,
    g_DxPacketTable,
    114,
    TPacketProcess<CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_ADD, 114>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_RESET,
    g_DxPacketTable,
    115,
    TPacketProcess<CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_RESET, 115>::DoJob
);

} // namespace

#endif

#if defined(L2_BUILD_L2SERVER)

namespace {

void WriteResidenceDecoLog(
    const int residenceId,
    const int decoType,
    const int decoId,
    const int level,
    const std::uint32_t expireTime,
    const int active
) {
    CLogFormatter formatter;
    formatter.WriteLog4(
        0x10f, nullptr, nullptr, nullptr, nullptr, nullptr, residenceId, decoType, decoId, level, expireTime, active, 0, 0,
        0, 0, 0
    );
}

void CopyTimeAttackMemberName(char16_t (&destination)[25], const char16_t* const source) {
    std::size_t length = 0;
    while (length < 24 && source[length] != u'\0') {
        ++length;
    }
    std::memcpy(destination, source, length * sizeof(char16_t));
    destination[length] = u'\0';
    destination[24] = u'\0';
}

void WriteDeclarePledgeWarLog(CPledge* const declaringPledge, CPledge* const declaredPledge, const bool bothDeclared) {
    const std::u16string declaringName = std::u16string(declaringPledge->GetPledgeName());
    const std::u16string declaredName = std::u16string(declaredPledge->GetPledgeName());
    CLogFormatter formatter;
    formatter.WriteLog(
        bothDeclared ? 0x7f1 : 0x110, declaringPledge->GetDbId(), 0, declaredPledge->GetDbId(), 0, 0, 0, 0,
        declaringName.c_str(), declaredName.c_str(), nullptr, declaringPledge->GetPledgeLevel(),
        declaringPledge->GetDbMemberCount(), declaringPledge->GetCurrentDeclareWarCount(), declaredPledge->GetPledgeLevel(),
        declaredPledge->GetDbMemberCount(), declaredPledge->GetCurrentDeclareWarCount(),
        declaringPledge->GetCurrentTargettedWarCount(), declaredPledge->GetCurrentTargettedWarCount(), 0, 0, nullptr,
        nullptr, bothDeclared ? nullptr : declaredName.c_str(), nullptr, 0
    );
}

void WriteCancelPledgeWarLog(CPledge* const declaringPledge, CPledge* const declaredPledge) {
    const std::u16string declaringName = std::u16string(declaringPledge->GetPledgeName());
    const std::u16string declaredName = std::u16string(declaredPledge->GetPledgeName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x111, declaringPledge->GetDbId(), 0, declaredPledge->GetDbId(), 0, 0, 0, 0, declaringName.c_str(),
        declaredName.c_str(), nullptr, declaringPledge->GetPledgeLevel(), declaringPledge->GetDbMemberCount(),
        declaringPledge->GetCurrentDeclareWarCount(), declaringPledge->GetCurrentTargettedWarCount(), 0, 0, 0, 0, 0, 0,
        nullptr, nullptr, declaredName.c_str(), nullptr, 0
    );
}

} // namespace

void CSetResidenceDeco::DoServerJob() {
    // Current L2Server 0x006f01e8.
    L2_GUARD_FUNCTION();
    if (nReturn != 0) {
        CResidence* const residence = g_ResidenceDB.GetResidence(nAgitId);
        if (residence != nullptr) {
            WriteResidenceDecoLog(nAgitId, nDecoType, nDecoId, nLevel, static_cast<std::uint32_t>(nExpire), 1);
            Deco& deco = residence->m_data->m_aDeco[static_cast<std::size_t>(nDecoType - 1)];
            deco.nId = nDecoId;
            deco.nLevel = nLevel;
            deco.nExpire = static_cast<std::uint32_t>(nExpire);
            residence->ValidateDeco();
            residence->SendDecoInfo(nullptr);
        }
    }
    if (nNpcIndex != -1 || nUserIndex != -1) {
        g_npcserver.Send("cddd", 0x46, nNpcIndex, nUserIndex, nReturn);
    }
}

void CResetResidenceDeco::DoServerJob() {
    // Current L2Server 0x006f03b4.
    L2_GUARD_FUNCTION();
    if (nReturn != 0) {
        CResidence* const residence = g_ResidenceDB.GetResidence(nAgitId);
        if (residence != nullptr) {
            Deco& deco = residence->m_data->m_aDeco[static_cast<std::size_t>(nDecoType - 1)];
            WriteResidenceDecoLog(nAgitId, nDecoType, deco.nId, deco.nLevel, deco.nExpire, 0);
            deco.nId = 0;
            deco.nLevel = 0;
            deco.nExpire = 0;
            residence->ValidateDeco();
            residence->SendDecoInfo(nullptr);
        }
    }
    if (nNpcIndex != -1 || nUserIndex != -1) {
        g_npcserver.Send("cddd", 0x47, nNpcIndex, nUserIndex, nReturn);
    }
}

void CResetAllResidenceDeco::DoServerJob() {
    // Current L2Server 0x006f058c.
    L2_GUARD_FUNCTION();
    CResidence* const residence = g_ResidenceDB.GetResidence(nAgitId);
    if (residence == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < residence->m_data->m_aDeco.size(); ++index) {
        Deco& deco = residence->m_data->m_aDeco[index];
        deco.nId = 0;
        deco.nLevel = 0;
        deco.nExpire = 0;
        WriteResidenceDecoLog(nAgitId, static_cast<int>(index), deco.nId, deco.nLevel, deco.nExpire, 0);
    }
    residence->InstallHpStove(0, 0);
    residence->InstallMpFlame(0, 0);
    residence->InstallHatcher(0);
    residence->InstallTeleport(0, 0);
    residence->ValidateDeco();
    residence->SendDecoInfo(nullptr);
}

void CRenewResidenceDeco::DoServerJob() {
    // Current L2Server 0x006f07c8.
    L2_GUARD_FUNCTION();
    CResidence* const residence = g_ResidenceDB.GetResidence(nAgitId);
    if (residence == nullptr) {
        return;
    }

    std::uint64_t totalPrice = 0;
    for (int index = 0; index < nDecoCount; ++index) {
        const CRenewResidenceDecoData& reply = aDecoData[index];
        Deco& deco = residence->m_data->m_aDeco[static_cast<std::size_t>(reply.nType - 1)];
        if (reply.nReturn == 0) {
            deco.nId = 0;
            deco.nLevel = 0;
            deco.nExpire = 0;
        } else {
            deco.nExpire = static_cast<std::uint32_t>(reply.nExpire);
            totalPrice += static_cast<std::uint64_t>(reply.nPrice);
        }
    }

    CWorldAgit* const agit = residence->CastWorldAgit();
    if (agit != nullptr) {
        CCastle* const castle = agit->GetParentCastle();
        if (castle != nullptr) {
            const std::uint32_t taxRate =
                agit->m_data->m_TaxInfoData[static_cast<std::size_t>(agit->m_data->m_ResidenceType)].m_nTaxRate;
            const std::uint64_t product = static_cast<std::uint64_t>(taxRate) * totalPrice;
            castle->ChangeTaxIncome(
                static_cast<std::int64_t>(static_cast<double>(static_cast<std::int64_t>(product)) / 100.0)
            );
            castle->SaveIncome(true, 4);
        }
    }
    residence->ValidateDeco();
    residence->SendDecoInfo(nullptr);
    residence->m_bDecoCheck = true;
}

void DxChangeSSQPeriod::DoServerJob() {
    // Current L2Server 0x006f2fec.
    L2_GUARD_FUNCTION();
    SSQSystem& system = SSQSystem::inst();
    if (mStatus == SSQStatus::EVENT) {
        {
            L2_WRITE_GUARD(system);
            system.SetEventStartTime(mStartTime);
            system.SetEventEndTime(mEndTime);
        }
    } else if (mStatus == SSQStatus::SEAL_EFFECT) {
        {
            L2_WRITE_GUARD(system);
            system.SetSealEffectTime(mStartTime);
            system.SetSealEffectEndTime(mEndTime);
        }
    }
}

void DxStartSSQAccount::DoServerJob() {
    // Current L2Server 0x006f3104. MStatus is ignored.
    SSQSystem& system = SSQSystem::inst();
    {
        L2_WRITE_GUARD(system);
        system.SetCurrentStatus(SSQStatus::ACCOUNTING);
        system.OnEventEnd();
        system.OnAccountStart();
        system.EnableTimer();
    }
}

void DxStartSSQSealEffect::DoServerJob() {
    // Current L2Server 0x006f3168. MStatus is ignored.
    SSQSystem& system = SSQSystem::inst();
    {
        L2_WRITE_GUARD(system);
        system.SetCurrentStatus(SSQStatus::SEAL_EFFECT);
        system.OnSealEffectStart();
        system.EnableTimer();
    }
}

void DxChangeSSQStatus::DoServerJob() {
    // Current L2Server 0x006f32b4.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(SSQSystem::inst());
        SSQSystem::inst().SetCurrentStatus(static_cast<SSQStatus::type>(mStatus));
    }
}

void DxSetTicketBuyCount::DoServerJob() {
    // Current L2Server 0x006f6124. MUserDBID is ignored.
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSID));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return;
    }
    {
        L2_GUARD(*user->m_lock);
        user->SetTicketBuyCount(mCount);
    }
}

void DxSetSevenSignQuestResult::DoServerJob() {
    // Current L2Server 0x006f31c4.
    L2_GUARD_FUNCTION();
    SSQSystem& system = SSQSystem::inst();
    {
        L2_WRITE_GUARD(system);
        system.SetWinner(static_cast<SSQPart::type>(mWinner));
        for (int seal = SSQSeal::AVARICE; seal <= SSQSeal::STRIFE; ++seal) {
            system.SetSealOwner(seal, static_cast<SSQPart::type>(mSealOwner[seal]));
        }
    }
}

void DxCloseSevenSignQuest::DoServerJob() {
    // Current L2Server 0x006f338c.
    L2_GUARD_FUNCTION();
    SSQSystem& system = SSQSystem::inst();
    {
        L2_WRITE_GUARD(system);
        system.SetCurrentStatus(static_cast<SSQStatus::type>(mStatus));
        system.OnSealEffectEnd();
        system.OnClose();
        system.EnableTimer();
    }
}

void DxAddSevenSignQuestMember::DoServerJob() {
    // Current L2Server 0x006f6234.
    L2_GUARD_FUNCTION();
    if (mErrorCode != 0) {
        return;
    }
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSID));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return;
    }

    const SSQPart::type part = static_cast<SSQPart::type>(mSSQPart);
    const SSQSeal::type seal = static_cast<SSQSeal::type>(mSealSelectionNo);

    {
        L2_GUARD(*user->m_lock);
        user->SetSSQRoundNumber(mSSQRound);
        user->SetSSQPart(part);
        user->SetSealSelectionNo(seal);
        user->SetSSQPosition(static_cast<SSQPosition::type>(mPosition));
        user->SetSSQPoint(mInitialPoint);
        user->SetTicketBuyCount(0);
    }

    SSQSystem& system = SSQSystem::inst();
    {
        L2_WRITE_GUARD(system);
        system.SetMemberCount(part, runtime::AddInt32Wrapping(system.GetMemberCount(part), 1));
        system.SetSealSelectionCount(part, seal, runtime::AddInt32Wrapping(system.GetSealSelectionCount(part, seal), 1));
        const std::uint64_t total = static_cast<std::uint64_t>(system.GetCollectedPoint(SSQPart::DAWN)) +
            static_cast<std::uint64_t>(system.GetCollectedPoint(SSQPart::TWILIGHT)) +
            static_cast<std::uint64_t>(mInitialPoint);
        if (total < UINT64_C(1000000000000)) {
            system.SetCollectedPoint(part, runtime::AddInt64Wrapping(system.GetCollectedPoint(part), mInitialPoint));
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] SSQ Addmember : Too large user point, result=[%I64d], delta=[%I64d]",
            //     L"..\\Shared\\DXPacket.cpp",
            //     0x717,
            //     total,
            //     mInitialPoint)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[seven signs] Aggregate points were not updated for character DB ID %d: computed total %llu is outside
            //     0..999999999999, delta %lld. Earlier character changes are retained.", mUserDBID, static_cast<unsigned
            //     long long>(total), static_cast<long long>(mInitialPoint))
            L2_LOG_ERROR(
                Log,
                u"[seven signs] Aggregate points were not updated for character DB ID %d: computed total %llu is outside "
                u"0..999999999999; delta %lld. Earlier character changes are retained.",
                mUserDBID, static_cast<unsigned long long>(total), static_cast<long long>(mInitialPoint)
            );
        }
    }
    const int z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const int y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const int x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    CSharedCreatureData* const sharedData = user->m_data;
    const char16_t* const targetAccountName = user->m_sAccountName.data();
    const char16_t* const userName = user->GetName();
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t databaseId = user->GetDBID();
    std::size_t accountNameLength = 0;
    while (accountNameLength < user->m_sAccountName.size() && targetAccountName[accountNameLength] != u'\0') {
        ++accountNameLength;
    }
    const std::wstring accountName = Utf16ToWide(std::u16string_view(targetAccountName, accountNameLength));
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
    //     L"%s,%s,,,",
    //     static_cast<std::int64_t>(0x136),
    //     static_cast<std::int64_t>(databaseId),
    //     static_cast<std::int64_t>(accountId),
    //     static_cast<std::int64_t>(x),
    //     static_cast<std::int64_t>(y),
    //     static_cast<std::int64_t>(z),
    //     static_cast<std::int64_t>(sharedData->m_nClass),
    //     static_cast<std::int64_t>(sharedData->m_Race),
    //     static_cast<std::int64_t>(sharedData->m_Sex),
    //     static_cast<std::int64_t>(sharedData->m_nLevel),
    //     static_cast<std::int64_t>(mSSQPart),
    //     static_cast<std::int64_t>(mSealSelectionNo),
    //     static_cast<std::int64_t>(mPosition),
    //     static_cast<std::int64_t>(mJoinType),
    //     userName,
    //     accountName.c_str())
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        u"%s,%s,,,",
        static_cast<std::int64_t>(0x136), static_cast<std::int64_t>(databaseId), static_cast<std::int64_t>(accountId),
        static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), static_cast<std::int64_t>(z),
        static_cast<std::int64_t>(sharedData->m_nClass), static_cast<std::int64_t>(sharedData->m_Race),
        static_cast<std::int64_t>(sharedData->m_Sex), static_cast<std::int64_t>(sharedData->m_nLevel),
        static_cast<std::int64_t>(mSSQPart), static_cast<std::int64_t>(mSealSelectionNo),
        static_cast<std::int64_t>(mPosition), static_cast<std::int64_t>(mJoinType), userName, accountName.c_str()
    );
}

void DxDeleteDepositedSevenSignQuestItem::DoServerJob() {
    // Current L2Server 0x006f65b0.
    L2_GUARD_FUNCTION();
    if (mErrorCode != 0) {
        return;
    }
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSID));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return;
    }

    std::int64_t newCount;
    {
        L2_GUARD(*user->m_lock);
        const std::int64_t oldCount = user->GetDepositedSSQItemCount(mSSQPart, mItemType);
        newCount = runtime::AddInt64Wrapping(oldCount, runtime::NegateInt64Wrapping(mCount));
        if (newCount < 0) {
            newCount = 0;
        }
        user->SetDepositedSSQItemCount(mSSQPart, mItemType, newCount);
    }
    if (newCount < 1) {
        return;
    }

    int itemClassId = 0;
    if (mItemType == SSQItem::TYPE_A) {
        itemClassId = 0x18d8;
    } else if (mItemType == SSQItem::TYPE_B) {
        itemClassId = 0x18d9;
    } else if (mItemType == SSQItem::TYPE_C) {
        itemClassId = 0x18da;
    }
    CItem* const item = itemClassId != 0 ? user->m_inv.GetFirstItemByClassID(itemClassId, false, true) : nullptr;
    const int z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const int y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const int x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    CSharedCreatureData* const sharedData = user->m_data;
    const char16_t* const targetAccountName = user->m_sAccountName.data();
    const char16_t* const userName = user->GetName();
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t databaseId = user->GetDBID();
    std::size_t accountNameLength = 0;
    while (accountNameLength < user->m_sAccountName.size() && targetAccountName[accountNameLength] != u'\0') {
        ++accountNameLength;
    }
    const std::wstring accountName = Utf16ToWide(std::u16string_view(targetAccountName, accountNameLength));
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
    //     L"%I64d,%s,%s,,,",
    //     static_cast<std::int64_t>(0x13b),
    //     static_cast<std::int64_t>(databaseId),
    //     static_cast<std::int64_t>(accountId),
    //     static_cast<std::int64_t>(x),
    //     static_cast<std::int64_t>(y),
    //     static_cast<std::int64_t>(z),
    //     static_cast<std::int64_t>(sharedData->m_nClass),
    //     static_cast<std::int64_t>(sharedData->m_Race),
    //     static_cast<std::int64_t>(sharedData->m_Sex),
    //     static_cast<std::int64_t>(sharedData->m_nLevel),
    //     static_cast<std::int64_t>(mSSQPart),
    //     static_cast<std::int64_t>(mItemType),
    //     mCount,
    //     newCount,
    //     static_cast<std::int64_t>(
    //         reinterpret_cast<std::intptr_t>(item)),
    //     userName,
    //     accountName.c_str())
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
        u"%I64d,%s,%s,,,",
        static_cast<std::int64_t>(0x13b), static_cast<std::int64_t>(databaseId), static_cast<std::int64_t>(accountId),
        static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), static_cast<std::int64_t>(z),
        static_cast<std::int64_t>(sharedData->m_nClass), static_cast<std::int64_t>(sharedData->m_Race),
        static_cast<std::int64_t>(sharedData->m_Sex), static_cast<std::int64_t>(sharedData->m_nLevel),
        static_cast<std::int64_t>(mSSQPart), static_cast<std::int64_t>(mItemType), mCount, newCount,
        static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(item)), userName, accountName.c_str()
    );
}

void DxChangeSSQUserPoint::DoServerJob() {
    // Current L2Server 0x006f6e10 dereferences a failed CastUser.
    L2_GUARD_FUNCTION();
    if (mErrorCode >= 1) {
        return;
    }
    CSPointer<User> user = GetUserBySID(mUserSID);
    {
        L2_GUARD(*user->m_lock);
        user->SetSSQPoint(mPoint);
    }

    SSQSystem& system = SSQSystem::inst();
    {
        L2_WRITE_GUARD(system);
        system.SetCollectedPoint(mPart, runtime::AddInt64Wrapping(system.GetCollectedPoint(mPart), mPoint));
    }
}

void DxDepositSevenSignQuestItemEx::DoServerJob() {
    // Current L2Server 0x006f688c.
    L2_GUARD_FUNCTION();
    if (mErrorCode != 0) {
        return;
    }
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSID));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return;
    }

    std::int64_t newUserPoint;
    {
        L2_GUARD(*user->m_lock);
        for (int index = 0; index < 3; ++index) {
            const std::int64_t count = mCountList[index];
            if (count <= 0) {
                continue;
            }
            const SSQItem::type itemType = static_cast<SSQItem::type>(index);
            const std::int64_t newCount =
                runtime::AddInt64Wrapping(user->GetDepositedSSQItemCount(mSSQPart, itemType), count);
            user->SetDepositedSSQItemCount(mSSQPart, itemType, newCount);
            const int itemClassId = 0x18d8 + index;
            CItem* const item = user->m_inv.GetFirstItemByClassID(itemClassId, false, true);
            const int z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
            const int y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
            const int x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
            CSharedCreatureData* const sharedData = user->m_data;
            const char16_t* const targetAccountName = user->m_sAccountName.data();
            const char16_t* const userName = user->GetName();
            const std::uint32_t accountId = user->GetAccountID();
            const std::uint32_t databaseId = user->GetDBID();
            std::size_t accountNameLength = 0;
            while (accountNameLength < user->m_sAccountName.size() && targetAccountName[accountNameLength] != u'\0') {
                ++accountNameLength;
            }
            const std::wstring accountName = Utf16ToWide(std::u16string_view(targetAccountName, accountNameLength));
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
            //     L"%I64d,%s,%s,,,",
            //     static_cast<std::int64_t>(0x13b),
            //     static_cast<std::int64_t>(databaseId),
            //     static_cast<std::int64_t>(accountId),
            //     static_cast<std::int64_t>(x),
            //     static_cast<std::int64_t>(y),
            //     static_cast<std::int64_t>(z),
            //     static_cast<std::int64_t>(sharedData->m_nClass),
            //     static_cast<std::int64_t>(sharedData->m_Race),
            //     static_cast<std::int64_t>(sharedData->m_Sex),
            //     static_cast<std::int64_t>(sharedData->m_nLevel),
            //     static_cast<std::int64_t>(mSSQPart),
            //     static_cast<std::int64_t>(index),
            //     count,
            //     newCount,
            //     static_cast<std::int64_t>(
            //         reinterpret_cast<std::intptr_t>(item)),
            //     userName,
            //     accountName.c_str())
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
                u"%I64d,%s,%s,,,",
                static_cast<std::int64_t>(0x13b), static_cast<std::int64_t>(databaseId),
                static_cast<std::int64_t>(accountId), static_cast<std::int64_t>(x), static_cast<std::int64_t>(y),
                static_cast<std::int64_t>(z), static_cast<std::int64_t>(sharedData->m_nClass),
                static_cast<std::int64_t>(sharedData->m_Race), static_cast<std::int64_t>(sharedData->m_Sex),
                static_cast<std::int64_t>(sharedData->m_nLevel), static_cast<std::int64_t>(mSSQPart),
                static_cast<std::int64_t>(index), count, newCount,
                static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(item)), userName, accountName.c_str()
            );
        }
        const std::int64_t previousUserPoint = user->GetSSQPoint();
        newUserPoint = runtime::AddInt64Wrapping(previousUserPoint, mPoint);
        user->SetSSQPoint(newUserPoint);
    }

    SSQSystem& system = SSQSystem::inst();
    std::int64_t newSystemPoint;
    {
        L2_WRITE_GUARD(system);
        const std::uint64_t total = static_cast<std::uint64_t>(SSQSystem::inst().GetCollectedPoint(SSQPart::DAWN)) +
            static_cast<std::uint64_t>(SSQSystem::inst().GetCollectedPoint(SSQPart::TWILIGHT)) +
            static_cast<std::uint64_t>(mPoint);
        newSystemPoint = runtime::AddInt64Wrapping(SSQSystem::inst().GetCollectedPoint(mSSQPart), mPoint);
        if (total - UINT64_C(1) < UINT64_C(999999999999)) {
            SSQSystem::inst().SetCollectedPoint(mSSQPart, newSystemPoint);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] DepositItem : Too large system point, result[%I64d], delta[%I64d]",
            //     L"..\\Shared\\DXPacket.cpp",
            //     0x822,
            //     total,
            //     mPoint)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[seven signs] Aggregate points were not updated for character DB ID %d: computed total %llu is
            //     outside 1..999999999999. Delta %lld. Earlier character changes are retained.", mUserDBID,
            //     static_cast<unsigned long long>(total), static_cast<long long>(mPoint))
            L2_LOG_ERROR(
                Log,
                u"[seven signs] Aggregate points were not updated for character DB ID %d: computed total %llu is outside "
                u"1..999999999999; delta %lld. Earlier character changes are retained.",
                mUserDBID, static_cast<unsigned long long>(total), static_cast<long long>(mPoint)
            );
        }
    }

    CUserSocket* const socket = user->GetSocket();
    socket->Send("chccQ", 0x62, 0x4f3, 1, 6, mPoint);
    const int z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const int y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const int x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    CSharedCreatureData* const sharedData = user->m_data;
    const char16_t* const targetAccountName = user->m_sAccountName.data();
    const char16_t* const userName = user->GetName();
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t databaseId = user->GetDBID();
    std::size_t accountNameLength = 0;
    while (accountNameLength < user->m_sAccountName.size() && targetAccountName[accountNameLength] != u'\0') {
        ++accountNameLength;
    }
    const std::wstring accountName = Utf16ToWide(std::u16string_view(targetAccountName, accountNameLength));
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
    //     L"%s,%s,,,",
    //     static_cast<std::int64_t>(0x13c),
    //     static_cast<std::int64_t>(databaseId),
    //     static_cast<std::int64_t>(accountId),
    //     static_cast<std::int64_t>(x),
    //     static_cast<std::int64_t>(y),
    //     static_cast<std::int64_t>(z),
    //     static_cast<std::int64_t>(sharedData->m_nClass),
    //     static_cast<std::int64_t>(sharedData->m_Race),
    //     static_cast<std::int64_t>(sharedData->m_Sex),
    //     static_cast<std::int64_t>(sharedData->m_nLevel),
    //     static_cast<std::int64_t>(mSSQPart),
    //     mPoint,
    //     newUserPoint,
    //     newSystemPoint,
    //     userName,
    //     accountName.c_str())
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        u"%s,%s,,,",
        static_cast<std::int64_t>(0x13c), static_cast<std::int64_t>(databaseId), static_cast<std::int64_t>(accountId),
        static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), static_cast<std::int64_t>(z),
        static_cast<std::int64_t>(sharedData->m_nClass), static_cast<std::int64_t>(sharedData->m_Race),
        static_cast<std::int64_t>(sharedData->m_Sex), static_cast<std::int64_t>(sharedData->m_nLevel),
        static_cast<std::int64_t>(mSSQPart), mPoint, newUserPoint, newSystemPoint, userName, accountName.c_str()
    );
}

void DxAddTimeAttackRecord::DoServerJob() {
    // Current L2Server 0x006f81b0.
    L2_GUARD_FUNCTION();
    if (mErrorCode >= 1) {
        return;
    }

    const auto update = [this](TimeAttackInfo* const info) {
        if (info == nullptr || info->mSharedInfo->mPoint >= mPoint) {
            return false;
        }
        {
            L2_WRITE_GUARD(*info);
            SharedTimeAttackInfo* const shared = info->mSharedInfo;
            shared->mSSQPart = mSSQPart;
            shared->mPoint = mPoint;
            shared->mRecordTime = mRecordTime;
            shared->mElapsedTime = mElapsedTime;
            shared->mMemberCount = mMemberCount;
            for (int index = 0; index < mMemberCount; ++index) {
                const int destinationIndex = index < 0 || index > 8 ? 0 : index;
                CopyTimeAttackMemberName(shared->mMemberNameList[destinationIndex], mMemberNameList[index]);
                shared->mMemberDBIDList[index] = mMemberDBIDList[index];
                shared->mMemberRewardFlags[index] = 0;
            }
        }
        return true;
    };

    if (mTopRecordUpdated != 0) {
        update(TimeAttackBoard::inst().GetInfo(mRoomNo, TimeAttackRecordType::TOP_OF_ROOM));
    }
    if (mPartRecordUpdated != 0 && update(TimeAttackBoard::inst().GetInfo(mRoomNo, static_cast<int>(mSSQPart)))) {
        SSQSystem::inst().ValidateMainEventPortion();
    }
}

void DxAddTimeAttackFee::DoServerJob() {
    // Current L2Server 0x006f84a8.
    L2_GUARD_FUNCTION();
    if (mErrorCode != 0) {
        return;
    }
    TimeAttackInfo* const info = TimeAttackBoard::inst().GetInfo(mRoomNO, TimeAttackRecordType::TOP_OF_ROOM);
    if (info == nullptr) {
        return;
    }
    {
        L2_WRITE_GUARD(*info);
        if (mFee == 0) {
            info->mSharedInfo->mFee = 0;
        } else {
            info->mSharedInfo->mFee = runtime::AddInt64Wrapping(info->mSharedInfo->mFee, mFee);
        }
    }
}

void DxSetTimeAttackRewardFlag::DoServerJob() {
    // Current L2Server 0x006f85a0.
    L2_GUARD_FUNCTION();
    if (mErrorCode != 0) {
        return;
    }
    TimeAttackInfo* const info = TimeAttackBoard::inst().GetInfo(mRoomNO, mPartType);
    if (info == nullptr) {
        return;
    }
    {
        L2_WRITE_GUARD(*info);
        info->mSharedInfo->SetRewardFlag(mUserDBID, mFlag);
    }
}

void DxChangeSealSelectionCount::DoServerJob() {
    // Current L2Server 0x006f3474.
    L2_GUARD_FUNCTION();
    if (mErrorCode != 0) {
        return;
    }
    {
        L2_WRITE_GUARD(SSQSystem::inst());
        SSQSystem::inst().SetSealSelectionCount(mPart, mSealNum, mCount);
    }
}

void DxStartQuickCycle::DoServerJob() {
    // Current L2Server 0x006f355c.
    L2_GUARD_FUNCTION();
    if (mErrorCode >= 1) {
        return;
    }
    {
        L2_WRITE_GUARD(SSQSystem::inst());
        SSQSystem::inst().SetCurrentStatus(mStatus);
        SSQSystem::inst().SetEventStartTime(mEventStartTime);
        SSQSystem::inst().SetEventEndTime(mEventEndTime);
        SSQSystem::inst().SetSealEffectTime(mSealEffectTime);
        SSQSystem::inst().SetSealEffectEndTime(mSealEffectEndTime);
    }
}

void DxChangeMainEventPoint::DoServerJob() {
    // Current L2Server 0x006f367c.
    L2_GUARD_FUNCTION();
    if (mErrorCode != 0) {
        return;
    }
    {
        L2_WRITE_GUARD(SSQSystem::inst());
        SSQSystem::inst().SetMainEventPoint(mPartType, static_cast<std::int64_t>(mMainEventPoint));
    }
}

void DxChangeSealOwner::DoServerJob() {
    // Current L2Server 0x006f3764.
    L2_GUARD_FUNCTION();
    if (mErrorCode != 0) {
        return;
    }
    {
        L2_WRITE_GUARD(SSQSystem::inst());
        SSQSystem::inst().SetSealOwner(mSealNumber, mPartType);
    }
}

void DxChangeSSQWinner::DoServerJob() {
    // Current L2Server 0x006f3848.
    L2_GUARD_FUNCTION();
    if (mErrorCode != 0) {
        return;
    }
    {
        L2_WRITE_GUARD(SSQSystem::inst());
        SSQSystem::inst().SetWinner(mPartType);
    }
}

void DxDismissSevenSignMember::DoServerJob() {
    // Current L2Server 0x006f6f7c.
    L2_GUARD_FUNCTION();
    if (mErrorCode != 0) {
        return;
    }
    CSPointer<User> user = GetUserBySID(mUserSID);
    if (!user) {
        return;
    }

    {
        L2_GUARD(*user->m_lock);
        user->SetSSQPart(SSQPart::NONE);
        user->SetSSQRoundNumber(0);
        user->SetSSQPoint(0);
        user->SetSealSelectionNo(SSQSeal::NONE);
        user->SetSSQPosition(SSQPosition::NONE);
    }

    SSQSystem& system = SSQSystem::inst();
    {
        L2_WRITE_GUARD(system);
        const std::uint32_t pointResult =
            static_cast<std::uint32_t>(system.GetCollectedPoint(mFromPart)) - static_cast<std::uint32_t>(mPoint);
        if (runtime::Int32FromBits(pointResult) >= 0) {
            system.SetCollectedPoint(mFromPart, static_cast<std::int64_t>(runtime::Int32FromBits(pointResult)));
        }
        const std::uint32_t sealResult =
            static_cast<std::uint32_t>(system.GetSealSelectionCount(mFromPart, mSealSelectionNo)) - 1U;
        if (runtime::Int32FromBits(sealResult) >= 0) {
            system.SetSealSelectionCount(mFromPart, mSealSelectionNo, runtime::Int32FromBits(sealResult));
        }
        const std::uint32_t memberResult = static_cast<std::uint32_t>(system.GetMemberCount(mFromPart)) - 1U;
        if (runtime::Int32FromBits(memberResult) >= 0) {
            system.SetMemberCount(mFromPart, runtime::Int32FromBits(memberResult));
        }
    }
}

void DxChangeCycleMode::DoServerJob() {
    // Current L2Server 0x006f0a98.
    L2_GUARD_FUNCTION();
    if (mErrorCode != 0) {
        return;
    }
    SSQSystem& system = SSQSystem::inst();
    {
        L2_WRITE_GUARD(system);
        system.SetCycleModeFlag(mCycleModeFlag);
    }
    system.Reset();
}

void CNewbieCheck::DoServerJob() {
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(nServerId));
    if (creature != nullptr && creature->IsPC()) {
        creature->m_data->m_nNewbieStatus = nIsNewbie;
    }
}

void DxSaveSiegeKillDeath::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxSaveNoblessType::DoServerJob() {
    // Current L2Server 0x006f86d0.
    L2_GUARD_FUNCTION();
    if (mReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"dxerror:DxSaveNoblessType [%d, %d]",
        //     mUserDbId,
        //     mNoblessType)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[olympiad] Cannot save noble status for character DB ID %d: CacheD returned failure. Requested type %d.",
        //     mUserDbId, mNoblessType)
        L2_LOG_ERROR(
            Log, u"[olympiad] Cannot save noble status for character DB ID %d: CacheD returned failure; requested type %d.",
            mUserDbId, mNoblessType
        );
        return;
    }

    db.RequestReloadNobless(mUserDbId);
    CSPointer<User> databaseUser = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, mUserDbId));
    if (databaseUser) {
        const std::int32_t previousType = databaseUser->m_data->m_nNoblessType;
        CLogFormatter formatter;
        formatter.WriteLog1(
            0x35f, databaseUser.get(), nullptr, nullptr, nullptr, nullptr, previousType, mNoblessType, 0, 0, 0, 0, 0
        );
        formatter.WriteAuditLog1(
            0x598, databaseUser.get(), nullptr, nullptr, nullptr, nullptr, previousType, mNoblessType, 0, 0, 0, 0, 0
        );
        if (mNoblessType == 1) {
            databaseUser->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_PC_GET_NOBLE, 0, 1);
        } else if (mNoblessType == 2) {
            databaseUser->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_PC_GET_HONORS, 0, 1);
        }
        databaseUser->PledgeMissionSendRewardCountOfOneWeekToUserWithType(mNoblessType);
    }

    if (mNpcSid == 0 || mUserSid == 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user) {
            user->m_nParamChanged |= std::int64_t{PCB_USER};
        }
        return;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user) {
        user->m_data->SetNoblessType(mNoblessType);
        CPledge* const pledge = user->GetPledge();
        if (pledge != nullptr) {
            pledge->ApplyPledgeSkillsTo(user, false, false);
            user->m_nParamChanged |= std::int64_t{PCB_USER};
        }
    }
    g_npcserver.Send("cddd", 0x51, mNpcSid, mUserSid, mNoblessType);
}

void DxSaveHeroType::DoServerJob() {
    // Current L2Server 0x006f7184.
    L2_GUARD_FUNCTION();
    if (mReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"dxerror:DxSaveHeroType [%d, %d]",
        //     mUserDbId,
        //     mHeroType)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[olympiad] Cannot save hero status for character DB ID %d: CacheD returned failure. Requested type %d.",
        //     mUserDbId, mHeroType)
        L2_LOG_ERROR(
            Log, u"[olympiad] Cannot save hero status for character DB ID %d: CacheD returned failure; requested type %d.",
            mUserDbId, mHeroType
        );
        return;
    }

    if (mNpcSid == 0 || mUserSid == 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user) {
            user->m_nParamChanged |= std::int64_t{PCB_USER};
        }
        return;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user) {
        user->m_data->SetHeroType(mHeroType);
        // if (mHeroType == 2) {
        if (mHeroType == HERO_ACTIVE) {
            user->SocialAction(0x4e30, 0x800, false);
            CPledge* const pledge = user->GetPledge();
            if (pledge != nullptr) {
                pledge->ApplyPledgeSkillsTo(user, false, false);
                user->m_nParamChanged |= std::int64_t{PCB_USER};
            }
        }
    }
    g_npcserver.Send("cddd", 0x52, mNpcSid, mUserSid, mHeroType);
}

void DxWriteNoblessAchievement::DoServerJob() {
    L2_GUARD_FUNCTION();
    if (mReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"dxerror:DxWriteNoblessAchievement [%d, %d, %d, %d]",
        //     mUserDbId,
        //     mType,
        //     mTarget,
        //     mTime)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[olympiad] Cannot save the achievement for character DB ID %d: CacheD returned failure. Type %d, target %d,
        //     time %d.", mUserDbId, mType, mTarget, mTime)
        L2_LOG_ERROR(
            Log,
            u"[olympiad] Cannot save the achievement for character DB ID %d: CacheD returned failure; type %d, target %d, "
            u"time %d.",
            mUserDbId, mType, mTarget, mTime
        );
    } else if (mNeedReload) {
        DxReloadHeroById request(mUserDbId);
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x2b), static_cast<int>(sizeof(request)), request);
    }
}

void DxReloadOlympiadPoint::DoServerJob() {
    // Current L2Server 0x006f749c. The fetched point and DBID are
    // ignored. The only observable action is CastUser.
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserServerId));
    if (creature != nullptr) {
        (void)creature->CastUser();
    }
}

bool DxDeclareWar::DeclareWar() {
    // Current L2Server 0x006f755c.
    L2_GUARD_FUNCTION();
    CPledge* const declaringPledge = CPledge::GetObjectW(static_cast<std::uint32_t>(mPledgeSid));
    CPledge* const declaredPledge = CPledge::GetObjectW(static_cast<std::uint32_t>(mEnemyPledgeSid));
    if (declaringPledge == nullptr) {
        return false;
    }
    if (declaredPledge == nullptr) {
        declaringPledge->EndTransaction();
        return false;
    }

    declaringPledge->DeclareWar(declaredPledge, mDeclareTime);
    declaringPledge->UpdateInfoWindowPacket();
    declaredPledge->TargetedBy(declaringPledge, 0, true);

    const bool bothDeclared = declaringPledge->DidDeclareWarBoth(declaredPledge);
    WriteDeclarePledgeWarLog(declaringPledge, declaredPledge, bothDeclared);
    if (bothDeclared) {
        declaringPledge->SystemMessage(0xd7, EST_STRING, std::u16string(declaredPledge->GetPledgeName()).c_str());
        declaredPledge->SystemMessage(0xd7, EST_STRING, std::u16string(declaringPledge->GetPledgeName()).c_str());
    } else {
        declaringPledge->SystemMessage(0x61a, EST_STRING, std::u16string(declaredPledge->GetPledgeName()).c_str());
        declaredPledge->SystemMessage(0x619, EST_STRING, std::u16string(declaringPledge->GetPledgeName()).c_str());
    }

    declaringPledge->MemberAffectedByWar();
    declaredPledge->MemberAffectedByWar();
    declaringPledge->EndTransaction();
    RelayClient::Inst()->SendDeclarePledgeWarPacket(declaringPledge, declaredPledge);

    CSPointer<User> user = GetUserBySID(mDeclareUserSid);
    if (user) {
        declaringPledge->SendViewPledgeWarList(user->GetSocket(), 0, 0);
    }
    GIP._Add(
        L"CMD_clanWarfareInfo(%d, %d, %d, %d, %d", g_nWorldId, declaringPledge->GetDbId(), declaredPledge->GetDbId(), 1,
        static_cast<std::int64_t>(std::time(nullptr))
    );
    return true;
}

void DxDeclareWar::DoServerJob() {
    // Current L2Server 0x006f7fe4.
    L2_GUARD_FUNCTION();
    if (mResult == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"dxerror:DxDeclareWar [%d, %d, %d]",
        //     mPledgeDbId,
        //     mEnemyPledgeDbId,
        //     mDeclareTime)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[clan war] Cannot declare war from clan DB ID %d against clan DB ID %d: CacheD returned failure. Declaration
        //     time %d.", mPledgeDbId, mEnemyPledgeDbId, mDeclareTime)
        L2_LOG_ERROR(
            Log,
            u"[clan war] Cannot declare war from clan DB ID %d against clan DB ID %d: CacheD returned failure; declaration "
            u"time %d.",
            mPledgeDbId, mEnemyPledgeDbId, mDeclareTime
        );
        return;
    }
    (void)DeclareWar();
}

bool DxCancelWar::PostCancelWar() {
    // Current L2Server 0x006f0db8.
    L2_GUARD_FUNCTION();
    CPledge* const declaringPledge = CPledge::GetObjectW(static_cast<std::uint32_t>(mPledgeSid));
    CPledge* const declaredPledge = CPledge::GetObjectW(static_cast<std::uint32_t>(mEnemyPledgeSid));
    if (declaringPledge == nullptr || declaredPledge == nullptr) {
        return false;
    }

    if (mUpdateNameValue) {
        PledgeStuff::UpdateNameValueOf(declaringPledge, -10000);
    }
    WriteCancelPledgeWarLog(declaringPledge, declaredPledge);
    if (mUpdateNameValue) {
        declaringPledge->SystemMessage(0x61f, EST_STRING, std::u16string(declaredPledge->GetPledgeName()).c_str());
        declaredPledge->SystemMessage(0x61e, EST_STRING, std::u16string(declaringPledge->GetPledgeName()).c_str());
    } else {
        declaringPledge->SystemMessage(0xee5, EST_STRING, std::u16string(declaredPledge->GetPledgeName()).c_str());
        declaredPledge->SystemMessage(0xee6, EST_STRING, std::u16string(declaringPledge->GetPledgeName()).c_str());
    }
    declaringPledge->MemberAffectedByWar();
    declaredPledge->MemberAffectedByWar();
    return true;
}

bool DxCancelWar::CancelWar() {
    // Current L2Server 0x006f7acc.
    L2_GUARD_FUNCTION();
    CPledge* const declaringPledge = CPledge::GetObjectW(static_cast<std::uint32_t>(mPledgeSid));
    CPledge* const declaredPledge = CPledge::GetObjectW(static_cast<std::uint32_t>(mEnemyPledgeSid));
    if (declaringPledge == nullptr) {
        return false;
    }
    if (declaredPledge == nullptr) {
        declaringPledge->EndTransaction();
        return false;
    }

    declaringPledge->CancelWar(declaredPledge, mCancelTime);
    declaringPledge->UpdateInfoWindowPacket();
    declaredPledge->TargetedBy(declaringPledge, 0, false);
    declaredPledge->UpdateInfoWindowPacket();
    (void)PostCancelWar();
    declaringPledge->EndTransaction();

    CSPointer<User> user = GetUserBySID(mCancelUserSid);
    if (user) {
        declaringPledge->SendViewPledgeWarList(user->GetSocket(), 0, 0);
    }
    GIP._Add(
        L"CMD_clanWarfareInfo(%d, %d, %d, %d, %d", g_nWorldId, declaringPledge->GetDbId(), declaredPledge->GetDbId(), 0,
        static_cast<std::int64_t>(std::time(nullptr))
    );
    return true;
}

void DxCancelWar::DoServerJob() {
    // Current L2Server 0x006f80b8. MStopType is ignored.
    L2_GUARD_FUNCTION();
    if (mResult == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"dxerror:DxCancelWar [%d, %d]",
        //     mPledgeDbId,
        //     mEnemyPledgeDbId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[clan war] Cannot cancel war from clan DB ID %d against clan DB ID %d: CacheD returned failure.",
        //     mPledgeDbId, mEnemyPledgeDbId)
        L2_LOG_ERROR(
            Log, u"[clan war] Cannot cancel war from clan DB ID %d against clan DB ID %d: CacheD returned failure.",
            mPledgeDbId, mEnemyPledgeDbId
        );
        return;
    }
    (void)CancelWar();
}

void DxWriteHeroWords::DoServerJob() {
    // Current L2Server 0x006f8e64. The words payload is not published by
    // this side. The DBID lookup and smart-pointer lifetime are the body.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, mUserDbId));
}

void DxAddOlympiadTradePoint::DoServerJob() {
    L2_GUARD_FUNCTION();
    if (mReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"dxerror:DxAddOlympiadTradePoint [%d, %d]",
        //     mUserDbId,
        //     mOlympiadTradePoint)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[olympiad] Cannot add %d trade points to character DB ID %d: CacheD returned failure.",
        //     mOlympiadTradePoint, mUserDbId)
        L2_LOG_ERROR(
            Log, u"[olympiad] Cannot add %d trade points to character DB ID %d: CacheD returned failure.",
            mOlympiadTradePoint, mUserDbId
        );
    }
}

void DxDeleteOlympiadTradePoint::DoServerJob() {
    // Current L2Server 0x006f7378.
    L2_GUARD_FUNCTION();
    if (mReturn == 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSid));
        if (creature != nullptr && creature->IsPC()) {
            User* const user = creature->CastUser();
            if (user != nullptr) {
                user->m_data->SetNPCGiveItemJobDone();
            }
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"dxerror:DxDeleteOlympiadTradePoint [%d, %d]",
        //     mUserDbId,
        //     mOlympiadTradePoint)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[olympiad] Cannot remove %d trade points from character DB ID %d: CacheD returned failure. The NPC result
        //     reply is not sent.", mOlympiadTradePoint, mUserDbId)
        L2_LOG_ERROR(
            Log,
            u"[olympiad] Cannot remove %d trade points from character DB ID %d: CacheD returned failure; the NPC result "
            u"reply is not sent.",
            mOlympiadTradePoint, mUserDbId
        );
        return;
    }
    NpcServerUtil::SendDeleteOlympiadTradePointReturned(mNpcSid, mUserSid, mOlympiadTradePoint, mReturn);
}

void CDxSetFortressFacility::DoServerJob() {
    // Current L2Server 0x006f5ea0.
    L2_GUARD_FUNCTION();
    if (m_nReturn != 0) {
        CResidence* const residence = g_ResidenceDB.GetResidence(m_nFortressId);
        if (residence != nullptr && residence->IsFortress()) {
            CFortress* const fortress = residence->CastFortress();
            if (fortress != nullptr) {
                const int previousLevel = fortress->GetFacilityLevel(m_nFacilityType);
                fortress->SetFacilityLevel(m_nFacilityType, m_nFacilityLevel);

                CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserId));
                User* user = nullptr;
                if (creature != nullptr && creature->IsPC()) {
                    user = creature->CastUser();
                }
                CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
                if (user != nullptr && pledge != nullptr) {
                    const int residenceId = fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0;
                    // Log.Add(
                    //     LOG_BOTH,
                    //     L"%I64d,%I64d,%I64d,,,,,,%s,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
                    //     0x7e0,
                    //     user->GetAccountID(),
                    //     user->GetDBID(),
                    //     pledge->GetPledgeName(),
                    //     pledge->GetDbId(),
                    //     residenceId,
                    //     m_nFacilityType,
                    //     previousLevel,
                    //     m_nFacilityLevel,
                    //     user->GetName(),
                    //     user->m_sAccountName.data())
                    // Log.Add(
                    //     LOG_BOTH,
                    //     L"%I64d,%I64d,%I64d,,,,,,%s,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
                    //     0x7e0,
                    //     user->GetAccountID(),
                    //     user->GetDBID(),
                    //     pledge->GetPledgeName(),
                    //     pledge->GetDbId(),
                    //     residenceId,
                    //     m_nFacilityType,
                    //     previousLevel,
                    //     m_nFacilityLevel,
                    //     Utf16ToWide(user->GetName()).c_str(),
                    //     user->m_sAccountName.data())
                    Log.Add(
                        LOG_BOTH, u"%I64d,%I64d,%I64d,,,,,,%s,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,", 0x7e0,
                        user->GetAccountID(), user->GetDBID(), pledge->GetPledgeName(), pledge->GetDbId(), residenceId,
                        m_nFacilityType, previousLevel, m_nFacilityLevel, user->GetName(), user->m_sAccountName.data()
                    );
                }
            }
        }
    }

    if (m_nNpcId != -1 && m_nUserId != -1) {
        g_npcserver.Send("cddddddd", 0x68, m_nFortressId, 3, m_nNpcId, m_nUserId, m_nReturn, -1, -1);
    }
}

void CDxResetFortressFacility::DoServerJob() {
    // Current L2Server 0x006f09bc.
    L2_GUARD_FUNCTION();
    CResidence* const residence = g_ResidenceDB.GetResidence(m_nFortressId);
    if (residence != nullptr && residence->IsFortress()) {
        CFortress* const fortress = residence->CastFortress();
        if (fortress != nullptr) {
            fortress->ResetFacilityLevel();
        }
    }
}

void CApplyInstantAgit::DoServerJob() {
    // Current L2Server 0x006f3b34.
    L2_GUARD_FUNCTION();
    if (nReturn != 0) {
        CInstantAgitManager::inst().OnApplyInstantAgit(instantAgitId, pledgeDBId);
    }
    if (npcIndex != -1 || userIndex != -1) {
        const auto response = static_cast<InstantAgitApplyResponse::Code>(nReturn < 1 ? nReason : 0);
        CInstantAgitManager::SendApplyResponse(
            npcIndex, userIndex, response, inzoneTypeId, instantAgitId, static_cast<std::int64_t>(rentalFee)
        );
        if (nReason == 2 && CInstantAgitManager::inst().GetInstantAgitData(instantAgitId) != nullptr) {
            CInstantAgitManager::inst().OnApplyInstantAgit(instantAgitId, pledgeDBId);
        }
    }
}

void CAddInstantAgitDeco::DoServerJob() {
    // Current L2Server 0x006f3c50.
    L2_GUARD_FUNCTION();
    if (nReturn != 0) {
        CInstantAgitManager::inst().OnAddInstantAgitDeco(pledgeDBId, agitDecoId);
    }
    if (npcIndex != -1 || userIndex != -1) {
        g_npcserver.Send("ccddd", 0xaf, 1, npcIndex, userIndex, nReturn);
    }
}

void DxMatchingPoint::DoServerJob() {
    // Current L2Server 0x006f8f40.
    L2_GUARD_FUNCTION();
    const wchar_t* const typeNames[] = {
        L"pledge game",
        L"olympiad",
    };
    const wchar_t* const operationNames[] = {
        L"load",
        L"save",
    };
    CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(m_nUserSID));
    CUserSocket* const socket = user->GetSocket();
    if (user && socket != nullptr) {
        std::array<char16_t, 1024> message{};
        std::int64_t basePoint = 0;
        g_MatchingPointDB.GetMatchingPointBase(m_cType, user->m_data->m_nLevel, basePoint);
        const std::int64_t total = runtime::AddInt64Wrapping(basePoint, m_nMatchingPointDiff);
        FormatUtf16To(
            message.data(), message.size(), u"matching_point %ls %ls: base: %lld, diff: %lld, total: %lld",
            operationNames[m_isSave ? 1 : 0], typeNames[static_cast<int>(m_cType)], static_cast<long long>(basePoint),
            static_cast<long long>(m_nMatchingPointDiff), static_cast<long long>(total)
        );
        socket->SendSystemMessage(u"SYS", message.data());
    }
}

void CResetAllResidenceDecoNpc::DoServerJob() {
    // Current L2Server 0x006f5cb4.
    L2_GUARD_FUNCTION();
    if (m_nReturn != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Failed at %s : %d, %d",
        //     L"CResetAllResidenceDecoNpc::DoServerJob",
        //     m_nAgitCID,
        //     m_nReturn)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] CacheD could not reset all decorations in residence %d. Response code %d.",
        //     m_nAgitCID, m_nReturn)
        L2_LOG_ERROR(
            Log, u"[decoration NPCs] CacheD could not reset all decorations in residence %d; response code %d.", m_nAgitCID,
            m_nReturn
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(m_nAgitCID);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Agit is null at %s : %d",
        //     L"CResetAllResidenceDecoNpc::DoServerJob",
        //     m_nAgitCID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] Cannot apply the reset-all reply: residence %d was not found.",
        //     m_nAgitCID)
        L2_LOG_ERROR(Log, u"[decoration NPCs] Cannot apply the reset-all reply: residence %d was not found.", m_nAgitCID);
        return;
    }

    CResidenceData* const data = residence->m_data;
    for (int slot = 0; slot < data->m_nDecoNpcSlotCount; ++slot) {
        auto& npc = data->m_aDecoNpc[slot];
        if (npc.nCID == 0) {
            continue;
        }
        if (npc.nNpcSID != 0) {
            DeleteResidenceDecoNpc(npc.nNpcSID);
        }
        const FVector position(0.0, 0.0, 0.0);
        ResidenceUtil::WriteDecoNpcSetLog(
            0x123, 0, 0, position, m_nPledgeDBID, m_nAgitCID, slot, npc.nCID, npc.tExpire,
            ResidenceUtil::DecoNpcSetReason::ChangeOwner
        );
        npc.nCID = 0;
        npc.tExpire = 0;
        npc.nNpcSID = 0;
    }
}

void CExpireResidenceDecoNpc::DoServerJob() {
    // Current L2Server 0x006f59bc.
    L2_GUARD_FUNCTION();
    CResidence* const residence = g_ResidenceDB.GetResidence(m_nAgitCID);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Agit is not exist at %s : %d",
        //     L"CExpireResidenceDecoNpc::DoServerJob",
        //     m_nAgitCID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] Cannot apply expiration replies: residence %d was not found.",
        //     m_nAgitCID)
        L2_LOG_ERROR(Log, u"[decoration NPCs] Cannot apply expiration replies: residence %d was not found.", m_nAgitCID);
        return;
    }

    constexpr int kSourceLine = 0x44c;
    for (int index = 0; index < m_nCount; ++index) {
        const int slot = m_aAgitSlots[index];
        if (m_aReturns[index] != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[DECONPC]Reset Deco Npc is failed at %s : %d, %d",
            //     L"CExpireResidenceDecoNpc::DoServerJob",
            //     m_nAgitCID,
            //     slot)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[decoration NPCs] CacheD could not expire the decoration in residence %d, slot %d. Response code %d.
            //     This slot is skipped.", m_nAgitCID, slot, m_aReturns[index])
            L2_LOG_ERROR(
                Log,
                u"[decoration NPCs] CacheD could not expire the decoration in residence %d, slot %d; response code %d. This "
                u"slot is skipped.",
                m_nAgitCID, slot, m_aReturns[index]
            );
            continue;
        }

        CResidenceData* const data = residence->m_data;
        if (slot < 0 || slot >= data->m_nDecoNpcSlotCount) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[DECONPC]Agit Slot is invalid at %s : %d, %d, %d",
            //     L"CExpireResidenceDecoNpc::DoServerJob",
            //     m_nAgitCID,
            //     slot,
            //     data->m_nDecoNpcSlotCount)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[decoration NPCs] Cannot finish expiration in residence %d: slot %d is outside the %d-slot table.",
            //     m_nAgitCID, slot, data->m_nDecoNpcSlotCount)
            L2_LOG_ERROR(
                Log, u"[decoration NPCs] Cannot finish expiration in residence %d: slot %d is outside the %d-slot table.",
                m_nAgitCID, slot, data->m_nDecoNpcSlotCount
            );
            EndResidenceDecoNpcJob(residence, kSourceLine);
            return;
        }

        auto& npc = data->m_aDecoNpc[slot];
        if (npc.nNpcSID != 0) {
            DeleteResidenceDecoNpc(npc.nNpcSID);
        }
        const FVector position(0.0, 0.0, 0.0);
        ResidenceUtil::WriteDecoNpcSetLog(
            0x123, 0, 0, position, m_nPledgeDBID, m_nAgitCID, slot, npc.nCID, npc.tExpire,
            ResidenceUtil::DecoNpcSetReason::Expire
        );
        npc.nCID = 0;
        npc.tExpire = 0;
        npc.nNpcSID = 0;
    }
    EndResidenceDecoNpcJob(residence, kSourceLine);
}

void DxDeletePreviousOlympiadPoint::DoServerJob() {
    L2_GUARD_FUNCTION();
    if (mReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"dxerror:DxDeletePreviousOlympiadPoint [%d, %d]",
        //     mUserDbId,
        //     mPreviousOlympiadPoint)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[olympiad] Cannot remove %d previous-season points from character DB ID %d: CacheD returned failure. The NPC
        //     result reply is not sent.", mPreviousOlympiadPoint, mUserDbId)
        L2_LOG_ERROR(
            Log,
            u"[olympiad] Cannot remove %d previous-season points from character DB ID %d: CacheD returned failure; the NPC "
            u"result reply is not sent.",
            mPreviousOlympiadPoint, mUserDbId
        );
    } else {
        g_npcserver.Send("cdddd", 0x53, mNpcSid, mUserSid, mPreviousOlympiadPoint, mReturn);
    }
}

void CReturnInstantAgit::DoServerJob() {
    L2_GUARD_FUNCTION();
    if (npcIndex != -1 || userIndex != -1) {
        g_npcserver.Send("ccddd", 0xaf, 2, npcIndex, userIndex, nReturn);
    }
}

void CExpireInstantAgitDecoNpc::DoServerJob() {
    L2_GUARD_FUNCTION();
    CInstantAgitManager::inst().OnExpireInstantAgitDecoNpc(m_nPledgeDBID, m_aAgitSlots, m_aReturns, m_nCount);
}

void CResetInstantAgitDecoNpc::DoServerJob() {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUserBySID(m_nUserSID);
    if (m_nReturn == 0) {
        int npcClassId = 0;
        std::int32_t expireTime = 0;
        if (CInstantAgitManager::inst().OnResetInstantAgitDecoNpc(
                user, m_nPledgeDBID, m_nAgitSlot, npcClassId, expireTime
            )) {
            const FVector position(static_cast<double>(m_nPosX), static_cast<double>(m_nPosY), static_cast<double>(m_nPosZ));
            ResidenceUtil::WriteDecoNpcSetLog(
                0x125, m_nUserDBID, m_nUserAccountID, position, m_nPledgeDBID, m_nInstantAgitCID, m_nAgitSlot, npcClassId,
                expireTime, ResidenceUtil::DecoNpcSetReason::Reset
            );
            return;
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Reset Deco Npc is failed at %s : %d, %d",
        //     L"CResetInstantAgitDecoNpc::DoServerJob",
        //     m_nInstantAgitCID,
        //     m_nAgitSlot)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] CacheD could not reset the decoration for clan DB ID %d in instanced hall %d, slot %d.
        //     response code %d.", m_nPledgeDBID, m_nInstantAgitCID, m_nAgitSlot, m_nReturn)
        L2_LOG_ERROR(
            Log,
            u"[decoration NPCs] CacheD could not reset the decoration for clan DB ID %d in instanced hall %d, slot %d; "
            u"response code %d.",
            m_nPledgeDBID, m_nInstantAgitCID, m_nAgitSlot, m_nReturn
        );
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"Failed to set deco npc");
        }
    }
}

void CSetInstantAgitDecoNpc::DoServerJob() {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUserBySID(m_nUserSID);
    const CDecoNpcData* const data = CDecoNpcDB::Inst().FindDecoNpc(static_cast<unsigned int>(m_nDecoCID));
    if (data == nullptr) {
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"invalid deco npc");
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Deco Npc Data is not exist at %s : %d",
        //     L"CSetInstantAgitDecoNpc::DoServerJob",
        //     m_nDecoCID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] Cannot apply the set reply for clan DB ID %d in instanced hall %d, slot %d: decoration
        //     definition %d was not found.", m_nPledgeDBID, m_nInstantAgitCID, m_nAgitSlot, m_nDecoCID)
        L2_LOG_ERROR(
            Log,
            u"[decoration NPCs] Cannot apply the set reply for clan DB ID %d in instanced hall %d, slot %d: decoration "
            u"definition %d was not found.",
            m_nPledgeDBID, m_nInstantAgitCID, m_nAgitSlot, m_nDecoCID
        );
        return;
    }

    if (m_nReturn == 0) {
        if (CInstantAgitManager::inst().OnAddInstantAgitDecoNpc(
                user, m_nPledgeDBID, m_nDecoCID, m_nAgitSlot, m_nExpireTime
            )) {
            const FVector position(static_cast<double>(m_nPosX), static_cast<double>(m_nPosY), static_cast<double>(m_nPosZ));
            ResidenceUtil::WriteDecoNpcSetLog(
                0x125, m_nUserDBID, m_nUserAccountID, position, m_nPledgeDBID, m_nInstantAgitCID, m_nAgitSlot, m_nDecoCID,
                m_nExpireTime, ResidenceUtil::DecoNpcSetReason::Set
            );
            return;
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Failed to set deco npc at %s : %d",
        //     L"CSetInstantAgitDecoNpc::DoServerJob",
        //     m_nReturn)
        // Log.Add(
        //     (m_nReturn == -3 || m_nReturn == -4) ? LOG_WARN : LOG_ERROR,
        //     L"[decoration NPCs] CacheD could not set decoration %d for clan DB ID %d in instanced hall %d, slot %d.
        //     response code %d.", m_nDecoCID, m_nPledgeDBID, m_nInstantAgitCID, m_nAgitSlot, m_nReturn)
        L2_LOG(
            Log, (m_nReturn == -3 || m_nReturn == -4) ? LOG_WARN : LOG_ERROR,
            u"[decoration NPCs] CacheD could not set decoration %d for clan DB ID %d in instanced hall %d, slot %d; "
            u"response code %d.",
            m_nDecoCID, m_nPledgeDBID, m_nInstantAgitCID, m_nAgitSlot, m_nReturn
        );
        if (m_nReturn == -4) {
            SendSystemMessageItemCID(user, 0x1123, data->GetFirstCostItemCID());
        } else if (m_nReturn == -3) {
            if (user && user->GetSocket() != nullptr) {
                user->GetSocket()->SendSystemMessage(0x1122);
            }
        } else {
            if (user && user->GetSocket() != nullptr) {
                user->GetSocket()->SendSystemMessage(u"SYS", u"Failed to set deco npc");
            }
            ResidenceUtil::SendDecoNpcSet(user, m_nReturn, m_nInstantAgitCID, m_nAgitSlot, m_nDecoCID, m_nExpireTime);
        }
    }
}

void CSetResidenceDecoNpc::DoServerJob() {
    // Current L2Server 0x006f980c, original Shared/DXPacket.cpp owner.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUserBySID(m_nUserSID);
    CResidence* const residence = g_ResidenceDB.GetResidence(m_nAgitCID);
    if (residence == nullptr) {
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"invalid residence");
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC] Agit is not exist at %s : %d",
        //     L"CSetResidenceDecoNpc::DoServerJob",
        //     m_nAgitCID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] Cannot apply the set reply: residence %d was not found.",
        //     m_nAgitCID)
        L2_LOG_ERROR(Log, u"[decoration NPCs] Cannot apply the set reply: residence %d was not found.", m_nAgitCID);
        return;
    }

    constexpr int kSourceLine = 0x337;
    const CDecoNpcData* const data = CDecoNpcDB::Inst().FindDecoNpc(static_cast<unsigned int>(m_nDecoCID));
    if (data == nullptr) {
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"invalid deco npc");
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC] Deco Npc Data is not exist at %s : %d",
        //     L"CSetResidenceDecoNpc::DoServerJob",
        //     m_nDecoCID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] Cannot apply the set reply for residence %d, slot %d: decoration definition %d was not
        //     found.", m_nAgitCID, m_nAgitSlot, m_nDecoCID)
        L2_LOG_ERROR(
            Log,
            u"[decoration NPCs] Cannot apply the set reply for residence %d, slot %d: decoration definition %d was not "
            u"found.",
            m_nAgitCID, m_nAgitSlot, m_nDecoCID
        );
        EndResidenceDecoNpcJob(residence, kSourceLine);
        return;
    }

    if (m_nReturn != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC] Failed to set deco npc at %s : %d",
        //     L"CSetResidenceDecoNpc::DoServerJob",
        //     m_nReturn)
        // Log.Add(
        //     (m_nReturn == -3 || m_nReturn == -4) ? LOG_WARN : LOG_ERROR,
        //     L"[decoration NPCs] CacheD could not set decoration %d for clan DB ID %d in residence %d, slot %d. Response
        //     code %d.", m_nDecoCID, m_nPledgeDBID, m_nAgitCID, m_nAgitSlot, m_nReturn)
        L2_LOG(
            Log, (m_nReturn == -3 || m_nReturn == -4) ? LOG_WARN : LOG_ERROR,
            u"[decoration NPCs] CacheD could not set decoration %d for clan DB ID %d in residence %d, slot %d; response "
            u"code %d.",
            m_nDecoCID, m_nPledgeDBID, m_nAgitCID, m_nAgitSlot, m_nReturn
        );
        if (m_nReturn == -4) {
            SendSystemMessageItemCID(user, 0x1123, data->GetFirstCostItemCID());
        } else if (m_nReturn == -3) {
            if (user && user->GetSocket() != nullptr) {
                user->GetSocket()->SendSystemMessage(0x1122);
            }
        } else if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"Failed to set deco npc");
        }
        EndResidenceDecoNpcJob(residence, kSourceLine);
        return;
    }

    CResidenceData* const residenceData = residence->m_data;
    if (m_nAgitSlot < 0 || m_nAgitSlot >= residenceData->m_nDecoNpcSlotCount) {
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"Failed to set deco npc");
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC] Agit Slot is invalid at %s : %d, %d, %d",
        //     L"CSetResidenceDecoNpc::DoServerJob",
        //     m_nAgitCID,
        //     m_nAgitSlot,
        //     residenceData->m_nDecoNpcSlotCount)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] Cannot apply the set reply for residence %d: slot %d is outside the %d-slot table.",
        //     m_nAgitCID, m_nAgitSlot, residenceData->m_nDecoNpcSlotCount)
        L2_LOG_ERROR(
            Log, u"[decoration NPCs] Cannot apply the set reply for residence %d: slot %d is outside the %d-slot table.",
            m_nAgitCID, m_nAgitSlot, residenceData->m_nDecoNpcSlotCount
        );
        EndResidenceDecoNpcJob(residence, kSourceLine);
        return;
    }

    const char16_t* const npcName = g_ObjectDB.GetName(data->m_nNpcCID);
    if (npcName == nullptr) {
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"Failed to set deco npc");
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC] No Npc Class name at %s : %d, %d",
        //     L"CSetResidenceDecoNpc::DoServerJob",
        //     m_nDecoCID,
        //     data->m_nNpcCID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] Cannot spawn decoration %d in residence %d, slot %d: NPC class %d has no registered
        //     name.", m_nDecoCID, m_nAgitCID, m_nAgitSlot, data->m_nNpcCID)
        L2_LOG_ERROR(
            Log,
            u"[decoration NPCs] Cannot spawn decoration %d in residence %d, slot %d: NPC class %d has no registered name.",
            m_nDecoCID, m_nAgitCID, m_nAgitSlot, data->m_nNpcCID
        );
        EndResidenceDecoNpcJob(residence, kSourceLine);
        return;
    }

    auto& npc = residenceData->m_aDecoNpc[m_nAgitSlot];
    npc.nCID = m_nDecoCID;
    npc.tExpire = m_nExpireTime;
    npc.nNpcSID = 0;
    const auto& slot = residenceData->m_aDecoNpcSlots[m_nAgitSlot];
    NpcServerUtil::SendRequestDecoNpcSpawn(npcName, slot.pos, 0, slot.nYaw, 1, m_nAgitCID, m_nAgitSlot);

    const FVector position(static_cast<double>(m_nPosX), static_cast<double>(m_nPosY), static_cast<double>(m_nPosZ));
    ResidenceUtil::WriteDecoNpcSetLog(
        0x123, m_nUserDBID, m_nUserAccountID, position, m_nPledgeDBID, m_nAgitCID, m_nAgitSlot, m_nDecoCID, m_nExpireTime,
        ResidenceUtil::DecoNpcSetReason::Set
    );
    if (user && user->GetSocket() != nullptr) {
        user->GetSocket()->SendSystemMessage(0x110d);
    }
    ResidenceUtil::SendDecoNpcSet(user, m_nReturn, m_nAgitCID, m_nAgitSlot, m_nDecoCID, m_nExpireTime);
    EndResidenceDecoNpcJob(residence, kSourceLine);
}

void CResetResidenceDecoNpc::DoServerJob() {
    // Current L2Server 0x006f5590, original Shared/DXPacket.cpp owner.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUserBySID(m_nUserSID);
    CResidence* const residence = g_ResidenceDB.GetResidence(m_nAgitCID);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Agit is not exist at %s : %d",
        //     L"CResetResidenceDecoNpc::DoServerJob",
        //     m_nAgitCID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] Cannot apply the reset reply: residence %d was not found.",
        //     m_nAgitCID)
        L2_LOG_ERROR(Log, u"[decoration NPCs] Cannot apply the reset reply: residence %d was not found.", m_nAgitCID);
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"Failed to set deco npc");
        }
        return;
    }

    constexpr int kSourceLine = 0x3d5;
    if (m_nReturn != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Reset Deco Npc is failed at %s : %d, %d",
        //     L"CResetResidenceDecoNpc::DoServerJob",
        //     m_nAgitCID,
        //     m_nAgitSlot)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] CacheD could not reset the decoration in residence %d, slot %d. Response code %d.",
        //     m_nAgitCID, m_nAgitSlot, m_nReturn)
        L2_LOG_ERROR(
            Log, u"[decoration NPCs] CacheD could not reset the decoration in residence %d, slot %d; response code %d.",
            m_nAgitCID, m_nAgitSlot, m_nReturn
        );
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"Failed to set deco npc");
        }
        EndResidenceDecoNpcJob(residence, kSourceLine);
        return;
    }

    CResidenceData* const residenceData = residence->m_data;
    if (m_nAgitSlot < 0 || m_nAgitSlot >= residenceData->m_nDecoNpcSlotCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Agit Slot is invalid at %s : %d, %d, %d",
        //     L"CResetResidenceDecoNpc::DoServerJob",
        //     m_nAgitCID,
        //     m_nAgitSlot,
        //     residenceData->m_nDecoNpcSlotCount)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[decoration NPCs] Cannot apply the reset reply for residence %d: slot %d is outside the %d-slot table.",
        //     m_nAgitCID, m_nAgitSlot, residenceData->m_nDecoNpcSlotCount)
        L2_LOG_ERROR(
            Log, u"[decoration NPCs] Cannot apply the reset reply for residence %d: slot %d is outside the %d-slot table.",
            m_nAgitCID, m_nAgitSlot, residenceData->m_nDecoNpcSlotCount
        );
        EndResidenceDecoNpcJob(residence, kSourceLine);
        return;
    }

    auto& npc = residenceData->m_aDecoNpc[m_nAgitSlot];
    if (npc.nNpcSID != 0) {
        DeleteResidenceDecoNpc(npc.nNpcSID);
    }
    const FVector position(static_cast<double>(m_nPosX), static_cast<double>(m_nPosY), static_cast<double>(m_nPosZ));
    ResidenceUtil::WriteDecoNpcSetLog(
        0x123, m_nUserDBID, m_nUserAccountID, position, m_nPledgeDBID, m_nAgitCID, m_nAgitSlot, npc.nCID, npc.tExpire,
        ResidenceUtil::DecoNpcSetReason::Reset
    );
    npc.nCID = 0;
    npc.tExpire = 0;
    npc.nNpcSID = 0;
    if (user && user->GetSocket() != nullptr) {
        user->GetSocket()->SendSystemMessage(0x111c);
    }
    ResidenceUtil::SendDecoNpcSetForReset(user, 0, m_nAgitCID, m_nAgitSlot);
    EndResidenceDecoNpcJob(residence, kSourceLine);
}

void DxLoadOlympiad::DoServerJob() {
    L2_GUARD_FUNCTION();
    g_JobFinishSignal.NotifyFinish(JFT_LOAD_OLYMPIAD);
}

void DxLoadPledgeGame::DoServerJob() {
    // Current L2Server 0x006f3a38.
    L2_GUARD_FUNCTION();
    g_PledgeGameSystem.LoadFromDb(
        mSeason, mStep, mSeasonStartTime, mMaxPrizeCount, mPrevSeasonWinnerPledgeDbId, mPrevSeasonWinnerPledgeName,
        mPrevSeasonWinnerCharacterDbId, mPrevSeasonWinnerCharacterName, mPrevSeasonWinnerGotItem
    );
    static_cast<void>(runtime::crt::LocalTime64(mSeasonStartTime));
    g_JobFinishSignal.NotifyFinish(JFT_LOAD_PLEDGEGAME);
}

void DxSetClass::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxStartOlympiadSeason::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxInitAllOlympiadPoint::DoServerJob() {
    L2_GUARD_FUNCTION();
    if (mReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"DxInitAllOlympiadPoint [%d]",
        //     mInitPoint)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[olympiad] Cannot initialize points for season %d, step %d: CacheD returned failure. Initial points %d.",
        //     mSeason, mStep, mInitPoint)
        L2_LOG_ERROR(
            Log, u"[olympiad] Cannot initialize points for season %d, step %d: CacheD returned failure; initial points %d.",
            mSeason, mStep, mInitPoint
        );
    }
}

void DxAddAllOlympiadBonusPoint::DoServerJob() {
    L2_GUARD_FUNCTION();
    if (mReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"dxerror:DxAddAllOlympiadBonusPoint [%d]",
        //     mBonusPoint)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[olympiad] Cannot add bonus points for season %d, step %d: CacheD returned failure. Bonus %d.",
        //     mSeason, mStep, mBonusPoint)
        L2_LOG_ERROR(
            Log, u"[olympiad] Cannot add bonus points for season %d, step %d: CacheD returned failure; bonus %d.", mSeason,
            mStep, mBonusPoint
        );
    }
}

void DxNominateHeroes::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxSaveOlympiadRecord::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxAddOlympiadPoint::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxResetOlympiadPoint::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxReloadHeroRank::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxReloadHeroById::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxSaveSeasonStartTime::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxSaveOlympiadTerm::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxSetHistoryOpen::DoServerJob() { L2_GUARD_FUNCTION(); }

void DxFishingChampionShip::DoServerJob() { L2_GUARD_FUNCTION(); }

void CDX_WOLYMPIAD_SAVE_RECORD::DoServerJob() {
    L2_GUARD_FUNCTION();
    if (m_nWinnerDBID != 0 && m_nResultType != 2) {
        CDX_WOLYMPIAD_RELOAD_USER request(m_nWinnerWorldID, m_nWinnerDBID);
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x4c), static_cast<int>(sizeof(request)), request);
    }
    if (m_nLoserDBID != 0) {
        CDX_WOLYMPIAD_RELOAD_USER request(m_nLoserWorldID, m_nLoserDBID);
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x4c), static_cast<int>(sizeof(request)), request);
    }
}

void CDX_WOLYMPIAD_ADD_POINT::DoServerJob() {
    L2_GUARD_FUNCTION();
    CDX_WOLYMPIAD_RELOAD_USER request(m_nWorldID, m_nUserDBID);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x4c), static_cast<int>(sizeof(request)), request);
}

void CDX_WOLYMPIAD_ADD_TRADE_POINT::DoServerJob() {
    L2_GUARD_FUNCTION();
    if (m_nReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]"
        //     L"CDX_WOLYMPIAD_ADD_TRADE_POINT failed: %d, %d, %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x19f3,
        //     m_nWorldID,
        //     m_nUserDBID,
        //     m_nTradePoint)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[world olympiad] Cannot add %d trade points to character DB ID %d in world %d: CacheD returned failure.",
        //     m_nTradePoint, m_nUserDBID, m_nWorldID)
        L2_LOG_ERROR(
            Log, u"[world olympiad] Cannot add %d trade points to character DB ID %d in world %d: CacheD returned failure.",
            m_nTradePoint, m_nUserDBID, m_nWorldID
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]"
        //     L"World Olympiad Trade Point added: %d, %d, %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x19fb,
        //     m_nWorldID,
        //     m_nUserDBID,
        //     m_nTradePoint)
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[world olympiad] CacheD confirmed addition of %d trade points to character DB ID %d in world %d (response
        //     %d).", m_nTradePoint, m_nUserDBID, m_nWorldID, m_nReturn)
        L2_LOG_INFO(
            Log,
            u"[world olympiad] CacheD confirmed addition of %d trade points to character DB ID %d in world %d (response "
            u"%d).",
            m_nTradePoint, m_nUserDBID, m_nWorldID, m_nReturn
        );
        CDX_WOLYMPIAD_RELOAD_USER request(m_nWorldID, m_nUserDBID);
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x4c), static_cast<int>(sizeof(request)), request);
    }
}

void CDX_WOLYMPIAD_RESET_POINT::DoServerJob() {
    L2_GUARD_FUNCTION();
    CDX_WOLYMPIAD_RELOAD_USER request(m_nWorldID, m_nUserDBID);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x4c), static_cast<int>(sizeof(request)), request);
}

void CDX_WOLYMPIAD_CHECK_PEER::DoServerJob() {
    // Current L2Server 0x006fa2cc.
    WOlympiadServiceUtil::SendOSA_VERSION(m_nWorldID, m_bIsValid);
    if (!m_bIsValid) {
        WOlympiadNetworkHost::inst().UnregisterPeer(m_nWorldID);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]peer is invalid: %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x1443,
        //     m_nWorldID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[world olympiad] Peer for world %d failed validation and was unregistered.",
        //     m_nWorldID)
        L2_LOG_ERROR(Log, u"[world olympiad] Peer for world %d failed validation and was unregistered.", m_nWorldID);
        return;
    }

    WOlympiadService& service = WOlympiadService::inst();
    WOlympiadServiceUtil::BroadcastOSN_NOTIFY_SEASON(
        service.GetSeason(), service.GetStep(), service.GetSeasonStart(), service.GetNominateSec(),
        service.GetLegendaryWorld()
    );
    const std::vector<std::CDualKey<int, int>> heroes = WOlympiadUsers::inst().GetHeroes(m_nWorldID);
    for (const std::CDualKey<int, int>& hero : heroes) {
        service.NotifyNoblessInfo(hero, false);
    }
    service.NotifyOlympiadMatchingInfo(m_nWorldID);
}

void CDX_WOLYMPIAD_LOAD::DoServerJob() {
    // Current L2Server 0x006f3df8.
    L2_GUARD_FUNCTION();
    WOlympiadService& service = WOlympiadService::inst();
    service.LoadFromDb(m_nSeason, m_nStep, m_tSeasonStart, m_idLegendary);

    for (int index = 0; index < OlympiadDefine::HERO_CLASS_COUNT; ++index) {
        CDX_WOLYMPIAD_GET_TOP10 request(service.GetSeason() - 1, static_cast<int>(OlympiadDefine::GetHeroClassType(index)));
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x44), static_cast<int>(sizeof(request)), request);
    }

    if (m_nNominateSec > 0) {
        const L2DateTime seasonStart = L2DateTime::FromTime_t(m_tSeasonStart);
        const bool monthly =
            seasonStart.m_nDay == 1 && seasonStart.m_nHour == 0 && seasonStart.m_nMin == 0 && seasonStart.m_nSec == 0;
        service.SetOlympiadTerm(
            monthly, false, m_nSeason, m_nStartSec, m_nBonus1Sec, m_nBonus2Sec, m_nBonus3Sec, m_nBonus4Sec, m_nNominateSec,
            false, true
        );
    }
    g_JobFinishSignal.NotifyFinish(JFT_LOAD_OLYMPIAD);
    WHeroSystem::inst().Init();
}

void CDX_WOLYMPIAD_GET_TOP10::DoServerJob() {
    // Current L2Server 0x006f19f8.
    L2_GUARD_FUNCTION();
    const ClassType heroClass = static_cast<ClassType>(m_nHeroClassType);
    WOlympiadServiceUtil::ResetRankInfo(heroClass);

    int tieCount = 1;
    int ranking = 0;
    int previousPoint = -1;
    int previousWinCount = -1;
    int previousMatchCount = -1;
    for (int index = 0; index < m_nCount; ++index) {
        const UserRank& rank = m_Ranks[index];
        if (previousPoint == rank.nPoint && previousWinCount == rank.nWinCount && previousMatchCount == rank.nMatchCount) {
            ++tieCount;
        } else {
            ranking += tieCount;
            previousPoint = rank.nPoint;
            previousWinCount = rank.nWinCount;
            previousMatchCount = rank.nMatchCount;
            tieCount = 1;
        }
        const std::wstring name = Utf16ToWide(rank.sCharName);
        WOlympiadServiceUtil::AddRankInfo(heroClass, ranking, name.c_str(), rank.nPoint);
    }
}

void CDX_WOLYMPIAD_GET_PLAYER_COUNT::DoServerJob() {
    // Current L2Server 0x006f1b28.
    L2_GUARD_FUNCTION();
    WOlympiadServiceUtil::SetSeasonPlayerCount(m_nCount);
}

void CDX_WOLYMPIAD_ADD_USER::DoServerJob() {
    // Current L2Server 0x006fa548.
    L2_GUARD_FUNCTION();
    if (m_nReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]failed to add user: %d, %d, %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x1703,
        //     m_nWorldID,
        //     m_nUserDBID,
        //     0)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[world olympiad] Cannot add character DB ID %d from world %d: CacheD returned failure. Initial points %d.",
        //     m_nUserDBID, m_nWorldID, m_nDefaultOlympiadPoint)
        L2_LOG_ERROR(
            Log,
            u"[world olympiad] Cannot add character DB ID %d from world %d: CacheD returned failure; initial points %d.",
            m_nUserDBID, m_nWorldID, m_nDefaultOlympiadPoint
        );
    }

    const bool success = m_nReturn == 1;
    const std::CDualKey<int, int> key(m_nWorldID, m_nUserDBID);
    if (success) {
        WOlympiadUsers::inst().SetNoblessInfo(key, 0, 0, 0, m_nDefaultOlympiadPoint, 0, 0, 0, 0, false);
        CDX_WOLYMPIAD_RELOAD_USER request(m_nWorldID, m_nUserDBID);
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x4c), static_cast<int>(sizeof(request)), request);
    }

    if (m_eNextAction == NA_Register) {
        WOlympiadService::inst().OnAddUser(success, m_nWorldID, m_nUserDBID);
        WOlympiadStuff::Write_LOG_WOLYMPIAD_SET_NOBLESS_InHost(
            m_nWorldID, m_nUserDBID, m_szCharName, m_nClass, m_nHeroClassType
        );
    } else if (m_eNextAction == NA_NoblessInfo && success) {
        WOlympiadService::inst().NotifyNoblessInfo(key, false);
    }
}

void CDX_WOLYMPIAD_NOMINATE_HEROES::DoServerJob() {
    // Current L2Server 0x006f3ffc.
    L2_GUARD_FUNCTION();
    WOlympiadService& service = WOlympiadService::inst();
    if (m_nNewSeason == 0) {
        db.RequestLoadAllWOlympiadUser(false, true);
        for (int index = 0; index < OlympiadDefine::HERO_CLASS_COUNT; ++index) {
            CDX_WOLYMPIAD_GET_TOP10 request(
                service.GetSeason() - 1, static_cast<int>(OlympiadDefine::GetHeroClassType(index))
            );
            db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x44), static_cast<int>(sizeof(request)), request);
        }
    } else {
        service.SetFromDb(m_nNewSeason, m_nNewStep, m_tNewSeasonStart, m_idLegendary);
        service.SetDefaultOlympiadTerm(m_nNewSeason);
        if (m_nNewSeason > 1) {
            WOlympiadServiceUtil::BroadcastStopSeason(m_nNewSeason - 1);
        }
        for (int index = 0; index < OlympiadDefine::HERO_CLASS_COUNT; ++index) {
            CDX_WOLYMPIAD_GET_TOP10 request(m_nNewSeason - 1, static_cast<int>(OlympiadDefine::GetHeroClassType(index)));
            db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x44), static_cast<int>(sizeof(request)), request);
        }
    }

    CDX_WOLYMPIAD_RELOAD_HERO_RANK reloadRank;
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x49), static_cast<int>(sizeof(reloadRank)), reloadRank);
    service.UnregisterDbProcessing();
}

void CDX_WOLYMPIAD_RELOAD_HERO_RANK::DoServerJob() {
    // Current L2Server 0x006f41f4.
    L2_GUARD_FUNCTION();
    WHeroSystem::inst().SetHeroRank(m_nHeroCount, m_nHeroClassList, m_nHeroWorldIdList, m_nHeroIdList);
    WHeroSystem::inst().RequestReloadAllHeroesToCacheD();
}

void CDX_WOLYMPIAD_RELOAD_USER::DoServerJob() {
    // Current L2Server 0x006fa42c.
    L2_GUARD_FUNCTION();
    const std::CDualKey<int, int> key(m_nWorldID, m_nUserDBID);
    WOlympiadUsers::inst().SetNoblessInfo(
        key, m_nHeroType, m_nWinCount, m_nPreviousPoint, m_nOlympiadPoint, m_nMatchCount, m_nOlympiadWinCount,
        m_nOlympiadLoseCount, m_nOlympiadTradePoint, false
    );
    WOlympiadService::inst().NotifyNoblessInfo(key, false);
}

void CDX_WOLYMPIAD_INIT_ALL_POINT::DoServerJob() {
    // Current L2Server 0x006f4370.
    L2_GUARD_FUNCTION();
    if (m_nReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]CDX_WOLYMPIAD_INIT_ALL_POINT: %d, %d, %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x175c,
        //     m_nSeason,
        //     m_nStep,
        //     m_nInitPoint)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[world olympiad] Cannot initialize points for season %d, step %d: CacheD returned failure. Initial points
        //     %d.", m_nSeason, m_nStep, m_nInitPoint)
        L2_LOG_ERROR(
            Log,
            u"[world olympiad] Cannot initialize points for season %d, step %d: CacheD returned failure; initial points %d.",
            m_nSeason, m_nStep, m_nInitPoint
        );
    } else {
        db.RequestLoadAllWOlympiadUser(false, true);
        WOlympiadService::inst().SetFromDb(m_nSeason, m_nStep, 0, -1);
        WOlympiadServiceUtil::BroadcastStartSeason(m_nSeason);
    }
    WOlympiadService::inst().UnregisterDbProcessing();
    if (m_nReturn != 0 && m_nInitPoint > 0) {
        WOlympiadServiceUtil::BroadcastOSN_OLYMPIAD_POINT(m_nInitPoint);
    }
}

void CDX_WOLYMPIAD_ADD_ALL_POINT::DoServerJob() {
    // Current L2Server 0x006f44a0.
    L2_GUARD_FUNCTION();
    if (m_nReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]CDX_WOLYMPIAD_ADD_ALL_POINT: %d, %d, %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x1797,
        //     m_nSeason,
        //     m_nStep,
        //     m_nBonusPoint)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[world olympiad] Cannot add bonus points for season %d, step %d: CacheD returned failure. Bonus %d.",
        //     m_nSeason, m_nStep, m_nBonusPoint)
        L2_LOG_ERROR(
            Log, u"[world olympiad] Cannot add bonus points for season %d, step %d: CacheD returned failure; bonus %d.",
            m_nSeason, m_nStep, m_nBonusPoint
        );
    } else {
        db.RequestLoadAllWOlympiadUser(false, false);
        WOlympiadService::inst().SetFromDb(m_nSeason, m_nStep, 0, -1);
    }
    WOlympiadService::inst().UnregisterDbProcessing();
    if (m_nReturn != 0) {
        WOlympiadServiceUtil::BroadcastOSN_OLYMPIAD_POINT(m_nBonusPoint);
    }
}

void CDX_WOLYMPIAD_RELOAD_HERO_BY_ID::DoServerJob() {
    // Current L2Server 0x006f46f8.
    L2_GUARD_FUNCTION();
    const std::CDualKey<int, int> key(m_nWorldID, m_nUserDBID);
    WHeroSystem& heroes = WHeroSystem::inst();
    const int currentClass = heroes.GetHeroClass(key);
    if (m_nHeroClassType != currentClass) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Wrong class: %d, %d, %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x185c,
        //     m_nWorldID,
        //     m_nUserDBID,
        //     currentClass)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[world olympiad] Hero reload ignored for character DB ID %d from world %d: expected hero class %d, received
        //     %d.", m_nUserDBID, m_nWorldID, currentClass, m_nHeroClassType)
        L2_LOG_ERROR(
            Log,
            u"[world olympiad] Hero reload ignored for character DB ID %d from world %d: expected hero class %d, received "
            u"%d.",
            m_nUserDBID, m_nWorldID, currentClass, m_nHeroClassType
        );
        return;
    }

    heroes.SetHeroProfile(m_nHeroClassType, key, FixedUtf16View(m_szCharName));
    heroes.SetHeroWinCount(m_nHeroClassType, m_nWinCount);
    heroes.SetHeroType(m_nHeroClassType, m_nHeroType);
    heroes.SetHeroWords(m_nHeroClassType, FixedUtf16View(m_szWords));
    heroes.SetHeroPledge(m_nHeroClassType, m_nPledgeDBID, FixedUtf16View(m_szPledgeName), m_nPledgeCrestID);
    heroes.SetHeroAlliance(m_nHeroClassType, m_nAllianceDBID, FixedUtf16View(m_szAllianceName), m_nAllianceCrestID);
    heroes.SetHistoryOpen(m_nHeroClassType, m_nHistoryOpen != 0);
    heroes.ClearHeroMatchResult(key);
    db.RequestWOlympiadGetMatchResult(m_nWorldID, m_nUserDBID);
}

void CDX_WOLYMPIAD_START_SEASON::DoServerJob() {
    // Current L2Server 0x006f49fc.
    L2_GUARD_FUNCTION();
    if (m_nReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]CDX_WOLYMPIAD_START_SEASON failed: %d, %d, %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x18b6,
        //     m_nSeason,
        //     m_nStep,
        //     m_tSeasonStart)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[world olympiad] Cannot start season %d, step %d: CacheD returned failure. Start timestamp %d.",
        //     m_nSeason, m_nStep, static_cast<int>(m_tSeasonStart))
        L2_LOG_ERROR(
            Log, u"[world olympiad] Cannot start season %d, step %d: CacheD returned failure; start timestamp %d.",
            m_nSeason, m_nStep, static_cast<int>(m_tSeasonStart)
        );
    } else {
        WOlympiadService::inst().SetFromDb(m_nSeason, m_nStep, m_tSeasonStart, 0);
    }
    WOlympiadService::inst().UnregisterDbProcessing();
}

void CDX_WOLYMPIAD_SAVE_HERO_TYPE::DoServerJob() {
    // Current L2Server 0x006fa734.
    L2_GUARD_FUNCTION();
    if (m_nReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]CDX_WOLYMPIAD_SAVE_HERO_TYPE failed: %d, %d, %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x190f,
        //     m_nWorldID,
        //     m_nUserDBID,
        //     m_nHeroType)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[world olympiad] Cannot save hero status for character DB ID %d from world %d: CacheD returned failure.
        //     requested type %d.", m_nUserDBID, m_nWorldID, m_nHeroType)
        L2_LOG_ERROR(
            Log,
            u"[world olympiad] Cannot save hero status for character DB ID %d from world %d: CacheD returned failure; "
            u"requested type %d.",
            m_nUserDBID, m_nWorldID, m_nHeroType
        );
        return;
    }

    const std::CDualKey<int, int> key(m_nWorldID, m_nUserDBID);
    if (HeroType_IsHero(m_nHeroType) && WOlympiadUsers::inst().GetNoblessIndex(key) != -1) {
        CQUERY_WOLYMPIAD_GET_HERO_BY_ID request(m_nWorldID, m_nUserDBID);
        db.RequestTPacketProcess(
            g_nWorldId, static_cast<short>(0x51), static_cast<int>(sizeof(request)), reinterpret_cast<CStackOnly&>(request)
        );
    }

    CDX_WOLYMPIAD_RELOAD_USER reload(m_nWorldID, m_nUserDBID);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x4c), static_cast<int>(sizeof(reload)), reload);
    WHeroSystem::inst().SetHeroTypeById(key, m_nHeroType);

    const SharedNoblessInfo* const info = WOlympiadUsers::inst().GetNoblessInfo(key);
    const int winCount = info != nullptr ? info->nWinCount : -1;
    if (HeroType_IsHero(m_nHeroType)) {
        WOlympiadStuff::Write_LOG_WOLYMPIAD_SET_HERO_InHost(
            m_nWorldID, m_nUserDBID, m_szCharName, WHeroSystem::inst().GetHeroClass(key), winCount
        );
    }

    olympiad::OSA_SET_HERO_TYPE response{};
    response.nUserDBID = m_nUserDBID;
    response.nHeroType = m_nHeroType;
    response.nNPCSID = m_nNpcSID;
    response.nWinCount = winCount;
    response.cResult = 0;
    WOlympiadNetworkHost::inst().SendPacket(m_nWorldID, OSA_SET_HERO_TYPE, response);
}

void CDX_WOLYMPIAD_DELETE_TRADE_POINT::DoServerJob() {
    // Current L2Server 0x006fa99c.
    L2_GUARD_FUNCTION();
    const std::CDualKey<int, int> key(m_nWorldID, m_nUserDBID);
    const SharedNoblessInfo* const info = WOlympiadUsers::inst().GetNoblessInfo(key);
    const int totalTradePoint = info != nullptr ? info->nOlympiadTradePoint : -1;
    if (m_nReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]CDX_WOLYMPIAD_DELETE_TRADE_POINT failed: %d, %d, %d, %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x19a7,
        //     m_nWorldID,
        //     m_nUserDBID,
        //     m_nTradePoint,
        //     totalTradePoint)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[world olympiad] Cannot remove %d trade points from character DB ID %d in world %d: CacheD returned failure.
        //     Local cached balance: %d (%ls).", m_nTradePoint, m_nUserDBID, m_nWorldID, totalTradePoint, info != nullptr ?
        //     L"record present" : L"record missing, value unavailable")
        L2_LOG_ERROR(
            Log,
            u"[world olympiad] Cannot remove %d trade points from character DB ID %d in world %d: CacheD returned failure. "
            u"Local cached balance: %d (%ls).",
            m_nTradePoint, m_nUserDBID, m_nWorldID, totalTradePoint,
            info != nullptr ? L"record present" : L"record missing; value unavailable"
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]World Olympiad Trade Point deleted: %d, %d, %d, %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x19b0,
        //     m_nWorldID,
        //     m_nUserDBID,
        //     m_nTradePoint,
        //     totalTradePoint)
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[world olympiad] CacheD confirmed removal of %d trade points from character DB ID %d in world %d (response
        //     %d). Local cached balance: %d (%ls).", m_nTradePoint, m_nUserDBID, m_nWorldID, m_nReturn, totalTradePoint,
        //     info != nullptr ? L"record present" : L"record missing, value unavailable")
        L2_LOG_INFO(
            Log,
            u"[world olympiad] CacheD confirmed removal of %d trade points from character DB ID %d in world %d (response "
            u"%d). Local cached balance: %d (%ls).",
            m_nTradePoint, m_nUserDBID, m_nWorldID, m_nReturn, totalTradePoint,
            info != nullptr ? L"record present" : L"record missing; value unavailable"
        );
        CDX_WOLYMPIAD_RELOAD_USER reload(m_nWorldID, m_nUserDBID);
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x4c), static_cast<int>(sizeof(reload)), reload);
    }

    if (m_nNpcSID != 0) {
        olympiad::OSA_DELETE_TRADE_POINT response{};
        response.nUserDBID = m_nUserDBID;
        response.nTradePoint = m_nTradePoint;
        response.nNPCSID = m_nNpcSID;
        response.bSuccess = m_nReturn != 0;
        WOlympiadNetworkHost::inst().SendPacket(m_nWorldID, OSA_DELETE_TRADE_POINT, response);
    }
}

void CDX_WOLYMPIAD_MATCHING_POINT::DoServerJob() {
    // Current L2Server 0x006f9168.
    L2_GUARD_FUNCTION();
    if (!m_bSuccess) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]CDX_WOLYMPIAD_MATCHING_POINT failed: %d, %d",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x1a52,
        //     m_nWorldID,
        //     m_nUserDBID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[world olympiad] Cannot load matching points for character DB ID %d from world %d: CacheD returned
        //     failure.", m_nUserDBID, m_nWorldID)
        L2_LOG_ERROR(
            Log,
            u"[world olympiad] Cannot load matching points for character DB ID %d from world %d: CacheD returned failure.",
            m_nUserDBID, m_nWorldID
        );
        return;
    }
    const std::int64_t total = g_MatchingPointDB.ValidateMatchingPoint(MatchingPoint::MPT_OLYMPIAD, 1000, m_nPoint, 0);
    char16_t message[256]{};
    FormatUtf16To(
        message, std::size(message), u"matching_point - base:%d, diff:%d, total:%d", 1000, static_cast<int>(m_nPoint),
        static_cast<int>(total)
    );
    const std::u16string target = std::u16string(message);
    WOlympiadServiceUtil::SendOSN_SYSTEM_MESSAGE(
        std::CDualKey<int, int>(m_nWorldID, m_nUserDBID), std::u16string(std::u16string_view(target))
    );
}

#endif

CDX_WOLYMPIAD_CHECK_PEER::CDX_WOLYMPIAD_CHECK_PEER(const int worldId) : m_nWorldID(worldId), m_bIsValid(false) {}

CDX_WOLYMPIAD_ADD_USER::CDX_WOLYMPIAD_ADD_USER(
    const int worldId,
    const int userDbId,
    const int classType,
    const int heroClassType,
    const char16_t* const charName,
    const int pledgeDbId,
    const char16_t* const pledgeName,
    const int pledgeCrestId,
    const int allianceDbId,
    const char16_t* const allianceName,
    const int allianceCrestId,
    const int defaultOlympiadPoint,
    const NextAction nextAction
) :
    m_nWorldID(worldId),
    m_nUserDBID(userDbId),
    m_nClass(classType),
    m_nHeroClassType(heroClassType),
    m_szCharName{},
    m_nPledgeDBID(pledgeDbId),
    m_szPledgeName{},
    m_nPledgeCrestID(pledgeCrestId),
    m_nAllianceDBID(allianceDbId),
    m_szAllianceName{},
    m_nAllianceCrestID(allianceCrestId),
    m_nDefaultOlympiadPoint(defaultOlympiadPoint),
    m_nReturn(0),
    m_eNextAction(nextAction) {
    CopyFixedTargetString(m_szCharName, charName);
    CopyFixedTargetString(m_szPledgeName, pledgeName);
    CopyFixedTargetString(m_szAllianceName, allianceName);
}

CDX_WOLYMPIAD_ADD_POINT::CDX_WOLYMPIAD_ADD_POINT(const int worldId, const int userDbId, const int point) :
    m_nWorldID(worldId), m_nPoint(point), m_nUserDBID(userDbId) {}

CDX_WOLYMPIAD_RESET_POINT::CDX_WOLYMPIAD_RESET_POINT(const int worldId, const int userDbId, const int point) :
    m_nWorldID(worldId), m_nUserDBID(userDbId), m_nPoint(point) {}

DxSaveSeasonStartTime::DxSaveSeasonStartTime(const int season, const std::int32_t seasonStartTime) :
    mSeason(season), mSeasonStartTime(seasonStartTime) {}

CDX_WOLYMPIAD_START_SEASON::CDX_WOLYMPIAD_START_SEASON(const int season, const int step, const std::int32_t seasonStart) :
    m_nSeason(season), m_nStep(step), m_tSeasonStart(seasonStart), m_nReturn(0), nothing{} {
#if defined(L2_BUILD_L2SERVER)
    WOlympiadService::inst().RegisterDbProcessing();
#endif
}

CDX_WOLYMPIAD_SAVE_HERO_TYPE::CDX_WOLYMPIAD_SAVE_HERO_TYPE(
    const int worldId,
    const int userDbId,
    const int heroType,
    const char16_t* const charName,
    const int pledgeDbId,
    const char16_t* const pledgeName,
    const int pledgeCrestId,
    const int allianceDbId,
    const char16_t* const allianceName,
    const int allianceCrestId,
    const int npcSid,
    const int userSid
) :
    m_nWorldID(worldId),
    m_nUserDBID(userDbId),
    m_nHeroType(heroType),
    m_szCharName{},
    m_nPledgeDBID(pledgeDbId),
    m_szPledgeName{},
    m_nPledgeCrestID(pledgeCrestId),
    m_nAllianceDBID(allianceDbId),
    m_szAllianceName{},
    m_nAllianceCrestID(allianceCrestId),
    m_nNpcSID(npcSid),
    m_nUserSID(userSid),
    m_nReturn(0) {
    CopyFixedTargetString(m_szCharName, charName);
    CopyFixedTargetString(m_szPledgeName, pledgeName);
    CopyFixedTargetString(m_szAllianceName, allianceName);
}

CDX_WOLYMPIAD_DELETE_TRADE_POINT::CDX_WOLYMPIAD_DELETE_TRADE_POINT(
    const int worldId,
    const int userDbId,
    const char16_t* const charName,
    const int accountId,
    const char16_t* const accountName,
    const int tradePoint,
    const int npcSid
) :
    m_nWorldID(worldId),
    m_nUserDBID(userDbId),
    m_szCharName{},
    m_nAccountID(accountId),
    m_szAccountName{},
    m_nTradePoint(tradePoint),
    m_nNpcSID(npcSid),
    m_nReturn(0) {
    CopyFixedTargetString(m_szCharName, charName);
    CopyFixedTargetString(m_szAccountName, accountName);
}

CDX_WOLYMPIAD_ADD_TRADE_POINT::CDX_WOLYMPIAD_ADD_TRADE_POINT(const int worldId, const int userDbId, const int tradePoint) :
    m_nWorldID(worldId), m_nUserDBID(userDbId), m_nTradePoint(tradePoint), m_nReturn(0) {}

CDX_WOLYMPIAD_MATCHING_POINT::CDX_WOLYMPIAD_MATCHING_POINT(const int worldId, const int userDbId) :
    m_nUserDBID(userDbId), m_nWorldID(worldId), m_nPoint(0), m_bSuccess(false) {}

CDX_ATOMICJOB_BASE::CDX_ATOMICJOB_BASE(AtomicJob* const job) :
    m_nJobKey(job != nullptr ? Singleton<ObjectCache<AtomicJob, 10240>>::inst().Push(job) : 0) {}

CDX_PLEDGE_MASTERY_LOAD::CDX_PLEDGE_MASTERY_LOAD(const int pledgeDatabaseId, AtomicJob* const job) :
    CDX_ATOMICJOB_BASE(job), m_idPledge(pledgeDatabaseId), m_bSuccess(true) {}

CDX_WOLYMPIAD_RECORD::CDX_WOLYMPIAD_RECORD(const int worldId, const int userDbId, const int season, AtomicJob* const job) :
    CDX_ATOMICJOB_BASE(job),
    idWorld(worldId),
    idUser(userDbId),
    nSeason(season),
    nPoint(0),
    nWinCount(0),
    nLoseCount(0),
    nPrevClassType(0),
    nPrevRank(0),
    nPrevRankCount(0),
    nPrevClassRank(0),
    nPrevClassRankCount(0),
    nPrevClassRankByServer(0),
    nPrevClassRankByServerCount(0),
    nPrevPoint(0),
    nPrevWinCount(0),
    nPrevLoseCount(0) {}

CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10::CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10(
    const int worldId, const int heroClassType, const int season, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job),
    m_idWorld(worldId),
    m_nHeroClassType(heroClassType),
    m_nSeason(season),
    m_nCount(0),
    m_Ranks{} {}

CDX_WOLYMPIAD_GENERATE_RECORD_FOR_TEST::CDX_WOLYMPIAD_GENERATE_RECORD_FOR_TEST(
    const int maxOlympiadPoint, const int maxOlympiadWinCount, const int maxOlympiadMatchCount, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job),
    nMaxOlympiadPoint(maxOlympiadPoint),
    nMaxOlympiadWinCount(maxOlympiadWinCount),
    nMaxOlympiadMatchCount(maxOlympiadMatchCount) {}

CDX_PLEDGE_CONTRIBUTION_MEMBER_ADD::CDX_PLEDGE_CONTRIBUTION_MEMBER_ADD(
    const int userDatabaseId, const int pledgeDatabaseId, const int point, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job), m_dbidUser(userDatabaseId), m_dbidPledge(pledgeDatabaseId), m_nPoint(point) {}

CDX_PLEDGE_CONTRIBUTION_MEMBER_RESET::CDX_PLEDGE_CONTRIBUTION_MEMBER_RESET(
    const int userDatabaseId, const int pledgeDatabaseId, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job), m_dbidUser(userDatabaseId), m_dbidPledge(pledgeDatabaseId) {}

CDX_PLEDGE_MISSION_SET_COUNT::CDX_PLEDGE_MISSION_SET_COUNT(
    const int userDbId, const int missionId, const int count, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job), nUserDbId_(userDbId), nMissionId_(missionId), nCount_(count) {}

CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_LOAD::CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_LOAD(
    const int userDbId, const std::int32_t checkWeekStartTime, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job),
    nUserDbId_(userDbId),
    tCheckWeekStartTime_(checkWeekStartTime),
    tWeekStartTime_(0),
    nCount_(0) {}

CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_RESET::CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_RESET(
    const int userDatabaseId, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job), nUserDbId_(userDatabaseId) {}

CDX_PLEDGE_CONTRIBUTION_MEMBER_TAKE_PERIOD_REWARD::CDX_PLEDGE_CONTRIBUTION_MEMBER_TAKE_PERIOD_REWARD(
    const int userDatabaseId, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job),
    m_idUser(userDatabaseId),
    m_idPledge(0),
    m_nResult(1),
    m_nRank(0),
    m_nRatio(0),
    m_nActiveMember(0) {}

CDX_PLEDGE_CONTRIBUTION_RANK_LOAD::CDX_PLEDGE_CONTRIBUTION_RANK_LOAD(AtomicJob* const job) :
    CDX_ATOMICJOB_BASE(job), nSeason(0), tStartTime(0) {}

CDX_PLEDGE_CONTRIBUTION_RANK_SAVE::CDX_PLEDGE_CONTRIBUTION_RANK_SAVE(
    const int season, const std::int32_t startTime, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job), nSeason(season), tStartTime(startTime) {}

CDX_PLEDGE_CONTRIBUTION_RANK_DECIDE::CDX_PLEDGE_CONTRIBUTION_RANK_DECIDE(
    const int season, const std::int32_t startTime, const int minimumPoint, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job), nSeason(season), tStartTime(startTime), nMinPoint(minimumPoint) {}

CDX_PLEDGE_RAID_LOAD::CDX_PLEDGE_RAID_LOAD(AtomicJob* const job) :
    CDX_ATOMICJOB_BASE(job), nSeason_(0), tStartTime_(0), nPrevSeason_(0), tPrevStartTime_(0) {}

CDX_PLEDGE_RAID_SAVE::CDX_PLEDGE_RAID_SAVE(const int season, const std::int32_t startTime, AtomicJob* const job) :
    CDX_ATOMICJOB_BASE(job), nSeason_(season), tStartTime_(startTime) {}

CDX_PLEDGE_RAID_NEXT_SEASON::CDX_PLEDGE_RAID_NEXT_SEASON(const std::int32_t startTime, AtomicJob* const job) :
    CDX_ATOMICJOB_BASE(job), tStartTime_(startTime) {}

CDX_PLEDGE_RAID_RESET_SEASON::CDX_PLEDGE_RAID_RESET_SEASON(const std::int32_t resetTime, AtomicJob* const job) :
    CDX_ATOMICJOB_BASE(job), tResetTime_(resetTime) {}

CDX_PLEDGE_RAID_GET_INFO::CDX_PLEDGE_RAID_GET_INFO(
    const int season, const int worldId, const int pledgeDbId, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job),
    nSeason_(season),
    nWorldId_(worldId),
    nPledgeDbId_(pledgeDbId),
    szPledgeName_{},
    nRank_(0),
    nPoint_(0),
    nRewardReason_(0),
    nTotalCount_(0) {}

CDX_PLEDGE_RAID_GET_TOTAL_COUNT::CDX_PLEDGE_RAID_GET_TOTAL_COUNT(
    const int season, const int isReward, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job), nSeason_(season), nIsReward_(isReward), nTotalCount_(0) {}

CDX_PLEDGE_RAID_SET_INFO::CDX_PLEDGE_RAID_SET_INFO(
    const int season,
    const int worldId,
    const int pledgeDbId,
    const wchar_t* const pledgeName,
    const std::int64_t point,
    const std::int32_t saveTime,
    AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job),
    nSeason_(season),
    nWorldId_(worldId),
    nPledgeDbId_(pledgeDbId),
    szPledgeName_{},
    tSaveTime_(saveTime),
    nPoint_(point) {
    const std::u16string targetName = pledgeName != nullptr ? WideToUtf16(pledgeName) : std::u16string{};
    const std::size_t copied = std::min<std::size_t>(targetName.size(), std::size(szPledgeName_) - 1);
    std::copy_n(targetName.data(), copied, szPledgeName_);
    szPledgeName_[copied] = u'\0';
}

CDX_PLEDGE_RAID_HUNT_NPC_INFO::CDX_PLEDGE_RAID_HUNT_NPC_INFO(
    const wchar_t* const npcDbName, const std::int32_t deathTime, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job), szNpcDbName_{}, tDeathTime_(deathTime) {
    const std::u16string targetName = npcDbName != nullptr ? WideToUtf16(npcDbName) : std::u16string{};
    const std::size_t copied = std::min<std::size_t>(targetName.size(), std::size(szNpcDbName_) - 1);
    std::copy_n(targetName.data(), copied, szNpcDbName_);
    szNpcDbName_[copied] = u'\0';
}

CDX_PLEDGE_RAID_INZONE_SAVE::CDX_PLEDGE_RAID_INZONE_SAVE(
    const int pledgeDbId, const std::int32_t restrictTime, AtomicJob* const job
) :
    CDX_ATOMICJOB_BASE(job), nPledgeDbId_(pledgeDbId), tRestrictTime_(restrictTime) {}

CDX_PLEDGE_RAID_INZONE_DELETE::CDX_PLEDGE_RAID_INZONE_DELETE(const int pledgeDbId, AtomicJob* const job) :
    CDX_ATOMICJOB_BASE(job), nPledgeDbId_(pledgeDbId) {}

CDX_PLEDGE_RAID_INZONE_RESET::CDX_PLEDGE_RAID_INZONE_RESET(AtomicJob* const job) : CDX_ATOMICJOB_BASE(job) {}

#if defined(L2_BUILD_L2SERVER)

void CDX_ATOMICJOB_BASE::DoServerJob() {
    L2_GUARD_FUNCTION();
    if (m_nJobKey == 0) {
        return;
    }
    AtomicJob* const job = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Pop(m_nJobKey);
    PledgeStuff::DoJobDx(job, this, true);
    PledgeStuff::DestroyJob(job);
}

#endif

DxAddTimeAttackRecord::DxAddTimeAttackRecord(
    int roomNo, SSQPart::type ssqPart, std::int64_t point, int recordTime, int elapsedTime
) :
    mErrorCode(0),
    mRoomNo(roomNo),
    mSSQPart(ssqPart),
    mPoint(point),
    mRecordTime(recordTime),
    mElapsedTime(elapsedTime),
    mMemberCount(0),
    mMemberNameList{},
    mMemberDBIDList{},
    mTopRecordUpdated(0),
    mPartRecordUpdated(0) {}

bool DxAddTimeAttackRecord::AddMember(const char16_t* memberName, int memberDatabaseId) {
    if (mMemberCount >= kMaximumTimeAttackMembers) {
        return false;
    }
    CopyTargetMemberName(mMemberNameList[mMemberCount], memberName);
    mMemberDBIDList[mMemberCount] = memberDatabaseId;
    ++mMemberCount;
    return true;
}

int DxAddTimeAttackRecord::GetMemberCount() const { return mMemberCount; }

DxAddTimeAttackFee::DxAddTimeAttackFee(int roomNo, std::int64_t fee) : mErrorCode(0), mRoomNO(roomNo), mFee(fee) {}

DxSetTimeAttackRewardFlag::DxSetTimeAttackRewardFlag(int userDatabaseId, int roomNo, int partType, int flag) :
    mErrorCode(0), mUserDBID(userDatabaseId), mRoomNO(roomNo), mPartType(partType), mFlag(flag) {}

CSetResidenceDecoNpc::CSetResidenceDecoNpc(
    int userSID,
    int userDBID,
    int userAccountID,
    const FVector& position,
    int pledgeDBID,
    int agitCID,
    int agitSlot,
    int decoCID,
    int expireTime,
    int cost,
    int itemCID,
    std::int64_t itemCount,
    int returnValue
) :
    m_nUserSID(userSID),
    m_nUserDBID(userDBID),
    m_nUserAccountID(userAccountID),
    m_nPosX(static_cast<int>(position.x)),
    m_nPosY(static_cast<int>(position.y)),
    m_nPosZ(static_cast<int>(position.z)),
    m_nPledgeDBID(pledgeDBID),
    m_nAgitCID(agitCID),
    m_nAgitSlot(agitSlot),
    m_nDecoCID(decoCID),
    m_nExpireTime(expireTime),
    m_nCost(cost),
    m_nItemCID(itemCID),
    m_nItemCount(itemCount),
    m_nReturn(returnValue) {}

CResetResidenceDecoNpc::CResetResidenceDecoNpc(
    int userSID, int userDBID, int userAccountID, const FVector& position, int pledgeDBID, int agitCID, int agitSlot
) :
    m_nUserSID(userSID),
    m_nUserDBID(userDBID),
    m_nUserAccountID(userAccountID),
    m_nPosX(static_cast<int>(position.x)),
    m_nPosY(static_cast<int>(position.y)),
    m_nPosZ(static_cast<int>(position.z)),
    m_nPledgeDBID(pledgeDBID),
    m_nAgitCID(agitCID),
    m_nAgitSlot(agitSlot),
    m_nReturn(0) {}

CResetAllResidenceDecoNpc::CResetAllResidenceDecoNpc(const int pledgeDatabaseId, const int agitId) noexcept :
    m_nPledgeDBID(pledgeDatabaseId), m_nAgitCID(agitId), m_nReturn(0) {}

CExpireResidenceDecoNpc::CExpireResidenceDecoNpc(
    const int pledgeDatabaseId, const int agitId, const int count, const int* const agitSlots
) noexcept :
    m_nPledgeDBID(pledgeDatabaseId), m_nAgitCID(agitId), m_nCount(count), m_aAgitSlots{}, m_aReturns{} {
    // Current L2Server 0x006f0974 trusts count and initializes only the
    // corresponding request/return pairs.
    for (int index = 0; index < m_nCount; ++index) {
        m_aAgitSlots[index] = agitSlots[index];
        m_aReturns[index] = 0;
    }
}

CSetInstantAgitDecoNpc::CSetInstantAgitDecoNpc(
    int userSID,
    int userDBID,
    int userAccountID,
    const FVector& position,
    int pledgeDBID,
    int instantAgitCID,
    int agitSlot,
    int decoCID,
    int expireTime,
    int cost,
    int itemCID,
    std::int64_t itemCount,
    int returnValue
) :
    m_nUserSID(userSID),
    m_nUserDBID(userDBID),
    m_nUserAccountID(userAccountID),
    m_nPosX(static_cast<int>(position.x)),
    m_nPosY(static_cast<int>(position.y)),
    m_nPosZ(static_cast<int>(position.z)),
    m_nPledgeDBID(pledgeDBID),
    m_nInstantAgitCID(instantAgitCID),
    m_nAgitSlot(agitSlot),
    m_nDecoCID(decoCID),
    m_nExpireTime(expireTime),
    m_nCost(cost),
    m_nItemCID(itemCID),
    m_nItemCount(itemCount),
    m_nReturn(returnValue) {}

CResetInstantAgitDecoNpc::CResetInstantAgitDecoNpc(
    int userSID, int userDBID, int userAccountID, const FVector& position, int instantAgitCID, int pledgeDBID, int agitSlot
) :
    m_nUserSID(userSID),
    m_nUserDBID(userDBID),
    m_nUserAccountID(userAccountID),
    m_nPosX(static_cast<int>(position.x)),
    m_nPosY(static_cast<int>(position.y)),
    m_nPosZ(static_cast<int>(position.z)),
    m_nInstantAgitCID(instantAgitCID),
    m_nPledgeDBID(pledgeDBID),
    m_nAgitSlot(agitSlot),
    m_nReturn(0) {}

CExpireInstantAgitDecoNpc::CExpireInstantAgitDecoNpc(int instantAgitCID, int pledgeDBID, int count, const int* agitSlots) :
    m_nInstantAgitCID(instantAgitCID), m_nPledgeDBID(pledgeDBID), m_nCount(count), m_aAgitSlots{}, m_aReturns{} {
    for (int index = 0; index < m_nCount; ++index) {
        m_aAgitSlots[index] = agitSlots[index];
        m_aReturns[index] = 0;
    }
}

CDX_WOLYMPIAD_GET_TOP10::CDX_WOLYMPIAD_GET_TOP10(const int heroClassType, const int season) :
    m_nHeroClassType(heroClassType), m_nSeason(season), m_nCount(0) {
    // The current inline constructor leaves all ten rank rows
    // untouched.
}

CDX_WOLYMPIAD_GET_PLAYER_COUNT::CDX_WOLYMPIAD_GET_PLAYER_COUNT(const int season) : m_nSeason(season), m_nCount(0) {}

CDX_WOLYMPIAD_RELOAD_HERO_RANK::CDX_WOLYMPIAD_RELOAD_HERO_RANK() :
    m_nHeroCount(0), m_nHeroClassList{}, m_nHeroWorldIdList{}, m_nHeroIdList{} {}

CDX_WOLYMPIAD_SAVE_TERM::CDX_WOLYMPIAD_SAVE_TERM(
    const int season,
    const int startSec,
    const int bonus1Sec,
    const int bonus2Sec,
    const int bonus3Sec,
    const int bonus4Sec,
    const int nominateSec
) :
    m_nSeason(season),
    m_nStartSec(startSec),
    m_nBonus1Sec(bonus1Sec),
    m_nBonus2Sec(bonus2Sec),
    m_nBonus3Sec(bonus3Sec),
    m_nBonus4Sec(bonus4Sec),
    m_nNominateSec(nominateSec) {}

CDX_WOLYMPIAD_SAVE_MATCH_COUNT::CDX_WOLYMPIAD_SAVE_MATCH_COUNT(
    const int worldId, const int userDbId, const int season, const int step, const int gameRule, const int matchCount
) :
    m_nWorldID(worldId),
    m_nUserDBID(userDbId),
    m_nSeason(season),
    m_nStep(step),
    m_nGameRule(gameRule),
    m_nMatchCount(matchCount) {}

CDX_WOLYMPIAD_LOG_PENALTY::CDX_WOLYMPIAD_LOG_PENALTY(
    const int worldId,
    const int userDbId,
    const int point,
    const int totalPoint,
    const int fieldId,
    const int gameRule,
    const int penaltyType
) :
    m_nWorldID(worldId),
    m_nUserDBID(userDbId),
    m_nPoint(point),
    m_nTotalPoint(totalPoint),
    m_nFieldID(fieldId),
    m_nGameRule(gameRule),
    m_nPenaltyType(penaltyType) {}

CDX_WOLYMPIAD_SAVE_RECORD::CDX_WOLYMPIAD_SAVE_RECORD(
    const int season,
    const int step,
    const int winnerWorldId,
    const int winnerDbId,
    const int loserWorldId,
    const int loserDbId,
    const int winnerPoint,
    const int winnerItemType,
    const int winnerItemAmount,
    const int loserPoint,
    const int loserItemType,
    const int loserItemAmount,
    const int time,
    const int resultType,
    const int elapsedTime,
    const int gameRuleType
) :
    m_nSeason(season),
    m_nStep(step),
    m_nWinnerWorldID(winnerWorldId),
    m_nWinnerDBID(winnerDbId),
    m_nLoserWorldID(loserWorldId),
    m_nLoserDBID(loserDbId),
    m_nWinnerPoint(winnerPoint),
    m_nWinnerItemType(winnerItemType),
    m_nWinnerItemAmount(winnerItemAmount),
    m_nLoserPoint(loserPoint),
    // Current 0x006f1c1c copies the winner item pair into both sides.
    // the two loser-item arguments are observable but unused.
    m_nLoserItemType(winnerItemType),
    m_nLoserItemAmount(winnerItemAmount),
    m_nTime(time),
    m_nResultType(resultType),
    m_nElapsedTime(elapsedTime),
    m_nGameRuleType(gameRuleType) {
    (void)loserItemType;
    (void)loserItemAmount;
}

CDX_WOLYMPIAD_MATCHING_POINT_SAVE::CDX_WOLYMPIAD_MATCHING_POINT_SAVE() :
    m_nCount(0), m_anNewPoints{}, m_anOldPoints{}, m_anUserDBIDs{}, m_anWorldIDs{}, m_anMatchIDs{} {
    // Current constructor leaves m_anResults untouched.
}

void CDX_WOLYMPIAD_MATCHING_POINT_SAVE::AddRecord(
    const int worldId, const int userDbId, const int matchId, const std::int64_t newPoint, const std::int64_t oldPoint
) {
    L2_GUARD_FUNCTION();
    if (m_nCount > 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"..\\Shared\\DXPacket.cpp",
        //     0x1a85)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[world olympiad] Matching-point record dropped: batch count %d reached capacity 3. Character DB ID %d, world
        //     %d, match %d.", m_nCount, userDbId, worldId, matchId)
        L2_LOG_ERROR(
            Log,
            u"[world olympiad] Matching-point record dropped: batch count %d reached capacity 3; character DB ID %d, world "
            u"%d, match %d.",
            m_nCount, userDbId, worldId, matchId
        );
        return;
    }
    m_anWorldIDs[m_nCount] = worldId;
    m_anUserDBIDs[m_nCount] = userDbId;
    m_anNewPoints[m_nCount] = newPoint;
    m_anOldPoints[m_nCount] = oldPoint;
    m_anMatchIDs[m_nCount] = matchId;
    ++m_nCount;
}

bool CDX_WOLYMPIAD_MATCHING_POINT_SAVE::HasRecord() const { return m_nCount != 0; }

CDX_WOLYMPIAD_NOMINATE_HEROES::CDX_WOLYMPIAD_NOMINATE_HEROES(
    const int nowSeason, const int newStep, const std::int32_t newSeasonStart, const bool nominateHeroes
) :
    m_nNowSeason(nowSeason),
    m_nNewSeason(0),
    m_nNewStep(newStep),
    m_tNewSeasonStart(newSeasonStart),
    m_bNominateHeroes(nominateHeroes),
    m_idLegendary(0),
    nothing() {}

CDX_WOLYMPIAD_ADD_ALL_POINT::CDX_WOLYMPIAD_ADD_ALL_POINT(const int season, const int step, const int bonusPoint) :
    m_nSeason(season), m_nStep(step), m_nBonusPoint(bonusPoint), m_nReturn(0), nothing() {}

CDX_WOLYMPIAD_SAVE_SEASON_START_TIME::CDX_WOLYMPIAD_SAVE_SEASON_START_TIME(
    const int season, const std::int32_t seasonStart
) :
    m_nSeason(season), m_tSeasonStartTime(seasonStart) {}

CRenewResidenceDeco::CRenewResidenceDeco(
    const int pledgeId,
    const int agitId,
    const int decoCount,
    int* const decoTypes,
    int* const expires,
    std::int64_t* const prices
) :
    nAgitId(agitId), nDecoCount(decoCount), nPledgeId(pledgeId) {
    // Current L2Server.exe 0x006f0760. The constructor is compiled by the
    // common DXPacket owner and trusts the raw count without clamping it to
    // the fixed 32-record carrier.
    for (int index = 0; index < nDecoCount; ++index) {
        aDecoData[index].nType = static_cast<short>(decoTypes[index]);
        aDecoData[index].nReturn = 0;
        aDecoData[index].nExpire = expires[index];
        aDecoData[index].nPrice = prices[index];
    }
}

#if defined(L2_BUILD_CACHED)

bool CNewbieCheck::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    nIsNewbie = 0;
    CUserSP user = g_userDb.GetUser(nDbId, true, false, runtime::process::CurrentDatabaseId());
    if (!user || user->GetWorld() != GetCacheDConfiguration().worldId) {
        return true;
    }

    if (nLevel >= kNewbieAccountLevelLimit) {
        g_newbieDB.SetExpert(nAccountId);
        return true;
    }

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(nAccountId));
    if (!account) {
        return true;
    }
    if (account->GetMaxLevel() >= kNewbieAccountLevelLimit) {
        g_newbieDB.SetExpert(nAccountId);
        return true;
    }

    if (nLevel < kNewbieCharacterLevelLimit && account->IsNewbieAccount(kNewbieCharacterLevelLimit)) {
        nIsNewbie = g_newbieDB.IsNewbieUser(nAccountId, nDbId) ? 1 : 0;
        return true;
    }

    if (g_newbieDB.IsNewbieAccount(nAccountId)) {
        CUserSP newbie = account->GetNewbieUser(kNewbieAccountLevelLimit);
        if (!newbie) {
            return true;
        }
        const int selectedCharacterId = nLevel < newbie->GetLevel() ? static_cast<int>(newbie->GetId()) : nDbId;
        g_newbieDB.SetNewbieUser(nAccountId, selectedCharacterId);
    }
    nIsNewbie = g_newbieDB.IsNewbieUser(nAccountId, nDbId) ? 1 : 0;
    return true;
}

bool CSetResidenceDeco::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&nReturn);
    const std::wstring decoName = FixedUtf16ToWide(sDecoName);
    const ResidenceDecoActivation active = ResidenceDecoActivation::Active;
    constexpr const wchar_t* statement = L"{Call lin_SetAgitDeco (?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nAgitId);
    connection.BindParameter(&nDecoType);
    connection.BindParameter(&nDecoId);
    connection.BindParameter(decoName.c_str());
    connection.BindParameter(&nLevel);
    connection.BindParameter(&nExpire);
    connection.BindParameter(&active);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return true;
}

bool CResetResidenceDeco::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    nReturn = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&nReturn);
    constexpr const wchar_t* statement = L"{Call lin_ResetAgitDeco (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nAgitId);
    connection.BindParameter(&nDecoType);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return true;
}

bool CResetAllResidenceDeco::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_ResetAllAgitDeco (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&nAgitId);
        connection.ExecutePrepared(statement);
    }
    {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_UninstallAgitDeco (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&nAgitId);
        connection.ExecutePrepared(statement);
    }
    return true;
}

bool CRenewResidenceDeco::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    CPledgeSP pledge = g_pledgeDb.GetPledge(nPledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        return true;
    }
    CWareHouseSP warehouse = pledge->GetWareHouse();
    if (!warehouse) {
        return true;
    }

    // Current 0x004b06e6 is an in-place selection sort over the raw count.
    // The sender's fixed carrier is 32 records. The shipping receiver does
    // not add a second clamp, so this path keeps that contract.
    for (int index = 0; index < nDecoCount; ++index) {
        int minimumIndex = index;
        std::int64_t minimumPrice = aDecoData[index].nPrice;
        for (int candidate = index + 1; candidate < nDecoCount; ++candidate) {
            if (aDecoData[candidate].nPrice < minimumPrice) {
                minimumIndex = candidate;
                minimumPrice = aDecoData[candidate].nPrice;
            }
        }
        if (minimumIndex != index) {
            std::swap(aDecoData[index], aDecoData[minimumIndex]);
        }
    }

    int paidCount = 0;
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        for (int index = 0; index < nDecoCount; ++index) {
            if (warehouse->DelAdena(aDecoData[index].nPrice)) {
                ++paidCount;
                continue;
            }

            for (int activate = index; activate < nDecoCount; ++activate) {
                DBConn connection(runtime::process::CurrentDatabaseId());
                const ResidenceDecoActivation inactive = ResidenceDecoActivation::Inactive;
                constexpr const wchar_t* statement = L"{Call lin_ActivateAgitDeco (?, ?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&inactive);
                connection.BindParameter(&nAgitId);
                connection.BindParameter(&aDecoData[activate].nType);
                connection.ExecutePrepared(statement);
            }
            break;
        }
    }

    const ResidenceDecoActivation active = ResidenceDecoActivation::Active;
    for (int index = 0; index < paidCount; ++index) {
        CRenewResidenceDecoData& deco = aDecoData[index];
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&deco.nReturn);
        constexpr const wchar_t* statement = L"{Call lin_RenewAgitDeco (?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&nAgitId);
        connection.BindParameter(&deco.nType);
        connection.BindParameter(&deco.nExpire);
        connection.BindParameter(&active);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }

        if (deco.nReturn == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "failed to renew agit deco [type=%d] "
            //     "[expire=%d] [agit=%d]",
            //     static_cast<int>(deco.nType),
            //     deco.nExpire,
            //     nAgitId)
            // Log.Add(
            //     LOG_ERROR,
            //     "[residence] Cannot confirm decoration renewal for residence %d, clan DB ID %d: response code 0. Type %d,
            //     expiry timestamp %d, requested price %lld adena.", nAgitId, nPledgeId, static_cast<int>(deco.nType),
            //     deco.nExpire, static_cast<long long>(deco.nPrice))
            L2_LOG_ERROR(
                Log,
                "[residence] Cannot confirm decoration renewal for residence %d, clan DB ID %d: response code 0; type %d, "
                "expiry timestamp %d, requested price %lld adena.",
                nAgitId, nPledgeId, static_cast<int>(deco.nType), deco.nExpire, static_cast<long long>(deco.nPrice)
            );
        } else {
            WriteRenewResidenceDecoAuditLog(nPledgeId, pledge->GetName(), nAgitId, deco.nPrice, deco.nType, deco.nExpire);
        }
    }
    return true;
}

bool DxSaveSiegeKillDeath::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveKillDeath (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mPledgeDbId);
    connection.BindParameter(&mKillNo);
    connection.BindParameter(&mDeathNo);
    mReturn = connection.ExecutePrepared(statement) ? 1 : 0;
    return true;
}

bool DxSaveNoblessType::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    mReturn = 0;
    connection.Bind(&mReturn);
    constexpr const wchar_t* saveStatement = L"{Call lin_SaveNoblessType (?, ?)}";
    connection.Prepare(saveStatement);
    connection.BindParameter(&mUserDbId);
    connection.BindParameter(&mNoblessType);
    if (connection.ExecutePrepared(saveStatement)) {
        connection.Fetch();
    }
    if (mReturn != 0) {
        return true;
    }

    connection.UnBind();
    connection.Bind(&mReturn);
    constexpr const wchar_t* createStatement = L"{Call lin_NewNobless (?, ?, ?)}";
    connection.Prepare(createStatement);
    connection.BindParameter(&mUserDbId);
    connection.BindParameter(&mNoblessType);
    connection.BindParameter(&mDefaultOlympiadPoint);
    if (connection.ExecutePrepared(createStatement)) {
        connection.Fetch();
    }
    return true;
}

bool DxSaveHeroType::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveHeroType (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mUserDbId);
    connection.BindParameter(&mHeroType);
    mReturn = connection.ExecutePrepared(statement) ? 1 : 0;
    return true;
}

bool DxWriteNoblessAchievement::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WriteNoblessAchievement (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mUserDbId);
    connection.BindParameter(&mType);
    connection.BindParameter(&mTarget);
    connection.BindParameter(&mTime);
    mReturn = connection.ExecutePrepared(statement) ? 1 : 0;
    return true;
}

bool DxReloadOlympiadPoint::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&mOlympiadPoint);
    constexpr const wchar_t* statement = L"{Call lin_ReloadOlympiadPoint (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mUserDbId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return true;
}

bool DxDeclareWar::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeclareWar (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mPledgeDbId);
    connection.BindParameter(&mEnemyPledgeDbId);
    connection.BindParameter(&mDeclareTime);
    mResult = connection.ExecutePrepared(statement) ? 1 : 0;
    return true;
}

bool DxCancelWar::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_CancelWar (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mPledgeDbId);
    connection.BindParameter(&mEnemyPledgeDbId);
    mResult = connection.ExecutePrepared(statement) ? 1 : 0;
    return true;
}

bool DxLoadOlympiad::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&mSeason);
    connection.Bind(&mStep);
    connection.Bind(reinterpret_cast<int*>(&mSeasonStart));
    connection.Bind(&mStartSec);
    connection.Bind(&mBonus1Sec);
    connection.Bind(&mBonus2Sec);
    connection.Bind(&mBonus3Sec);
    connection.Bind(&mBonus4Sec);
    connection.Bind(&mNominateSec);
    constexpr const wchar_t* statement = L"{Call lin_LoadOlympiad}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return true;
}

bool DxLoadPledgeGame::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> pledgeName{};
    std::array<char16_t, 25> characterName{};
    int previousWinnerCharacterDatabaseId = mPrevSeasonWinnerCharacterDbId;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&mSeason);
    connection.Bind(&mStep);
    connection.Bind(reinterpret_cast<int*>(&mSeasonStartTime));
    connection.Bind(&mMaxPrizeCount);
    connection.Bind(&mPrevSeasonWinnerPledgeDbId);
    connection.Bind(pledgeName.data(), 0x32);
    // The target member is unaligned at +0x46. Bind through
    // an aligned host scalar, then copy back only when the target Fetch would
    // have written the packet.
    connection.Bind(&previousWinnerCharacterDatabaseId);
    connection.Bind(characterName.data(), 0x32);
    connection.Bind(&mPrevSeasonWinnerGotItem);
    int season = 0;
    constexpr const wchar_t* statement = L"{Call lin_GetPledgeGame (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&season);
    bool fetched = false;
    if (connection.ExecutePrepared(statement)) {
        fetched = connection.Fetch();
    }
    if (fetched) {
        mPrevSeasonWinnerCharacterDbId = previousWinnerCharacterDatabaseId;
        const std::u16string pledgeNameUtf16 = std::u16string(pledgeName.data());
        const std::u16string characterNameUtf16 = std::u16string(characterName.data());
        CopyTargetMemberName(mPrevSeasonWinnerPledgeName, pledgeNameUtf16.c_str());
        CopyTargetMemberName(mPrevSeasonWinnerCharacterName, characterNameUtf16.c_str());
    }
    g_previousPledgeGameWinnerCharacterDatabaseId = mPrevSeasonWinnerCharacterDbId;
    return true;
}

bool DxSetClass::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetClass (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mUserDbId);
    connection.BindParameter(&mSubJobId);
    connection.BindParameter(&mClass);
    connection.ExecutePrepared(statement);
    return true;
}

bool DxWriteHeroWords::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    const std::wstring heroWords = FixedUtf16ToWide(mHeroWords);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WriteHeroWords (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mUserDbId);
    connection.BindParameter(heroWords.c_str());
    connection.ExecutePrepared(statement);
    return true;
}

bool DxStartOlympiadSeason::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&mReturn);
    constexpr const wchar_t* statement = L"{Call lin_StartOlympiadSeason (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mSeason);
    connection.BindParameter(&mStep);
    connection.BindParameter(reinterpret_cast<const long*>(&mSeasonStart));
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return true;
}

bool DxInitAllOlympiadPoint::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_InitAllOlympiadPoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mSeason);
    connection.BindParameter(&mStep);
    connection.BindParameter(&mInitPoint);
    mReturn = connection.ExecutePrepared(statement) ? 1 : 0;
    return true;
}

bool DxAddAllOlympiadBonusPoint::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddAllOlympiadBonusPoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mSeason);
    connection.BindParameter(&mStep);
    connection.BindParameter(&mBonusPoint);
    mReturn = connection.ExecutePrepared(statement) ? 1 : 0;
    if (mReturn == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "DxAddAllOlympiadBonusPoint CacheDJob fail. [%d]",
        //     0)
        // Log.Add(
        //     LOG_ERROR,
        //     "[olympiad] Cannot add bonus points for season %d, step %d: database execution failed. Bonus %d.",
        //     mSeason, mStep, mBonusPoint)
        L2_LOG_ERROR(
            Log, "[olympiad] Cannot add bonus points for season %d, step %d: database execution failed; bonus %d.", mSeason,
            mStep, mBonusPoint
        );
    }
    return true;
}

bool DxNominateHeroes::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&mNewSeason);
    constexpr const wchar_t* nominateStatement = L"{Call lin_NominateHeroes (?, ?, ?)}";
    connection.Prepare(nominateStatement);
    connection.BindParameter(&mNowSeason);
    connection.BindParameter(&mNewStep);
    connection.BindParameter(&mNewSeasonStart);
    if (connection.ExecutePrepared(nominateStatement)) {
        connection.Fetch();
    }

    connection.UnBind();
    MatchingPoint::MATCHINGPOINT_TYPE matchingPointType = MatchingPoint::MPT_OLYMPIAD;
    constexpr const wchar_t* resetStatement = L"{Call lin_ResetMathingPointByType (?)}";
    connection.Prepare(resetStatement);
    connection.BindParameter(&matchingPointType);
    connection.ExecutePrepared(resetStatement);
    g_userDb.SetReloadMatchingPointAll();
    return true;
}

bool DxSaveOlympiadRecord::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveOlympiadRecord "
                                         L"(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mSeason);
    connection.BindParameter(&mStep);
    connection.BindParameter(&mWinnerId);
    connection.BindParameter(&mWinnerPoint);
    connection.BindParameter(&mLoserId);
    connection.BindParameter(&mLoserPoint);
    connection.BindParameter(&mTime);
    connection.BindParameter(&mResultType);
    connection.BindParameter(&mElapsedTime);
    connection.BindParameter(&mGameRuleType);
    connection.ExecutePrepared(statement);
    return true;
}

bool DxReloadHeroRank::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    int heroClass = 0;
    int heroId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&heroClass);
    connection.Bind(&heroId);
    constexpr const wchar_t* statement = L"{Call lin_GetAllHeroes}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        return true;
    }

    mHeroCount = 0;
    while (connection.Fetch()) {
        // Current 0x004ab792 trusts lin_GetAllHeroes to return no more than
        // the 36 class slots carried by the packet and performs no clamp.
        mHeroClassList[mHeroCount] = heroClass;
        mHeroIdList[mHeroCount] = heroId;
        ++mHeroCount;
    }
    return true;
}

bool DxReloadHeroById::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> charName = MakeUtf16ResultBuffer<25>(mCharName);
    std::array<char16_t, 25> pledgeName = MakeUtf16ResultBuffer<25>(mPledgeName);
    std::array<char16_t, 25> allianceName = MakeUtf16ResultBuffer<25>(mAllianceName);
    // The target binds only 0x80 bytes although the carrier reserves 129
    // UTF-16 units. Bytes beyond the first 64 units remain untouched.
    std::array<char16_t, 64> words = MakeUtf16ResultBuffer<64>(mWords);
    int mainClass = mMainClass;
    int heroType = mHeroType;
    int pledgeId = mPledgeId;
    int pledgeCrestId = mPledgeCrestId;
    int allianceId = mAllianceId;
    int allianceCrestId = mAllianceCrestId;
    int winCount = mWinCount;
    int historyOpen = mHistoryOpen;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(charName.data(), 0x32);
    connection.Bind(&mainClass);
    connection.Bind(&heroType);
    connection.Bind(&pledgeId);
    connection.Bind(pledgeName.data(), 0x32);
    connection.Bind(&pledgeCrestId);
    connection.Bind(&allianceId);
    connection.Bind(allianceName.data(), 0x32);
    connection.Bind(&allianceCrestId);
    connection.Bind(&winCount);
    connection.Bind(words.data(), 0x80);
    connection.Bind(&historyOpen);
    constexpr const wchar_t* statement = L"{Call lin_GetHeroById (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mCharId);

    bool fetched = false;
    if (connection.ExecutePrepared(statement)) {
        fetched = connection.Fetch();
    }
    mReturn = fetched;
    if (!fetched) {
        return true;
    }

    CopyUtf16Result(mCharName, charName, 0x32);
    CopyUtf16Result(mPledgeName, pledgeName, 0x32);
    CopyUtf16Result(mAllianceName, allianceName, 0x32);
    CopyUtf16Result(mWords, words, 0x80);
    mMainClass = mainClass;
    mHeroType = heroType;
    mPledgeId = pledgeId;
    mPledgeCrestId = pledgeCrestId;
    mAllianceId = allianceId;
    mAllianceCrestId = allianceCrestId;
    mWinCount = winCount;
    mHistoryOpen = historyOpen;
    return true;
}

bool DxDeletePreviousOlympiadPoint::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    mReturn = 0;
    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_DeletePreviousOlympiadPoint (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mUserDbId);
    connection.BindParameter(&mPreviousOlympiadPoint);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        mReturn = result;
    }
    return true;
}

bool DxSaveSeasonStartTime::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    alignas(long) const std::int32_t seasonStartTime = mSeasonStartTime;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveSeasonStartTime (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mSeason);
    connection.BindParameter(reinterpret_cast<const long*>(&seasonStartTime));
    connection.ExecutePrepared(statement);
    return true;
}

bool DxSaveOlympiadTerm::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveOlympiadTerm (?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mSeason);
    connection.BindParameter(&mStartSec);
    connection.BindParameter(&mBonus1Sec);
    connection.BindParameter(&mBonus2Sec);
    connection.BindParameter(&mBonus3Sec);
    connection.BindParameter(&mBonus4Sec);
    connection.BindParameter(&mNominateSec);
    connection.ExecutePrepared(statement);
    return true;
}

bool DxSetHistoryOpen::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetHistoryOpen (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mCharId);
    connection.BindParameter(&mHistoryOpen);
    connection.ExecutePrepared(statement);
    return true;
}

bool DxDeleteOlympiadTradePoint::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    mReturn = 0;
    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_DeleteOlympiadTradePoint (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mUserDbId);
    connection.BindParameter(&mOlympiadTradePoint);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        mReturn = result;
    }
    return true;
}

bool DxAddOlympiadTradePoint::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    mReturn = 0;
    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_AddOlympiadTradePoint (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mUserDbId);
    connection.BindParameter(&mOlympiadTradePoint);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        mReturn = result;
    }
    return true;
}

bool CDxSetFortressFacility::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetFortressFacility (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nFortressId);
    connection.BindParameter(&m_nFacilityType);
    connection.BindParameter(&m_nFacilityLevel);
    if (!connection.ExecutePrepared(statement)) {
        // Current 0x004b179a leaves the incoming field unchanged on success.
        m_nReturn = 0;
    }
    return true;
}

bool CDxResetFortressFacility::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetFortressFacility (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nFortressId);
    connection.ExecutePrepared(statement);
    return true;
}

bool DxAddOlympiadPoint::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddOlympiadPoint (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mUserId);
    connection.BindParameter(&mPoint);
    connection.ExecutePrepared(statement);
    return true;
}

bool CApplyInstantAgit::DoCacheDJob() {
    // Current Cached.exe 0x004b3a4c.
    L2_GUARD_FUNCTION();

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeDBId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        nReason = 3;
        return true;
    }

    CWareHouseSP warehouse = pledge->GetWareHouse();
    if (!warehouse) {
        nReason = 6;
        return true;
    }

    bool paid;
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        paid = warehouse->DelAdena(rentalFee);
    }
    if (!paid) {
        nReason = 6;
        return true;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&nReturn);
    constexpr const wchar_t* statement = L"{Call lin_ApplyInstantAgit (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeDBId);
    connection.BindParameter(&instantAgitId);
    connection.BindParameter(&rentalFee);
    connection.BindParameter(&applyTime);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]already apply Instant Agit"
        //     "[agitId:%d][pledgeId:%d] ",
        //     "..\\Shared\\DXPacket.cpp",
        //     0x1159,
        //     instantAgitId,
        //     pledgeDBId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[clan hall] Application for clan DB ID %d to instanced hall %d failed during database execution. Return of
        //     the %d-adena fee will be attempted.", pledgeDBId, instantAgitId, rentalFee)
        L2_LOG_ERROR(
            Log,
            "[clan hall] Application for clan DB ID %d to instanced hall %d failed during database execution; return of the "
            "%d-adena fee will be attempted.",
            pledgeDBId, instantAgitId, rentalFee
        );
        {
            L2_WRITE_GUARD(*warehouse.operator->());
            warehouse->AddAdena(rentalFee);
        }
        nReason = 2;
        return true;
    }

    // connection.Fetch()
    const bool resultFetched = connection.Fetch();
    // Log.Add(
    //     LOG_ERROR,
    //     "[NO_ERROR][InstantAgitApply] Success! "
    //     "PledgeDBID[%d] InstantAgitId[%d]",
    //     pledgeDBId,
    //     instantAgitId)
    if (!resultFetched) {
        // Log.Add(LOG_ERROR,
        //     "[clan hall] Application for clan DB ID %d to instanced hall %d returned no database row. Fee %d adena was
        //     processed without a confirmed response.", pledgeDBId, instantAgitId, rentalFee)
        L2_LOG_ERROR(
            Log,
            "[clan hall] Application for clan DB ID %d to instanced hall %d returned no database row; fee %d adena was "
            "processed without a confirmed response.",
            pledgeDBId, instantAgitId, rentalFee
        );
    } else {
        // Log.Add(nReturn == 0 ? LOG_WARN : LOG_NORMAL,
        //     "[clan hall] Application for clan DB ID %d to instanced hall %d returned response code %d. Fee %d adena.",
        //     pledgeDBId, instantAgitId, nReturn, rentalFee)
        L2_LOG(
            Log, nReturn == 0 ? LOG_WARN : LOG_NORMAL,
            "[clan hall] Application for clan DB ID %d to instanced hall %d returned response code %d; fee %d adena.",
            pledgeDBId, instantAgitId, nReturn, rentalFee
        );
    }
    return true;
}

bool CAddInstantAgitDeco::DoCacheDJob() {
    // Current Cached.exe 0x004b3e44. The packet's adenaAmount is not read by
    // this shipping body.
    L2_GUARD_FUNCTION();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeDBId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        return true;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&nReturn);
    constexpr const wchar_t* statement = L"{Call lin_AddInstantAgitDeco (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&instantAgitId);
    connection.BindParameter(&pledgeDBId);
    connection.BindParameter(&agitDecoId);
    connection.BindParameter(&removeAgitDecoId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return true;
}

bool CReturnInstantAgit::DoCacheDJob() {
    // Current Cached.exe 0x004b3fc0 sets the response from execution success.
    // the procedure has no result-column fetch.
    L2_GUARD_FUNCTION();
    nReturn = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ReturnInstantAgit (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeDBId);
    if (connection.ExecutePrepared(statement)) {
        nReturn = 1;
    }
    return true;
}

bool DxResetOlympiadPoint::DoCacheDJob() {
    // Current Cached.exe 0x004b3384 binds user ID before the point even
    // though the packed carrier stores mPoint first.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetOlympiadPointById (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mUserId);
    connection.BindParameter(&mPoint);
    connection.ExecutePrepared(statement);
    return true;
}

bool DxFishingChampionShip::DoCacheDJob() {
    // Current Cached.exe 0x004ab7d0 contains only the generated guard and
    // returns true without reading or changing the 0x12a-byte carrier.
    L2_GUARD_FUNCTION();
    return true;
}

bool DxMatchingPoint::DoCacheDJob() {
    // Current Cached.exe 0x004ae0c0 permits the sentinel type value 2 here.
    // CUser's exact accessor/mutator then rejects it independently.
    L2_GUARD_FUNCTION();
    const int type = static_cast<int>(m_cType);
    if (m_nUserSID == 0 || m_nUserDBID == 0 || type < 0 || type >= 3) {
        return false;
    }

    CUserSP user = g_userDb.GetUser(static_cast<int>(m_nUserDBID), true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        return false;
    }

    if (!m_isSave) {
        m_nMatchingPointDiff = user->GetMatchingPoint(m_cType);
    } else {
        std::int64_t previousPoint = 0;
        if (user->SetMatchingPoint(m_cType, m_nMatchingPointDiff, previousPoint)) {
            user->SaveMatchingPoint();
        }
    }
    return true;
}

bool CSetResidenceDecoNpc::DoCacheDJob() {
    // Current Cached.exe 0x004b0bcc. Result values are the shipping protocol:
    // -1 pledge, -2 warehouse, -3 adena, -4 item, -5 database execution.
    L2_GUARD_FUNCTION();
    m_nReturn = 0;

    CPledgeSP pledge = g_pledgeDb.GetPledge(m_nPledgeDBID, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        m_nReturn = -1;
        // Log.Add(
        //     LOG_ERROR,
        //     "[DECONPC] No pledge at %s : %d",
        //     "CSetResidenceDecoNpc::DoCacheDJob",
        //     m_nPledgeDBID)
        // Log.Add(
        //     LOG_ERROR,
        //     "[decoration NPCs] Cannot set decoration %d in residence %d, slot %d: clan DB ID %d was not found.",
        //     m_nDecoCID, m_nAgitCID, m_nAgitSlot, m_nPledgeDBID)
        L2_LOG_ERROR(
            Log, "[decoration NPCs] Cannot set decoration %d in residence %d, slot %d: clan DB ID %d was not found.",
            m_nDecoCID, m_nAgitCID, m_nAgitSlot, m_nPledgeDBID
        );
        return true;
    }

    CWareHouseSP warehouse = pledge->GetWareHouse();
    if (!warehouse) {
        m_nReturn = -2;
        // Log.Add(
        //     LOG_ERROR,
        //     "[DECONPC] No pledge warehouse at %s : %d",
        //     "CSetResidenceDecoNpc::DoCacheDJob",
        //     m_nPledgeDBID)
        // Log.Add(
        //     LOG_ERROR,
        //     "[decoration NPCs] Cannot set decoration %d in residence %d, slot %d: warehouse for clan DB ID %d is
        //     unavailable.", m_nDecoCID, m_nAgitCID, m_nAgitSlot, m_nPledgeDBID)
        L2_LOG_ERROR(
            Log,
            "[decoration NPCs] Cannot set decoration %d in residence %d, slot %d: warehouse for clan DB ID %d is "
            "unavailable.",
            m_nDecoCID, m_nAgitCID, m_nAgitSlot, m_nPledgeDBID
        );
        return true;
    }

    std::int64_t deletedAdena = 0;
    int deletedItemCount = 0;
    {
        L2_WRITE_GUARD(*warehouse.operator->());

        if (m_nCost > 0) {
            if (!warehouse->DelAdena(m_nCost)) {
                m_nReturn = -3;
            } else {
                deletedAdena = m_nCost;
            }
        }

        if (m_nReturn == 0 && m_nItemCID > 0 && m_nItemCount > 0) {
            unsigned int deletedItemId = 0;
            std::int64_t remainingItemCount = 0;
            const CWareHouse::DeleteResult result =
                warehouse->_DeleteAssetOrStackableItem(m_nItemCID, m_nItemCount, deletedItemId, remainingItemCount);
            if (static_cast<int>(result) != 0) {
                m_nReturn = -4;
            } else {
                // Current 0x004b0ea6 truncates the successful 64-bit request to
                // its low signed 32 bits for logging and rollback.
                deletedItemCount = runtime::Int32FromBits(m_nItemCount);
            }
        }

        if (m_nReturn == 0) {
            bool executed = false;
            {
                DBConn connection(runtime::process::CurrentDatabaseId());
                connection.Bind(&m_nReturn);
                constexpr const wchar_t* statement = L"{Call lin_AgitDecoNpcSet (?, ?, ?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&m_nAgitCID);
                connection.BindParameter(&m_nAgitSlot);
                connection.BindParameter(&m_nDecoCID);
                connection.BindParameter(&m_nExpireTime);
                executed = connection.ExecutePrepared(statement);
                if (executed) {
                    connection.Fetch();
                    if (m_nReturn == 0) {
                        if (deletedAdena > 0) {
                            WriteDecoNpcCostLog(
                                0x124, m_nPledgeDBID, pledge->GetName(), m_nAgitCID, m_nAgitSlot, 57,
                                runtime::Int32FromBits(deletedAdena), m_nExpireTime
                            );
                        }
                        if (deletedItemCount > 0) {
                            // The current residence body at 0x004b11a0 passes
                            // deletedAdena here, unlike the instant-agit twin.
                            WriteDecoNpcCostLog(
                                0x124, m_nPledgeDBID, pledge->GetName(), m_nAgitCID, m_nAgitSlot,
                                static_cast<std::uint32_t>(m_nItemCID), runtime::Int32FromBits(deletedAdena),
                                m_nExpireTime
                            );
                        }
                    }
                } else {
                    m_nReturn = -5;
                }
            }

            if (executed) {
                return true;
            }
        }
    }
    // Shipping rollback releases the original lock and then reacquires it at
    // a second source line before restoring whichever debits succeeded.
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        if (deletedAdena > 0 && !warehouse->AddAdena(deletedAdena)) {
            // Noise: Unreachable with current AddAdena: this branch requires a positive amount, for which the helper always
            // returns true. That result does not verify item restoration. Log.Add(
            //     LOG_ERROR,
            //     "[DECONPC] Deleted adenas but failed at %s : %d, %d",
            //     "CSetResidenceDecoNpc::DoCacheDJob",
            //     m_nPledgeDBID,
            //     runtime::Int32FromBits(deletedAdena))
        }
        if (deletedItemCount > 0) {
            CItemSP restored = warehouse->AddAssetItem(m_nItemCID, deletedItemCount);
            if (!restored) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[DECONPC] Deleted items but failed at %s : %d, %d, %d",
                //     "CSetResidenceDecoNpc::DoCacheDJob",
                //     m_nPledgeDBID,
                //     m_nItemCID,
                //     deletedItemCount)
                // Log.Add(
                //     LOG_ERROR,
                //     "[decoration NPCs] Rollback could not restore %d items of class %d to clan DB ID %d after setting a
                //     decoration in residence %d, slot %d failed (response %d).", deletedItemCount, m_nItemCID,
                //     m_nPledgeDBID, m_nAgitCID, m_nAgitSlot, m_nReturn)
                L2_LOG_ERROR(
                    Log,
                    "[decoration NPCs] Rollback could not restore %d items of class %d to clan DB ID %d after setting a "
                    "decoration in residence %d, slot %d failed (response %d).",
                    deletedItemCount, m_nItemCID, m_nPledgeDBID, m_nAgitCID, m_nAgitSlot, m_nReturn
                );
            }
        }
    }
    return true;
}

bool CResetResidenceDecoNpc::DoCacheDJob() {
    // Current Cached.exe 0x004b125c.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_nReturn);
    constexpr const wchar_t* statement = L"{Call lin_AgitDecoNpcReset (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nAgitCID);
    connection.BindParameter(&m_nAgitSlot);
    if (!connection.ExecutePrepared(statement)) {
        m_nReturn = -1;
    } else {
        connection.Fetch();
    }
    return true;
}

bool CResetAllResidenceDecoNpc::DoCacheDJob() {
    // Current Cached.exe 0x004b1554 leaves the incoming response untouched on
    // success and writes -1 only when statement execution fails.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AgitDecoNpcResetAll (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nAgitCID);
    if (!connection.ExecutePrepared(statement)) {
        m_nReturn = -1;
    }
    return true;
}

bool CExpireResidenceDecoNpc::DoCacheDJob() {
    // Current Cached.exe 0x004b13c4 trusts m_nCount instead of
    // adding a receiver-side clamp to the five-entry carrier.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AgitDecoNpcReset (?, ?)}";
    for (int index = 0; index < m_nCount; ++index) {
        connection.UnBind();
        connection.Bind(&m_aReturns[index]);
        connection.Prepare(statement);
        connection.BindParameter(&m_nAgitCID);
        connection.BindParameter(&m_aAgitSlots[index]);
        if (!connection.ExecutePrepared(statement)) {
            m_aReturns[index] = -1;
        } else {
            connection.Fetch();
        }
    }
    return true;
}

bool CSetInstantAgitDecoNpc::DoCacheDJob() {
    // Current Cached.exe 0x004b40e8. This is a distinct shipping body from
    // CSetResidenceDecoNpc, including its source lines and log spellings.
    L2_GUARD_FUNCTION();
    m_nReturn = 0;

    CPledgeSP pledge = g_pledgeDb.GetPledge(m_nPledgeDBID, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        m_nReturn = -1;
        // Log.Add(
        //     LOG_ERROR,
        //     "[DECONPC]No pledge at %s : %d",
        //     "CSetInstantAgitDecoNpc::DoCacheDJob",
        //     m_nPledgeDBID)
        // Log.Add(
        //     LOG_ERROR,
        //     "[decoration NPCs] Cannot set decoration %d in instanced clan hall %d, slot %d: clan DB ID %d was not found.",
        //     m_nDecoCID, m_nInstantAgitCID, m_nAgitSlot, m_nPledgeDBID)
        L2_LOG_ERROR(
            Log,
            "[decoration NPCs] Cannot set decoration %d in instanced clan hall %d, slot %d: clan DB ID %d was not found.",
            m_nDecoCID, m_nInstantAgitCID, m_nAgitSlot, m_nPledgeDBID
        );
        return true;
    }

    CWareHouseSP warehouse = pledge->GetWareHouse();
    if (!warehouse) {
        m_nReturn = -2;
        // Log.Add(
        //     LOG_ERROR,
        //     "[DECONPC]No pledge warehouse at %s : %d",
        //     "CSetInstantAgitDecoNpc::DoCacheDJob",
        //     m_nPledgeDBID)
        // Log.Add(
        //     LOG_ERROR,
        //     "[decoration NPCs] Cannot set decoration %d in instanced clan hall %d, slot %d: warehouse for clan DB ID %d is
        //     unavailable.", m_nDecoCID, m_nInstantAgitCID, m_nAgitSlot, m_nPledgeDBID)
        L2_LOG_ERROR(
            Log,
            "[decoration NPCs] Cannot set decoration %d in instanced clan hall %d, slot %d: warehouse for clan DB ID %d is "
            "unavailable.",
            m_nDecoCID, m_nInstantAgitCID, m_nAgitSlot, m_nPledgeDBID
        );
        return true;
    }

    std::int64_t deletedAdena = 0;
    int deletedItemCount = 0;
    {
        L2_WRITE_GUARD(*warehouse.operator->());

        if (m_nCost > 0) {
            if (!warehouse->DelAdena(m_nCost)) {
                m_nReturn = -3;
            } else {
                deletedAdena = m_nCost;
            }
        }

        if (m_nReturn == 0 && m_nItemCID > 0 && m_nItemCount > 0) {
            unsigned int deletedItemId = 0;
            std::int64_t remainingItemCount = 0;
            const CWareHouse::DeleteResult result =
                warehouse->_DeleteAssetOrStackableItem(m_nItemCID, m_nItemCount, deletedItemId, remainingItemCount);
            if (static_cast<int>(result) != 0) {
                m_nReturn = -4;
            } else {
                deletedItemCount = runtime::Int32FromBits(m_nItemCount);
            }
        }

        if (m_nReturn == 0) {
            bool executed = false;
            {
                DBConn connection(runtime::process::CurrentDatabaseId());
                connection.Bind(&m_nReturn);
                constexpr const wchar_t* statement = L"{Call lin_InstantAgitDecoNpcSet (?, ?, ?, ?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&m_nInstantAgitCID);
                connection.BindParameter(&m_nPledgeDBID);
                connection.BindParameter(&m_nAgitSlot);
                connection.BindParameter(&m_nDecoCID);
                connection.BindParameter(&m_nExpireTime);
                executed = connection.ExecutePrepared(statement);
                if (executed) {
                    connection.Fetch();
                    if (m_nReturn == 0) {
                        if (deletedAdena > 0) {
                            WriteDecoNpcCostLog(
                                0x126, m_nPledgeDBID, pledge->GetName(), m_nInstantAgitCID, m_nAgitSlot, 57,
                                runtime::Int32FromBits(deletedAdena), m_nExpireTime
                            );
                        }
                        if (deletedItemCount > 0) {
                            WriteDecoNpcCostLog(
                                0x126, m_nPledgeDBID, pledge->GetName(), m_nInstantAgitCID, m_nAgitSlot,
                                static_cast<std::uint32_t>(m_nItemCID), deletedItemCount, m_nExpireTime
                            );
                        }
                    }
                } else {
                    m_nReturn = -5;
                }
            }

            if (executed) {
                return true;
            }
        }
    }
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        if (deletedAdena > 0 && !warehouse->AddAdena(deletedAdena)) {
            // Noise: Unreachable with current AddAdena: this branch requires a positive amount, for which the helper always
            // returns true. That result does not verify item restoration. Log.Add(
            //     LOG_ERROR,
            //     "[DECONPC]Deleted adenas but failed at %s : %d, %d",
            //     "CSetInstantAgitDecoNpc::DoCacheDJob",
            //     m_nPledgeDBID,
            //     runtime::Int32FromBits(deletedAdena))
        }
        if (deletedItemCount > 0) {
            CItemSP restored = warehouse->AddAssetItem(m_nItemCID, deletedItemCount);
            if (!restored) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[DECONPC]Deleted items but failed at %s : %d, %d, %d",
                //     "CSetInstantAgitDecoNpc::DoCacheDJob",
                //     m_nPledgeDBID,
                //     m_nItemCID,
                //     deletedItemCount)
                // Log.Add(
                //     LOG_ERROR,
                //     "[decoration NPCs] Rollback could not restore %d items of class %d to clan DB ID %d after setting a
                //     decoration in instanced clan hall %d, slot %d failed (response %d).", deletedItemCount, m_nItemCID,
                //     m_nPledgeDBID, m_nInstantAgitCID, m_nAgitSlot, m_nReturn)
                L2_LOG_ERROR(
                    Log,
                    "[decoration NPCs] Rollback could not restore %d items of class %d to clan DB ID %d after setting a "
                    "decoration in instanced clan hall %d, slot %d failed (response %d).",
                    deletedItemCount, m_nItemCID, m_nPledgeDBID, m_nInstantAgitCID, m_nAgitSlot, m_nReturn
                );
            }
        }
    }
    return true;
}

bool CResetInstantAgitDecoNpc::DoCacheDJob() {
    // Current Cached.exe 0x004b477c.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_nReturn);
    constexpr const wchar_t* statement = L"{Call lin_InstantAgitDecoNpcReset (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nInstantAgitCID);
    connection.BindParameter(&m_nPledgeDBID);
    connection.BindParameter(&m_nAgitSlot);
    if (!connection.ExecutePrepared(statement)) {
        m_nReturn = -1;
    } else {
        connection.Fetch();
    }
    return true;
}

bool CExpireInstantAgitDecoNpc::DoCacheDJob() {
    // Current Cached.exe 0x004b48f8 keeps the raw m_nCount loop just like
    // the residence variant.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_InstantAgitDecoNpcReset (?, ?, ?)}";
    for (int index = 0; index < m_nCount; ++index) {
        connection.UnBind();
        connection.Bind(&m_aReturns[index]);
        connection.Prepare(statement);
        connection.BindParameter(&m_nInstantAgitCID);
        connection.BindParameter(&m_nPledgeDBID);
        connection.BindParameter(&m_aAgitSlots[index]);
        if (!connection.ExecutePrepared(statement)) {
            m_aReturns[index] = -1;
        } else {
            connection.Fetch();
        }
    }
    return true;
}

bool CDX_WOLYMPIAD_CHECK_PEER::DoCacheDJob() {
    // Current Cached.exe 0x004aee7c scans the configured DBList vector.
    L2_GUARD_FUNCTION();
    m_bIsValid = CSqlPoolManager::Instance().HasDatabaseId(m_nWorldID);
    return true;
}

bool CDX_WOLYMPIAD_LOAD::DoCacheDJob() {
    // Current Cached.exe 0x004ab850.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_nSeason);
    connection.Bind(&m_nStep);
    connection.Bind(&m_tSeasonStart);
    connection.Bind(&m_nStartSec);
    connection.Bind(&m_nBonus1Sec);
    connection.Bind(&m_nBonus2Sec);
    connection.Bind(&m_nBonus3Sec);
    connection.Bind(&m_nBonus4Sec);
    connection.Bind(&m_nNominateSec);
    connection.Bind(&m_idLegendary);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadLoad}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return true;
}

bool CDX_WOLYMPIAD_GET_TOP10::DoCacheDJob() {
    // Current Cached.exe 0x004b4aa0.
    L2_GUARD_FUNCTION();
    m_nCount = 0;
    std::memset(m_Ranks, 0, sizeof(m_Ranks));

    int worldId = 0;
    int userDatabaseId = 0;
    int point = 0;
    int matchCount = 0;
    int winCount = 0;
    std::array<char16_t, 25> characterName{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&worldId);
    connection.Bind(&userDatabaseId);
    connection.Bind(characterName.data(), 0x32);
    connection.Bind(&point);
    connection.Bind(&matchCount);
    connection.Bind(&winCount);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadGetTop10 (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nHeroClassType);
    connection.BindParameter(&m_nSeason);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    while (connection.Fetch() && m_nCount < 10) {
        UserRank& rank = m_Ranks[m_nCount];
        rank.nWorldID = worldId;
        rank.nUserDBID = userDatabaseId;
        rank.nPoint = point;
        rank.nMatchCount = matchCount;
        rank.nWinCount = winCount;
        CopyUtf16Result(rank.sCharName, characterName, 0x32);
        ++m_nCount;
    }
    return true;
}

bool CDX_WOLYMPIAD_GET_PLAYER_COUNT::DoCacheDJob() {
    // Current Cached.exe 0x004b4c90.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_nCount);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadGetPlayerCount (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nSeason);
    return connection.ExecutePrepared(statement) && connection.Fetch();
}

bool CDX_WOLYMPIAD_ADD_USER::DoCacheDJob() {
    // Current Cached.exe 0x004b5344. m_eNextAction is returned to L2Server
    // unchanged and is not consumed by the CacheD half of this job.
    L2_GUARD_FUNCTION();
    m_nReturn = 0;
    const std::wstring characterName = FixedUtf16ToWide(m_szCharName);
    const std::wstring pledgeName = FixedUtf16ToWide(m_szPledgeName);
    const std::wstring allianceName = FixedUtf16ToWide(m_szAllianceName);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadAddUser (?,?,?,?,? ,?,?,?,?,? ,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nWorldID);
    connection.BindParameter(&m_nUserDBID);
    connection.BindParameter(&m_nClass);
    connection.BindParameter(&m_nHeroClassType);
    connection.BindParameter(characterName.c_str());
    connection.BindParameter(&m_nPledgeDBID);
    connection.BindParameter(pledgeName.c_str());
    connection.BindParameter(&m_nPledgeCrestID);
    connection.BindParameter(&m_nAllianceDBID);
    connection.BindParameter(allianceName.c_str());
    connection.BindParameter(&m_nAllianceCrestID);
    connection.BindParameter(&m_nDefaultOlympiadPoint);
    if (connection.ExecutePrepared(statement)) {
        m_nReturn = 1;
    } else {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD] qsWOlympiadAddUser failed: %d, %d",
        //     m_nWorldID,
        //     m_nUserDBID)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[world olympiad] Cannot add character DB ID %d in world %d: database execution failed.",
        //     m_nUserDBID, m_nWorldID)
        L2_LOG_ERROR(
            Log, u"[world olympiad] Cannot add character DB ID %d in world %d: database execution failed.", m_nUserDBID,
            m_nWorldID
        );
    }
    return true;
}

bool CDX_WOLYMPIAD_SAVE_TERM::DoCacheDJob() {
    // Current Cached.exe 0x004b4de0.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadSaveTerm (?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nSeason);
    connection.BindParameter(&m_nStartSec);
    connection.BindParameter(&m_nBonus1Sec);
    connection.BindParameter(&m_nBonus2Sec);
    connection.BindParameter(&m_nBonus3Sec);
    connection.BindParameter(&m_nBonus4Sec);
    connection.BindParameter(&m_nNominateSec);
    connection.ExecutePrepared(statement);
    return true;
}

bool CDX_WOLYMPIAD_NOMINATE_HEROES::DoCacheDJob() {
    // Current Cached.exe 0x004b4efc.
    L2_GUARD_FUNCTION();
    int newSeason = m_nNewSeason;
    int legendaryId = m_idLegendary;
    const unsigned int nominateHeroes = m_bNominateHeroes ? 1U : 0U;
    const long newSeasonStart = m_tNewSeasonStart;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&newSeason);
    connection.Bind(&legendaryId);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadNominateHeroes (?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nNowSeason);
    connection.BindParameter(&m_nNewStep);
    // The source member is Win64 long (four bytes). DBConn's long overload
    // keeps its distinct target SQL binding on LP64 hosts.
    connection.BindParameter(&newSeasonStart);
    connection.BindParameter(&nominateHeroes);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        m_nNewSeason = newSeason;
        m_idLegendary = legendaryId;
    }
    return true;
}

bool CDX_WOLYMPIAD_RELOAD_HERO_RANK::DoCacheDJob() {
    // Current Cached.exe 0x004aba00 does not clamp the result
    // count to the three 36-element target arrays.
    L2_GUARD_FUNCTION();
    int heroClass = 0;
    int worldId = 0;
    int heroId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&heroClass);
    connection.Bind(&worldId);
    connection.Bind(&heroId);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadLoadAllHeroes}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        return true;
    }

    m_nHeroCount = 0;
    while (connection.Fetch()) {
        m_nHeroClassList[m_nHeroCount] = heroClass;
        m_nHeroWorldIdList[m_nHeroCount] = worldId;
        m_nHeroIdList[m_nHeroCount] = heroId;
        ++m_nHeroCount;
    }
    return true;
}

bool CDX_WOLYMPIAD_SAVE_MATCH_COUNT::DoCacheDJob() {
    // Current Cached.exe 0x004b5044 is one of the few jobs whose SQL result
    // directly controls whether the DX reply is sent.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadSaveMatchCount (?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nWorldID);
    connection.BindParameter(&m_nUserDBID);
    connection.BindParameter(&m_nSeason);
    connection.BindParameter(&m_nStep);
    connection.BindParameter(&m_nGameRule);
    connection.BindParameter(&m_nMatchCount);
    return connection.ExecutePrepared(statement);
}

bool CDX_WOLYMPIAD_LOG_PENALTY::DoCacheDJob() {
    // Current Cached.exe 0x004ae25c. m_nGameRule is present in the packet but
    // not read by the shipping body.
    L2_GUARD_FUNCTION();
    CUserSP user = g_userDb.GetUser(m_nUserDBID, false, false, m_nWorldID);
    if (!user) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD]Cannot find user: %d, %d",
        //     m_nWorldID,
        //     m_nUserDBID)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[world olympiad] Penalty audit record omitted: character DB ID %d in world %d was not found. Field %d,
        //     penalty type %d.", m_nUserDBID, m_nWorldID, m_nFieldID, m_nPenaltyType)
        L2_LOG_ERROR(
            Log,
            u"[world olympiad] Penalty audit record omitted: character DB ID %d in world %d was not found; field %d, "
            u"penalty type %d.",
            m_nUserDBID, m_nWorldID, m_nFieldID, m_nPenaltyType
        );
        return true;
    }

    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fd6, m_nUserDBID, user->GetAccountID(), 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, user->GetClass(),
        user->GetLevel(), m_nPoint, m_nTotalPoint, m_nFieldID, m_nPenaltyType, 0, 0, 0, m_nWorldID, characterName.c_str(),
        accountName.c_str(), nullptr, nullptr, 0
    );
    return true;
}

bool CDX_WOLYMPIAD_RELOAD_USER::DoCacheDJob() {
    // Current Cached.exe 0x004b515c.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_nHeroType);
    connection.Bind(&m_nWinCount);
    connection.Bind(&m_nPreviousPoint);
    connection.Bind(&m_nOlympiadPoint);
    connection.Bind(&m_nMatchCount);
    connection.Bind(&m_nOlympiadWinCount);
    connection.Bind(&m_nOlympiadLoseCount);
    connection.Bind(&m_nHistoryOpen);
    connection.Bind(&m_nOlympiadTradePoint);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadGetUserById (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nWorldID);
    connection.BindParameter(&m_nUserDBID);
    return connection.ExecutePrepared(statement) && connection.Fetch();
}

bool CDX_WOLYMPIAD_INIT_ALL_POINT::DoCacheDJob() {
    // Current Cached.exe 0x004b5504.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadInitAllPoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nSeason);
    connection.BindParameter(&m_nStep);
    connection.BindParameter(&m_nInitPoint);
    m_nReturn = connection.ExecutePrepared(statement) ? 1 : 0;
    return true;
}

bool CDX_WOLYMPIAD_ADD_ALL_POINT::DoCacheDJob() {
    // Current Cached.exe 0x004b5668.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadAddAllPoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nSeason);
    connection.BindParameter(&m_nStep);
    connection.BindParameter(&m_nBonusPoint);
    m_nReturn = connection.ExecutePrepared(statement) ? 1 : 0;
    return true;
}

bool CDX_WOLYMPIAD_SAVE_RECORD::DoCacheDJob() {
    // Current Cached.exe 0x004b57cc. The helper's stack shuffling obscures
    // this order in the decompiler, but its 16 bindings follow the packet and
    // procedure order below exactly.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadSaveRecord "
                                         L"(?,?,?,?,?, ?,?,?,?,?, ?,?,?,?,? ,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nSeason);
    connection.BindParameter(&m_nStep);
    connection.BindParameter(&m_nWinnerWorldID);
    connection.BindParameter(&m_nWinnerDBID);
    connection.BindParameter(&m_nLoserWorldID);
    connection.BindParameter(&m_nLoserDBID);
    connection.BindParameter(&m_nWinnerPoint);
    connection.BindParameter(&m_nWinnerItemType);
    connection.BindParameter(&m_nWinnerItemAmount);
    connection.BindParameter(&m_nLoserPoint);
    connection.BindParameter(&m_nLoserItemType);
    connection.BindParameter(&m_nLoserItemAmount);
    connection.BindParameter(&m_nTime);
    connection.BindParameter(&m_nResultType);
    connection.BindParameter(&m_nElapsedTime);
    connection.BindParameter(&m_nGameRuleType);
    connection.ExecutePrepared(statement);
    return true;
}

bool CDX_WOLYMPIAD_SAVE_SEASON_START_TIME::DoCacheDJob() {
    // Current Cached.exe 0x004b5974.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const long seasonStartTime = m_tSeasonStartTime;
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadSaveSeasonStartTime (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nSeason);
    connection.BindParameter(&seasonStartTime);
    connection.ExecutePrepared(statement);
    return true;
}

bool CDX_WOLYMPIAD_RELOAD_HERO_BY_ID::DoCacheDJob() {
    // Current Cached.exe 0x004b5aa4 binds only 0x80 target bytes for words,
    // despite the 129-unit carrier kept by the PDB.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 25> characterName{};
    std::array<char16_t, 25> pledgeName{};
    std::array<char16_t, 25> allianceName{};
    std::array<char16_t, 64> words{};
    int heroClassType = m_nHeroClassType;
    int heroType = m_nHeroType;
    int pledgeDatabaseId = m_nPledgeDBID;
    int pledgeCrestId = m_nPledgeCrestID;
    int allianceDatabaseId = m_nAllianceDBID;
    int allianceCrestId = m_nAllianceCrestID;
    int winCount = m_nWinCount;
    int historyOpen = m_nHistoryOpen;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(characterName.data(), 0x32);
    connection.Bind(&heroClassType);
    connection.Bind(&heroType);
    connection.Bind(&pledgeDatabaseId);
    connection.Bind(pledgeName.data(), 0x32);
    connection.Bind(&pledgeCrestId);
    connection.Bind(&allianceDatabaseId);
    connection.Bind(allianceName.data(), 0x32);
    connection.Bind(&allianceCrestId);
    connection.Bind(&winCount);
    connection.Bind(words.data(), 0x80);
    connection.Bind(&historyOpen);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadGetHeroById (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nWorldID);
    connection.BindParameter(&m_nUserDBID);
    m_nReturn = false;
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        CopyUtf16Result(m_szCharName, characterName, 0x32);
        m_nHeroClassType = heroClassType;
        m_nHeroType = heroType;
        m_nPledgeDBID = pledgeDatabaseId;
        CopyUtf16Result(m_szPledgeName, pledgeName, 0x32);
        m_nPledgeCrestID = pledgeCrestId;
        m_nAllianceDBID = allianceDatabaseId;
        CopyUtf16Result(m_szAllianceName, allianceName, 0x32);
        m_nAllianceCrestID = allianceCrestId;
        m_nWinCount = winCount;
        CopyUtf16Result(m_szWords, words, 0x80);
        m_nHistoryOpen = historyOpen;
        m_nReturn = true;
    }
    return true;
}

bool CDX_WOLYMPIAD_ADD_POINT::DoCacheDJob() {
    // Current Cached.exe 0x004b5cdc: packet order is world, point, user while
    // the stored procedure receives world, user, point.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadAddPoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nWorldID);
    connection.BindParameter(&m_nUserDBID);
    connection.BindParameter(&m_nPoint);
    connection.ExecutePrepared(statement);
    return true;
}

bool CDX_WOLYMPIAD_START_SEASON::DoCacheDJob() {
    // Current Cached.exe 0x004b5e34.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const long seasonStart = m_tSeasonStart;
    connection.Bind(&m_nReturn);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadStartSeason (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nSeason);
    connection.BindParameter(&m_nStep);
    connection.BindParameter(&seasonStart);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return true;
}

bool CDX_WOLYMPIAD_SAVE_HERO_TYPE::DoCacheDJob() {
    // Current Cached.exe 0x004b5fac. m_nNpcSID and m_nUserSID are returned
    // untouched. Neither is read by the CacheD body.
    L2_GUARD_FUNCTION();
    const std::wstring characterName = FixedUtf16ToWide(m_szCharName);
    const std::wstring pledgeName = FixedUtf16ToWide(m_szPledgeName);
    const std::wstring allianceName = FixedUtf16ToWide(m_szAllianceName);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadSaveHeroType (?,?,?,?,?, ?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nWorldID);
    connection.BindParameter(&m_nUserDBID);
    connection.BindParameter(&m_nHeroType);
    connection.BindParameter(characterName.c_str());
    connection.BindParameter(&m_nPledgeDBID);
    connection.BindParameter(pledgeName.c_str());
    connection.BindParameter(&m_nPledgeCrestID);
    connection.BindParameter(&m_nAllianceDBID);
    connection.BindParameter(allianceName.c_str());
    connection.BindParameter(&m_nAllianceCrestID);
    m_nReturn = connection.ExecutePrepared(statement) ? 1 : 0;
    return true;
}

bool CDX_WOLYMPIAD_DELETE_TRADE_POINT::DoCacheDJob() {
    // Current Cached.exe 0x004b60fc.
    L2_GUARD_FUNCTION();
    m_nReturn = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_nReturn);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadDeleteTradePoint (?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nWorldID);
    connection.BindParameter(&m_nUserDBID);
    connection.BindParameter(&m_nTradePoint);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }

    if (m_nAccountID < 1) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[NO_ERROR][WORLD_OLYMPIAD] Delete olympiad trade point "
        //     L"by builder: %d, %d, %d",
        //     m_nWorldID,
        //     m_nUserDBID,
        //     m_nTradePoint)
        // Log.AddT(
        //     LOG_NORMAL,
        //     L"[world olympiad] Builder requested removal of %d trade points from character DB ID %d in world %d. Response
        //     code %d.", m_nTradePoint, m_nUserDBID, m_nWorldID, m_nReturn)
        L2_LOG_INFO(
            Log,
            u"[world olympiad] Builder requested removal of %d trade points from character DB ID %d in world %d; response "
            u"code %d.",
            m_nTradePoint, m_nUserDBID, m_nWorldID, m_nReturn
        );
    } else {
        CLogFormatter formatter;
        formatter.WriteLog(
            0x1fdf, m_nUserDBID, m_nAccountID, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, m_nReturn, m_nTradePoint, 0, 0, 0,
            0, 0, 0, 0, m_nWorldID, m_szCharName, m_szAccountName, nullptr, nullptr, 0
        );
    }
    return true;
}

bool CDX_WOLYMPIAD_ADD_TRADE_POINT::DoCacheDJob() {
    // Current Cached.exe 0x004b635c.
    L2_GUARD_FUNCTION();
    m_nReturn = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_nReturn);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadAddTradePoint (?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nWorldID);
    connection.BindParameter(&m_nUserDBID);
    connection.BindParameter(&m_nTradePoint);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return true;
}

bool CDX_WOLYMPIAD_RESET_POINT::DoCacheDJob() {
    // Current Cached.exe 0x004b64d8.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadResetPoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nWorldID);
    connection.BindParameter(&m_nUserDBID);
    connection.BindParameter(&m_nPoint);
    connection.ExecutePrepared(statement);
    return true;
}

bool CDX_WOLYMPIAD_MATCHING_POINT::DoCacheDJob() {
    // Current Cached.exe 0x004b6630.
    L2_GUARD_FUNCTION();
    std::int64_t point = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&point);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadMatchingPointLoad (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nWorldID);
    connection.BindParameter(&m_nUserDBID);
    m_bSuccess = connection.ExecutePrepared(statement) && connection.Fetch();
    if (m_bSuccess) {
        std::memcpy(&m_nPoint, &point, sizeof(point));
    }
    return true;
}

bool CDX_WOLYMPIAD_MATCHING_POINT_SAVE::DoCacheDJob() {
    // Current Cached.exe 0x004b67a0 keeps the raw packet count. A zero
    // count suppresses the DX response. A negative count skips the loop but
    // still returns true, exactly as the target comparison does.
    L2_GUARD_FUNCTION();
    if (m_nCount == 0) {
        return false;
    }

    constexpr const wchar_t* statement = L"{Call lin_WOlympiadMatchingPointSave (?, ?, ?)}";
    for (int index = 0; index < m_nCount; ++index) {
        m_anResults[index] = 1;
        std::int64_t newPoint = 0;
        std::int64_t oldPoint = 0;
        std::memcpy(&newPoint, &m_anNewPoints[index], sizeof(newPoint));
        std::memcpy(&oldPoint, &m_anOldPoints[index], sizeof(oldPoint));
        const int userDatabaseId = m_anUserDBIDs[index];
        const int worldId = m_anWorldIDs[index];
        const int matchId = m_anMatchIDs[index];

        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Prepare(statement);
        connection.BindParameter(&worldId);
        connection.BindParameter(&userDatabaseId);
        connection.BindParameter(&newPoint);
        if (!connection.ExecutePrepared(statement)) {
            m_anResults[index] = 0;
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD]Save Match Point failed: "
            //     L"%d, %d, %I64d",
            //     worldId,
            //     userDatabaseId,
            //     newPoint)
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[world olympiad] Cannot save matching points for character DB ID %d in world %d: database execution
            //     failed, match %d, new points %lld.", userDatabaseId, worldId, matchId, static_cast<long long>(newPoint))
            L2_LOG_ERROR(
                Log,
                u"[world olympiad] Cannot save matching points for character DB ID %d in world %d: database execution "
                u"failed; match %d, new points %lld.",
                userDatabaseId, worldId, matchId, static_cast<long long>(newPoint)
            );
        } else {
            Write_LOG_WOLYMPIAD_SAVE_MATCH_POINT_InHost(worldId, userDatabaseId, newPoint, oldPoint, matchId);
        }
    }
    return true;
}

bool CDX_WOLYMPIAD_MATCHING_POINT_RESET_ALL::DoCacheDJob() {
    // Current Cached.exe 0x004abb90 logs the reset message regardless of the
    // stored procedure result.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadMatchingPointResetAll}";
    connection.Prepare(statement);
    // connection.ExecutePrepared(statement)
    const bool resetSucceeded = connection.ExecutePrepared(statement);
    // Log.AddT(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD]Match Point reset all")
    // Log.AddT(
    //     resetSucceeded ? LOG_NORMAL : LOG_ERROR,
    //     L"[world olympiad] Resetting all matching points %ls.",
    //     resetSucceeded ? L"succeeded" : L"failed")
    L2_LOG(
        Log, resetSucceeded ? LOG_NORMAL : LOG_ERROR, u"[world olympiad] Resetting all matching points %ls.",
        resetSucceeded ? L"succeeded" : L"failed"
    );
    return true;
}

bool CDX_WOLYMPIAD_RECORD::DoCacheDJob() {
    // Current Cached.exe 0x004b7ec0.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&nPoint);
    connection.Bind(&nWinCount);
    connection.Bind(&nLoseCount);
    connection.Bind(&nPrevClassType);
    connection.Bind(&nPrevRank);
    connection.Bind(&nPrevRankCount);
    connection.Bind(&nPrevClassRank);
    connection.Bind(&nPrevClassRankCount);
    connection.Bind(&nPrevClassRankByServer);
    connection.Bind(&nPrevClassRankByServerCount);
    connection.Bind(&nPrevPoint);
    connection.Bind(&nPrevWinCount);
    connection.Bind(&nPrevLoseCount);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadGetRecord (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&idWorld);
    connection.BindParameter(&idUser);
    connection.BindParameter(&nSeason);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1e78)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[world olympiad] Cannot load the record for character DB ID %d in world %d, season %d: no database row was
        //     obtained.", idUser, idWorld, nSeason)
        L2_LOG_ERROR(
            Log,
            u"[world olympiad] Cannot load the record for character DB ID %d in world %d, season %d: no database row was "
            u"obtained.",
            idUser, idWorld, nSeason
        );
    }
    return true;
}

bool CDX_WOLYMPIAD_GENERATE_RECORD_FOR_TEST::DoCacheDJob() {
    // Current Cached.exe 0x004b810c. "FOR_TEST" is the original shipping PDB
    // identifier and this handler is registered in the production DX table.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadGenerateRecord_ForTest (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nMaxOlympiadPoint);
    connection.BindParameter(&nMaxOlympiadWinCount);
    connection.BindParameter(&nMaxOlympiadMatchCount);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1e9a)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[world olympiad] Cannot generate test records: database execution failed. Maximum points %d, wins %d,
        //     matches %d.", nMaxOlympiadPoint, nMaxOlympiadWinCount, nMaxOlympiadMatchCount)
        L2_LOG_ERROR(
            Log,
            u"[world olympiad] Cannot generate test records: database execution failed; maximum points %d, wins %d, matches "
            u"%d.",
            nMaxOlympiadPoint, nMaxOlympiadWinCount, nMaxOlympiadMatchCount
        );
    }
    return true;
}

bool CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10::DoCacheDJob() {
    // Current Cached.exe 0x004b8290.
    L2_GUARD_FUNCTION();
    m_nCount = 0;
    std::memset(m_Ranks, 0, sizeof(m_Ranks));

    int worldId = 0;
    int userDatabaseId = 0;
    int point = 0;
    int matchCount = 0;
    int winCount = 0;
    std::array<char16_t, 25> characterName{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&worldId);
    connection.Bind(&userDatabaseId);
    connection.Bind(characterName.data(), 0x32);
    connection.Bind(&point);
    connection.Bind(&matchCount);
    connection.Bind(&winCount);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadGetClassRankByServerTop10 (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_idWorld);
    connection.BindParameter(&m_nHeroClassType);
    connection.BindParameter(&m_nSeason);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    while (connection.Fetch() && m_nCount < 10) {
        UserRank& rank = m_Ranks[m_nCount];
        rank.nWorldID = worldId;
        rank.nUserDBID = userDatabaseId;
        rank.nPoint = point;
        rank.nMatchCount = matchCount;
        rank.nWinCount = winCount;
        CopyUtf16Result(rank.sCharName, characterName, 0x32);
        ++m_nCount;
    }
    return true;
}

bool CDX_PLEDGE_MASTERY_LOAD::DoCacheDJob() {
    // Current Cached.exe 0x004ae778.
    L2_GUARD_FUNCTION();
    std::memset(m_anPoints, 0, sizeof(m_anPoints));
    m_bSuccess = false;
    CPledgeSP pledge = g_pledgeDb.GetPledge(m_idPledge, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        m_bSuccess = true;
        pledge->m_mastery->GetPoints(m_anPoints);
    }
    return true;
}

bool CDX_PLEDGE_CONTRIBUTION_MEMBER_ADD::DoCacheDJob() {
    // Current Cached.exe 0x004abdd8 discards AddPoint's result.
    L2_GUARD_FUNCTION();
    PledgeContributionInCache::AddPoint(m_dbidPledge, m_dbidUser, m_nPoint);
    return true;
}

bool CDX_PLEDGE_CONTRIBUTION_MEMBER_RESET::DoCacheDJob() {
    // Current Cached.exe 0x004b6c4c performs both writes and ignores both
    // results before acknowledging the DX job.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    int zero = 0;
    constexpr const wchar_t* resetStatement = L"{Call lin_PledgeContributionMemberResetInfo (?,?,?,?)}";
    connection.Prepare(resetStatement);
    connection.BindParameter(&m_dbidPledge);
    connection.BindParameter(&m_dbidUser);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    connection.ExecutePrepared(resetStatement);

    connection.ResetParam();
    constexpr const wchar_t* rewardStatement = L"{Call lin_PledgeContributionMemberSaveReward (?,?,?)}";
    connection.Prepare(rewardStatement);
    connection.BindParameter(&m_dbidPledge);
    connection.BindParameter(&m_dbidUser);
    connection.BindParameter(&zero);
    connection.ExecutePrepared(rewardStatement);
    return true;
}

bool CDX_PLEDGE_CONTRIBUTION_MEMBER_TAKE_PERIOD_REWARD::DoCacheDJob() {
    // Current Cached.exe 0x004ae894.
    L2_GUARD_FUNCTION();
    m_nResult = 3;
    CUserSP user = g_userDb.GetUser(m_idUser, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No user: %d",
        //     kDxPacketSourcePath,
        //     0x1c17,
        //     m_idUser)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan contribution] Period reward unavailable for character DB ID %d: character was not found.",
        //     m_idUser)
        L2_LOG_ERROR(
            Log, u"[clan contribution] Period reward unavailable for character DB ID %d: character was not found.", m_idUser
        );
        return true;
    }

    m_idPledge = static_cast<int>(user->GetPledgeID());
    if (m_idPledge == 0) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No pledge: %d",
        //     kDxPacketSourcePath,
        //     0x1c1f,
        //     m_idUser)
        // Log.AddT(
        //     LOG_WARN,
        //     L"[clan contribution] Period reward unavailable for character DB ID %d: character has no clan.",
        //     m_idUser)
        L2_LOG_WARN(
            Log, u"[clan contribution] Period reward unavailable for character DB ID %d: character has no clan.", m_idUser
        );
        return true;
    }

    int databaseResult = 0;
    if (PledgeContributionInCache::TakePeriodReward(
            m_idPledge, m_idUser, databaseResult, m_nRank, m_nRatio, m_nActiveMember
        )) {
        m_nResult = 0;
    } else if (databaseResult == 0) {
        m_nResult = 1;
    } else if (databaseResult == 2) {
        m_nResult = 2;
    } else {
        m_nResult = 3;
    }
    return true;
}

bool CDX_PLEDGE_CONTRIBUTION_RANK_LOAD::DoCacheDJob() {
    // Current Cached.exe 0x004abca8.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&nSeason);
    connection.Bind(&tStartTime);
    constexpr const wchar_t* statement = L"{Call lin_PledgeContributionRankLoad}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return true;
}

bool CDX_PLEDGE_CONTRIBUTION_RANK_SAVE::DoCacheDJob() {
    // Current Cached.exe 0x004b6994.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const long startTime = tStartTime;
    constexpr const wchar_t* statement = L"{Call lin_PledgeContributionRankSave (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nSeason);
    connection.BindParameter(&startTime);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1b92)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan contribution] Cannot save ranking season %d with start timestamp %d: database execution failed.",
        //     nSeason, static_cast<int>(tStartTime))
        L2_LOG_ERROR(
            Log, u"[clan contribution] Cannot save ranking season %d with start timestamp %d: database execution failed.",
            nSeason, static_cast<int>(tStartTime)
        );
    }
    return true;
}

bool CDX_PLEDGE_CONTRIBUTION_RANK_DECIDE::DoCacheDJob() {
    // Current Cached.exe 0x004b6af0 does not read nSeason.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const long startTime = tStartTime;
    constexpr const wchar_t* statement = L"{Call lin_PledgeContributionRankDecide (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&startTime);
    connection.BindParameter(&nMinPoint);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1bb5)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan contribution] Cannot decide rankings for start timestamp %d and minimum points %d: database execution
        //     failed.", static_cast<int>(tStartTime), nMinPoint)
        L2_LOG_ERROR(
            Log,
            u"[clan contribution] Cannot decide rankings for start timestamp %d and minimum points %d: database execution "
            u"failed.",
            static_cast<int>(tStartTime), nMinPoint
        );
    }
    return true;
}

bool CDX_PLEDGE_RAID_LOAD::DoCacheDJob() {
    // Current Cached.exe 0x004abe84 fetches the first row even after an
    // Execute failure and uses a missing second row as an all-zero previous
    // season record.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    int season = 0;
    std::int32_t startTime = 0;
    connection.Bind(&season);
    connection.Bind(&startTime);
    constexpr const wchar_t* statement = L"{Call lin_PledgeRaidLoad}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1c5e)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan raids] Cannot load seasons: database execution failed.")
        L2_LOG_ERROR(Log, u"[clan raids] Cannot load seasons: database execution failed.");
    }
    connection.Fetch();
    nSeason_ = season;
    tStartTime_ = startTime;
    if (connection.Fetch()) {
        nPrevSeason_ = season;
        tPrevStartTime_ = startTime;
    } else {
        nPrevSeason_ = 0;
        tPrevStartTime_ = 0;
    }
    return true;
}

bool CDX_PLEDGE_RAID_SAVE::DoCacheDJob() {
    // Current Cached.exe 0x004b6dc8.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const long startTime = tStartTime_;
    constexpr const wchar_t* statement = L"{Call lin_PledgeRaidSave (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nSeason_);
    connection.BindParameter(&startTime);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1c8c)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan raids] Cannot save season %d with start timestamp %d: database execution failed.",
        //     nSeason_, static_cast<int>(tStartTime_))
        L2_LOG_ERROR(
            Log, u"[clan raids] Cannot save season %d with start timestamp %d: database execution failed.", nSeason_,
            static_cast<int>(tStartTime_)
        );
    }
    return true;
}

bool CDX_PLEDGE_RAID_NEXT_SEASON::DoCacheDJob() {
    // Current Cached.exe 0x004b6f24 executes this prepared call twice. The
    // first result is discarded and the second controls the error log.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const long startTime = tStartTime_;
    constexpr const wchar_t* statement = L"{Call lin_PledgeRaidNextSeason (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&startTime);
    connection.ExecutePrepared(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1caa)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan raids] Advancing to the next season failed on the second database execution. Start timestamp %d.",
        //     static_cast<int>(tStartTime_))
        L2_LOG_ERROR(
            Log, u"[clan raids] Advancing to the next season failed on the second database execution; start timestamp %d.",
            static_cast<int>(tStartTime_)
        );
    }
    return true;
}

bool CDX_PLEDGE_RAID_RESET_SEASON::DoCacheDJob() {
    // Current Cached.exe 0x004ac028 logs the reset timestamp regardless of
    // whether the stored procedure succeeds.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeRaidResetSeason}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1cc7)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan raids] Resetting seasons failed. Request timestamp %d.",
        //     static_cast<int>(tResetTime_))
        L2_LOG_ERROR(Log, u"[clan raids] Resetting seasons failed; request timestamp %d.", static_cast<int>(tResetTime_));
    } else {
        // Log.AddT(
        //     LOG_NORMAL,
        //     L"[clan raids] Resetting seasons succeeded. Request timestamp %d.",
        //     static_cast<int>(tResetTime_))
        L2_LOG_INFO(Log, u"[clan raids] Resetting seasons succeeded; request timestamp %d.", static_cast<int>(tResetTime_));
    }
    // Noise: The unconditional reset line implies success after failure and duplicates the outcome above.
    // Log.AddT(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d][Raid] Reset Season : t[%d]",
    //     kDxPacketSourcePath,
    //     0x1cca,
    //     tResetTime_)
    return true;
}

bool CDX_PLEDGE_RAID_GET_INFO::DoCacheDJob() {
    // Current Cached.exe 0x004b706c uses a second DBConn only after the total
    // count query returns a row, then fetches the detail row unconditionally.
    L2_GUARD_FUNCTION();
    DBConn countConnection(runtime::process::CurrentDatabaseId());
    countConnection.Bind(&nTotalCount_);
    constexpr const wchar_t* countStatement = L"{Call lin_PledgeRaidGetTotalCount (?)}";
    countConnection.Prepare(countStatement);
    countConnection.BindParameter(&nSeason_);
    if (!countConnection.ExecutePrepared(countStatement) || !countConnection.Fetch()) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1cee)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan raids] Cannot load the total for season %d: no database row was obtained. Clan details were not
        //     queried.", nSeason_)
        L2_LOG_ERROR(
            Log,
            u"[clan raids] Cannot load the total for season %d: no database row was obtained; clan details were not "
            u"queried.",
            nSeason_
        );
        return true;
    }

    DBConn infoConnection(runtime::process::CurrentDatabaseId());
    std::array<char16_t, 25> pledgeName = MakeUtf16ResultBuffer<25>(szPledgeName_);
    infoConnection.Bind(&nWorldId_);
    infoConnection.Bind(&nPledgeDbId_);
    infoConnection.Bind(pledgeName.data(), 0x32);
    infoConnection.Bind(&nRank_);
    infoConnection.Bind(&nPoint_);
    infoConnection.Bind(&nRewardReason_);
    constexpr const wchar_t* infoStatement = L"{Call lin_PledgeRaidGetInfo (?,?,?)}";
    infoConnection.Prepare(infoStatement);
    infoConnection.BindParameter(&nSeason_);
    infoConnection.BindParameter(&nWorldId_);
    infoConnection.BindParameter(&nPledgeDbId_);
    if (!infoConnection.ExecutePrepared(infoStatement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1cfd)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan raids] Cannot load details for clan DB ID %d in world %d, season %d: database execution failed.",
        //     nPledgeDbId_, nWorldId_, nSeason_)
        L2_LOG_ERROR(
            Log, u"[clan raids] Cannot load details for clan DB ID %d in world %d, season %d: database execution failed.",
            nPledgeDbId_, nWorldId_, nSeason_
        );
    }
    infoConnection.Fetch();
    CopyUtf16Result(szPledgeName_, pledgeName, 0x32);
    return true;
}

bool CDX_PLEDGE_RAID_GET_TOTAL_COUNT::DoCacheDJob() {
    // Current Cached.exe 0x004b7310 ignores nIsReward_.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&nTotalCount_);
    constexpr const wchar_t* statement = L"{Call lin_PledgeRaidGetTotalCount (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nSeason_);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1d1d)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan raids] Cannot load the total for season %d: no database row was obtained.",
        //     nSeason_)
        L2_LOG_ERROR(Log, u"[clan raids] Cannot load the total for season %d: no database row was obtained.", nSeason_);
    }
    return true;
}

bool CDX_PLEDGE_RAID_SET_INFO::DoCacheDJob() {
    // Current Cached.exe 0x004b7474. Disassembly fixes the non-layout order
    // of the final two SQL parameters: point first, then target long time.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const std::wstring pledgeName = FixedUtf16ToWide(szPledgeName_);
    const long saveTime = tSaveTime_;
    constexpr const wchar_t* statement = L"{Call lin_PledgeRaidSetInfo (?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nSeason_);
    connection.BindParameter(&nWorldId_);
    connection.BindParameter(&nPledgeDbId_);
    connection.BindParameter(pledgeName.c_str());
    connection.BindParameter(&nPoint_);
    connection.BindParameter(&saveTime);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1d41)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan raids] Cannot save clan DB ID %d in world %d, season %d: database execution failed. Points %lld, save
        //     timestamp %d.", nPledgeDbId_, nWorldId_, nSeason_, static_cast<long long>(nPoint_),
        //     static_cast<int>(tSaveTime_))
        L2_LOG_ERROR(
            Log,
            u"[clan raids] Cannot save clan DB ID %d in world %d, season %d: database execution failed; points %lld, save "
            u"timestamp %d.",
            nPledgeDbId_, nWorldId_, nSeason_, static_cast<long long>(nPoint_), static_cast<int>(tSaveTime_)
        );
    }
    return true;
}

bool CDX_PLEDGE_RAID_HUNT_NPC_INFO::DoCacheDJob() {
    // Current Cached.exe 0x004ba280 reads BossInfo+0x58 directly.
    L2_GUARD_FUNCTION();
    const std::wstring npcDbName = FixedUtf16ToWide(szNpcDbName_);
    BossInfoSP info = Singleton<BossInfoDB>::inst().GetInfo(npcDbName.c_str());
    if (info) {
        tDeathTime_ = info->m_deathTime;
    }
    return true;
}

bool CDX_PLEDGE_RAID_INZONE_SAVE::DoCacheDJob() {
    // Current Cached.exe 0x004b75bc.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const long restrictTime = tRestrictTime_;
    constexpr const wchar_t* statement = L"{Call lin_PledgeRaidInzoneSave (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nPledgeDbId_);
    connection.BindParameter(&restrictTime);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1d7e)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan raids] Cannot save instance restriction for clan DB ID %d at timestamp %d: database execution
        //     failed.", nPledgeDbId_, static_cast<int>(tRestrictTime_))
        L2_LOG_ERROR(
            Log,
            u"[clan raids] Cannot save instance restriction for clan DB ID %d at timestamp %d: database execution failed.",
            nPledgeDbId_, static_cast<int>(tRestrictTime_)
        );
    }
    return true;
}

bool CDX_PLEDGE_RAID_INZONE_DELETE::DoCacheDJob() {
    // Current Cached.exe 0x004b7718.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeRaidInzoneDelete (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nPledgeDbId_);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1d9b)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan raids] Cannot delete instance restriction for clan DB ID %d: database execution failed.",
        //     nPledgeDbId_)
        L2_LOG_ERROR(
            Log, u"[clan raids] Cannot delete instance restriction for clan DB ID %d: database execution failed.",
            nPledgeDbId_
        );
    }
    return true;
}

bool CDX_PLEDGE_RAID_INZONE_RESET::DoCacheDJob() {
    // Current Cached.exe 0x004ac184.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeRaidInzoneReset}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1db7)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan raids] Cannot reset instance restrictions: database execution failed.")
        L2_LOG_ERROR(Log, u"[clan raids] Cannot reset instance restrictions: database execution failed.");
    }
    return true;
}

bool CDX_PLEDGE_MISSION_SET_COUNT::DoCacheDJob() {
    // Current Cached.exe 0x004b7860.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeMissionSetCount (?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nUserDbId_);
    connection.BindParameter(&nMissionId_);
    connection.BindParameter(&nCount_);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1dd5)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan missions] Cannot save count %d for mission %d, character DB ID %d: database execution failed.",
        //     nCount_, nMissionId_, nUserDbId_)
        L2_LOG_ERROR(
            Log, u"[clan missions] Cannot save count %d for mission %d, character DB ID %d: database execution failed.",
            nCount_, nMissionId_, nUserDbId_
        );
    }
    return true;
}

bool CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_LOAD::DoCacheDJob() {
    // Current Cached.exe 0x004b79e4 binds count before week-start time.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&nCount_);
    connection.Bind(&tWeekStartTime_);
    constexpr const wchar_t* loadStatement = L"{Call lin_PledgeMissionCountOfOneWeekLoad (?)}";
    connection.Prepare(loadStatement);
    connection.BindParameter(&nUserDbId_);
    if (!connection.ExecutePrepared(loadStatement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1df6)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan missions] Cannot load the weekly count for character DB ID %d: database execution failed.",
        //     nUserDbId_)
        L2_LOG_ERROR(
            Log, u"[clan missions] Cannot load the weekly count for character DB ID %d: database execution failed.",
            nUserDbId_
        );
    }
    connection.Fetch();

    if (tWeekStartTime_ != 0 && tWeekStartTime_ < tCheckWeekStartTime_) {
        connection.UnBind();
        constexpr const wchar_t* resetStatement = L"{Call lin_PledgeMissionCountOfOneWeekReset (?)}";
        connection.Prepare(resetStatement);
        connection.BindParameter(&nUserDbId_);
        if (!connection.ExecutePrepared(resetStatement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     kDxPacketSourcePath,
            //     0x1dff)
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[clan missions] Cannot reset the expired weekly count in the database for character DB ID %d. The reply
            //     count and start timestamp will still be cleared.", nUserDbId_)
            L2_LOG_ERROR(
                Log,
                u"[clan missions] Cannot reset the expired weekly count in the database for character DB ID %d; the reply "
                u"count and start timestamp will still be cleared.",
                nUserDbId_
            );
        }
        nCount_ = 0;
        tWeekStartTime_ = 0;
    }
    return true;
}

bool CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_ADD::DoCacheDJob() {
    // Current Cached.exe 0x004b7bf4.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const long weekStartTime = tWeekStartTime_;
    constexpr const wchar_t* statement = L"{Call lin_PledgeMissionCountOfOneWeekAdd (?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nUserDbId_);
    connection.BindParameter(&nAddCount_);
    connection.BindParameter(&weekStartTime);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1e21)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan missions] Cannot add %d to the weekly count for character DB ID %d, week-start timestamp %d: database
        //     execution failed.", nAddCount_, nUserDbId_, static_cast<int>(tWeekStartTime_))
        L2_LOG_ERROR(
            Log,
            u"[clan missions] Cannot add %d to the weekly count for character DB ID %d, week-start timestamp %d: database "
            u"execution failed.",
            nAddCount_, nUserDbId_, static_cast<int>(tWeekStartTime_)
        );
    }
    return true;
}

bool CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_RESET::DoCacheDJob() {
    // Current Cached.exe 0x004b7d78.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeMissionCountOfOneWeekReset (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nUserDbId_);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kDxPacketSourcePath,
        //     0x1e3d)
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[clan missions] Cannot reset the weekly count for character DB ID %d: database execution failed.",
        //     nUserDbId_)
        L2_LOG_ERROR(
            Log, u"[clan missions] Cannot reset the weekly count for character DB ID %d: database execution failed.",
            nUserDbId_
        );
    }
    return true;
}

bool DxSetTicketBuyCount::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(mUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        {
            L2_WRITE_GUARD(*user.operator->());
            if (static_cast<std::uint32_t>(mCount) < 0x100U) {
                user->m_ssqInfo.ticketBuyCount = mCount;
            }
            user->SaveSSQInfo();
        }
    }
    return true;
}

bool DxAddSevenSignQuestMember::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(mUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        mErrorCode = 1;
        return true;
    }

    {
        L2_WRITE_GUARD(*user.operator->());
        user->m_ssqInfo.roundNumber = mSSQRound;
        user->m_ssqInfo.part = mSSQPart;
        user->m_ssqInfo.sealSelectionNumber = mSealSelectionNo;
        user->m_ssqInfo.position = mPosition;
        user->SetSSQPoint(mInitialPoint);
        user->m_ssqInfo.ticketBuyCount = 0;
        user->m_ssqInfo.joinTime = runtime::crt::Time32Now();
        user->SaveSSQInfo();
    }

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        const int part = mSSQPart;
        database.m_memberCount[part] = runtime::AddInt32Wrapping(database.m_memberCount[part], 1);
        database.m_sealSelectionCount[part][mSealSelectionNo] =
            runtime::AddInt32Wrapping(database.m_sealSelectionCount[part][mSealSelectionNo], 1);

        const std::int64_t systemPoint = runtime::AddInt64Wrapping(
            mInitialPoint,
            runtime::AddInt64Wrapping(
                database.m_collectedPoint[SSQPart::TWILIGHT], database.m_collectedPoint[SSQPart::DAWN]
            )
        );
        if (static_cast<std::uint64_t>(systemPoint) < 1'000'000'000'000ULL) {
            database.SetCollectedPoint(
                static_cast<SSQPart::type>(part),
                runtime::AddInt64Wrapping(database.m_collectedPoint[part], mInitialPoint)
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "[..\\Shared\\DXPacket.cpp][%d] SSQ Addmember : "
            //     "Too large user point, result=[%lld], delta=[%lld]",
            //     0x6cf,
            //     static_cast<long long>(systemPoint),
            //     static_cast<long long>(mInitialPoint))
            // Log.Add(
            //     LOG_ERROR,
            //     "[seven signs] Aggregate points were not updated for character DB ID %d: computed total %lld is outside
            //     0..999999999999, delta %lld. Earlier character changes are retained.", mUserDBID, static_cast<long
            //     long>(systemPoint), static_cast<long long>(mInitialPoint))
            L2_LOG_ERROR(
                Log,
                "[seven signs] Aggregate points were not updated for character DB ID %d: computed total %lld is outside "
                "0..999999999999; delta %lld. Earlier character changes are retained.",
                mUserDBID, static_cast<long long>(systemPoint), static_cast<long long>(mInitialPoint)
            );
        }
        database.SaveJoinInfo(static_cast<SSQPart::type>(part));
    }
    mErrorCode = 0;
    return true;
}

bool DxDeleteDepositedSevenSignQuestItem::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(mUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        mErrorCode = 1;
        return true;
    }

    {
        L2_WRITE_GUARD(*user.operator->());
        std::int64_t currentCount = 0;
        const std::uint32_t item = static_cast<std::uint32_t>(mItemType);
        if (item < 3U) {
            if (mSSQPart == SSQPart::TWILIGHT) {
                currentCount = user->m_ssqInfo.twilightItemCount[item];
            } else if (mSSQPart == SSQPart::DAWN) {
                currentCount = user->m_ssqInfo.dawnItemCount[item];
            }
        }
        std::int64_t remaining = runtime::SubtractInt64Wrapping(currentCount, mCount);
        if (remaining < 0) {
            remaining = 0;
        }
        user->SetDepositedSSQItemCount(mSSQPart, mItemType, remaining);
        user->SaveSSQInfo();
    }
    mErrorCode = 0;
    return true;
}

bool DxDepositSevenSignQuestItemEx::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(mUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        mErrorCode = 1;
        return true;
    }

    {
        L2_WRITE_GUARD(*user.operator->());
        for (int item = 0; item < 3; ++item) {
            const std::int64_t delta = mCountList[item];
            if (delta <= 0) {
                continue;
            }

            std::int64_t currentCount = 0;
            if (mSSQPart == SSQPart::TWILIGHT) {
                currentCount = user->m_ssqInfo.twilightItemCount[item];
            } else if (mSSQPart == SSQPart::DAWN) {
                currentCount = user->m_ssqInfo.dawnItemCount[item];
            }
            std::int64_t count = runtime::AddInt64Wrapping(currentCount, delta);
            if (count < 0) {
                count = 0;
            }
            user->SetDepositedSSQItemCount(mSSQPart, static_cast<SSQItem::type>(item), count);
        }
        user->SetSSQPoint(runtime::AddInt64Wrapping(user->m_ssqInfo.point, mPoint));
        user->SaveSSQInfo();
    }

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        const std::int64_t systemPoint = runtime::AddInt64Wrapping(
            mPoint,
            runtime::AddInt64Wrapping(
                database.m_collectedPoint[SSQPart::TWILIGHT], database.m_collectedPoint[SSQPart::DAWN]
            )
        );
        const std::uint64_t systemPointBits = static_cast<std::uint64_t>(systemPoint);
        if (systemPointBits - 1ULL < 999'999'999'999ULL) {
            const int part = static_cast<int>(mSSQPart);
            database.SetCollectedPoint(mSSQPart, runtime::AddInt64Wrapping(database.m_collectedPoint[part], mPoint));
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "[..\\Shared\\DXPacket.cpp][%d] DepositItem : "
            //     "Too large system point, result[%d], delta[%d]",
            //     0x7c7,
            //     runtime::Int32FromBits(systemPoint),
            //     runtime::Int32FromBits(mPoint))
            // Log.Add(
            //     LOG_ERROR,
            //     "[seven signs] Aggregate points were not updated for character DB ID %d: computed total %lld is
            //     outside 1..999999999999, delta %lld. Earlier character changes are retained.", mUserDBID, static_cast<long
            //     long>(systemPoint), static_cast<long long>(mPoint))
            L2_LOG_ERROR(
                Log,
                "[seven signs] Aggregate points were not updated for character DB ID %d: computed total %lld is outside "
                "1..999999999999; delta %lld. Earlier character changes are retained.",
                mUserDBID, static_cast<long long>(systemPoint), static_cast<long long>(mPoint)
            );
        }
        database.SaveJoinInfo(mSSQPart);
    }
    mErrorCode = 0;
    return true;
}

bool DxChangeSSQUserPoint::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(mUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        mErrorCode = 1;
        return true;
    }

    {
        L2_WRITE_GUARD(*user.operator->());
        if (mPoint > 0) {
            user->SetSSQPoint(mPoint);
            user->SaveSSQInfo();
        }
    }

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        if (mPoint > 0) {
            const int part = static_cast<int>(mPart);
            database.SetCollectedPoint(mPart, runtime::AddInt64Wrapping(database.m_collectedPoint[part], mPoint));
            database.SaveJoinInfo(mPart);
        }
    }
    mErrorCode = 0;
    return true;
}

bool DxDismissSevenSignMember::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    mErrorCode = 0;
    CUserSP user = g_userDb.GetUser(mUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        mErrorCode = 1;
        return false;
    }

    {
        L2_WRITE_GUARD(*user.operator->());
        user->m_ssqInfo.roundNumber = 0;
        user->m_ssqInfo.part = 0;
        user->m_ssqInfo.position = 0;
        user->SetSSQPoint(0);
        user->m_ssqInfo.sealSelectionNumber = 0;
        user->SaveSSQInfo();
    }

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        const int part = static_cast<int>(mFromPart);
        const std::int32_t point = runtime::SubtractInt32Wrapping(
            runtime::Int32FromBits(database.m_collectedPoint[part]), runtime::Int32FromBits(mPoint)
        );
        if (point >= 0) {
            database.SetCollectedPoint(mFromPart, point);
        }

        const int seal = static_cast<int>(mSealSelectionNo);
        const std::int32_t selectionCount = runtime::SubtractInt32Wrapping(database.m_sealSelectionCount[part][seal], 1);
        if (selectionCount >= 0) {
            database.m_sealSelectionCount[part][seal] = selectionCount;
        }

        const std::int32_t memberCount = runtime::SubtractInt32Wrapping(database.m_memberCount[part], 1);
        if (memberCount >= 0) {
            database.m_memberCount[part] = memberCount;
        }
        database.SaveJoinInfo(mFromPart);
    }
    return true;
}

bool DxChangeSSQPeriod::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    if (mStatus == SSQStatus::EVENT) {
        L2_WRITE_GUARD(database);
        database.SetEventStartTime(mStartTime);
        database.SetEventEndTime(mEndTime);
        database.SaveSystemInfo();
    } else if (mStatus == SSQStatus::SEAL_EFFECT) {
        L2_WRITE_GUARD(database);
        database.SetSealEffectTime(mStartTime);
        database.SetSealEffectEndTime(mEndTime);
        database.SaveSystemInfo();
    } else {
        return true;
    }
    return true;
}

bool DxStartSSQAccount::DoCacheDJob() {
    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.SetCurrentStatus(SSQStatus::ACCOUNTING);
        database.SaveSystemInfo();
    }
    return true;
}

bool DxStartSSQSealEffect::DoCacheDJob() {
    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.SetCurrentStatus(SSQStatus::SEAL_EFFECT);
        database.SaveSystemInfo();
    }
    return true;
}

bool DxSetSevenSignQuestResult::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.m_winner = static_cast<SSQPart::type>(mWinner);
        // The current body copies only seals 1 through 3.
        for (int seal = 1; seal < 4; ++seal) {
            database.SetSealOwner(seal, static_cast<SSQPart::type>(mSealOwner[seal]));
        }
        database.SaveSystemInfo();
    }
    return true;
}

bool DxChangeSSQStatus::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.SetCurrentStatus(static_cast<SSQStatus::type>(mStatus));
        database.SaveSystemInfo();
    }
    return true;
}

bool DxCloseSevenSignQuest::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.SetCurrentStatus(static_cast<SSQStatus::type>(mStatus));
        database.SaveSystemInfo();
    }
    return true;
}

bool DxChangeSealSelectionCount::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.m_sealSelectionCount[static_cast<int>(mPart)][static_cast<int>(mSealNum)] = mCount;
        database.SaveJoinInfo(mPart);
    }
    mErrorCode = 0;
    return true;
}

bool DxStartQuickCycle::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.SetCurrentStatus(mStatus);
        database.SetEventStartTime(mEventStartTime);
        database.SetEventEndTime(mEventEndTime);
        database.SetSealEffectTime(mSealEffectTime);
        database.SetSealEffectEndTime(mSealEffectEndTime);
        database.SaveSystemInfo();
    }
    mErrorCode = 0;
    return true;
}

bool DxChangeMainEventPoint::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.SetMainEventPoint(mPartType, static_cast<std::int64_t>(mMainEventPoint));
        database.SaveJoinInfo(mPartType);
    }
    mErrorCode = 0;
    return true;
}

bool DxChangeSealOwner::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    mErrorCode = 0;
    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.SetSealOwner(static_cast<int>(mSealNumber), mPartType);
        database.SaveSystemInfo();
    }
    return true;
}

bool DxChangeSSQWinner::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    mErrorCode = 0;
    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.m_winner = mPartType;
        database.SaveSystemInfo();
    }
    return true;
}

bool DxChangeCycleMode::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    mErrorCode = 0;
    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.m_castleSnapshotTime = mCycleModeFlag;
        database.SaveSystemInfo();
    }
    return true;
}

bool DxAddTimeAttackRecord::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    TimeAttackRecord* const topRecord = TimeAttackDB::Instance().GetRecord(mRoomNo, TOP_OF_ROOM);
    if (topRecord) {
        L2_WRITE_GUARD(*topRecord);
        if (topRecord->m_point < mPoint) {
            topRecord->m_ssqPart = static_cast<int>(mSSQPart);
            topRecord->m_point = mPoint;
            topRecord->m_recordTime = mRecordTime;
            topRecord->m_elapsedTime = mElapsedTime;
            topRecord->m_memberCount = mMemberCount;
            int* const databaseIds = topRecord->m_memberDatabaseIdList;
            int* const rewardFlags = topRecord->m_memberRewardFlags;
            for (int index = 0; index < mMemberCount; ++index) {
                if (index < kMaximumTimeAttackMembers) {
                    CopyTargetMemberName(topRecord->m_memberNameList[index], mMemberNameList[index]);
                }
                databaseIds[index] = mMemberDBIDList[index];
                rewardFlags[index] = 0;
            }
            if (topRecord->Save()) {
                topRecord->LogForAudit();
            }
            mTopRecordUpdated = 1;
        } else {
            mTopRecordUpdated = 0;
        }
    }

    TimeAttackRecord* const partRecord = TimeAttackDB::Instance().GetRecord(mRoomNo, static_cast<int>(mSSQPart));
    if (partRecord) {
        L2_WRITE_GUARD(*partRecord);
        if (partRecord->m_point < mPoint) {
            partRecord->m_ssqPart = static_cast<int>(mSSQPart);
            partRecord->m_point = mPoint;
            partRecord->m_recordTime = mRecordTime;
            partRecord->m_elapsedTime = mElapsedTime;
            partRecord->m_memberCount = mMemberCount;
            int* const databaseIds = partRecord->m_memberDatabaseIdList;
            int* const rewardFlags = partRecord->m_memberRewardFlags;
            for (int index = 0; index < mMemberCount; ++index) {
                if (index < kMaximumTimeAttackMembers) {
                    CopyTargetMemberName(partRecord->m_memberNameList[index], mMemberNameList[index]);
                }
                databaseIds[index] = mMemberDBIDList[index];
                rewardFlags[index] = 0;
            }
            if (partRecord->Save()) {
                partRecord->LogForAudit();
            }
            mPartRecordUpdated = 1;
        } else {
            mPartRecordUpdated = 0;
        }
    }
    return true;
}

bool DxAddTimeAttackFee::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    mErrorCode = 0;
    TimeAttackRecord* const record = TimeAttackDB::Instance().GetRecord(mRoomNO, TOP_OF_ROOM);
    if (!record) {
        mErrorCode = 1;
        return true;
    }

    {
        L2_WRITE_GUARD(*record);
        record->m_fee = mFee == 0 ? 0 : runtime::AddInt64Wrapping(record->m_fee, mFee);
        record->Save();
    }
    mErrorCode = 0;
    return true;
}

bool DxSetTimeAttackRewardFlag::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    mErrorCode = 0;
    TimeAttackRecord* const record = TimeAttackDB::Instance().GetRecord(mRoomNO, mPartType);
    if (!record) {
        mErrorCode = 1;
        return true;
    }

    L2_WRITE_GUARD(*record);
    if (!record->IsMember(mUserDBID)) {
        mErrorCode = 1;
        return true;
    }
    record->SetRewardFlag(mUserDBID, mFlag);
    record->Save();
    mErrorCode = 0;
    return true;
}

#endif

#include "DBPacketEx.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <vector>
#include "AccountJobFinishSignal.h"
#include "AirShip.h"
#include "Alliance.h"
#include "Auction.h"
#include "Auth.h"
#include "BRShopServer.h"
#include "BalthusEvent.h"
#include "BeautyManager.h"
#include "Boss.h"
#include "CConfig.h"
#include "CommCli.h"
#include "CommissionTrade.h"
#include "CompeteAgit.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "DBSocket.h"
#include "Default.h"
#include "Dominion.h"
#include "DynamicQuest.h"
#include "FieldCycle.h"
#include "Fortress.h"
#include "Friends.h"
#include "GameEventSystem.h"
#include "Henna.h"
#include "HtmlBuilder.h"
#include "IOCPMessageServer.h"
#include "IPBlock.h"
#include "InstantAgit.h"
#include "InstantZone.h"
#include "Item.h"
#include "ItemAnnounceInfo.h"
#include "ItemAnnouncer.h"
#include "ItemAssembler.h"
#include "KalieEvent.h"
#include "L2Packet.h"
#include "L2Time.h"
#include "LuckyGameSystem.h"
#include "MainEvent.h"
#include "MarketPriceList.h"
#include "Mentor.h"
#include "MiniGameAgit.h"
#include "MultiPartyCommandChannel.h"
#include "NPC.h"
#include "NoblessSystem.h"
#include "NpcServer.h"
#include "NpcSocket.h"
#include "ObjectDB.h"
#include "OlympiadService.h"
#include "OlympiadServiceProxy.h"
#include "OlympiadStuff.h"
#include "OwnThing.h"
#include "PCParam.h"
#include "ParserExpData.h"
#include "Party.h"
#include "Pledge.h"
#include "PledgeGameSystem.h"
#include "PledgeJoinSystem.h"
#include "PledgeStuff.h"
#include "Post.h"
#include "PotentialSkill.h"
#include "PremiumService.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Raid.h"
#include "Relay.h"
#include "ReservationAnnounceTimer.h"
#include "SSQ.h"
#include "SequenceJob.h"
#include "SerializedMessage.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/Config.h"
#include "Shared/src/DateTime.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/PledgeCrest.h"
#include "Shared/src/PledgeRanking.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/Vitality.h"
#include "Shared/src/proto/DBPacket.h"
#include "Skill.h"
#include "ToDoList.h"
#include "TradeItem.h"
#include "TrainingZoneSetting.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"
#include "WHeroSystem.h"

class CDBSocket;

// DBPacketEx.cpp-local concrete leaf. Current RTTI fixes this exact complete
// inheritance chain and proves that the class appends no state.
class CItemAssemblerPost final : public CItemAssemblerFromDB {
public:
    ~CItemAssemblerPost() override;

private:
    friend bool ReplyOpenPost(CDBSocket* socket, const unsigned char* packet);
};

CItemAssemblerPost::~CItemAssemblerPost() = default;


// Current L2Server object 0x02fc30f0, original DBPacketEx.cpp owner.
CDBExCallbackMap g_DBPacketTableEx(&DummyPacket, u"RP_EX_PROTOCOL");

namespace {

std::tm* DBPacketExLocalTime32(const std::int32_t value) noexcept { return runtime::crt::LocalTime32(value); }

template <std::size_t Capacity>
const char16_t* DBPacketExHostWideToTargetUtf16(
    const wchar_t* const source, std::array<char16_t, Capacity>& destination
) noexcept {
    destination.fill(u'\0');
    if (source == nullptr) {
        return destination.data();
    }

    std::size_t output = 0;
    for (std::size_t input = 0; source[input] != L'\0' && output + 1 < destination.size(); ++input) {
        const std::uint32_t value = static_cast<std::uint32_t>(source[input]);
        if (value <= 0xffffU) {
            destination[output++] = static_cast<char16_t>(value);
        } else if (value <= 0x10ffffU && output + 2 < destination.size()) {
            const std::uint32_t adjusted = value - 0x10000U;
            destination[output++] = static_cast<char16_t>(0xd800U + (adjusted >> 10U));
            destination[output++] = static_cast<char16_t>(0xdc00U + (adjusted & 0x3ffU));
        }
    }
    return destination.data();
}

// The source is UTF-16 already. Copy it within the destination.
template <std::size_t Capacity>
const char16_t* DBPacketExHostWideToTargetUtf16(
    const char16_t* source, std::array<char16_t, Capacity>& destination
) noexcept {
    destination.fill(u'\0');
    if (source == nullptr) {
        return destination.data();
    }
    for (std::size_t index = 0; source[index] != u'\0' && index + 1 < Capacity; ++index) {
        destination[index] = source[index];
    }
    return destination.data();
}

std::int32_t TrainingZoneLogDwordFromDouble(const double value) noexcept {
    // The current Win64 callbacks pass FVector doubles to three `%d`
    // variadic lanes. Preserve the low dword that the target formatter
    // consumes instead of normalizing the coordinates numerically.
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t lowBits = static_cast<std::uint32_t>(bits);
    std::int32_t result = 0;
    std::memcpy(&result, &lowBits, sizeof(result));
    return result;
}


bool DecodePacket(network::IPacket& packet, const unsigned char* source) {
    // Current L2Server 0x006704d4, original anonymous DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();
    network::Stream stream(const_cast<unsigned char*>(source), 0x4000);
    return packet.Decode(&stream);
}

template <typename Packet>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((used))
#endif
bool ProcessAtomicJobPacket(CDBSocket*, const unsigned char* source) {
    L2_GUARD_FUNCTION();

    Packet packet;
    if (DecodePacket(packet, source) && packet.nJobKey != 0) {
        AtomicJob* const atomicJob = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Pop(packet.nJobKey);
        PledgeStuff::DoJobPacket(atomicJob, &packet, true);
        PledgeStuff::DestroyJob(atomicJob);
    }
    return false;
}

template bool ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO>(CDBSocket*, const unsigned char*);
template bool ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK>(CDBSocket*, const unsigned char*);
template bool ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_ITEM_LOAD>(CDBSocket*, const unsigned char*);
template bool ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_ITEM_SAVE>(CDBSocket*, const unsigned char*);
template bool ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_ITEM_DELETE>(CDBSocket*, const unsigned char*);
template bool ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_RAID_GET_INFO_LIST>(CDBSocket*, const unsigned char*);
template bool ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON>(CDBSocket*, const unsigned char*);
template bool ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_RAID_INZONE_LOAD>(CDBSocket*, const unsigned char*);
template bool ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_MISSION_LOAD>(CDBSocket*, const unsigned char*);
template bool ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_ANNOUNCE>(CDBSocket*, const unsigned char*);
template bool ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_ANNOUNCE_SET>(CDBSocket*, const unsigned char*);

struct BrProductItemInfo {
    std::int32_t itemId;
    std::int32_t amount;
    std::int32_t weight;
    std::int32_t tradable;
};


// Current DBPacketEx.cpp translation-unit copy at L2Server .rdata
// 0x00f61cd8.
constexpr std::int32_t BaseCrystal[] = {0, 1, 2, 3, 4, 5, 5, 5, 8, 8, 8, 11, 12};

CSPointer<User> FindCommissionUser(const int characterId) {
    return g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
}

CCommissionItem MakeCommissionItem(const CItemDisassemblerFromCacheD& itemData) {
    return CCommissionItem(
        static_cast<std::uint32_t>(itemData.m_nDBID), static_cast<std::uint32_t>(itemData.m_nClassID), itemData.m_nAmount,
        itemData.m_nEnchanted, itemData.m_nDamaged, static_cast<std::uint16_t>(itemData.m_nIsBlessed), itemData.m_nIdent,
        static_cast<std::uint16_t>(itemData.m_nWished), itemData.m_nVariation1, itemData.m_nVariation2,
        itemData.m_nIntensiveItemClassID, itemData.m_nDuration, itemData.m_Attribute.m_nAttackType,
        itemData.m_Attribute.m_nAttackValue, itemData.m_Attribute.m_nDefendValue, itemData.m_nShapeShiftedClassID,
        itemData.m_mEnsoulInfo, itemData.m_hLocked, itemData.m_hUseCount
    );
}

void SendPledgeRecruitBoardDetail(
    CSPointer<User> user,
    const std::uint32_t pledgeServerId,
    const std::int32_t replyValue,
    const std::int32_t recruitType,
    const std::int32_t recruitPledgeType,
    const std::int32_t payloadSize,
    const unsigned char* const payload
) {
    user->GetSocket()->Send(
        "chddbdd", 0xfe, 0x142, pledgeServerId, replyValue, payloadSize, payload, recruitType, recruitPledgeType
    );
}

class PledgeTransactionReleaser final {
public:
    explicit PledgeTransactionReleaser(CPledge* const pledge) noexcept : m_pledge(pledge) {}

    ~PledgeTransactionReleaser() { m_pledge->EndTransaction(); }

    PledgeTransactionReleaser(const PledgeTransactionReleaser&) = delete;
    PledgeTransactionReleaser& operator=(const PledgeTransactionReleaser&) = delete;

private:
    CPledge* m_pledge;
};

#if defined(__clang__) || defined(__GNUC__)
#define L2_USED_DBPACKETEX_CALLBACK __attribute__((used))
#else
#define L2_USED_DBPACKETEX_CALLBACK
#endif

// Current DBPacketEx.cpp-local 0x0069dadc. HtmlBuilderPacketEx.cpp later
// overwrites the same opcode with its compiled local callback.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_MAIL_LIST_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ListMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current DBPacketEx.cpp-local 0x0069dc2c.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_READ_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ReadMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current DBPacketEx.cpp-local 0x0069dd8c.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_SEND_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    SendMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current DBPacketEx.cpp-local 0x0069dec8.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_SAVE_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    SaveMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current DBPacketEx.cpp-local 0x0069e004.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_GET_TEMP_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ModifyTempMailFormHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current DBPacketEx.cpp-local 0x0069e164.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_REPLY_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ReplyMailFormHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current DBPacketEx.cpp-local 0x0069e2c4.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_FORWARD_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ForwardMailFormHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current DBPacketEx.cpp-local 0x0069e424.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_MODIFY_TEMP_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ModifyTempMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current DBPacketEx.cpp-local 0x0069e550.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_MODIFY_SEND_TEMP_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ModifySendTempMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current DBPacketEx.cpp-local 0x0069e690.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_DELETE_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    DeleteMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current DBPacketEx.cpp-local 0x0069e7e4.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_ARCHIVE_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ArchiveMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0069e938, original static DBPacketEx.cpp owner.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_UNREAD_MAIL_COUNT_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    UnreadMailCountMsgSender builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0069ea68, original static DBPacketEx.cpp owner.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_SEND_PLEDGE_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    SendPledgeMailHandler builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0069ebb8, original static DBPacketEx.cpp owner.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_DELETE_FRIEND_BATCH_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    DeleteFriendBatchHandler builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0069ecf4, original static DBPacketEx.cpp owner.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_DELETE_BLOCK_BATCH_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    DeleteBlockBatchHandler builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0069ee30, original static DBPacketEx.cpp owner.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_GET_PLEDGE_ANNOUNCE_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    PledgeAnnounceHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0069ef84, original static DBPacketEx.cpp owner.
L2_USED_DBPACKETEX_CALLBACK bool ReplyHtmlBuilder_RP_SET_PLEDGE_ANNOUNCE_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    SetPledgeAnnounceHtmlHandler builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0067085c, original static DBPacketEx.cpp owner.
L2_USED_DBPACKETEX_CALLBACK bool ReplyLoadMarketPriceList(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int count = 0;
    packet = ::Disassemble(packet, "d", &count);

    int itemType = 0;
    int enchant = 0;
    double price = 0.0;
    for (int index = 0; index < count; ++index) {
        packet = ::Disassemble(packet, "ddf", &itemType, &enchant, &price);
        CMarketPriceList::Instance().Put(itemType, enchant, price);
        itemType = 0;
        enchant = 0;
        price = 0.0;
    }

    // Log.Add(LOG_NORMAL, L"Load Success[MarketPrice]")
    // L2_LOG_INFO(Log, L"Load Success[MarketPrice]")
    L2_LOG_INFO(Log, "Market-price reply processed: reported entries=%d", count);
    return false;
}

#undef L2_USED_DBPACKETEX_CALLBACK

} // namespace

bool ReplyRenewSubJob(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x006764e0, RP_EX 0x14.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t mode = 0;
    std::int32_t requestContext1 = 0;
    std::int32_t requestContext2 = 0;
    std::int32_t subJobId = 0;
    std::int32_t subJobType = 0;
    std::int32_t newClass = 0;
    std::int32_t renewReplyValue = 0;
    std::int32_t result = 0;
    const unsigned char* const cursor = ::Disassemble(
        packet, "ddddddddd", &userServerId, &mode, &requestContext1, &requestContext2, &subJobId, &subJobType, &newClass,
        &renewReplyValue, &result
    );

    CVitalData vitalData;
    vitalData.Disassemble(cursor);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"DBPacketEx.cpp",
        //     0x15c,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"DBPacketEx.cpp",
        //     0x15c,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Subjob renewal reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    if (result == 0) {
        user->m_data->m_bIsActive = true;
        runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"renew subjob failed [%s][id:%d][type:%d][class:%d]",
        //     user->GetName(),
        //     subJobId,
        //     subJobType,
        //     newClass)
        // L2_LOG_ERROR(Log,
        //     L"renew subjob failed [%s][id:%d][type:%d][class:%d]",
        //     user->GetName(),
        //     subJobId,
        //     subJobType,
        //     newClass)
        // L2_LOG_ERROR(Log,
        //     "Subjob renewal failed: user=%s, subjob=%d, type=%d, requested class=%d",
        //     ::WideToUtf8(user->GetName()).c_str(), subJobId, subJobType, newClass)
        L2_LOG_ERROR(
            Log, "Subjob renewal failed: user=%s, subjob=%d, type=%d, requested class=%d",
            ::Utf16ToUtf8(user->GetName()).c_str(), subJobId, subJobType, newClass
        );
    } else {
        if (subJobType == 1) {
            user->InitDualClass(subJobId, newClass);
            user->OnChangeSubJobEnd(SubJob::RENEW_DUAL_CLASS, vitalData);
            user->GetSocket()->Send(
                "chcchch", 0x62, SystemMessageId::SUBCLASS_UPGRADED_TO_DUAL_CLASS, 2, 0xf,
                static_cast<std::int16_t>(renewReplyValue), 0xf, static_cast<std::int16_t>(newClass)
            );
        } else {
            user->InitSubJob(subJobId, newClass);
            user->OnChangeSubJobEnd(SubJob::RENEW_SUBJOB, vitalData);
            user->GetSocket()->Send(
                "chcch", 0x62, SystemMessageId::NEW_SUBCLASS_ACQUIRED, 1, 0xf, static_cast<std::int16_t>(newClass)
            );
        }
        user->SendVoteSystemInfoPacket();
    }

    if (mode == 0) {
        g_npcserver.Send("cdddd", 0x45, requestContext1, requestContext2, renewReplyValue, result != 0 ? newClass : -1);
    }
    user->SendUserSubJobInfo(SubJob::CREATE_NEW_CLASS);
    db.LoadPeriodicHennaInfo(user);
    return false;
}

bool ReplyLoadPledgeSiegeKillDeathCount(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x0067680c, RP_EX 0x1d.
    L2_GUARD_FUNCTION();

    std::int32_t ignoredCastleServerId;
    std::int32_t count;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &ignoredCastleServerId, &count);
    static_cast<void>(ignoredCastleServerId);

    for (std::int32_t index = 0; index < count; ++index) {
        std::int32_t pledgeDatabaseId;
        std::int32_t killCount;
        std::int32_t deathCount;
        cursor = ::Disassemble(cursor, "ddd", &pledgeDatabaseId, &killCount, &deathCount);

        CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
        if (pledge) {
            pledge->InitSiegeKillDeathCountFromDb(killCount, deathCount);
        }
    }
    return false;
}

bool TeleportPartyPacket(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x0067ea18, RP_EX 0x1e.
    L2_GUARD_FUNCTION();

    std::int32_t characterDatabaseId = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    ::Disassemble(packet, "dddd", &characterDatabaseId, &x, &y, &z);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterDatabaseId));
    if (!user) {
        return false;
    }

    CParty* const party = user->GetParty();
    if (party == nullptr) {
        user->StopMove();
        user->TeleportToLocation(x, y, z, 0, 0x10000, TeleportSourceType::PartyPacket, nullptr, 0);
        return false;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    const int memberCount = static_cast<int>(members.size());
    for (int index = 0; index < memberCount; ++index) {
        const double randomX = ::genrand64_real2();
        const double randomY = ::genrand64_real2();
        User* const member = runtime::CheckedVectorAt(members, index);
        member->StopMove();
        member->TeleportToLocation(
            runtime::AddInt32Wrapping(x, static_cast<int>(randomX * 21.0 + 0.0)),
            runtime::AddInt32Wrapping(y, static_cast<int>(randomY * 21.0 + 0.0)), z, 0, 0x10000,
            TeleportSourceType::PartyPacket, nullptr, 0
        );
    }

    user->StopMove();
    user->TeleportToLocation(x, y, z, 0, 0x10000, TeleportSourceType::PartyPacket, nullptr, 0);
    return false;
}

bool ReplyLoadPCCafePointPacket(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x006a0ef8, RP_EX 0x1f.
    L2_GUARD_FUNCTION();

    std::int32_t characterDatabaseId = 0;
    std::int32_t point = 0;
    std::int32_t type = 0;
    std::int32_t savedDate = 0;
    ::Disassemble(packet, "dddd", &characterDatabaseId, &point, &type, &savedDate);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterDatabaseId));
    if (!user) {
        return false;
    }

    if (type == 1) {
        user->SendPCCafePointInfo(point, 0, true, false, 0);
        CEvent* const baseEvent = Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_PCCAFE_SETTING);
        if (baseEvent != nullptr) {
            auto* const event = static_cast<CEvent_PCCafe*>(baseEvent);
            if (user->CanGetPCCafePoint() && event->GetUserEventEnabled(CEvent_PCCafe::ECT_DAILYGIVE) &&
                event->CanGiveDailyPoint(user->GetDBID())) {
                const int dailyPoint = event->GetDailyGivePoint();
                user->UpdatePoint(dailyPoint, PT_PCCAFE_POINT, DJWP_ATTENTION_ADD, 0);

                CUserSocket* const userSocket = user->GetSocket();
                const std::wstring pcCafeCode = Utf16ToWide(reinterpret_cast<const char16_t*>(userSocket->GetPCCafeCodeW()));
                CLogFormatter formatter;
                formatter.WriteLog1_1(
                    0x3cb, user.get(), nullptr, nullptr, WideToUtf16(const_cast<wchar_t*>(pcCafeCode.c_str())), nullptr,
                    dailyPoint, runtime::AddInt32Wrapping(dailyPoint, user->m_data->m_nPCCafePoint), 0, 0, 0, 0, 0
                );
            }
        }
    } else if (type == 3 && point >= 0) {
        user->SetHomePCCafeCount(point, savedDate);
    }
    return false;
}

bool ReplySavePCCafePointPacket(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x0067edc4, RP_EX 0x20.
    L2_GUARD_FUNCTION();

    std::int32_t characterDatabaseId = 0;
    std::int32_t point = 0;
    std::int32_t messageType = 0;
    std::int32_t type = 0;
    std::int32_t savedDate = 0;
    std::int32_t messageId = 0;
    ::Disassemble(packet, "dddddd", &characterDatabaseId, &point, &messageType, &type, &savedDate, &messageId);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterDatabaseId));
    if (user) {
        if (type == 1) {
            user->SendPCCafePointInfo(point, messageType, false, false, messageId);
        } else if (type == 3) {
            user->SetHomePCCafeCount(point, savedDate);
        }
    }
    return false;
}

bool ReplyNPCPickItemPacket(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00676eb4, RP_EX 0x21.
    L2_GUARD_FUNCTION();

    std::int32_t success = 0;
    std::int32_t ignoredNpcId = 0;
    std::int32_t itemDatabaseId = 0;
    std::int32_t itemServerId = 0;
    std::int64_t ignoredAmount = 0;
    ::Disassemble(packet, "ddddQ", &success, &ignoredNpcId, &itemDatabaseId, &itemServerId, &ignoredAmount);
    static_cast<void>(ignoredNpcId);
    static_cast<void>(ignoredAmount);

    if (success == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NPCPickItem failed at cached",
        //     L"DBPacketEx.cpp",
        //     0x42d)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] NPCPickItem failed at cached",
        //     L"DBPacketEx.cpp",
        //     0x42d)
        L2_LOG_ERROR(
            Log, "CacheD rejected NPC item pickup: item object=%u, database ID=%u", static_cast<unsigned int>(itemServerId),
            static_cast<unsigned int>(itemDatabaseId)
        );
        return false;
    }

    CItem* const item = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(itemServerId));
    if (item == nullptr) {
        return false;
    }

    const std::uint32_t currentDatabaseId = item->GetDBID();
    if (currentDatabaseId == static_cast<std::uint32_t>(itemDatabaseId)) {
        item->Delete();
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Item[cls:%d srv:%d db:%d] has disappeared. "
        //     L"Picking item cannot be continued",
        //     item->m_data->m_nClassID,
        //     static_cast<std::int32_t>(currentDatabaseId),
        //     itemDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"Item[cls:%d srv:%d db:%d] has disappeared. "
        //     L"Picking item cannot be continued",
        //     item->m_data->m_nClassID,
        //     static_cast<std::int32_t>(currentDatabaseId),
        //     itemDatabaseId)
        L2_LOG_ERROR(
            Log, "NPC item pickup cannot finish: item class=%d, object=%u, current database ID=%u, reply database ID=%u",
            item->m_data->m_nClassID, static_cast<unsigned int>(itemServerId), static_cast<unsigned int>(currentDatabaseId),
            static_cast<unsigned int>(itemDatabaseId)
        );
    }
    return false;
}

bool ReplyCreateAcademy(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00677030, RP_EX 0x24.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t npcServerId = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::int32_t result = 0;
    std::array<char16_t, 25> academyName{};
    ::Disassemble(packet, "ddddS", &userServerId, &npcServerId, &pledgeDatabaseId, &result, 0x32, academyName.data());

    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = userCreature != nullptr && userCreature->IsPC() ? userCreature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x4a2,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x4a2,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Academy creation reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    CNPC* npc = nullptr;
    if (npcServerId != 0) {
        CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcServerId));
        npc = npcCreature != nullptr && npcCreature->IsNPC() ? npcCreature->CastNPC() : nullptr;
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x4ad,
            //     npcServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x4ad,
            //     npcServerId)
            L2_LOG_ERROR(
                Log, "Academy creation reply ignored: NPC object %u is unavailable or is not an NPC",
                static_cast<unsigned int>(npcServerId)
            );
            return false;
        }
    }

    if (result == 0) {
        if (npc != nullptr) {
            g_npcserver.Send("cdddd", 0x55, user->Id(), user->m_data->index, npc->m_data->index, 0);
        }
        return false;
    }

    CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
    if (!pledge) {
        return false;
    }

    std::wstring hostAcademyName = Utf16ToWide(academyName.data());
    pledge->SetSubPledge(ACADEMY, hostAcademyName.data(), 0, false);
    pledge->SendSubPledgeUpdatedPacket(ACADEMY);
    pledge->AcquireSkillForAcademy(pledge->GetMasterDbId(), true);
    if (npc != nullptr) {
        g_npcserver.Send("cdddd", 0x55, user->Id(), user->m_data->index, npc->m_data->index, 2);
    }
    return false;
}

bool ReplyCreateSubPledge(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x0067f780, RP_EX 0x31.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t npcServerId;
    std::array<char16_t, 25> subPledgeName{};
    std::int32_t masterCharacterId;
    std::int32_t actorCharacterId;
    std::int32_t pledgeDatabaseId;
    std::int32_t pledgeTypeValue;
    std::int32_t result = 0;
    ::Disassemble(
        packet, "ddSddddd", &userServerId, &npcServerId, 0x32, subPledgeName.data(), &masterCharacterId, &actorCharacterId,
        &pledgeDatabaseId, &pledgeTypeValue, &result
    );

    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = userCreature != nullptr && userCreature->IsPC() ? userCreature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x5ed,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x5ed,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Subpledge creation reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    CNPC* npc = nullptr;
    if (npcServerId != 0) {
        CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcServerId));
        npc = npcCreature != nullptr && npcCreature->IsNPC() ? npcCreature->CastNPC() : nullptr;
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x5f8,
            //     npcServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x5f8,
            //     npcServerId)
            L2_LOG_ERROR(
                Log,
                "Subpledge creation reply continues without NPC notification: object %u is unavailable or is not an NPC",
                static_cast<unsigned int>(npcServerId)
            );
        }
    }

    if (result == 0) {
        if (npc != nullptr) {
            g_npcserver.Send(
                "cddddddS", 0x57, user->Id(), user->m_data->index, npc->m_data->index, 0, pledgeTypeValue, masterCharacterId,
                subPledgeName.data()
            );
        }
        return false;
    }

    const std::int32_t worldId = runtime::process::CurrentDatabaseId();
    // The current callback extracts a raw pointer from the returned CSPointer
    // temporary and releases that temporary before the following operations.
    CPledge* const pledge = CPledge::FindPledge(worldId, pledgeDatabaseId).get();
    if (pledge == nullptr) {
        return false;
    }

    CSPointer<User> master = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(worldId, masterCharacterId));
    if (master) {
        pledge->ApplyPledgeSkillsTo(master.get(), false, false);
    }

    const PledgeType pledgeType = static_cast<PledgeType>(pledgeTypeValue);
    std::wstring hostSubPledgeName = Utf16ToWide(subPledgeName.data());
    pledge->SetSubPledge(pledgeType, hostSubPledgeName.data(), static_cast<std::uint32_t>(masterCharacterId), false);
    pledge->SendSubPledgeUpdatedPacket(pledgeType);
    if (npc != nullptr) {
        g_npcserver.Send(
            "cddddddS", 0x57, user->Id(), user->m_data->index, npc->m_data->index, 2, pledgeTypeValue, masterCharacterId,
            subPledgeName.data()
        );
    }
    return false;
}

bool ReplyUpdateQuest(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x0067fb18, RP_EX 0x32.
    L2_GUARD_FUNCTION();

    std::int32_t characterDatabaseId;
    std::int32_t updateType = 0;
    std::int32_t questId = 0;
    std::int32_t state1 = 0;
    std::int32_t state2 = 0;
    std::int32_t state3 = 0;
    std::int32_t state4 = 0;
    // The current callback supplies seven destinations to a five-field
    // format. Disassemble consumes only through state2, leaving the final
    // two target-zeroed lanes unchanged.
    ::Disassemble(packet, "ddddd", &characterDatabaseId, &updateType, &questId, &state1, &state2, &state3, &state4);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterDatabaseId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot find user from g_UserDBIDMap, world id[%d] dbid[%d]",
        //     runtime::process::CurrentDatabaseId(),
        //     characterDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"Cannot find user from g_UserDBIDMap, world id[%d] dbid[%d]",
        //     runtime::process::CurrentDatabaseId(),
        //     characterDatabaseId)
        L2_LOG_ERROR(
            Log,
            "Quest update reply ignored: character is unavailable; world=%d, character DB ID=%d, quest=%d, update type=%d",
            runtime::process::CurrentDatabaseId(), characterDatabaseId, questId, updateType
        );
        return false;
    }

    {
        L2_GUARD(*user->m_lock);
        if (updateType == 1) {
            user->m_data->SetOneTimeFlag(questId, state1 == 1);
        } else if (updateType == 2) {
            user->m_data->SetMemoState(questId, 0, state1);
            user->m_data->SetMemoState(questId, 1, state2);
            user->m_data->SetMemoState(questId, 2, state3);
            user->m_data->SetMemoState(questId, 3, state4);
        } else if (updateType == 3) {
            user->m_data->RemoveMemo(questId);
        }
    }
    return false;
}

bool ReplyUpdateSubPledgeMaster(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00677984, RP_EX 0x33.
    L2_GUARD_FUNCTION();

    std::int32_t npcServerId;
    std::int32_t userServerId;
    std::int32_t pledgeDatabaseId;
    std::int32_t pledgeTypeValue;
    std::int32_t masterCharacterId;
    std::int32_t result;
    ::Disassemble(
        packet, "dddddd", &npcServerId, &userServerId, &pledgeDatabaseId, &pledgeTypeValue, &masterCharacterId, &result
    );
    static_cast<void>(masterCharacterId);

    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = userCreature != nullptr && userCreature->IsPC() ? userCreature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x676,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x676,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Subpledge master update reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    CNPC* npc = nullptr;
    if (npcServerId != 0) {
        CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcServerId));
        npc = npcCreature != nullptr && npcCreature->IsNPC() ? npcCreature->CastNPC() : nullptr;
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x680,
            //     npcServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x680,
            //     npcServerId)
            L2_LOG_ERROR(
                Log,
                "Subpledge master update reply continues without NPC notification: object %u is unavailable or is not an "
                "NPC",
                static_cast<unsigned int>(npcServerId)
            );
        }
    }

    if (result == 0) {
        if (npc != nullptr) {
            g_npcserver.Send("cddddd", 0x59, user->Id(), user->m_data->index, pledgeTypeValue, npc->m_data->index, 0);
        }
        return false;
    }

    // The current callback releases the returned CSPointer temporary before
    // using the extracted raw pledge pointer.
    CPledge* const pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId).get();
    if (pledge != nullptr) {
        pledge->SendSubPledgeUpdatedPacket(static_cast<PledgeType>(pledgeTypeValue));
        if (npc != nullptr) {
            g_npcserver.Send("cddddd", 0x59, user->Id(), user->m_data->index, pledgeTypeValue, npc->m_data->index, 1);
        }
    }
    return false;
}

bool ReplyUpgradeSubPledgeMemberCount(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00677c3c, RP_EX 0x34.
    L2_GUARD_FUNCTION();

    std::int32_t npcServerId;
    std::int32_t userServerId;
    std::int32_t pledgeDatabaseId;
    std::int32_t pledgeTypeValue;
    std::int32_t result;
    ::Disassemble(packet, "ddddd", &npcServerId, &userServerId, &pledgeDatabaseId, &pledgeTypeValue, &result);

    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = userCreature != nullptr && userCreature->IsPC() ? userCreature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x6b1,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x6b1,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Subpledge capacity upgrade reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcServerId));
    CNPC* const npc = npcCreature != nullptr && npcCreature->IsNPC() ? npcCreature->CastNPC() : nullptr;
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid npc[%x]",
        //     L"DBPacketEx.cpp",
        //     0x6b8,
        //     npcServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid npc[%x]",
        //     L"DBPacketEx.cpp",
        //     0x6b8,
        //     npcServerId)
        L2_LOG_ERROR(
            Log, "Subpledge capacity upgrade reply ignored: NPC object %u is unavailable or is not an NPC",
            static_cast<unsigned int>(npcServerId)
        );
        return false;
    }

    if (result != 0) {
        // Preserve the same temporary-to-raw pledge lifetime as the target.
        CPledge* const pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId).get();
        CSubPledge* const subPledge =
            pledge != nullptr ? pledge->GetSubPledge(static_cast<PledgeType>(pledgeTypeValue)) : nullptr;
        if (subPledge != nullptr) {
            subPledge->m_bMemberCountUpgrade = true;
            g_npcserver.Send("cddddd", 0x6c, npc->m_data->index, user->Id(), user->m_data->index, pledgeTypeValue, result);
            return false;
        }
    }

    g_npcserver.Send("cddddd", 0x6c, npc->m_data->index, user->Id(), user->m_data->index, pledgeTypeValue, 0);
    return false;
}

bool ReplyGraduateAcademy(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00677ee8, RP_EX 0x35.
    L2_GUARD_FUNCTION();

    std::int32_t pledgeDatabaseId = 0;
    std::int32_t userServerId = 0;
    std::int32_t memberDatabaseId = 0;
    ::Disassemble(packet, "ddd", &pledgeDatabaseId, &userServerId, &memberDatabaseId);
    static_cast<void>(memberDatabaseId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x6e2,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x6e2,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Academy graduation reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    // Preserve the current temporary-to-raw pledge lifetime.
    CPledge* const pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId).get();
    if (pledge != nullptr) {
        user->m_data->m_nAcademyPledgeDBID = static_cast<std::uint32_t>(pledgeDatabaseId);
        pledge->ReplyGraduateAcademy(user);
    }
    return false;
}

bool ReplySetAcademyMaster(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x0067fde0, RP_EX 0x36.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::int32_t academyMemberDatabaseId;
    std::int32_t academyMasterDatabaseId = 0;
    std::int32_t result = 0;
    ::Disassemble(
        packet, "ddddd", &userServerId, &pledgeDatabaseId, &academyMemberDatabaseId, &academyMasterDatabaseId, &result
    );

    // Preserve the current temporary-to-raw pledge lifetime.
    CPledge* const pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId).get();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "set/unset academy master failed. academy_member[%d] "
        //     "master[%d] pledge[%d]",
        //     academyMemberDatabaseId,
        //     academyMasterDatabaseId,
        //     pledgeDatabaseId)
        // L2_LOG_ERROR(Log,
        //     "set/unset academy master failed. academy_member[%d] "
        //     "master[%d] pledge[%d]",
        //     academyMemberDatabaseId,
        //     academyMasterDatabaseId,
        //     pledgeDatabaseId)
        L2_LOG_ERROR(
            Log, "Academy sponsor reply ignored: clan %d is unavailable; member DB ID=%d, sponsor DB ID=%d",
            pledgeDatabaseId, academyMemberDatabaseId, academyMasterDatabaseId
        );
        return false;
    }

    if (result == 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user[%d]",
            //     L"DBPacketEx.cpp",
            //     0x70b,
            //     userServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid user[%d]",
            //     L"DBPacketEx.cpp",
            //     0x70b,
            //     userServerId)
            L2_LOG_ERROR(
                Log, "Academy sponsor failure reply cannot be shown: user object %u is unavailable or is not a player",
                static_cast<unsigned int>(userServerId)
            );
            return false;
        }
        pledge->ViewPledgeMemberInfo(user, pledge->GetMemberName(academyMasterDatabaseId));
        pledge->EndTransaction();
        return false;
    }

    if (result != 1) {
        pledge->EndTransaction();
        return false;
    }

    if (academyMasterDatabaseId == 0) {
        const std::uint32_t oldMasterDatabaseId = pledge->GetAcademyMasterDbId(academyMemberDatabaseId);
        std::wstring academyMemberName(pledge->GetMemberName(academyMemberDatabaseId));
        if (!pledge->SetAcademyMaster(WideToUtf16(academyMemberName).c_str(), u"")) {
            // Log.Add(
            //     LOG_ERROR,
            //     "unset academy master succeeded at CacheD, but failed "
            //     "at server. academy_member[%d] master[%d] pledge[%d]",
            //     academyMemberDatabaseId,
            //     academyMasterDatabaseId,
            //     pledgeDatabaseId)
            // L2_LOG_ERROR(Log,
            //     "unset academy master succeeded at CacheD, but failed "
            //     "at server. academy_member[%d] master[%d] pledge[%d]",
            //     academyMemberDatabaseId,
            //     academyMasterDatabaseId,
            //     pledgeDatabaseId)
            L2_LOG_ERROR(
                Log,
                "CacheD confirmed academy sponsor removal, but the local clan update failed: member DB ID=%d, requested "
                "sponsor DB ID=%d, clan=%d",
                academyMemberDatabaseId, academyMasterDatabaseId, pledgeDatabaseId
            );
            pledge->EndTransaction();
            return false;
        }

        {
            CSPointer<User> academyMember = g_UserDBIDMap.FindObject(
                std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), academyMemberDatabaseId)
            );
            if (academyMember) {
                academyMember->m_data->m_bIsRelated = false;
                academyMember->m_data->m_nRelatedIndex = 0;
            }
            pledge->UpdatePledgeMemberListPacket(academyMemberDatabaseId);

            CSPointer<User> academyMaster = g_UserDBIDMap.FindObject(
                std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), oldMasterDatabaseId)
            );
            if (academyMaster) {
                academyMaster->m_data->m_bIsRelated = false;
                academyMaster->m_data->m_nRelatedIndex = 0;
            }
            pledge->UpdatePledgeMemberListPacket(oldMasterDatabaseId);

            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
            User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
            if (user == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Invalid user[%d]",
                //     L"DBPacketEx.cpp",
                //     0x72f,
                //     userServerId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Invalid user[%d]",
                //     L"DBPacketEx.cpp",
                //     0x72f,
                //     userServerId)
                L2_LOG_ERROR(
                    Log,
                    "Academy sponsor removal applied, but member info cannot be shown: user object %u is unavailable or is "
                    "not a player",
                    static_cast<unsigned int>(userServerId)
                );
                return false;
            }
            pledge->ViewPledgeMemberInfo(user, pledge->GetMemberName(academyMasterDatabaseId));
        }
        pledge->EndTransaction();
        return false;
    }

    std::wstring academyMasterName(pledge->GetMemberName(academyMasterDatabaseId));
    std::wstring academyMemberName(pledge->GetMemberName(academyMemberDatabaseId));
    if (!pledge->SetAcademyMaster(WideToUtf16(academyMemberName).c_str(), WideToUtf16(academyMasterName).c_str())) {
        // Log.Add(
        //     LOG_ERROR,
        //     "set academy master succeeded at CacheD, but failed at "
        //     "server. academy_member[%d] master[%d] pledge[%d]",
        //     academyMemberDatabaseId,
        //     academyMasterDatabaseId,
        //     pledgeDatabaseId)
        // L2_LOG_ERROR(Log,
        //     "set academy master succeeded at CacheD, but failed at "
        //     "server. academy_member[%d] master[%d] pledge[%d]",
        //     academyMemberDatabaseId,
        //     academyMasterDatabaseId,
        //     pledgeDatabaseId)
        L2_LOG_ERROR(
            Log,
            "CacheD confirmed academy sponsor assignment, but the local clan update failed: member DB ID=%d, sponsor DB "
            "ID=%d, clan=%d",
            academyMemberDatabaseId, academyMasterDatabaseId, pledgeDatabaseId
        );
        pledge->EndTransaction();
        return false;
    }

    {
        CSPointer<User> academyMember = g_UserDBIDMap.FindObject(
            std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), academyMemberDatabaseId)
        );
        if (academyMember) {
            academyMember->m_data->m_bIsRelated = true;
        }
        pledge->UpdatePledgeMemberListPacket(academyMemberDatabaseId);

        CSPointer<User> academyMaster = g_UserDBIDMap.FindObject(
            std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), academyMasterDatabaseId)
        );
        if (academyMaster) {
            academyMaster->m_data->m_bIsRelated = true;
        }
        pledge->UpdatePledgeMemberListPacket(academyMasterDatabaseId);

        if (academyMember && academyMaster) {
            academyMember->m_data->m_nRelatedIndex = static_cast<std::uint32_t>(academyMaster->m_data->index);
            academyMaster->m_data->m_nRelatedIndex = static_cast<std::uint32_t>(academyMember->m_data->index);
        }

        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user[%d]",
            //     L"DBPacketEx.cpp",
            //     0x758,
            //     userServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid user[%d]",
            //     L"DBPacketEx.cpp",
            //     0x758,
            //     userServerId)
            L2_LOG_ERROR(
                Log,
                "Academy sponsor assignment applied, but member info cannot be shown: user object %u is unavailable or is "
                "not a player",
                static_cast<unsigned int>(userServerId)
            );
            return false;
        }
        pledge->ViewPledgeMemberInfo(user, pledge->GetMemberName(academyMasterDatabaseId));
    }
    pledge->EndTransaction();
    return false;
}

bool ReplyPledgeMasterTransfer(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00678084, RP_EX 0x37.
    L2_GUARD_FUNCTION();

    std::int32_t npcServerId = 0;
    std::int32_t userServerId = 0;
    std::int32_t characterDatabaseId = 0;
    std::int32_t pledgeServerId = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::array<char16_t, 25> newMasterName{};
    std::int32_t result = 0;
    ::Disassemble(
        packet, "dddddSd", &npcServerId, &userServerId, &characterDatabaseId, &pledgeServerId, &pledgeDatabaseId, 0x32,
        newMasterName.data(), &result
    );
    static_cast<void>(characterDatabaseId);
    static_cast<void>(pledgeDatabaseId);

    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = userCreature != nullptr && userCreature->IsPC() ? userCreature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x786,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x786,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Clan leadership transfer reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    CPledge* const pledge = CPledge::GetObjectW(static_cast<std::uint32_t>(pledgeServerId));
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%d]",
        //     L"DBPacketEx.cpp",
        //     0x78d,
        //     pledgeServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid pledge[%d]",
        //     L"DBPacketEx.cpp",
        //     0x78d,
        //     pledgeServerId)
        L2_LOG_ERROR(
            Log, "Clan leadership transfer reply ignored: clan object %u is unavailable",
            static_cast<unsigned int>(pledgeServerId)
        );
        return false;
    }
    pledge->SetWaitingTransferMaster(true);

    if (npcServerId != 0) {
        CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcServerId));
        CNPC* const npc = npcCreature != nullptr && npcCreature->IsNPC() ? npcCreature->CastNPC() : nullptr;
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x798,
            //     npcServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x798,
            //     npcServerId)
            L2_LOG_ERROR(
                Log, "Clan leadership transfer reply cannot notify NPC: object %u is unavailable or is not an NPC",
                static_cast<unsigned int>(npcServerId)
            );
        } else {
            g_npcserver.Send(
                "cdddSd", 0x5b, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()),
                newMasterName.data(), result
            );
        }
    }
    return false;
}

bool ReplyPledgeMasterTransferCancel(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00678318, RP_EX 0x38.
    L2_GUARD_FUNCTION();

    std::int32_t npcServerId = 0;
    std::int32_t userServerId = 0;
    std::int32_t characterDatabaseId = 0;
    std::int32_t pledgeServerId = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::int32_t result = 0;
    ::Disassemble(
        packet, "dddddd", &npcServerId, &userServerId, &characterDatabaseId, &pledgeServerId, &pledgeDatabaseId, &result
    );
    static_cast<void>(characterDatabaseId);
    static_cast<void>(pledgeDatabaseId);

    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = userCreature != nullptr && userCreature->IsPC() ? userCreature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x7b4,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x7b4,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Clan leadership transfer cancellation reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    CPledge* const pledge = CPledge::GetObjectW(static_cast<std::uint32_t>(pledgeServerId));
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%d]",
        //     L"DBPacketEx.cpp",
        //     0x7bb,
        //     pledgeServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid pledge[%d]",
        //     L"DBPacketEx.cpp",
        //     0x7bb,
        //     pledgeServerId)
        L2_LOG_ERROR(
            Log, "Clan leadership transfer cancellation reply ignored: clan object %u is unavailable",
            static_cast<unsigned int>(pledgeServerId)
        );
        return false;
    }
    pledge->SetWaitingTransferMaster(false);

    if (npcServerId != 0) {
        CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcServerId));
        CNPC* const npc = npcCreature != nullptr && npcCreature->IsNPC() ? npcCreature->CastNPC() : nullptr;
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x7c7,
            //     npcServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x7c7,
            //     npcServerId)
            L2_LOG_ERROR(
                Log,
                "Clan leadership transfer cancellation reply cannot notify NPC: object %u is unavailable or is not an NPC",
                static_cast<unsigned int>(npcServerId)
            );
        } else {
            g_npcserver.Send(
                "cdddd", 0x5c, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()), result
            );
        }
    }
    return false;
}

bool ReplyPCCafeCouponCheck(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00678578, RP_EX 0x39.
    L2_GUARD_FUNCTION();

    char result = 0;
    std::int32_t userServerId = 0;
    std::int32_t couponPart1 = 0;
    std::int32_t couponPart2 = 0;
    std::int32_t couponPart3 = 0;
    std::int32_t couponPart4 = 0;
    ::Disassemble(packet, "cddddd", &result, &userServerId, &couponPart1, &couponPart2, &couponPart3, &couponPart4);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x7df,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x7df,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "PC cafe coupon check reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    user->RepliedPCCafeCouponResult(result, couponPart1, couponPart2, couponPart3, couponPart4);
    return false;
}

bool ReplyRenameSubpledge(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x006786f8, RP_EX 0x3a.
    L2_GUARD_FUNCTION();

    std::int32_t npcServerId = 0;
    std::int32_t userServerId = 0;
    std::int32_t characterDatabaseId = 0;
    std::int32_t pledgeServerId = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::int32_t pledgeTypeValue;
    std::array<char16_t, 25> newName{};
    std::int32_t result;
    ::Disassemble(
        packet, "ddddddSd", &npcServerId, &userServerId, &characterDatabaseId, &pledgeServerId, &pledgeDatabaseId,
        &pledgeTypeValue, 0x32, newName.data(), &result
    );
    static_cast<void>(characterDatabaseId);
    static_cast<void>(pledgeDatabaseId);

    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = userCreature != nullptr && userCreature->IsPC() ? userCreature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x7fb,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%d]",
        //     L"DBPacketEx.cpp",
        //     0x7fb,
        //     userServerId)
        L2_LOG_ERROR(
            Log,
            "Subpledge rename reply continues without NPC notification: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
    }

    CNPC* npc = nullptr;
    if (npcServerId != 0) {
        CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcServerId));
        npc = npcCreature != nullptr && npcCreature->IsNPC() ? npcCreature->CastNPC() : nullptr;
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x804,
            //     npcServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBPacketEx.cpp",
            //     0x804,
            //     npcServerId)
            L2_LOG_ERROR(
                Log, "Subpledge rename reply continues without NPC notification: object %u is unavailable or is not an NPC",
                static_cast<unsigned int>(npcServerId)
            );
        }
    }

    if (result == 1) {
        CPledge* const pledge = CPledge::GetObjectW(static_cast<std::uint32_t>(pledgeServerId));
        if (pledge != nullptr) {
            std::wstring hostName = Utf16ToWide(newName.data());
            const PledgeType pledgeType = static_cast<PledgeType>(pledgeTypeValue);
            pledge->RenameSubpledge(pledgeType, hostName.data());
            pledge->SendSubPledgeUpdatedPacket(pledgeType);
            if (npc != nullptr && user != nullptr) {
                g_npcserver.Send(
                    "cddddSd", 0x5e, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()),
                    pledgeTypeValue, newName.data(), 1
                );
            }
        }
    } else if (npc != nullptr && user != nullptr) {
        g_npcserver.Send(
            "cddddSd", 0x5e, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()), pledgeTypeValue,
            newName.data(), result
        );
    }
    return false;
}

bool ReplyLoadTransferredPledgeMaster(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x006710bc, RP_EX 0x3b.
    L2_GUARD_FUNCTION();
    commcli.RequestPledgeMasterTransfer(packet);
    return false;
}

bool ReplyLoadNobless(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x0067533c, RP_EX 0x15.
    L2_GUARD_FUNCTION();

    std::int32_t packetNumber;
    std::int32_t startUp;
    std::int32_t ignoredRequestContext;
    std::int32_t count;
    const unsigned char* cursor = ::Disassemble(packet, "dddd", &packetNumber, &startUp, &ignoredRequestContext, &count);
    static_cast<void>(ignoredRequestContext);

    for (std::int32_t index = 0; index < count; ++index) {
        std::int32_t characterDatabaseId;
        std::int32_t noblessType;
        std::int32_t ignoredHeroType;
        std::int32_t ignoredWinCount;
        std::int32_t ignoredPreviousPoint;
        std::int32_t ignoredOlympiadPoint;
        std::int32_t ignoredMatchCount;
        std::int32_t ignoredOlympiadWinCount;
        std::int32_t ignoredOlympiadLoseCount;
        std::int32_t ignoredOlympiadTradePoint;
        cursor = ::Disassemble(
            cursor, "dddddddddd", &characterDatabaseId, &noblessType, &ignoredHeroType, &ignoredWinCount,
            &ignoredPreviousPoint, &ignoredOlympiadPoint, &ignoredMatchCount, &ignoredOlympiadWinCount,
            &ignoredOlympiadLoseCount, &ignoredOlympiadTradePoint
        );
        static_cast<void>(ignoredHeroType);
        static_cast<void>(ignoredWinCount);
        static_cast<void>(ignoredPreviousPoint);
        static_cast<void>(ignoredOlympiadPoint);
        static_cast<void>(ignoredMatchCount);
        static_cast<void>(ignoredOlympiadWinCount);
        static_cast<void>(ignoredOlympiadLoseCount);
        static_cast<void>(ignoredOlympiadTradePoint);

        NoblessSystem::inst().SetNoblessInfoFromDB(characterDatabaseId, noblessType);
        if (startUp == 0 && noblessType != 0) {
            WOlympiadServiceProxyUtil::RegisterToOlympiad(characterDatabaseId);
        }
        if (startUp != 0) {
            g_JobFinishSignal.SetNoblessLoaded();
        }
    }

    CLogFormatter formatter;
    formatter.WriteLog4(0x1f7b, nullptr, nullptr, nullptr, nullptr, nullptr, packetNumber, count, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    return false;
}

bool ReplySetPledgeEmblem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00676958, RP_EX 0x16.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::uint32_t pledgeServerId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "ddd", &userServerId, &pledgeServerId, &result);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"DBPacketEx.cpp",
        //     0x277,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"DBPacketEx.cpp",
        //     0x277,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Clan emblem update reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBPacketEx.cpp",
        //     0x280,
        //     pledgeServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBPacketEx.cpp",
        //     0x280,
        //     pledgeServerId)
        L2_LOG_ERROR(
            Log, "Clan emblem update reply ignored: clan object %u is unavailable", static_cast<unsigned int>(pledgeServerId)
        );
        return false;
    }

    if (result < 0) {
        if (result == -1) {
            user->SendSystemMessage(SystemMessageId::CLAN_MARK_REGISTRATION_INTERVAL_RESTRICTION);
        }
        return false;
    }

    g_PledgeEmblemIDMapDB.Delete(g_nWorldId, static_cast<int>(pledge->GetEmblemId()));
    pledge->SetNewEmblem(static_cast<std::uint32_t>(result));
    pledge->Save(0x14);
    pledge->UpdatePledgeInfoPacket();
    userSocket->Send("chc", 0xfe, 0x1ec, 1);
    user->SendSystemMessage(
        result == 0 ? SystemMessageId::CLAN_MARK_DELETED : SystemMessageId::CLAN_MARK_REGISTERED_WITH_DISPLAY_RULES
    );
    return false;
}

bool ReplyGetPledgeEmblem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00676b84, RP_EX 0x17.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t pledgeEmblemId = 0;
    std::int32_t worldId = 0;
    std::uint32_t pledgeServerId = 0;
    std::array<SharedUtilsWideChar, 0x400> downloadUrl{};
    ::Disassemble(packet, "ddddS", &userServerId, &pledgeEmblemId, &worldId, &pledgeServerId, 0x800, downloadUrl.data());

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2df,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2df,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Clan emblem lookup reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (pledgeEmblemId != 0) {
        g_PledgeEmblemIDMapDB.Add(worldId, pledgeEmblemId, downloadUrl.data());
        user->CheckAndSendPledgeEmblem(
            static_cast<std::uint32_t>(pledgeEmblemId), pledgeServerId, static_cast<std::uint32_t>(worldId)
        );
    }
    return false;
}

bool ReplyDelPledgeEmblem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00676d44, RP_EX 0x18.
    L2_GUARD_FUNCTION();

    std::int32_t pledgeDatabaseId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &pledgeDatabaseId, &result);
    if (result == 1) {
        db.RequestDeletePledgeEmblem(static_cast<std::uint32_t>(pledgeDatabaseId));
    } else if (result == 2) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeDatabaseId);
        if (pledge) {
            pledge->SetNewEmblem(0);
            pledge->UpdatePledgeInfoPacket();
        }
    }
    return false;
}

bool ReplyLoadSevenSignQuestInfo(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    int loadFlag = 0;
    packet = ::Disassemble(packet, "d", &loadFlag);
    if (loadFlag != 0) {
        {
            L2_WRITE_GUARD(SSQSystem::inst());
            packet = SSQSystem::inst().SetAll(packet);
            SSQSystem::inst().Notify();
        }
    }
    SSQSystem::inst().ValidateCollectPointPortion();
    return false;
}

bool ReplyLoadSSQTopPointUser(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ReplyStartSevenSignEvent(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00670728.
    L2_GUARD_FUNCTION();
    int result = 0;
    packet = ::Disassemble(packet, "d", &result);
    if (result == 0) {
        {
            L2_WRITE_GUARD(SSQSystem::inst());
            packet = SSQSystem::inst().SetAll(packet);
            SSQSystem::inst().OnEventStart();
            SSQSystem::inst().EnableTimer();
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Can't start SSQ event, errorcode=[%d]",
        //     L"DBPacketEx.cpp",
        //     0x101,
        //     result)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Can't start SSQ event, errorcode=[%d]",
        //     L"DBPacketEx.cpp",
        //     0x101,
        //     result)
        L2_LOG_ERROR(Log, "Seven Signs event could not start: CacheD result=%d", result);
    }
    return false;
}

bool ReplyLoadTimeAttackInfo(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    int result = 0;
    packet = ::Disassemble(packet, "d", &result);
    if (result == 0) {
        for (int roomNumber = 1; roomNumber < 6; ++roomNumber) {
            for (int recordType = 0; recordType < 3; ++recordType) {
                TimeAttackInfo* const info = TimeAttackBoard::inst().GetInfo(roomNumber, recordType);
                {
                    L2_WRITE_GUARD(*info);
                    packet = info->Set(packet);
                }
            }
        }
        SSQSystem::inst().ValidateMainEventPortion();
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Can't Load time attack info",
        //     L"DBPacketEx.cpp",
        //     0xd4)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Can't Load time attack info",
        //     L"DBPacketEx.cpp",
        //     0xd4)
        L2_LOG_ERROR(Log, "Time-attack data could not be loaded: CacheD result=%d", result);
    }
    return false;
}

bool ReplyFishingRegister(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ReplyGetLastStartTime(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ReplyLoadWarehouseQuestItemList(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ReplyGetNoblessAchievements(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t characterId;
    std::int32_t count;
    ::Disassemble(packet, "dd", &characterId, &count);
    return false;
}

bool ReplyLargePacket(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t first;
    std::int32_t second;
    std::int32_t third;
    std::int32_t fourth;
    ::Disassemble(packet, "dddd", &first, &second, &third, &fourth);
    return false;
}

bool ReplyGetHeroMatchResultPacket(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t first;
    std::int32_t second;
    ::Disassemble(packet, "dd", &first, &second);
    return false;
}

bool ReplyLoadFieldCycleInfo(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x006712c8.
    L2_GUARD_FUNCTION();

    std::int32_t rowCount;
    const unsigned char* cursor = ::Disassemble(packet, "d", &rowCount);
    for (std::int32_t row = 0; row < rowCount; ++row) {
        std::int32_t fieldCycleId;
        std::int32_t point;
        std::int32_t step;
        std::int32_t stepChangedTime;
        std::int32_t pointChangedTime;
        std::int32_t accumulatedPoint;
        std::int32_t missionStartedTime;
        cursor = ::Disassemble(
            cursor, "ddddddd", &fieldCycleId, &point, &step, &stepChangedTime, &pointChangedTime, &accumulatedPoint,
            &missionStartedTime
        );
        g_FieldCycleSystem.LoadFromDBContinue(
            fieldCycleId, step, point, stepChangedTime, pointChangedTime, accumulatedPoint, missionStartedTime
        );
    }
    return false;
}

bool ReplyLoadAllAuction(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0069b9f4.
    L2_GUARD_FUNCTION();
    int isContinuation;
    int auctionCount;
    const unsigned char* const cursor = ::Disassemble(packet, "dd", &isContinuation, &auctionCount);
    CAuctionManager::inst().InitAllAuctions(auctionCount, cursor);
    if (isContinuation != 1) {
        CAuctionNPCDB::inst().InitAllAuctionNPC();
    }
    return false;
}

bool ReplyLoadAllAuctionBid(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0069bac4.
    L2_GUARD_FUNCTION();
    int auctionDatabaseId;
    int isContinuation;
    int bidCount;
    const unsigned char* const cursor = ::Disassemble(packet, "ddd", &auctionDatabaseId, &isContinuation, &bidCount);
    CAuction* const auction = CAuctionManager::inst().GetAuction(auctionDatabaseId);
    if (auction != nullptr) {
        auction->InitBid(bidCount, cursor);
        if (isContinuation != 1) {
            auction->InitBidDone();
        }
    }
    return false;
}

bool ReplyManipulateWarehouseItem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00696ff8.
    L2_GUARD_FUNCTION();

    int userDatabaseId = 0;
    int worldId = 0;
    int atomicJobId = -1;
    int etcManipulationCount = 0;
    int itemCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "ddddd", &userDatabaseId, &worldId, &atomicJobId, &etcManipulationCount, &itemCount);

    AtomicJob* const atomicJob = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Pop(atomicJobId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), userDatabaseId));
    if (itemCount < 0 || etcManipulationCount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Kicked for invalid items. user dbid(%d)",
        //     L"DBPacketEx.cpp",
        //     0xb08,
        //     userDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Kicked for invalid items. user dbid(%d)",
        //     L"DBPacketEx.cpp",
        //     0xb08,
        //     userDatabaseId)
        L2_LOG_ERROR(
            Log,
            "Warehouse reply rejected: negative counts, character DB ID=%d, items=%d, extra operations=%d; closing player "
            "connection if available",
            userDatabaseId, itemCount, etcManipulationCount
        );
        if (user) {
            user->StopMove();
            if (user->GetSocket() != nullptr) {
                user->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            }
        }
        if (atomicJob != nullptr) {
            atomicJob->Destroy();
        }
        return false;
    }

    if (itemCount == 0 && etcManipulationCount == 0) {
        if (atomicJob != nullptr) {
            atomicJob->Destroy();
        }
        return false;
    }

    cursor = L2EtcMani::ParseEtcMani(etcManipulationCount, cursor);
    while (0 <= --itemCount) {
        CItemDisassemblerItemMani itemData;
        cursor = itemData.DisassembleItemInfo(cursor);
        if (itemData.m_nIndex < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] manipulate item fail. "
            //     L"unknown manipulate type[%d]",
            //     L"DBPacketEx.cpp",
            //     0xba6,
            //     itemData.m_nIndex)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] manipulate item fail. "
            //     L"unknown manipulate type[%d]",
            //     L"DBPacketEx.cpp",
            //     0xba6,
            //     itemData.m_nIndex)
            L2_LOG_ERROR(
                Log, "Warehouse item operation skipped: unsupported type=%d, character DB ID=%d", itemData.m_nIndex,
                userDatabaseId
            );
        } else if (itemData.m_nIndex < 3) {
            CObject* const object = g_ObjectDB.GetObjectW(itemData.m_nClassID, false);
            if (object == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] manipulate item fail. "
                //     L"unknown item type[%d]",
                //     L"DBPacketEx.cpp",
                //     0xb90,
                //     itemData.m_nClassID)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] manipulate item fail. "
                //     L"unknown item type[%d]",
                //     L"DBPacketEx.cpp",
                //     0xb90,
                //     itemData.m_nClassID)
                L2_LOG_ERROR(
                    Log,
                    "Warehouse item template is missing: class=%d, character DB ID=%d; item notification processing "
                    "continues",
                    itemData.m_nClassID, userDatabaseId
                );
            } else {
                CItem* const item = static_cast<CItem*>(object);
                if (item->IsDuration()) {
                    db.UpdateItemDuration(
                        static_cast<unsigned int>(userDatabaseId), worldId, static_cast<unsigned int>(itemData.m_nDBID),
                        item->m_const->m_nDuration
                    );
                }
            }
            if (user) {
                user->SendItemIncreaseSystemMessage(itemData.m_nClassID, itemData.m_nChangeAmount, itemData.m_nEnchanted);
            }
        } else if (itemData.m_nIndex > 4) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] manipulate item fail. "
            //     L"unknown manipulate type[%d]",
            //     L"DBPacketEx.cpp",
            //     0xba6,
            //     itemData.m_nIndex)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] manipulate item fail. "
            //     L"unknown manipulate type[%d]",
            //     L"DBPacketEx.cpp",
            //     0xba6,
            //     itemData.m_nIndex)
            L2_LOG_ERROR(
                Log, "Warehouse item operation skipped: unsupported type=%d, character DB ID=%d", itemData.m_nIndex,
                userDatabaseId
            );
        } else if (user) {
            user->SendItemDecreaseSystemMessage(itemData.m_nClassID, itemData.m_nChangeAmount, itemData.m_nEnchanted, true);
        }
    }

    if (atomicJob != nullptr) {
        atomicJob->Do();
        atomicJob->Destroy();
    }
    return false;
}

namespace DBPacketExHandler {

bool ReplyCreateItemAuction(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0069bbac.
    L2_GUARD_FUNCTION();

    int auctionDatabaseId = 0;
    int npcClassId;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &auctionDatabaseId, &npcClassId);
    CAuctionNPC* const npc = CAuctionNPCDB::inst().GetAuctionNPC(npcClassId);
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] cannot find auction npc[%d].. -_-a",
        //     L"DBPacketEx.cpp",
        //     0xa77,
        //     npcClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] cannot find auction npc[%d].. -_-a",
        //     L"DBPacketEx.cpp",
        //     0xa77,
        //     npcClassId)
        L2_LOG_ERROR(Log, "Item auction creation reply ignored: auction NPC class %d is unavailable", npcClassId);
        return false;
    }
    if (auctionDatabaseId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to create auction",
        //     L"DBPacketEx.cpp",
        //     0xa7d)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to create auction",
        //     L"DBPacketEx.cpp",
        //     0xa7d)
        L2_LOG_ERROR(Log, "Item auction creation failed: CacheD returned auction ID zero for NPC class %d", npcClassId);
        npc->m_bAuctionCreating = false;
        return false;
    }

    int startTime;
    int endTime;
    int itemIndex;
    ::Disassemble(cursor, "ddd", &startTime, &endTime, &itemIndex);
    const NPCAuctionItem npcItem = npc->GetAuctionItem(itemIndex);
    if (npcItem.m_nItemType == 0 && npcItem.m_nItemAmount == 0 && npcItem.m_nItemPrice == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid item index[%d]",
        //     L"DBPacketEx.cpp",
        //     0xa87,
        //     itemIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid item index[%d]",
        //     L"DBPacketEx.cpp",
        //     0xa87,
        //     itemIndex)
        L2_LOG_ERROR(
            Log, "Item auction creation reply rejected: item entry %d is empty for NPC class %d, auction=%d", itemIndex,
            npcClassId, auctionDatabaseId
        );
        npc->m_bAuctionCreating = false;
        return false;
    }

    AuctionItem item;
    item.m_nOwnerDBID = npcClassId;
    item.m_nItemIndex = itemIndex;
    item.m_nClassID = npcItem.m_nItemType;
    item.m_nAmount = npcItem.m_nItemAmount;
    const std::int32_t duration =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(endTime) - static_cast<std::uint32_t>(startTime));
    void* const storage = CAuction::operator new(sizeof(CAuction), true);
    CAuction* const auction = storage == nullptr
        ? nullptr
        : ::new (storage)
              CAuction(auctionDatabaseId, npcClassId, startTime, duration, AS_NONE, AES_NONE, item, npcItem.m_nItemPrice);
    if (auction == nullptr || !CAuctionManager::inst().RegisterAuction(auction)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][ItemAuction] fail to create auction id[%d] owner[%d]",
        //     L"DBPacketEx.cpp",
        //     0xaa2,
        //     auctionDatabaseId,
        //     npcClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][ItemAuction] fail to create auction id[%d] owner[%d]",
        //     L"DBPacketEx.cpp",
        //     0xaa2,
        //     auctionDatabaseId,
        //     npcClassId)
        L2_LOG_ERROR(
            Log, "Item auction could not be created or registered locally: auction=%d, NPC class=%d, allocation missing=%d",
            auctionDatabaseId, npcClassId, auction == nullptr
        );
    } else {
        npc->UpdateAuctionDate();
        npc->m_nCurrentAuctionItemIndex = itemIndex;
        npc->m_bBroadcastingNextItem = false;
        const NPCAuctionItem nextItem = npc->PickNextAuctionItem();
        const std::int32_t displayStartTime = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(npc->GetNextStartTime()) - static_cast<std::uint32_t>(npc->m_tNextInfoOpenTime)
        );
        db.RequestUpdateItemAuctionInfoForL2Assist(
            auction->m_nAuctionDBID, npcItem.m_nItemType, npcItem.m_nItemPrice, npcItem.m_nItemAmount, nextItem.m_nItemType,
            nextItem.m_nItemPrice, nextItem.m_nItemAmount, displayStartTime
        );
    }
    npc->m_bAuctionCreating = false;
    return false;
}

} // namespace DBPacketExHandler

bool ReplyGetNoblessTop10(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t olympiadClass = 0;
    std::int32_t rowCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &olympiadClass, &rowCount);

    SharedOlympiadInfo* olympiadInfo = SharedOlympiadInfo::GetInfo();
    const ClassType classType = static_cast<ClassType>(olympiadClass);
    const int classIndex = OlympiadDefine::GetHeroClassIndex(classType);
    if (classIndex != -1) {
        olympiadInfo->arrRankingCount[classIndex] = 0;
    }

    std::int32_t ranking = 0;
    std::int32_t sameRankingCount = 1;
    std::int32_t previousPoint = -1;
    std::int32_t previousMatchCount = -1;
    std::int32_t previousWinCount = -1;
    for (std::int32_t index = 0; index < rowCount; ++index) {
        std::int32_t characterId = 0;
        std::array<SharedUtilsWideChar, 25> characterName{};
        std::int32_t point = 0;
        std::int32_t matchCount = 0;
        std::int32_t olympiadWinCount = 0;
        cursor =
            ::Disassemble(cursor, "dSddd", &characterId, 0x32, characterName.data(), &point, &matchCount, &olympiadWinCount);

        if (previousPoint == point && previousMatchCount == matchCount && previousWinCount == olympiadWinCount) {
            ++sameRankingCount;
        } else {
            ranking = runtime::AddInt32Wrapping(ranking, sameRankingCount);
            sameRankingCount = 1;
            previousPoint = point;
            previousMatchCount = matchCount;
            previousWinCount = olympiadWinCount;
        }

        olympiadInfo = SharedOlympiadInfo::GetInfo();
        olympiadInfo->AddRankInfo(classType, ranking, characterName.data(), point);
    }
    return false;
}

bool ReplyGetPointtop10Packet(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t rowCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &rowCount);
    SharedRimPointInfo* const rankingInfo = SharedRimPointInfo::GetInfo();
    rankingInfo->nRankingCount = 0;

    std::int32_t ranking = 0;
    std::int32_t sameRankingCount = 1;
    std::int32_t previousPoint = -1;
    for (std::int32_t index = 0; index < rowCount; ++index) {
        std::int32_t characterId = 0;
        std::array<char16_t, 25> characterName{};
        std::int32_t characterClass = 0;
        std::int32_t characterLevel = 0;
        std::int32_t point = 0;
        cursor = ::Disassemble(
            cursor, "dSddd", &characterId, 0x32, characterName.data(), &characterClass, &characterLevel, &point
        );

        if (previousPoint == point && characterLevel == -1) {
            sameRankingCount = runtime::AddInt32Wrapping(sameRankingCount, 1);
        } else {
            ranking = runtime::AddInt32Wrapping(ranking, sameRankingCount);
            sameRankingCount = 1;
            previousPoint = point;
        }

        if (rankingInfo->nRankingCount < 10) {
            sRimRankInfo& row = rankingInfo->arrRankingList[rankingInfo->nRankingCount];
            row.nRanking = ranking;
            row.nCharId = characterId;
            row.nType = characterClass;
            row.nLevel = characterLevel;

            std::size_t nameLength = 0;
            while (nameLength + 1 < characterName.size() && characterName[nameLength] != char16_t{}) {
                ++nameLength;
            }
            std::copy_n(characterName.data(), nameLength + 1, row.sName);
            row.nPoint = point;
            ++rankingInfo->nRankingCount;
        }
    }
    return false;
}

bool ReplyCachePing(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00671a64, CacheD RP_EX 0x6f.
    L2_GUARD_FUNCTION();
    const std::int32_t replyTime = static_cast<std::int32_t>(runtime::GetTickCount());
    std::int32_t sendTime;
    ::Disassemble(packet, "d", &sendTime);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[Cache Ping] SendTime [%d] ReplyTime [%d] ",
    //     sendTime,
    //     replyTime)
    // L2_LOG_ERROR(Log,
    //     L"[Cache Ping] SendTime [%d] ReplyTime [%d] ",
    //     sendTime,
    //     replyTime)
    L2_LOG_INFO(
        Log, "CacheD ping reply: round trip=%u ms, send tick=%d, reply tick=%d",
        static_cast<unsigned int>(static_cast<std::uint32_t>(replyTime) - static_cast<std::uint32_t>(sendTime)), sendTime,
        replyTime
    );
    return false;
}

bool ReplyShutDownRequestByServerStopper(CDBSocket*, const unsigned char*) {
    // Current L2Server 0x00671b2c, CacheD RP_EX 0x70.
    L2_GUARD_FUNCTION();

    (new CShutDownMessage())->Post(0);
    return false;
}

bool ReplyWritePost(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068301c, CacheD RP_EX 0x71.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t mailId = 0;
    std::int32_t senderId = 0;
    std::int32_t receiverId = 0;
    std::int32_t trade = 0;
    std::int32_t returnable = 0;
    std::int32_t senderType = 0;
    std::int32_t adenaDatabaseId = 0;
    std::int32_t spentAdena = 0;
    std::int32_t itemCount = 0;
    std::int32_t dailyCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "ddddddddddd", &result, &mailId, &senderId, &receiverId, &trade, &returnable, &senderType, &adenaDatabaseId,
        &spentAdena, &itemCount, &dailyCount
    );

    if (itemCount > 8) {
        itemCount = 8;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Kicked for invalid post items. "
        //     L"nItemCount > MAX_ITEM_IN_POST. nSenderid(%d), "
        //     L"nId(%d), nReceiverId(%d)",
        //     L"DBPacketEx.cpp",
        //     0x1196,
        //     senderId,
        //     mailId,
        //     receiverId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Kicked for invalid post items. "
        //     L"nItemCount > MAX_ITEM_IN_POST. nSenderid(%d), "
        //     L"nId(%d), nReceiverId(%d)",
        //     L"DBPacketEx.cpp",
        //     0x1196,
        //     senderId,
        //     mailId,
        //     receiverId)
        L2_LOG_ERROR(
            Log,
            "Post reply has more than eight attachments; processing is capped at eight: sender=%d, mail=%d, receiver=%d",
            senderId, mailId, receiverId
        );
    }

    const int databaseId = runtime::process::CurrentDatabaseId();
    if (result < 1) {
        CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, senderId));
        if (sender) {
            sender->m_pPostInfo->RecoverSendingDelay();

            unsigned int messageId = SystemMessageId::CANNOT_SEND_POST_RECIPIENT_MAILBOX_FULL;
            if (result == -2) {
                messageId = SystemMessageId::CANNOT_SEND_POST_TO_MISSING_OR_DELETED_RECIPIENT;
            } else if (result == -4) {
                messageId = SystemMessageId::CANNOT_SEND_POST_TO_CHARACTER_WHO_BLOCKED_YOU;
            } else if (result == -7) {
                messageId = SystemMessageId::CANNOT_SEND_MAIL_TO_SELF;
            } else if (result == -8) {
                messageId = SystemMessageId::DAILY_POST_ALLOWANCE_EXHAUSTED;
            } else if (result == -10) {
                messageId = SystemMessageId::USER_NOT_IN_FRIEND_LIST;
            }
            sender->SendSystemMessage(messageId);
            runtime::raw_atomic::ExchangeI32(&sender->m_lWaitItemJob, 0);
            sender->GetSocket()->Send("chd", 0xfe, 0xb5, 0);
        }
        return false;
    }

    unsigned char userMask = 0;
    CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, senderId));
    if (sender) {
        userMask = 1;
        if (spentAdena >= 0 || itemCount > 0) {
            CPost::ItemPair items[8];
            for (int index = 0; index < itemCount; ++index) {
                cursor = ::Disassemble(cursor, "dQ", &items[index].db_id, &items[index].amount);
            }
            PostUtil::ReplyWritePost(sender.get(), itemCount, items, adenaDatabaseId, spentAdena);
        }

        if (senderType == 0) {
            sender->GetSocket()->Send("chd", 0xfe, 0xb5, 1);
            sender->SendSystemMessage(SystemMessageId::POST_MAIL_SENT);
            if (dailyCount != -1 && g_bDailyPostLimitForFreeUser && F2PConfig::IsDailyPostLimitUser(sender.get())) {
                db.RequestSetDailyCount(sender.get(), 2);
                sender->SendSystemMessage_d(SystemMessageId::DAILY_MAIL_SENDS_REMAINING, dailyCount);
            }
        }
    }

    CSPointer<User> receiver = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, receiverId));
    if (receiver) {
        userMask |= 2;
    }
    PostUtil::MakeNewPost(
        mailId, senderId, receiverId, trade, returnable != 0, Post::ProcessWaiting, runtime::crt::Time32Now(),
        itemCount > 0, senderType, userMask, false, 0
    );
    return false;
}

bool ReplySendPost(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068371c, CacheD RP_EX 0x72.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t mailId = 0;
    std::int32_t senderId = 0;
    std::int32_t receiverId = 0;
    std::int32_t unused1 = 0;
    std::int32_t unused2 = 0;
    ::Disassemble(packet, "dddddd", &result, &mailId, &senderId, &receiverId, &unused1, &unused2);

    if (result != 0) {
        const int databaseId = runtime::process::CurrentDatabaseId();
        CSPointer<User> receiver = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, receiverId));
        if (receiver) {
            receiver->GetSocket()->Send("chd", 0xfe, 0xaa, 1);
            runtime::raw_atomic::IncrementI32(&receiver->m_lUnreadMail);
            receiver->GetSocket()->Send("chd", 0xfe, 0x13c, receiver->m_lUnreadMail);
        }

        CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, senderId));
        PostUtil::SendPost(mailId, receiverId, senderId);
    }
    return false;
}

bool ReplyReceivePost(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006839f4, CacheD RP_EX 0x73.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t mailId = 0;
    std::int32_t requestContext = 0;
    std::int32_t senderId = 0;
    std::int32_t receiverId = 0;
    std::int32_t replyContext = 0;
    std::int64_t requiredMoney = 0;
    std::int32_t containsAdenaAttachment = 0;
    std::int32_t itemCount = 0;
    packet = ::Disassemble(
        packet, "ddddddQdd", &result, &mailId, &requestContext, &senderId, &receiverId, &replyContext, &requiredMoney,
        &containsAdenaAttachment, &itemCount
    );
    static_cast<void>(requestContext);
    static_cast<void>(requiredMoney);

    const int databaseId = runtime::process::CurrentDatabaseId();
    if (result == 0) {
        bool postEnded = false;
        CSPointer<User> receiver = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, receiverId));
        if (receiver) {
            receiver->m_pPostInfo->CancelTryReceivePost();
            CSPointer<CPost> post = receiver->m_pPostInfo->FindReceivedPostWithRef(mailId);
            if (post) {
                post->EndDB();
                postEnded = true;
            }
            runtime::raw_atomic::ExchangeI32(&receiver->m_lWaitItemJob, 0);
            receiver->SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_MAIL_SENDER_CANNOT_RECEIVE_PAYMENT);
        }
        if (!postEnded) {
            CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, senderId));
            if (sender) {
                CSPointer<CPost> post = sender->m_pPostInfo->FindSentPostWithRef(mailId);
                if (post) {
                    post->EndDB();
                }
            }
        }
        return false;
    }

    PostUtil::ReceivePost(mailId, receiverId, senderId);
    CSPointer<User> receiver = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, receiverId));
    if (receiver) {
        receiver->m_pPostInfo->ReceivePost();
        std::int32_t payerAdenaRetained = 0;
        std::int32_t deletedPayerAdenaItemId = 0;

        {
            L2_WRITE_GUARD(receiver->m_inv);
            for (int index = 0; index < itemCount; ++index) {
                if (!DBSocketUtil::ParseItem(packet, *receiver.get(), true, containsAdenaAttachment != 0)) {
                    break;
                }
            }
            packet = ::Disassemble(packet, "dd", &payerAdenaRetained, &deletedPayerAdenaItemId);
            if (payerAdenaRetained == 0) {
                CItem* const adena = receiver->m_inv.GetByDBID(deletedPayerAdenaItemId);
                if (adena == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[ReceivePost] No Adena exists. %d of user [%s].",
                    //     deletedPayerAdenaItemId,
                    //     receiver->GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[ReceivePost] No Adena exists. %d of user [%s].",
                    //     deletedPayerAdenaItemId,
                    //     receiver->GetName())
                    // L2_LOG_ERROR(Log,
                    //     "Mail payment cleanup could not find payer adena: mail=%d, item DB ID=%d, receiver=%s",
                    //     mailId, deletedPayerAdenaItemId, ::WideToUtf8(receiver->GetName()).c_str())
                    L2_LOG_ERROR(
                        Log, "Mail payment cleanup could not find payer adena: mail=%d, item DB ID=%d, receiver=%s", mailId,
                        deletedPayerAdenaItemId, ::Utf16ToUtf8(receiver->GetName()).c_str()
                    );
                } else {
                    receiver->m_inv.Pop(adena->m_data->index, false);
                    receiver->m_inv.SetInventoryChanged(adena, IUT_DELETE);
                    adena->Delete();
                }
            }
        }
        receiver->SendItemList(true, true, nullptr);
        g_npcserver.Send("cdddd", 0xc6, receiver->m_data->index, 2, 1, 0);
        runtime::raw_atomic::ExchangeI32(&receiver->m_lWaitItemJob, 0);
    }

    std::int32_t recipientAdenaItemId = 0;
    std::int64_t recipientAdenaAmount = 0;
    CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, senderId));
    if (sender) {
        packet = ::Disassemble(packet, "dQ", &recipientAdenaItemId, &recipientAdenaAmount);
        if (recipientAdenaItemId < 1) {
            if (receiver) {
                sender->GetSocket()->SendSystemMessage_S(
                    SystemMessageId::RECIPIENT_COLLECTED_MAIL_ATTACHMENT, receiver->GetName()
                );
            }
        } else {
            {
                L2_WRITE_GUARD(sender->m_inv);
                CItem* adena = sender->m_inv.GetByDBID(recipientAdenaItemId);
                if (adena == nullptr) {
                    adena = g_ObjectDB.CreateItem(0x39);
                    if (adena != nullptr) {
                        if (!sender->m_inv.Push(adena->m_data->index)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "inven push failed at file[%s], line[%d]",
                            //     "DBPacketEx.cpp",
                            //     0x1278)
                            // L2_LOG_ERROR(Log,
                            //     "inven push failed at file[%s], line[%d]",
                            //     "DBPacketEx.cpp",
                            //     0x1278)
                            L2_LOG_ERROR(
                                Log,
                                "Mail payment adena could not be inserted into sender inventory: mail=%d, sender=%d, item "
                                "DB ID=%d; item processing continues",
                                mailId, senderId, recipientAdenaItemId
                            );
                        }
                        sender->m_inv.SetInventoryChanged(adena, IUT_ADD);
                    } else {
                        // Log.Add(LOG_ERROR, L"CreateItem fail.")
                        // L2_LOG_ERROR(Log, L"CreateItem fail.")
                        L2_LOG_ERROR(
                            Log, "Cannot create adena item for mail payment: mail=%d, sender=%d, item DB ID=%d", mailId,
                            senderId, recipientAdenaItemId
                        );
                    }
                }
                if (adena != nullptr) {
                    {
                        L2_GUARD(*adena->m_lock);
                        adena->SetDBID(static_cast<unsigned int>(recipientAdenaItemId));
                        if (!adena->AddAmount(recipientAdenaAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                            //     "DBPacketEx.cpp",
                            //     0x1286,
                            //     adena->m_data->m_nCount,
                            //     recipientAdenaAmount)
                            // L2_LOG_ERROR(Log,
                            //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                            //     "DBPacketEx.cpp",
                            //     0x1286,
                            //     adena->m_data->m_nCount,
                            //     recipientAdenaAmount)
                            L2_LOG_ERROR(
                                Log,
                                "Mail payment adena amount update failed: mail=%d, current amount=%lld, requested "
                                "increase=%lld",
                                mailId, static_cast<long long>(adena->m_data->m_nCount),
                                static_cast<long long>(recipientAdenaAmount)
                            );
                        }
                    }
                    sender->m_inv.SetInventoryChanged(adena, IUT_UPDATE);
                }
            }
            sender->SendItemList(true, true, nullptr);
            if (receiver) {
                sender->GetSocket()->Send(
                    "chccQcS", 0x62, SystemMessageId::COLLECT_ON_DELIVERY_MAIL_PAYMENT_RECEIVED, 2, 6,
                    static_cast<long long>(recipientAdenaAmount), 0xc, receiver->GetName()
                );
            }
        }
    }

    bool postEnded = false;
    if (receiver) {
        CSPointer<CPost> post = receiver->m_pPostInfo->FindReceivedPostWithRef(mailId);
        if (post) {
            post->EndDB();
            postEnded = true;
        }
    }
    if (!postEnded && sender) {
        CSPointer<CPost> post = sender->m_pPostInfo->FindSentPostWithRef(mailId);
        if (post) {
            post->EndDB();
            postEnded = true;
        }
    }

    if (postEnded) {
        if (replyContext == 0) {
            std::array<char, 1024> notification;
            int packetLength = 0;
            if (receiver) {
                packetLength = ::Assemble(notification.data(), static_cast<int>(notification.size()), "dd", mailId, 2);
                receiver->GetSocket()->Send("chddb", 0xfe, 0xb4, 1, 1, packetLength, notification.data());
                receiver->SendSystemMessage(SystemMessageId::MAIL_RECEIVED);
            }
            if (sender) {
                packetLength += ::Assemble(
                    notification.data() + packetLength, static_cast<int>(notification.size()) - packetLength, "dd", mailId, 2
                );
                sender->GetSocket()->Send("chddb", 0xfe, 0xb4, 0, 1, packetLength, notification.data());
            }
        } else {
            PostUtil::DeletePost(mailId, receiverId, senderId, 3);
            if (receiver) {
                receiver->SendSystemMessage(SystemMessageId::MAIL_RECEIVED);
            }
        }
    }
    return false;
}

bool ReplyRejectPost(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006847a0, CacheD RP_EX 0x74.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t mailId = 0;
    std::int32_t firstUserId = 0;
    std::int32_t secondUserId = 0;
    std::int32_t trade = 0;
    std::int32_t returnable = 0;
    std::int32_t requestContext = 0;
    std::array<char16_t, 25> receiverName{};
    std::int32_t requestValue = 0;
    std::int32_t senderType = 0;
    ::Disassemble(
        packet, "dddddddSdd", &result, &mailId, &firstUserId, &secondUserId, &trade, &returnable, &requestContext, 0x32,
        receiverName.data(), &requestValue, &senderType
    );
    static_cast<void>(requestContext);

    const int databaseId = runtime::process::CurrentDatabaseId();
    if (result == 0) {
        bool postEnded = false;
        CSPointer<User> receiver = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, secondUserId));
        if (receiver) {
            CSPointer<CPost> post = receiver->m_pPostInfo->FindReceivedPostWithRef(mailId);
            if (post) {
                post->EndDB();
                postEnded = true;
            }
        }
        if (!postEnded) {
            CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, firstUserId));
            if (sender) {
                CSPointer<CPost> post = sender->m_pPostInfo->FindSentPostWithRef(mailId);
                if (post) {
                    post->EndDB();
                }
            }
        }
        return false;
    }

    unsigned char userMask = 0;
    CSPointer<User> firstUser = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, firstUserId));
    if (firstUser) {
        firstUser->SendSystemMessage(
            requestValue == 0 ? SystemMessageId::MAIL_RETURNED_COLLECTION_TIME_EXPIRED : SystemMessageId::MAIL_RETURNED
        );
        userMask = 1;
    }

    CSPointer<User> secondUser = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, secondUserId));
    if (secondUser) {
        secondUser->GetSocket()->SendSystemMessage_S(
            requestValue == 0 ? SystemMessageId::MAIL_AUTO_RETURNED_RECIPIENT_DID_NOT_COLLECT
                              : SystemMessageId::CHARACTER_RETURNED_MAIL,
            receiverName.data()
        );
        userMask |= 2;
    }

    PostUtil::DeletePost(mailId, firstUserId, secondUserId, 3);
    PostUtil::MakeNewPost(
        mailId, -1, secondUserId, trade, returnable != 0, Post::ProcessWaiting, runtime::crt::Time32Now(), true,
        senderType, userMask, false, 0
    );
    return false;
}

bool ReplyCancelPost(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00684b78, CacheD RP_EX 0x75.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t mailId = 0;
    std::int32_t requestContext = 0;
    std::int32_t senderId = 0;
    std::int32_t receiverId = 0;
    std::int32_t itemCount = 0;
    packet = ::Disassemble(packet, "dddddd", &result, &mailId, &requestContext, &senderId, &receiverId, &itemCount);

    const int databaseId = runtime::process::CurrentDatabaseId();
    if (result == 0) {
        CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, senderId));
        if (sender) {
            sender->m_pPostInfo->CancelTryCancelPost();
            CSPointer<CPost> post = sender->m_pPostInfo->FindSentPostWithRef(mailId);
            if (post) {
                post->EndDB();
            }
            runtime::raw_atomic::ExchangeI32(&sender->m_lWaitItemJob, 0);
        }
        return false;
    }

    std::array<wchar_t, 25> senderName{};
    CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, senderId));
    if (sender) {
        const char16_t* const sourceName = sender->GetName();
        if (sourceName == nullptr) {
            runtime::process::HandleInvalidParameter(L"wcscpy_s");
        } else {
            std::size_t nameLength = 0;
            while (nameLength < senderName.size() && sourceName[nameLength] != L'\0') {
                ++nameLength;
            }
            if (nameLength >= senderName.size()) {
                runtime::process::HandleInvalidParameter(L"wcscpy_s");
            } else {
                std::copy_n(sourceName, nameLength + 1, senderName.data());
            }
        }

        if (itemCount > 0) {
            {
                L2_WRITE_GUARD(sender->m_inv);
                for (int index = 0; index < itemCount; ++index) {
                    if (!DBSocketUtil::ParseItem(packet, *sender.get(), false, false)) {
                        break;
                    }
                }
            }
            sender->SendItemList(true, true, nullptr);
            runtime::raw_atomic::ExchangeI32(&sender->m_lWaitItemJob, 0);
        }
        sender->SendSystemMessage(SystemMessageId::MAIL_CANCELLED);
        sender->m_pPostInfo->CancelPost();
    }

    CSPointer<User> receiver = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, receiverId));
    if (receiver) {
        receiver->GetSocket()->SendSystemMessage_S(
            SystemMessageId::SENDER_CANCELLED_MAIL, WideToUtf16(senderName.data()).c_str()
        );
    }
    PostUtil::CancelPost(mailId, receiverId, senderId, requestContext);
    return false;
}

bool ReplyDeletePost(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00671bfc, CacheD RP_EX 0x76.
    L2_GUARD_FUNCTION();

    std::int32_t deleteMask = 0;
    std::int32_t senderId = 0;
    std::int32_t receiverId = 0;
    std::int32_t replyCount = 0;
    packet = ::Disassemble(packet, "dddd", &deleteMask, &senderId, &receiverId, &replyCount);

    for (int index = 0; index < replyCount; ++index) {
        std::int32_t result = 0;
        std::int32_t mailId = 0;
        std::int32_t requestValue = 0;
        packet = ::Disassemble(packet, "ddd", &result, &mailId, &requestValue);
        static_cast<void>(requestValue);
        if (result != 0) {
            PostUtil::DeletePost(mailId, receiverId, senderId, static_cast<unsigned char>(deleteMask));
        }
    }
    return false;
}

bool ReplyOpenPost(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00697b88, CacheD RP_EX 0x77.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t mailId = 0;
    std::int32_t requestContext = 0;
    std::array<char16_t, 25> senderName{};
    std::array<char16_t, 25> receiverName{};
    std::int32_t trade = 0;
    std::array<char16_t, 61> title{};
    std::array<char16_t, 1001> contents{};
    std::int32_t itemCount = 0;
    packet = ::Disassemble(
        packet, "dddSSdSSd", &result, &mailId, &requestContext, 0x32, senderName.data(), 0x32, receiverName.data(), &trade,
        0x7a, title.data(), 0x7d2, contents.data(), &itemCount
    );
    if (result == 0) {
        return false;
    }

    const std::u16string activeNameUtf16(requestContext == 0 ? senderName.data() : receiverName.data());
    CSPointer<User> activeUser = GetUserFromNameMap(g_nWorldId, activeNameUtf16);
    if (!activeUser) {
        return false;
    }
    if (itemCount > 8) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user %s attempted change post-trade packet. count %d > %d",
        //     activeUser->GetName(),
        //     itemCount,
        //     8)
        // L2_LOG_ERROR(Log,
        //     L"user %s attempted change post-trade packet. count %d > %d",
        //     activeUser->GetName(),
        //     itemCount,
        //     8)
        // L2_LOG_ERROR(Log,
        //     "Open-mail reply rejected: attachment count=%d exceeds 8, mail=%d, user=%s",
        //     itemCount, mailId, ::WideToUtf8(activeUser->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Open-mail reply rejected: attachment count=%d exceeds 8, mail=%d, user=%s", itemCount, mailId,
            ::Utf16ToUtf8(activeUser->GetName()).c_str()
        );
        return false;
    }

    std::array<CPost::ItemPair, 8> items{};
    std::array<int, 8> classIds{};
    CPacketHelperEx<20480> itemPacket;
    for (int index = 0; index < itemCount; ++index) {
        CItemAssemblerPost item;
        packet = item.DisassembleFromDB(*activeUser.get(), packet);
        items[static_cast<std::size_t>(index)].db_id = item.m_nItemDBID;
        items[static_cast<std::size_t>(index)].amount = item.m_nAmount;
        classIds[static_cast<std::size_t>(index)] = item.m_nClassID;
        const int assembleResult = item.AssembleItemInfo(itemPacket);
        if (assembleResult < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Assembe Error at file[%s], line[%d], assemble result = [%d], total length = [%d]",
            //     L"DBPacketEx.cpp",
            //     0x1411,
            //     assembleResult,
            //     itemPacket.GetLength())
            // L2_LOG_ERROR(Log,
            //     L"Assembe Error at file[%s], line[%d], assemble result = [%d], total length = [%d]",
            //     L"DBPacketEx.cpp",
            //     0x1411,
            //     assembleResult,
            //     itemPacket.GetLength())
            L2_LOG_ERROR(
                Log, "Open-mail attachment encoding failed: mail=%d, attachment index=%d, result=%d, packet bytes=%d",
                mailId, index, assembleResult, itemPacket.GetLength()
            );
            return false;
        }
    }

    std::int64_t tradeMoney = 0;
    std::int32_t returnable = 0;
    std::int32_t returned = 0;
    std::int32_t processTransitioned = 0;
    std::int32_t sentTime = 0;
    std::int32_t requestValue = 0;
    std::int32_t processBeforeOpen = 0;
    std::int32_t senderType = 0;
    std::int32_t recordedId = 0;
    const unsigned char* const optionalPacket = ::Disassemble(
        packet, "Qdddddddd", &tradeMoney, &returnable, &returned, &processTransitioned, &sentTime, &requestValue,
        &processBeforeOpen, &senderType, &recordedId
    );
    static_cast<void>(sentTime);
    static_cast<void>(requestValue);

    const bool sentPost = requestContext == 0;
    if (activeUser->m_pPostInfo->OpenPost(mailId, itemCount, items.data(), classIds.data(), tradeMoney, sentPost)) {
        CUserSocket* const userSocket = activeUser->GetSocket();
        if (sentPost) {
            userSocket->Send(
                "chdddSSSdbQd", 0xfe, 0xae, senderType, mailId, trade, receiverName.data(), title.data(), contents.data(),
                itemCount, itemPacket.GetLength(), itemPacket.GetPacket(), static_cast<long long>(tradeMoney),
                processBeforeOpen
            );
        } else if (senderType == 4) {
            userSocket->Send(
                "chddddddSSSdbQdd", 0xfe, 0xac, senderType, 0xda4, 0xda5, mailId, trade, returned, senderName.data(),
                title.data(), contents.data(), itemCount, itemPacket.GetLength(), itemPacket.GetPacket(),
                static_cast<long long>(tradeMoney), returnable, recordedId
            );
        } else if (senderType == 5) {
            std::array<std::int32_t, 8> commission{};
            ::Disassemble(
                optionalPacket, "dddddddd", &commission[0], &commission[1], &commission[2], &commission[3], &commission[4],
                &commission[5], &commission[6], &commission[7]
            );
            userSocket->Send(
                "chddddddddddddddSSSdbQdd", 0xfe, 0xac, senderType, commission[0], commission[1], commission[2],
                commission[3], commission[4], commission[5], commission[6], commission[7], 0xda2, 0xda3, mailId, trade,
                returned, senderName.data(), title.data(), contents.data(), itemCount, itemPacket.GetLength(),
                itemPacket.GetPacket(), static_cast<long long>(tradeMoney), returnable, recordedId
            );
        } else {
            userSocket->Send(
                "chddddSSSdbQdd", 0xfe, 0xac, senderType, mailId, trade, returned, senderName.data(), title.data(),
                contents.data(), itemCount, itemPacket.GetLength(), itemPacket.GetPacket(),
                static_cast<long long>(tradeMoney), returnable, recordedId
            );
        }
    }

    if (processTransitioned != 0) {
        int receiverDatabaseId = 0;
        int senderDatabaseId = 0;

        const std::u16string receiverNameWideUtf16(receiverName.data());
        CSPointer<User> receiver = GetUserFromNameMap(g_nWorldId, receiverNameWideUtf16);
        if (receiver) {
            receiverDatabaseId = static_cast<int>(receiver->GetDBID());
            std::array<char, 1024> notification{};
            const int length = ::Assemble(notification.data(), static_cast<int>(notification.size()), "dd", mailId, 1);
            activeUser->GetSocket()->Send("chddb", 0xfe, 0xb4, 1, 1, length, notification.data());
            const int unreadMail = runtime::raw_atomic::AddI32(&receiver->m_lUnreadMail, -1);
            receiver->GetSocket()->Send("chd", 0xfe, 0x13c, unreadMail);
        }

        const std::u16string senderNameWideUtf16(senderName.data());
        CSPointer<User> sender = GetUserFromNameMap(g_nWorldId, senderNameWideUtf16);
        if (sender) {
            senderDatabaseId = static_cast<int>(sender->GetDBID());
            std::array<char, 1024> notification{};
            const int length = ::Assemble(notification.data(), static_cast<int>(notification.size()), "dd", mailId, 1);
            activeUser->GetSocket()->Send("chddb", 0xfe, 0xb4, 0, 1, length, notification.data());
        }
        PostUtil::OpenPost(mailId, receiverDatabaseId, senderDatabaseId);
    }
    return false;
}

bool ReplyMakeReceivedPostList(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00685068, CacheD RP_EX 0x78.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t postCount = 0;
    packet = ::Disassemble(packet, "dd", &characterId, &postCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid user %d requested load post list.",
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"Invalid user %d requested load post list.",
        //     characterId)
        L2_LOG_ERROR(Log, "Received-mail initialization reply ignored: character DB ID %d is unavailable", characterId);
        return false;
    }
    if (postCount > 0x1e0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"User %s has too many received count %d",
        //     user->GetName(),
        //     postCount)
        // L2_LOG_ERROR(Log,
        //     L"User %s has too many received count %d",
        //     user->GetName(),
        //     postCount)
        // L2_LOG_ERROR(Log,
        //     "Received-mail initialization reply exceeds 480 entries: count=%d, user=%s. Processing continues",
        //     postCount, ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Received-mail initialization reply exceeds 480 entries: count=%d, user=%s; processing continues",
            postCount, ::Utf16ToUtf8(user->GetName()).c_str()
        );
    }

    runtime::raw_atomic::ExchangeI32(&user->m_lUnreadMail, 0);
    bool sentUnreadNotification = false;
    for (int index = 0; index < postCount; ++index) {
        std::int32_t mailId = 0;
        std::int32_t senderId = 0;
        std::int32_t trade = 0;
        std::int32_t returnable = 0;
        Post::PostProcessType process = Post::ProcessWaiting;
        std::int32_t deleted = 0;
        std::int32_t sentTime = 0;
        std::int32_t senderType = 0;
        std::int32_t itemCount = 0;
        packet = ::Disassemble(
            packet, "ddddddddd", &mailId, &senderId, &trade, &returnable, &process, &deleted, &sentTime, &senderType,
            &itemCount
        );

        if (process == Post::ProcessWaiting) {
            sentTime = runtime::crt::Time32Now();
        }
        if (process == Post::ProcessSent) {
            if (!sentUnreadNotification) {
                user->GetSocket()->Send("chd", 0xfe, 0xaa, 0);
                sentUnreadNotification = true;
            }
            runtime::raw_atomic::IncrementI32(&user->m_lUnreadMail);
        }

        PostUtil::MakeNewPost(
            mailId, senderId, characterId, trade, returnable != 0, process, sentTime, itemCount > 0, senderType, 2, true,
            static_cast<unsigned char>(deleted)
        );
    }

    user->GetSocket()->Send("chd", 0xfe, 0x13c, runtime::raw_atomic::LoadI32Relaxed(&user->m_lUnreadMail));
    return false;
}

bool ReplyMakeSentPostList(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00685498, CacheD RP_EX 0xb7.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t postCount = 0;
    packet = ::Disassemble(packet, "dd", &characterId, &postCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid user %d requested load post list.",
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"Invalid user %d requested load post list.",
        //     characterId)
        L2_LOG_ERROR(Log, "Sent-mail initialization reply ignored: character DB ID %d is unavailable", characterId);
        return false;
    }
    if (postCount > 0x1e0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"User %s has too many sent count %d",
        //     user->GetName(),
        //     postCount)
        // L2_LOG_ERROR(Log,
        //     L"User %s has too many sent count %d",
        //     user->GetName(),
        //     postCount)
        // L2_LOG_ERROR(Log,
        //     "Sent-mail initialization reply exceeds 480 entries: count=%d, user=%s. Processing continues",
        //     postCount, ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Sent-mail initialization reply exceeds 480 entries: count=%d, user=%s; processing continues", postCount,
            ::Utf16ToUtf8(user->GetName()).c_str()
        );
    }

    for (int index = 0; index < postCount; ++index) {
        std::int32_t mailId = 0;
        std::int32_t receiverId = 0;
        std::int32_t trade = 0;
        std::int32_t returnable = 0;
        Post::PostProcessType process = Post::ProcessWaiting;
        std::int32_t deleted = 0;
        std::int32_t sentTime = 0;
        std::int32_t senderType = 0;
        std::int32_t itemCount = 0;
        packet = ::Disassemble(
            packet, "ddddddddd", &mailId, &receiverId, &trade, &returnable, &process, &deleted, &sentTime, &senderType,
            &itemCount
        );

        PostUtil::MakeNewPost(
            mailId, characterId, receiverId, trade, returnable != 0, process, sentTime, itemCount > 0, senderType, 1, true,
            static_cast<unsigned char>(deleted)
        );
    }
    return false;
}

bool ReplyLoadSentPostList(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068571c, CacheD RP_EX 0x79.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t postCount = 0;
    packet = ::Disassemble(packet, "dd", &characterId, &postCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid user %d requested load sent post list.",
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"Invalid user %d requested load sent post list.",
        //     characterId)
        L2_LOG_ERROR(Log, "Sent-mail list reply ignored: character DB ID %d is unavailable", characterId);
        return false;
    }
    if (postCount > 0x1e0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"User %s has too many sent count %d",
        //     user->GetName(),
        //     postCount)
        // L2_LOG_ERROR(Log,
        //     L"User %s has too many sent count %d",
        //     user->GetName(),
        //     postCount)
        // L2_LOG_ERROR(Log,
        //     "Sent-mail list reply exceeds 480 entries: count=%d, user=%s. Processing continues",
        //     postCount, ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Sent-mail list reply exceeds 480 entries: count=%d, user=%s; processing continues", postCount,
            ::Utf16ToUtf8(user->GetName()).c_str()
        );
    }

    std::array<char, 0x4f9c> postList;
    int postListLength = 0;
    for (int index = 0; index < postCount; ++index) {
        std::int32_t mailId = 0;
        std::array<char16_t, 25> receiverName{};
        std::int32_t trade = 0;
        std::array<char16_t, 61> title;
        Post::PostProcessType process = Post::ProcessWaiting;
        std::int32_t sentTime = 0;
        std::int32_t returnable = 0;
        std::int32_t itemCount = 0;
        std::int32_t senderType = 0;
        packet = ::Disassemble(
            packet, "dSdSddddd", &mailId, 0x32, receiverName.data(), &trade, 0x7a, title.data(), &process, &sentTime,
            &returnable, &itemCount, &senderType
        );

        const std::int32_t nextJobTime = PostUtil::DecideNextJobTime(sentTime, process, trade, returnable != 0, false);
        postListLength += ::Assemble(
            postList.data() + postListLength, 0x4f9c - postListLength, "dSSdddddd", mailId, title.data(),
            receiverName.data(), trade, nextJobTime, process < Post::ProcessOpened, returnable, itemCount > 0, senderType
        );
    }

    user->GetSocket()->Send("chddb", 0xfe, 0xad, runtime::crt::Time32Now(), postCount, postListLength, postList.data());
    return false;
}

bool ReplyLoadReceivedPostList(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00685ad0, CacheD RP_EX 0x7a.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t postCount = 0;
    packet = ::Disassemble(packet, "dd", &characterId, &postCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid user %d requested load received post list.",
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"Invalid user %d requested load received post list.",
        //     characterId)
        L2_LOG_ERROR(Log, "Received-mail list reply ignored: character DB ID %d is unavailable", characterId);
        return false;
    }
    if (postCount > 0x1e0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"User %s has too many received count %d",
        //     user->GetName(),
        //     postCount)
        // L2_LOG_ERROR(Log,
        //     L"User %s has too many received count %d",
        //     user->GetName(),
        //     postCount)
        // L2_LOG_ERROR(Log,
        //     "Received-mail list reply exceeds 480 entries: count=%d, user=%s. Processing continues",
        //     postCount, ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Received-mail list reply exceeds 480 entries: count=%d, user=%s; processing continues", postCount,
            ::Utf16ToUtf8(user->GetName()).c_str()
        );
    }

    std::array<char, 0x4f9c> postList;
    int postListLength = 0;
    for (int index = 0; index < postCount; ++index) {
        std::int32_t mailId = 0;
        std::array<char16_t, 25> senderName{};
        std::int32_t trade = 0;
        std::array<char16_t, 61> title{};
        Post::PostProcessType process = Post::ProcessWaiting;
        std::int32_t sentTime = 0;
        std::int32_t returnable = 0;
        std::int32_t returned = 0;
        std::int32_t systemSender = 0;
        std::int32_t itemCount = 0;
        std::int32_t senderType = 0;
        std::int32_t recordedId = 0;
        packet = ::Disassemble(
            packet, "dSdSdddddddd", &mailId, 0x32, senderName.data(), &trade, 0x7a, title.data(), &process, &sentTime,
            &returnable, &returned, &systemSender, &itemCount, &senderType, &recordedId
        );
        static_cast<void>(systemSender);

        const std::int32_t nextJobTime = PostUtil::DecideNextJobTime(sentTime, process, trade, returnable != 0, false);
        if (senderType == 4 || senderType == 5) {
            postListLength += ::Assemble(
                postList.data() + postListLength, 0x4f9c - postListLength, "dddSSddddddd", senderType,
                senderType == 4 ? 0xda4 : 0xda2, mailId, title.data(), senderName.data(), trade, nextJobTime,
                process < Post::ProcessOpened, returnable, itemCount > 0, returned, recordedId
            );
        } else {
            postListLength += ::Assemble(
                postList.data() + postListLength, 0x4f9c - postListLength, "ddSSddddddd", senderType, mailId, title.data(),
                senderName.data(), trade, nextJobTime, process < Post::ProcessOpened, returnable, itemCount > 0, returned,
                recordedId
            );
        }
    }

    user->GetSocket()->Send(
        "chddbdd", 0xfe, 0xab, runtime::crt::Time32Now(), postCount, postListLength, postList.data(), g_nPostFeeBase,
        g_nPostFeePerSlot
    );
    return false;
}

bool ReplyLoadDominion(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00675910, CacheD RP_EX 0x65.
    L2_GUARD_FUNCTION();

    std::int32_t dominionId = 0;
    std::int32_t dominionStatus = 0;
    std::int32_t residenceState = 0;
    ::Disassemble(packet, "ddd", &dominionId, &dominionStatus, &residenceState);

    CDominion* const dominion = g_ResidenceDB.GetDominion(dominionId);
    if (dominion == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Dominion not found! id:%d",
        //     dominionId)
        // L2_LOG_ERROR(Log,
        //     L"Dominion not found! id:%d",
        //     dominionId)
        L2_LOG_ERROR(Log, "Dominion load reply ignored: dominion %d is unavailable", dominionId);
        return false;
    }

    dominion->m_data->m_nDominionStatus = dominionStatus;
    if (dominion->m_data != nullptr) {
        dominion->m_data->m_ResidenceState = static_cast<ResidenceState>(residenceState);
    }
    if ((dominionStatus == 1 || dominionStatus == 2) &&
        (residenceState == 1 || residenceState == 2 || residenceState == 5)) {
        CDominionManager::Inst()->AddStart(dominion);
        dominion->MakeEntry();
    }
    if (dominionStatus == 2) {
        dominion->EnableDominionExtends();
    }
    db.RequestLoadDominionRegistry(*dominion);
    g_JobFinishSignal.SetDominionLoaded();
    return false;
}

bool ReplyLoadOwnthing(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006716c8, CacheD RP_EX 0x66.
    L2_GUARD_FUNCTION();

    std::int32_t originalDominionId = 0;
    std::int32_t currentDominionId = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    ::Disassemble(packet, "ddddd", &originalDominionId, &currentDominionId, &x, &y, &z);

    CResidence* const originalResidence = g_ResidenceDB.GetResidence(originalDominionId);
    if (originalResidence == nullptr || !originalResidence->IsDominion()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Original dominion not found for ownthing! id:%d",
        //     originalDominionId)
        // L2_LOG_ERROR(Log,
        //     L"Original dominion not found for ownthing! id:%d",
        //     originalDominionId)
        L2_LOG_ERROR(
            Log, "Own-thing load reply ignored: original residence %d is missing or is not a dominion", originalDominionId
        );
        return false;
    }
    CDominion* const originalDominion = originalResidence->CastDominion();

    CResidence* const currentResidence = g_ResidenceDB.GetResidence(currentDominionId);
    if (currentResidence == nullptr || !currentResidence->IsDominion()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Current dominion not found for ownthing! id:%d",
        //     currentDominionId)
        // L2_LOG_ERROR(Log,
        //     L"Current dominion not found for ownthing! id:%d",
        //     currentDominionId)
        L2_LOG_ERROR(
            Log, "Own-thing load reply ignored: current residence %d is missing or is not a dominion", currentDominionId
        );
        return false;
    }
    CDominion* const currentDominion = currentResidence->CastDominion();

    COwnThing* const ownThing = originalDominion->m_pMainOwnThing;
    if (ownThing == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"cannot find ownthing! dominion id:%d",
        //     originalDominionId)
        // L2_LOG_ERROR(Log,
        //     L"cannot find ownthing! dominion id:%d",
        //     originalDominionId)
        L2_LOG_ERROR(
            Log, "Own-thing load reply ignored: original dominion %d has no main own-thing object", originalDominionId
        );
        return false;
    }

    ownThing->InsertOwnThingToDominion(currentDominion);
    const int state = currentDominion->m_data == nullptr ? 8 : static_cast<int>(currentDominion->m_data->m_ResidenceState);
    ownThing->Disappear();
    if (state == 5) {
        ownThing->SetPos(FVector{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)});
        ownThing->Appear();
    } else {
        ownThing->SetPos(currentDominion->GetPos(ownThing));
    }
    return false;
}

bool ReplyLoadDominionRegistry(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00671928, CacheD RP_EX 0x67.
    L2_GUARD_FUNCTION();

    std::int32_t dominionId = 0;
    std::int32_t rowCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &dominionId, &rowCount);
    CDominion* const dominion = g_ResidenceDB.GetDominion(dominionId);
    if (dominion == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Dominion not found for registry! id:%d",
        //     dominionId)
        // L2_LOG_ERROR(Log,
        //     L"Dominion not found for registry! id:%d",
        //     dominionId)
        L2_LOG_ERROR(
            Log, "Dominion registration list reply ignored: dominion %d is unavailable, reported entries=%d", dominionId,
            rowCount
        );
        return false;
    }

    for (std::int32_t index = 0; index < rowCount; ++index) {
        std::int32_t registryType = 0;
        std::int32_t registryId = 0;
        cursor = ::Disassemble(cursor, "dd", &registryType, &registryId);
        if (registryType == 0) {
            dominion->RegisterUser(registryId, false);
        } else {
            dominion->RegisterPledge(registryId, false);
        }
    }
    return false;
}

bool ReplyLoadDominionSiege(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00675ab8, CacheD RP_EX 0x68.
    L2_GUARD_FUNCTION();

    std::int32_t siegeState = 0;
    std::int32_t previousSiegeEndTime = 0;
    std::int32_t siegeElapsedTime = 0;
    std::int32_t nextSiegeStartTime = 0;
    ::Disassemble(packet, "dddd", &siegeState, &previousSiegeEndTime, &siegeElapsedTime, &nextSiegeStartTime);

    CDominionManager::Inst()->ChangeState(static_cast<DominionSiegeStateType>(siegeState), false);
    CDominionManager::Inst()->SetSiegeEndTime(previousSiegeEndTime, false);
    CDominionManager::Inst()->m_tSiegeElapsedTime = siegeElapsedTime;
    if (siegeState == DSST_DOMINION_SIEGE) {
        CDominionManager::Inst()->m_bServerDownResume = true;
        const std::int32_t now = runtime::crt::Time32Now();
        CDominionManager::Inst()->SetNextSiegeTime(now, true);
    } else if (nextSiegeStartTime > 0) {
        CDominionManager::Inst()->SetNextSiegeTime(nextSiegeStartTime, false);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid start time in dominion manager. %d",
        //     nextSiegeStartTime)
        // L2_LOG_ERROR(Log,
        //     L"Invalid start time in dominion manager. %d",
        //     nextSiegeStartTime)
        L2_LOG_ERROR(
            Log, "Dominion siege reply has invalid next start time %d; using the current-time fallback", nextSiegeStartTime
        );
        const std::int32_t now = runtime::crt::Time32Now();
        CDominionManager::Inst()->SetNextSiegeTime(now, true);
    }
    return false;
}

bool ReplyLoadDominionRenamed(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00682efc, CacheD RP_EX 0x6c.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t renamed = 0;
    ::Disassemble(packet, "dd", &characterId, &renamed);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->RenameByDominionSiege(renamed != 0, false);
    }
    return false;
}

bool ReplyLoadBirthInfo(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00682c4c, CacheD RP_EX 0x69.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t birthdayGiftCount = 0;
    ::Disassemble(packet, "dd", &characterId, &birthdayGiftCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->m_data->m_nGetBirthdayGiftCount = birthdayGiftCount;
    }
    return false;
}

bool ReplyGetLastBirthdayGiftDate(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006829f0, CacheD RP_EX 0x6a.
    L2_GUARD_FUNCTION();

    SYSTEMTIME createDate{};
    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm currentLocalTime = *DBPacketExLocalTime32(now);

    std::int32_t characterId = 0;
    std::int32_t lastGiftYear = 0;
    std::int32_t lastGiftMonth = 0;
    std::int32_t lastGiftDay = 0;
    ::Disassemble(packet, "ddddb", &characterId, &lastGiftYear, &lastGiftMonth, &lastGiftDay, 0x10, &createDate);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        std::memcpy(&user->m_data->m_tCreateDate, &createDate, sizeof(createDate));
        user->SetGetbirthdayGiftDate(lastGiftYear, lastGiftMonth, lastGiftDay);

        if (g_bBirthdayEventStart > 0 && g_bBirthdayEventStart <= now && g_nBirthdayItemClassID > 0) {
            const std::tm* const eventStart = DBPacketExLocalTime32(g_bBirthdayEventStart);
            const int eventMonth = eventStart->tm_mon + 1;
            if (eventStart->tm_year < currentLocalTime.tm_year || eventMonth < static_cast<int>(createDate.wMonth) ||
                (eventMonth == static_cast<int>(createDate.wMonth) &&
                    eventStart->tm_mday <= static_cast<int>(createDate.wDay))) {
                user->ReportCreateDate();
            }
        }
    }
    return false;
}

bool ReplyLoad5YearInfo(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00682d68, CacheD RP_EX 0x6b.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t fiveYearGiftCount = 0;
    SYSTEMTIME createDate{};
    ::Disassemble(packet, "ddb", &characterId, &fiveYearGiftCount, 0x10, &createDate);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        std::memcpy(&user->m_data->m_tCreateDate, &createDate, sizeof(createDate));
        user->m_data->m_nGet5YearGiftCount = fiveYearGiftCount;
    }
    return false;
}

bool ReplyBrGetLectureMark(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00687578, RP_EX 0x93.
    L2_GUARD_FUNCTION();

    std::int32_t accountId;
    char lectureMark;
    char message;
    ::Disassemble(packet, "dcc", &accountId, &lectureMark, &message);

    CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyBrGetLectureMark fail, AccountId = %d",
        //     L"DBPacketEx.cpp",
        //     0x17f4,
        //     accountId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReplyBrGetLectureMark fail, AccountId = %d",
        //     L"DBPacketEx.cpp",
        //     0x17f4,
        //     accountId)
        L2_LOG_ERROR(Log, "Lecture-mark load reply ignored: no active user for account %d", accountId);
        return false;
    }

    user->m_data->m_NeedUpdatePacketBitSet.SetCharValue_IfModifed(user->m_cLectureMark, lectureMark, 0x2d);
    user->ParamChanged(std::int64_t{PCB_CHAR_LECTUREMARK});
    if (message == 1 || message == 3) {
        user->SendSystemMessage(SystemMessageId::EVANGELIST_MARK_AVAILABLE);
    }
    if (message == 2 || message == 3) {
        user->SendSystemMessage(SystemMessageId::INITIAL_STAGE_COMPLETED);
    }
    return false;
}

bool ReplyBrSetLectureMark(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006877dc, RP_EX 0x94.
    L2_GUARD_FUNCTION();

    std::int32_t accountId;
    char lectureMark;
    ::Disassemble(packet, "dc", &accountId, &lectureMark);

    CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyBrSetLectureMark fail, AccountId = %d",
        //     L"DBPacketEx.cpp",
        //     0x1817,
        //     accountId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReplyBrSetLectureMark fail, AccountId = %d",
        //     L"DBPacketEx.cpp",
        //     0x1817,
        //     accountId)
        L2_LOG_ERROR(Log, "Lecture-mark update reply ignored: no active user for account %d", accountId);
        return false;
    }

    user->m_data->m_NeedUpdatePacketBitSet.SetCharValue_IfModifed(user->m_cLectureMark, lectureMark, 0x2d);
    user->ParamChanged(std::int64_t{PCB_CHAR_LECTUREMARK});
    return false;
}

bool ReplyBrSetCreateDate(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId;
    std::int32_t successfulYear;
    std::int32_t month;
    std::int32_t day;
    std::int32_t hour;
    std::int32_t minute;
    ::Disassemble(packet, "dddddd", &characterId, &successfulYear, &month, &day, &hour, &minute);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        char16_t message[0x100]{};
        message[0] = u'0';
        const char16_t* const format =
            successfulYear == 0 ? u"set_create_date fail, %d/%d/%d %02d:%02d" : u"set_create_date ok, %d/%d/%d %02d:%02d";
        FormatUtf16To(message, std::size(message), format, successfulYear, month, day, hour, minute);
        char16_t systemContext[] = u"SYS";
        user->SendSystemMessage(systemContext, message);
    }
    return false;
}

bool ReplyBrLoadDropItemAmountPerServer(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a1380, RP_EX 0x96.
    L2_GUARD_FUNCTION();

    std::int32_t itemCount;
    const unsigned char* cursor = ::Disassemble(packet, "d", &itemCount);
    for (std::int32_t index = 0; index < itemCount; ++index) {
        std::int32_t itemClassId;
        std::int32_t amount;
        std::int32_t timestamp;
        cursor = ::Disassemble(cursor, "ddd", &itemClassId, &amount, &timestamp);
        Singleton<CEventManager>::inst().SetDropItemAmountPerServer(itemClassId, amount, timestamp);
    }
    return false;
}

bool ReplyBrSaveDropItemAmountPerServer(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a1484, RP_EX 0x97.
    L2_GUARD_FUNCTION();

    std::int32_t itemClassId;
    std::int32_t amount;
    ::Disassemble(packet, "dd", &itemClassId, &amount);
    Singleton<CEventManager>::inst().SetDropItemAmountPerServer(itemClassId, amount, runtime::crt::Time32Now());
    return false;
}

bool LoadOlympiadMatchCount(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t count = 0;
    packet = ::Disassemble(packet, "d", &count);
    for (std::int32_t index = 0; index < count; ++index) {
        std::int32_t characterId = 0;
        std::int32_t season = 0;
        std::int32_t step = 0;
        std::int32_t gameRule = 3;
        std::int32_t matchCount = 0;
        packet = ::Disassemble(packet, "ddddd", &characterId, &season, &step, &gameRule, &matchCount);
        g_JobFinishSignal.SetMatchCountLoaded();
    }
    return false;
}

bool GetOlympiadPlayerCount(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t playerCount = 0;
    ::Disassemble(packet, "d", &playerCount);
    SharedOlympiadInfo::GetInfo()->m_nSeasonPlayerCount = playerCount;
    return false;
}

bool SendTutorial(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userSocketId;
    std::array<unsigned char, 0x80> tutorial{};
    ::Disassemble(packet, "db", &userSocketId, static_cast<int>(tutorial.size()), tutorial.data());

    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(userSocketId);
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBPacketEx.cpp",
        //     0xc97)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBPacketEx.cpp",
        //     0xc97)
        L2_LOG_ERROR(
            Log, "Tutorial data reply cannot be delivered: user socket %u is unavailable",
            static_cast<unsigned int>(userSocketId)
        );
        return false;
    }
    socket->Send("chb", 0xfe, 0x6c, static_cast<int>(tutorial.size()), tutorial.data());
    return false;
}

bool SendUISetting(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userSocketId;
    std::uint32_t settingSize = 0;
    std::array<unsigned char, 8000> buffer{};
    const unsigned char* const setting = ::Disassemble(packet, "dd", &userSocketId, &settingSize);
    if (settingSize - 1U >= 8000U) {
        return false;
    }

    ::Disassemble(setting, "b", static_cast<int>(settingSize), buffer.data());
    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(userSocketId);
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBPacketEx.cpp",
        //     0xcec)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBPacketEx.cpp",
        //     0xcec)
        L2_LOG_ERROR(
            Log, "UI settings reply cannot be delivered: user socket %u is unavailable, settings bytes=%u",
            static_cast<unsigned int>(userSocketId), static_cast<unsigned int>(settingSize)
        );
        return false;
    }
    socket->Send("chdb", 0xfe, 0x71, static_cast<int>(settingSize), static_cast<int>(settingSize), buffer.data());
    return false;
}

namespace PeriodicItemJobHandler {

void OnLoadInfo(User* const user, const unsigned char* const packet) {
    // Current L2Server 0x00671d2c, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t itemCount;
    const unsigned char* const cursor = ::Disassemble(packet, "d", &itemCount);
    user->OnLoadPeriodicItemInfo(itemCount, cursor);
}

void OnExpired(User* const user, const unsigned char* const packet) {
    // Current L2Server 0x00679f48, original owner DBPacketEx.cpp. The
    // paired reply returns server ID before database ID. The
    // current consumer then uses the latter for smart lookup and the former
    // as the periodic-map key.
    L2_GUARD_FUNCTION();

    std::int32_t itemServerId;
    std::int32_t itemDatabaseId;
    ::Disassemble(packet, "dd", &itemServerId, &itemDatabaseId);
    if (itemServerId > 0) {
        CItem* const item = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(itemDatabaseId));
        user->OnPeriodicItemDestroyed(item, itemServerId);
    }
}

} // namespace PeriodicItemJobHandler

namespace PeriodicShapeShiftedItemJobHandler {

void OnLoadInfo(User* user, const unsigned char* packet) {
    // Current L2Server 0x006726fc, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t itemCount = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "d", &itemCount);
    user->OnLoadPeriodicShapeShiftedItemInfo(itemCount, cursor);
}

} // namespace PeriodicShapeShiftedItemJobHandler

namespace PeriodicHennaJobHandler {

void OnLoadInfo(User* const user, const unsigned char* const packet) {
    // Current L2Server 0x00672894, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();
    user->OnLoadPeriodicHennaInfo(packet);
}

void OnExpired(User* const user, const unsigned char* const packet) {
    // Current L2Server 0x0067293c, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t hennaId = 0;
    ::Disassemble(packet, "d", &hennaId);
    if (hennaId > 0) {
        user->GetSocket()->Send(
            "chccd", 0x62, SystemMessageId::NAMED_OBJECT_DISAPPEARED_AFTER_USAGE_EXPIRATION, 1, 0x16, hennaId
        );
        user->OnPremiumHennaExpired();
    }
}

void OnSetPeriod(User* const user, const unsigned char* const packet) {
    // Current L2Server 0x00672a50, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t hennaId = 0;
    std::int32_t expireTime = 0;
    ::Disassemble(packet, "dd", &hennaId, &expireTime);
    CHenna* const henna = g_HennaDB.Find(hennaId);
    if (henna != nullptr) {
        user->AddHennaFromBD(hennaId, henna->IsPremiumHenna(), expireTime);
        user->SendPeriodicHennaInfo(hennaId);
    }
}

} // namespace PeriodicHennaJobHandler

namespace DBPacketExHandler {

bool ReplyUpdatePledgeNameValue(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067732c, RP_EX 0x2c.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::uint32_t pledgeServerId;
    std::int32_t pledgeDatabaseId;
    std::int32_t ignoredRequestContext;
    std::int32_t nameValueDelta;
    std::int32_t result;
    const unsigned char* cursor = ::Disassemble(
        packet, "dddddd", &userServerId, &pledgeServerId, &pledgeDatabaseId, &ignoredRequestContext, &nameValueDelta, &result
    );
    static_cast<void>(ignoredRequestContext);

    const bool handledResult =
        result == 0 || result == -1 || result == -2 || result == 1 || result == 2 || result == 3 || result == 4;
    if (!handledResult) {
        if (result == -4 || result == -3) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%S][%d] : pledge namevalue update failed : "
            //     "Pledge sid(%d), dbId(%d), namevalue delta(%d), "
            //     "nResult(%d)",
            //     L"DBPacketEx.cpp",
            //     0x5be,
            //     pledgeServerId,
            //     pledgeDatabaseId,
            //     nameValueDelta,
            //     result)
            // L2_LOG_ERROR(Log,
            //     "[%S][%d] : pledge namevalue update failed : "
            //     "Pledge sid(%d), dbId(%d), namevalue delta(%d), "
            //     "nResult(%d)",
            //     L"DBPacketEx.cpp",
            //     0x5be,
            //     pledgeServerId,
            //     pledgeDatabaseId,
            //     nameValueDelta,
            //     result)
            L2_LOG_ERROR(
                Log, "Clan reputation update failed: clan object=%u, clan DB ID=%d, requested change=%d, CacheD result=%d",
                static_cast<unsigned int>(pledgeServerId), pledgeDatabaseId, nameValueDelta, result
            );
            if (result == -3) {
                CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
                User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
                if (user != nullptr) {
                    user->GetSocket()->SendSystemMessage(SystemMessageId::NOT_ENOUGH_CLAN_REPUTATION);
                }
            }
        }
        return false;
    }

    CPledge* pledge = nullptr;
    if (pledgeServerId == 0) {
        CSPointer<CPledge> found = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
        pledge = found.get();
    } else {
        pledge = CPledge::GetObjectW(pledgeServerId);
    }
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%S][%d] : pledge namevalue update failed : "
        //     "Pledge sid(%d), dbId(%d), namevalue delta(%d), "
        //     "nResult(%d)",
        //     L"DBPacketEx.cpp",
        //     0x520,
        //     pledgeServerId,
        //     pledgeDatabaseId,
        //     nameValueDelta,
        //     result)
        // L2_LOG_ERROR(Log,
        //     "[%S][%d] : pledge namevalue update failed : "
        //     "Pledge sid(%d), dbId(%d), namevalue delta(%d), "
        //     "nResult(%d)",
        //     L"DBPacketEx.cpp",
        //     0x520,
        //     pledgeServerId,
        //     pledgeDatabaseId,
        //     nameValueDelta,
        //     result)
        L2_LOG_ERROR(
            Log, "Clan reputation reply cannot be applied: clan is unavailable, object=%u, DB ID=%d, change=%d, result=%d",
            static_cast<unsigned int>(pledgeServerId), pledgeDatabaseId, nameValueDelta, result
        );
        return false;
    }

    pledge->UpdateNameValue(nameValueDelta);
    switch (result) {
        case -2:
            if (nameValueDelta < 0) {
                CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
                User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
                if (user != nullptr) {
                    pledge->SystemMessage(
                        SystemMessageId::CLAN_REPUTATION_CONSUMED_BY_CHARACTER_ITEM_PURCHASE, EST_CHAR_NAME, user->GetName(),
                        runtime::SubtractInt32Wrapping(0, nameValueDelta)
                    );
                }
            }
            break;

        case -1:
            if (nameValueDelta < 0) {
                CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
                User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
                if (user != nullptr) {
                    user->GetSocket()->SendSystemMessage_d(
                        SystemMessageId::CLAN_REPUTATION_CONSUMED, runtime::SubtractInt32Wrapping(0, nameValueDelta)
                    );
                }
            }
            break;

        case 0:
            pledge->SystemMessage(
                nameValueDelta > 0 ? SystemMessageId::CLAN_REPUTATION_GAINED : SystemMessageId::CLAN_REPUTATION_LOST,
                nameValueDelta
            );
            break;

        case 1: {
            std::int32_t parameter = 0;
            ::Disassemble(cursor, "d", &parameter);
            pledge->SystemMessage(
                SystemMessageId::ACADEMY_GRADUATE_COUNT_GRANTED_BONUS_CLAN_REPUTATION, parameter, nameValueDelta
            );
            break;
        }

        case 2: {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
            User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
            if (user != nullptr) {
                std::int32_t ignoredParameter = 0;
                ::Disassemble(cursor, "d", &ignoredParameter);
                static_cast<void>(ignoredParameter);
                pledge->SystemMessage(
                    SystemMessageId::ACADEMY_MEMBER_AWAKENED_CLAN_REPUTATION_GAINED, EST_CHAR_NAME, user->GetName(),
                    nameValueDelta
                );
            }
            break;
        }

        case 3: {
            std::int32_t otherPledgeDatabaseId;
            ::Disassemble(cursor, "d", &otherPledgeDatabaseId);
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
            User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
            if (user == nullptr) {
                break;
            }

            CPledge* otherPledge = nullptr;
            {
                CSPointer<CPledge> found =
                    CPledge::FindPledge(runtime::process::CurrentDatabaseId(), otherPledgeDatabaseId);
                otherPledge = found.get();
            }
            if (otherPledge == nullptr) {
                break;
            }

            if (nameValueDelta < 1) {
                pledge->SystemMessage(
                    SystemMessageId::CLAN_REPUTATION_LOST_FOR_MEMBER_DEATH, EST_CHAR_NAME, user->GetName(), EST_STRING,
                    std::u16string(otherPledge->GetPledgeName()).c_str(), nameValueDelta
                );
            } else {
                pledge->SystemMessage(
                    SystemMessageId::CLAN_REPUTATION_GAINED_FOR_ENEMY_MEMBER_DEATH, EST_STRING,
                    std::u16string(otherPledge->GetPledgeName()).c_str(), EST_CHAR_NAME, user->GetName(), nameValueDelta
                );
            }
            break;
        }

        case 4:
            if (nameValueDelta > 0) {
                CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
                User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
                if (user == nullptr) {
                    pledge->SystemMessage(SystemMessageId::CLAN_REPUTATION_GAINED, nameValueDelta);
                } else {
                    pledge->SystemMessage(
                        SystemMessageId::CHARACTER_LEVEL_UP_GRANTED_CLAN_REPUTATION, EST_CHAR_NAME, user->GetName(),
                        nameValueDelta
                    );
                }
                std::int32_t ignoredFirst = 0;
                std::int32_t ignoredSecond = 0;
                std::int32_t ignoredThird = 0;
                ::Disassemble(cursor, "ddd", &ignoredFirst, &ignoredSecond, &ignoredThird);
            }
            break;

        default:
            break;
    }
    return false;
}

bool ReplyPledgeLoadOnPledgeRecruitBoard(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t pledgeDatabaseId = 0;
    ::Disassemble(packet, "d", &pledgeDatabaseId);
    CPledge* pledge = nullptr;
    {
        CSPointer<CPledge> found = CPledge::FindPledge(g_nWorldId, pledgeDatabaseId);
        pledge = found.get();
    }
    if (pledge == nullptr) {
        db.RequestLoadPledge(g_nWorldId, static_cast<unsigned int>(pledgeDatabaseId), false);
    }
    return false;
}

bool ReplyPledgeNotifyTableInit(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006761a0, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t pledgeDatabaseId = 0;
    std::int32_t notifyTime = 0;
    ::Disassemble(packet, "dd", &pledgeDatabaseId, &notifyTime);
    CPledgeJoinSystem::Inst()->SetNotifyTable(pledgeDatabaseId, notifyTime);
    return false;
}

bool ReplySearchPledgeRecruitBoard(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068d0b0, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t worldId = 0;
    std::int32_t firstReplyValue = 0;
    std::int32_t secondReplyValue = 0;
    std::int32_t pledgeCount = 0;
    packet = ::Disassemble(packet, "ddddd", &characterId, &worldId, &firstReplyValue, &secondReplyValue, &pledgeCount);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(worldId, characterId));
    if (!user) {
        return false;
    }

    CPacketHelperEx<20480> pledgeRows;
    for (std::int32_t index = 0; index < pledgeCount; ++index) {
        std::int32_t pledgeDatabaseId = 0;
        packet = ::Disassemble(packet, "d", &pledgeDatabaseId);

        CPledge* pledge = nullptr;
        {
            CSPointer<CPledge> found = CPledge::FindPledge(worldId, pledgeDatabaseId);
            pledge = found.get();
        }
        if (pledge == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Pledge is not loaded before used")
            // L2_LOG_ERROR(Log,
            //     L"Pledge is not loaded before used")
            L2_LOG_ERROR(
                Log, "Clan recruitment search entry skipped: clan DB ID=%d is not loaded in world %d", pledgeDatabaseId,
                worldId
            );
            continue;
        }

        const std::int32_t allianceDatabaseId = pledge->GetAllianceDbId();
        const std::int32_t pledgeServerId = static_cast<std::int32_t>(pledge->Id());
        pledgeRows.Assemble("dd", pledgeServerId, allianceDatabaseId);
    }

    std::int32_t trailingSize = 0;
    packet = ::Disassemble(packet, "d", &trailingSize);
    user->GetSocket()->Send(
        "chdddbb", 0xfe, 0x141, firstReplyValue, secondReplyValue, pledgeCount, pledgeRows.GetLength(),
        pledgeRows.GetPacket(), trailingSize, packet
    );
    return false;
}

bool ReplyItemCheck(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::uint32_t accountId = 0;
    std::int32_t result = 0;
    std::int32_t action = 0;
    ::Disassemble(packet, "dddd", &characterId, &accountId, &result, &action);
    if (result != 0) {
        return false;
    }

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (action == 2) {
        if (user) {
            user->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        } else {
            {
                L2_GUARD(accountMapSect);
                const auto account = accountMap.find(accountId);
                if (account != accountMap.end()) {
                    account->second->ForceClose(SCR_ILLEGAL_BEHAVIOR);
                }
            }
        }
    } else if (action == 3) {
        if (user) {
            user->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        } else {
            {
                L2_GUARD(accountMapSect);
                const auto account = accountMap.find(accountId);
                if (account != accountMap.end()) {
                    account->second->ForceClose(SCR_ILLEGAL_BEHAVIOR);
                }
            }
        }
        authd.RequestBanAccount(static_cast<int>(accountId), Auth::ChangeItemDataOnDB);
    }
    return false;
}

bool ReplyCheckCharName(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userSocketId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &userSocketId, &result);
    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(userSocketId);
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBPacketEx.cpp",
        //     0x1ebb)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBPacketEx.cpp",
        //     0x1ebb)
        L2_LOG_ERROR(
            Log, "Character-name check reply cannot be delivered: user socket %u is unavailable",
            static_cast<unsigned int>(userSocketId)
        );
        return false;
    }
    socket->Send("chd", 0xfe, 0x10b, result);
    return false;
}

bool ReplyLoadAccountMaxLevel(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t accountId = 0;
    std::int32_t characterId = 0;
    std::int32_t characterLevel = 0;
    ::Disassemble(packet, "ddd", &accountId, &characterId, &characterLevel);
    authd.AuthCheckCharMaxLevel(accountId, characterId, characterLevel);
    return false;
}

bool ReplySaveSummonInfectedSkills(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0069876c, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId = 0;
    std::int32_t characterId = 0;
    std::int32_t summonClassId = 0;
    std::int32_t summonIndex = 0;
    std::int32_t atomicJobId = 0;
    ::Disassemble(packet, "dddddd", &result, &userServerId, &characterId, &summonClassId, &summonIndex, &atomicJobId);
    static_cast<void>(result);
    static_cast<void>(characterId);
    static_cast<void>(summonClassId);
    static_cast<void>(summonIndex);

    AtomicJob* const atomicJob = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Pop(atomicJobId);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr && atomicJob != nullptr && atomicJob == g_AtomicPetDBJob) {
        static_cast<void>(user->OnPetStep(2));
    }
    return false;
}

bool ReplyDeleteSummon(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006988c8, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId = 0;
    std::int32_t characterId = 0;
    std::int32_t summonIndex = 0;
    std::int32_t atomicJobId = 0;
    ::Disassemble(packet, "ddddd", &result, &userServerId, &characterId, &summonIndex, &atomicJobId);
    static_cast<void>(result);
    static_cast<void>(characterId);
    static_cast<void>(summonIndex);

    AtomicJob* const atomicJob = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Pop(atomicJobId);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr && atomicJob != nullptr && atomicJob == g_AtomicPetDBJob) {
        static_cast<void>(user->OnPetStep(4));
    }
    return false;
}

bool ReplySavePet(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00698a18, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t petDatabaseId = 0;
    std::int32_t userServerId = 0;
    std::int32_t atomicJobId = 0;
    ::Disassemble(packet, "dddd", &result, &petDatabaseId, &userServerId, &atomicJobId);
    static_cast<void>(petDatabaseId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    AtomicJob* const atomicJob = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Pop(atomicJobId);
    if (result == 0 && user != nullptr && atomicJob != nullptr && atomicJob == g_AtomicPetDBJob) {
        static_cast<void>(user->OnPetStep(5));
    }
    return false;
}

bool NewManipulateItemPacket(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00698b60, RP_EX 0xf0.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t manipulateType = 0x23;
    std::int32_t requestContext = 0;
    std::int32_t atomicJobId = -1;
    std::int32_t etcManipulationCount = 0;
    std::int32_t itemCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "dddddd", &userServerId, &manipulateType, &requestContext, &atomicJobId, &etcManipulationCount, &itemCount
    );

    AtomicJob* const atomicJob = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Pop(atomicJobId);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x27e0,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x27e0,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Item manipulation reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        if (atomicJob != nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] User[sid:%d]'s AtomicJob is ignored. "
            //     L"AtomicJobType[%d]",
            //     L"DBPacketEx.cpp",
            //     0x27e4,
            //     userServerId,
            //     static_cast<int>(atomicJob->eType))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] User[sid:%d]'s AtomicJob is ignored. "
            //     L"AtomicJobType[%d]",
            //     L"DBPacketEx.cpp",
            //     0x27e4,
            //     userServerId,
            //     static_cast<int>(atomicJob->eType))
            L2_LOG_ERROR(
                Log, "Item manipulation atomic job cannot run without its user: user object=%u, job type=%d",
                static_cast<unsigned int>(userServerId), static_cast<int>(atomicJob->eType)
            );
        }
        return false;
    }

    static_cast<void>(user->GetSocket());
    const ManipulateType::Enum typedManipulateType = static_cast<ManipulateType::Enum>(manipulateType);
    if (itemCount < 0 || etcManipulationCount < 0) {
        const int userDatabaseId = static_cast<int>(user->GetDBID());
        const char16_t* const userName = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"Kicked for invalid items. (%s)(%d)",
        //     userName,
        //     userDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"Kicked for invalid items. (%s)(%d)",
        //     userName,
        //     userDatabaseId)
        // L2_LOG_ERROR(Log,
        //     "Item manipulation reply rejected: negative counts, user=%s, character DB ID=%d, items=%d, extra
        //     operations=%d, closing connection if available",
        //     ::WideToUtf8(userName).c_str(), userDatabaseId, itemCount, etcManipulationCount)
        L2_LOG_ERROR(
            Log,
            "Item manipulation reply rejected: negative counts, user=%s, character DB ID=%d, items=%d, extra operations=%d; "
            "closing connection if available",
            ::Utf16ToUtf8(userName).c_str(), userDatabaseId, itemCount, etcManipulationCount
        );
        user->StopMove();
        CUserSocket* const userSocket = user->GetSocket();
        if (userSocket != nullptr) {
            userSocket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        }
        if (atomicJob != nullptr) {
            atomicJob->Destroy();
        }
        {
            L2_GUARD(user->m_ManipulateQueueLock);
            user->_DoneManiItemJob(typedManipulateType, requestContext);
        }
        return false;
    }

    if (itemCount == 0 && etcManipulationCount == 0) {
        if (atomicJob != nullptr) {
            atomicJob->Destroy();
        }
        {
            L2_GUARD(user->m_ManipulateQueueLock);
            user->_DoneManiItemJob(typedManipulateType, requestContext);
        }
        return false;
    }

    cursor = L2EtcMani::ParseEtcMani(etcManipulationCount, cursor);
    std::vector<int> removedItemClassIds;
    std::vector<std::pair<int, int>> addedItems;

    while (itemCount-- > 0) {
        CItemDisassemblerItemMani itemData;
        cursor = itemData.DisassembleItemInfo(cursor);

        if (itemData.m_nIndex == 1) {
            CItem* const item = g_ObjectDB.CreateItem(itemData.m_nClassID);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] CreateItem Failed",
                //     L"DBPacketEx.cpp",
                //     0x294a)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] CreateItem Failed",
                //     L"DBPacketEx.cpp",
                //     0x294a)
                L2_LOG_ERROR(
                    Log, "Item creation from CacheD reply failed: class=%d, item DB ID=%d, user object=%u",
                    itemData.m_nClassID, itemData.m_nDBID, static_cast<unsigned int>(userServerId)
                );
                continue;
            }

            item->SetDBID(static_cast<unsigned int>(itemData.m_nDBID));
            if (!item->SetAmount(itemData.m_nChangeAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                //     L"DBPacketEx.cpp",
                //     0x290b,
                //     item->m_data->m_nCount,
                //     itemData.m_nChangeAmount)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                //     L"DBPacketEx.cpp",
                //     0x290b,
                //     item->m_data->m_nCount,
                //     itemData.m_nChangeAmount)
                L2_LOG_ERROR(
                    Log,
                    "Item initial amount assignment failed: item DB ID=%d, current amount=%lld, reply change=%lld; "
                    "processing continues",
                    itemData.m_nDBID, static_cast<long long>(item->m_data->m_nCount),
                    static_cast<long long>(itemData.m_nChangeAmount)
                );
            }
            item->SetEnchanted(itemData.m_nEnchanted);
            item->m_Useable = user->CheckUseable(item);
            item->m_data->m_nDamaged = itemData.m_nDamaged;
            item->m_data->m_bBlessed = itemData.m_nIsBlessed;
            item->SetVariation(
                ItemOptionKey{itemData.m_nVariation1, itemData.m_nVariation2},
                static_cast<std::uint32_t>(itemData.m_nIntensiveItemClassID)
            );
            item->SetAttribute(itemData.m_Attribute);
            if (item->IsDuration()) {
                db.UpdateItemDuration(*user, static_cast<unsigned int>(itemData.m_nDBID), item->GetDuration());
            }
            if (item->m_const->m_period > 0) {
                user->OnGetPeriodicItem(item);
            }
            if (itemData.m_nShapeShiftedClassID > 0) {
                item->SetShapeShiftingClassId(itemData.m_nShapeShiftedClassID);
                if (itemData.m_nShapeShiftedPeriod >= 0) {
                    user->OnGetPeriodicShapeShiftedItem(item, itemData.m_nShapeShiftedPeriod);
                    user->SendPeriodicShapeShiftedItemList();
                }
            }
            if (item->m_const->m_dAgathionMaxEnergy > 0.0) {
                item->SetAgathionEnergy(item->m_const->m_dAgathionMaxEnergy);
                user->SendBrAgathionEnergyInfo();
                db.RequestSaveItemAgathionEnergy(
                    user, static_cast<int>(item->m_data->m_nId), static_cast<int>(item->GetDBID()), item->GetAgathionEnergy()
                );
            }
            item->SetEnsoulOptionData(itemData.m_mEnsoulInfo, true);
            item->m_data->SetLocked(itemData.m_hLocked);
            item->m_data->SetUseCount(itemData.m_hUseCount);
            if (!user->m_inv.Push(item->m_data->index)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "inven push failed at file[%s], line[%d]",
                //     "DBPacketEx.cpp",
                //     0x293b)
                // L2_LOG_ERROR(Log,
                //     "inven push failed at file[%s], line[%d]",
                //     "DBPacketEx.cpp",
                //     0x293b)
                L2_LOG_ERROR(
                    Log,
                    "New item could not be inserted into inventory: item DB ID=%d, class=%d, user object=%u; item "
                    "processing continues",
                    itemData.m_nDBID, itemData.m_nClassID, static_cast<unsigned int>(userServerId)
                );
            }
            user->AutoEquipArrowOrBolt(item);
            user->m_inv.SetInventoryChanged(item, IUT_ADD);
            user->SendItemIncreaseSystemMessage(itemData.m_nClassID, itemData.m_nChangeAmount, item->m_data->GetEnchanted());
            addedItems.push_back(std::make_pair(item->GetClassId(), item->m_data->GetEnchanted()));
            continue;
        }

        if (itemData.m_nIndex == 2) {
            CItem* const item = user->m_inv.GetByDBID(itemData.m_nDBID);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] pItemInv == NULL, bad. "
                //     L"[%s][%d][%d][%I64d][%I64d]",
                //     L"DBPacketEx.cpp",
                //     0x2966,
                //     user->GetName(),
                //     itemData.m_nDBID,
                //     itemData.m_nClassID,
                //     itemData.m_nChangeAmount,
                //     itemData.m_nAmount)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] pItemInv == NULL, bad. "
                //     L"[%s][%d][%d][%I64d][%I64d]",
                //     L"DBPacketEx.cpp",
                //     0x2966,
                //     user->GetName(),
                //     itemData.m_nDBID,
                //     itemData.m_nClassID,
                //     itemData.m_nChangeAmount,
                //     itemData.m_nAmount)
                // L2_LOG_ERROR(Log,
                //     "Item increase entry skipped: inventory item is missing, user=%s, item DB ID=%d, class=%d,
                //     change=%lld, reply amount=%lld",
                //     ::WideToUtf8(user->GetName()).c_str(), itemData.m_nDBID, itemData.m_nClassID,
                //     static_cast<long long>(itemData.m_nChangeAmount), static_cast<long long>(itemData.m_nAmount))
                L2_LOG_ERROR(
                    Log,
                    "Item increase entry skipped: inventory item is missing, user=%s, item DB ID=%d, class=%d, change=%lld, "
                    "reply amount=%lld",
                    ::Utf16ToUtf8(user->GetName()).c_str(), itemData.m_nDBID, itemData.m_nClassID,
                    static_cast<long long>(itemData.m_nChangeAmount), static_cast<long long>(itemData.m_nAmount)
                );
                continue;
            }
            {
                L2_GUARD(*item->m_lock);
                if (!item->AddAmount(itemData.m_nChangeAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                    //     L"DBPacketEx.cpp",
                    //     0x2959,
                    //     item->m_data->m_nCount,
                    //     itemData.m_nChangeAmount)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                    //     L"DBPacketEx.cpp",
                    //     0x2959,
                    //     item->m_data->m_nCount,
                    //     itemData.m_nChangeAmount)
                    L2_LOG_ERROR(
                        Log,
                        "Item amount increase failed: item DB ID=%d, current amount=%lld, reply change=%lld; processing "
                        "continues",
                        itemData.m_nDBID, static_cast<long long>(item->m_data->m_nCount),
                        static_cast<long long>(itemData.m_nChangeAmount)
                    );
                }
            }
            user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
            user->SendItemIncreaseSystemMessage(itemData.m_nClassID, itemData.m_nChangeAmount, item->m_data->GetEnchanted());
            addedItems.push_back(std::make_pair(item->GetClassId(), item->m_data->GetEnchanted()));
            continue;
        }

        if (itemData.m_nIndex == 3) {
            CItem* const item = user->m_inv.GetByDBID(itemData.m_nDBID);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] pItemInv == NULL, bad. "
                //     L"[%s][%d][%d][%I64d][%I64d]",
                //     L"DBPacketEx.cpp",
                //     0x2981,
                //     user->GetName(),
                //     itemData.m_nDBID,
                //     itemData.m_nClassID,
                //     itemData.m_nChangeAmount,
                //     itemData.m_nAmount)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] pItemInv == NULL, bad. "
                //     L"[%s][%d][%d][%I64d][%I64d]",
                //     L"DBPacketEx.cpp",
                //     0x2981,
                //     user->GetName(),
                //     itemData.m_nDBID,
                //     itemData.m_nClassID,
                //     itemData.m_nChangeAmount,
                //     itemData.m_nAmount)
                // L2_LOG_ERROR(Log,
                //     "Item decrease entry skipped: inventory item is missing, user=%s, item DB ID=%d, class=%d,
                //     change=%lld, reply amount=%lld",
                //     ::WideToUtf8(user->GetName()).c_str(), itemData.m_nDBID, itemData.m_nClassID,
                //     static_cast<long long>(itemData.m_nChangeAmount), static_cast<long long>(itemData.m_nAmount))
                L2_LOG_ERROR(
                    Log,
                    "Item decrease entry skipped: inventory item is missing, user=%s, item DB ID=%d, class=%d, change=%lld, "
                    "reply amount=%lld",
                    ::Utf16ToUtf8(user->GetName()).c_str(), itemData.m_nDBID, itemData.m_nClassID,
                    static_cast<long long>(itemData.m_nChangeAmount), static_cast<long long>(itemData.m_nAmount)
                );
                continue;
            }
            {
                L2_GUARD(*item->m_lock);
                if (!item->AddAmount(itemData.m_nChangeAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                    //     L"DBPacketEx.cpp",
                    //     0x2974,
                    //     item->m_data->m_nCount,
                    //     itemData.m_nChangeAmount)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                    //     L"DBPacketEx.cpp",
                    //     0x2974,
                    //     item->m_data->m_nCount,
                    //     itemData.m_nChangeAmount)
                    L2_LOG_ERROR(
                        Log,
                        "Item amount decrease failed: item DB ID=%d, current amount=%lld, reply change=%lld; processing "
                        "continues",
                        itemData.m_nDBID, static_cast<long long>(item->m_data->m_nCount),
                        static_cast<long long>(itemData.m_nChangeAmount)
                    );
                }
            }
            user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
            user->SendItemDecreaseSystemMessage(itemData.m_nClassID, itemData.m_nChangeAmount, itemData.m_nEnchanted, true);
            removedItemClassIds.push_back(itemData.m_nClassID);
            continue;
        }

        if (itemData.m_nIndex == 4) {
            CItem* const item = user->m_inv.GetByDBID(itemData.m_nDBID);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] pItemInv == NULL, bad. "
                //     L"[%s][%d][%d][%I64d][%I64d]",
                //     L"DBPacketEx.cpp",
                //     0x298c,
                //     user->GetName(),
                //     itemData.m_nDBID,
                //     itemData.m_nClassID,
                //     itemData.m_nChangeAmount,
                //     itemData.m_nAmount)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] pItemInv == NULL, bad. "
                //     L"[%s][%d][%d][%I64d][%I64d]",
                //     L"DBPacketEx.cpp",
                //     0x298c,
                //     user->GetName(),
                //     itemData.m_nDBID,
                //     itemData.m_nClassID,
                //     itemData.m_nChangeAmount,
                //     itemData.m_nAmount)
                // L2_LOG_ERROR(Log,
                //     "Item deletion entry skipped: inventory item is missing, user=%s, item DB ID=%d, class=%d,
                //     change=%lld, reply amount=%lld",
                //     ::WideToUtf8(user->GetName()).c_str(), itemData.m_nDBID, itemData.m_nClassID,
                //     static_cast<long long>(itemData.m_nChangeAmount), static_cast<long long>(itemData.m_nAmount))
                L2_LOG_ERROR(
                    Log,
                    "Item deletion entry skipped: inventory item is missing, user=%s, item DB ID=%d, class=%d, change=%lld, "
                    "reply amount=%lld",
                    ::Utf16ToUtf8(user->GetName()).c_str(), itemData.m_nDBID, itemData.m_nClassID,
                    static_cast<long long>(itemData.m_nChangeAmount), static_cast<long long>(itemData.m_nAmount)
                );
                continue;
            }
            user->SendItemDecreaseSystemMessage(itemData.m_nClassID, itemData.m_nChangeAmount, itemData.m_nEnchanted, true);
            removedItemClassIds.push_back(item->GetClassId());
            if (itemData.m_nShapeShiftedClassID > 0 && itemData.m_nShapeShiftedPeriod >= 0) {
                user->OnRestorePeriodicShapeShiftedItem(item);
            }
            if (user->UnequipItem(item, true) == ST_NONE) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR][%s][%d] ManipulateItemPacket Insert Queue "
                //     L"ItemDBID[%d], UserDBID[%d]",
                //     L"DBPacketEx.cpp",
                //     0x29a3,
                //     item->GetDBID(),
                //     user->GetDBID())
                // L2_LOG_ERROR(Log,
                //     L"[NO_ERROR][%s][%d] ManipulateItemPacket Insert Queue "
                //     L"ItemDBID[%d], UserDBID[%d]",
                //     L"DBPacketEx.cpp",
                //     0x29a3,
                //     item->GetDBID(),
                //     user->GetDBID())
                L2_LOG_DEBUG(
                    Log, "Item deletion deferred after unequip returned ST_NONE: item database ID=%u, character ID=%u",
                    static_cast<unsigned>(item->GetDBID()), static_cast<unsigned>(user->GetDBID())
                );
            } else {
                user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                user->m_inv.Pop(item->m_data->index, false);
                item->Delete();
            }
            continue;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] manipulate item fail. unknown manipulate type[%d]",
        //     L"DBPacketEx.cpp",
        //     0x29b3,
        //     itemData.m_nIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] manipulate item fail. unknown manipulate type[%d]",
        //     L"DBPacketEx.cpp",
        //     0x29b3,
        //     itemData.m_nIndex)
        L2_LOG_ERROR(
            Log, "Item manipulation entry skipped: unsupported operation=%d, user object=%u", itemData.m_nIndex,
            static_cast<unsigned int>(userServerId)
        );
    }

    if (atomicJob != nullptr) {
        static_cast<void>(atomicJob->Do());
        atomicJob->Destroy();
    }
    user->SendItemList(true, true, nullptr);

    if (g_L2ServerType != L2ServerType::WORLDSERVER && !WOlympiadServiceConfig::inst().IsHost() &&
        !removedItemClassIds.empty() && !addedItems.empty()) {
        ItemAnnounceDB* const announceDatabase = &ItemAnnounceDB::inst();
        CUserBroadcaster* const broadcaster = &CUserBroadcaster::inst();
        RandomBoxItemAnnouncer announcer(broadcaster, announceDatabase);
        std::vector<RandomBoxItemAnnouncer::Announcement> announcements;
        announcer.SelectAnnouncements(announcements, removedItemClassIds, addedItems);
        if (!announcements.empty()) {
            announcer.Announce(user->GetName(), announcements);
        }
    }

    {
        L2_GUARD(user->m_ManipulateQueueLock);
        user->_DoneManiItemJob(typedManipulateType, requestContext);
    }
    return false;
}

bool ResponseDeleteMacroAllEx(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t responseValue = 0;
    std::int32_t ignored = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dddd", &characterId, &responseValue, &ignored, &result);

    User* user = nullptr;
    {
        CSPointer<User> found = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterId));
        user = found.get();
    }

    if (result == 0) {
        char16_t emptyMessage[] = u"";
        user->SendSystemMessage(emptyMessage, u"Delete Macro All Is Failed!");
    } else {
        user->GetSocket()->Send("ccdcc", 0xe8, responseValue, 0, 0, 0);
    }
    return false;
}

bool ResponseMPCCTelBookmarkEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068b964, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t result = 0;
    std::int32_t ignored = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    ::Disassemble(packet, "dddddd", &userServerId, &result, &ignored, &x, &y, &z);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2308,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2308,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Command-channel bookmark teleport reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result == 1) {
        CMultiPartyCommandChannel* const channel = user->GetMPCC();
        if (channel != nullptr) {
            std::vector<User*> members;
            channel->GetAllMember(members);
            const int memberCount = static_cast<int>(members.size());
            for (int index = 0; index < memberCount; ++index) {
                const double randomX = ::genrand64_real2();
                const double randomY = ::genrand64_real2();
                User* const member = runtime::CheckedVectorAt(members, index);
                member->StopMove();
                member->TeleportToLocation(
                    runtime::AddInt32Wrapping(x, static_cast<int>(randomX * 21.0 + 0.0)),
                    runtime::AddInt32Wrapping(y, static_cast<int>(randomY * 21.0 + 0.0)), z, 0, 0x10000,
                    TeleportSourceType::MPCCTelBookMarkPacket, nullptr, 0
                );
            }
        }
    }
    return false;
}

bool ResponseLoadExDropCountEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00699f98, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t lastDropTime = 0;
    std::int32_t count = 0;
    packet = ::Disassemble(packet, "ddd", &characterId, &lastDropTime, &count);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterId));
    if (!user) {
        return false;
    }

    if (count == 0) {
        user->m_tmExDropItemGetTime = runtime::crt::Time32Now();
        return false;
    }

    user->m_tmExDropItemGetTime = lastDropTime;
    {
        L2_GUARD(*user->m_lock);
        for (std::int32_t index = 0; index < count; ++index) {
            std::int32_t countId = 0;
            std::int32_t dropCount = 0;
            packet = ::Disassemble(packet, "dd", &countId, &dropCount);
            user->m_ExDropMap.emplace(countId, ExDropInfo{dropCount, 0.0});
        }

        if (ExDropItemSystem::inst().IsNeedInit(user->m_tmExDropItemGetTime)) {
            user->ResetDropCount();
        }
    }
    return false;
}

bool ResponseBrInterestedProduct(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068c9a0, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    CPacketHelperEx<20480> response;
    CProductSP product;

    std::int32_t characterId = 0;
    std::int32_t productCount = 0;
    packet = ::Disassemble(packet, "dd", &characterId, &productCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    if (productCount < 1) {
        CUserSocket* const socket = user->GetSocket();
        const std::int64_t productPoint = user->m_inv.GetItemAmount(0x5cfd, 0, true);
        const std::int64_t adena = user->m_inv.GetAdenaAmount();
        socket->Send("chQQcd", 0xfe, 0xd7, adena, productPoint, 2, productCount);
        return false;
    }

    std::int32_t validProductCount = 0;
    BrProductItemInfo itemInfo[8];
    for (std::int32_t productIndex = 0; productIndex < productCount; ++productIndex) {
        std::int32_t productId = 0;
        packet = ::Disassemble(packet, "d", &productId);
        product = g_ShopServer.GetProduct(productId);
        if (!product) {
            continue;
        }

        const std::int32_t itemCount = product->GetItemCount();
        for (std::int32_t itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
            CProduct::ItemData* const productItem = product->GetItemData(itemIndex);
            if (productItem == nullptr) {
                continue;
            }

            CObject* const object = g_ObjectDB.GetObjectW(productItem->typeId, false);
            if (object == nullptr || !object->IsItem()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] GetObject Fail",
                //     "DBPacketEx.cpp",
                //     0x24fe)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] GetObject Fail",
                //     "DBPacketEx.cpp",
                //     0x24fe)
                L2_LOG_ERROR(
                    Log,
                    "Interested-product item metadata unavailable: product=%d, item index=%d, class=%d is missing or is not "
                    "an item",
                    productId, itemIndex, productItem->typeId
                );
                continue;
            }

            CItem* const item = static_cast<CItem*>(object);
            // The current body has neither initialization nor
            // an eight-row clamp. Invalid rows keep prior stack bytes and
            // larger products walk past this fixed carrier.
            BrProductItemInfo& info = itemInfo[itemIndex];
            info.itemId = productItem->typeId;
            info.amount = productItem->amount;
            info.weight = item->m_data->m_Type == IT_QUESTITEM ? 0 : item->m_const->m_nWeight;
            info.tradable = item->IsTrade() ? 1 : 0;
        }

        ++validProductCount;
        const std::int32_t maxBirthday = product->GetMaxBirthday();
        const std::int32_t minBirthday = product->GetMinBirthday();
        const std::int32_t saleEndTime = product->GetSaleEndTime();
        const std::int32_t saleStartTime = product->GetSaleStartTime();
        response.Assemble(
            "dccdcdddcccccddcccddddcb", product->m_productId,
            static_cast<int>(static_cast<std::int8_t>(product->m_category)),
            static_cast<int>(static_cast<std::int8_t>(product->m_paymentType)), product->m_price,
            static_cast<int>(static_cast<std::int8_t>(product->m_itemPanelType)), product->m_tab, saleStartTime, saleEndTime,
            static_cast<int>(product->m_dayOfWeek), static_cast<int>(product->m_startTime.hour),
            static_cast<int>(product->m_startTime.minute), static_cast<int>(product->m_endTime.hour),
            static_cast<int>(product->m_endTime.minute), product->m_soldCount, product->m_stock,
            static_cast<int>(static_cast<std::int8_t>(product->m_salePercent)),
            static_cast<int>(static_cast<std::int8_t>(product->m_minLevel)),
            static_cast<int>(static_cast<std::int8_t>(product->m_maxLevel)), minBirthday, maxBirthday,
            product->m_limitPeriod, product->m_limitCount, static_cast<int>(static_cast<std::int8_t>(itemCount)),
            itemCount << 4, itemInfo
        );
    }

    CUserSocket* const socket = user->GetSocket();
    const std::int32_t responseLength = response.GetLength();
    const std::int64_t productPoint = user->m_inv.GetItemAmount(0x5cfd, 0, true);
    const std::int64_t adena = user->m_inv.GetAdenaAmount();
    socket->Send("chQQcdb", 0xfe, 0xd7, adena, productPoint, 2, validProductCount, responseLength, response.GetPacket());
    return false;
}

bool ReplyLoadUserMonsterBook(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    packet = ::Disassemble(packet, "d", &characterId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->OnReplyLoadUserMonsterBook(packet);
    }
    return false;
}

bool ResponseLockedItem(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    packet = ::Disassemble(packet, "d", &characterId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->OnResponseLockedItem(packet);
    }
    return false;
}

bool ResponseUpgradeSystem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00692df0, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    packet = ::Disassemble(packet, "d", &characterId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->OnResponseUpgradeSystem(packet);
    }
    return false;
}

bool ReplyLoadCardUpdownGameRecord(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00692f14, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    packet = ::Disassemble(packet, "d", &characterId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        std::int32_t gameId = 0;
        std::int32_t finishTime = 0;
        std::int32_t fixedRewardClassId = 0;
        std::int64_t fixedRewardAmount = 0;
        std::int32_t randomRewardClassId = 0;
        std::int64_t randomRewardAmount = 0;
        ::Disassemble(
            packet, "dddQdQ", &gameId, &finishTime, &fixedRewardClassId, &fixedRewardAmount, &randomRewardClassId,
            &randomRewardAmount
        );
        user->m_UpDownGame.OnReplyLoad(
            gameId, finishTime, fixedRewardClassId, fixedRewardAmount, randomRewardClassId, randomRewardAmount
        );
    }
    return false;
}

bool ReplyUpdateCardUpdownGameRecord(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006930f0, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    packet = ::Disassemble(packet, "d", &characterId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->m_UpDownGame.OnReplyUpdate(packet);
    }
    return false;
}

bool Reply_RP_EX_PLEDGE_MANIPULATE_ITEMS(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00699858, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t worldId = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::int32_t userDatabaseId = 0;
    std::int32_t atomicJobId = -1;
    std::int32_t result = 0;
    std::int32_t etcManipulationCount = 0;
    std::int32_t itemCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "ddddddd", &worldId, &pledgeDatabaseId, &userDatabaseId, &atomicJobId, &result, &etcManipulationCount,
        &itemCount
    );
    static_cast<void>(worldId);
    static_cast<void>(pledgeDatabaseId);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), userDatabaseId));
    AtomicJob* const atomicJob = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Pop(atomicJobId);

    if (itemCount < 0 || etcManipulationCount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Kicked for invalid items. user dbid: %d",
        //     L"DBPacketEx.cpp",
        //     0x331c,
        //     userDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Kicked for invalid items. user dbid: %d",
        //     L"DBPacketEx.cpp",
        //     0x331c,
        //     userDatabaseId)
        L2_LOG_ERROR(
            Log,
            "Clan item reply has negative counts: character DB ID=%d, items=%d, extra operations=%d; closing connection if "
            "available",
            userDatabaseId, itemCount, etcManipulationCount
        );
        if (user) {
            user->StopMove();
            if (user->GetSocket() != nullptr) {
                user->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            }
        }
        PledgeStuff::DoJob(atomicJob, result, true);
        PledgeStuff::DestroyJob(atomicJob);
    } else if (itemCount == 0 && etcManipulationCount == 0) {
        PledgeStuff::DoJob(atomicJob, result, true);
        PledgeStuff::DestroyJob(atomicJob);
    } else {
        cursor = L2EtcMani::ParseEtcMani(etcManipulationCount, cursor);
        std::vector<CreateItemStruct> items;
        ItemUtil::DisassembleItemList(cursor, itemCount, items);
        PledgeStuff::DoJob(atomicJob, result, false);
        PledgeStuff::DestroyJob(atomicJob);
    }
    return false;
}

bool Reply_RP_EX_PLEDGE_MANIPULATE_ITEMS_NEW(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00699b9c, original owner DBPacketEx.cpp. The
    // current instruction surface is a second complete creation of
    // Reply_RP_EX_PLEDGE_MANIPULATE_ITEMS, not a wrapper.
    L2_GUARD_FUNCTION();

    std::int32_t worldId = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::int32_t userDatabaseId = 0;
    std::int32_t atomicJobId = -1;
    std::int32_t result = 0;
    std::int32_t etcManipulationCount = 0;
    std::int32_t itemCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "ddddddd", &worldId, &pledgeDatabaseId, &userDatabaseId, &atomicJobId, &result, &etcManipulationCount,
        &itemCount
    );
    static_cast<void>(worldId);
    static_cast<void>(pledgeDatabaseId);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), userDatabaseId));
    AtomicJob* const atomicJob = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Pop(atomicJobId);

    if (itemCount < 0 || etcManipulationCount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Kicked for invalid items. user dbid: %d",
        //     L"DBPacketEx.cpp",
        //     0x3355,
        //     userDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Kicked for invalid items. user dbid: %d",
        //     L"DBPacketEx.cpp",
        //     0x3355,
        //     userDatabaseId)
        L2_LOG_ERROR(
            Log,
            "Clan item reply has negative counts: character DB ID=%d, items=%d, extra operations=%d; closing connection if "
            "available",
            userDatabaseId, itemCount, etcManipulationCount
        );
        if (user) {
            user->StopMove();
            if (user->GetSocket() != nullptr) {
                user->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            }
        }
        PledgeStuff::DoJob(atomicJob, result, true);
        PledgeStuff::DestroyJob(atomicJob);
    } else if (itemCount == 0 && etcManipulationCount == 0) {
        PledgeStuff::DoJob(atomicJob, result, true);
        PledgeStuff::DestroyJob(atomicJob);
    } else {
        cursor = L2EtcMani::ParseEtcMani(etcManipulationCount, cursor);
        std::vector<CreateItemStruct> items;
        ItemUtil::DisassembleItemList(cursor, itemCount, items);
        PledgeStuff::DoJob(atomicJob, result, false);
        PledgeStuff::DestroyJob(atomicJob);
    }
    return false;
}

bool Reply_RP_EX_SEQUENCE_JOB(CDBSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00676410, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t jobKey = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "d", &jobKey);
    Singleton<CSequenceJobManager>::inst().Receive(jobKey, socket, cursor);
    return false;
}

bool Reply_RP_EX_BUILDER_COMMAND(CDBSocket*, const unsigned char* const packet) {
    // Current L2Server 0x0069cbb4, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    protocol::RP_EX_BUILDER_COMMAND reply;
    if (!DecodePacket(reply, packet)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[BUILDER][%s][%d]wrong packet data",
        //     L"DBPacketEx.cpp",
        //     0x33a5)
        // L2_LOG_ERROR(Log,
        //     L"[BUILDER][%s][%d]wrong packet data",
        //     L"DBPacketEx.cpp",
        //     0x33a5)
        L2_LOG_ERROR(Log, "Builder command reply rejected: packet decoding failed");
        return false;
    }

    std::vector<std::u16string> parameters(reply.params.begin(), reply.params.end());
    if (parameters.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[BUILDER][%s][%d]param data is not exist",
        //     L"DBPacketEx.cpp",
        //     0x33b0)
        // L2_LOG_ERROR(Log,
        //     L"[BUILDER][%s][%d]param data is not exist",
        //     L"DBPacketEx.cpp",
        //     0x33b0)
        L2_LOG_ERROR(Log, "Builder command reply rejected: parameter list is empty");
        return false;
    }

    // The current body compares only the first entry, then directly reads
    // entries one and two. It has no size >= 3 guard.
    if (runtime::StringView(parameters.data()[0]) == u"version") {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(reply.nUserSid);
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            return false;
        }

        const std::wstring databaseVersion = Utf16ToWide(parameters.data()[1].c_str());
        const std::wstring cacheVersion = Utf16ToWide(parameters.data()[2].c_str());
        std::array<char16_t, 1024> message{};
        constexpr char16_t format[] = u"DB : %s, CacheD : %s";
        static_cast<void>(
            FormatUtf16To(message.data(), message.size(), format, databaseVersion.c_str(), cacheVersion.c_str())
        );
        char16_t systemContext[] = u"SYS";
        user->SendSystemMessage(systemContext, message.data());
    }
    return false;
}

} // namespace DBPacketExHandler

bool ReplySendBirthdayMail(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0069cefc, CacheD RP_EX 0xa0.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t itemTemplateId = 0;
    std::int32_t itemAmount = 0;
    std::int32_t mailId = 0;
    std::int32_t senderId = 0;
    std::int32_t receiverId = 0;
    std::int32_t trade = 0;
    std::int32_t returnable = 0;
    std::int32_t senderType = 0;
    std::int32_t itemCount = 0;
    std::int32_t requestContext = 0;
    ::Disassemble(
        packet, "ddddddddddd", &result, &itemTemplateId, &itemAmount, &mailId, &senderId, &receiverId, &trade, &returnable,
        &senderType, &itemCount, &requestContext
    );

    const int databaseId = runtime::process::CurrentDatabaseId();
    if (result < 1) {
        {
            CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, senderId));
            if (sender) {
                sender->m_pPostInfo->RecoverSendingDelay();
                if (senderType == 8) {
                    KalieEvent::Inst().ChangeRewardValueToOne();
                    runtime::raw_atomic::ExchangeI32(&sender->m_lWaitItemJob, 0);
                    return false;
                }
                if (senderType == 15) {
                    BalthusEvent::inst().StartRewardGivingForUserTimerExpired();
                    runtime::raw_atomic::ExchangeI32(&sender->m_lWaitItemJob, 0);
                    return false;
                }
                if (result == -6) {
                    sender->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_OWN_MAILBOX_LIMIT_EXCEEDED);
                }
                runtime::raw_atomic::ExchangeI32(&sender->m_lWaitItemJob, 0);
                if (senderType == 3) {
                    sender->m_nGiftDayIn7Days = static_cast<std::int32_t>(sender->m_data->m_tCreateDate.wDay);
                }
            }
        }
    } else {
        unsigned char userMask = 0;
        {
            CSPointer<User> receiver = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, receiverId));
            if (receiver) {
                userMask = 2;
                if (senderType == 8) {
                    KalieEvent::Inst().SuccesssGiveItemToLuckyUser(receiver.get());
                }
                if (senderType == 15) {
                    BalthusEvent::inst().SendNoticeToLotteryWinner(receiver);
                    BalthusEvent::inst().BroadcastWinnerInfo(receiver);
                    BalthusEvent::inst().WriteJackpotLog(receiver);
                }
            }

            PostUtil::MakeNewPost(
                mailId, senderId, receiverId, trade, returnable != 0, Post::ProcessWaiting, runtime::crt::Time32Now(),
                itemCount > 0, senderType, userMask, false, 0
            );
        }
    }

    if (senderType == 14) {
        RelayClient::Inst()->SendGivePostResult(requestContext, receiverId, itemTemplateId, itemAmount, result);
    }
    return false;
}

bool ReplyDeleteBirthdayMail(CDBSocket* socket, const unsigned char* packet) {
    // Current L2Server 0x00671e6c, CacheD RP_EX 0xa1. The target keeps
    // this guarded wrapper around the guarded 0x76 owner.
    L2_GUARD_FUNCTION();
    return ReplyDeletePost(socket, packet);
}

bool ReplyConvertItem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006795b8, CacheD RP_EX 0x4f.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t itemDatabaseId;
    std::int32_t newItemClassId;
    ::Disassemble(packet, "ddd", &userServerId, &itemDatabaseId, &newItemClassId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0xbc2,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0xbc2,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Item conversion reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    CItem* const oldItem = user->m_inv.GetByDBID(itemDatabaseId);
    if (oldItem == nullptr) {
        user->m_bConvertingItem = false;
        return false;
    }

    const std::int32_t oldItemClassId = oldItem->m_data->m_nClassID;
    CItem* const newItem = g_ObjectDB.CreateItem(newItemClassId);
    if (newItem == nullptr) {
        user->m_bConvertingItem = false;
        return false;
    }

    {
        L2_GUARD(*user->m_lock);
        {
            L2_WRITE_GUARD(user->m_inv);
            {
                L2_GUARD(*oldItem->m_lock);

                newItem->SetDBID(oldItem->GetDBID());
                static_cast<void>(newItem->SetAmount(1));
                newItem->SetEnchanted(oldItem->m_data->GetEnchanted());
                newItem->m_Useable = user->CheckUseable(oldItem);
                newItem->m_data->m_nDamaged = oldItem->m_data->m_nDamaged;
                newItem->m_data->m_bBlessed = oldItem->m_data->m_bBlessed;
                newItem->SetAttribute(oldItem->m_itemBaseAttribute);

                const std::int32_t shapeShiftedClassId = oldItem->m_data->GetShapeShiftedClassId();
                if (shapeShiftedClassId > 0) {
                    const ShapeShiftAutoMatchingType::type newMatchingType = newItem->GetShapeShiftMatchingType();
                    const ShapeShiftAutoMatchingType::type oldMatchingType = oldItem->GetShapeShiftMatchingType();
                    if (newMatchingType == oldMatchingType) {
                        newItem->SetShapeShiftingClassId(shapeShiftedClassId);
                    } else if (newItem->IsPeriodicShapeShiftedItem(user)) {
                        db.RequestExpireShapeShiftItem(user, newItem);
                    }
                }

                newItem->SetEnsoulOptionData(oldItem->m_mEnsoulInfo, true);
                newItem->m_data->SetLocked(oldItem->m_data->GetLocked());
                newItem->m_data->SetUseCount(oldItem->m_data->GetUseCount());
            }

            static_cast<void>(user->m_inv.Push(newItem->m_data->index));
            static_cast<void>(user->m_inv.SetInventoryChanged(newItem, IUT_ADD));
        }
    }

    if (user->EquipItem(newItem, ST_MAX) == -1) {
        user->UnequipItem(oldItem, true);
    } else {
        user->UnequipItem(oldItem, false);
        static_cast<void>(user->m_inv.Pop(oldItem->m_data->index, false));
        static_cast<void>(user->m_inv.SetInventoryChanged(oldItem, IUT_DELETE));
        oldItem->Delete();
        user->SendItemList(true, true, nullptr);
    }

    std::array<char16_t, 256> attributeWide{};
    std::array<char16_t, 256> variationWide{};
    std::array<char16_t, 256> ensoulWide{};
    newItem->m_itemBaseAttribute.GetLogString(attributeWide.data());
    static_cast<void>(FormatUtf16To(
        variationWide.data(), variationWide.size(), u"%d:%d:%d", newItem->m_itemOption.intensiveItemClassID,
        newItem->m_itemOption.itemOptionKey.variationOption1, newItem->m_itemOption.itemOptionKey.variationOption2
    ));
    newItem->MakeEnsoulLogInfo(ensoulWide.data());

    std::array<char16_t, 256> attribute{};
    std::array<char16_t, 256> variation{};
    std::array<char16_t, 256> ensoul{};
    DBPacketExHostWideToTargetUtf16(attributeWide.data(), attribute);
    DBPacketExHostWideToTargetUtf16(variationWide.data(), variation);
    DBPacketExHostWideToTargetUtf16(ensoulWide.data(), ensoul);

    const std::int32_t newItemEnchanted = newItem->m_data->GetEnchanted();
    // The current callback evaluates these three virtual calls separately,
    // in z/y/x order, before placing the values into x/y/z log lanes.
    const std::int64_t logZ = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const std::int64_t logY = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const std::int64_t logX = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    const std::uint32_t newItemDatabaseId = newItem->GetDBID();
    const char16_t* const actorName = user->GetName();
    const std::int32_t loggedNewItemClassId = newItem->m_data->m_nClassID;
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t userDatabaseId = user->GetDBID();
    std::u16string actorNameStorage;
    const char16_t* const actorNameUtf16 = (actorNameStorage = actorName).c_str();

    CLogFormatter formatter;
    formatter.WriteLog(
        0x3da, userDatabaseId, accountId, 0, 0, logX, logY, logZ, attribute.data(), variation.data(), ensoul.data(),
        newItemEnchanted, oldItemClassId, loggedNewItemClassId, 0, 0, 0, 0, 0, 0, 0, actorNameUtf16,
        user->m_sAccountName.data(), nullptr, nullptr, newItemDatabaseId
    );

    user->m_bConvertingItem = false;
    return false;
}

bool ReplyChangeMergedName(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068101c, CacheD RP_EX 0x50.
    L2_GUARD_FUNCTION();

    std::int32_t userSocketId;
    std::int32_t type;
    std::int32_t objectId;
    std::array<char16_t, 25> name{};
    const unsigned char* const cursor = ::Disassemble(packet, "dddS", &userSocketId, &type, &objectId, 0x32, name.data());

    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(userSocketId);
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBPacketEx.cpp",
        //     0xc3a)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBPacketEx.cpp",
        //     0xc3a)
        L2_LOG_ERROR(
            Log, "Merged-name change reply ignored: user socket %u is unavailable", static_cast<unsigned int>(userSocketId)
        );
        return false;
    }
    if (objectId == 0) {
        socket->Send("chdd", 0xfe, 0x6a, type, 2);
        return false;
    }

    if (type == 0) {
        for (std::size_t index = 0; index < socket->m_nCharacterDBIDs.size(); ++index) {
            if (socket->m_nCharacterDBIDs[index] == objectId) {
                socket->m_nCharacterPrevServers[index] = 0;
                break;
            }
        }

        std::int32_t pledgeDatabaseId;
        ::Disassemble(cursor, "d", &pledgeDatabaseId);
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeDatabaseId);
        if (pledge) {
            std::wstring hostName = Utf16ToWide(name.data());
            static_cast<void>(pledge->ChangeMemberName(static_cast<std::uint32_t>(objectId), hostName.data()));
            if (pledge->GetMasterDbId() == static_cast<std::uint32_t>(objectId)) {
                commcli.RequestChangePledgeMasterName(g_nWorldId, objectId, hostName.data());
            }
        }
        db.LoadSelectedCharacter(g_nWorldId, objectId, socket.get(), ALT_NONE, 0, 0, 0, false);
        return false;
    }

    if (type != 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid type[%d]",
        //     L"DBPacketEx.cpp",
        //     0xc7f,
        //     type)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid type[%d]",
        //     L"DBPacketEx.cpp",
        //     0xc7f,
        //     type)
        L2_LOG_ERROR(Log, "Merged-name change reply rejected: unsupported type=%d, object=%d", type, objectId);
        return false;
    }

    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, objectId);
    if (pledge) {
        pledge->m_bMergedProcessing = false;
        std::int32_t result;
        ::Disassemble(cursor, "d", &result);
        if (result < 1) {
            socket->Send("chdd", 0xfe, 0x6a, type, 2);
        } else {
            pledge->m_data->m_nPrevServer = 0;
            socket->SendSystemMessage(SystemMessageId::CLAN_NAME_CHANGE_REQUESTED);
            const char16_t* const masterName = pledge->GetMasterName();
            std::wstring hostName = Utf16ToWide(name.data());
            commcli.RequestSetPledgeInfo(g_nWorldId, objectId, hostName.data(), Utf16ToWide(masterName).c_str());
        }
    }
    return false;
}

bool ReplyLoadInZoneRestriction(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t count;
    const unsigned char* const rows = ::Disassemble(packet, "dd", &userServerId, &count);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0xcae,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0xcae,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Instance restriction reply cannot be applied: user object=%u is unavailable, reported entries=%d",
            static_cast<unsigned>(userServerId), count
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    user->SetInZoneRestriction(count, rows);
    return false;
}

bool ReplySetCastleSiegeDefenceCountEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00679cf4. The target releases the
    // temporary FindPledge smart pointer before using its captured raw
    // pointer.
    L2_GUARD_FUNCTION();

    std::int32_t pledgeId = 0;
    std::int32_t defenceCount = 0;
    ::Disassemble(packet, "dd", &pledgeId, &defenceCount);

    CPledge* const pledge = CPledge::FindPledge(g_nWorldId, pledgeId).get();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%d][%d]",
        //     L"DBPacketEx.cpp",
        //     0xd09,
        //     g_nWorldId,
        //     pledgeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid pledge[%d][%d]",
        //     L"DBPacketEx.cpp",
        //     0xd09,
        //     g_nWorldId,
        //     pledgeId)
        L2_LOG_ERROR(
            Log,
            "Castle siege defence count reply cannot be applied: clan database ID=%d is unavailable, world=%d, defence "
            "count=%d",
            pledgeId, g_nWorldId, defenceCount
        );
    } else {
        static_cast<void>(pledge->SetCastleSiegeDefenceCount(static_cast<unsigned int>(defenceCount)));
    }
    return false;
}

bool ReplyWaitingTransferPledgeMasterEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00679e38.
    L2_GUARD_FUNCTION();

    std::int32_t pledgeId = 0;
    ::Disassemble(packet, "d", &pledgeId);

    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
    if (pledge) {
        pledge->SetWaitingTransferMaster(true);
    }
    return false;
}

bool ReplyLoadUserPointPacket(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t pointType = 0;
    std::int32_t point = 0;
    ::Disassemble(packet, "ddd", &characterId, &pointType, &point);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }
    if (point < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "User point packet failed. [%d]:%d User [%s]",
        //     pointType,
        //     point,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "User point packet failed. [%d]:%d User [%s]",
        //     pointType,
        //     point,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "User point reply rejected: negative balance %d for point type %d, user=%s",
        //     point, pointType, ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "User point reply rejected: negative balance %d for point type %d, user=%s", point, pointType,
            ::Utf16ToUtf8(user->GetName()).c_str()
        );
        return false;
    }

    user->SetPoint(pointType, point);
    return false;
}

bool ReplySaveUserPointPacket(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067f0c8.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t point = 0;
    std::uint32_t result = 0;
    std::int32_t pointType = 0;
    std::int32_t reserved = 0;
    ::Disassemble(packet, "ddddd", &characterId, &point, &result, &pointType, &reserved);
    static_cast<void>(reserved);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user || result >= 4U) {
        return false;
    }

    if (pointType == 5 || pointType == 8) {
        if (result == 3U) {
            user->AddPoint(pointType, point, false);
        } else if ((pointType != 5 || user->CanGetPVPPoint()) && user->CheckPointAddable(pointType, point)) {
            const int addablePoint = user->GetRealAddablePoint(point, pointType);
            user->AddPoint(pointType, addablePoint, false);
            user->SendSystemMessage_d(SystemMessageId::FAME_GAINED, addablePoint);
        } else {
            const char16_t* const userName = user->GetName();
            const int currentPoint = user->GetPoint(pointType);
            // Log.Add(
            //     LOG_ERROR,
            //     L"multisell pvp/raid point cannot be added. [%d + %d] User [%s]",
            //     currentPoint,
            //     point,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     L"multisell pvp/raid point cannot be added. [%d + %d] User [%s]",
            //     currentPoint,
            //     point,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     "Multisell point credit rejected by local eligibility or amount checks: type=%d, current=%d, requested=%d,
            //     user=%s", pointType, currentPoint, point, ::WideToUtf8(userName).c_str())
            L2_LOG_ERROR(
                Log,
                "Multisell point credit rejected by local eligibility or amount checks: type=%d, current=%d, requested=%d, "
                "user=%s",
                pointType, currentPoint, point, ::Utf16ToUtf8(userName).c_str()
            );
        }
    } else if (pointType == 7) {
        CUserSocket* const socket = user->GetSocket();
        g_FieldCycleSystem.AddPoint(3, point, FieldCycleReason::ByMultiSell, nullptr, socket);
    } else {
        user->SetPoint(pointType, point);
    }
    return false;
}

bool ReplyLoadBossRecord(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t round = 0;
    packet = ::Disassemble(packet, "dd", &characterId, &round);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    user->SetBossRecordRound(round);
    std::int32_t count = 0;
    packet = ::Disassemble(packet, "d", &count);
    for (std::int32_t index = 0; index < count; ++index) {
        std::int32_t bossId = 0;
        std::int32_t point = 0;
        std::int32_t accumulatedPoint = 0;
        packet = ::Disassemble(packet, "ddd", &bossId, &point, &accumulatedPoint);
        user->AddBossRecord(new CBossRecord(bossId, accumulatedPoint, point));
    }
    return false;
}

bool ReplyLoadBossRecordRanking(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t count = 0;
    packet = ::Disassemble(packet, "d", &count);
    for (std::int32_t index = 0; index < count; ++index) {
        std::int32_t characterId = 0;
        std::int32_t record = 0;
        packet = ::Disassemble(packet, "dd", &characterId, &record);
        CBossRecordRanking::inst().AddBossRecord(characterId, record);
    }
    return false;
}

bool ReplyLoadBossRecordRound(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t round = 0;
    std::int32_t startTime = 0;
    std::int32_t endTime = 0;
    ::Disassemble(packet, "ddd", &round, &startTime, &endTime);
    CBossRecordRound::inst().Set(round, startTime, endTime);
    CBossRecordRound::inst().AddTimer(5000, 0);
    return false;
}

bool ReplyMoveToNextBossRecordRound(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t round = 0;
    std::int32_t startTime = 0;
    std::int32_t endTime = 0;
    ::Disassemble(packet, "ddd", &round, &startTime, &endTime);
    if (round > 0) {
        CBossRecordRound::inst().Set(round, startTime, endTime);
        CBossRecordRanking::inst().Initialize();
    }
    CBossRecordRound::inst().AddTimer(5000, 0);
    return false;
}

bool ReplyLoadSubPledge(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00670f74, RP_EX 0x2d.
    L2_GUARD_FUNCTION();
    CPledge::LoadSubPledge(packet);
    return false;
}

bool ReplyLoadPledgeSkill(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00671018, RP_EX 0x2e.
    L2_GUARD_FUNCTION();
    CPledge::LoadPledgeSkill(packet);
    return false;
}

bool ReplyLoadSharedReuseDelaysOfItems(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t succeeded = 0;
    packet = ::Disassemble(packet, "dd", &characterId, &succeeded);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    if (succeeded == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"load shared reuse delays of items failed. "
        //     L"[%s] does not exist in CachedD",
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"load shared reuse delays of items failed. "
        //     L"[%s] does not exist in CachedD",
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "CacheD failed to load shared item reuse delays: user=%s. Closing connection if available",
        //     ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "CacheD failed to load shared item reuse delays: user=%s; closing connection if available",
            ::Utf16ToUtf8(user->GetName()).c_str()
        );
        if (user->GetSocket() != nullptr) {
            user->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        }
        return false;
    }

    std::int32_t count = 0;
    packet = ::Disassemble(packet, "d", &count);
    for (std::int32_t index = 0; index < count; ++index) {
        std::int32_t sharedReuseGroupId = 0;
        std::int32_t reuseEndTime = 0;
        packet = ::Disassemble(packet, "dd", &sharedReuseGroupId, &reuseEndTime);
        user->AddSharedReuseDelayOfItem(sharedReuseGroupId, reuseEndTime);
    }
    return false;
}

bool ReplyVariationItem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00678b84, RP_EX 0x40.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "d", &result);
    if (result == 1) {
        std::int32_t userServerId;
        std::int32_t targetItemServerId;
        std::int32_t mineralItemServerId;
        std::int32_t supportItemServerId;
        std::int64_t supportItemAmount;
        std::int32_t variationOption1;
        std::int32_t variationOption2;
        std::int64_t mineralItemAmount;
        ::Disassemble(
            cursor, "ddddQddQ", &userServerId, &targetItemServerId, &mineralItemServerId, &supportItemServerId,
            &supportItemAmount, &variationOption1, &variationOption2, &mineralItemAmount
        );

        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user[sid:%d]",
            //     L"DBPacketEx.cpp",
            //     0x921,
            //     userServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid user[sid:%d]",
            //     L"DBPacketEx.cpp",
            //     0x921,
            //     userServerId)
            L2_LOG_ERROR(
                Log, "Augmentation success reply ignored: user object %u is unavailable or is not a player",
                static_cast<unsigned int>(userServerId)
            );
            return false;
        }

        static_cast<void>(user->GetSocket());
        CItem* const targetItem = g_ItemSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetItemServerId));
        CItem* const mineralItem = g_ItemSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mineralItemServerId));
        CItem* const supportItem = g_ItemSmartIdManager.GetObjectW(static_cast<std::uint32_t>(supportItemServerId));
        if (targetItem == nullptr || mineralItem == nullptr || (supportItemServerId > 0 && supportItem == nullptr) ||
            supportItemAmount < -1 || mineralItemAmount < 0) {
            user->GetSocket()->Send("chddd", 0xfe, 0x57, 0, 0, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
            return false;
        }

        ItemOptionKey optionKey{};
        optionKey.variationOption1 = variationOption1;
        optionKey.variationOption2 = variationOption2;
        if (!user->ReplyVariationItem(
                targetItem, mineralItem, supportItem, supportItemAmount, optionKey, mineralItemAmount
            )) {
            user->GetSocket()->Send("chddd", 0xfe, 0x57, 0, 0, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
        }
        return false;
    }

    std::int32_t userServerId;
    std::int32_t targetItemServerId;
    ::Disassemble(cursor, "dd", &userServerId, &targetItemServerId);
    static_cast<void>(targetItemServerId);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x941,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x941,
        //     userServerId)
        L2_LOG_ERROR(
            Log,
            "Augmentation failure reply cannot be delivered: user object %u is unavailable or is not a player, result=%d",
            static_cast<unsigned int>(userServerId), result
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    user->GetSocket()->Send("chddd", 0xfe, 0x57, 0, 0, 0);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    if (result == -4) {
        user->SendSystemMessage(SystemMessageId::INVALID_GEMSTONE_QUANTITY);
    } else if (result < -3 || result >= 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Variation error_reason error")
        // L2_LOG_ERROR(Log,
        //     "Variation error_reason error")
        L2_LOG_ERROR(
            Log, "Augmentation reply has an unrecognized failure result=%d, user object=%u", result,
            static_cast<unsigned int>(userServerId)
        );
    }
    return false;
}

bool ReplyCancelVariationItem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00678f58, RP_EX 0x41.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId;
    std::int32_t targetItemServerId;
    std::int64_t adenaAmount;
    ::Disassemble(packet, "dddQ", &result, &userServerId, &targetItemServerId, &adenaAmount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x96c,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x96c,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Augmentation removal reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result == 1) {
        CItem* const targetItem = g_ItemSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetItemServerId));
        if (targetItem == nullptr || adenaAmount < 0) {
            user->GetSocket()->Send("chd", 0xfe, 0x59, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
            return false;
        }
        if (!user->ReplyCancelVariationItem(targetItem, adenaAmount)) {
            user->GetSocket()->Send("chd", 0xfe, 0x59, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
        }
        return false;
    }

    user->GetSocket()->Send("chd", 0xfe, 0x59, 0);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    if (result == -5) {
        user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
    } else if (result < -4 || result >= 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Variation error_reason error")
        // L2_LOG_ERROR(Log,
        //     "Variation error_reason error")
        L2_LOG_ERROR(
            Log, "Augmentation removal reply has an unrecognized failure result=%d, user object=%u", result,
            static_cast<unsigned int>(userServerId)
        );
    }
    return false;
}

bool BROnCreateItem(CSPointer<User> user, CItem* item, int itemDatabaseId) {
    // Current L2Server 0x0067a170, original owner DBPacketEx.cpp.
    if (!user->m_inv.Push(item->m_data->index)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "inven push failed at file[%s], line[%d]",
        //     "DBPacketEx.cpp",
        //     0x1608)
        // L2_LOG_ERROR(Log,
        //     "inven push failed at file[%s], line[%d]",
        //     "DBPacketEx.cpp",
        //     0x1608)
        // L2_LOG_ERROR(Log,
        //     "Product item could not be inserted into inventory: item DB ID=%d, user=%s",
        //     itemDatabaseId, ::WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Product item could not be inserted into inventory: item DB ID=%d, user=%s", itemDatabaseId,
            ::Utf16ToUtf8(user->GetName()).c_str()
        );
        return false;
    }
    user->m_inv.SetInventoryChanged(item, IUT_ADD);

    CSharedItemData* weapon = nullptr;
    bool matchingWeapon = false;
    if (item->m_const->m_EtcitemType == EIT_ARROW) {
        weapon = user->GetEquipedWeapon();
        if (weapon != nullptr) {
            matchingWeapon = weapon->GetConstant()->m_AttackType == AT_BOW;
        }
    } else if (item->m_const->m_EtcitemType == EIT_BOLT) {
        weapon = user->GetEquipedWeapon();
        if (weapon != nullptr) {
            matchingWeapon = weapon->GetConstant()->m_AttackType == AT_CROSSBOW;
            if (!matchingWeapon) {
                matchingWeapon = weapon->GetConstant()->m_AttackType == AT_TWOHANDCROSSBOW;
            }
        }
    }
    if (matchingWeapon &&
        BaseCrystal[static_cast<int>(weapon->GetConstant()->GetCrystalType())] ==
            BaseCrystal[static_cast<int>(item->m_const->GetCrystalType())]) {
        user->AutoEquipArrowOrBolt(item);
    }

    if (item->IsDuration()) {
        db.UpdateItemDuration(*user.get(), static_cast<unsigned int>(itemDatabaseId), item->GetDuration());
    }
    if (item->m_const->m_period > 0) {
        user->OnGetPeriodicItem(item);
    }
    if (item->m_const->m_dAgathionMaxEnergy > 0.0) {
        item->SetAgathionEnergy(item->m_const->m_dAgathionMaxEnergy);
        user->SendBrAgathionEnergyInfo();
        db.RequestSaveItemAgathionEnergy(
            user.get(), item->m_data->m_nId, static_cast<int>(item->GetDBID()), item->GetAgathionEnergy()
        );
    }
    return false;
}

bool ReplyBrGiveItem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006860e8, RP_EX 0x89.
    L2_GUARD_FUNCTION();

    std::int32_t productType;
    std::int32_t paymentItemDatabaseId = 0;
    std::int64_t remainingAmount;
    std::int32_t characterId;
    std::int32_t productId;
    std::int32_t productAmount;
    std::int32_t soldCount;
    std::int32_t result;
    std::int32_t createdItemCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "ddQdddddd", &productType, &paymentItemDatabaseId, &remainingAmount, &characterId, &productId,
        &productAmount, &soldCount, &result, &createdItemCount
    );

    std::int32_t truncatedRemainingAmount = 0;
    if (paymentItemDatabaseId > 0) {
        truncatedRemainingAmount = runtime::Int32FromBits(static_cast<std::uint32_t>(remainingAmount));
    }

    if (!g_PremiumSetting.g_bStandAloneShopTest && result == 1 && (productType == 0 || productType == 99)) {
        g_PremiumSvc.RequestDeleteItem(remainingAmount);
    }

    CProductSP product = g_ShopServer.GetProduct(productId);
    if (product && result == 1) {
        product->SetSoldCount(soldCount);
    }

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user char DB Id[%d]",
        //     L"DBPacketEx.cpp",
        //     0x1675,
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user char DB Id[%d]",
        //     L"DBPacketEx.cpp",
        //     0x1675,
        //     characterId)
        L2_LOG_ERROR(
            Log, "Product reply cannot be applied to character: character DB ID=%d is unavailable, product=%d, result=%d",
            characterId, productId, result
        );
        return false;
    }

    const bool unsupportedProductType = (productType < 0 || productType > 2) && productType != 98;
    bool processReply = unsupportedProductType;
    if (!unsupportedProductType) {
        user->EndBuyProduct();
        processReply = result == 1;
    }

    if (!processReply) {
        user->GetSocket()->Send("chd", 0xfe, 0xd9, result);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] result is not ok[%d]",
        //     L"DBPacketEx.cpp",
        //     0x1680,
        //     result)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] result is not ok[%d]",
        //     L"DBPacketEx.cpp",
        //     0x1680,
        //     result)
        L2_LOG_ERROR(
            Log, "Product purchase failed: product=%d, type=%d, character DB ID=%d, result=%d", productId, productType,
            characterId, result
        );
        return false;
    }

    {
        L2_WRITE_GUARD(user->m_inv);
        for (std::int32_t index = 0; index < createdItemCount; ++index) {
            std::int32_t consumeType;
            std::int32_t itemDatabaseId;
            std::int32_t itemType;
            std::int64_t resultingAmount = 0;
            cursor = ::Disassemble(cursor, "dddQ", &consumeType, &itemDatabaseId, &itemType, &resultingAmount);

            if (consumeType == 2) {
                bool created = false;
                CItem* item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    item = g_ObjectDB.CreateItem(itemType);
                    if (item == nullptr) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "item create fail at file[%s], line[%d]",
                        //     "DBPacketEx.cpp",
                        //     0x169d)
                        // L2_LOG_ERROR(Log,
                        //     "item create fail at file[%s], line[%d]",
                        //     "DBPacketEx.cpp",
                        //     0x169d)
                        L2_LOG_ERROR(
                            Log, "Product item creation failed: product=%d, item class=%d, item DB ID=%d; entry skipped",
                            productId, itemType, itemDatabaseId
                        );
                        continue;
                    }
                    created = true;
                }

                {
                    L2_GUARD(*item->m_lock);
                    if (!item->SetAmount(resultingAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d] item amount is invalid[%d][%Q]",
                        //     "DBPacketEx.cpp",
                        //     0x16a6,
                        //     item->m_data->m_nCount,
                        //     resultingAmount)
                        // L2_LOG_ERROR(Log,
                        //     L"[%S][%d] item amount is invalid[%d][%Q]",
                        //     "DBPacketEx.cpp",
                        //     0x16a6,
                        //     item->m_data->m_nCount,
                        //     resultingAmount)
                        L2_LOG_ERROR(
                            Log,
                            "Product item amount assignment failed: item DB ID=%d, current=%lld, requested=%lld; item "
                            "processing continues",
                            itemDatabaseId, static_cast<long long>(item->m_data->m_nCount),
                            static_cast<long long>(resultingAmount)
                        );
                    }
                    item->SetDBID(static_cast<unsigned int>(itemDatabaseId));
                }

                if (created) {
                    static_cast<void>(BROnCreateItem(user, item, itemDatabaseId));
                } else {
                    user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                }
                continue;
            }

            CItem* const item = g_ObjectDB.CreateItem(itemType);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     "item create fail at file[%s], line[%d]",
                //     "DBPacketEx.cpp",
                //     0x16b6)
                // L2_LOG_ERROR(Log,
                //     "item create fail at file[%s], line[%d]",
                //     "DBPacketEx.cpp",
                //     0x16b6)
                L2_LOG_ERROR(
                    Log, "Product item creation failed: product=%d, item class=%d, item DB ID=%d; entry skipped", productId,
                    itemType, itemDatabaseId
                );
                continue;
            }

            {
                L2_GUARD(*item->m_lock);
                if (!item->SetAmount(1)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%d][%d]",
                    //     "DBPacketEx.cpp",
                    //     0x16bd,
                    //     item->m_data->m_nCount,
                    //     1)
                    // L2_LOG_ERROR(Log,
                    //     L"[%S][%d] item amount is invalid[%d][%d]",
                    //     "DBPacketEx.cpp",
                    //     0x16bd,
                    //     item->m_data->m_nCount,
                    //     1)
                    L2_LOG_ERROR(
                        Log,
                        "Product item amount assignment failed: item DB ID=%d, current=%lld, requested=1; item processing "
                        "continues",
                        itemDatabaseId, static_cast<long long>(item->m_data->m_nCount)
                    );
                }
                item->SetDBID(static_cast<unsigned int>(itemDatabaseId));
            }
            static_cast<void>(BROnCreateItem(user, item, itemDatabaseId));
        }

        if (productType == 1 || productType == 2) {
            CItem* const paymentItem = user->m_inv.GetByDBID(paymentItemDatabaseId);
            if (paymentItem != nullptr) {
                if (truncatedRemainingAmount == 0) {
                    user->m_inv.Pop(paymentItem->m_data->index, false);
                    user->m_inv.SetInventoryChanged(paymentItem, IUT_DELETE);
                    paymentItem->Delete();
                } else {
                    {
                        L2_GUARD(*paymentItem->m_lock);
                        if (!paymentItem->SetAmount(static_cast<std::int64_t>(truncatedRemainingAmount))) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBPacketEx.cpp",
                            //     0x16d1,
                            //     paymentItem->m_data->m_nCount,
                            //     truncatedRemainingAmount)
                            // L2_LOG_ERROR(Log,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBPacketEx.cpp",
                            //     0x16d1,
                            //     paymentItem->m_data->m_nCount,
                            //     truncatedRemainingAmount)
                            L2_LOG_ERROR(
                                Log, "Product payment balance update failed: item DB ID=%d, current=%lld, requested=%lld",
                                paymentItemDatabaseId, static_cast<long long>(paymentItem->m_data->m_nCount),
                                static_cast<long long>(truncatedRemainingAmount)
                            );
                        }
                    }
                    user->m_inv.SetInventoryChanged(paymentItem, IUT_UPDATE);
                }
            }
        }
    }
    user->m_inv.UpdateRuneItem();
    user->SendItemList(true, false, nullptr);
    user->SendItemList(false, false, nullptr);
    if ((productType >= 0 && productType <= 2) || productType == 98) {
        user->GetSocket()->Send("chd", 0xfe, 0xd9, result);
    }
    return false;
}

bool ReplyBrPlayingUser(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a1244, RP_EX 0x8a.
    L2_GUARD_FUNCTION();

    std::int32_t playingUserCount;
    ::Disassemble(packet, "d", &playingUserCount);
    SharedEventInfo::Inst()->m_gameEvent.m_PlayingUserAverage = playingUserCount;

    std::vector<CEvent*> events;
    CEventManager::inst().GetEvent(GAME_EVENT::ET_DROP, events);
    for (CEvent* const event : events) {
        if (event != nullptr) {
            static_cast<CEvent_Drop*>(event)->UpdateTick(playingUserCount);
        }
    }
    return false;
}

bool ReplyBrProductSoldCount(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067a4d0, RP_EX 0x8b.
    L2_GUARD_FUNCTION();

    std::array<std::int32_t, 1000> productIds{};
    std::array<std::int32_t, 1000> soldCounts{};
    std::int32_t productCount;
    std::int32_t productElementWidth;
    std::int32_t soldCountElementWidth;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &productCount, &productElementWidth);
    cursor =
        ::Disassemble(cursor, "b", runtime::MultiplyInt32Wrapping(productCount, productElementWidth), productIds.data());
    cursor = ::Disassemble(cursor, "d", &soldCountElementWidth);
    ::Disassemble(cursor, "b", runtime::MultiplyInt32Wrapping(soldCountElementWidth, productCount), soldCounts.data());

    for (std::int32_t index = 0; index < productCount; ++index) {
        CProductSP product = g_ShopServer.GetProduct(productIds[static_cast<std::size_t>(index)]);
        if (product) {
            product->SetSoldCount(soldCounts[static_cast<std::size_t>(index)]);
        }
    }
    return false;
}

bool ReplyBrRecentProduct(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00686a20, RP_EX 0x8c.
    L2_GUARD_FUNCTION();

    std::array<std::int32_t, 30> productIds{};
    CPacketHelperEx<20480> response;
    CProductSP product;

    std::int32_t characterId;
    std::int32_t productCount;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &characterId, &productCount);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    if (productCount < 1) {
        CUserSocket* const socket = user->GetSocket();
        const std::int64_t productPoint = user->m_inv.GetItemAmount(0x5cfd, 0, true);
        const std::int64_t adena = user->m_inv.GetAdenaAmount();
        socket->Send("chQQcd", 0xfe, 0xd7, adena, productPoint, 1, productCount);
        return false;
    }

    ::Disassemble(cursor, "b", static_cast<int>(sizeof(productIds)), productIds.data());
    std::int32_t validProductCount = 0;
    BrProductItemInfo itemInfo[8];
    for (std::int32_t productIndex = 0; productIndex < productCount; ++productIndex) {
        product = g_ShopServer.GetProduct(productIds[static_cast<std::size_t>(productIndex)]);
        if (!product) {
            continue;
        }

        const std::int32_t itemCount = product->GetItemCount();
        for (std::int32_t itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
            CProduct::ItemData* const productItem = product->GetItemData(itemIndex);
            if (productItem == nullptr) {
                continue;
            }

            CObject* const object = g_ObjectDB.GetObjectW(productItem->typeId, false);
            if (object == nullptr || !object->IsItem()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] GetObject Fail",
                //     "DBPacketEx.cpp",
                //     0x1780)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] GetObject Fail",
                //     "DBPacketEx.cpp",
                //     0x1780)
                L2_LOG_ERROR(
                    Log,
                    "Product-list item metadata unavailable: product index=%d, item index=%d, class=%d is missing or is not "
                    "an item",
                    productIndex, itemIndex, productItem->typeId
                );
                continue;
            }

            CItem* const item = static_cast<CItem*>(object);
            // The target initializes neither this eight-row carrier nor a
            // per-product replacement, and it performs no eight-row clamp.
            BrProductItemInfo& info = itemInfo[itemIndex];
            info.itemId = productItem->typeId;
            info.amount = productItem->amount;
            info.weight = item->m_data->m_ItemConsumeType == ICT_ASSET ? 0 : item->m_const->m_nWeight;
            info.tradable = item->IsTrade() ? 1 : 0;
        }

        ++validProductCount;
        const std::int32_t maxBirthday = product->GetMaxBirthday();
        const std::int32_t minBirthday = product->GetMinBirthday();
        const std::int32_t saleEndTime = product->GetSaleEndTime();
        const std::int32_t saleStartTime = product->GetSaleStartTime();
        response.Assemble(
            "dccdcdddcccccddcccddddcb", product->m_productId,
            static_cast<int>(static_cast<std::int8_t>(product->m_category)),
            static_cast<int>(static_cast<std::int8_t>(product->m_paymentType)), product->m_price,
            static_cast<int>(static_cast<std::int8_t>(product->m_itemPanelType)), product->m_tab, saleStartTime, saleEndTime,
            static_cast<int>(product->m_dayOfWeek), static_cast<int>(product->m_startTime.hour),
            static_cast<int>(product->m_startTime.minute), static_cast<int>(product->m_endTime.hour),
            static_cast<int>(product->m_endTime.minute), product->m_soldCount, product->m_stock,
            static_cast<int>(static_cast<std::int8_t>(product->m_salePercent)),
            static_cast<int>(static_cast<std::int8_t>(product->m_minLevel)),
            static_cast<int>(static_cast<std::int8_t>(product->m_maxLevel)), minBirthday, maxBirthday,
            product->m_limitPeriod, product->m_limitCount, static_cast<int>(static_cast<std::int8_t>(itemCount)),
            itemCount << 4, itemInfo
        );
    }

    CUserSocket* const socket = user->GetSocket();
    const std::int32_t responseLength = response.GetLength();
    const std::int64_t productPoint = user->m_inv.GetItemAmount(0x5cfd, 0, true);
    const std::int64_t adena = user->m_inv.GetAdenaAmount();
    socket->Send("chQQcdb", 0xfe, 0xd7, adena, productPoint, 1, validProductCount, responseLength, response.GetPacket());
    return false;
}

bool ReplyBrLoadEventTopRankers(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a154c, RP_EX 0x8d.
    L2_GUARD_FUNCTION();

    [[maybe_unused]] CPacketHelperEx<20480> unusedPacket;
    std::int32_t characterId;
    std::int32_t eventId;
    std::int32_t roundNo;
    std::int32_t topPlayerCount;
    std::int32_t topPlayerScore;
    std::int32_t myScore;
    ::Disassemble(packet, "dddddd", &characterId, &eventId, &roundNo, &topPlayerCount, &topPlayerScore, &myScore);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterId));
    if (user) {
        CEvent_World* const worldEvent = static_cast<CEvent_World*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_WORLD));
        if (worldEvent != nullptr) {
            CUserSocket* const socket = user->GetSocket();
            const std::int32_t currentRound = worldEvent->GetRoundNo(runtime::crt::Time32Now());
            socket->Send(
                "chddddd", 0xfe, 0xbe, eventId, runtime::SubtractInt32Wrapping(currentRound, roundNo), topPlayerCount,
                topPlayerScore, myScore
            );
        }
    }
    return false;
}

bool ReplyBrUpdateEventRanking(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a1794, RP_EX 0x8e.
    L2_GUARD_FUNCTION();

    CFHTML::StrStrMapType parameters;
    const std::u16string title = CFHTML::MakeHTMLFromStrMap(u"ev_halloween_1st_title.htm", parameters, 0);
    const std::u16string body = CFHTML::MakeHTMLFromStrMap(u"ev_halloween_1st_contents.htm", parameters, 0);

    std::int32_t characterCount;
    std::int32_t ignoredHeader1;
    std::int32_t ignoredHeader2;
    const unsigned char* cursor = ::Disassemble(packet, "ddd", &characterCount, &ignoredHeader1, &ignoredHeader2);
    std::array<char16_t, 25> characterName{};
    for (std::int32_t index = 0; index < characterCount; ++index) {
        cursor = ::Disassemble(cursor, "S", 0x32, characterName.data());
        CEventManager::inst().WriteEventPost(characterName.data(), title.data(), body.data());
    }
    return false;
}

bool ReplyBrSaveItemAgathionEnergy(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t itemServerId;
    std::int32_t itemDatabaseId;
    double energy;
    ::Disassemble(packet, "ddf", &itemServerId, &itemDatabaseId, &energy);

    CItem* const item = g_ItemSmartIdManager.GetObjectW(static_cast<std::uint32_t>(itemServerId));
    if (item) {
        item->SetAgathionEnergy(energy);
    }
    return false;
}

bool ReplyLoadDuelRecord(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t individualWinCount = 0;
    std::int32_t individualLoseCount = 0;
    std::int32_t individualDrawCount = 0;
    std::int32_t partyWinCount = 0;
    std::int32_t partyLoseCount = 0;
    std::int32_t partyDrawCount = 0;
    ::Disassemble(
        packet, "ddddddd", &characterId, &individualWinCount, &individualLoseCount, &individualDrawCount, &partyWinCount,
        &partyLoseCount, &partyDrawCount
    );

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->m_DuelUserInfo.UpdateDuelRecord(
            individualWinCount, individualLoseCount, individualDrawCount, partyWinCount, partyLoseCount, partyDrawCount
        );
    }
    return false;
}

bool ReplyBrInsertMinigameScore(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId;
    std::int32_t result;
    ::Disassemble(packet, "dd", &characterId, &result);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user && result != 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyBrInsertMinigameScore fail, charId = %d",
        //     L"DBPacketEx.cpp",
        //     0x17de,
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReplyBrInsertMinigameScore fail, charId = %d",
        //     L"DBPacketEx.cpp",
        //     0x17de,
        //     characterId)
        L2_LOG_ERROR(Log, "Minigame score save failed: character DB ID=%d, CacheD result=%d", characterId, result);
    }
    return false;
}

bool ReplyBrLoadItemAgathionEnergy(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId;
    std::int32_t itemServerId;
    std::int32_t itemDatabaseId;
    double energy;
    ::Disassemble(packet, "dddf", &characterId, &itemServerId, &itemDatabaseId, &energy);

    CItem* const item = g_ItemSmartIdManager.GetObjectW(static_cast<std::uint32_t>(itemServerId));
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (item) {
        item->SetAgathionEnergy(energy);
        if (user) {
            user->SendBrAgathionEnergyInfo();
        }
    }
    return false;
}

bool ReplyLoadPetItemCount(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t itemCount = 0;
    ::Disassemble(packet, "dd", &characterId, &itemCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->m_nPetItemCount = itemCount;
    }
    return false;
}

bool ReplySaveBotReportPacket(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t eventId = 0;
    ::Disassemble(packet, "dd", &characterId, &eventId);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        if (g_CaptchaSetting.enable) {
            user->CheckBotReportEventCaptcha(eventId);
        } else {
            user->CheckBotReportEvent(eventId);
        }
    }
    return false;
}

bool ReplyLoadBotReportPacket(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006823c0, CacheD RP_EX 0x63.
    L2_GUARD_FUNCTION();

    std::int32_t requesterId = 0;
    std::int32_t targetCharacterId = 0;
    std::int32_t reportPoint = 0;
    std::int32_t confirmTimes = 0;
    std::int32_t confirmSeconds = 0;
    std::int32_t delaySeconds = 0;
    ::Disassemble(
        packet, "dddddd", &requesterId, &targetCharacterId, &reportPoint, &confirmTimes, &confirmSeconds, &delaySeconds
    );

    if (requesterId == -1) {
        CSPointer<User> user =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), targetCharacterId));
        if (!user) {
            return false;
        }

        if (reportPoint < 0) {
            reportPoint = 0;
        }
        user->m_nBotReportPoint = reportPoint;
        runtime::raw_atomic::ExchangeI32(&user->m_CaptchaConfirmTimes, confirmTimes);
        user->SetCaptchaConfirmSec(confirmSeconds);
        user->SetCaptchaDelaySec(delaySeconds);
        if (g_CaptchaSetting.enable) {
            user->CheckBotReportEventCaptcha(reportPoint);
        } else {
            user->CheckBotReportEvent(reportPoint);
        }
        return false;
    }

    CSPointer<User> requester =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), requesterId));
    if (requester) {
        std::array<char16_t, 256> message{};
        static_cast<void>(FormatUtf16To(message.data(), message.size(), u"count : %d", reportPoint));
        char16_t systemContext[] = u"SYS";
        requester->GetSocket()->SendSystemMessage(systemContext, message.data());
    }
    return false;
}

bool ReplyLoadDailyQuestPacket(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00681aa4, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &characterId, &count);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid user %d has daily quest Packet.",
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"Invalid user %d has daily quest Packet.",
        //     characterId)
        L2_LOG_ERROR(Log, "Daily quest reply ignored: character DB ID %d is unavailable", characterId);
        return false;
    }
    if (count >= 0x12d) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"%d User has too many memo.",
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"%d User has too many memo.",
        //     characterId)
        L2_LOG_ERROR(Log, "Daily quest reply rejected: character DB ID=%d, entries=%d exceed 300", characterId, count);
        return false;
    }

    {
        L2_GUARD(*user->m_lock);
        for (std::int32_t index = 0; index < count; ++index) {
            CSharedCreatureData::DailyQuestState& state = user->m_data->m_DailyQuest[index];
            cursor = ::Disassemble(cursor, "dd", &state.nQuestID, &state.nCompleteTime);
        }
    }
    user->SendCompletedDailyQuestList();
    return false;
}

bool ReplyLoadLastColorPacket(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00681914, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t colorIndex = 0;
    std::int32_t savedTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &colorIndex, &savedTime);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_nNickNameColor, colorIndex, 0x56);
        user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_NICKNAME});
    }
    return false;
}

bool ReplyLoadNRMemoPacket(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &characterId, &count);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid user %d has NRMemo Packet.",
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"Invalid user %d has NRMemo Packet.",
        //     characterId)
        L2_LOG_ERROR(Log, "NR memo reply ignored: character DB ID %d is unavailable", characterId);
        return false;
    }

    user->SendUserConnectedEvent();
    if (count >= 0x15) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"%d User has too many NR memo.",
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"%d User has too many NR memo.",
        //     characterId)
        L2_LOG_ERROR(Log, "NR memo reply rejected: character DB ID=%d, entries=%d exceed 20", characterId, count);
        return false;
    }

    {
        L2_GUARD(*user->m_lock);
        for (std::int32_t index = 0; index < count; ++index) {
            MemoStruct& memo = user->m_data->m_NRMemo[index];
            cursor = ::Disassemble(
                cursor, "dddddd", &memo.nQuestID, &memo.nJournal, &memo.nState, &memo.nState2, &memo.nState3, &memo.nState4
            );
            if (memo.nJournal == 0) {
                memo.nJournal = 1;
            }
        }
    }
    user->SendQuestList(nullptr);
    return false;
}

bool ReplyLoadQuestNPCLog(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &userServerId);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x1986,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x1986,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Quest NPC log reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    std::int32_t count = 0;
    cursor = ::Disassemble(cursor, "d", &count);
    {
        L2_GUARD(*user->m_lock);
        for (std::int32_t index = 0; index < count; ++index) {
            std::int32_t memoIndex = 0;
            std::int32_t questId = 0;
            std::int32_t userLogType = 0;
            std::int32_t npcId = 0;
            std::int32_t npcCount = 0;
            cursor = ::Disassemble(cursor, "ddddd", &memoIndex, &questId, &userLogType, &npcId, &npcCount);
            MemoStruct& memo = user->m_data->m_Memo[memoIndex];
            if (memo.nQuestID == questId) {
                static_cast<void>(memo.UpdateNPCLog(static_cast<UserNPCLogType>(userLogType), npcId, npcCount));
            }
        }
    }
    return false;
}

bool ReplyLoadInZoneFlag(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00671490. The three carriers are intentionally
    // uninitialized before the shipping Disassemble call.
    L2_GUARD_FUNCTION();

    std::int32_t inzoneUid;
    std::int32_t inzoneTypeId;
    std::int32_t flagCount;
    const unsigned char* const flags = ::Disassemble(packet, "ddd", &inzoneUid, &inzoneTypeId, &flagCount);
    g_InstantZoneMgr.SetInstantZoneFlag(inzoneUid, inzoneTypeId, flagCount, flags);
    return false;
}

bool ReplyLoadInzoneMaxDBID(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00671560 directly publishes the decoded value into
    // CInstantZoneManager +0x88. It does not use an atomic or a setter.
    L2_GUARD_FUNCTION();

    std::int32_t maximumDbId;
    ::Disassemble(packet, "d", &maximumDbId);
    g_InstantZoneMgr.m_nMaxInzoneDBID = maximumDbId;
    return false;
}

bool ReplyAddPostFriend(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00687e1c, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::array<char16_t, 25> friendName{};
    std::int32_t result = 0;
    ::Disassemble(packet, "dSd", &characterId, 0x32, friendName.data(), &result);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        if (result > 0) {
            user->m_pPostInfo->GetPostFriend().AddPostFriend(friendName.data());
            user->GetSocket()->Send(
                "chccS", 0x62, SystemMessageId::MAIL_RECIPIENT_ADDED_TO_CONTACT_LIST, 1, 0x0c, friendName.data()
            );
        }
        user->GetSocket()->Send("chSd", 0xfe, 0xd3, friendName.data(), result);
    }
    return false;
}

bool ReplyLoadPostFriend(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068806c, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t friendCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &characterId, &friendCount);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        for (std::int32_t index = 0; index < friendCount; ++index) {
            std::array<char16_t, 25> friendName{};
            cursor = ::Disassemble(cursor, "S", 0x32, friendName.data());
            user->m_pPostInfo->GetPostFriend().AddPostFriend(friendName.data());
        }
        user->m_pPostInfo->GetPostFriend().SetLoaded();
        user->m_pPostInfo->GetPostFriend().SendPostFriend(user->GetSocket());
    }
    return false;
}

namespace DBPacketExHandler {

bool ReplyLoadReservedAnnounce(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ReplyAddReservedAnnounce(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ReplyDeleteReservedAnnounce(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool Reply_RP_EX_LOAD_RESERVATION_ANNOUNCE(CDBSocket*, const unsigned char* const packet) {
    // Current L2Server 0x006a0d78, RP_EX 0x131.
    L2_GUARD_FUNCTION();
    protocol::RP_EX_LOAD_RESERVATION_ANNOUNCE reply;
    if (!DecodePacket(reply, packet)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[BUILDER][%s][%d]wrong packet data",
        //     L"DBPacketEx.cpp",
        //     0x33f9)
        // L2_LOG_ERROR(Log,
        //     L"[BUILDER][%s][%d]wrong packet data",
        //     L"DBPacketEx.cpp",
        //     0x33f9)
        L2_LOG_ERROR(Log, "Scheduled-announcement load reply rejected: packet decoding failed");
        return false;
    }

    if (!reply.data.empty()) {
        CReservationAnnounceTimer::inst().LoadAnnounce(reply);
    }
    return false;
}

bool Reply_RP_EX_SHOW_RESERVATION_ANNOUNCE(CDBSocket*, const unsigned char* const packet) {
    // Current L2Server 0x006a08d8, RP_EX 0x132.
    L2_GUARD_FUNCTION();
    protocol::RP_EX_SHOW_RESERVATION_ANNOUNCE reply;
    if (!DecodePacket(reply, packet)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[BUILDER][%s][%d]wrong packet data",
        //     L"DBPacketEx.cpp",
        //     0x340e)
        // L2_LOG_ERROR(Log,
        //     L"[BUILDER][%s][%d]wrong packet data",
        //     L"DBPacketEx.cpp",
        //     0x340e)
        L2_LOG_ERROR(Log, "Scheduled-announcement display reply rejected: packet decoding failed");
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(reply.nUserServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x3414,
        //     reply.nUserServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x3414,
        //     reply.nUserServerId)
        L2_LOG_ERROR(
            Log, "Scheduled-announcement display reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(reply.nUserServerId)
        );
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    char16_t systemContext[] = u"SYS";
    if (reply.data.empty()) {
        socket->SendSystemMessage(systemContext, u"reservation announce is empty");
        return false;
    }

    for (const protocol::PkReservationAnncounceData& row : reply.data) {
        const L2DateTime start = L2DateTime::FromTime_t(row.StartDate);
        const L2DateTime end = L2DateTime::FromTime_t(row.endDate);
        int lineNumber = 1;
        for (const std::u16string& text : row.Msgs) {
            std::array<char16_t, 512> message{};
            constexpr char16_t format[] = u"[announce num : %d]"
                                          u"[%d-%d-%d %d:%d ~ %d-%d-%d %d:%d]"
                                          u"[period : %d][location : %d]"
                                          u"[line num : %d]%s";
            static_cast<void>(FormatUtf16To(
                message.data(), message.size(), format, row.announceId, start.Year(), start.Month(), start.Day(),
                start.Hour(), start.Minute(), end.Year(), end.Month(), end.Day(), end.Hour(), end.Minute(), row.period,
                row.location, lineNumber, text
            ));
            socket->SendSystemMessage(systemContext, message.data());
            message.fill(L'\0');
            ++lineNumber;
        }
    }
    return false;
}

bool Reply_RP_EX_REFRASH_RESERVATION_ANNOUNCE(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    db.LoadReservationAnnounce();
    return false;
}

bool ReplyReloadEventData(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ReplyLoadEventData(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a19f8, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t index = 0;
    std::int32_t total = 0;
    std::array<char16_t, 129> eventName{};
    std::array<char16_t, 8192> contents{};
    ::Disassemble(packet, "ddSS", &index, &total, 0x102, eventName.data(), 0x4000, contents.data());
    CEventManager::inst().ReplyLoadEventFromDB(
        index, total, std::u16string(eventName.data()), std::u16string(contents.data())
    );
    return false;
}

bool ResponseUpdateKalieEventEx(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ResponseUpdateKalieEventUserEx(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ResponseLoadKalieEventEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006724a4, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t state = 0;
    std::int32_t progress = 0;
    ::Disassemble(packet, "dd", &state, &progress);
    KalieEvent& event = KalieEvent::Inst();
    event.SetCurrentState(state);
    runtime::raw_atomic::ExchangeI32(&event.m_nProgress, progress);
    KalieEvent::Inst().InitEventInfoForUser();
    return false;
}

bool ResponseLoadKalieEventUserEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a1bcc, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterDatabaseId = 0;
    std::int32_t tokenCount = 0;
    ::Disassemble(packet, "dd", &characterDatabaseId, &tokenCount);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterDatabaseId));
    if (!user) {
        return false;
    }

    user->SetKalieToken(tokenCount);
    KalieEvent::Inst().SendKalieInfo(*user.get(), -1);
    CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_KALIE);
    if (event == nullptr || !event->InEventTime()) {
        return false;
    }

    user->GetSocket()->SendSystemMessage(
        KalieEvent::Inst().m_bEventStarted ? SystemMessageId::LUCKY_GIFT_EVENT_IN_PROGRESS
                                           : SystemMessageId::LUCKY_GIFT_EVENT_WAITING_TO_START
    );
    return false;
}

bool ResponseLoadBalthusEventUserInfoEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a1e24, RP_EX 0x110.
    L2_GUARD_FUNCTION();

    protocol::RP_LOAD_BALTHUS_EVENT_USER_INFO response;
    network::Stream stream(const_cast<unsigned char*>(packet), 0x4000);
    if (!response.Decode(&stream)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Load Balthus Event UserInfo failed, "
        //     L"invalid packet body",
        //     L"DBPacketEx.cpp",
        //     0x2376)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Load Balthus Event UserInfo failed, "
        //     L"invalid packet body",
        //     L"DBPacketEx.cpp",
        //     0x2376)
        L2_LOG_ERROR(Log, "Balthus event user-info reply rejected: packet decoding failed");
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(response.nUserSID));
    User* const rawUser = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (rawUser == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x237a,
        //     response.nUserSID)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x237a,
        //     response.nUserSID)
        L2_LOG_ERROR(
            Log, "Balthus event user-info reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(response.nUserSID)
        );
        return false;
    }

    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);
    runtime::raw_atomic::ExchangeI32(&user->m_nBalthusTokenCount, response.nRemainToken);
    BalthusEvent::inst().SendEventInfo(user, GAME_EVENT::BALTHUS_NO_CHANGE);

    CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_BALTHUS);
    if (event != nullptr && event->InEventTime()) {
        user->GetSocket()->SendSystemMessage(
            BalthusEvent::inst().m_bEventStarted ? SystemMessageId::LUCKY_GIFT_EVENT_IN_PROGRESS
                                                 : SystemMessageId::LUCKY_GIFT_EVENT_WAITING_TO_START
        );
    }
    return false;
}

bool ResponseResetBeautyList(CDBSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ReplyLoadWolrdChatTime(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00672b44, RP_EX 0x100.
    L2_GUARD_FUNCTION();

    std::int32_t nextResetTime = 0;
    ::Disassemble(packet, "d", &nextResetTime);
    g_WorldChat.tNextResetTime = nextResetTime;
    g_L2Time.CheckResetWorldChat();
    return false;
}

bool ReplyLoadMatchingPoint(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0066c9a0, RP_EX 0x101.
    L2_GUARD_FUNCTION();

    // Source metadata is supplied by the logging macro.
    // constexpr const wchar_t* kOwner =
    //     L"DBPacketExHandler::ReplyLoadMatchingPoint".
    if (packet == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MatchPoint][%s][%d] invalid parameter, pPacket",
        //     kOwner,
        //     0x25ff)
        // L2_LOG_ERROR(Log,
        //     L"[MatchPoint][%s][%d] invalid parameter, pPacket",
        //     kOwner,
        //     0x25ff)
        L2_LOG_ERROR(Log, "Matching-point load reply rejected: packet is null");
        return false;
    }

    std::map<int, std::int64_t> points;
    std::uint8_t type = 0;
    std::int32_t count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "cd", &type, &count);

    if (type >= static_cast<std::uint8_t>(MatchingPoint::MPT_MAX)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MatchPoint][%s][%d] invalid cType",
        //     kOwner,
        //     0x260c)
        // L2_LOG_ERROR(Log,
        //     L"[MatchPoint][%s][%d] invalid cType",
        //     kOwner,
        //     0x260c)
        L2_LOG_ERROR(Log, "Matching-point load reply rejected: unsupported type=%u", static_cast<unsigned int>(type));
    } else if (count < 1) {
        // The current image reports the count failure as an
        // invalid cType and still completes MPT_BASE with an empty map.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MatchPoint][%s][%d] invalid cType",
        //     kOwner,
        //     0x261c)
        // L2_LOG_ERROR(Log,
        //     L"[MatchPoint][%s][%d] invalid cType",
        //     kOwner,
        //     0x261c)
        L2_LOG_ERROR(
            Log, "Matching-point reply has no rows: type=%u, count=%d, completing base preparation=%d",
            static_cast<unsigned int>(type), count, type == static_cast<std::uint8_t>(MatchingPoint::MPT_BASE)
        );
        if (type == static_cast<std::uint8_t>(MatchingPoint::MPT_BASE)) {
            g_PledgeGameSystem.OnPrepareGameComplete(points);
        }
    } else {
        for (std::int32_t index = 0; index < count; ++index) {
            std::int32_t userDatabaseId = 0;
            std::int64_t matchingPoint = 0;
            cursor = ::Disassemble(cursor, "dq", &userDatabaseId, &matchingPoint);
            // Current std::map insertion retains the first value when the
            // reply repeats a database id.
            points.emplace(userDatabaseId, matchingPoint);
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][MatchPoint][%s][%d] success",
        //     kOwner,
        //     0x2633)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][MatchPoint][%s][%d] success",
        //     kOwner,
        //     0x2633)
        L2_LOG_INFO(
            Log, "Matching-point reply rows processed: type=%u, reported rows=%d", static_cast<unsigned int>(type), count
        );
        if (type == static_cast<std::uint8_t>(MatchingPoint::MPT_BASE)) {
            g_PledgeGameSystem.OnPrepareGameComplete(points);
        } else if (type == static_cast<std::uint8_t>(MatchingPoint::MPT_OLYMPIAD)) {
            std::uint8_t ignored = 0;
            ::Disassemble(cursor, "c", &ignored);
        }
    }
    return false;
}

bool ReplyCreateDualJob(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067c6b8, RP_EX 0x102.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t ignoredUserDatabaseId = 0;
    std::int32_t npcServerId = 0;
    std::int32_t npcUserServerId = 0;
    std::int32_t dualJobId = 0;
    std::int32_t classId = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "dddddd", &userServerId, &ignoredUserDatabaseId, &npcServerId, &npcUserServerId, &dualJobId, &classId
    );
    static_cast<void>(ignoredUserDatabaseId);

    CVitalData vitalData;
    vitalData.Disassemble(cursor);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2d4c,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2d4c,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Dual-class creation reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (dualJobId == 0) {
        user->m_data->m_bIsActive = true;
        runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"create subjob failed [%s][%d]",
        //     user->GetName(),
        //     classId)
        // L2_LOG_ERROR(Log,
        //     L"create subjob failed [%s][%d]",
        //     user->GetName(),
        //     classId)
        // L2_LOG_ERROR(Log,
        //     "Dual-class creation failed: CacheD returned job ID zero, user=%s, requested class=%d",
        //     ::WideToUtf8(user->GetName()).c_str(), classId)
        L2_LOG_ERROR(
            Log, "Dual-class creation failed: CacheD returned job ID zero, user=%s, requested class=%d",
            ::Utf16ToUtf8(user->GetName()).c_str(), classId
        );
    } else {
        user->InitDualJob(dualJobId, classId);
        user->OnChangeSubJobEnd(SubJob::CREATE_DUALJOB, vitalData);
        user->SendVoteSystemInfoPacket();
        user->GetSocket()->Send(
            "chcch", 0x62, SystemMessageId::NEW_SUBCLASS_ACQUIRED, 1, 0xf, static_cast<std::int16_t>(classId)
        );
    }

    g_npcserver.Send("cddd", 0xba, npcServerId, npcUserServerId, dualJobId != 0 ? classId : -1);
    user->SendUserSubJobInfo(SubJob::CREATE_NEW_CLASS);
    return false;
}

bool ReplyPremiumKamarokaRound(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00672c00, RP_EX 0x103.
    L2_GUARD_FUNCTION();

    std::int32_t round = 0;
    ::Disassemble(packet, "d", &round);
    CPremiumInstantRankingSystem::Inst().SetRound(round);
    CPremiumInstantRankingSystem::Inst().InitStatus();
    CPremiumInstantRankingSystem::Inst().LoadFromDB();
    return false;
}

bool ReplyPremiumKamarokaRankingList(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00672cd0, RP_EX 0x104.
    L2_GUARD_FUNCTION();

    CPremiumInstantRankingSystem::Inst().ReplyRankingList(packet);
    return false;
}

bool ReplyPremiumKamarokaAddNewRecord(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00672d7c, RP_EX 0x105.
    L2_GUARD_FUNCTION();

    CPremiumInstantRankingSystem::Inst().ReplyAddNewRecord(packet);
    return false;
}

bool ReplyPremiumKamarokaPersonalRankInfo(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00691b90, RP_EX 0x106.
    L2_GUARD_FUNCTION();

    std::int32_t userDatabaseId = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "d", &userDatabaseId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), userDatabaseId));
    if (user) {
        CPremiumInstantRankingSystem::Inst().ShowUserRankingInfo(user.get(), cursor);
    }
    return false;
}

bool ResponseGetDailyCount(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068ab48, RP_EX 0xbb. The middle dword is decoded
    // but discarded. The final dword is stored at User+0x6658.
    L2_GUARD_FUNCTION();

    std::int32_t characterId;
    std::int32_t ignored;
    std::int32_t restartItemCount;
    ::Disassemble(packet, "ddd", &characterId, &ignored, &restartItemCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->m_nRestartItemCount = restartItemCount;
    }
    return false;
}

bool ResponseUpdateWeddingEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0069c3d0, RP_EX 0x109.
    L2_GUARD_FUNCTION();

    std::int32_t firstCharacterId = 0;
    std::int32_t secondCharacterId = 0;
    char action = 0;
    ::Disassemble(packet, "ddc", &firstCharacterId, &secondCharacterId, &action);

    CSPointer<User> firstUser =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), firstCharacterId));
    CSPointer<User> secondUser =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), secondCharacterId));
    if (!firstUser || !secondUser) {
        return false;
    }

    // The current callback constructs both strings before examining the
    // action even though neither later contributes a value.
    [[maybe_unused]] std::u16string firstUnusedName;
    [[maybe_unused]] std::u16string secondUnusedName;

    if (action == 1) {
        firstUser->m_nSpouseDbID = secondCharacterId;
        secondUser->m_nSpouseDbID = firstCharacterId;

        std::array<char16_t, 1024> announcement{};
        // Original: u"%s님과 %s님이 결혼하였습니다. 모두 축하해주세요."
        constexpr char16_t announcementFormat[] = u"%s and %s are now married. Please congratulate them!";
        const char16_t* const firstName = firstUser->GetName();
        const char16_t* const secondName = secondUser->GetName();
        static_cast<void>(
            FormatUtf16To(announcement.data(), announcement.size(), announcementFormat, secondName, firstName)
        );

        char16_t systemContext[] = u"SYS";
        firstUser->SendSystemMessage(systemContext, announcement.data());
        secondUser->SendSystemMessage(systemContext, announcement.data());

        CFHTML::StrStrMapType parameters;
        const std::u16string title = CFHTML::MakeHTMLFromStrMap(u"g_birthday_message_01.htm", parameters, 0);
        const std::u16string contents = CFHTML::MakeHTMLFromStrMap(u"g_birthday_message_00.htm", parameters, 0);
        db.RequestWritePostBirth(
            static_cast<std::int32_t>(firstUser->GetDBID()), title.c_str(), contents.c_str(), 0x52a9, 3, 1, 0, 0
        );
        db.RequestWritePostBirth(
            static_cast<std::int32_t>(secondUser->GetDBID()), title.c_str(), contents.c_str(), 0x52a9, 3, 1, 0, 0
        );

        const CSkillInfo* skill = g_SkillDB.GetSkillInfo(CSkillKey(0x23de, 1, 0), nullptr);
        if (skill != nullptr) {
            skill->ActivateSkill(firstUser.get(), firstUser.get(), 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
            skill->ActivateSkill(firstUser.get(), secondUser.get(), 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        }

        skill = g_SkillDB.GetSkillInfo(CSkillKey(0x3261, 1, 0), nullptr);
        if (skill != nullptr) {
            skill->ActivateSkill(firstUser.get(), firstUser.get(), 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
            skill->ActivateSkill(firstUser.get(), secondUser.get(), 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        }
    } else if (action == 2) {
        firstUser->BreakFriendship(secondCharacterId);
        firstUser->m_nSpouseDbID = 0;
        secondUser->m_nSpouseDbID = 0;

        char16_t systemContext[] = u"SYS";
        // Original: u"이혼하였습니다. 이제부터 당신은 자유입니다."
        constexpr char16_t divorceMessage[] = u"You are now divorced. From now on you are free.";
        firstUser->GetSocket()->SendSystemMessage(systemContext, divorceMessage);
        secondUser->GetSocket()->SendSystemMessage(systemContext, divorceMessage);
    }
    return false;
}

bool ResponseLoadWeddingEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00691ea8, RP_EX 0x10a.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t spouseDatabaseId = 0;
    ::Disassemble(packet, "dd", &characterId, &spouseDatabaseId);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user && spouseDatabaseId > 0) {
        user->m_nSpouseDbID = spouseDatabaseId;
    }
    return false;
}

bool ReplyPledgeOnPledgeRecruitBoard(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068d398, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t worldId = 0;
    std::int32_t characterId = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::int32_t replyValue = 0;
    std::int32_t recruitType = 0;
    std::int32_t recruitPledgeType = 0;
    std::int32_t payloadSize = 0;
    packet = ::Disassemble(
        packet, "ddddddd", &worldId, &characterId, &pledgeDatabaseId, &replyValue, &recruitType, &recruitPledgeType,
        &payloadSize
    );

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(worldId, characterId));
    CPledge* pledge = nullptr;
    {
        CSPointer<CPledge> found = CPledge::FindPledge(worldId, pledgeDatabaseId);
        pledge = found.get();
    }

    if (user) {
        std::uint32_t pledgeServerId = 0;
        if (pledge == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Pledge is not loaded before used")
            // L2_LOG_ERROR(Log,
            //     L"Pledge is not loaded before used")
            L2_LOG_ERROR(
                Log,
                "Clan recruitment detail reply has no loaded clan: world=%d, clan DB ID=%d; sending details with clan "
                "object ID zero",
                worldId, pledgeDatabaseId
            );
        } else {
            pledge->m_nRecruitType = recruitType;
            pledge->m_recruitPledgeType = static_cast<PledgeType>(recruitPledgeType);
            pledgeServerId = pledge->Id();
        }
        SendPledgeRecruitBoardDetail(user, pledgeServerId, replyValue, recruitType, recruitPledgeType, payloadSize, packet);
    }
    return false;
}

bool ReplyPledgeWaitingUserList(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t worldId = 0;
    std::int32_t characterId = 0;
    std::int32_t count = 0;
    std::int32_t payloadSize = 0;
    packet = ::Disassemble(packet, "dddd", &worldId, &characterId, &count, &payloadSize);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(worldId, characterId));
    if (user) {
        user->GetSocket()->Send("chdb", 0xfe, 0x144, count, payloadSize, packet);
    }
    return false;
}

bool ReplyUserOnPledgeWaitingUserList(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t worldId = 0;
    std::int32_t requesterId = 0;
    std::int32_t targetId = 0;
    char16_t selfIntroduction[51]{};
    ::Disassemble(packet, "dddS", &worldId, &requesterId, &targetId, 0x66, selfIntroduction);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(worldId, requesterId));
    if (user) {
        user->GetSocket()->Send("chdS", 0xfe, 0x145, targetId, selfIntroduction);
    }
    return false;
}

bool ReplyPledgeDraftUserList(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t worldId = 0;
    std::int32_t requesterId = 0;
    std::int32_t count = 0;
    std::int32_t payloadSize = 0;
    packet = ::Disassemble(packet, "dddd", &worldId, &requesterId, &count, &payloadSize);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(worldId, requesterId));
    if (user) {
        user->GetSocket()->Send("chdb", 0xfe, 0x146, count, payloadSize, packet);
    }
    return false;
}

bool ReplyApplyInfoOnPledgeWaitingList(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068d9bc, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t worldId = 0;
    std::int32_t characterId = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::int32_t payloadSize = 0;
    packet = ::Disassemble(packet, "dddd", &worldId, &characterId, &pledgeDatabaseId, &payloadSize);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(worldId, characterId));
    if (user) {
        CPledge* pledge = nullptr;
        {
            CSPointer<CPledge> found = CPledge::FindPledge(g_nWorldId, pledgeDatabaseId);
            pledge = found.get();
        }
        if (pledge != nullptr) {
            user->GetSocket()->Send("chdb", 0xfe, 0x143, pledge->Id(), payloadSize, packet);
        }
    }
    return false;
}

bool ReplyPledgeRecruitApplyInfo(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &characterId, &result);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->GetSocket()->Send("chd", 0xfe, 0x140, result);
    }
    return false;
}

bool ReplyPledgeJoinSystemCode(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068dcd8, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t code = 0;
    std::int32_t characterId = 0;
    packet = ::Disassemble(packet, "dd", &code, &characterId);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterId));
    if (!user) {
        return false;
    }

    if (code == 0) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CLAN_HAS_TOO_MANY_APPLICANTS);
        return false;
    }

    std::int32_t type = 0;
    std::int32_t remainingTime = 0;
    ::Disassemble(packet, "dd", &type, &remainingTime);
    if (code == 3) {
        if (type != 0) {
            CPledgeJoinSystem::Inst()->SendRemainTimeOnPledge(user.get(), remainingTime);
        }
    } else if (code == 2) {
        CPledgeJoinSystem::Inst()->SendRemainTimeOnUser(user.get(), type, remainingTime);
    }
    return false;
}

bool ReplyClearPledgeTimeTable(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067628c, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int16_t type = 0;
    std::int32_t key = 0;
    ::Disassemble(packet, "hd", &type, &key);
    if (type == 0) {
        CPledgeJoinSystem::Inst()->ClearPledgeRecruitTimePenalty(key);
    } else if (type == 1) {
        CPledgeJoinSystem::Inst()->ClearWaitTimePenalty(key);
    } else if (type == 2) {
        CPledgeJoinSystem::Inst()->ClearDraftTimePenalty(key);
    }
    return false;
}

bool ReplyPetitionExternalNotify(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t worldId = 0;
    std::int32_t characterId = 0;
    ::Disassemble(packet, "dd", &worldId, &characterId);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(worldId, characterId));
    if (user) {
        user->GetSocket()->Send("ch", 0xfe, 0x14b);
    }
    return false;
}

bool ResponseLoadPledgeGameEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00672114, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t season = 0;
    std::int32_t step = 0;
    std::int32_t seasonStartTime = 0;
    std::int32_t maxPrizeCount = 0;
    std::int32_t previousWinnerPledgeId = 0;
    std::int32_t legacyIntegerColumnSix = 0;
    std::int32_t previousWinnerCharacterId = 0;
    ::Disassemble(
        packet, "ddddddd", &season, &step, &seasonStartTime, &maxPrizeCount, &previousWinnerPledgeId,
        &legacyIntegerColumnSix, &previousWinnerCharacterId
    );
    return false;
}

bool ResponseStartPledgeGameEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067220c, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t season = 0;
    std::int32_t step = 0;
    ::Disassemble(packet, "dd", &season, &step);
    g_PledgeGameSystem.Init();
    return false;
}

bool ResponseEndPledgeGameEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006722c8, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t season = 0;
    std::int32_t step = 0;
    std::int32_t seasonStartTime = 0;
    std::int32_t maxPrizeCount = 0;
    std::int32_t initialize = 0;
    ::Disassemble(packet, "ddddd", &season, &step, &seasonStartTime, &maxPrizeCount, &initialize);
    if (initialize != 0) {
        g_PledgeGameSystem.Init();
    }
    return false;
}

bool ResponseUpdatePledgeGameResultEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006723b8, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t season = 0;
    std::int32_t userDbId = 0;
    std::int32_t prizeCount = 0;
    std::int32_t topRecord = 0;
    ::Disassemble(packet, "dddd", &season, &userDbId, &prizeCount, &topRecord);
    static_cast<void>(g_PledgeGameSystem.UpdateTopRecord(topRecord));
    return false;
}

bool ResponseGetPledgeGameRecordEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068b81c, original DBPacketEx.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t season = 0;
    std::int32_t characterId = 0;
    std::int32_t prizeCount = 0;
    std::int32_t surrenderCount = 0;
    ::Disassemble(packet, "dddd", &season, &characterId, &prizeCount, &surrenderCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        g_PledgeGameSystem.SetPledgeGameRecord(
            season, static_cast<std::int32_t>(user->GetServerId()), prizeCount, surrenderCount
        );
    }
    return false;
}

bool ReplyAbnormalUserAction(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t firstUserServerId = 0;
    std::int32_t secondUserServerId = 0;
    std::int32_t action = 0;
    std::int32_t value1 = 0;
    std::int32_t value2 = 0;
    std::int32_t value3 = 0;
    ::Disassemble(packet, "dddddd", &firstUserServerId, &secondUserServerId, &action, &value1, &value2, &value3);

    CCreature* creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(firstUserServerId));
    User* user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user) {
        user->LogAbnormalAction(static_cast<AbnormalUserAction>(action), true, value1, value2, value3, 0);
    }

    creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(secondUserServerId));
    user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user) {
        user->LogAbnormalAction(static_cast<AbnormalUserAction>(action), true, value1, value2, value3, 0);
    }
    return false;
}

bool Reply_RP_EX_SEND_SYSTEM_MESSAGE(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int16_t messageId = -1;
    char parameterType = 0;
    packet = ::Disassemble(packet, "dhc", &userServerId, &messageId, &parameterType);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        return false;
    }

    if (parameterType == 0 && messageId > 0) {
        user->SendSystemMessage(static_cast<unsigned int>(messageId));
        return false;
    }

    std::int32_t parameterSize = 0;
    packet = ::Disassemble(packet, "d", &parameterSize);
    user->GetSocket()->Send(
        "chcb", 0x62, static_cast<int>(messageId), static_cast<int>(parameterType), parameterSize, packet
    );
    return false;
}

bool Reply_RP_EX_LOAD_ITEM_REUSE_DELAY(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t count = 0;
    packet = ::Disassemble(packet, "dd", &userServerId, &count);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x33d1,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x33d1,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Item reuse-delay reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (count <= 0) {
        return false;
    }

    const std::int32_t currentTime = static_cast<std::int32_t>(std::time(nullptr));
    for (std::int32_t index = 0; index < count; ++index) {
        std::int32_t itemId = 0;
        std::int32_t reuseEndTime = 0;
        packet = ::Disassemble(packet, "dd", &itemId, &reuseEndTime);
        if (currentTime >= reuseEndTime) {
            continue;
        }

        const std::int32_t remainingSeconds =
            static_cast<std::int32_t>(static_cast<std::uint32_t>(reuseEndTime) - static_cast<std::uint32_t>(currentTime));
        const std::int64_t remainingDelay = static_cast<std::int64_t>(remainingSeconds) * 1000;
        if (remainingDelay > std::numeric_limits<std::uint32_t>::max()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Reply_RP_EX_LOAD_ITEM_REUSE_DELAY, "
            //     L"remain delay overflow: [%d, %d, %d]",
            //     L"DBPacketEx.cpp",
            //     0x33e3,
            //     userServerId,
            //     itemId,
            //     reuseEndTime)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Reply_RP_EX_LOAD_ITEM_REUSE_DELAY, "
            //     L"remain delay overflow: [%d, %d, %d]",
            //     L"DBPacketEx.cpp",
            //     0x33e3,
            //     userServerId,
            //     itemId,
            //     reuseEndTime)
            L2_LOG_ERROR(
                Log,
                "Item reuse delay exceeds 32-bit milliseconds: user object=%u, item=%d, end time=%d, delay=%lld ms; storing "
                "truncated value",
                static_cast<unsigned int>(userServerId), itemId, reuseEndTime, static_cast<long long>(remainingDelay)
            );
        }
        user->InsertItemReuseDelay(itemId, static_cast<std::uint32_t>(remainingDelay));
    }
    return false;
}

bool ReplyAgathionElapsedTimeAck(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t agathionId = 0;
    std::int32_t elapsedTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &agathionId, &elapsedTime);

    User* user = nullptr;
    {
        CSPointer<User> found =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
        user = found.get();
    }
    if (user && user->GetSocket()) {
        user->SetAgathionElapsedTime(agathionId, elapsedTime);
    }
    return false;
}

bool ReplySaveDailyPCCafePoint(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t dailyPoint = 0;
    std::int32_t bonusCheckLevel = 0;
    ::Disassemble(packet, "ddd", &characterId, &dailyPoint, &bonusCheckLevel);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->SetDailyPCCafeInfo(dailyPoint, bonusCheckLevel);
    }
    return false;
}

bool DeleteBuffByAdmin(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t skillId = 0;
    ::Disassemble(packet, "dd", &characterId, &skillId);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    if (skillId != 0x38eb && skillId != 0x5b30) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"DeleteBuffByAdmin invalid skill_id [%d]")
        // L2_LOG_ERROR(Log,
        //     L"DeleteBuffByAdmin invalid skill_id [%d]")
        L2_LOG_ERROR(Log, "Admin buff deletion rejected: unsupported skill=%d, character DB ID=%d", skillId, characterId);
        return false;
    }

    L2_GUARD(user->m_InfectedSkillsSect);
    if (user->_DeleteAbnormalStatus(ABNS_DEATH_PENALTY_GD, nullptr, nullptr, true, SPET_ADMIN, false)) {
        user->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        user->m_bSpelledInfoChanged = true;
        user->m_NeedUpdatePacketBits |= 4;
    }
    return false;
}

bool ReplyLoadGroupToggleEx(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t count = 0;
    const unsigned char* const rows = ::Disassemble(packet, "dd", &userServerId, &count);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user) {
        user->OnGroupToggleState(count, rows);
        user->m_bAbnormalStatusChanged = true;
    }
    return false;
}

bool ReplyLoadFriendDetailInfo(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067da14, RP_EX 0x10b.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t friendCharacterId = 0;
    std::int32_t friendPledgeId = 0;
    std::int32_t friendAllianceId = 0;
    char birthMonth = 0;
    char birthDay = 0;
    std::int32_t lastLogoutTime = 0;
    ::Disassemble(
        packet, "ddddccd", &userServerId, &friendCharacterId, &friendPledgeId, &friendAllianceId, &birthMonth, &birthDay,
        &lastLogoutTime
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2e75,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2e75,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Friend-detail reply ignored: user object %u is unavailable or is not a player",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    FriendDetailInfo detailInfo;
    detailInfo.nCharDBID = friendCharacterId;
    detailInfo.nPledgeDBID = friendPledgeId;
    detailInfo.nAllianceDBID = friendAllianceId;
    detailInfo.cBirthMonth = birthMonth;
    detailInfo.cBirthDay = birthDay;
    detailInfo.tLastLogoutTime = lastLogoutTime;
    CSPointer<CFriends> friends = user->GetFriends();
    if (friends) {
        friends->SetDetail(static_cast<unsigned int>(friendCharacterId), detailInfo);
        user->CheckAbnormalVisualEffect();
        user->SendFriendDetailInfo(friendCharacterId, false);
    }
    return false;
}

bool ReplyLoadDroidNPCAppearance(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00691fcc, RP_EX 0x10c.
    L2_GUARD_FUNCTION();

    std::uint32_t command = 0;
    std::int32_t npcServerId = 0;
    char success = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddc", &command, &npcServerId, &success);

    if (command >= static_cast<std::uint32_t>(protocol::DROIDNPC_COMMAND::DC_MAX)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyLoadDroidNPCAppearance failed 0, "
        //     L"load character failed",
        //     L"DBPacketEx.cpp",
        //     0x2e9d)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReplyLoadDroidNPCAppearance failed 0, "
        //     L"load character failed",
        //     L"DBPacketEx.cpp",
        //     0x2e9d)
        L2_LOG_ERROR(
            Log, "Droid appearance reply rejected: unsupported command=%u, NPC object=%u",
            static_cast<unsigned int>(command), static_cast<unsigned int>(npcServerId)
        );
        return false;
    }
    if (success != 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyLoadDroidNPCAppearance failed 1, "
        //     L"load character failed",
        //     L"DBPacketEx.cpp",
        //     0x2ea3)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReplyLoadDroidNPCAppearance failed 1, "
        //     L"load character failed",
        //     L"DBPacketEx.cpp",
        //     0x2ea3)
        L2_LOG_ERROR(
            Log, "Droid appearance load failed: result=%d, NPC object=%u", static_cast<int>(success),
            static_cast<unsigned int>(npcServerId)
        );
        return false;
    }

    std::int32_t payloadSize = 0;
    cursor = ::Disassemble(cursor, "d", &payloadSize);
    if (payloadSize == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyLoadDroidNPCAppearance failed 2, "
        //     L"load character failed",
        //     L"DBPacketEx.cpp",
        //     0x2eab)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReplyLoadDroidNPCAppearance failed 2, "
        //     L"load character failed",
        //     L"DBPacketEx.cpp",
        //     0x2eab)
        L2_LOG_ERROR(
            Log, "Droid appearance reply rejected: empty payload, NPC object=%u", static_cast<unsigned int>(npcServerId)
        );
        return false;
    }

    protocol::RP_Load_DroidNPC_Appearance appearance;
    network::Stream stream(const_cast<unsigned char*>(cursor), payloadSize);
    if (!appearance.Decode(&stream)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyLoadDroidNPCAppearance failed 3, "
        //     L"load character failed",
        //     L"DBPacketEx.cpp",
        //     0x2eb3)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReplyLoadDroidNPCAppearance failed 3, "
        //     L"load character failed",
        //     L"DBPacketEx.cpp",
        //     0x2eb3)
        L2_LOG_ERROR(
            Log, "Droid appearance reply rejected: payload decoding failed, bytes=%d, NPC object=%u", payloadSize,
            static_cast<unsigned int>(npcServerId)
        );
        return false;
    }

    CSPointer<CNPC> npc = g_NPCMap.FindObject(npcServerId);
    if (!npc || !npc->m_isDroidNPC) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyLoadDroidNPCAppearance failed 4, "
        //     L"load character failed",
        //     L"DBPacketEx.cpp",
        //     0x2eba)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReplyLoadDroidNPCAppearance failed 4, "
        //     L"load character failed",
        //     L"DBPacketEx.cpp",
        //     0x2eba)
        L2_LOG_ERROR(
            Log, "Droid appearance reply ignored: NPC object %u is unavailable or is not a droid",
            static_cast<unsigned int>(npcServerId)
        );
        return false;
    }

    const RaceType race = static_cast<RaceType>(appearance.cRace);
    const SexType sex = static_cast<SexType>(appearance.cGender);
    const ClassType baseClass =
        CPCParamDB::GetBasicClassByRaceAndFTM(race, appearance.cJobGroup == 0, appearance.cGender == 0);

    const std::wstring characterName = Utf16ToWide(appearance.szCharName.data());
    npc->SetName(characterName.c_str());
    npc->SetNickName(appearance.szNickName.data());

    DroidNPCInfo& info = npc->m_DroidNPCInfo;
    info.cGender = appearance.cGender;
    info.cRace = appearance.cRace;
    info.nBaseClass = static_cast<std::int32_t>(baseClass);
    info.nNowClass = appearance.nClass;
    info.cHero = appearance.cHero;
    info.cNobless = appearance.cNobless;
    info.dCollisionRadius = g_PCParamDB.GetCollisionRadius(sex, baseClass, race);
    info.dCollisionHeight = g_PCParamDB.GetCollisionHeight(sex, baseClass, race);

    std::uint32_t pledgeServerId = 0;
    CAlliance* alliance = nullptr;
    if (appearance.nPledgeDBID > 0) {
        // The current callback extracts the raw pointer from the returned
        // CSPointer temporary and releases that temporary before these two
        // calls. Preserve that unusual lifetime literally.
        CPledge* const pledge = CPledge::FindPledge(g_nWorldId, appearance.nPledgeDBID).get();
        if (pledge != nullptr) {
            pledgeServerId = pledge->Id();
            alliance = pledge->GetAlliance();
        }
        if (alliance != nullptr) {
            // Current callback performs and discards this virtual GetDBID.
            static_cast<void>(alliance->GetDBID());
        }
    }

    info.nPledgeType = appearance.nPledgeType;
    info.nPledgeSID = static_cast<std::int32_t>(pledgeServerId);
    info.nPledgeDBID = appearance.nPledgeDBID;
    info.nPledgeCrestDBID = appearance.nPledgeCrestDBID;
    info.nPledgeEmblemDBID = appearance.nPledgeEmblemDBID;
    info.nAllianceDBID = appearance.nAllianceDBID;
    info.nAllianceCrestDBID = appearance.nAllianceCrestDBID;
    info.nFace = appearance.nFace;
    info.nHairShape = appearance.nHairShape;
    info.nHairColor = appearance.nHairColor;
    info.nFaceEx = appearance.nFaceEx;
    info.nHairShapeEx = appearance.nHairShapeEx;
    info.nHairColorEx = appearance.nHairColorEx;
    info.cHairAccFlag = appearance.cHairAccFlag;
    info.nSlotUnderware = appearance.nSlotUnderware;
    info.nSlotHead = appearance.nSlotHead;
    info.nSlotRightHand = appearance.nSlotRightHand;
    info.nSlotLeftHand = appearance.nSlotLeftHand;
    info.nSlotBothHands = appearance.nSlotBothHands;
    info.nSlotGloves = appearance.nSlotGloves;
    info.nSlotChest = appearance.nSlotChest;
    info.nSlotLegs = appearance.nSlotLegs;
    info.nSlotFeet = appearance.nSlotFeet;
    info.nSlotBack = appearance.nSlotBack;
    info.nSlotHair = appearance.nSlotHair;
    info.nSlotHair2 = appearance.nSlotHair2;
    info.nShapeShiftRightHand = appearance.nShapeShiftRightHand;
    info.nShapeShiftLeftHand = appearance.nShapeShiftLeftHand;
    info.nShapeShiftBothHands = appearance.nShapeShiftBothHands;
    info.nShapeShiftGloves = appearance.nShapeShiftGloves;
    info.nShapeShiftChest = appearance.nShapeShiftChest;
    info.nShapeShiftLegs = appearance.nShapeShiftLegs;
    info.nShapeShiftFeet = appearance.nShapeShiftFeet;
    info.nShapeShiftHair = appearance.nShapeShiftHair;
    info.nShapeShfitHair2 = appearance.nShapeShfitHair2;
    info.nDeco1 = appearance.nDeco1;
    info.nDeco2 = appearance.nDeco2;
    info.nDeco3 = appearance.nDeco3;
    info.nDeco4 = appearance.nDeco4;
    info.nDeco5 = appearance.nDeco5;
    info.nDeco6 = appearance.nDeco6;
    info.nEnchantWeapon = appearance.nEnchantWeapon;
    info.nEnchantChest = appearance.nEnchantChest;
    info.nEnchantLegs = appearance.nEnchantLegs;
    info.nEnchantHead = appearance.nEnchantHead;
    info.nEnchantGloves = appearance.nEnchantGloves;
    info.nEnchantFeet = appearance.nEnchantFeet;
    info.nVariationOpt1 = appearance.nVariationOpt1;
    info.nVariationOpt2 = appearance.nVariationOpt2;
    info.nNickNameColor = appearance.nNickNameColor;
    info.nCharNameColor = appearance.nCharNameColor;
    info.nKarma = appearance.nKarma;
    info.nAP = appearance.nAP;
    info.nBonusCount = appearance.nBonusCount;

    if (command == static_cast<std::uint32_t>(protocol::DROIDNPC_COMMAND::DC_SPAWNNPC)) {
        NpcEnterWorld_Do(
            npc->m_data->index, &npc->m_vDroidSpawnPos, npc->m_nDroidSpawnDir, npc->m_nDroidSpawnDefaultParam,
            npc->m_nDroidSpawnDefaultHP, npc->m_nDroidSpawnDefaultMP, npc->m_nDroidSpawnInZoneID
        );
        // NpcEnterWorld_Do already increments this counter. The current
        // RP_EX callback increments it a second time.
        runtime::raw_atomic::IncrementI32(&g_nNPCCount);
    } else if (command == static_cast<std::uint32_t>(protocol::DROIDNPC_COMMAND::DC_CHANGENPC)) {
        CSerializedMessage* const message = new CSerializedCreatureResetVisiblity(npc->CastCreature());
        npc->PushSerializedMessage(message);
        npc->EndDroidNPCSetting();
    }
    return false;
}

bool Reply_RP_EX_SIGNIN_PLEDGE(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067ca14, RP_EX 0x10d.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t pledgeServerId = 0;
    std::int32_t pledgeType = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dddd", &userServerId, &pledgeServerId, &pledgeType, &result);

    CPledge* const pledge = CPledge::GetObjectW(static_cast<std::uint32_t>(pledgeServerId));
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBPacketEx.cpp",
        //     0x2f69,
        //     pledgeServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBPacketEx.cpp",
        //     0x2f69,
        //     pledgeServerId)
        L2_LOG_ERROR(
            Log, "Clan sign-in reply cannot be applied: clan object=%u is unavailable, user object=%u, result=%d",
            static_cast<unsigned>(pledgeServerId), static_cast<unsigned>(userServerId), result
        );
        return false;
    }
    PledgeTransactionReleaser releaseTransaction(pledge);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2f6d,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2f6d,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Clan sign-in reply cannot be applied: user object=%u is unavailable, clan object=%u, result=%d",
            static_cast<unsigned>(userServerId), static_cast<unsigned>(pledgeServerId), result
        );
        return false;
    }

    CUserSocket* userSocket = user->GetSocket();
    if (result == 1) {
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nPledgeType, pledgeType, 0x34);
        const int gradeId = CPledge::GetDefaultPowerGrade(static_cast<PledgeType>(pledgeType));
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nGradeId, gradeId, 0x46);
        db.RequestSetPledgePowerGrade(user->GetDBID(), user->m_data->m_nGradeId);
        pledge->Join(user);
        user->ParamChanged(std::int64_t{PCB_USER});
        CPledgeJoinSystem::Inst()->ClearUserListByPledgeJoin(user);

        userSocket = user->GetSocket();
        if (userSocket != nullptr) {
            userSocket->Send("chddd", 0xfe, 0x17d, 3, pledge->GetDbId(), pledge->GetWorldId());
        }
        userSocket->SendSystemMessage(SystemMessageId::JOINED_CLAN);
        userSocket->Send("chd", 0xfe, 0x18d, 0);
    } else {
        userSocket->SendSystemMessage(SystemMessageId::OPERATION_NOT_PROCESSED_TRY_AGAIN);
        userSocket->Send("chd", 0xfe, 0x18d, -1);
    }

    pledge->SetPledgeWorkDone();
    return false;
}

bool ReplyLoadFactionPoint(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006955f8, RP_EX 0x10e.
    L2_GUARD_FUNCTION();

    protocol::RP_LOAD_FACTION_POINT response;
    network::Stream stream(const_cast<unsigned char*>(packet), 0x4000);
    if (!response.Decode(&stream)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyLoadFactionPoint failed, "
        //     L"invalid packet body",
        //     L"DBPacketEx.cpp",
        //     0x2ff0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReplyLoadFactionPoint failed, "
        //     L"invalid packet body",
        //     L"DBPacketEx.cpp",
        //     0x2ff0)
        L2_LOG_ERROR(Log, "Faction point load reply rejected: packet body could not be decoded");
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(response.nUserServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2ff5,
        //     response.nUserServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2ff5,
        //     response.nUserServerId)
        L2_LOG_ERROR(
            Log, "Faction point load reply cannot be applied: user object=%u is unavailable",
            static_cast<unsigned>(response.nUserServerId)
        );
        return false;
    }

    for (const protocol::FactionPointInfo& faction : response.factionList) {
        // The current callback performs this table read before any faction
        // id validation. Keep the literal type-1 access and skip policy.
        if (FactionByLevel[faction.nType - 1] != nullptr) {
            static_cast<void>(user->m_data->UpdateFactionPoint(faction.nType, faction.nPoint));
            static_cast<void>(user->ValidateFactionLevel(faction.nType));
        }
    }

    // The DB-loaded assignment is itself short-circuited when faction info
    // was already sent in the current machine.
    if (!user->m_bIsSendFactionInfoCompleted && (user->m_bIsDBFactionInfoLoaded = true) &&
        user->m_bIsSendUserInfoCompleted) {
        static_cast<void>(user->SendFactionInfoList(0));
        user->m_bIsSendFactionInfoCompleted = true;
    }
    return false;
}

bool ReplyUpdateFactionPoint(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067cd40, RP_EX 0x10f.
    L2_GUARD_FUNCTION();

    protocol::RP_UPDATE_FACTION_POINT response;
    network::Stream stream(const_cast<unsigned char*>(packet), 0x4000);
    if (!response.Decode(&stream)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyUpdateFactionPoint failed, "
        //     L"invalid packet body",
        //     L"DBPacketEx.cpp",
        //     0x2fa3)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReplyUpdateFactionPoint failed, "
        //     L"invalid packet body",
        //     L"DBPacketEx.cpp",
        //     0x2fa3)
        L2_LOG_ERROR(Log, "Faction point update reply rejected: packet body could not be decoded");
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(response.nUserServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2fa8,
        //     response.nUserServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2fa8,
        //     response.nUserServerId)
        L2_LOG_ERROR(
            Log, "Faction point update reply cannot be applied: user object=%u is unavailable, faction=%d",
            static_cast<unsigned>(response.nUserServerId), response.nFaction.nType
        );
        return false;
    }

    // Both lanes are read before UpdateFactionPoint validates
    // the id. This keeps the current invalid-id access order.
    const int factionIndex = response.nFaction.nType - 1;
    const std::int64_t previousPoint = user->m_data->m_nFactionPoint[factionIndex];
    const int previousLevel = user->m_data->m_nFactionLevel[factionIndex];
    if (!user->m_data->UpdateFactionPoint(response.nFaction.nType, response.nFaction.nPoint)) {
        return false;
    }
    const int levelDelta = user->ValidateFactionLevel(response.nFaction.nType);

    int logId = 0;
    if (response.cCommand == static_cast<std::int8_t>(protocol::Faction::MODIFY)) {
        logId = 0x1fcf;
    } else if (response.cCommand == static_cast<std::int8_t>(protocol::Faction::SET)) {
        logId = 0x1fd0;
    }
    if (logId != 0) {
        CLogFormatter formatter;
        formatter.WriteLog1(
            logId, user, nullptr, nullptr, nullptr, nullptr, static_cast<std::int64_t>(response.nReason),
            static_cast<std::int64_t>(response.nFaction.nType), previousPoint, response.nFaction.nPoint,
            static_cast<std::int64_t>(previousLevel),
            static_cast<std::int64_t>(runtime::AddInt32Wrapping(previousLevel, levelDelta)),
            static_cast<std::int64_t>(user->m_data->m_nSubJobId)
        );
    }

    const std::int64_t pointDelta = runtime::SubtractInt64Wrapping(response.nFaction.nPoint, previousPoint);
    if (pointDelta > 0) {
        user->GetSocket()->Send(
            "chccccd", 0x62, SystemMessageId::FACTION_FRIENDSHIP_POINTS_GAINED, 2, 0x18,
            static_cast<int>(static_cast<std::int8_t>(response.nFaction.nType)), 1,
            runtime::Int32FromBits(static_cast<std::uint32_t>(pointDelta))
        );
    }

    if (levelDelta > 0) {
        user->BroadcastSocialAction(0x415a, 0x800, 0);
        static_cast<void>(user->SendFactionInfoList(0));
        user->GetSocket()->Send(
            "chccc", 0x62, SystemMessageId::FACTION_FRIENDSHIP_LEVEL_INCREASED, 1, 0x18,
            static_cast<int>(static_cast<std::int8_t>(response.nFaction.nType))
        );
        user->GetSocket()->Send("ch", 0xfe, 0x1c2);
    }
    return false;
}

bool ReplyUpdateBlockMemoEx(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t blockedCharacterId = 0;
    std::array<char16_t, 51> packetMemo{};
    ::Disassemble(packet, "ddS", &characterId, &blockedCharacterId, 0x66, packetMemo.data());

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterId));
    if (user) {
        user->UpdateBlockMemo(blockedCharacterId, packetMemo.data());
    }
    return false;
}

bool ReplyUpdateFriendMemoEx(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t friendCharacterId = 0;
    std::array<char16_t, 51> packetMemo{};
    ::Disassemble(packet, "ddS", &characterId, &friendCharacterId, 0x66, packetMemo.data());

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterId));
    if (user) {
        user->OnReplyUpdateFriendMemo(friendCharacterId, packetMemo.data());
    }
    return false;
}

bool ReplySummonLoadAbnormalStatusEx(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t characterId = 0;
    std::int32_t summonClassId = 0;
    std::int32_t summonDatabaseId = 0;
    const unsigned char* const rows =
        ::Disassemble(packet, "dddd", &userServerId, &characterId, &summonClassId, &summonDatabaseId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user) {
        user->OnSummonAbnormalState(summonClassId, summonDatabaseId, rows);
    }
    return false;
}

bool ReplyCharacterSavePacket(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &characterId, &result);
    static_cast<void>(result);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        runtime::raw_atomic::ExchangeI32(&user->m_lSaveCharacter, 0);
    }
    return false;
}

} // namespace DBPacketExHandler

bool ReplyBrGetEventScore(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0069a6b0, RP_EX 0x8f.
    L2_GUARD_FUNCTION();

    std::int32_t eventType;
    std::int32_t roundNo;
    std::int32_t characterId;
    std::array<char16_t, 25> characterName{};
    std::int32_t score;
    ::Disassemble(packet, "dddSd", &eventType, &roundNo, &characterId, 0x32, characterName.data(), &score);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterId));
    if (!user) {
        if (characterName[0] == u'\0' && eventType == GAME_EVENT::ET_LUCKY_GAME) {
            CLuckyGameSystem::Inst()->LoadFromDB(roundNo, score);
        }
        return false;
    }

    if (eventType == GAME_EVENT::ET_WORLD) {
        const std::wstring hostCharacterName = Utf16ToWide(characterName.data());
        std::array<char16_t, 256> message{};
        static_cast<void>(
            FormatUtf16To(message.data(), message.size(), u"score of %s = %d", hostCharacterName.c_str(), score)
        );
        char16_t systemContext[] = u"SYS";
        user->SendSystemMessage(systemContext, message.data());
    } else if (eventType == GAME_EVENT::ET_INVITATION) {
        user->SetInkEnergy(score);
    } else if (eventType == GAME_EVENT::ET_LUCKY_GAME) {
        CLuckyGameSystem::Inst()->SetPersonalPoint(user.get(), roundNo, score, true);
    }
    return false;
}

bool ReplyBrSetEventScore(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t eventType;
    std::int32_t ignored;
    std::int32_t characterId;
    std::int32_t score;
    ::Disassemble(packet, "dddd", &eventType, &ignored, &characterId, &score);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterId));
    if (user && eventType == GAME_EVENT::ET_INVITATION) {
        user->SetInkEnergy(score);
    }
    return false;
}

bool ReplyLoadPVPPointRestrainPacket(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t giveCount = 0;
    std::int32_t lastGiveTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &giveCount, &lastGiveTime);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->SetKillPVPPointChecker(giveCount, lastGiveTime);
    }
    return false;
}

namespace DBPacketExHandler {

bool LoadTimeToGiveKarmaPoint(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t killedTime = 0;
    ::Disassemble(packet, "dd", &characterId, &killedTime);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->SetKilledTimeByUserAsChao(killedTime);
    }
    return false;
}

bool ResponseChangeAttributeItem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067ad50, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId = 0;
    packet = ::Disassemble(packet, "dd", &result, &userServerId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x1f41,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x1f41,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Item attribute change reply cannot be applied: user object=%u is unavailable, result=%d",
            static_cast<unsigned>(userServerId), result
        );
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (result == 0) {
        user->SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_CHANGE);
        user->SendSystemMessage(SystemMessageId::ATTRIBUTE_CHANGE_FAILED);
        userSocket->Send("ch", 0xfe, 0x116);
        return false;
    }

    std::int32_t sourceItemServerId = 0;
    std::int64_t amount = 0;
    std::int32_t targetItemServerId = 0;
    ::Disassemble(packet, "dQd", &sourceItemServerId, &amount, &targetItemServerId);

    CItem* const sourceItem = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(sourceItemServerId));
    CItem* const targetItem = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(targetItemServerId));
    if (sourceItem == nullptr || targetItem == nullptr) {
        user->SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_CHANGE);
        user->SendSystemMessage(SystemMessageId::ATTRIBUTE_CHANGE_FAILED);
        userSocket->Send("ch", 0xfe, 0x116);
        return false;
    }

    user->ReplyChangeAttributeItem(*sourceItem, amount, *targetItem);
    return false;
}

bool ResponseCancelDualClass(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067af88, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t unusedResult = 0;
    std::int32_t unusedClassId = 0;
    ::Disassemble(packet, "ddd", &userServerId, &unusedResult, &unusedClassId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"DBPacketEx.cpp",
        //     0x1f74,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"DBPacketEx.cpp",
        //     0x1f74,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Dual class cancellation reply cannot be applied: user object=%u is unavailable",
            static_cast<unsigned>(userServerId)
        );
        return false;
    }

    user->m_data->m_DualClassID = -1;
    user->m_data->m_IsDualClass = false;
    user->SendUserSubJobInfo(SubJob::NOTIFY_CLASS_INFO);
    runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
    return false;
}

bool ResponseShapeShiftItemEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067b0e8, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId = 0;
    std::int32_t sourceItemServerId = 0;
    std::int64_t sourceAmount = 0;
    std::int32_t targetItemServerId = 0;
    std::int32_t extraItemServerId = 0;
    std::int32_t consumeExtraItem = 0;
    std::int32_t shapeShiftingClassId = 0;
    std::int64_t adenaAmount = 0;
    std::int32_t itemPeriod = -1;
    ::Disassemble(
        packet, "dddQddddQd", &result, &userServerId, &sourceItemServerId, &sourceAmount, &targetItemServerId,
        &extraItemServerId, &consumeExtraItem, &shapeShiftingClassId, &adenaAmount, &itemPeriod
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2134,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2134,
        //     userServerId)
        L2_LOG_ERROR(
            Log,
            "Item appearance change reply cannot be applied: user object=%u is unavailable, target item object=%u, "
            "result=%d",
            static_cast<unsigned>(userServerId), static_cast<unsigned>(targetItemServerId), result
        );
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (result == 1) {
        CItem* const sourceItem = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(sourceItemServerId));
        CItem* const targetItem = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(targetItemServerId));
        CItem* const extraItem = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(extraItemServerId));
        if (sourceItem != nullptr && targetItem != nullptr && extraItem != nullptr) {
            if (targetItem->IsPeriodicShapeShiftedItem(user)) {
                user->OnRestorePeriodicShapeShiftedItem(targetItem);
            }
            if (itemPeriod >= 0) {
                user->OnGetPeriodicShapeShiftedItem(targetItem, itemPeriod);
                user->SendPeriodicShapeShiftedItemList();
            }
            user->ReplyShapeShiftItem(
                sourceItem, sourceAmount, *targetItem, extraItem, consumeExtraItem, 0, shapeShiftingClassId, adenaAmount,
                itemPeriod
            );
            return false;
        }
    }

    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
    userSocket->Send(
        "chdddd", 0xfe, 300,
        // 2,
        ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
    );
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    return false;
}

bool ResponseShapeShiftItemFixedEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067b424, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId = 0;
    std::int32_t sourceItemServerId = 0;
    std::int64_t sourceAmount = 0;
    std::int32_t targetItemServerId = 0;
    std::int32_t shapeShiftingClassId = 0;
    std::int64_t adenaAmount = 0;
    std::int32_t itemPeriod = -1;
    ::Disassemble(
        packet, "dddQddQd", &result, &userServerId, &sourceItemServerId, &sourceAmount, &targetItemServerId,
        &shapeShiftingClassId, &adenaAmount, &itemPeriod
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2190,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2190,
        //     userServerId)
        L2_LOG_ERROR(
            Log,
            "Fixed item appearance change reply cannot be applied: user object=%u is unavailable, target item object=%u, "
            "result=%d",
            static_cast<unsigned>(userServerId), static_cast<unsigned>(targetItemServerId), result
        );
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (result == 1) {
        CItem* const sourceItem = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(sourceItemServerId));
        CItem* const targetItem = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(targetItemServerId));
        if (sourceItem != nullptr && targetItem != nullptr) {
            if (targetItem->IsPeriodicShapeShiftedItem(user)) {
                user->OnRestorePeriodicShapeShiftedItem(targetItem);
            }
            if (itemPeriod >= 0) {
                user->OnGetPeriodicShapeShiftedItem(targetItem, itemPeriod);
                user->SendPeriodicShapeShiftedItemList();
            }
            user->ReplyShapeShiftItem(
                sourceItem, sourceAmount, *targetItem, nullptr, 0, 0, shapeShiftingClassId, adenaAmount, itemPeriod
            );
            return false;
        }
    }

    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
    userSocket->Send(
        "chdddd", 0xfe, 300,
        // 2,
        ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
    );
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    return false;
}

bool ResponseRestoreShapeShiftItemEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067b724, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId = 0;
    std::int32_t sourceItemServerId = 0;
    std::int64_t sourceAmount = 0;
    std::int32_t targetItemServerId = 0;
    std::int32_t shapeShiftingClassId = 0;
    ::Disassemble(
        packet, "dddQdd", &result, &userServerId, &sourceItemServerId, &sourceAmount, &targetItemServerId,
        &shapeShiftingClassId
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x21e2,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x21e2,
        //     userServerId)
        L2_LOG_ERROR(
            Log,
            "Item appearance restoration reply cannot be applied: user object=%u is unavailable, target item object=%u, "
            "result=%d",
            static_cast<unsigned>(userServerId), static_cast<unsigned>(targetItemServerId), result
        );
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (result == 1) {
        CItem* const sourceItem = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(sourceItemServerId));
        CItem* const targetItem = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(targetItemServerId));
        if (sourceItem != nullptr && targetItem != nullptr) {
            user->OnRestorePeriodicShapeShiftedItem(targetItem);
            user->ReplyShapeShiftItem(sourceItem, sourceAmount, *targetItem, nullptr, 0, 1, shapeShiftingClassId, 0, -1);
            return false;
        }
    }

    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
    user->SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
    userSocket->Send(
        "chdddd", 0xfe, 300,
        // 2,
        ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
    );
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    return false;
}

bool ResponseExpireShapeShiftItemEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067b9d4, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId = 0;
    std::int32_t itemServerId = 0;
    std::int32_t shapeShiftingClassId = 0;
    ::Disassemble(packet, "dddd", &result, &userServerId, &itemServerId, &shapeShiftingClassId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2228,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2228,
        //     userServerId)
        L2_LOG_ERROR(
            Log,
            "Item appearance expiration reply cannot be applied: user object=%u is unavailable, item object=%u, result=%d",
            static_cast<unsigned>(userServerId), static_cast<unsigned>(itemServerId), result
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result == 0) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
        return false;
    }

    CItem* const item = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(itemServerId));
    if (item == nullptr) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
        return false;
    }

    user->OnExpiredPeriodicShapeShiftedItem(item);
    {
        L2_GUARD(*item->m_lock);
        item->SetShapeShiftingClassId(shapeShiftingClassId);
    }
    user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
    user->m_data->SetNeedUpdatePacket(0x49);
    user->ValidateExChanged(ValidateEx::VEB_SET_ITEM);
    user->ValidateChanged(VB_MAX);
    user->SendItemList(false, true, nullptr);
    user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_SHAPESHIFT});
    return false;
}

bool ReplyCancelEquipmentAddedEffectAndDropItem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067bc08, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId;
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    std::int32_t beforeOwnerId;
    std::int32_t beforeItemDbId;
    std::int64_t beforeAmount;
    std::int32_t afterOwnerId;
    std::int32_t afterItemDbId;
    std::int64_t afterAmount;
    std::int32_t setContributeData;
    std::int32_t effectMask;
    ::Disassemble(
        packet, "dddddddQddQhd", &result, &userServerId, &x, &y, &z, &beforeOwnerId, &beforeItemDbId, &beforeAmount,
        &afterOwnerId, &afterItemDbId, &afterAmount, &setContributeData, &effectMask
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2497,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2497,
        //     userServerId)
        L2_LOG_ERROR(
            Log,
            "Equipment effect cancellation and item drop reply cannot be applied: user object=%u is unavailable, result=%d",
            static_cast<unsigned>(userServerId), result
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result == 1) {
        CItem* const beforeItem = user->m_inv.GetByDBID(beforeItemDbId);
        CItem* const afterItem = user->m_inv.GetByDBID(afterItemDbId);
        FVector position(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
        if (!user->ReplyCancelEquipmentAddedEffectAndDropItem(
                &position, beforeItem, afterItem, beforeOwnerId, afterOwnerId, beforeAmount, afterAmount, effectMask,
                setContributeData == 1
            )) {
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Chao item penalty has a problem")
        // L2_LOG_ERROR(Log,
        //     L"Chao item penalty has a problem")
        L2_LOG_ERROR(
            Log,
            "Equipment effect cancellation and item drop failed in CacheD: user object=%u, result=%d, before item database "
            "ID=%d, after item database ID=%d",
            static_cast<unsigned>(userServerId), result, beforeItemDbId, afterItemDbId
        );
    }
    return false;
}

bool ResponseLoadMenteeStateEx(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t state = 0;
    ::Disassemble(packet, "dd", &characterId, &state);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user && state != 0) {
        user->SetMentoringGraduate(true);
        g_MenteeWaitingList.DeleteWaitingList(user.get());
    }
    return false;
}

} // namespace DBPacketExHandler

bool ReplyPeriodicItemJob(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067a024, RP_EX 0x5c.
    L2_GUARD_FUNCTION();

    std::int32_t job;
    std::int32_t userServerId;
    const unsigned char* const cursor = ::Disassemble(packet, "dd", &job, &userServerId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x15e2,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x15e2,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Periodic item reply cannot be applied: user object=%u is unavailable, job=%d",
            static_cast<unsigned>(userServerId), job
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (job == 0) {
        PeriodicItemJobHandler::OnLoadInfo(user, cursor);
    } else if (job == 2) {
        PeriodicItemJobHandler::OnExpired(user, cursor);
    }
    return false;
}

namespace DBPacketExHandler {

bool ReplyPeriodicShapeShiftedItemJob(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067c014, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "dd", &result, &userServerId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2539,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2539,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Periodic item appearance reply cannot be applied: user object=%u is unavailable, operation=%d",
            static_cast<unsigned>(userServerId), result
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result == 0) {
        PeriodicShapeShiftedItemJobHandler::OnLoadInfo(user, cursor);
    }
    return false;
}

bool ReplyPeriodicHennaJob(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067c558, RP_EX 0xfc.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "dd", &result, &userServerId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2c7e,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2c7e,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Periodic henna reply cannot be applied: user object=%u is unavailable, operation=%d",
            static_cast<unsigned>(userServerId), result
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result == 0) {
        PeriodicHennaJobHandler::OnLoadInfo(user, cursor);
    } else if (result == 2) {
        PeriodicHennaJobHandler::OnExpired(user, cursor);
    } else if (result == 3) {
        PeriodicHennaJobHandler::OnSetPeriod(user, cursor);
    }
    return false;
}

bool ReplySetQuest(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006918bc, RP_EX 0xfd.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    User* const rawUser = user.get();

    // The current callback releases its CSPointer before touching the quest
    // lock through the saved raw pointer. Preserve that lifetime ordering.
    user.Release();
    if (rawUser != nullptr) {
        {
            L2_GUARD(rawUser->m_QuestSaveLock);
            rawUser->m_bWaitForSavingQuest = false;
        }
    }
    return false;
}

bool ReplyAccountStamp(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a235c, RP_EX 0xfe.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int8_t result = 0;
    std::int8_t sendMessage = 1;
    const unsigned char* cursor = ::Disassemble(packet, "dc", &characterId, &result);
    cursor = ::Disassemble(cursor, "c", &sendMessage);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user || result != 0) {
        return false;
    }

    CPacketHelperEx<20480> attendedStampPacket;
    std::int32_t currentStamp = 0;
    std::int32_t attendedCount = 0;
    std::int8_t attendedStamp = 0;

    CEvent_AccountEvent* const event =
        static_cast<CEvent_AccountEvent*>(Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_ACCOUNT_EVENT));
    if (event == nullptr) {
        return false;
    }

    cursor = ::Disassemble(cursor, "dd", &currentStamp, &attendedCount);

    const int maximumStampByte = static_cast<int>(static_cast<std::int8_t>(event->m_nMaxStamp));
    const int currentStampByte = static_cast<int>(static_cast<std::int8_t>(currentStamp));
    if (event->m_mapAttendReward.empty()) {
        user->GetSocket()->Send("chccc", 0xfe, 0x174, maximumStampByte, currentStampByte, 0);
        return false;
    }

    attendedStampPacket.Assemble("c", static_cast<int>(static_cast<std::int8_t>(event->m_mapAttendReward.size())));

    CPacketHelperEx<20480> claimableStampPacket;
    std::vector<int> attendedStamps;
    for (std::int32_t index = 0; index < attendedCount; ++index) {
        cursor = ::Disassemble(cursor, "c", &attendedStamp);
        attendedStamps.push_back(static_cast<int>(attendedStamp));
    }

    for (const auto& rewardEntry : event->m_mapAttendReward) {
        const int stamp = rewardEntry.first;
        attendedStampPacket.Assemble("c", static_cast<int>(static_cast<std::int8_t>(stamp)));

        if (std::find(attendedStamps.begin(), attendedStamps.end(), stamp) == attendedStamps.end() &&
            stamp <= currentStamp) {
            claimableStampPacket.Assemble("c", static_cast<int>(static_cast<std::int8_t>(stamp)));
        }
    }

    user->GetSocket()->Send(
        "chccbcb", 0xfe, 0x174, maximumStampByte, currentStampByte, attendedStampPacket.GetLength(),
        attendedStampPacket.GetPacket(), claimableStampPacket.GetAssembledCount(), claimableStampPacket.GetLength(),
        claimableStampPacket.GetPacket()
    );

    if (sendMessage != 0) {
        event->SetSendMsg(user->GetAccountID(), false);
    }
    return false;
}

bool ResponseBrCanBuyShopItem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068a4dc, CacheD RP_EX 0xb9.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t productId = 0;
    std::int32_t productAmount = 0;
    std::int32_t characterId = 0;
    std::int8_t gift = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddddc", &result, &productId, &productAmount, &characterId, &gift);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    if (result != BR_BUY_SUCCESS) {
        user->GetSocket()->Send("chd", 0xfe, 0x11a, result);
        return false;
    }

    std::wstring receiverName;
    std::wstring title;
    std::wstring content;
    const bool isGift = gift == 1;
    if (isGift) {
        std::array<char16_t, 25> receiverNamePacket{};
        std::array<char16_t, 61> titlePacket{};
        std::array<char16_t, 1001> contentPacket{};
        ::Disassemble(cursor, "SSS", 0x32, receiverNamePacket.data(), 0x7a, titlePacket.data(), 0x7d2, contentPacket.data());
        receiverName = Utf16ToWide(receiverNamePacket.data());
        title = Utf16ToWide(titlePacket.data());
        content = Utf16ToWide(contentPacket.data());
    }

    user->RequestBuyProduct(productId, productAmount, isGift, receiverName.data(), title.data(), content.data());
    return false;
}

bool ResponseBrSendShopGiftMail(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068a770, CacheD RP_EX 0xba.
    L2_GUARD_FUNCTION();

    std::int64_t purchaseNumber = 0;
    std::int32_t productId = 0;
    std::int32_t productAmount = 0;
    std::int32_t soldCount = 0;
    std::int32_t result = 0;
    std::int32_t postId = 0;
    std::int32_t senderId = 0;
    std::int32_t receiverId = 0;
    std::int32_t trade = 0;
    std::int32_t returnable = 0;
    std::int32_t senderType = 0;
    std::int32_t itemCount = 0;
    ::Disassemble(
        packet, "Qddddddddddd", &purchaseNumber, &productId, &productAmount, &soldCount, &result, &postId, &senderId,
        &receiverId, &trade, &returnable, &senderType, &itemCount
    );

    if (result == BR_BUY_SUCCESS) {
        g_PremiumSvc.RequestDeleteItem(purchaseNumber);

        CSPointer<User> receiver =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), receiverId));
        unsigned char userMask = receiver ? 2 : 0;

        CSPointer<User> sender =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), senderId));
        if (!sender) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user char DB Id[%d]",
            //     L"DBPacketEx.cpp",
            //     0x1fd3,
            //     senderId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid user char DB Id[%d]",
            //     L"DBPacketEx.cpp",
            //     0x1fd3,
            //     senderId)
            L2_LOG_ERROR(
                Log,
                "Shop gift mail succeeded but sender notification cannot be delivered: sender character ID=%d, receiver "
                "character ID=%d, mail ID=%d, purchase=%lld",
                senderId, receiverId, postId, static_cast<long long>(purchaseNumber)
            );
        } else {
            userMask |= 1;
            sender->EndBuyProduct();
            sender->GetSocket()->Send("chd", 0xfe, 0x11a, result);
        }

        PostUtil::MakeNewPost(
            postId, senderId, receiverId, trade, returnable != 0, Post::ProcessWaiting, runtime::crt::Time32Now(),
            itemCount > 0, senderType, userMask, false, 0
        );

        CProductSP product = g_ShopServer.GetProduct(productId);
        if (product) {
            product->SetSoldCount(soldCount);
        }
        return false;
    }

    CSPointer<User> sender =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), senderId));
    if (sender) {
        sender->EndBuyProduct();
        sender->GetSocket()->Send("chd", 0xfe, 0x11a, result);
    }
    return false;
}

bool ResponseLoadMenteeListEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068ac68, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t menteeCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &characterId, &menteeCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        for (std::int32_t index = 0; index < menteeCount; ++index) {
            std::int32_t menteeDatabaseId = 0;
            std::array<char16_t, 25> menteeName{};
            std::int32_t menteeClass = 0;
            std::int32_t menteeLevel = 0;
            cursor = ::Disassemble(cursor, "dSdd", &menteeDatabaseId, 0x32, menteeName.data(), &menteeClass, &menteeLevel);
            user->AddMentee(menteeDatabaseId, menteeName.data(), menteeClass, menteeLevel);
        }
    }
    return false;
}

bool ResponseLoadMentorEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068b3fc, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t mentorDatabaseId = 0;
    std::array<char16_t, 25> mentorName{};
    std::int32_t mentorClass = 0;
    std::int32_t mentorLevel = 0;
    ::Disassemble(packet, "ddSdd", &characterId, &mentorDatabaseId, 0x32, mentorName.data(), &mentorClass, &mentorLevel);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->SetMyMentor(mentorDatabaseId, mentorName.data(), mentorClass, mentorLevel);
    }
    return false;
}

bool ResponseLoadMentorPenaltytEx(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t endType = 0;
    std::int32_t baseTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &endType, &baseTime);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        const std::int32_t duration = endType < 4 ? (g_nMentorCancelPanelty != 0 ? g_nMentorCancelPanelty : 0x2a300)
                                                  : (g_nMentorGraduatePanelty != 0 ? g_nMentorGraduatePanelty : 0x15180);
        user->SetMentorPenaltyEndTime(runtime::AddInt32Wrapping(baseTime, duration));
    }
    return false;
}

bool ResponseAddNewMenteeEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068ae50, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t mentorDatabaseId = 0;
    std::int32_t menteeDatabaseId = 0;
    std::int32_t menteeCount = 0;
    ::Disassemble(packet, "ddd", &mentorDatabaseId, &menteeDatabaseId, &menteeCount);

    CSPointer<User> mentor =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), mentorDatabaseId));
    CSPointer<User> mentee =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), menteeDatabaseId));
    if (mentor && mentee) {
        if (menteeCount < 3) {
            mentor->ReplyAddMentee(mentee.get());
        } else {
            mentor->GetSocket()->SendSystemMessage(SystemMessageId::MENTOR_MENTEE_LIMIT);
            mentor->SendMentorList();
        }
    }
    return false;
}

bool ResponseUdateMentorStatusEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068b0d4, original owner DBPacketEx.cpp. The
    // shipping symbol intentionally spells "Udate" without the first p.
    L2_GUARD_FUNCTION();

    std::int32_t firstDatabaseId = 0;
    std::int32_t secondDatabaseId = 0;
    std::int32_t endType = 0;
    ::Disassemble(packet, "ddd", &firstDatabaseId, &secondDatabaseId, &endType);

    if (endType == 2) {
        CSPointer<User> user =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), firstDatabaseId));
        if (user) {
            user->ReplyEndMentoring(secondDatabaseId, endType);
        }
    } else if (endType == 3 || endType == 4) {
        CSPointer<User> user =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), secondDatabaseId));
        if (user) {
            user->ReplyEndMentoring(firstDatabaseId, endType);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"FILE[%s] LINE[%d] Cannot find mentee dbid[%d] "
            //     L"endtype[%d] worldid[%d]",
            //     L"DBPacketEx.cpp",
            //     0x2070,
            //     secondDatabaseId,
            //     endType,
            //     runtime::process::CurrentDatabaseId())
            // L2_LOG_ERROR(Log,
            //     L"FILE[%s] LINE[%d] Cannot find mentee dbid[%d] "
            //     L"endtype[%d] worldid[%d]",
            //     L"DBPacketEx.cpp",
            //     0x2070,
            //     secondDatabaseId,
            //     endType,
            //     runtime::process::CurrentDatabaseId())
            L2_LOG_ERROR(
                Log,
                "Mentoring end reply cannot be applied: mentee character ID=%d is unavailable, other character ID=%d, end "
                "type=%d, database=%d",
                secondDatabaseId, firstDatabaseId, endType, runtime::process::CurrentDatabaseId()
            );
        }
    }
    return false;
}

bool ResponseBeautyLoad(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006760c8, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();
    CBeautyManager::Inst()->LoadData(packet);
    return false;
}

bool TradeDivideAdena(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068e648, RP_EX 0xf1.
    L2_GUARD_FUNCTION();

    std::int32_t sourceCharacterId = 0;
    std::int32_t targetCharacterId = 0;
    packet = ::Disassemble(packet, "dd", &sourceCharacterId, &targetCharacterId);
    CSPointer<User> source =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), sourceCharacterId));
    CSPointer<User> target =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), targetCharacterId));
    if (!source || !target) {
        return false;
    }

    std::int32_t result = 0;
    packet = ::Disassemble(packet, "d", &result);
    if (result == 0) {
        source->GetSocket()->SendSystemMessage_S(SystemMessageId::ADENA_NOT_DISTRIBUTED_TO_CHARACTER, target->GetName());
        target->GetSocket()->SendSystemMessage(SystemMessageId::ADENA_DISTRIBUTION_NOT_RECEIVED);
        runtime::raw_atomic::ExchangeI32(&source->m_nWaitGetItemOnce, 0);
        target->SendItemList(true, true, nullptr);
        source->SendItemList(true, true, nullptr);
        return false;
    }

    std::int32_t sourceTradeType = 0;
    std::int32_t sourceItemDatabaseId = 0;
    std::int32_t sourceItemServerId = 0;
    std::int64_t sourceAmount = 0;
    std::int32_t targetTradeType = 0;
    std::int32_t targetItemDatabaseId = 0;
    std::int32_t targetItemServerId = 0;
    std::int64_t targetAmount = 0;
    std::int32_t requestValue = 0;
    std::int64_t tradeAmount = 0;
    std::int8_t requestFlag = 0;

    CItem* sourceItem = nullptr;
    enum class DistributionOutcome { SourceRejected, SourceItemMissing, TargetItemMissing, TargetCreationFailed, Completed };
    const DistributionOutcome outcome = [&]() {
        L2_WRITE_GUARD(source->m_inv);
        L2_WRITE_GUARD(target->m_inv);
        ::Disassemble(
            packet, "dddQdddQdQc", &sourceTradeType, &sourceItemDatabaseId, &sourceItemServerId, &sourceAmount,
            &targetTradeType, &targetItemDatabaseId, &targetItemServerId, &targetAmount, &requestValue, &tradeAmount,
            &requestFlag
        );
        static_cast<void>(sourceItemServerId);
        static_cast<void>(targetItemServerId);

        if (sourceTradeType == 0) {
            return DistributionOutcome::SourceRejected;
        }

        sourceItem = source->m_inv.GetByDBID(sourceItemDatabaseId);
        if (targetTradeType == 1 || targetTradeType == 2) {
            if (sourceItem == nullptr) {
                return DistributionOutcome::SourceItemMissing;
            }

            if (!sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceItem->m_data->m_nCount, sourceAmount))) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "DBPacketEx.cpp",
                //     targetTradeType == 1 ? 0x2a23 : 0x2a47,
                //     sourceItem->m_data->m_nCount,
                //     sourceAmount)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "DBPacketEx.cpp",
                //     targetTradeType == 1 ? 0x2a23 : 0x2a47,
                //     sourceItem->m_data->m_nCount,
                //     sourceAmount)
                L2_LOG_ERROR(
                    Log,
                    "Adena distribution could not decrease source item amount; processing continues: item database ID=%d, "
                    "current=%lld, decrease=%lld, source character ID=%d",
                    sourceItemDatabaseId, static_cast<long long>(sourceItem->m_data->m_nCount),
                    static_cast<long long>(sourceAmount), sourceCharacterId
                );
            }
            source->m_inv.SetInventoryChanged(sourceItem, IUT_UPDATE);
        }

        if (targetTradeType == 1) {
            CItem* const targetItem = target->m_inv.GetByDBID(targetItemDatabaseId);
            if (targetItem == nullptr) {
                return DistributionOutcome::TargetItemMissing;
            }
            if (!targetItem->SetAmount(runtime::AddInt64Wrapping(targetItem->m_data->m_nCount, targetAmount))) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "DBPacketEx.cpp",
                //     0x2a34,
                //     targetItem->m_data->m_nCount,
                //     targetAmount)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "DBPacketEx.cpp",
                //     0x2a34,
                //     targetItem->m_data->m_nCount,
                //     targetAmount)
                L2_LOG_ERROR(
                    Log,
                    "Adena distribution could not increase target item amount; processing continues: item database ID=%d, "
                    "current=%lld, increase=%lld, target character ID=%d",
                    targetItemDatabaseId, static_cast<long long>(targetItem->m_data->m_nCount),
                    static_cast<long long>(targetAmount), targetCharacterId
                );
            }
            target->m_inv.SetInventoryChanged(targetItem, IUT_UPDATE);
        } else if (targetTradeType == 2) {
            CItem* const targetItem = g_ObjectDB.CreateItem(sourceItem->m_data->m_nClassID);
            if (targetItem == nullptr) {
                return DistributionOutcome::TargetCreationFailed;
            }
            targetItem->SetDBID(static_cast<unsigned int>(targetItemDatabaseId));
            if (!targetItem->SetAmount(targetAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "DBPacketEx.cpp",
                //     0x2a57,
                //     targetItem->m_data->m_nCount,
                //     targetAmount)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "DBPacketEx.cpp",
                //     0x2a57,
                //     targetItem->m_data->m_nCount,
                //     targetAmount)
                L2_LOG_ERROR(
                    Log,
                    "Adena distribution could not set new target item amount; processing continues: item database ID=%d, "
                    "current=%lld, requested=%lld, target character ID=%d",
                    targetItemDatabaseId, static_cast<long long>(targetItem->m_data->m_nCount),
                    static_cast<long long>(targetAmount), targetCharacterId
                );
            }
            if (!target->m_inv.Push(static_cast<int>(targetItem->m_nId))) {
                // Log.Add(
                //     LOG_ERROR,
                //     "inven push failed at file[%s], line[%d]",
                //     "DBPacketEx.cpp",
                //     0x2a5c)
                // L2_LOG_ERROR(Log,
                //     "inven push failed at file[%s], line[%d]",
                //     "DBPacketEx.cpp",
                //     0x2a5c)
                L2_LOG_ERROR(
                    Log,
                    "Adena distribution could not insert target item into inventory; processing continues: item database "
                    "ID=%d, target character ID=%d",
                    targetItemDatabaseId, targetCharacterId
                );
            }
            target->m_inv.SetInventoryChanged(targetItem, IUT_ADD);
        }

        const std::int64_t totalAmount = runtime::MultiplyInt64Wrapping(requestValue, tradeAmount);
        target->GetSocket()->Send(
            "chccdQQS", 0xfe, 0x15d, 0, static_cast<int>(requestFlag), requestValue, tradeAmount, totalAmount,
            source->GetName()
        );

        return DistributionOutcome::Completed;
    }();

    switch (outcome) {
        case DistributionOutcome::SourceRejected:
            runtime::raw_atomic::ExchangeI32(&source->m_nWaitGetItemOnce, 0);
            source->GetSocket()->SendSystemMessage_S(SystemMessageId::ADENA_NOT_DISTRIBUTED_TO_CHARACTER, target->GetName());
            target->GetSocket()->SendSystemMessage(SystemMessageId::ADENA_DISTRIBUTION_NOT_RECEIVED);
            return false;
        case DistributionOutcome::SourceItemMissing:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Cannot find item from inv. dbid[%d]",
            //     "DBPacketEx.cpp",
            //     targetTradeType == 1 ? 0x2a1d : 0x2a41,
            //     sourceItemDatabaseId)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]Cannot find item from inv. dbid[%d]",
            //     "DBPacketEx.cpp",
            //     targetTradeType == 1 ? 0x2a1d : 0x2a41,
            //     sourceItemDatabaseId)
            L2_LOG_ERROR(
                Log,
                "Adena distribution reply stopped: source inventory item database ID=%d is unavailable, source character "
                "ID=%d, target character ID=%d",
                sourceItemDatabaseId, sourceCharacterId, targetCharacterId
            );
            runtime::raw_atomic::ExchangeI32(&source->m_nWaitGetItemOnce, 0);
            return false;
        case DistributionOutcome::TargetItemMissing:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Cannot find item from inv. dbid[%d]",
            //     "DBPacketEx.cpp",
            //     0x2a2e,
            //     targetItemDatabaseId)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]Cannot find item from inv. dbid[%d]",
            //     "DBPacketEx.cpp",
            //     0x2a2e,
            //     targetItemDatabaseId)
            L2_LOG_ERROR(
                Log,
                "Adena distribution reply stopped: target inventory item database ID=%d is unavailable, source character "
                "ID=%d, target character ID=%d",
                targetItemDatabaseId, sourceCharacterId, targetCharacterId
            );
            return false;
        case DistributionOutcome::TargetCreationFailed:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Cannot create item dbid[%d]",
            //     "DBPacketEx.cpp",
            //     0x2a50,
            //     sourceItem->m_data->m_nClassID)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]Cannot create item dbid[%d]",
            //     "DBPacketEx.cpp",
            //     0x2a50,
            //     sourceItem->m_data->m_nClassID)
            L2_LOG_ERROR(
                Log,
                "Adena distribution reply stopped: target item creation failed, item class=%d, item database ID=%d, target "
                "character ID=%d",
                sourceItem->m_data->m_nClassID, targetItemDatabaseId, targetCharacterId
            );
            runtime::raw_atomic::ExchangeI32(&source->m_nWaitGetItemOnce, 0);
            return false;
        case DistributionOutcome::Completed:
            break;
    }
    runtime::raw_atomic::ExchangeI32(&source->m_nWaitGetItemOnce, 0);
    target->SendItemList(true, true, nullptr);
    source->SendItemList(true, true, nullptr);
    return false;
}

bool ResponseBeautyPurchaseList(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068f64c, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int8_t requestFlag = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dc", &characterId, &requestFlag);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    std::int32_t count = 0;
    cursor = ::Disassemble(cursor, "d", &count);
    for (std::int32_t index = 0; index < count; ++index) {
        std::int32_t type = 0;
        std::int32_t primary = 0;
        std::int32_t secondary = 0;
        cursor = ::Disassemble(cursor, "ddd", &type, &primary, &secondary);
        if (secondary != -1) {
            user->AddBeautyPurchaseData(type, primary, secondary);
        }
    }

    runtime::raw_atomic::ExchangeI32(&user->m_BeautyPurchase.m_nLoaded, 1);
    CBeautyManager::Inst()->GetDataList(user->GetServerId(), static_cast<Beauty::BeautyState>(requestFlag));
    return false;
}

bool ReplyAcquirePotentialSkill(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068f8ec, RP_EX 0xf3.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t result = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &characterId, &result);
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"User is not valid userdbid : %d",
        //     characterId)
        // L2_LOG_ERROR(Log,
        //     L"User is not valid userdbid : %d",
        //     characterId)
        L2_LOG_ERROR(
            Log, "Potential skill acquisition reply cannot be applied: character ID=%d is unavailable, result=%d",
            characterId, result
        );
        return false;
    }

    if (result != 0) {
        std::int32_t ignoredTotalCount = 0;
        cursor = ::Disassemble(cursor, "d", &ignoredTotalCount);
        user->ResetAbilitySkillList();

        for (std::int32_t category = 1; category < 4; ++category) {
            std::int32_t skillCount = 0;
            cursor = ::Disassemble(cursor, "d", &skillCount);
            std::int32_t acquiredLevelCount = 0;
            for (std::int32_t index = 0; index < skillCount; ++index) {
                std::int32_t skillId = 0;
                std::int32_t skillLevel = 0;
                cursor = ::Disassemble(cursor, "dd", &skillId, &skillLevel);
                user->AcquireSkill(skillId, skillLevel, 0, false, true);
                user->SetAbilitySkillList(skillId, skillLevel);
                acquiredLevelCount = runtime::AddInt32Wrapping(acquiredLevelCount, skillLevel);
            }

            if (category == 1) {
                user->m_data->m_stPotentialCount.nCount1 = acquiredLevelCount;
            } else if (category == 2) {
                user->m_data->m_stPotentialCount.nCount2 = acquiredLevelCount;
            } else {
                user->m_data->m_stPotentialCount.nCount3 = acquiredLevelCount;
            }
        }
        user->m_data->m_NeedUpdatePacketBitSet.SetBit(100);
    }

    user->SendAbilitySkillList(result);
    runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
    user->SetMaxMagicLevel();
    user->ValidateChanged(VB_MAX);
    user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR});
    return false;
}

bool AcquiredAbilitySkillList(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067c350, RP_EX 0xf4.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t potentialCount1 = 0;
    std::int32_t potentialCount2 = 0;
    std::int32_t potentialCount3 = 0;
    std::int32_t skillCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "ddddd", &userServerId, &potentialCount1, &potentialCount2, &potentialCount3, &skillCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2af9,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x2af9,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Ability skill list reply cannot be applied: user object=%u is unavailable, reported skills=%d",
            static_cast<unsigned>(userServerId), skillCount
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    user->ResetAbilitySkillList();
    for (std::int32_t index = 0; index < skillCount; ++index) {
        std::int32_t skillId = 0;
        std::int32_t skillLevel = 0;
        cursor = ::Disassemble(cursor, "dd", &skillId, &skillLevel);
        user->SetAbilitySkillList(skillId, skillLevel);
    }
    user->m_data->m_stPotentialCount.nCount1 = potentialCount1;
    user->m_data->m_stPotentialCount.nCount2 = potentialCount2;
    user->m_data->m_stPotentialCount.nCount3 = potentialCount3;
    user->m_data->m_NeedUpdatePacketBitSet.SetBit(100);
    user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR});
    user->SendAbilitySkillList(1);
    return false;
}

bool ResetAbilitySkill(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068fd60, RP_EX 0xf5.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t result = 0;
    std::int64_t resetSp = 0;
    ::Disassemble(packet, "ddQ", &characterId, &result, &resetSp);

    CSPointer<User> userPointer =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    User* const user = userPointer.get();
    userPointer.Release();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]ResetAbilitySkill Invailed User",
        //     L"DBPacketEx.cpp",
        //     0x2b28)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]ResetAbilitySkill Invailed User",
        //     L"DBPacketEx.cpp",
        //     0x2b28)
        L2_LOG_ERROR(
            Log, "Ability skill reset reply cannot be applied: character ID=%d is unavailable, result=%d", characterId,
            result
        );
        return false;
    }

    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]ResetAbilitySkill failed[%d]",
        //     L"DBPacketEx.cpp",
        //     0x2b39,
        //     user->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]ResetAbilitySkill failed[%d]",
        //     L"DBPacketEx.cpp",
        //     0x2b39,
        //     user->GetDBID())
        L2_LOG_ERROR(Log, "Ability skill reset failed in CacheD: character ID=%d, result=%d", user->GetDBID(), result);
        if (CPotentialSkillDB::Inst().m_nResetSP != 0) {
            {
                L2_GUARD(*user->m_lock);
                user->SPInc(CPotentialSkillDB::Inst().m_nResetSP);
            }
        }
    } else {
        const std::int32_t totalAcquired = runtime::AddInt32Wrapping(
            runtime::AddInt32Wrapping(user->m_data->m_stPotentialCount.nCount1, user->m_data->m_stPotentialCount.nCount2),
            user->m_data->m_stPotentialCount.nCount3
        );
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x1fb2, user, nullptr, nullptr, nullptr, nullptr, totalAcquired, user->m_data->GetAP(), resetSp, 0, 0, 0, 0
        );
        user->ResetAbilitySkillList();
        user->m_data->m_stPotentialCount.nCount2 = 0;
        user->m_data->m_stPotentialCount.nCount3 = 0;
        user->m_data->m_stPotentialCount.nCount1 = 0;
        user->m_data->m_NeedUpdatePacketBitSet.SetBit(100);
        user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR});
    }

    user->SendAbilitySkillList(result);
    user->AcquireAutoGetSkills(1);
    user->AcquireExtraSkills();
    user->ValidateChanged(
        VB_BASEDATA | VB_BASEATTR | VB_WEAPONARMOR | VB_SKILLLIST | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED
    );
    runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
    return false;
}

bool Reply_RP_EX_CHANGE_AP(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0069003c, RP_EX 0xf6.
    L2_GUARD_FUNCTION();

    protocol::RP_EX_CHANGE_AP reply;
    if (!DecodePacket(reply, packet)) {
        return false;
    }

    CSPointer<User> userPointer =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), reply.characterId));
    User* const user = userPointer.get();
    userPointer.Release();
    if (user == nullptr) {
        return false;
    }

    if (reply.saved) {
        {
            L2_GUARD(*user->m_lock);
            user->m_data->SetAP(reply.abilityPoints, false);
        }
    }
    user->m_data->m_NeedUpdatePacketBitSet.SetBit(100);
    user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR});
    user->SendAbilitySkillList(1);
    runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
    return false;
}

bool ReplyStartTrainingZone(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006901d0, RP_EX 0xf8.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t result = 0;
    std::int32_t trainingMinutes = 0;
    ::Disassemble(packet, "ddd", &characterId, &result, &trainingMinutes);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    if (result < 1) {
        if (result == 0) {
            user->SendSystemMessage(SystemMessageId::DAILY_TRAINING_COMPLETED);
        } else if (result == -1) {
            user->SendSystemMessage(SystemMessageId::ANOTHER_CHARACTER_ON_ACCOUNT_ALREADY_IN_TRAINING_CAMP);
        }
        return false;
    }

    user->StopMove();
    user->m_data->m_TrainingState = 1;
    const FVector position = CTrainingZoneSetting::Inst().GetTrainingZonePos();
    static_cast<void>(user->TeleportToLocation(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), 0, 0x10000, TeleportSourceType::TrainingZone, nullptr, 0
    ));

    const std::int32_t trainingSeconds = runtime::MultiplyInt32Wrapping(trainingMinutes, 60);
    user->m_TrainingEndTime = runtime::AddInt32Wrapping(trainingSeconds, result);
    const double experiencePerMinute = ExpByLevelTraining[user->m_data->m_nLevel];
    user->GetSocket()->Send("chddff", 0xfe, 0x162, 0, trainingSeconds, experiencePerMinute, experiencePerMinute / 250.0);
    return false;
}

bool ReplyEndTrainingZone(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00690504, RP_EX 0xf9.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int32_t trainingTime = 0;
    std::int32_t suppressCompleteHtml = 0;
    std::int32_t suppressReplyPacket = 0;
    ::Disassemble(packet, "ddddddd", &characterId, &x, &y, &z, &trainingTime, &suppressCompleteHtml, &suppressReplyPacket);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    if (user->m_AreaMod.m_bIsInTrainingZone) {
        user->StopMove();
        static_cast<void>(user->TeleportToLocation(x, y, z, 0, 0x10000, TeleportSourceType::TrainingZone, nullptr, 0));
    }

    if (trainingTime < 1) {
        user->m_data->m_TrainingState = 0;
        user->GetSocket()->SendSystemMessage(SystemMessageId::TRAINING_REWARD_REQUIRES_AT_LEAST_ONE_MINUTE);
    } else {
        user->m_data->m_TrainingState = 2;
        user->m_data->m_TrainingTime = trainingTime;
        user->m_data->m_TrainingLevel = user->m_data->m_nLevel;
        user->m_data->m_TrainingExp = runtime::Win64CvttDoubleToInt64(
            static_cast<double>(trainingTime) * ExpByLevelTraining[user->m_data->m_nLevel]
        );
    }

    if (suppressReplyPacket == 0) {
        user->GetSocket()->Send("ch", 0xfe, 0x163);
    }
    if (suppressCompleteHtml == 0) {
        const std::u16string completeHtml(CTrainingZoneSetting::Inst().GetCompleteHTML());
        user->ShowHTML(completeHtml.c_str());
    }
    return false;
}

bool ReplyRewardTrainingZone(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006909b0, RP_EX 0xfa.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &characterId, &result);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    if (result != 1) {
        user->SendSystemMessage(SystemMessageId::TRAINING_REWARD_REQUIRES_ORIGINAL_ENTRY_CLASS);
        return false;
    }

    const std::int64_t experience = user->m_data->m_TrainingExp;
    const std::int64_t sp = experience / 250;
    user->m_data->m_TrainingState = 0;
    user->SendSystemMessage(SystemMessageId::CALCULATING_TRAINING_XP_AND_SP);
    static_cast<void>(user->ExpInc(experience, true));
    static_cast<void>(user->SPInc(sp));
    user->GetSocket()->Send("chccQcQ", 0x62, SystemMessageId::GAINED_EXPERIENCE_AND_SP, 2, 6, experience, 6, sp);
    user->m_data->m_TrainingExp = 0;

    const std::int64_t logX = runtime::Win64CvttDoubleToInt64(user->GetPos().x);
    const char16_t* const actorName = user->GetName();
    const std::int64_t trainingTime = user->m_data->m_TrainingTime;
    const std::int64_t trainingLevel = user->m_data->m_TrainingLevel;
    const std::int64_t level = user->m_data->m_nLevel;
    const std::int64_t characterClass = static_cast<int>(user->m_data->m_nClass);
    const std::int64_t sex = static_cast<int>(user->m_data->m_Sex);
    const std::int64_t race = static_cast<int>(user->m_data->m_Race);
    const std::int64_t logZ = runtime::Win64CvttDoubleToInt64(user->GetPos().z);
    const std::int64_t logY = runtime::Win64CvttDoubleToInt64(user->GetPos().y);
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t userDbId = user->GetDBID();
    std::u16string actorNameStorage;
    const char16_t* const actorNameUtf16 = (actorNameStorage = actorName).c_str();

    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fb9, userDbId, accountId, 0, 0, logX, logY, logZ, nullptr, nullptr, nullptr, race, sex, characterClass, level,
        experience, sp, trainingLevel, trainingTime, 0, 0, actorNameUtf16, user->m_sAccountName.data(), nullptr, nullptr, 0
    );
    return false;
}

bool ReplyLoadTrainingZone(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00691104, RP_EX 0xfb.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t state = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "dd", &characterId, &state);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    if (state == 0) {
        if (user->m_AreaMod.m_bIsInTrainingZone) {
            const std::int32_t logX = TrainingZoneLogDwordFromDouble(user->GetPos().x);
            const std::int32_t logZ = TrainingZoneLogDwordFromDouble(user->GetPos().z);
            const std::int32_t logY = TrainingZoneLogDwordFromDouble(user->GetPos().y);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[TrainingCamp] Invalid Pos - WAITING. charID[%d] [%d,%d,%d]",
            //     characterId,
            //     logX,
            //     logY,
            //     logZ)
            // L2_LOG_ERROR(Log,
            //     L"[TrainingCamp] Invalid Pos - WAITING. charID[%d] [%d,%d,%d]",
            //     characterId,
            //     logX,
            //     logY,
            //     logZ)
            L2_LOG_ERROR(
                Log,
                "Training state mismatch: waiting character is inside the training zone; village restart requested, "
                "character ID=%d, position=(%d,%d,%d)",
                characterId, logX, logY, logZ
            );
            static_cast<void>(user->Restart(RESTART_VILLAGE, 0));
        }
        user->m_data->m_TrainingState = 0;
    } else if (state == 1) {
        if (!user->m_AreaMod.m_bIsInTrainingZone) {
            const std::int32_t logX = TrainingZoneLogDwordFromDouble(user->GetPos().x);
            const std::int32_t logZ = TrainingZoneLogDwordFromDouble(user->GetPos().z);
            const std::int32_t logY = TrainingZoneLogDwordFromDouble(user->GetPos().y);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[TrainingCamp] Invalid Pos - TRAINING. charID[%d] [%d,%d,%d]",
            //     characterId,
            //     logX,
            //     logY,
            //     logZ)
            // L2_LOG_ERROR(Log,
            //     L"[TrainingCamp] Invalid Pos - TRAINING. charID[%d] [%d,%d,%d]",
            //     characterId,
            //     logX,
            //     logY,
            //     logZ)
            L2_LOG_ERROR(
                Log,
                "Training state mismatch: training character is outside the training zone; teleport requested, character "
                "ID=%d, position=(%d,%d,%d)",
                characterId, logX, logY, logZ
            );
            const FVector position = CTrainingZoneSetting::Inst().GetTrainingZonePos();
            static_cast<void>(user->TeleportToLocation(
                runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                runtime::Win64CvttDoubleToInt32(position.z), 0, 0x10000, TeleportSourceType::TrainingZone, nullptr, 0
            ));
        }

        std::int32_t trainingStartTime = 0;
        std::int32_t trainingMinutes = 0;
        ::Disassemble(cursor, "dd", &trainingStartTime, &trainingMinutes);
        const std::int32_t elapsed = runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), trainingStartTime);
        const std::int32_t trainingSeconds = runtime::MultiplyInt32Wrapping(trainingMinutes, 60);
        user->m_data->m_TrainingState = 1;
        user->m_TrainingEndTime = runtime::AddInt32Wrapping(trainingSeconds, trainingStartTime);
        const double experiencePerMinute = ExpByLevelTraining[user->m_data->m_nLevel];
        user->GetSocket()->Send(
            "chddff", 0xfe, 0x162, elapsed / 60, runtime::SubtractInt32Wrapping(trainingSeconds, elapsed),
            experiencePerMinute, experiencePerMinute / 250.0
        );
    } else if (state == 2) {
        if (user->m_AreaMod.m_bIsInTrainingZone) {
            const std::int32_t logX = TrainingZoneLogDwordFromDouble(user->GetPos().x);
            const std::int32_t logZ = TrainingZoneLogDwordFromDouble(user->GetPos().z);
            const std::int32_t logY = TrainingZoneLogDwordFromDouble(user->GetPos().y);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[TrainingCamp] Invalid Pos - CALCULATING. charID[%d] [%d,%d,%d]",
            //     characterId,
            //     logX,
            //     logY,
            //     logZ)
            // L2_LOG_ERROR(Log,
            //     L"[TrainingCamp] Invalid Pos - CALCULATING. charID[%d] [%d,%d,%d]",
            //     characterId,
            //     logX,
            //     logY,
            //     logZ)
            L2_LOG_ERROR(
                Log,
                "Training state mismatch: calculating character is inside the training zone; village restart requested, "
                "character ID=%d, position=(%d,%d,%d)",
                characterId, logX, logY, logZ
            );
            static_cast<void>(user->Restart(RESTART_VILLAGE, 0));
        }

        std::int32_t trainingTime = 0;
        std::int32_t trainingLevel = 0;
        ::Disassemble(cursor, "dd", &trainingTime, &trainingLevel);
        user->m_data->m_TrainingState = 2;
        user->m_data->m_TrainingTime = trainingTime;
        user->m_data->m_TrainingLevel = trainingLevel;
        user->m_data->m_TrainingExp =
            runtime::Win64CvttDoubleToInt64(static_cast<double>(trainingTime) * ExpByLevelTraining[trainingLevel]);
    } else if (state == 100 || state == 200) {
        char16_t systemContext[] = u"SYS";
        user->GetSocket()->SendSystemMessage(
            systemContext,
            state == 100 ? u"you or your another character is in training" : u"training time of today changed."
        );
    }
    return false;
}

bool ResponseBuyBeautyItem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068bbdc, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &result, &characterId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user || user->m_BeautyPurchase.m_nLoaded != 1) {
        return false;
    }

    unsigned int messageId = 0;
    if (result == 0) {
        const int hairShape = user->m_data->m_nHairShapeEx != 0 ? user->m_data->m_nHairShapeEx : user->m_data->m_nHairShape;
        const int hairColor = user->m_data->m_nHairColorEx != 0 ? user->m_data->m_nHairColorEx : user->m_data->m_nHairColor;
        const int face = user->m_data->m_nFaceEx != 0 ? user->m_data->m_nFaceEx : user->m_data->m_nFace;
        user->GetSocket()->Send(
            "chQQddddd", 0xfe, 0x136, user->m_inv.GetAdenaAmount(), user->m_inv.GetItemAmount(0x8dd4, 0, true), 0,
            // 0,
            Beautyshop::BSRT_FAIL, face, hairColor, hairShape
        );
        messageId = SystemMessageId::STYLE_CHANGE_FAILED;
    } else {
        std::int32_t face = 0;
        std::int32_t hairShape = 0;
        std::int32_t hairColor = 0;
        std::int64_t adenaCost = 0;
        std::int64_t beautyCoinCost = 0;
        std::int32_t count = 0;
        cursor = ::Disassemble(cursor, "dddQQd", &face, &hairShape, &hairColor, &adenaCost, &beautyCoinCost, &count);
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nFaceEx, face, 0x15);
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nHairShapeEx, hairShape, 0x16);
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nHairColorEx, hairColor, 0x17);
        user->DeleteBeautyItem(adenaCost, beautyCoinCost);

        for (std::int32_t index = 0; index < count; ++index) {
            std::int32_t state = Beauty::STATEMAX;
            std::int32_t secondary = -1;
            std::int32_t primary = 0;
            cursor = ::Disassemble(cursor, "ddd", &state, &secondary, &primary);
            CBeautyManager::Inst()->IncrementBeautyItem(static_cast<Beauty::BeautyState>(state), secondary, primary);
            user->AddBeautyPurchaseData(state, primary, secondary);
        }

        user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_BEAUTYSHOP});
        const int currentHairShape =
            user->m_data->m_nHairShapeEx != 0 ? user->m_data->m_nHairShapeEx : user->m_data->m_nHairShape;
        const int currentHairColor =
            user->m_data->m_nHairColorEx != 0 ? user->m_data->m_nHairColorEx : user->m_data->m_nHairColor;
        const int currentFace = user->m_data->m_nFaceEx != 0 ? user->m_data->m_nFaceEx : user->m_data->m_nFace;
        user->GetSocket()->Send(
            "chQQddddd", 0xfe, 0x136, user->m_inv.GetAdenaAmount(), user->m_inv.GetItemAmount(0x8dd4, 0, true), 0,
            // 1,
            Beautyshop::BSRT_SUCCESS, currentFace, currentHairColor, currentHairShape
        );
        messageId = SystemMessageId::STYLE_CHANGE_SUCCEEDED;
    }

    user->SendSystemMessage(messageId);
    CBeautyManager::Inst()->GetDataList(user->GetServerId(), Beauty::HAIR);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    return false;
}

bool ResponseResetBeautyItem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068c350, original owner DBPacketEx.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &result, &characterId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    unsigned int messageId = 0;
    if (result == 0) {
        const int hairShape = user->m_data->m_nHairShapeEx != 0 ? user->m_data->m_nHairShapeEx : user->m_data->m_nHairShape;
        const int hairColor = user->m_data->m_nHairColorEx != 0 ? user->m_data->m_nHairColorEx : user->m_data->m_nHairColor;
        const int face = user->m_data->m_nFaceEx != 0 ? user->m_data->m_nFaceEx : user->m_data->m_nFace;
        user->GetSocket()->Send(
            "chQQddddd", 0xfe, 0x136, user->m_inv.GetAdenaAmount(), user->m_inv.GetItemAmount(0x8dd4, 0, true), 1,
            // 0,
            Beautyshop::BSRT_FAIL, face, hairColor, hairShape
        );
        messageId = SystemMessageId::PREVIOUS_STYLE_RESTORATION_FAILED;
    } else {
        std::int32_t face = 0;
        std::int32_t hairShape = 0;
        std::int32_t hairColor = 0;
        std::int64_t adenaCost = 0;
        std::int64_t beautyCoinCost = 0;
        cursor = ::Disassemble(cursor, "dddQQ", &face, &hairShape, &hairColor, &adenaCost, &beautyCoinCost);
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nFaceEx, face, 0x15);
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nHairShapeEx, hairShape, 0x16);
        user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nHairColorEx, hairColor, 0x17);
        user->DeleteBeautyItem(adenaCost, beautyCoinCost);

        for (int index = 0; index < 3; ++index) {
            std::int32_t state = Beauty::STATEMAX;
            std::int32_t secondary = -1;
            std::int32_t primary = 0;
            cursor = ::Disassemble(cursor, "ddd", &state, &secondary, &primary);
        }

        user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_BEAUTYSHOP});
        const int currentHairShape =
            user->m_data->m_nHairShapeEx != 0 ? user->m_data->m_nHairShapeEx : user->m_data->m_nHairShape;
        const int currentHairColor =
            user->m_data->m_nHairColorEx != 0 ? user->m_data->m_nHairColorEx : user->m_data->m_nHairColor;
        const int currentFace = user->m_data->m_nFaceEx != 0 ? user->m_data->m_nFaceEx : user->m_data->m_nFace;
        user->GetSocket()->Send(
            "chQQddddd", 0xfe, 0x136, user->m_inv.GetAdenaAmount(), user->m_inv.GetItemAmount(0x8dd4, 0, true), 1,
            // 1,
            Beautyshop::BSRT_SUCCESS, currentFace, currentHairColor, currentHairShape
        );
        messageId = SystemMessageId::PREVIOUS_STYLE_RESTORED;
    }

    user->SendSystemMessage(messageId);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    return false;
}

bool ReplyLoadWolrdChatPoint(CDBSocket* socket, const unsigned char* packet) {
    // Current L2Server 0x00691a18, RP_EX 0xff.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t usedPoint = 0;
    ::Disassemble(packet, "dd", &characterId, &usedPoint);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    User* const rawUser = user.get();

    // The current callback drops the map-acquired hold before the atomic
    // point write and both socket lookups.
    user.Release();
    if (rawUser != nullptr) {
        rawUser->SetUsedWorldChatPoint(usedPoint);
        static_cast<void>(rawUser->GetSocket());
        if (socket != nullptr) {
            rawUser->GetSocket()->Send("chd", 0xfe, 0x175, rawUser->GetWorldChatCount());
        }
    }
    return false;
}

bool ReplyLoadClientINI(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::uint32_t accountId = 0;
    std::uint16_t type = 0;
    std::array<char, 0x3c01> clientIni{};
    ::Disassemble(packet, "dhs", &accountId, &type, static_cast<int>(clientIni.size()), clientIni.data());

    protocol::CS_ClientINI response{};
    response.cType = static_cast<char>(type);
    response.strIniStr.assign(std::string_view(clientIni.data()));

    {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(accountId);
        if (account != accountMap.end()) {
            account->second->Send(S_EX, S_EX_CLIENT_INI, response);
        }
    }
    return false;
}

bool ReplyLoadHonorsSkill(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &characterId);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        std::int32_t count = 0;
        cursor = ::Disassemble(cursor, "d", &count);
        for (std::int32_t index = 0; index < count; ++index) {
            std::int32_t skillId = 0;
            std::int32_t level = 0;
            cursor = ::Disassemble(cursor, "dd", &skillId, &level);
            user->SetHonorsSkillList(skillId, level, true);
        }
        user->AcquireAllHonorsSkill();
    }
    return false;
}

bool ReplyLoadUserInzonePoint(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    protocol::RP_LOAD_USER_INZONE_POINT response;
    network::Stream stream(const_cast<unsigned char*>(packet), 0x4000);
    if (!response.Decode(&stream)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyLoadUserInzonePoint failed, "
        //     L"invalid packet body",
        //     L"DBPacketEx.cpp",
        //     0x302a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ReplyLoadUserInzonePoint failed, "
        //     L"invalid packet body",
        //     L"DBPacketEx.cpp",
        //     0x302a)
        L2_LOG_ERROR(Log, "Instance point load reply rejected: packet body could not be decoded");
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(response.nUserSID));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x302e,
        //     response.nUserSID)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x302e,
        //     response.nUserSID)
        L2_LOG_ERROR(
            Log, "Instance point load reply cannot be applied: user object=%u is unavailable",
            static_cast<unsigned>(response.nUserSID)
        );
        return false;
    }

    for (const protocol::UserInzonePoint& point : response.userInzonePointList) {
        user->SetInzonePoint(point.nInzoneTypeId, point.nPoint, point.nUpdateTime);
    }
    return false;
}

bool Reply_RP_EX_WOLYMPIAD_LOAD_ALL_USER(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a2890, RP_EX 0x112.
    L2_GUARD_FUNCTION();

    protocol::RP_EX_WOLYMPIAD_LOAD_ALL_USER reply;
    if (!DecodePacket(reply, packet)) {
        return false;
    }

    for (const protocol::PkWOlympiadUser& user : reply.vUsers) {
        const std::CDualKey<int, int> key(user.nWorldID, user.nUserDBID);
        WOlympiadUsers::inst().SetNoblessInfo(
            key, user.nHeroType, user.nWinCount, user.nPreviousPoint, user.nOlympiadPoint, user.nMatchCount,
            user.nOlympiadWinCount, user.nOlympiadLoseCount, user.nOlympiadTradePoint, reply.bNewSeason
        );
        if (!reply.bStartUp) {
            WOlympiadService::inst().NotifyNoblessInfo(key, reply.bNewSeason);
        } else {
            g_JobFinishSignal.SetWOlympiadUserLoaded();
        }
    }

    CLogFormatter formatter;
    formatter.WriteLog4(
        0x1f7b, nullptr, nullptr, nullptr, nullptr, nullptr, reply.nPacketNum,
        static_cast<std::int64_t>(reply.vUsers.size()), 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    return false;
}

bool Reply_RP_EX_WOLYMPIAD_GET_MATCH_COUNT(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a2aa8, RP_EX 0x114.
    L2_GUARD_FUNCTION();

    protocol::RP_EX_WOLYMPIAD_GET_MATCH_COUNT reply;
    if (!DecodePacket(reply, packet)) {
        return false;
    }

    for (const protocol::PkWOlympiadMatchCount& match : reply.vMatchCounts) {
        WOlympiadUsers::inst().SetMatchCount(
            std::CDualKey<int, int>(match.nWorldID, match.nUserDBID), match.nSeason, match.nStep,
            static_cast<GameRuleType>(match.nGameRule), match.nMatchCount
        );
        g_JobFinishSignal.SetMatchCountLoaded();
    }
    return false;
}

bool Reply_RP_EX_WOLYMPIAD_GET_MATCH_RESULT(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00695a24, RP_EX 0x116.
    L2_GUARD_FUNCTION();

    protocol::RP_EX_WOLYMPIAD_GET_MATCH_RESULT reply;
    if (!DecodePacket(reply, packet)) {
        return false;
    }

    WHeroSystem::inst().AddHeroMatchResult(std::CDualKey<int, int>(reply.nWorldID, reply.nUserDBID), reply.vResults);
    return false;
}

bool Reply_RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00695b48, RP_EX 0x117.
    L2_GUARD_FUNCTION();

    protocol::RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD reply;
    if (!DecodePacket(reply, packet)) {
        return false;
    }

    static_cast<void>(WOlympiadService::inst().OnLoadMatchPointComplete(reply));
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]"
    //     L"Load matching point success: %d, %d",
    //     L"DBPacketEx.cpp",
    //     0x30b0,
    //     static_cast<int>(
    //         static_cast<std::int8_t>(reply.cGameRule)),
    //     reply.vPoints.size())
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]"
    //     L"Load matching point success: %d, %d",
    //     L"DBPacketEx.cpp",
    //     0x30b0,
    //     static_cast<int>(
    //         static_cast<std::int8_t>(reply.cGameRule)),
    //     reply.vPoints.size())
    L2_LOG_INFO(
        Log, "World Olympiad matching point reply processed: game rule=%d, reported entries=%zu",
        static_cast<int>(static_cast<std::int8_t>(reply.cGameRule)), reply.vPoints.size()
    );
    return false;
}

bool ResponseLoadOneDayRewardInfo(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00695ca0, RP_EX 0x118.
    L2_GUARD_FUNCTION();

    if (g_L2ServerType != L2ServerType::WORLDSERVER && g_bUseOneDayReward) {
        std::int32_t userServerId = 0;
        std::int32_t success = 0;
        SYSTEMTIME createTime{};
        std::int32_t rewardCount = 0;
        const unsigned char* cursor =
            ::Disassemble(packet, "ddbd", &userServerId, &success, 0x10, &createTime, &rewardCount);

        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
        User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user[sid:%d]",
            //     L"DBPacketEx.cpp",
            //     0x31b6,
            //     userServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid user[sid:%d]",
            //     L"DBPacketEx.cpp",
            //     0x31b6,
            //     userServerId)
            L2_LOG_ERROR(
                Log, "Daily reward load reply cannot be applied: user object=%u is unavailable, result=%d",
                static_cast<unsigned>(userServerId), success
            );
        } else {
            static_cast<void>(user->GetSocket());
            if (success == 1) {
                std::int32_t databaseId = 0;
                std::int32_t timeRewarded = 0;
                std::int32_t updateCheckTime = 0;
                std::int32_t accumulateCount = 0;
                std::int32_t stateType = 0;
                auto remaining = g_ToDoListManager.GetDailyMissionList();

                {
                    L2_GUARD(
                        *user,
                        (scoped_lock_detail::OwnerMethods<&User::EnterOneDayRewardLock, &User::LeaveOneDayRewardLock>{})
                    );
                    for (std::int32_t index = 0; index < rewardCount; ++index) {
                        cursor = ::Disassemble(
                            cursor, "ddddd", &databaseId, &timeRewarded, &updateCheckTime, &accumulateCount, &stateType
                        );
                        if (user->SetOneDayRewardMapForDB(
                                databaseId, timeRewarded, updateCheckTime, accumulateCount, stateType, createTime
                            )) {
                            remaining.erase(databaseId);
                        }
                    }
                    for (const int missingDatabaseId : remaining) {
                        static_cast<void>(user->SetOneDayRewardMapForDB(missingDatabaseId, 0, 0, 0, 0, createTime));
                    }
                    user->m_bLoadDBOneDayReward = true;
                }
                user->m_nUseTimeStampForToday = 0;
                CIOObject::SetTimeStamp(&user->m_nUseTimeStampForToday);
                if (user->m_bSendOneDayReward) {
                    user->SendToDoList(E_TODOLIST_TYPE_DAILY_REWARD, false);
                }
                user->CheckOneDayReward(true);
            }
        }
    }
    return false;
}

bool ResponseLoadAutoSoulShotUI(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0067d0c0, RP_EX 0x119.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t success = 0;
    std::array<std::int32_t, 4> itemClassIds{};
    std::array<std::int32_t, 4> activeStates{};
    ::Disassemble(
        packet, "dddddddddd", &userServerId, &success, &itemClassIds[0], &activeStates[0], &itemClassIds[1],
        &activeStates[1], &itemClassIds[2], &activeStates[2], &itemClassIds[3], &activeStates[3]
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x31fb,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0x31fb,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Automatic soulshot settings reply cannot be applied: user object=%u is unavailable, result=%d",
            static_cast<unsigned>(userServerId), success
        );
    } else {
        static_cast<void>(user->GetSocket());
        if (success == 0) {
            std::memset(user->m_nAutoSoulshotFlags, 0, 0x44);
            user->m_nAutoSoulshotFlag = 0;
            for (int index = 0; index < 4; ++index) {
                AutoSoulShotUi& state = user->m_AutoSoulShotUi[index];
                state.m_ItemClassID = 0;
                state.m_IsActiveState = index + 1;
                user->GetSocket()->Send("chddd", 0xfe, 0x0c, state.m_ItemClassID, state.m_IsActiveState, index);
            }
        } else {
            for (int index = 0; index < 4; ++index) {
                if (activeStates[index] < 0 || activeStates[index] > 3) {
                    activeStates[index] = 1;
                }

                if (itemClassIds[index] == 0 || activeStates[index] != 3) {
                    itemClassIds[index] = 0;
                } else {
                    CItem* const item = user->m_inv.GetFirstItemByClassID(itemClassIds[index], false, false);
                    if (item == nullptr || !item->IsItem()) {
                        itemClassIds[index] = 0;
                    }
                }

                AutoSoulShotUi& state = user->m_AutoSoulShotUi[index];
                state.m_ItemClassID = itemClassIds[index];
                state.m_IsActiveState = activeStates[index];
                if (itemClassIds[index] == 0) {
                    user->GetSocket()->Send("chddd", 0xfe, 0x0c, state.m_ItemClassID, state.m_IsActiveState, index);
                } else {
                    static_cast<void>(user->OnExAutoSoulShotPacket(itemClassIds[index], true, index, activeStates[index]));
                }
            }
        }
    }
    return false;
}

bool ResponseLoadUserPledgeBonus(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00692930, RP_EX 0x11a.
    L2_GUARD_FUNCTION();

    std::int32_t characterDatabaseId = 0;
    std::int32_t timeStampType = 0;
    std::int32_t savedTime = 0;
    ::Disassemble(packet, "ddd", &characterDatabaseId, &timeStampType, &savedTime);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterDatabaseId));
    if (user) {
        const std::int32_t adjustedSavedTime = runtime::SubtractInt32Wrapping(savedTime, 0x5b68);
        const std::tm* const savedLocalTime = DBPacketExLocalTime32(adjustedSavedTime);
        const int savedYear = savedLocalTime->tm_year;
        const int savedMonth = savedLocalTime->tm_mon;
        const int savedDay = savedLocalTime->tm_mday;

        const std::int32_t adjustedCurrentTime = runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), 0x5b68);
        const std::tm* const currentLocalTime = DBPacketExLocalTime32(adjustedCurrentTime);
        if (savedYear == currentLocalTime->tm_year && savedMonth == currentLocalTime->tm_mon &&
            savedDay == currentLocalTime->tm_mday) {
            const std::size_t index = static_cast<std::size_t>(timeStampType);
            if (timeStampType == 0) {
                savedTime = runtime::SubtractInt32Wrapping(0, savedTime);
                user->tPledgeBonusTimeStamp[index] = savedTime;
                CPledge* const pledge = user->GetPledge();
                if (pledge != nullptr) {
                    pledge->SetPledgeMissionCheck(user.get(), savedTime);
                }
            } else {
                user->tPledgeBonusTimeStamp[index] = savedTime;
            }
        }
    }
    return false;
}

} // namespace DBPacketExHandler

bool ReplyLoadBlockedIP(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t operation;
    std::int32_t count;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &operation, &count);

    IPBlockManager& manager = IPBlockManager::inst();
    if (operation == 2) {
        manager.Clear();
    }

    for (std::int32_t index = 0; index < count; ++index) {
        std::int64_t startIP;
        std::int64_t endIP;
        cursor = ::Disassemble(cursor, "QQ", &startIP, &endIP);
        if (operation == 0) {
            manager.AddBlockedIP(startIP, endIP);
        } else if (operation > 0) {
            manager.DeleteBlockedIP(startIP, endIP);
        }
    }
    return false;
}

bool ReplyCheckRaidWorldEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00675ec0.
    L2_GUARD_FUNCTION();

    std::int32_t worldId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &worldId, &result);
    RaidServerManager::inst().ReplyVersionPacket(worldId, result);
    return false;
}

bool ReplyLoadAllianceEx(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0069f5b0, CacheD RP_EX 0x7f.
    L2_GUARD_FUNCTION();

    std::int32_t allianceDatabaseId = 0;
    std::array<SharedUtilsWideChar, 25> allianceName;
    std::int32_t masterPledgeDatabaseId = 0;
    std::int32_t oustTime = 0;
    std::int32_t crestId = 0;
    std::int32_t worldId = 0;
    std::int32_t memberCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "dSddddd", &allianceDatabaseId, 0x32, allianceName.data(), &masterPledgeDatabaseId, &oustTime, &crestId,
        &worldId, &memberCount
    );

    const std::CDualKey<int, int> allianceKey(worldId, allianceDatabaseId);
    if (g_AllianceDbIdMap.ExistKey(allianceKey)) {
        return false;
    }

    const std::wstring hostAllianceName = Utf16ToWide(reinterpret_cast<const char16_t*>(allianceName.data()));
    CAlliance* const alliance = new CAlliance(
        static_cast<std::uint32_t>(allianceDatabaseId), hostAllianceName.c_str(),
        static_cast<std::uint32_t>(masterPledgeDatabaseId), worldId, static_cast<std::uint32_t>(oustTime),
        static_cast<std::uint32_t>(crestId)
    );

    for (std::int32_t index = 0; index < memberCount; ++index) {
        std::int32_t pledgeDatabaseId = 0;
        cursor = ::Disassemble(cursor, "d", &pledgeDatabaseId);
        CSPointer<CPledge> pledge = CPledge::FindPledge(worldId, pledgeDatabaseId);
        if (!pledge) {
            db.RequestLoadPledge(worldId, static_cast<std::uint32_t>(pledgeDatabaseId), false);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] ::ReplyLoadAllianceEx LoadPledge "
            //     L"PledgeDBID[%d]",
            //     pledgeDatabaseId)
            // Noise: routine per-clan load request during alliance loading. This is not a failure.
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR] ::ReplyLoadAllianceEx LoadPledge "
            //     L"PledgeDBID[%d]",
            //     pledgeDatabaseId)
        } else {
            alliance->AddMemberPledge(pledge.get());
            pledge->SetAlliance(alliance, false);
        }
    }

    std::array<SharedUtilsWideChar, 25> lowerAllianceName;
    ::ToLowerCase(allianceName.data(), lowerAllianceName.data());
    const std::u16string lowerHostAllianceName = SanitizeUtf16(reinterpret_cast<const char16_t*>(lowerAllianceName.data()));
    g_AllianceNameMap.Insert(worldId, lowerHostAllianceName, alliance);
    g_AllianceDbIdMap.Insert(allianceKey, alliance, const_cast<char*>("DBPacketEx.cpp"), 0x1972);
    L2_RELEASE(alliance, ObjectRefType::CreateDelete);
    return false;
}

bool ReplyBrLoadMinigameScores(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId;
    std::int32_t myRank;
    std::int32_t myScore;
    std::int32_t bottomRank;
    std::int32_t bottomScore;
    const unsigned char* cursor = ::Disassemble(packet, "ddddd", &characterId, &myRank, &myScore, &bottomRank, &bottomScore);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        CPacketHelperEx<20480> scores;
        scores.Assemble("dddd", myRank, myScore, bottomRank, bottomScore);

        std::int32_t rank;
        std::array<char16_t, 25> characterName{};
        std::int32_t score;
        for (int index = 0; index < 100; ++index) {
            cursor = ::Disassemble(cursor, "dSd", &rank, 0x32, characterName.data(), &score);
            scores.Assemble("dSd", rank, characterName.data(), score);
            if (rank == -1) {
                break;
            }
        }

        user->GetSocket()->Send("chb", 0xfe, 0xde, scores.GetLength(), scores.GetPacket());
    }
    return false;
}

namespace DBPacketExHandler {

bool ReplyLoadOtherUserMacroEx(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::array<char16_t, 25> sourceCharacterName{};
    std::int32_t result = 0;
    ::Disassemble(packet, "dSd", &userServerId, 0x32, sourceCharacterName.data(), &result);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user) {
        if (result == 1) {
            db.RequestGetMacro(user);
        } else {
            const std::wstring sourceName = Utf16ToWide(sourceCharacterName.data());
            std::array<char16_t, 1024> message{};
            constexpr const char16_t* format = u"[CopyMacro] User[%s] does not exist.";
            FormatUtf16To(message.data(), message.size(), format, sourceName.c_str());
            char16_t context[] = u"";
            user->GetSocket()->SendSystemMessage(context, message.data());
        }
    }
    return false;
}

bool ReplyLoadOtherBuilderBookmarkEx(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::array<char16_t, 25> requestedCharacterName{};
    std::int32_t count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dSd", &userServerId, 0x32, requestedCharacterName.data(), &count);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user) {
        if (count < 1) {
            const std::wstring requestedName = Utf16ToWide(requestedCharacterName.data());
            std::array<char16_t, 1024> message{};
            constexpr const char16_t* format = u"User[%s] has no bookmarks or user does not exist.";
            FormatUtf16To(message.data(), message.size(), format, requestedName.c_str());
            char16_t context[] = u"";
            user->GetSocket()->SendSystemMessage(context, message.data());
        } else {
            for (std::int32_t index = 0; index < count; ++index) {
                std::array<char16_t, 51> bookmarkName{};
                std::int32_t x = 0;
                std::int32_t y = 0;
                std::int32_t z = 0;
                cursor = ::Disassemble(cursor, "Sddd", 0x66, bookmarkName.data(), &x, &y, &z);
                db.RequestBuilderBookMarkAdd(user, bookmarkName.data(), x, y, z);
            }
        }
    }
    return false;
}

} // namespace DBPacketExHandler

bool ReplyAcademyGraduateList(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x006789e0, RP_EX 0x3c.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t graduateCount;
    std::array<char16_t, 25> graduateName{};
    const unsigned char* cursor = ::Disassemble(packet, "dd", &userServerId, &graduateCount);
    if (graduateCount > 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
        User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
        if (!user) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user[%d]",
            //     L"DBPacketEx.cpp",
            //     0x857,
            //     userServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid user[%d]",
            //     L"DBPacketEx.cpp",
            //     0x857,
            //     userServerId)
            L2_LOG_ERROR(
                Log, "Academy graduate list cannot be delivered: user object=%u is unavailable, reported graduates=%d",
                static_cast<unsigned>(userServerId), graduateCount
            );
        } else {
            user->SendSystemMessage(SystemMessageId::RECENT_CLAN_ACADEMY_GRADUATES_HEADING);
            for (std::int32_t index = 0; index < graduateCount; ++index) {
                cursor = ::Disassemble(cursor, "S", 0x32, graduateName.data());
                user->GetSocket()->SendSystemMessage_Char(SystemMessageId::CLAN_ACADEMY_GRADUATE_ENTRY, graduateName.data());
            }
        }
    }
    return false;
}

bool ReplyPickCursedWeapon(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x0069b884, RP_EX 0x3d.
    L2_GUARD_FUNCTION();

    std::int32_t characterDatabaseId;
    std::int32_t itemDatabaseId;
    std::int32_t itemServerId;
    const unsigned char* const cursor = ::Disassemble(packet, "ddd", &characterDatabaseId, &itemDatabaseId, &itemServerId);

    CItem* const item = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(itemServerId));
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterDatabaseId));
    if (item != nullptr) {
        if (item->GetDBID() == 0) {
            item->SetDBID(static_cast<std::uint32_t>(itemDatabaseId));
        }
        if (user) {
            user->OnReplyPickCursedWeapon(*item);
        }
        CCursedWeaponMgr::inst().ReplyPickCursedWeaponFromDB(item, user.get(), cursor);
    }
    return false;
}

bool ReplyReorganizePledgeMember(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x006807b4, RP_EX 0x3e.
    L2_GUARD_FUNCTION();

    [[maybe_unused]] std::int32_t userServerId;
    std::int32_t pledgeDatabaseId;
    std::int32_t operation;
    std::uint32_t memberDatabaseId;
    PledgeType pledgeType;
    std::uint32_t swapMemberDatabaseId;
    std::int32_t result;
    ::Disassemble(
        packet, "ddddddd", &userServerId, &pledgeDatabaseId, &operation, &memberDatabaseId, &pledgeType,
        &swapMemberDatabaseId, &result
    );

    CPledge* const pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId).get();
    if (pledge == nullptr) {
        return false;
    }

    if (result == 1) {
        if (operation == 0) {
            pledge->ChangeMemberPledgeType(memberDatabaseId, pledgeType);
            int gradeId = CPledge::GetDefaultPowerGrade(pledgeType);
            wchar_t* memberName = pledge->GetMemberName(memberDatabaseId);
            pledge->SetPledgePowerGrade(memberName, gradeId);
            gradeId = CPledge::GetDefaultPowerGrade(pledgeType);
            db.RequestSetPledgePowerGrade(memberDatabaseId, gradeId);

            CSPointer<User> member = g_UserDBIDMap.FindObject(
                std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), static_cast<int>(memberDatabaseId))
            );
            if (member) {
                member->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
                    member->m_data->m_nPledgeType, static_cast<int>(pledgeType), 0x34
                );
                member->m_nParamChanged |= std::int64_t{PCB_PLEDGE};
                gradeId = CPledge::GetDefaultPowerGrade(pledgeType);
                if (member->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
                        member->m_data->m_nGradeId, gradeId, 0x46
                    )) {
                    member->ParamChanged(std::int64_t{PCB_USER});
                }
                pledge->RemovePledgeSkillsFrom(member.get());
                pledge->ApplyPledgeSkillsTo(member.get(), true, false);
            }
            memberName = pledge->GetMemberName(memberDatabaseId);
            pledge->ViewPledgeMemberInfo(memberName);
            pledge->UpdatePledgeMemberListPacket(memberDatabaseId);
        } else if (operation == 1) {
            const PledgeType oldPledgeType = pledge->ChangeMemberPledgeType(memberDatabaseId, pledgeType);
            int gradeId = CPledge::GetDefaultPowerGrade(pledgeType);
            wchar_t* memberName = pledge->GetMemberName(memberDatabaseId);
            pledge->SetPledgePowerGrade(memberName, gradeId);

            pledge->ChangeMemberPledgeType(swapMemberDatabaseId, oldPledgeType);
            gradeId = CPledge::GetDefaultPowerGrade(oldPledgeType);
            memberName = pledge->GetMemberName(swapMemberDatabaseId);
            pledge->SetPledgePowerGrade(memberName, gradeId);

            gradeId = CPledge::GetDefaultPowerGrade(pledgeType);
            db.RequestSetPledgePowerGrade(memberDatabaseId, gradeId);
            gradeId = CPledge::GetDefaultPowerGrade(oldPledgeType);
            db.RequestSetPledgePowerGrade(swapMemberDatabaseId, gradeId);

            CSPointer<User> member = g_UserDBIDMap.FindObject(
                std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), static_cast<int>(memberDatabaseId))
            );
            if (member) {
                member->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
                    member->m_data->m_nPledgeType, static_cast<int>(pledgeType), 0x34
                );
                member->m_nParamChanged |= std::int64_t{PCB_PLEDGE};
                gradeId = CPledge::GetDefaultPowerGrade(pledgeType);
                if (member->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
                        member->m_data->m_nGradeId, gradeId, 0x46
                    )) {
                    member->ParamChanged(std::int64_t{PCB_USER});
                }
                pledge->RemovePledgeSkillsFrom(member.get());
                pledge->ApplyPledgeSkillsTo(member.get(), true, false);
            }

            CSPointer<User> swapMember = g_UserDBIDMap.FindObject(
                std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), static_cast<int>(swapMemberDatabaseId))
            );
            if (swapMember) {
                swapMember->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
                    swapMember->m_data->m_nPledgeType, static_cast<int>(oldPledgeType), 0x34
                );
                swapMember->m_nParamChanged |= std::int64_t{PCB_PLEDGE};
                gradeId = CPledge::GetDefaultPowerGrade(oldPledgeType);
                if (swapMember->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
                        swapMember->m_data->m_nGradeId, gradeId, 0x46
                    )) {
                    swapMember->ParamChanged(std::int64_t{PCB_USER});
                }
                pledge->RemovePledgeSkillsFrom(swapMember.get());
                pledge->ApplyPledgeSkillsTo(swapMember.get(), true, false);
            }

            pledge->UpdatePledgeMemberListPacket(memberDatabaseId);
            pledge->UpdatePledgeMemberListPacket(swapMemberDatabaseId);
        }
    }
    pledge->EndTransaction();
    return false;
}

bool ReplyLoadBotReportTopTenPacket(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &characterId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        std::array<std::array<char16_t, 25>, 40> names{};
        std::array<std::int32_t, 40> scores{};
        for (std::size_t index = 0; index < names.size(); ++index) {
            cursor = ::Disassemble(cursor, "Sd", 0x32, names[index].data(), &scores[index]);
        }

        std::array<char16_t, 256> message{};
        FormatUtf16To(message.data(), message.size(), u"BOT Ranking 10\n");
        char16_t context[] = u"SYS";
        user->GetSocket()->SendSystemMessage(context, message.data());

        for (std::size_t index = 0; index < names.size(); ++index) {
            const std::wstring wideName = Utf16ToWide(names[index].data());
            constexpr const char16_t* format = u"<%d>: %s - %d\n";
            FormatUtf16To(
                message.data(), message.size(), format, static_cast<int>(index + 1), wideName.c_str(), scores[index]
            );
            user->GetSocket()->SendSystemMessage(context, message.data());
        }
    }
    return false;
}

namespace DBPacketExHandler {

bool ResponseCommissionTradeLoadInfo(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00675f84, RP_EX 0xa8.
    L2_GUARD_FUNCTION();

    std::int32_t rowType = 2;
    std::int64_t totalCount = 0;
    ::Disassemble(packet, "dQ", &rowType, &totalCount);

    CCommissionTradeManager& manager = CCommissionTradeManager::Inst();
    manager.m_nCommissionLoadCount = static_cast<std::int32_t>(totalCount);
    db.RequestCommissionTradeLoadAll(totalCount);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] [%s][%d] CommissionLoadInfo "
    //     L"nTotalCount[%I64d]",
    //     L"DBPacketEx.cpp",
    //     0x1c28,
    //     totalCount)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] [%s][%d] CommissionLoadInfo "
    //     L"nTotalCount[%I64d]",
    //     L"DBPacketEx.cpp",
    //     0x1c28,
    //     totalCount)
    L2_LOG_INFO(Log, "Commission trade loading requested: reported entries=%lld", static_cast<long long>(totalCount));
    return false;
}

bool ResponseCommissionTradeLoadAll(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0069bea0, RP_EX 0xa9.
    L2_GUARD_FUNCTION();

    std::int32_t rowType = 2;
    packet = ::Disassemble(packet, "d", &rowType);

    CCommissionTradeManager& manager = CCommissionTradeManager::Inst();
    if (rowType == 1) {
        std::int64_t commissionId = 0;
        Commission::Enum commissionType = Commission::None;
        std::int32_t userDatabaseId = 0;
        std::int32_t unusedOwnerLane = 0;
        std::array<char16_t, 100> itemName{};
        std::int64_t itemPrice = 0;
        std::int64_t unusedParsedAmount = 0;
        std::int32_t expiredDate = 0;
        CommissionExpired::Enum expiredType = CommissionExpired::EpInvalid;
        std::array<char16_t, 30> userName{};
        CommissionFirst::Enum firstType = CommissionFirst::FirstMAX;
        packet = ::Disassemble(
            packet, "QdddSQQddSd", &commissionId, &commissionType, &userDatabaseId, &unusedOwnerLane, 200, itemName.data(),
            &itemPrice, &unusedParsedAmount, &expiredDate, &expiredType, 0x3c, userName.data(), &firstType
        );

        CItemDisassemblerFromCacheD itemData;
        itemData.DisassembleItemInfo(packet);
        CCommissionItem item(
            static_cast<std::uint32_t>(itemData.m_nDBID), static_cast<std::uint32_t>(itemData.m_nClassID),
            itemData.m_nAmount, itemData.m_nEnchanted, itemData.m_nDamaged,
            static_cast<std::uint16_t>(itemData.m_nIsBlessed), itemData.m_nIdent,
            static_cast<std::uint16_t>(itemData.m_nWished), itemData.m_nVariation1, itemData.m_nVariation2,
            itemData.m_nIntensiveItemClassID, itemData.m_nDuration, itemData.m_Attribute.m_nAttackType,
            itemData.m_Attribute.m_nAttackValue, itemData.m_Attribute.m_nDefendValue, itemData.m_nShapeShiftedClassID,
            itemData.m_mEnsoulInfo, itemData.m_hLocked, itemData.m_hUseCount
        );
        manager.RegisterCommissionItem(
            commissionId, userDatabaseId, itemName.data(), itemPrice, itemData.m_nAmount, expiredDate, item, commissionType,
            expiredType, userName.data(), firstType
        );
        manager.RegisterExpiredCommissionData(
            commissionId, userDatabaseId, itemData.m_nDBID, expiredType, expiredDate, commissionType
        );

        runtime::raw_atomic::DecrementI32(&manager.m_nCommissionLoadCount);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] [%s][%d] CommissionLoadAll Leave "
        //     L"SetCount[%I64d], Type[%d]",
        //     L"DBPacketEx.cpp",
        //     0x1c9c,
        //     static_cast<std::int64_t>(
        //         manager.m_nCommissionLoadCount),
        //     static_cast<std::int32_t>(commissionType))
        // Noise: successful commission loading logged one record per item. The load request is logged above.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] [%s][%d] CommissionLoadAll Leave "
        //     L"SetCount[%I64d], Type[%d]",
        //     L"DBPacketEx.cpp",
        //     0x1c9c,
        //     static_cast<std::int64_t>(
        //         manager.m_nCommissionLoadCount),
        //     static_cast<std::int32_t>(commissionType))
    } else if (rowType == -2) {
        manager.SetCommissionStart();
        return false;
    } else {
        std::int64_t commissionId = 0;
        Commission::Enum commissionType = Commission::None;
        ::Disassemble(packet, "Qd", &commissionId, &commissionType);
        runtime::raw_atomic::DecrementI32(&manager.m_nCommissionLoadCount);
    }

    if (manager.m_nCommissionLoadCount < 1) {
        manager.SetCommissionStart();
    }
    return false;
}

bool ResponseCommissionTradeRegister(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0069a988, RP_EX 0xaa.
    L2_GUARD_FUNCTION();
    std::int32_t rowType = 2;
    packet = ::Disassemble(packet, "d", &rowType);
    CCommissionTradeManager& manager = CCommissionTradeManager::Inst();
    if (rowType == 1) {
        std::int64_t commissionId = 0;
        std::int32_t characterId = 0;
        std::int32_t itemId = 0;
        std::array<char16_t, 100> itemName{};
        std::int64_t itemPrice = 0;
        std::int64_t itemAmount = 0;
        std::int64_t commissionFee = 0;
        std::int32_t expiredDate = 0;
        Commission::Enum commissionType = Commission::None;
        CommissionExpired::Enum expiredType = CommissionExpired::EpInvalid;
        std::array<char16_t, 30> characterName{};
        CommissionFirst::Enum firstType = CommissionFirst::FirstMAX;
        CommissionDiscountInfo::Enum discountType = CommissionDiscountInfo::CDI_DEFAULT;
        std::int32_t premiumItemId = 0;
        std::int64_t premiumItemAmount = 0;
        packet = ::Disassemble(
            packet, "QddSQQQdddSdddQ", &commissionId, &characterId, &itemId, 200, itemName.data(), &itemPrice, &itemAmount,
            &commissionFee, &expiredDate, &commissionType, &expiredType, 0x3c, characterName.data(), &firstType,
            &discountType, &premiumItemId, &premiumItemAmount
        );
        static_cast<void>(premiumItemId);
        static_cast<void>(premiumItemAmount);

        CItemDisassemblerFromCacheD itemData;
        itemData.DisassembleItemInfo(packet);
        CSPointer<User> user = FindCommissionUser(characterId);
        if (!user) {
            CCommissionItem item = MakeCommissionItem(itemData);
            manager.RegisterCommissionItem(
                commissionId, characterId, itemName.data(), itemPrice, itemAmount, expiredDate, item, commissionType,
                expiredType, characterName.data(), firstType
            );
            manager.RegisterExpiredCommissionData(
                commissionId, characterId, itemData.m_nDBID, expiredType, expiredDate, commissionType
            );
            manager.DoneCommissionJob(commissionType);
            return false;
        }

        CItem* const inventoryItem = user->m_inv.GetByDBID(itemId);
        if (inventoryItem != nullptr) {
            CCommissionItem item = MakeCommissionItem(itemData);
            user->DeleteCommissionItem(itemId, itemAmount);
            user->DeleteCommissionFee(commissionFee);
            user->RegisterCommissionHistory(
                inventoryItem->m_data->m_nClassID, itemPrice, itemAmount, expiredType, discountType
            );
            manager.RegisterCommissionItem(
                commissionId, characterId, itemName.data(), itemPrice, itemAmount, expiredDate, item, commissionType,
                expiredType, characterName.data(), firstType
            );
            manager.RegisterExpiredCommissionData(
                commissionId, characterId, itemData.m_nDBID, expiredType, expiredDate, commissionType
            );
        }
        manager.DoneCommissionJob(commissionType);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SetPendingUseETCItem(nullptr, PUIT_COMMISSION);
        user->SendSystemMessage(SystemMessageId::ITEM_REGISTRATION_SUCCEEDED);
        user->GetSocket()->Send("chd", 0xfe, 0xf5, 1);
        return false;
    }

    std::int32_t characterId = 0;
    Commission::Enum commissionType = Commission::None;
    ::Disassemble(packet, "dd", &characterId, &commissionType);
    CSPointer<User> user = FindCommissionUser(characterId);
    manager.DoneCommissionJob(commissionType);
    if (user) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SetPendingUseETCItem(nullptr, PUIT_COMMISSION);
        user->SendSystemMessage(SystemMessageId::ITEM_REGISTRATION_FAILED);
        user->GetSocket()->Send("chd", 0xfe, 0xf5, 0);
    }
    return false;
}

bool ResponseCommissionTradeDelete(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00688848, RP_EX 0xab.
    L2_GUARD_FUNCTION();
    std::int32_t rowType = 2;
    packet = ::Disassemble(packet, "d", &rowType);

    std::int64_t commissionId = 0;
    CommissionExpired::Enum expiredType = CommissionExpired::EpInvalid;
    Commission::Enum commissionType = Commission::None;
    std::int32_t characterId = 0;
    CommissionFirst::Enum firstType = CommissionFirst::FirstMAX;
    CommissionOwner::Enum owner = CommissionOwner::CoInvalid;
    std::int32_t itemId = 0;
    if (rowType == 1) {
        packet = ::Disassemble(
            packet, "Qdddddd", &commissionId, &characterId, &itemId, &expiredType, &commissionType, &owner, &firstType
        );
    } else {
        packet =
            ::Disassemble(packet, "Qddddd", &commissionId, &expiredType, &commissionType, &characterId, &firstType, &owner);
    }

    CCommissionTradeManager& manager = CCommissionTradeManager::Inst();
    CSPointer<User> user = FindCommissionUser(characterId);
    if (rowType == 1 && owner == CommissionOwner::CoUser && user) {
        int itemLane = 0;
        int itemClassId = 0;
        user->InsertCommissionItem(packet, itemLane, itemClassId);
        static_cast<void>(itemLane);
        static_cast<void>(itemClassId);
    }

    if (rowType != 1 && owner == CommissionOwner::CoUser && user) {
        user->SendSystemMessage(SystemMessageId::REGISTERED_ITEM_SALE_CANCELLATION_FAILED);
    }
    manager.DeleteCommissionItem(commissionId, firstType);
    manager.DeleteExpiredCommissionData(commissionId, expiredType);
    manager.DoneCommissionJob(commissionType);

    if (user) {
        if (owner == CommissionOwner::CoUser) {
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            if (rowType == 1) {
                user->SendSystemMessage(SystemMessageId::ITEM_SALE_CANCELLATION_SUCCEEDED);
            }
        }
        user->GetSocket()->Send("chd", 0xfe, 0xf6, 1);
    }
    return false;
}

bool ResponseCommissionTradeBuyItem(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00689100, RP_EX 0xac.
    L2_GUARD_FUNCTION();
    std::int32_t rowType = 2;
    packet = ::Disassemble(packet, "d", &rowType);
    CCommissionTradeManager& manager = CCommissionTradeManager::Inst();
    if (rowType == 1) {
        std::int64_t commissionId = 0;
        Commission::Enum commissionType = Commission::Invalid;
        CommissionExpired::Enum expiredType = CommissionExpired::EpInvalid;
        std::int32_t buyerId = 0;
        std::int32_t sellerId = 0;
        std::int64_t totalPrice = 0;
        std::int64_t amount = 0;
        CommissionFirst::Enum firstType = CommissionFirst::FirstMAX;
        packet = ::Disassemble(
            packet, "QddddQQd", &commissionId, &commissionType, &expiredType, &buyerId, &sellerId, &totalPrice, &amount,
            &firstType
        );
        static_cast<void>(sellerId);

        CSPointer<User> user = FindCommissionUser(buyerId);
        if (!user) {
            manager.DeleteCommissionItem(commissionId, firstType);
            manager.DeleteExpiredCommissionData(commissionId, expiredType);
            manager.DoneCommissionJob(commissionType);
        } else {
            int itemLane = 0;
            int itemClassId = 0;
            user->InsertCommissionItem(packet, itemLane, itemClassId);
            user->DeleteCommissionFee(totalPrice);
            manager.DeleteCommissionItem(commissionId, firstType);
            manager.DeleteExpiredCommissionData(commissionId, expiredType);
            manager.DoneCommissionJob(commissionType);
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            user->GetSocket()->Send("chdddQ", 0xfe, 0xf9, 1, itemLane, itemClassId, amount);
        }
        RelayClient::Inst()->SendCommissionTradeBuyPacket(static_cast<int>(commissionId));
        return false;
    }

    Commission::Enum commissionType = Commission::Invalid;
    std::int32_t buyerId = 0;
    ::Disassemble(packet, "dd", &commissionType, &buyerId);
    manager.DoneCommissionJob(commissionType);
    CSPointer<User> user = FindCommissionUser(buyerId);
    if (user) {
        user->SendSystemMessage(SystemMessageId::ITEM_PURCHASE_FAILED);
        user->GetSocket()->Send("chd", 0xfe, 0xf9, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    }
    return false;
}

bool ResponseCommissionTradeRegisteredList(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068967c, RP_EX 0xad.
    L2_GUARD_FUNCTION();
    std::int32_t rowType = 2;
    std::int32_t characterId = 0;
    packet = ::Disassemble(packet, "dd", &rowType, &characterId);
    CSPointer<User> user = FindCommissionUser(characterId);
    if (user) {
        if (rowType == 1) {
            std::int32_t count = 0;
            packet = ::Disassemble(packet, "d", &count);
            CCommissionTradeManager::Inst().CommissionList(CommissionStatus::CsRegisteredOk, characterId, count, packet);
        } else {
            user->GetSocket()->Send("chd", 0xfe, 0xf7, -2);
        }
    }
    return false;
}

bool ResponseCommissionTradeSearchList(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00689828, RP_EX 0xae.
    L2_GUARD_FUNCTION();
    std::int32_t rowType = 2;
    std::int32_t characterId = 0;
    packet = ::Disassemble(packet, "dd", &rowType, &characterId);
    CSPointer<User> user = FindCommissionUser(characterId);
    if (user) {
        if (rowType == 1) {
            std::int32_t count = 0;
            packet = ::Disassemble(packet, "d", &count);
            CCommissionTradeManager::Inst().CommissionList(CommissionStatus::CsSearchOk, characterId, count, packet);
        } else {
            user->GetSocket()->Send("chd", 0xfe, 0xf7, -1);
        }
    }
    return false;
}

bool ResponseCommissionTradeRegTotalCount(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006899d0, RP_EX 0xaf.
    L2_GUARD_FUNCTION();
    std::int32_t rowType = 2;
    std::int32_t characterId = 0;
    packet = ::Disassemble(packet, "dd", &rowType, &characterId);
    CSPointer<User> user = FindCommissionUser(characterId);
    if (!user) {
        return false;
    }
    if (rowType == 1) {
        std::int32_t itemServerId = 0;
        std::int32_t totalCount = 0;
        ::Disassemble(packet, "dd", &itemServerId, &totalCount);
        CCommissionTradeManager& manager = CCommissionTradeManager::Inst();
        if (!manager.m_bLimit || totalCount < 10) {
            CItem* const item = user->m_inv.GetByServerID(itemServerId);
            if (item != nullptr) {
                user->SetPendingUseETCItem(item, PUIT_COMMISSION);
                manager.RequestCommissionTradeInfo(user.get(), item);
                return false;
            }
            user->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_MISSING_ITEM);
        } else {
            user->SendSystemMessage(SystemMessageId::ITEM_REGISTRATION_CAPACITY_EXHAUSTED);
        }
    } else {
        user->SendSystemMessage(SystemMessageId::ITEM_REGISTRATION_FAILED);
    }
    user->GetSocket()->Send("chd", 0xfe, 0xf4, 0);
    return false;
}

bool ReplySetPledgeEmblemAck(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::uint32_t pledgeServerId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "ddd", &userServerId, &pledgeServerId, &result);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"DBPacketEx.cpp",
        //     0x24bd,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"DBPacketEx.cpp",
        //     0x24bd,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Clan emblem acknowledgement cannot be delivered: user object=%u is unavailable, clan object=%u, result=%d",
            static_cast<unsigned>(userServerId), static_cast<unsigned>(pledgeServerId), result
        );
        return false;
    }

    if (!CPledge::GetObjectW(pledgeServerId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBPacketEx.cpp",
        //     0x24c4,
        //     pledgeServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBPacketEx.cpp",
        //     0x24c4,
        //     pledgeServerId)
        L2_LOG_ERROR(
            Log, "Clan emblem acknowledgement cannot be delivered: clan object=%u is unavailable, user object=%u, result=%d",
            static_cast<unsigned>(pledgeServerId), static_cast<unsigned>(userServerId), result
        );
        return false;
    }

    user->GetSocket()->Send("chd", 0xfe, 0x133, result);
    return false;
}

bool ReplyRegisterXmasWish(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x006a20b0.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &characterId, &result);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user && user->GetSocket() != nullptr) {
        if (result == 0) {
            CEvent* const event = Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_ONETIME);
            if (event != nullptr) {
                CEvent_OneTimeEvent* const oneTimeEvent = static_cast<CEvent_OneTimeEvent*>(event);
                oneTimeEvent->GiveItem(*user.get());
                oneTimeEvent->SetItemGiveToUser(static_cast<std::int32_t>(user->GetDBID()));
                user->SendSystemMessage(SystemMessageId::WISH_TREE_WISH_REGISTERED);
            }
        } else if (result == 2) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::WISH_ALREADY_REGISTERED_AND_REWARDED);
        } else {
            CEvent* const event = Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_ONETIME);
            if (event != nullptr) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::WISH_REGISTRATION_FAILED);
            }
        }
    }
    return false;
}

bool ReplyConfirmXmasWish(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &characterId, &result);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        std::int32_t response = 2;
        if (result == 3) {
            const double carryingRate = static_cast<double>(user->m_inv.GetCarryingWeight()) / user->CarryWeight();
            response = carryingRate < 0.8 && user->m_inv.GetInventoryCount() < user->GetInventoryLimit() ? 0 : 1;
        }
        user->GetSocket()->Send("chd", 0xfe, 0x14c, response);
    }
    return false;
}

bool ReplyGetDormantGiftInfo(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0068e3e4.
    L2_GUARD_FUNCTION();

    std::uint32_t accountId = 0;
    std::int32_t recordedCharacterId = 0;
    std::int32_t dormantType = 0;
    std::int32_t giftTime = 0;
    std::int32_t requestKind = 0;
    ::Disassemble(packet, "ddddd", &accountId, &recordedCharacterId, &dormantType, &giftTime, &requestKind);

    if (requestKind == 1) {
        CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
        if (!user) {
            return false;
        }

        // if (dormantType == 3) {
        if (dormantType == DormantConst::DT_DORMANT) {
            const std::int32_t now = static_cast<std::int32_t>(static_cast<std::uint32_t>(std::time(nullptr)));
            const std::int32_t elapsed = runtime::SubtractInt32Wrapping(now, giftTime);
            if (recordedCharacterId == 0 || runtime::MultiplyInt32Wrapping(g_DormantUserDay, 0x15180) < elapsed) {
                user->SendDormantGift(dormantType);
            }
            // } else if (dormantType == 2 &&
        } else if (dormantType == DormantConst::DT_NEWBIE && recordedCharacterId == 0) {
            // user->SendDormantGift(2)
            user->SendDormantGift(DormantConst::DT_NEWBIE);
        }
        return false;
    }

    if (requestKind == 2 &&
        // dormantType == 2 &&
        dormantType == DormantConst::DT_NEWBIE && recordedCharacterId == 0) {
        {
            L2_GUARD(accountMapSect);
            const auto account = accountMap.find(accountId);
            if (account != accountMap.end() && account->second != nullptr) {
                account->second->SendSystemMessage(SystemMessageId::NEW_ACCOUNT_FIRST_CHARACTER_ITEM_REWARD_NOTICE);
            }
        }
    }
    return false;
}

} // namespace DBPacketExHandler

namespace protocol {

bool CS_ClientINI::Encode(network::Stream* stream) const { return stream->Encode(&cType) && stream->Encode(&strIniStr); }

bool CS_ClientINI::Decode(network::Stream* stream) { return stream->Decode(&cType) && stream->Decode(&strIniStr); }

} // namespace protocol

bool ReplySavePremiumItemForChar(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::uint32_t accountId = 0;
    std::array<char16_t, 24> buyerName{};
    std::int32_t itemCount = 0;
    packet = ::Disassemble(packet, "dSd", &accountId, 0x30, buyerName.data(), &itemCount);

    CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
    std::array<std::uint8_t, 0x2000> warehouseIds{};
    int packetLength = 0;
    bool added = false;

    for (int index = 0; index < itemCount; ++index) {
        std::int64_t warehouseNo = 0;
        std::int32_t itemClassId = 0;
        std::int64_t itemAmount = 0;
        std::int32_t isGift = 0;
        packet = ::Disassemble(packet, "QdQd", &warehouseNo, &itemClassId, &itemAmount, &isGift);

        if (user) {
            L2_GUARD(user->m_PremiumItemLock);
            if (user->m_mapPremiumItem.find(warehouseNo) == user->m_mapPremiumItem.end()) {
                const std::wstring buyerNameWide = Utf16ToWide(buyerName.data());
                user->m_mapPremiumItem.emplace(
                    warehouseNo,
                    new CPremiumItem(
                        warehouseNo, itemClassId, itemAmount, isGift == 1, const_cast<wchar_t*>(buyerNameWide.c_str())
                    )
                );
                added = true;
            }
        }

        packetLength += ::Assemble(
            reinterpret_cast<char*>(warehouseIds.data()) + packetLength,
            static_cast<int>(warehouseIds.size()) - packetLength, "Q", warehouseNo
        );
    }

    if (itemCount > 0) {
        g_PremiumSvc.RequestDeleteItem(itemCount, warehouseIds.data());
    }

    if (user && added) {
        user->SendPremiumItemNotification();
        if (g_GoodsInventorySetting.enable) {
            user->RequestInquiryGoodsInventory(false);
        }
    }
    return false;
}

bool ReplyLoadUserPremiumItemPacket(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t page = 0;
    std::int32_t itemCount = 0;
    packet = ::Disassemble(packet, "ddd", &userServerId, &page, &itemCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0xdea,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0xdea,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Premium item list reply cannot be applied: user object=%u is unavailable, page=%d, reported items=%d",
            static_cast<unsigned>(userServerId), page, itemCount
        );
        return false;
    }

    (void)user->GetSocket();
    bool added = false;
    {
        L2_GUARD(user->m_PremiumItemLock);
        if (page == 1) {
            user->m_mapPremiumItem.clear();
        }

        for (int index = 0; index < itemCount; ++index) {
            std::int64_t warehouseNo = 0;
            std::int32_t itemClassId = 0;
            std::int64_t itemAmount = 0;
            std::int32_t recipientAccountId = 0;
            std::array<char16_t, 24> buyerName{};
            packet = ::Disassemble(
                packet, "QdQdS", &warehouseNo, &itemClassId, &itemAmount, &recipientAccountId, 0x30, buyerName.data()
            );

            if (user->m_mapPremiumItem.find(warehouseNo) == user->m_mapPremiumItem.end()) {
                const std::wstring buyerNameWide = Utf16ToWide(buyerName.data());
                user->m_mapPremiumItem.emplace(
                    warehouseNo,
                    new CPremiumItem(
                        warehouseNo, itemClassId, itemAmount,
                        user->GetAccountID() == static_cast<std::uint32_t>(recipientAccountId),
                        const_cast<wchar_t*>(buyerNameWide.c_str())
                    )
                );
                added = true;
            }
        }
    }

    if (g_GoodsInventorySetting.enable) {
        user->RequestInquiryExistGoods();
    } else if (added) {
        user->SendPremiumItemNotification();
    }
    return false;
}

bool ReplyManipulatePremiumItemsPacket(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int64_t warehouseNo = 0;
    std::int64_t withdrawAmount = 0;
    packet = ::Disassemble(packet, "dQQ", &userServerId, &warehouseNo, &withdrawAmount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0xe28,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBPacketEx.cpp",
        //     0xe28,
        //     userServerId)
        L2_LOG_ERROR(
            Log,
            "Premium item withdrawal reply cannot be applied: user object=%u is unavailable, warehouse number=%lld, "
            "amount=%lld",
            static_cast<unsigned>(userServerId), static_cast<long long>(warehouseNo), static_cast<long long>(withdrawAmount)
        );
        return false;
    }

    (void)user->GetSocket();
    if (warehouseNo == 0) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->GetSocket()->Send(
            "chd", 0xfe, 0x10e,
            // -2)
            GIR_ETC_SYSTEM_ERROR
        );
        return false;
    }

    {
        L2_GUARD(user->m_PremiumItemLock);
        auto iterator = user->m_mapPremiumItem.find(warehouseNo);
        if (iterator == user->m_mapPremiumItem.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][Premium] try to withdraw premium item. "
            //     L"but apply to only cached. [user:%d item:%I64d]",
            //     L"DBPacketEx.cpp",
            //     0xe3a,
            //     user->GetDBID(),
            //     warehouseNo)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][Premium] try to withdraw premium item. "
            //     L"but apply to only cached. [user:%d item:%I64d]",
            //     L"DBPacketEx.cpp",
            //     0xe3a,
            //     user->GetDBID(),
            //     warehouseNo)
            L2_LOG_ERROR(
                Log,
                "Premium item withdrawal reply rejected: warehouse number=%lld is absent from the local premium item list, "
                "character ID=%d",
                static_cast<long long>(warehouseNo), user->GetDBID()
            );
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            user->GetSocket()->Send(
                "chd", 0xfe, 0x10e,
                // -2)
                GIR_ETC_SYSTEM_ERROR
            );
            return false;
        }

        CPremiumItem* const premiumItem = iterator->second;
        const std::uint64_t remainingBits =
            static_cast<std::uint64_t>(premiumItem->m_nItemAmount) - static_cast<std::uint64_t>(withdrawAmount);
        std::memcpy(&premiumItem->m_nItemAmount, &remainingBits, sizeof(remainingBits));
        if (premiumItem->m_nItemAmount < 1) {
            user->m_mapPremiumItem.erase(iterator);
            L2_RELEASE(premiumItem, ObjectRefType::CreateDelete);
        }
    }

    {
        L2_WRITE_GUARD(user->m_inv);
        {
            CItemDisassemblerItemMani itemData;
            packet = itemData.DisassembleItemInfo(packet);

            if (itemData.m_nIndex == 1) {
                CItem* const item = g_ObjectDB.CreateItem(itemData.m_nClassID);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d][Premium] CreateItem Failed. item[%I64d]",
                    //     L"DBPacketEx.cpp",
                    //     0xefe,
                    //     warehouseNo)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d][Premium] CreateItem Failed. item[%I64d]",
                    //     L"DBPacketEx.cpp",
                    //     0xefe,
                    //     warehouseNo)
                    L2_LOG_ERROR(
                        Log,
                        "Premium item withdrawal could not create inventory item: warehouse number=%lld, item class=%d, "
                        "item database ID=%d",
                        static_cast<long long>(warehouseNo), itemData.m_nClassID, itemData.m_nDBID
                    );
                } else {
                    item->SetDBID(static_cast<unsigned int>(itemData.m_nDBID));
                    // Current 0x0069749c, locked CACHED_REPLY_EX_STATUS row 0x5f:
                    // amount-update failure skips this item unlock. Retain the asymmetric
                    // owner calls. A local guard would change that shipping path.
                    item->m_lock->Enter(L"DBPacketEx.cpp", 0xed8);
                    if (!item->SetAmount(itemData.m_nChangeAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d][Premium] item[%I64d] amount is "
                        //     L"invalid[%I64d][%I64d]",
                        //     L"DBPacketEx.cpp",
                        //     0xedc,
                        //     warehouseNo,
                        //     item->m_data->m_nCount,
                        //     itemData.m_nChangeAmount)
                        // L2_LOG_ERROR(Log,
                        //     L"[%s][%d][Premium] item[%I64d] amount is "
                        //     L"invalid[%I64d][%I64d]",
                        //     L"DBPacketEx.cpp",
                        //     0xedc,
                        //     warehouseNo,
                        //     item->m_data->m_nCount,
                        //     itemData.m_nChangeAmount)
                        L2_LOG_ERROR(
                            Log,
                            "Premium item withdrawal could not set new item amount: warehouse number=%lld, item database "
                            "ID=%d, current=%lld, requested=%lld",
                            static_cast<long long>(warehouseNo), itemData.m_nDBID,
                            static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(itemData.m_nChangeAmount)
                        );
                    } else {
                        item->SetEnchanted(itemData.m_nEnchanted);
                        item->m_Useable = user->CheckUseable(item);
                        item->m_data->m_nDamaged = itemData.m_nDamaged;
                        item->m_data->m_bBlessed = itemData.m_nIsBlessed;
                        item->SetVariation(
                            ItemOptionKey{itemData.m_nVariation1, itemData.m_nVariation2},
                            static_cast<std::uint32_t>(itemData.m_nIntensiveItemClassID)
                        );
                        item->SetAttribute(itemData.m_Attribute);
                        item->m_lock->Leave(nullptr, 0);

                        if (item->IsDuration()) {
                            db.UpdateItemDuration(*user, static_cast<unsigned int>(itemData.m_nDBID), item->GetDuration());
                        }
                        if (item->m_const->m_dAgathionMaxEnergy > 0.0) {
                            db.RequestLoadItemAgathionEnergy(user, item->m_data->m_nId, static_cast<int>(item->GetDBID()));
                        }
                        if (user->m_inv.Push(item->m_data->index)) {
                            user->m_inv.SetInventoryChanged(item, IUT_ADD);
                            user->SendItemIncreaseSystemMessage(
                                itemData.m_nClassID, itemData.m_nChangeAmount, item->m_data->GetEnchanted()
                            );
                        } else {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "[%s][%d][Premium] inven push failed. "
                            //     "item[%I64d]",
                            //     L"DBPacketEx.cpp",
                            //     0xef4,
                            //     warehouseNo)
                            // L2_LOG_ERROR(Log,
                            //     "[%s][%d][Premium] inven push failed. "
                            //     "item[%I64d]",
                            //     L"DBPacketEx.cpp",
                            //     0xef4,
                            //     warehouseNo)
                            L2_LOG_ERROR(
                                Log, "Premium item could not be inserted into inventory: warehouse number=%lld",
                                static_cast<long long>(warehouseNo)
                            );
                        }
                    }
                }
            } else if (itemData.m_nIndex == 2) {
                CItem* const item = user->m_inv.GetByDBID(itemData.m_nDBID);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d][Premium] pItemInv == NULL, this "
                    //     L"situation must not occur. dbid[%d]",
                    //     L"DBPacketEx.cpp",
                    //     0xf18,
                    //     itemData.m_nDBID)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d][Premium] pItemInv == NULL, this "
                    //     L"situation must not occur. dbid[%d]",
                    //     L"DBPacketEx.cpp",
                    //     0xf18,
                    //     itemData.m_nDBID)
                    L2_LOG_ERROR(
                        Log,
                        "Premium item withdrawal cannot update inventory item: item database ID=%d is unavailable, "
                        "warehouse number=%lld",
                        itemData.m_nDBID, static_cast<long long>(warehouseNo)
                    );
                } else {
                    // Current 0x0069749c, locked CACHED_REPLY_EX_STATUS row 0x5f:
                    // amount-update failure skips this item unlock. Retain the asymmetric
                    // owner calls. A local guard would change that shipping path.
                    item->m_lock->Enter(L"DBPacketEx.cpp", 0xf09);
                    if (!item->AddAmount(itemData.m_nChangeAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d][Premium] item[%I64d] amount is "
                        //     L"invalid[%I64d][%I64d]",
                        //     L"DBPacketEx.cpp",
                        //     0xf0d,
                        //     warehouseNo,
                        //     item->m_data->m_nCount,
                        //     itemData.m_nChangeAmount)
                        // L2_LOG_ERROR(Log,
                        //     L"[%s][%d][Premium] item[%I64d] amount is "
                        //     L"invalid[%I64d][%I64d]",
                        //     L"DBPacketEx.cpp",
                        //     0xf0d,
                        //     warehouseNo,
                        //     item->m_data->m_nCount,
                        //     itemData.m_nChangeAmount)
                        L2_LOG_ERROR(
                            Log,
                            "Premium item withdrawal could not increase inventory item amount: warehouse number=%lld, item "
                            "database ID=%d, current=%lld, increase=%lld",
                            static_cast<long long>(warehouseNo), itemData.m_nDBID,
                            static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(itemData.m_nChangeAmount)
                        );
                    } else {
                        item->m_lock->Leave(nullptr, 0);
                        user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                        user->SendItemIncreaseSystemMessage(
                            itemData.m_nClassID, itemData.m_nChangeAmount, item->m_data->GetEnchanted()
                        );
                    }
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d][Premium] manipulate item fail. unknown "
                //     L"manipulate type[%d]",
                //     L"DBPacketEx.cpp",
                //     0xf20,
                //     itemData.m_nIndex)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d][Premium] manipulate item fail. unknown "
                //     L"manipulate type[%d]",
                //     L"DBPacketEx.cpp",
                //     0xf20,
                //     itemData.m_nIndex)
                L2_LOG_ERROR(
                    Log,
                    "Premium item withdrawal has an unsupported inventory operation: operation=%d, warehouse number=%lld, "
                    "item database ID=%d",
                    itemData.m_nIndex, static_cast<long long>(warehouseNo), itemData.m_nDBID
                );
            }
        }
    }

    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    user->SendItemList(true, true, nullptr);
    if (!g_GoodsInventorySetting.enable) {
        user->SendSystemMessage(SystemMessageId::SHARED_DIMENSIONAL_ITEM_RETRIEVED);
        user->ShowPremiumItemList();
    } else {
        user->GetSocket()->Send(
            "chd", 0xfe, 0x10e,
            // 2)
            GIR_RETRIEV_OK
        );
        user->RequestInquiryGoodsInventory(true);
    }
    return false;
}

bool ReplyLoadPremiumItemsForBatchPacket(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t itemCount = 0;
    packet = ::Disassemble(packet, "d", &itemCount);
    if (itemCount > 0) {
        g_PremiumSvc.RequestDeleteItem(itemCount, packet);
    }
    return false;
}

namespace {

// The current DBPacketEx.cpp initializer owner emits these 308 Bind calls in
// the exact current CRT order. The eleven mail callbacks at the end are the
// earlier bindings later overwritten by HtmlBuilderPacketEx.cpp.
#define L2_BIND_DB_REPLY_EX(Name, Opcode, Callback) L2_BIND_PACKET(Name, g_DBPacketTableEx, Opcode, Callback)

L2_BIND_DB_REPLY_EX(kBindRpExCentral000, 0x000, &ReplyLoadSevenSignQuestInfo);
L2_BIND_DB_REPLY_EX(kBindRpExCentral001, 0x001, &ReplyLoadSSQTopPointUser);
L2_BIND_DB_REPLY_EX(kBindRpExCentral004, 0x004, &ReplyLoadTimeAttackInfo);
L2_BIND_DB_REPLY_EX(kBindRpExCentral007, 0x007, &ReplyStartSevenSignEvent);
L2_BIND_DB_REPLY_EX(kBindRpExCentral013, 0x013, &ReplyLoadMarketPriceList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral014, 0x014, &ReplyRenewSubJob);
L2_BIND_DB_REPLY_EX(kBindRpExCentral015, 0x015, &ReplyLoadNobless);
L2_BIND_DB_REPLY_EX(kBindRpExCentral016, 0x016, &ReplySetPledgeEmblem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral017, 0x017, &ReplyGetPledgeEmblem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral018, 0x018, &ReplyDelPledgeEmblem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral019, 0x019, &ReplyGetNoblessTop10);
L2_BIND_DB_REPLY_EX(kBindRpExCentral087, 0x087, &LoadOlympiadMatchCount);
L2_BIND_DB_REPLY_EX(kBindRpExCentral088, 0x088, &GetOlympiadPlayerCount);
L2_BIND_DB_REPLY_EX(kBindRpExCentral01A, 0x01a, &ReplyGetNoblessAchievements);
L2_BIND_DB_REPLY_EX(kBindRpExCentral01D, 0x01d, &ReplyLoadPledgeSiegeKillDeathCount);
L2_BIND_DB_REPLY_EX(kBindRpExCentral01E, 0x01e, &TeleportPartyPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral01F, 0x01f, &ReplyLoadPCCafePointPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral020, 0x020, &ReplySavePCCafePointPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral021, 0x021, &ReplyNPCPickItemPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral022, 0x022, &ReplyFishingRegister);
L2_BIND_DB_REPLY_EX(kBindRpExCentral023, 0x023, &ReplyLoadBossRecord);
L2_BIND_DB_REPLY_EX(kBindRpExCentral024, 0x024, &ReplyCreateAcademy);
L2_BIND_DB_REPLY_EX(kBindRpExCentral025, 0x025, &ReplyLoadBossRecordRanking);
L2_BIND_DB_REPLY_EX(kBindRpExCentral026, 0x026, &ReplyLoadBossRecordRound);
L2_BIND_DB_REPLY_EX(kBindRpExCentral027, 0x027, &ReplyMoveToNextBossRecordRound);
L2_BIND_DB_REPLY_EX(kBindRpExCentral028, 0x028, &CMiniGameAgitDBHandler::ReplyLoadMiniGameAgit);
L2_BIND_DB_REPLY_EX(kBindRpExCentral029, 0x029, &CMiniGameAgitDBHandler::ReplyAddMiniGameAgitPledge);
L2_BIND_DB_REPLY_EX(kBindRpExCentral02A, 0x02a, &CMiniGameAgitDBHandler::ReplyAddMiniGameAgitMember);
L2_BIND_DB_REPLY_EX(kBindRpExCentral02B, 0x02b, &CMiniGameAgitDBHandler::ReplyResetMiniGameAgit);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0D0, 0x0d0, &CCompeteAgitDBHandler::ReplyResetCompeteAgit);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0D1, 0x0d1, &CCompeteAgitDBHandler::ReplyLoadCompeteAgit);
L2_BIND_DB_REPLY_EX(kBindRpExCentral02C, 0x02c, &DBPacketExHandler::ReplyUpdatePledgeNameValue);
L2_BIND_DB_REPLY_EX(kBindRpExCentral02D, 0x02d, &ReplyLoadSubPledge);
L2_BIND_DB_REPLY_EX(kBindRpExCentral02F, 0x02f, &CCursedWeaponDBHandler::ReplyLoadCursedWeaponList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral030, 0x030, &CPledgeRanking::LoadRanking);
L2_BIND_DB_REPLY_EX(kBindRpExCentral031, 0x031, &ReplyCreateSubPledge);
L2_BIND_DB_REPLY_EX(kBindRpExCentral02E, 0x02e, &ReplyLoadPledgeSkill);
L2_BIND_DB_REPLY_EX(kBindRpExCentral032, 0x032, &ReplyUpdateQuest);
L2_BIND_DB_REPLY_EX(kBindRpExCentral033, 0x033, &ReplyUpdateSubPledgeMaster);
L2_BIND_DB_REPLY_EX(kBindRpExCentral034, 0x034, &ReplyUpgradeSubPledgeMemberCount);
L2_BIND_DB_REPLY_EX(kBindRpExCentral035, 0x035, &ReplyGraduateAcademy);
L2_BIND_DB_REPLY_EX(kBindRpExCentral036, 0x036, &ReplySetAcademyMaster);
L2_BIND_DB_REPLY_EX(kBindRpExCentral037, 0x037, &ReplyPledgeMasterTransfer);
L2_BIND_DB_REPLY_EX(kBindRpExCentral038, 0x038, &ReplyPledgeMasterTransferCancel);
L2_BIND_DB_REPLY_EX(kBindRpExCentral039, 0x039, &ReplyPCCafeCouponCheck);
L2_BIND_DB_REPLY_EX(kBindRpExCentral03A, 0x03a, &ReplyRenameSubpledge);
L2_BIND_DB_REPLY_EX(kBindRpExCentral03B, 0x03b, &ReplyLoadTransferredPledgeMaster);
L2_BIND_DB_REPLY_EX(kBindRpExCentral03C, 0x03c, &ReplyAcademyGraduateList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral03D, 0x03d, &ReplyPickCursedWeapon);
L2_BIND_DB_REPLY_EX(kBindRpExCentral03E, 0x03e, &ReplyReorganizePledgeMember);
L2_BIND_DB_REPLY_EX(kBindRpExCentral03F, 0x03f, &ReplyLoadSharedReuseDelaysOfItems);
L2_BIND_DB_REPLY_EX(kBindRpExCentral040, 0x040, &ReplyVariationItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral041, 0x041, &ReplyCancelVariationItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral042, 0x042, &ReplyLoadDuelRecord);
L2_BIND_DB_REPLY_EX(kBindRpExCentral043, 0x043, &ReplyLargePacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral044, 0x044, &ReplyGetHeroMatchResultPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral045, 0x045, &ReplyLoadFieldCycleInfo);
L2_BIND_DB_REPLY_EX(kBindRpExCentral046, 0x046, &CFortressDBEventHandler::ReplyLoadFortress);
L2_BIND_DB_REPLY_EX(kBindRpExCentral047, 0x047, &CFortressDBEventHandler::ReplyLoadFortressSiege);
L2_BIND_DB_REPLY_EX(kBindRpExCentral048, 0x048, &CFortressDBEventHandler::ReplyLoadFortressSiegeRegistry);
L2_BIND_DB_REPLY_EX(kBindRpExCentral049, 0x049, &CFortressDBEventHandler::ReplyPayFortressRent);
L2_BIND_DB_REPLY_EX(kBindRpExCentral04A, 0x04a, &ReplyLoadAllAuction);
L2_BIND_DB_REPLY_EX(kBindRpExCentral04B, 0x04b, &ReplyLoadAllAuctionBid);
L2_BIND_DB_REPLY_EX(kBindRpExCentral04C, 0x04c, &ReplyGetLastStartTime);
L2_BIND_DB_REPLY_EX(kBindRpExCentral04D, 0x04d, &DBPacketExHandler::ReplyCreateItemAuction);
L2_BIND_DB_REPLY_EX(kBindRpExCentral04E, 0x04e, &ReplyManipulateWarehouseItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral04F, 0x04f, &ReplyConvertItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral050, 0x050, &ReplyChangeMergedName);
L2_BIND_DB_REPLY_EX(kBindRpExCentral051, 0x051, &SendTutorial);
L2_BIND_DB_REPLY_EX(kBindRpExCentral052, 0x052, &ReplyLoadInZoneRestriction);
L2_BIND_DB_REPLY_EX(kBindRpExCentral082, 0x082, &ReplyLoadInZoneFlag);
L2_BIND_DB_REPLY_EX(kBindRpExCentral084, 0x084, &ReplyLoadInzoneMaxDBID);
L2_BIND_DB_REPLY_EX(kBindRpExCentral053, 0x053, &SendUISetting);
L2_BIND_DB_REPLY_EX(kBindRpExCentral054, 0x054, &ReplySetCastleSiegeDefenceCountEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral055, 0x055, &ReplyWaitingTransferPledgeMasterEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral056, 0x056, &ReplyLoadUserPointPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral057, 0x057, &ReplySaveUserPointPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral058, 0x058, &ReplyLoadPVPPointRestrainPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral059, 0x059, &ReplyLoadLastColorPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral05A, 0x05a, &ReplyLoadNRMemoPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral06D, 0x06d, &ReplyLoadDailyQuestPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral05B, 0x05b, &CUserBookmarkHandler::DBPacketHandler);
L2_BIND_DB_REPLY_EX(kBindRpExCentral05C, 0x05c, &ReplyPeriodicItemJob);
L2_BIND_DB_REPLY_EX(kBindRpExCentral089, 0x089, &ReplyBrGiveItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral08A, 0x08a, &ReplyBrPlayingUser);
L2_BIND_DB_REPLY_EX(kBindRpExCentral08B, 0x08b, &ReplyBrProductSoldCount);
L2_BIND_DB_REPLY_EX(kBindRpExCentral08C, 0x08c, &ReplyBrRecentProduct);
L2_BIND_DB_REPLY_EX(kBindRpExCentral091, 0x091, &ReplyBrLoadMinigameScores);
L2_BIND_DB_REPLY_EX(kBindRpExCentral092, 0x092, &ReplyBrInsertMinigameScore);
L2_BIND_DB_REPLY_EX(kBindRpExCentral093, 0x093, &ReplyBrGetLectureMark);
L2_BIND_DB_REPLY_EX(kBindRpExCentral094, 0x094, &ReplyBrSetLectureMark);
L2_BIND_DB_REPLY_EX(kBindRpExCentral095, 0x095, &ReplyBrSetCreateDate);
L2_BIND_DB_REPLY_EX(kBindRpExCentral096, 0x096, &ReplyBrLoadDropItemAmountPerServer);
L2_BIND_DB_REPLY_EX(kBindRpExCentral097, 0x097, &ReplyBrSaveDropItemAmountPerServer);
L2_BIND_DB_REPLY_EX(kBindRpExCentral098, 0x098, &ReplyBrLoadItemAgathionEnergy);
L2_BIND_DB_REPLY_EX(kBindRpExCentral099, 0x099, &ReplyBrSaveItemAgathionEnergy);
L2_BIND_DB_REPLY_EX(kBindRpExCentral05D, 0x05d, &ReplySavePremiumItemForChar);
L2_BIND_DB_REPLY_EX(kBindRpExCentral05E, 0x05e, &ReplyLoadUserPremiumItemPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral05F, 0x05f, &ReplyManipulatePremiumItemsPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral060, 0x060, &ReplyLoadPremiumItemsForBatchPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral061, 0x061, &ReplyGetPointtop10Packet);
L2_BIND_DB_REPLY_EX(kBindRpExCentral062, 0x062, &ReplySaveBotReportPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral063, 0x063, &ReplyLoadBotReportPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral064, 0x064, &ReplyLoadBotReportTopTenPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral066, 0x066, &ReplyLoadOwnthing);
L2_BIND_DB_REPLY_EX(kBindRpExCentral065, 0x065, &ReplyLoadDominion);
L2_BIND_DB_REPLY_EX(kBindRpExCentral067, 0x067, &ReplyLoadDominionRegistry);
L2_BIND_DB_REPLY_EX(kBindRpExCentral068, 0x068, &ReplyLoadDominionSiege);
L2_BIND_DB_REPLY_EX(kBindRpExCentral06C, 0x06c, &ReplyLoadDominionRenamed);
L2_BIND_DB_REPLY_EX(kBindRpExCentral069, 0x069, &ReplyLoadBirthInfo);
L2_BIND_DB_REPLY_EX(kBindRpExCentral06A, 0x06a, &ReplyGetLastBirthdayGiftDate);
L2_BIND_DB_REPLY_EX(kBindRpExCentral06B, 0x06b, &ReplyLoad5YearInfo);
L2_BIND_DB_REPLY_EX(kBindRpExCentral06E, 0x06e, &CAirShipHandler::DBPacketHandler);
L2_BIND_DB_REPLY_EX(kBindRpExCentral06F, 0x06f, &ReplyCachePing);
L2_BIND_DB_REPLY_EX(kBindRpExCentral070, 0x070, &ReplyShutDownRequestByServerStopper);
L2_BIND_DB_REPLY_EX(kBindRpExCentral071, 0x071, &ReplyWritePost);
L2_BIND_DB_REPLY_EX(kBindRpExCentral072, 0x072, &ReplySendPost);
L2_BIND_DB_REPLY_EX(kBindRpExCentral073, 0x073, &ReplyReceivePost);
L2_BIND_DB_REPLY_EX(kBindRpExCentral074, 0x074, &ReplyRejectPost);
L2_BIND_DB_REPLY_EX(kBindRpExCentral075, 0x075, &ReplyCancelPost);
L2_BIND_DB_REPLY_EX(kBindRpExCentral076, 0x076, &ReplyDeletePost);
L2_BIND_DB_REPLY_EX(kBindRpExCentral077, 0x077, &ReplyOpenPost);
L2_BIND_DB_REPLY_EX(kBindRpExCentral078, 0x078, &ReplyMakeReceivedPostList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral079, 0x079, &ReplyLoadSentPostList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral07A, 0x07a, &ReplyLoadReceivedPostList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral07B, 0x07b, &ReplyLoadBlockedIP);
L2_BIND_DB_REPLY_EX(kBindRpExCentral07C, 0x07c, &ReplyLoadPetItemCount);
L2_BIND_DB_REPLY_EX(kBindRpExCentral07D, 0x07d, &DBPacketExHandler::ReplySummonLoadAbnormalStatusEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral080, 0x080, &DBPacketExHandler::ReplyAbnormalUserAction);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0A2, 0x0a2, &DBPacketExHandler::ReplyLoadGroupToggleEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral07E, 0x07e, &ReplyCheckRaidWorldEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral08D, 0x08d, &ReplyBrLoadEventTopRankers);
L2_BIND_DB_REPLY_EX(kBindRpExCentral08E, 0x08e, &ReplyBrUpdateEventRanking);
L2_BIND_DB_REPLY_EX(kBindRpExCentral08F, 0x08f, &ReplyBrGetEventScore);
L2_BIND_DB_REPLY_EX(kBindRpExCentral090, 0x090, &ReplyBrSetEventScore);
L2_BIND_DB_REPLY_EX(kBindRpExCentral07F, 0x07f, &ReplyLoadAllianceEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral081, 0x081, &ReplyLoadQuestNPCLog);
L2_BIND_DB_REPLY_EX(kBindRpExCentral083, 0x083, &ReplyLoadWarehouseQuestItemList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral085, 0x085, &ReplyAddPostFriend);
L2_BIND_DB_REPLY_EX(kBindRpExCentral086, 0x086, &ReplyLoadPostFriend);
L2_BIND_DB_REPLY_EX(kBindRpExCentral09A, 0x09a, &DBPacketExHandler::ReplyLoadReservedAnnounce);
L2_BIND_DB_REPLY_EX(kBindRpExCentral09B, 0x09b, &DBPacketExHandler::ReplyAddReservedAnnounce);
L2_BIND_DB_REPLY_EX(kBindRpExCentral09C, 0x09c, &DBPacketExHandler::ReplyDeleteReservedAnnounce);
L2_BIND_DB_REPLY_EX(kBindRpExCentral09D, 0x09d, &DBPacketExHandler::ReplyLoadEventData);
L2_BIND_DB_REPLY_EX(kBindRpExCentral09E, 0x09e, &DBPacketExHandler::ReplyReloadEventData);
L2_BIND_DB_REPLY_EX(kBindRpExCentral09F, 0x09f, &DBPacketExHandler::ReplyItemCheck);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0A0, 0x0a0, &ReplySendBirthdayMail);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0A1, 0x0a1, &ReplyDeleteBirthdayMail);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0A3, 0x0a3, &DynamicQuestManager::DBPacketHandler);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0A4, 0x0a4, &DBPacketExHandler::ReplyLoadOtherUserMacroEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0A5, 0x0a5, &DBPacketExHandler::ReplyLoadOtherBuilderBookmarkEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0A6, 0x0a6, &DBPacketExHandler::ReplyUpdateBlockMemoEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0A7, 0x0a7, &DBPacketExHandler::ReplyUpdateFriendMemoEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0A8, 0x0a8, &DBPacketExHandler::ResponseCommissionTradeLoadInfo);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0A9, 0x0a9, &DBPacketExHandler::ResponseCommissionTradeLoadAll);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0AA, 0x0aa, &DBPacketExHandler::ResponseCommissionTradeRegister);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0AB, 0x0ab, &DBPacketExHandler::ResponseCommissionTradeDelete);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0AC, 0x0ac, &DBPacketExHandler::ResponseCommissionTradeBuyItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0AD, 0x0ad, &DBPacketExHandler::ResponseCommissionTradeRegisteredList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0AE, 0x0ae, &DBPacketExHandler::ResponseCommissionTradeSearchList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0AF, 0x0af, &DBPacketExHandler::ResponseCommissionTradeRegTotalCount);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0B0, 0x0b0, &CInstantAgitManager::DBPacketHandler);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0B1, 0x0b1, &DBPacketExHandler::ReplyCheckCharName);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0B2, 0x0b2, &DBPacketExHandler::ReplyAgathionElapsedTimeAck);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0B3, 0x0b3, &DBPacketExHandler::ReplySaveDailyPCCafePoint);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0B4, 0x0b4, &DBPacketExHandler::DeleteBuffByAdmin);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0B5, 0x0b5, &DBPacketExHandler::LoadTimeToGiveKarmaPoint);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0B6, 0x0b6, &DBPacketExHandler::ResponseChangeAttributeItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0B7, 0x0b7, &ReplyMakeSentPostList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0B8, 0x0b8, &DBPacketExHandler::ResponseCancelDualClass);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0B9, 0x0b9, &DBPacketExHandler::ResponseBrCanBuyShopItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0BA, 0x0ba, &DBPacketExHandler::ResponseBrSendShopGiftMail);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0BB, 0x0bb, &DBPacketExHandler::ResponseGetDailyCount);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0BC, 0x0bc, &DBPacketExHandler::ResponseLoadMenteeListEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0BE, 0x0be, &DBPacketExHandler::ResponseLoadMentorPenaltytEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0BD, 0x0bd, &DBPacketExHandler::ResponseLoadMentorEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0C8, 0x0c8, &DBPacketExHandler::ResponseAddNewMenteeEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0C9, 0x0c9, &DBPacketExHandler::ResponseUdateMentorStatusEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0D8, 0x0d8, &DBPacketExHandler::ResponseLoadMenteeStateEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0CA, 0x0ca, &DBPacketExHandler::ResponseDeleteMacroAllEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0BF, 0x0bf, &DBPacketExHandler::ResponseShapeShiftItemEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0C0, 0x0c0, &DBPacketExHandler::ResponseShapeShiftItemFixedEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0C1, 0x0c1, &DBPacketExHandler::ResponseRestoreShapeShiftItemEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0C2, 0x0c2, &DBPacketExHandler::ResponseExpireShapeShiftItemEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0C3, 0x0c3, &DBPacketExHandler::ResponseLoadPledgeGameEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0C4, 0x0c4, &DBPacketExHandler::ResponseStartPledgeGameEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0C5, 0x0c5, &DBPacketExHandler::ResponseEndPledgeGameEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0C6, 0x0c6, &DBPacketExHandler::ResponseUpdatePledgeGameResultEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0C7, 0x0c7, &DBPacketExHandler::ResponseGetPledgeGameRecordEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0CB, 0x0cb, &DBPacketExHandler::ResponseMPCCTelBookmarkEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0CC, 0x0cc, &DBPacketExHandler::ResponseLoadKalieEventEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0CD, 0x0cd, &DBPacketExHandler::ResponseLoadKalieEventUserEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0CE, 0x0ce, &DBPacketExHandler::ResponseUpdateKalieEventEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0CF, 0x0cf, &DBPacketExHandler::ResponseUpdateKalieEventUserEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral110, 0x110, &DBPacketExHandler::ResponseLoadBalthusEventUserInfoEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0D2, 0x0d2, &DBPacketExHandler::ResponseLoadExDropCountEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0D4, 0x0d4, &DBPacketExHandler::ResponseBeautyLoad);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0D5, 0x0d5, &DBPacketExHandler::ResponseBuyBeautyItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0D7, 0x0d7, &DBPacketExHandler::ResponseResetBeautyList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0D6, 0x0d6, &DBPacketExHandler::ResponseResetBeautyItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0D9, 0x0d9, &DBPacketExHandler::ReplyCancelEquipmentAddedEffectAndDropItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0D3, 0x0d3, &DBPacketExHandler::ReplySetPledgeEmblemAck);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0DA, 0x0da, &DBPacketExHandler::ResponseBrInterestedProduct);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0DB, 0x0db, &DBPacketExHandler::ReplyPeriodicShapeShiftedItemJob);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0DC, 0x0dc, &DBPacketExHandler::ReplyPledgeLoadOnPledgeRecruitBoard);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0DD, 0x0dd, &DBPacketExHandler::ReplyPledgeNotifyTableInit);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0DE, 0x0de, &DBPacketExHandler::ReplySearchPledgeRecruitBoard);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0DF, 0x0df, &DBPacketExHandler::ReplyPledgeOnPledgeRecruitBoard);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0E0, 0x0e0, &DBPacketExHandler::ReplyPledgeWaitingUserList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0E1, 0x0e1, &DBPacketExHandler::ReplyUserOnPledgeWaitingUserList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0E2, 0x0e2, &DBPacketExHandler::ReplyPledgeDraftUserList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0E3, 0x0e3, &DBPacketExHandler::ReplyApplyInfoOnPledgeWaitingList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0E4, 0x0e4, &DBPacketExHandler::ReplyPledgeRecruitApplyInfo);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0E5, 0x0e5, &DBPacketExHandler::ReplyPledgeJoinSystemCode);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0E6, 0x0e6, &DBPacketExHandler::ReplyPetitionExternalNotify);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0E7, 0x0e7, &DBPacketExHandler::ReplyLoadAccountMaxLevel);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0E8, 0x0e8, &DBPacketExHandler::ReplySaveSummonInfectedSkills);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0E9, 0x0e9, &DBPacketExHandler::ReplyDeleteSummon);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0EA, 0x0ea, &DBPacketExHandler::ReplySavePet);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0EB, 0x0eb, &DBPacketExHandler::ReplyCharacterSavePacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0EC, 0x0ec, &DBPacketExHandler::ReplyRegisterXmasWish);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0ED, 0x0ed, &DBPacketExHandler::ReplyConfirmXmasWish);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0EE, 0x0ee, &DBPacketExHandler::ReplyGetDormantGiftInfo);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0EF, 0x0ef, &DBPacketExHandler::ReplyClearPledgeTimeTable);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0F0, 0x0f0, &DBPacketExHandler::NewManipulateItemPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0F1, 0x0f1, &DBPacketExHandler::TradeDivideAdena);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0F2, 0x0f2, &DBPacketExHandler::ResponseBeautyPurchaseList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0F3, 0x0f3, &DBPacketExHandler::ReplyAcquirePotentialSkill);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0F4, 0x0f4, &DBPacketExHandler::AcquiredAbilitySkillList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0F5, 0x0f5, &DBPacketExHandler::ResetAbilitySkill);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0F6, 0x0f6, &DBPacketExHandler::Reply_RP_EX_CHANGE_AP);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0F7, 0x0f7, &DummyPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0F8, 0x0f8, &DBPacketExHandler::ReplyStartTrainingZone);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0F9, 0x0f9, &DBPacketExHandler::ReplyEndTrainingZone);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0FA, 0x0fa, &DBPacketExHandler::ReplyRewardTrainingZone);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0FB, 0x0fb, &DBPacketExHandler::ReplyLoadTrainingZone);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0FC, 0x0fc, &DBPacketExHandler::ReplyPeriodicHennaJob);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0FD, 0x0fd, &DBPacketExHandler::ReplySetQuest);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0FE, 0x0fe, &DBPacketExHandler::ReplyAccountStamp);
L2_BIND_DB_REPLY_EX(kBindRpExCentral0FF, 0x0ff, &DBPacketExHandler::ReplyLoadWolrdChatPoint);
L2_BIND_DB_REPLY_EX(kBindRpExCentral100, 0x100, &DBPacketExHandler::ReplyLoadWolrdChatTime);
L2_BIND_DB_REPLY_EX(kBindRpExCentral101, 0x101, &DBPacketExHandler::ReplyLoadMatchingPoint);
L2_BIND_DB_REPLY_EX(kBindRpExCentral102, 0x102, &DBPacketExHandler::ReplyCreateDualJob);
L2_BIND_DB_REPLY_EX(kBindRpExCentral103, 0x103, &DBPacketExHandler::ReplyPremiumKamarokaRound);
L2_BIND_DB_REPLY_EX(kBindRpExCentral104, 0x104, &DBPacketExHandler::ReplyPremiumKamarokaRankingList);
L2_BIND_DB_REPLY_EX(kBindRpExCentral105, 0x105, &DBPacketExHandler::ReplyPremiumKamarokaAddNewRecord);
L2_BIND_DB_REPLY_EX(kBindRpExCentral106, 0x106, &DBPacketExHandler::ReplyPremiumKamarokaPersonalRankInfo);
L2_BIND_DB_REPLY_EX(kBindRpExCentral107, 0x107, &DBPacketExHandler::ReplyLoadClientINI);
L2_BIND_DB_REPLY_EX(kBindRpExCentral108, 0x108, &DBPacketExHandler::ReplyLoadHonorsSkill);
L2_BIND_DB_REPLY_EX(kBindRpExCentral109, 0x109, &DBPacketExHandler::ResponseUpdateWeddingEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral10A, 0x10a, &DBPacketExHandler::ResponseLoadWeddingEx);
L2_BIND_DB_REPLY_EX(kBindRpExCentral10B, 0x10b, &DBPacketExHandler::ReplyLoadFriendDetailInfo);
L2_BIND_DB_REPLY_EX(kBindRpExCentral10C, 0x10c, &DBPacketExHandler::ReplyLoadDroidNPCAppearance);
L2_BIND_DB_REPLY_EX(kBindRpExCentral10D, 0x10d, &DBPacketExHandler::Reply_RP_EX_SIGNIN_PLEDGE);
L2_BIND_DB_REPLY_EX(kBindRpExCentral10F, 0x10f, &DBPacketExHandler::ReplyUpdateFactionPoint);
L2_BIND_DB_REPLY_EX(kBindRpExCentral10E, 0x10e, &DBPacketExHandler::ReplyLoadFactionPoint);
L2_BIND_DB_REPLY_EX(kBindRpExCentral111, 0x111, &DBPacketExHandler::ReplyLoadUserInzonePoint);
L2_BIND_DB_REPLY_EX(kBindRpExCentral112, 0x112, &DBPacketExHandler::Reply_RP_EX_WOLYMPIAD_LOAD_ALL_USER);
L2_BIND_DB_REPLY_EX(kBindRpExCentral114, 0x114, &DBPacketExHandler::Reply_RP_EX_WOLYMPIAD_GET_MATCH_COUNT);
L2_BIND_DB_REPLY_EX(kBindRpExCentral116, 0x116, &DBPacketExHandler::Reply_RP_EX_WOLYMPIAD_GET_MATCH_RESULT);
L2_BIND_DB_REPLY_EX(kBindRpExCentral117, 0x117, &DBPacketExHandler::Reply_RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD);
L2_BIND_DB_REPLY_EX(kBindRpExCentral113, 0x113, &DummyPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral115, 0x115, &DummyPacket);
L2_BIND_DB_REPLY_EX(kBindRpExCentral118, 0x118, &DBPacketExHandler::ResponseLoadOneDayRewardInfo);
L2_BIND_DB_REPLY_EX(kBindRpExCentral119, 0x119, &DBPacketExHandler::ResponseLoadAutoSoulShotUI);
L2_BIND_DB_REPLY_EX(kBindRpExCentral11A, 0x11a, &DBPacketExHandler::ResponseLoadUserPledgeBonus);
L2_BIND_DB_REPLY_EX(kBindRpExCentral11B, 0x11b, &DBPacketExHandler::ReplyLoadUserMonsterBook);
L2_BIND_DB_REPLY_EX(kBindRpExCentral11C, 0x11c, &DBPacketExHandler::ResponseUpgradeSystem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral12E, 0x12e, &DBPacketExHandler::ResponseLockedItem);
L2_BIND_DB_REPLY_EX(kBindRpExCentral11D, 0x11d, &DBPacketExHandler::ReplyLoadCardUpdownGameRecord);
L2_BIND_DB_REPLY_EX(kBindRpExCentral11E, 0x11e, &DBPacketExHandler::ReplyUpdateCardUpdownGameRecord);
L2_BIND_DB_REPLY_EX(kBindRpExCentral11F, 0x11f, &DBPacketExHandler::Reply_RP_EX_SEND_SYSTEM_MESSAGE);
L2_BIND_DB_REPLY_EX(kBindRpExCentral120, 0x120, &DBPacketExHandler::Reply_RP_EX_PLEDGE_MANIPULATE_ITEMS);
L2_BIND_DB_REPLY_EX(kBindRpExCentral121, 0x121, &ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO>);
L2_BIND_DB_REPLY_EX(kBindRpExCentral122, 0x122, &ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK>);
L2_BIND_DB_REPLY_EX(kBindRpExCentral123, 0x123, &ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_ITEM_LOAD>);
L2_BIND_DB_REPLY_EX(kBindRpExCentral124, 0x124, &ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_ITEM_SAVE>);
L2_BIND_DB_REPLY_EX(kBindRpExCentral125, 0x125, &ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_ITEM_DELETE>);
L2_BIND_DB_REPLY_EX(kBindRpExCentral126, 0x126, &ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_RAID_GET_INFO_LIST>);
L2_BIND_DB_REPLY_EX(kBindRpExCentral127, 0x127, &ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON>);
L2_BIND_DB_REPLY_EX(kBindRpExCentral128, 0x128, &ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_RAID_INZONE_LOAD>);
L2_BIND_DB_REPLY_EX(kBindRpExCentral129, 0x129, &ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_MISSION_LOAD>);
L2_BIND_DB_REPLY_EX(kBindRpExCentral12A, 0x12a, &ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_ANNOUNCE>);
L2_BIND_DB_REPLY_EX(kBindRpExCentral12B, 0x12b, &ProcessAtomicJobPacket<protocol::RP_EX_PLEDGE_ANNOUNCE_SET>);
L2_BIND_DB_REPLY_EX(kBindRpExCentral130, 0x130, &DBPacketExHandler::Reply_RP_EX_PLEDGE_MANIPULATE_ITEMS_NEW);
L2_BIND_DB_REPLY_EX(kBindRpExCentral12C, 0x12c, &DBPacketExHandler::Reply_RP_EX_SEQUENCE_JOB);
L2_BIND_DB_REPLY_EX(kBindRpExCentral12D, 0x12d, &DBPacketExHandler::Reply_RP_EX_BUILDER_COMMAND);
L2_BIND_DB_REPLY_EX(kBindRpExCentral12F, 0x12f, &DBPacketExHandler::Reply_RP_EX_LOAD_ITEM_REUSE_DELAY);
L2_BIND_DB_REPLY_EX(kBindRpExCentral131, 0x131, &DBPacketExHandler::Reply_RP_EX_LOAD_RESERVATION_ANNOUNCE);
L2_BIND_DB_REPLY_EX(kBindRpExCentral132, 0x132, &DBPacketExHandler::Reply_RP_EX_SHOW_RESERVATION_ANNOUNCE);
L2_BIND_DB_REPLY_EX(kBindRpExCentral133, 0x133, &DBPacketExHandler::Reply_RP_EX_REFRASH_RESERVATION_ANNOUNCE);
L2_BIND_DB_REPLY_EX(kBindRpExCentral002, 0x002, &ReplyHtmlBuilder_RP_MAIL_LIST_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral003, 0x003, &ReplyHtmlBuilder_RP_READ_MAIL_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral005, 0x005, &ReplyHtmlBuilder_RP_SEND_MAIL_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral006, 0x006, &ReplyHtmlBuilder_RP_SAVE_MAIL_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral008, 0x008, &ReplyHtmlBuilder_RP_GET_TEMP_MAIL_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral009, 0x009, &ReplyHtmlBuilder_RP_REPLY_MAIL_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral00A, 0x00a, &ReplyHtmlBuilder_RP_FORWARD_MAIL_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral00B, 0x00b, &ReplyHtmlBuilder_RP_MODIFY_TEMP_MAIL_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral00C, 0x00c, &ReplyHtmlBuilder_RP_MODIFY_SEND_TEMP_MAIL_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral00D, 0x00d, &ReplyHtmlBuilder_RP_DELETE_MAIL_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral00E, 0x00e, &ReplyHtmlBuilder_RP_ARCHIVE_MAIL_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral00F, 0x00f, &ReplyHtmlBuilder_RP_UNREAD_MAIL_COUNT_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral010, 0x010, &ReplyHtmlBuilder_RP_SEND_PLEDGE_MAIL_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral011, 0x011, &ReplyHtmlBuilder_RP_DELETE_FRIEND_BATCH_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral012, 0x012, &ReplyHtmlBuilder_RP_DELETE_BLOCK_BATCH_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral01B, 0x01b, &ReplyHtmlBuilder_RP_GET_PLEDGE_ANNOUNCE_EX);
L2_BIND_DB_REPLY_EX(kBindRpExCentral01C, 0x01c, &ReplyHtmlBuilder_RP_SET_PLEDGE_ANNOUNCE_EX);

#undef L2_BIND_DB_REPLY_EX

} // namespace

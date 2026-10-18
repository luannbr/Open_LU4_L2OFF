#include "Auction.h"
#include "CWorld.h"
#include "DB.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "NPC.h"
#include "ObjectDB.h"
#include "ParserAuctionData.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserPacketEx.h"
#include "UserSocket.h"


#include "Shared/src/EtcMani.h"
#include "Shared/src/Exception.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include "Shared/src/Runtime/RawAtomic.h"


#include <algorithm>
#include <array>
#include <atomic>
#include <ctime>
#include <new>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <sys/timeb.h>
#endif

namespace {

constexpr const char* kAuctionSource = "Auction.cpp";

#if defined(_WIN32)
constexpr const wchar_t* kAuctionDataPath = L"..\\Script\\auctiondata.txt";
#else
constexpr const wchar_t* kAuctionDataPath = L"../script/auctiondata.txt";
#endif

// Current CAuction RTTI has CObject/CIOObject at displacement zero. All
// verifier calls target the already recovered MemoryObject slots 0, 1 and 3.
// no CAuction field or unpublished tail method is accessed here.
::CIOObject* AsAuctionIoObject(CAuction* auction) noexcept { return reinterpret_cast<::CIOObject*>(auction); }

int StandardTimezoneMinutesWest() {
#if defined(_WIN32)
    __timeb64 timeBuffer{};
    if (::_ftime64_s(&timeBuffer) != 0) {
        return 0;
    }
    return static_cast<int>(timeBuffer.timezone);
#else
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    if (::gmtime_r(&now, &utc) == nullptr) {
        return 0;
    }
    // _timeb::timezone is the standard-time offset west of UTC. Force the
    // UTC tuple through mktime with DST disabled to keep that contract.
    utc.tm_isdst = 0;
    const std::time_t utcAsLocal = std::mktime(&utc);
    if (utcAsLocal == static_cast<std::time_t>(-1)) {
        return 0;
    }
    return static_cast<int>(std::difftime(utcAsLocal, now) / 60.0);
#endif
}

CNPC* GetUniqueNPC(const int npcClassId) {
    // Current L2Server 0x00497c2c.
    L2_GUARD_FUNCTION();
    return g_UniqNPCDB.GetUniqNPC(0, runtime::AddInt32Wrapping(npcClassId, 1000000));
}

CSPointer<User> FindLocalAuctionUser(const int userDatabaseId) {
    return g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
}

void AssembleAuctionItemInfo(CItem& item, const std::int64_t amount, CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x0049c21c.
    L2_GUARD_FUNCTION();
    CItemAssemblerAmount assembler(item, amount);
    assembler.AssembleItemInfo(packet);
}

const wchar_t* AuctionAccountName(const std::array<char16_t, 15>& source, std::array<wchar_t, 16>& storage) {
#if defined(_WIN32)
    static_cast<void>(storage);
    return reinterpret_cast<const wchar_t*>(source.data());
#else
    std::size_t length = 0;
    while (length < source.size() && source[length] != u'\0') {
        ++length;
    }
    const std::wstring converted = ::Utf16ToWide(std::u16string_view(source.data(), length));
    const std::size_t copyLength = std::min(converted.size(), storage.size() - 1U);
    std::copy_n(converted.data(), copyLength, storage.data());
    storage[copyLength] = L'\0';
    return storage.data();
#endif
}

void CopyAuctionCharacterName(const wchar_t* const source, std::array<wchar_t, 25>& destination) {
    if (source == nullptr) {
        destination[0] = L'\0';
        return;
    }
    const std::size_t sourceLength = std::wcslen(source);
    const std::size_t copyLength = std::min(sourceLength, destination.size() - 1U);
    std::copy_n(source, copyLength, destination.data());
    destination[copyLength] = L'\0';
}

} // namespace

CAuction::CAuction(
    const int auctionDatabaseId,
    const int npcClassId,
    const std::int32_t startTime,
    const int duration,
    const AuctionStatus status,
    const AuctionExtendStatus extendStatus,
    const AuctionItem& item,
    const std::int64_t initialPrice
) :
    m_nAuctionDBID(auctionDatabaseId),
    m_nNPCClassID(npcClassId),
    m_tStartTime(startTime),
    m_nDuration(duration),
    m_eStatus(status),
    m_eExtendStatus(extendStatus),
    m_AuctionItem(item),
    m_nInitPrice(initialPrice),
    m_nMaxBidderDBID(0),
    m_nPrevMaxBidderDBID(0),
    m_nCurrentPrice(0),
    m_nAuctionDBJobCount(0),
    m_nLastBiddingTick(0),
    m_nRealDone(1),
    m_bHalfHourBroadcasting(false),
    m_nAuctionTransactionID(0),
    m_nReturnedTransactionID(-1) {
    OnCreate();
}

void CAuction::OnCreate() {
    // Current L2Server 0x00497aac.
    L2_GUARD_FUNCTION();
    g_ServerContext.Auction.fetch_add(1, std::memory_order_seq_cst);
}

CAuction::~CAuction() { g_ServerContext.Auction.fetch_sub(1, std::memory_order_seq_cst); }

L2_DEFINE_OBJECT_CLASS_ID(CAuction)

L2_DEFINE_OBJECT_CLASS_NAME(CAuction, m_pClassName_CAuction)

L2_DEFINE_OBJECT_ALLOCATOR(CAuction, __GetClassName())

L2_DEFINE_OBJECT_NEW(CAuction)

L2_DEFINE_OBJECT_TYPED_RELEASE(CAuction)

int CAuction::GetIndex() const { return static_cast<int>(Id() & 0xfffffU); }

CObject* CAuction::Copy() { return nullptr; }

void CAuction::Delete() {
    L2_GUARD_FUNCTION();
    m_mapBidder.clear();
    m_Deleted = 1;
    static_cast<void>(new CAuctionReleaseVerifier(this));
    SetDelete();
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

FVector CAuction::GetPos() const { return FVector{}; }

void CAuction::SetAuctionStatus(const AuctionStatus status) {
    L2_GUARD_FUNCTION();
    m_eStatus = status;
    db.RequestSetAuctionStatus(m_nAuctionDBID, static_cast<int>(status));
}

void CAuction::SetAuctionItemInfo(const AuctionItem& item, const int initialPrice) {
    // Current L2Server 0x00497d8c.
    L2_GUARD_FUNCTION();
    m_AuctionItem = item;
    m_nInitPrice = static_cast<std::int64_t>(initialPrice);
    db.RequestSetAuctionItem(m_nAuctionDBID, item.m_nItemIndex);
    db.RequestUpdateItemAuctionInfoForL2Assist(
        m_nAuctionDBID, item.m_nClassID, static_cast<std::int64_t>(initialPrice), item.m_nAmount, 0, 0, 0, 0
    );
}

void CAuction::SetAuctionDuration(const int duration) {
    L2_GUARD_FUNCTION();
    m_nDuration = duration;
    db.RequestSetEndTime(m_nAuctionDBID, m_tStartTime + duration, static_cast<int>(m_eExtendStatus));
}

void CAuction::LoadBid() {
    // Current L2Server 0x00497f64.
    L2_GUARD_FUNCTION();
    db.RequestLoadAllAuctionBid(m_nAuctionDBID);
}

void CAuction::InitBid(const int bidCount, const unsigned char* packet) {
    // Current L2Server 0x0049b3c4.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        if (bidCount > 0) {
            std::uint32_t remaining = static_cast<std::uint32_t>(bidCount);
            do {
                int bidderDatabaseId;
                std::int64_t biddingPrice;
                packet = ::Disassemble(packet, "dQ", &bidderDatabaseId, &biddingPrice);
                m_mapBidder.emplace(bidderDatabaseId, biddingPrice);
                if (m_nCurrentPrice <= biddingPrice && m_eStatus == AS_DOING) {
                    m_nMaxBidderDBID = bidderDatabaseId;
                    m_nCurrentPrice = biddingPrice;
                }
                --remaining;
            } while (remaining != 0);
        }
    }
}

void CAuction::InitBidDone() {
    // Current L2Server 0x0049800c.
    L2_GUARD_FUNCTION();
    AddTimer(1000, 0);
}

int CAuction::GetAuctionRemainTime() const {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const std::int32_t end = m_tStartTime + m_nDuration;
    if (now < m_tStartTime || end < now) {
        return 0;
    }
    return end - now;
}

std::int64_t CAuction::GetBidFailureMoney(const int userDatabaseId) const {
    // Current L2Server 0x004997e8.
    L2_GUARD_FUNCTION();
    std::int64_t failureMoney = 0;
    {
        L2_GUARD(m_Lock);
        const auto position = m_mapBidder.find(userDatabaseId);
        if (position != m_mapBidder.end() && userDatabaseId != m_nMaxBidderDBID) {
            failureMoney = position->second;
        }
    }
    return failureMoney;
}

wchar_t* CAuction::GetStateName() const {
    // Current L2Server 0x004980b8.
    L2_GUARD_FUNCTION();
    switch (m_eStatus) {
        case AS_NONE:
            return const_cast<wchar_t*>(L"None");
        case AS_DOING:
            return const_cast<wchar_t*>(L"doing");
        case AS_DONE:
            return const_cast<wchar_t*>(L"done");
        default:
            return const_cast<wchar_t*>(L"error");
    }
}

void CAuction::SendAuctionState(CUserSocket& socket) const {
    // Current L2Server 0x004986e8.
    L2_GUARD_FUNCTION();

    const int maxBidderDatabaseId = m_nMaxBidderDBID;
    const std::int64_t initialPrice = m_nInitPrice;
    const std::int64_t currentPrice = std::max(m_nCurrentPrice, initialPrice);
    const AuctionItem auctionItem = m_AuctionItem;
    const int itemIndex = auctionItem.m_nItemIndex;
    const int sellerDatabaseId = auctionItem.m_nOwnerDBID;
    const int auctionDatabaseId = m_nAuctionDBID;
    wchar_t* const stateName = GetStateName();

    std::array<char16_t, 1024> message{};
    FormatUtf16To(
        message.data(), message.size(),
        u"ID[%d], Seller[%d], Index[%d], InitPrice[%I64d], "
        u"CurrentPrice[%I64d]>, User[%d], State[%s]",
        auctionDatabaseId, sellerDatabaseId, itemIndex, static_cast<long long>(initialPrice),
        static_cast<long long>(currentPrice), maxBidderDatabaseId, stateName
    );
    socket.SendSystemMessage(u"SYS", message.data());
}

bool CAuction::Bid(User* const user, const std::int64_t biddingPrice) {
    // Current L2Server 0x0049d8bc, original owner Auction.cpp.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    std::vector<CAuction*> auctions;
    CAuctionManager::inst().GetAuctions(m_AuctionItem.m_nOwnerDBID, auctions);
    for (CAuction* const auction : auctions) {
        if (auction != this && auction->GetBidFailureMoney(static_cast<int>(user->GetDBID())) > 0) {
            user->SendSystemMessage(SystemMessageId::CANNOT_BID_UNCLAIMED_BID_FUNDS_REMAIN);
            return false;
        }
    }

    if (m_eStatus != AS_DOING) {
        user->SendSystemMessage(SystemMessageId::OUTSIDE_AUCTION_PERIOD);
        return false;
    }
    if (biddingPrice >= INT64_C(1000000000000)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_BID_HIGHEST_BID_EXCEEDS_LIMIT);
        return false;
    }

    int transactionId = 0;
    {
        L2_GUARD(m_Lock);
        if (GetBidFailureMoney(static_cast<int>(user->GetDBID())) > 0 ||
            static_cast<int>(user->GetDBID()) == m_nMaxBidderDBID) {
            user->SendSystemMessage(SystemMessageId::ALREADY_HIGHEST_BIDDER_OR_HAS_UNCLAIMED_BID_FUNDS);
            return false;
        }
        if (m_nCurrentPrice >= biddingPrice || m_nInitPrice >= biddingPrice) {
            user->SendSystemMessage(SystemMessageId::BID_MUST_EXCEED_CURRENT_HIGHEST);
            return false;
        }

        {
            L2_READ_GUARD(user->m_inv);
            const std::int64_t adenaAmount = user->m_inv.GetAdenaAmount();
            if (adenaAmount < biddingPrice) {
                user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA_FOR_BID);
                return false;
            }
        }

        transactionId = m_nAuctionTransactionID;
        m_nAuctionTransactionID = runtime::AddInt32Wrapping(m_nAuctionTransactionID, 1);
    }

    runtime::raw_atomic::IncrementI32(&m_nAuctionDBJobCount);
    m_nLastBiddingTick = runtime::TickCount32();

    std::array<ItemManip, 100> manipulations{};
    manipulations[0].mClassId = 0x39;
    manipulations[0].mCount = runtime::NegateInt64Wrapping(biddingPrice);

    EtcManiCreateItemAuctionBid createBid;
    createBid.m_nUserSid = user->Id();
    createBid.m_nUserDBID = user->GetDBID();
    createBid.m_nAuctionID = m_nAuctionDBID;
    createBid.m_nBidID = 0;
    createBid.m_nBiddingPrice = biddingPrice;
    createBid.m_nTransactionID = transactionId;

    CPacketHelperEx<20480> packet;
    createBid.Assemble(packet);

    L2_GUARD(user->m_ManipulateQueueLock);
    auto message = user->SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    auto queue = user->SaveManipulateQueue(
        ManipulateType::MANI_AUCTION, 0, 1, manipulations.data(), nullptr, 1, packet, false, ItemGetReason::Etc, message
    );
    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        return user->_HandleItemManipulation(queue);
    }
    if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        runtime::raw_atomic::DecrementI32(&m_nAuctionDBJobCount);
        return false;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     kAuctionSourceWide,
        //     0x210)
        // Noise: A bid accepted into the deferred item-manipulation queue is normal. Completion is reported by the existing
        // bid/reply flow. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     kAuctionSourceWide,
        //     0x210)
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     kAuctionSourceWide,
    //     0x215)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     kAuctionSourceWide,
    //     0x215)
    L2_LOG_ERROR(
        Log,
        "Cannot queue bid for auction %d, character DB ID %u, transaction %d: unexpected item-job result %d; the request "
        "returns failure.",
        m_nAuctionDBID, static_cast<unsigned>(createBid.m_nUserDBID), transactionId, static_cast<int>(result)
    );
    runtime::raw_atomic::DecrementI32(&m_nAuctionDBJobCount);
    return false;
}

bool CAuction::WithdrawBiddingPrice(User* const user) {
    // Current L2Server 0x0049e1a8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    std::int64_t failureMoney = 0;
    {
        L2_GUARD(m_Lock);
        failureMoney = GetBidFailureMoney(user->GetDBID());
        if (failureMoney == 0) {
            user->SendSystemMessage(SystemMessageId::NO_UNCLAIMED_BID_FUNDS);
            return false;
        }

        std::int64_t adenaAmount;
        {
            L2_READ_GUARD(user->m_inv);
            adenaAmount = user->m_inv.GetAdenaAmount();
        }
        if (runtime::AddInt64Wrapping(adenaAmount, failureMoney) >= INT64_C(1000000000000)) {
            user->SendSystemMessage(SystemMessageId::INVENTORY_ADENA_LIMIT_EXCEEDED);
            return false;
        }
    }

    runtime::raw_atomic::IncrementI32(&m_nAuctionDBJobCount);
    m_nLastBiddingTick = runtime::TickCount32();

    std::array<ItemManip, 100> manipulations{};
    manipulations[0].mClassId = 0x39;
    manipulations[0].mCount = failureMoney;

    const int auctionDatabaseId = m_nAuctionDBID;
    const unsigned int userDatabaseId = user->GetDBID();
    EtcManiWithdrawItemAuctionBid withdrawBid;
    withdrawBid.m_nBidID = 0;
    withdrawBid.m_nUserDBID = userDatabaseId;
    withdrawBid.m_nAuctionID = auctionDatabaseId;
    withdrawBid.m_nAdenaAmount = failureMoney;
    withdrawBid.m_bLog = true;

    CPacketHelperEx<20480> packet;
    withdrawBid.Assemble(packet);

    L2_GUARD(user->m_ManipulateQueueLock);
    auto message = user->SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    auto queue = user->SaveManipulateQueue(
        ManipulateType::MANI_AUCTION, 0, 1, manipulations.data(), nullptr, 1, packet, false, ItemGetReason::Etc, message
    );
    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        return user->_HandleItemManipulation(queue);
    }
    if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        runtime::raw_atomic::DecrementI32(&m_nAuctionDBJobCount);
        return false;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     kAuctionSourceWide,
        //     0x347)
        // Noise: A refund accepted into the deferred item-manipulation queue is normal. This is not a completed refund or an
        // error. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     kAuctionSourceWide,
        //     0x347)
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     kAuctionSourceWide,
    //     0x34c)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     kAuctionSourceWide,
    //     0x34c)
    L2_LOG_ERROR(
        Log,
        "Cannot queue refund for auction %d, character DB ID %u, amount %lld adena: unexpected item-job result %d; the "
        "request returns failure.",
        auctionDatabaseId, static_cast<unsigned>(userDatabaseId), static_cast<long long>(failureMoney),
        static_cast<int>(result)
    );
    runtime::raw_atomic::DecrementI32(&m_nAuctionDBJobCount);
    return false;
}

bool CAuction::ShowAuctionUI(User* const user, const bool fromNpc) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    int remainTime = GetAuctionRemainTime();
    if (remainTime < 0) {
        remainTime = 0;
    }

    CObject* const object = g_ObjectDB.GetObjectW(m_AuctionItem.m_nClassID, false);
    if (object == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalide item. item class[%d]",
        //     m_AuctionItem.m_nClassID)
        // L2_LOG_ERROR(Log,
        //     L"invalide item. item class[%d]",
        //     m_AuctionItem.m_nClassID)
        L2_LOG_ERROR(
            Log, "Cannot show auction %d: item prototype for class %d was not found; the current-auction UI is not sent.",
            m_nAuctionDBID, m_AuctionItem.m_nClassID
        );
        return false;
    }

    CPacketHelperEx<20480> packet;
    const std::int64_t price = std::max(m_nCurrentPrice, m_nInitPrice);
    packet.Assemble("cdQd", fromNpc ? 1 : 0, m_nAuctionDBID, static_cast<long long>(price), remainTime);
    AssembleAuctionItemInfo(*static_cast<CItem*>(object), m_AuctionItem.m_nAmount, packet);
    user->GetSocket()->Send("chb", 0xfe, 0x69, packet.GetLength(), packet.GetPacket());
    return true;
}

bool CAuction::ShowAuctionUIByNPC(CNPC* const npc, User* const user) {
    // Current L2Server 0x0049ef38.
    L2_GUARD_FUNCTION();
    if (npc == nullptr || user == nullptr) {
        return false;
    }
    TalkChannelCache::inst().mChannels[9].Cache(user->Id(), npc->Id(), 0);
    return ShowAuctionUI(user, true);
}

bool CAuction::ShowNextAuctionUI(User* const user) {
    // Current L2Server 0x0049ea0c. The target evaluates
    // GetNextStartTime twice. Preserve that observable call shape.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    CAuctionNPC* const auctionNpc = CAuctionNPCDB::inst().GetAuctionNPC(m_nNPCClassID);
    if (auctionNpc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid auction NPC[%d]",
        //     kAuctionSourceWide,
        //     0x413,
        //     m_nNPCClassID)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid auction NPC[%d]",
        //     kAuctionSourceWide,
        //     0x413,
        //     m_nNPCClassID)
        L2_LOG_ERROR(
            Log,
            "Cannot show the next auction for auction %d: NPC class %d has no auction-controller definition; the UI is not "
            "sent.",
            m_nAuctionDBID, m_nNPCClassID
        );
        return false;
    }

    CPacketHelperEx<20480> packet;
    const std::int32_t nextStartTime = auctionNpc->GetNextStartTime();
    if (auctionNpc->CanShowNextItemAuction()) {
        const NPCAuctionItem nextItem = auctionNpc->PickNextAuctionItem();
        CObject* const object = g_ObjectDB.GetObjectW(nextItem.m_nItemType, false);
        if (object == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid auction Item type[%d]",
            //     kAuctionSourceWide,
            //     0x425,
            //     nextItem.m_nItemType)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid auction Item type[%d]",
            //     kAuctionSourceWide,
            //     0x425,
            //     nextItem.m_nItemType)
            L2_LOG_ERROR(
                Log,
                "Cannot show the next auction for NPC class %d: item prototype for class %d was not found; the UI is not "
                "sent.",
                m_nNPCClassID, nextItem.m_nItemType
            );
            return false;
        }
        packet.Assemble("Qd", static_cast<long long>(nextItem.m_nItemPrice), nextStartTime);
        AssembleAuctionItemInfo(*static_cast<CItem*>(object), nextItem.m_nItemAmount, packet);
    } else {
        packet.Assemble("Qd", static_cast<long long>(-1), nextStartTime);
    }
    user->GetSocket()->Send("chb", 0xfe, 0x1c3, packet.GetLength(), packet.GetPacket());
    return true;
}

bool CAuction::ShowNextAuctionUIByNPC(CNPC* const npc, User* const user) {
    // Current L2Server 0x0049f020.
    L2_GUARD_FUNCTION();
    if (npc == nullptr || user == nullptr) {
        return false;
    }
    TalkChannelCache::inst().mChannels[9].Cache(user->Id(), npc->Id(), 0);
    return ShowNextAuctionUI(user);
}

bool CAuction::DeleteBid(const int userDatabaseId) {
    L2_GUARD_FUNCTION();
    bool erased;
    {
        L2_GUARD(m_Lock);
        const auto found = m_mapBidder.find(userDatabaseId);
        erased = found != m_mapBidder.end();
        if (erased) {
            m_mapBidder.erase(found);
        }
    }
    return erased;
}

bool CAuction::CreateBid(const int userDatabaseId, const std::int64_t biddingPrice, const int transactionId) {
    // Current L2Server.exe 0x0049ac5c.
    L2_GUARD_FUNCTION();

    std::int64_t notifyPrice = 0;
    int notifyUserDatabaseId = userDatabaseId;

    int returnedTransactionId;
    int previousTransactionId;
    int previousMaxBidderDatabaseId;
    std::int64_t previousMaxBidderPrice;
    {
        L2_GUARD(m_Lock);
        m_mapBidder[userDatabaseId] = biddingPrice;
        {
            CSPointer<User> bidder = FindLocalAuctionUser(userDatabaseId);
            if (bidder) {
                bidder->SendSystemMessage(SystemMessageId::ITEM_AUCTION_BID_PLACED);
            }
        }

        returnedTransactionId = m_nReturnedTransactionID;
        previousTransactionId = returnedTransactionId;
        if (transactionId > returnedTransactionId) {
            m_nReturnedTransactionID = transactionId;
            previousTransactionId = 0;
        }

        if (m_nCurrentPrice < biddingPrice) {
            notifyUserDatabaseId = m_nMaxBidderDBID;
            m_nPrevMaxBidderDBID = m_nMaxBidderDBID;
            m_nMaxBidderDBID = userDatabaseId;
            m_nCurrentPrice = biddingPrice;
            notifyPrice = biddingPrice;
        }

        ExpandAuctionDuration();
        previousMaxBidderDatabaseId = m_nPrevMaxBidderDBID;
        previousMaxBidderPrice = 0;
        const auto previousBid = m_mapBidder.find(previousMaxBidderDatabaseId);
        if (previousBid != m_mapBidder.end()) {
            previousMaxBidderPrice = previousBid->second;
        }
    }

    CSPointer<User> notifiedUser = FindLocalAuctionUser(notifyUserDatabaseId);
    if (notifiedUser) {
        protocol::S_EX_ITEM_AUCTION_UPDATED_BIDDING_INFO packet{};
        packet.nCurrentPrice = notifyPrice;
        notifiedUser->GetSocket()->Send(S_EX, S_EX_ITEM_AUCTION_UPDATED_BIDDING_INFO, packet);
        notifiedUser->SendSystemMessage_Q(SystemMessageId::OUTBID_NEW_HIGHEST_BID_AMOUNT, notifyPrice);
    }

    if (transactionId <= returnedTransactionId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CAuction::CreateBid... Transaction order mismatch "
        //     L"Tran|lastTran[%d|%d] a[%d]u[%d]",
        //     kAuctionSourceWide,
        //     0x26f,
        //     transactionId,
        //     previousTransactionId,
        //     m_nAuctionDBID,
        //     userDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CAuction::CreateBid... Transaction order mismatch "
        //     L"Tran|lastTran[%d|%d] a[%d]u[%d]",
        //     kAuctionSourceWide,
        //     0x26f,
        //     transactionId,
        //     previousTransactionId,
        //     m_nAuctionDBID,
        //     userDatabaseId)
        L2_LOG_ERROR(
            Log,
            "Out-of-order bid reply for auction %d, character DB ID %d: transaction %d <= previous returned transaction %d; "
            "bid state has already been updated and processing continues.",
            m_nAuctionDBID, userDatabaseId, transactionId, previousTransactionId
        );
    }

    CSPointer<User> bidder = FindLocalAuctionUser(userDatabaseId);
    std::uint32_t bidderServerId = 0;
    std::array<wchar_t, 25> characterName{};
    std::array<wchar_t, 16> accountNameStorage{};
    const wchar_t* accountName = L"";
    FVector position{};
    std::int64_t adenaAfterBidding = 0;
    if (bidder) {
        bidderServerId = bidder->GetServerId();
        CopyAuctionCharacterName(Utf16ToWide(bidder->GetName()).c_str(), characterName);
        accountName = AuctionAccountName(bidder->m_sAccountName, accountNameStorage);
        position = bidder->GetPos();
        {
            L2_READ_GUARD(bidder->m_inv);
            adenaAfterBidding = bidder->m_inv.GetAdenaAmount() - biddingPrice;
        }
        if (adenaAfterBidding < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] adena is insufficient. bidding[%I64d] "
            //     L"after bidding[%I64d], user[%s]",
            //     kAuctionSourceWide,
            //     0x293,
            //     biddingPrice,
            //     adenaAfterBidding,
            //     characterName.data())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] adena is insufficient. bidding[%I64d] "
            //     L"after bidding[%I64d], user[%s]",
            //     kAuctionSourceWide,
            //     0x293,
            //     biddingPrice,
            //     adenaAfterBidding,
            //     characterName.data())
            L2_LOG_ERROR(
                Log,
                u"Bid reply for auction %d, character '%ls' (DB ID %d): bid %lld adena gives a negative calculated balance "
                u"of %lld; processing continues.",
                m_nAuctionDBID, characterName.data(), userDatabaseId, static_cast<long long>(biddingPrice),
                static_cast<long long>(adenaAfterBidding)
            );
        }
        bidder->PledgeMissionCheckAndAddCount(static_cast<PledgeMission::GoalType>(0x27), 0, 1);
    }

    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,%I64d,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        u"%s,%s,,,",
        static_cast<std::int64_t>(0x3dd), static_cast<std::int64_t>(userDatabaseId),
        static_cast<std::int64_t>(bidderServerId), static_cast<std::int64_t>(previousMaxBidderDatabaseId),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)), static_cast<std::int64_t>(m_nAuctionDBID),
        static_cast<std::int64_t>(m_AuctionItem.m_nClassID), m_AuctionItem.m_nAmount, biddingPrice,
        static_cast<std::int64_t>(m_AuctionItem.m_nOwnerDBID), adenaAfterBidding, previousMaxBidderPrice,
        static_cast<std::int64_t>(transactionId), characterName.data(), accountName
    );
    return true;
}

bool CAuction::FinishAuction() {
    // Current L2Server.exe 0x0049d0bc.
    L2_GUARD_FUNCTION();
    if (m_eStatus != AS_DONE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Auction Status[%d] is Invalid in "
        //     L"CAuction::FinishAuction",
        //     kAuctionSourceWide,
        //     0x91,
        //     static_cast<int>(m_eStatus))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Auction Status[%d] is Invalid in "
        //     L"CAuction::FinishAuction",
        //     kAuctionSourceWide,
        //     0x91,
        //     static_cast<int>(m_eStatus))
        L2_LOG_ERROR(
            Log, "Cannot finish auction %d: status is %d, expected DONE (%d); finalization is skipped.", m_nAuctionDBID,
            static_cast<int>(m_eStatus), static_cast<int>(AS_DONE)
        );
        return false;
    }
    if (m_nAuctionDBID == 0 || m_AuctionItem.m_nOwnerDBID == 0) {
        return false;
    }

    CAuction* const auction = CAuctionManager::inst().GetAuction(m_nAuctionDBID);
    if (auction == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] cannot find auction [%d]",
        //     kAuctionSourceWide,
        //     0xa0,
        //     m_nAuctionDBID)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] cannot find auction [%d]",
        //     kAuctionSourceWide,
        //     0xa0,
        //     m_nAuctionDBID)
        L2_LOG_ERROR(
            Log, "Cannot finish auction %d: it is not registered in the auction manager; finalization is skipped.",
            m_nAuctionDBID
        );
        return false;
    }

    int winnerDatabaseId;
    std::int64_t winningPrice;
    {
        L2_GUARD(m_Lock);
        winnerDatabaseId = m_nMaxBidderDBID;
        winningPrice = std::max(m_nCurrentPrice, m_nInitPrice);
    }

    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,,,,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,"
        u",,,,,,,,,,",
        static_cast<std::int64_t>(0x3dc), static_cast<std::int64_t>(winnerDatabaseId),
        static_cast<std::int64_t>(m_nAuctionDBID), static_cast<std::int64_t>(m_AuctionItem.m_nClassID),
        m_AuctionItem.m_nAmount, winningPrice, static_cast<std::int64_t>(m_AuctionItem.m_nOwnerDBID)
    );
    if (winnerDatabaseId == 0) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"there is no bidder. auction[%d]",
        //     m_nAuctionDBID)
        // L2_LOG_INFO(Log,
        //     L"there is no bidder. auction[%d]",
        //     m_nAuctionDBID)
        L2_LOG_INFO(Log, "Auction %d ended without a winning bidder; no winner delivery is requested.", m_nAuctionDBID);
        return true;
    }

    {
        CSPointer<User> winner = FindLocalAuctionUser(winnerDatabaseId);
        if (winner) {
            winner->SendSystemMessage(SystemMessageId::ITEM_AUCTION_WON_COLLECT_FROM_WAREHOUSE);
        }
    }

    std::array<ItemManip, 100> manipulations{};
    CObject* const object = g_ObjectDB.GetObjectW(m_AuctionItem.m_nClassID, false);
    if (object == nullptr || !object->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     kAuctionSourceWide,
        //     0xcb,
        //     m_AuctionItem.m_nClassID)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     kAuctionSourceWide,
        //     0xcb,
        //     m_AuctionItem.m_nClassID)
        L2_LOG_ERROR(
            Log,
            "Cannot deliver the winning item for auction %d: class %d is missing or is not an item; finalization returns "
            "failure.",
            m_nAuctionDBID, m_AuctionItem.m_nClassID
        );
        return false;
    }

    CItem* const item = object->CastItem();
    manipulations[0].mClassId = m_AuctionItem.m_nClassID;
    manipulations[0].mCount = m_AuctionItem.m_nAmount;
    manipulations[0].mEnchanted = item->m_data->GetEnchanted();

    EtcManiSuccessfulItemAuctionBid successfulBid;
    successfulBid.m_nBidID = 0;
    successfulBid.m_nUserDBID = static_cast<unsigned int>(winnerDatabaseId);
    successfulBid.m_nAuctionID = m_nAuctionDBID;
    successfulBid.m_nAdenaAmount = winningPrice;
    successfulBid.m_nItemClassId = m_AuctionItem.m_nClassID;
    successfulBid.m_nItemCount = static_cast<int>(m_AuctionItem.m_nAmount);
    successfulBid.m_nAuctionNPCId = m_AuctionItem.m_nOwnerDBID;

    CPacketHelperEx<20480> packet;
    successfulBid.Assemble(packet);
    return db.CreateUpdateItemsAtWarehouse(winnerDatabaseId, 1, manipulations.data(), nullptr, 1, &packet);
}

void CAuction::TimerExpired(const int id) {
    // Current L2Server.exe 0x0049d4dc.
    static_cast<void>(id);
    L2_GUARD_FUNCTION();
    if (IsDeleting() || m_Deleted != 0) {
        return;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    if (m_eStatus == AS_NONE) {
        if (m_tStartTime <= now) {
            SetAuctionStatus(AS_DOING);
            if (GetUniqueNPC(m_nNPCClassID) != nullptr) {
                CUserBroadcaster::inst().BroadcastToAllUserEx("chccd", 0x62, 0x823, 1, 3, m_AuctionItem.m_nClassID);
            }
            Log.Add(
                LOG_BOTH,
                u"%I64d,,,,,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,"
                u",,,,,,,,,,",
                static_cast<std::int64_t>(0x3db), static_cast<std::int64_t>(m_nAuctionDBID),
                static_cast<std::int64_t>(m_AuctionItem.m_nClassID), m_AuctionItem.m_nAmount, m_nInitPrice,
                static_cast<std::int64_t>(m_AuctionItem.m_nOwnerDBID)
            );
        }
    } else if (m_eStatus == AS_DOING) {
        runtime::raw_atomic::ExchangeI32(&m_nRealDone, 0);
        if (!m_bHalfHourBroadcasting) {
            const std::int32_t halfHourNow = runtime::crt::Time32Now();
            if (runtime::AddInt32Wrapping(m_tStartTime, 1800) < halfHourNow) {
                CNPC* const npc = GetUniqueNPC(m_nNPCClassID);
                if (npc != nullptr) {
                    g_ptlUserMap->BroadcastToNeighborRect(
                        nullptr, 0x80000, npc->GetPos(), 0x4000, "chccd", 0x62, 0x1144, 1, 3, m_AuctionItem.m_nClassID
                    );
                    m_bHalfHourBroadcasting = true;
                }
            }
        }

        const std::int32_t endTime = runtime::AddInt32Wrapping(m_tStartTime, m_nDuration);
        if (endTime <= now) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"[%s][%d] auction end. id[%d] start[%d] end[%d] now[%d]",
            //     kAuctionSourceWide,
            //     0x124,
            //     m_nAuctionDBID,
            //     m_tStartTime,
            //     endTime,
            //     now)
            // L2_LOG_INFO(Log,
            //     L"[%s][%d] auction end. id[%d] start[%d] end[%d] now[%d]",
            //     kAuctionSourceWide,
            //     0x124,
            //     m_nAuctionDBID,
            //     m_tStartTime,
            //     endTime,
            //     now)
            L2_LOG_INFO(
                Log,
                "Bidding period ended for auction %d: start %d, end %d, now %d (Unix seconds); changing status to DONE.",
                m_nAuctionDBID, m_tStartTime, endTime, now
            );
            SetAuctionStatus(AS_DONE);
            CNPC* const npc = GetUniqueNPC(m_nNPCClassID);
            if (npc != nullptr && m_nCurrentPrice != 0) {
                g_ptlUserMap->BroadcastToNeighborRect(
                    nullptr, 0x80000, npc->GetPos(), 0x4000, "chccdcQ", 0x62, 0x1145, 2, 3, m_AuctionItem.m_nClassID, 6,
                    m_nCurrentPrice
                );
            }
        }
    } else if (m_eStatus == AS_DONE) {
        if (m_nRealDone == 0) {
            if (m_nAuctionDBJobCount == 0) {
                if (runtime::raw_atomic::ExchangeI32(&m_nRealDone, 1) == 0) {
                    FinishAuction();
                }
            } else {
                const std::uint32_t tick = runtime::TickCount32();
                if (m_nLastBiddingTick + 60000U <= tick) {
                    m_nLastBiddingTick = runtime::TickCount32();
                    runtime::raw_atomic::AddI32(&m_nAuctionDBJobCount, -1);
                }
            }
        }

        const std::int32_t expirationBoundary = static_cast<std::int32_t>(static_cast<std::uint32_t>(now) - 0x93a80U);
        if (m_tStartTime <= expirationBoundary && CAuctionManager::inst().DeleteAuction(this)) {
            return;
        }
    }
    AddTimer(1000, 0);
}

CAuctionManager::CAuctionManager() = default;

CAuctionManager::~CAuctionManager() = default;

void CAuctionManager::Load() {
    // Current L2Server 0x00498224 uses wrapping time32 subtraction.
    L2_GUARD_FUNCTION();
    const std::uint32_t now = static_cast<std::uint32_t>(runtime::crt::Time32Now());
    db.RequestLoadAllAuction(static_cast<std::int32_t>(now - 0x93a80U));
}

CAuction* CAuctionManager::GetAuction(const int auctionDatabaseId) {
    L2_GUARD_FUNCTION();
    CAuction* auction;
    {
        L2_GUARD(m_Lock);
        const auto found = m_mapAuctionMap.find(auctionDatabaseId);
        auction = found == m_mapAuctionMap.end() ? nullptr : found->second;
    }
    return auction;
}

int CAuctionManager::BuilderCMD_SendAuctionManagerState(CUserSocket& socket) {
    // Current L2Server 0x0049990c.
    L2_GUARD_FUNCTION();
    socket.SendSystemMessage(u"SYS", u"====Start Auction Map List====");

    int count;
    {
        L2_GUARD(m_Lock);
        const std::int32_t now = runtime::crt::Time32Now();
        std::array<char16_t, 2044> message{};
        FormatUtf16To(
            message.data(), message.size(), u"auction num=[%d] now=[%d]", static_cast<int>(m_mapAuctionMap.size()), now
        );
        socket.SendSystemMessage(u"SYS", message.data());

        count = 0;
        for (const auto& entry : m_mapAuctionMap) {
            entry.second->SendAuctionState(socket);
            ++count;
        }
    }

    socket.SendSystemMessage(u"SYS", u"==== End Auction Map List ====");
    return count;
}

CAuction* CAuctionManager::GetLatestAuction(const int npcClassId) {
    // Current L2Server 0x0049a364.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    std::int32_t latestStartTime = 0;
    CAuction* latest = nullptr;

    {
        L2_GUARD(m_Lock);
        const auto range = m_mmapAuctionSellerMap.equal_range(npcClassId);
        for (auto position = range.first; position != range.second; ++position) {
            CAuction* const auction = position->second;
            if (latestStartTime <= auction->m_tStartTime && auction->m_tStartTime <= now) {
                latestStartTime = auction->m_tStartTime;
                latest = auction;
            }
        }
    }
    return latest;
}

CAuction* CAuctionManager::GetActiveAuction(const int npcClassId) {
    // Current L2Server 0x0049a4ec.
    L2_GUARD_FUNCTION();
    CAuction* result = nullptr;
    {
        L2_GUARD(m_Lock);
        const auto range = m_mmapAuctionSellerMap.equal_range(npcClassId);
        for (auto position = range.first; position != range.second; ++position) {
            CAuction* const auction = position->second;
            if (auction != nullptr && (auction->m_eStatus == AS_DOING || auction->m_nAuctionDBJobCount > 0)) {
                result = auction;
                break;
            }
        }
    }
    return result;
}

void CAuctionManager::GetAuctions(const int npcClassId, std::vector<CAuction*>& auctions) {
    // Current L2Server 0x0049c9f0.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        const auto range = m_mmapAuctionSellerMap.equal_range(npcClassId);
        for (auto position = range.first; position != range.second; ++position) {
            auctions.push_back(position->second);
        }
    }
}

void CAuctionManager::BuilderCMD_ClearAllItemAuctions(const int npcClassId) {
    // Current L2Server 0x0049b69c.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        const auto range = m_mmapAuctionSellerMap.equal_range(npcClassId);
        for (auto position = range.first; position != range.second; ++position) {
            CAuction* const auction = position->second;
            auction->SetAuctionStatus(AS_DONE);
            auction->SetAuctionDuration(0);
            m_mapAuctionMap.erase(auction->m_nAuctionDBID);
        }
        m_mmapAuctionSellerMap.erase(npcClassId);
    }
}

bool CAuctionManager::IsValidAuction(CAuction* const auction) {
    // Current L2Server 0x0049ec60.
    L2_GUARD_FUNCTION();
    if (auction == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid auction.. ",
        //     kAuctionSourceWide,
        //     0x48d)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid auction.. ",
        //     kAuctionSourceWide,
        //     0x48d)
        L2_LOG_ERROR(Log, "Cannot validate auction: the object pointer is null.");
        return false;
    }

    const int auctionDatabaseId = auction->m_nAuctionDBID;
    const int sellerDatabaseId = auction->m_AuctionItem.m_nOwnerDBID;
    if (auctionDatabaseId == 0 || sellerDatabaseId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid auction.. Auction ID[%d], Seller ID[%d]",
        //     kAuctionSourceWide,
        //     0x495,
        //     auctionDatabaseId,
        //     sellerDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid auction.. Auction ID[%d], Seller ID[%d]",
        //     kAuctionSourceWide,
        //     0x495,
        //     auctionDatabaseId,
        //     sellerDatabaseId)
        L2_LOG_ERROR(
            Log, "Cannot validate auction %d with seller ID %d: both IDs must be nonzero.", auctionDatabaseId,
            sellerDatabaseId
        );
        return false;
    }
    if (CAuctionNPCDB::inst().GetAuctionNPC(sellerDatabaseId) != nullptr) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid auction NPC.. Auction ID[%d], Seller ID[%d]",
    //     kAuctionSourceWide,
    //     0x49c,
    //     auctionDatabaseId,
    //     sellerDatabaseId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid auction NPC.. Auction ID[%d], Seller ID[%d]",
    //     kAuctionSourceWide,
    //     0x49c,
    //     auctionDatabaseId,
    //     sellerDatabaseId)
    L2_LOG_ERROR(
        Log, "Cannot validate auction %d: seller ID %d has no auction-controller definition.", auctionDatabaseId,
        sellerDatabaseId
    );
    return false;
}

bool CAuctionManager::PushAuctionMap(CAuction* const auction) {
    // Current L2Server 0x0049edb4. Callers hold m_Lock.
    L2_GUARD_FUNCTION();
    if (!IsValidAuction(auction)) {
        return false;
    }

    const int auctionDatabaseId = auction->m_nAuctionDBID;
    const int sellerDatabaseId = auction->m_AuctionItem.m_nOwnerDBID;
    if (m_mapAuctionMap.find(auctionDatabaseId) != m_mapAuctionMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] duplicated auction id..[%d]",
        //     kAuctionSourceWide,
        //     0x4ce,
        //     auctionDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] duplicated auction id..[%d]",
        //     kAuctionSourceWide,
        //     0x4ce,
        //     auctionDatabaseId)
        L2_LOG_ERROR(Log, "Cannot register duplicate auction ID %d; the existing auction is retained.", auctionDatabaseId);
        return false;
    }

    m_mapAuctionMap.emplace(auctionDatabaseId, auction);
    m_mmapAuctionSellerMap.emplace(sellerDatabaseId, auction);
    return true;
}

void CAuctionManager::InitAllAuctions(const int auctionCount, const unsigned char* packet) {
    // Current L2Server 0x0049f104.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        if (auctionCount > 0) {
            std::uint32_t remaining = static_cast<std::uint32_t>(auctionCount);
            do {
                int auctionDatabaseId;
                int status;
                int extendStatus;
                int npcClassId;
                int startTime;
                int endTime;
                int itemIndex;
                packet = ::Disassemble(
                    packet, "ddddddd", &auctionDatabaseId, &status, &extendStatus, &npcClassId, &startTime, &endTime,
                    &itemIndex
                );

                CAuctionNPC* const npc = CAuctionNPCDB::inst().GetAuctionNPC(npcClassId);
                if (npc == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d][ItemAuction] Invalid auction NPC[%d]",
                    //     kAuctionSourceWide,
                    //     0x46a,
                    //     npcClassId)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d][ItemAuction] Invalid auction NPC[%d]",
                    //     kAuctionSourceWide,
                    //     0x46a,
                    //     npcClassId)
                    L2_LOG_ERROR(
                        Log,
                        "Cannot restore auction %d: NPC class %d has no auction-controller definition; this record is "
                        "skipped.",
                        auctionDatabaseId, npcClassId
                    );
                } else {
                    const NPCAuctionItem npcItem = npc->GetAuctionItem(itemIndex);
                    if (npcItem.m_nItemType == 0 && npcItem.m_nItemAmount == 0 && npcItem.m_nItemPrice == 0) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d][ItemAuction] Finding auction item failed. [%d]",
                        //     kAuctionSourceWide,
                        //     0x471,
                        //     itemIndex)
                        // L2_LOG_ERROR(Log,
                        //     L"[%s][%d][ItemAuction] Finding auction item failed. [%d]",
                        //     kAuctionSourceWide,
                        //     0x471,
                        //     itemIndex)
                        L2_LOG_ERROR(
                            Log,
                            "Cannot restore auction %d for NPC class %d: item index %d resolves to zero type, amount and "
                            "price; this record is skipped.",
                            auctionDatabaseId, npcClassId, itemIndex
                        );
                    } else {
                        AuctionItem item;
                        item.m_nOwnerDBID = npcClassId;
                        item.m_nItemIndex = itemIndex;
                        item.m_nClassID = npcItem.m_nItemType;
                        item.m_nAmount = npcItem.m_nItemAmount;
                        const std::int32_t duration = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(endTime) - static_cast<std::uint32_t>(startTime)
                        );

                        void* const storage = CAuction::operator new(sizeof(CAuction), true);
                        CAuction* const auction = storage == nullptr
                            ? nullptr
                            : ::new (storage) CAuction(
                                  auctionDatabaseId, npcClassId, startTime, duration, static_cast<AuctionStatus>(status),
                                  static_cast<AuctionExtendStatus>(extendStatus), item, npcItem.m_nItemPrice
                              );
                        if (!PushAuctionMap(auction)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%s][%d][ItemAuction] PushAuctionMap failed. [%d]",
                            //     kAuctionSourceWide,
                            //     0x47d,
                            //     auctionDatabaseId)
                            // L2_LOG_ERROR(Log,
                            //     L"[%s][%d][ItemAuction] PushAuctionMap failed. [%d]",
                            //     kAuctionSourceWide,
                            //     0x47d,
                            //     auctionDatabaseId)
                            L2_LOG_ERROR(
                                Log,
                                "Cannot register restored auction %d for NPC class %d (object %p); the subsequent bid-load "
                                "call is still attempted.",
                                auctionDatabaseId, npcClassId, static_cast<void*>(auction)
                            );
                        }
                        auction->LoadBid();
                    }
                }
                --remaining;
            } while (remaining != 0);
        }
    }
}

bool CAuctionManager::RegisterAuction(CAuction* const auction) {
    // Current L2Server 0x0049f36c.
    L2_GUARD_FUNCTION();
    if (!IsValidAuction(auction)) {
        return false;
    }

    bool result;
    {
        L2_GUARD(m_Lock);
        result = PushAuctionMap(auction);
    }
    if (result) {
        auction->AddTimer(1000, 0);
    }
    return result;
}

bool CAuctionManager::DeleteAuction(CAuction* const auction) {
    L2_GUARD_FUNCTION();
    if (auction == nullptr || auction->m_nAuctionDBID == 0 || auction->m_AuctionItem.m_nOwnerDBID == 0) {
        return false;
    }

    const int auctionDatabaseId = auction->m_nAuctionDBID;
    const int sellerDatabaseId = auction->m_AuctionItem.m_nOwnerDBID;
    {
        L2_GUARD(m_Lock);
        const auto found = m_mapAuctionMap.find(auctionDatabaseId);
        if (found == m_mapAuctionMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] auction is deleted already. Auction[%d]",
            //     kAuctionSourceWide,
            //     0x4f0,
            //     auctionDatabaseId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] auction is deleted already. Auction[%d]",
            //     kAuctionSourceWide,
            //     0x4f0,
            //     auctionDatabaseId)
            L2_LOG_ERROR(
                Log,
                "Auction %d is missing from the main registry; seller-index cleanup and deletion of the supplied object %p "
                "continue.",
                auctionDatabaseId, static_cast<void*>(auction)
            );
        } else {
            m_mapAuctionMap.erase(found);
        }

        const auto sellerRange = m_mmapAuctionSellerMap.equal_range(sellerDatabaseId);
        for (auto current = sellerRange.first; current != sellerRange.second;) {
            if (current->second == auction) {
                current = m_mmapAuctionSellerMap.erase(current);
            } else {
                ++current;
            }
        }
    }
    auction->Delete();
    return true;
}

void CAuction::ExpandAuctionDuration() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    if (m_eExtendStatus == AES_NONE) {
        if (runtime::AddInt32Wrapping(now, 600) <= runtime::AddInt32Wrapping(m_tStartTime, m_nDuration)) {
            return;
        }
        m_eExtendStatus = AES_FIRST_EXTEND;
        SetAuctionDuration(runtime::AddInt32Wrapping(m_nDuration, 300));
        CNPC* const npc = GetUniqueNPC(m_nNPCClassID);
        if (npc != nullptr) {
            g_ptlUserMap->BroadcastToNeighborRect(nullptr, 0x80000, npc->GetPos(), 0x4000, "chc", 0x62, 0x86f, 0);
        }
    } else if (m_eExtendStatus == AES_FIRST_EXTEND) {
        if (runtime::AddInt32Wrapping(now, 600) <= runtime::AddInt32Wrapping(m_tStartTime, m_nDuration)) {
            return;
        }
        m_eExtendStatus = AES_SECOND_EXTEND;
        SetAuctionDuration(runtime::AddInt32Wrapping(m_nDuration, 180));
        CNPC* const npc = GetUniqueNPC(m_nNPCClassID);
        if (npc != nullptr) {
            g_ptlUserMap->BroadcastToNeighborRect(nullptr, 0x80000, npc->GetPos(), 0x4000, "chc", 0x62, 0x870, 0);
        }
    }
}

CAuctionNPC::CAuctionNPC() :
    m_nNPCClassID(0),
    m_bDoing(false),
    m_nNextAuctionDateIndex(0),
    m_tNextStartTime(0),
    m_tNextInfoOpenTime(0),
    m_alignment74To78{},
    m_nCurrentAuctionItemIndex(0),
    m_bAuctionCreating(false),
    m_bShowNextAuctionByBuilder(false),
    m_bBroadcastingNextItem(false),
    m_alignment9fToA0{} {}

CAuctionNPC::~CAuctionNPC() = default;

L2_DEFINE_OBJECT_CLASS_ID(CAuctionNPC)

L2_DEFINE_OBJECT_CLASS_NAME(CAuctionNPC, m_pClassName_CAuctionNPC)

L2_DEFINE_OBJECT_ALLOCATOR(CAuctionNPC, __GetClassName())

L2_DEFINE_OBJECT_NEW(CAuctionNPC)

L2_DEFINE_OBJECT_TYPED_RELEASE(CAuctionNPC)

int CAuctionNPC::GetIndex() const { return static_cast<int>(Id() & 0xfffffU); }

CObject* CAuctionNPC::Copy() { return nullptr; }

void CAuctionNPC::Delete() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Auction NPC cannot be deleted!!!",
    //     kAuctionSourceWide,
    //     0x5e0)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Auction NPC cannot be deleted!!!",
    //     kAuctionSourceWide,
    //     0x5e0)
    L2_LOG_ERROR(
        Log,
        "Unexpected auction-NPC controller deletion (%p); reporting the call stack, then clearing items and releasing the "
        "object.",
        static_cast<void*>(this)
    );
    runtime::process::ReportCallStack(2, false);
    m_AuctionItemList.clear();
    SetDelete();
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

FVector CAuctionNPC::GetPos() const { return m_Position; }

void CAuctionNPC::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    if (!IsDeleting() && m_Deleted == 0) {
        if (NeedToUpdatePosition()) {
            UpdatePosition();
        }
        if (CanMakeAuction()) {
            CreateNextItemAuction();
        }
        CheckBroadcastingTime();
        AddTimer(1000, 0);
    }
}

void CAuctionNPC::UpdateAuctionDate() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now() + 600;
    std::tm local{};
    if (!runtime::crt::LocalTime32(now, local)) {
        return;
    }

    int nextIndex = 0;
    int nextDelayMinutes = 0x93a80;
    for (std::size_t index = 0; index < m_AuctionDateList.size(); ++index) {
        const AuctionDate& date = m_AuctionDateList[index];
        const int delay = ResetBindingDayHelper::FindNextResetBindingDay(
            1 << (static_cast<unsigned>(date.m_nWeekDay) & 31U), date.m_nStartHour * 60 + date.m_nStartMin, local.tm_wday,
            local.tm_hour * 60 + local.tm_min
        );
        if (delay < nextDelayMinutes) {
            nextIndex = static_cast<int>(index);
            nextDelayMinutes = delay;
        }
    }

    m_nNextAuctionDateIndex = nextIndex;
    m_tNextStartTime = ((nextDelayMinutes * 60 + now) / 60) * 60;

    std::tm next{};
    if (runtime::crt::LocalTime32(m_tNextStartTime, next)) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[%s][%d] AuctionNPC[%d] set last start time[%d-%d-%d %d:%d:%d]",
        //     kAuctionSourceWide,
        //     0x60e,
        //     m_nNPCClassID,
        //     next.tm_year + 1900,
        //     next.tm_mon + 1,
        //     next.tm_mday,
        //     next.tm_hour,
        //     next.tm_min,
        //     next.tm_sec)
        // L2_LOG_INFO(Log,
        //     L"[%s][%d] AuctionNPC[%d] set last start time[%d-%d-%d %d:%d:%d]",
        //     kAuctionSourceWide,
        //     0x60e,
        //     m_nNPCClassID,
        //     next.tm_year + 1900,
        //     next.tm_mon + 1,
        //     next.tm_mday,
        //     next.tm_hour,
        //     next.tm_min,
        //     next.tm_sec)
        L2_LOG_INFO(
            Log, "NPC class %d: next auction schedule set to %04lld-%02d-%02d %02d:%02d:%02d local time (Unix seconds %d).",
            m_nNPCClassID, static_cast<long long>(next.tm_year) + 1900, next.tm_mon + 1, next.tm_mday, next.tm_hour,
            next.tm_min, next.tm_sec, m_tNextStartTime
        );
    }
}

int CAuctionNPC::GetNextAuctionItemIndex() const {
    L2_GUARD_FUNCTION();
    const int itemCount = static_cast<int>(m_AuctionItemList.size());
    const int next = (m_nCurrentAuctionItemIndex + 1) % itemCount;
    if (next < 0 || itemCount <= next) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ERROR m_nCurrentItemIndex[%d] is invalid !!!",
        //     kAuctionSourceWide,
        //     0x649,
        //     m_nCurrentAuctionItemIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ERROR m_nCurrentItemIndex[%d] is invalid !!!",
        //     kAuctionSourceWide,
        //     0x649,
        //     m_nCurrentAuctionItemIndex)
        L2_LOG_ERROR(
            Log,
            "NPC class %d: next auction item index %d is invalid (current index %d, item count %d); index 0 is returned.",
            m_nNPCClassID, next, m_nCurrentAuctionItemIndex, itemCount
        );
        return 0;
    }
    return next;
}

void CAuctionNPC::SendAuctionNpcState(CUserSocket& socket) {
    // Current L2Server 0x00499230.
    L2_GUARD_FUNCTION();

    const std::int32_t nextStartTime = GetNextStartTime();
    std::tm* const localTime = runtime::crt::LocalTime32(nextStartTime);
    const int nextItemIndex = GetNextAuctionItemIndex();

    std::array<char16_t, 1024> message{};
    FormatUtf16To(
        message.data(), message.size(),
        u"ID[%d], Next Auction Info: time[%d/%d/%d  %d:%d:%d], "
        u"item index[%d]",
        m_nNPCClassID, localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour,
        localTime->tm_min, localTime->tm_sec, nextItemIndex
    );
    socket.SendSystemMessage(u"SYS", message.data());

    for (std::size_t index = 0; index < m_AuctionDateList.size(); ++index) {
        const AuctionDate& date = m_AuctionDateList[index];
        const bool auctionCreating = m_bAuctionCreating != false;
        const int weekDay = date.m_nWeekDay;
        const int duration = date.m_nDuration;
        const int startMinute = date.m_nStartMin;
        const int startHour = date.m_nStartHour;
        const bool doing = m_bDoing != false;
        const int npcClassId = m_nNPCClassID;
        FormatUtf16To(
            message.data(), message.size(),
            u"ID[%d], doing[%d], time[%d hour, %d min, du = %d, "
            u"day = %d], creating[%d]",
            npcClassId, doing, startHour, startMinute, duration, weekDay, auctionCreating
        );
        socket.SendSystemMessage(u"SYS", message.data());
    }
}

NPCAuctionItem CAuctionNPC::PickNextAuctionItem() {
    L2_GUARD_FUNCTION();
    return m_AuctionItemList[static_cast<std::size_t>(GetNextAuctionItemIndex())];
}

NPCAuctionItem CAuctionNPC::GetAuctionItem(int itemIndex) const {
    L2_GUARD_FUNCTION();
    if (itemIndex < 0 || static_cast<std::size_t>(itemIndex) >= m_AuctionItemList.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][ItemAuction] Invalid item index[%d]",
        //     kAuctionSourceWide,
        //     0x668,
        //     itemIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][ItemAuction] Invalid item index[%d]",
        //     kAuctionSourceWide,
        //     0x668,
        //     itemIndex)
        L2_LOG_ERROR(
            Log, "NPC class %d: auction item index %d is outside the list of %llu items; an empty item result is returned.",
            m_nNPCClassID, itemIndex, static_cast<unsigned long long>(m_AuctionItemList.size())
        );
        return NPCAuctionItem{};
    }
    return m_AuctionItemList[static_cast<std::size_t>(itemIndex)];
}

std::int32_t CAuctionNPC::GetNextStartTime() {
    L2_GUARD_FUNCTION();
    const std::int32_t timezoneMinutesWest = StandardTimezoneMinutesWest();
    const AuctionDate& date = m_AuctionDateList[static_cast<std::size_t>(m_nNextAuctionDateIndex)];
    const std::int32_t dayCount = m_tNextStartTime / 86400;
    return (((dayCount * 24 + date.m_nStartHour) * 60 + date.m_nStartMin + timezoneMinutesWest) * 60);
}

bool CAuctionNPC::CanMakeAuction() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const std::int32_t start = GetNextStartTime();
    const std::int32_t duration = m_AuctionDateList[static_cast<std::size_t>(m_nNextAuctionDateIndex)].m_nDuration;
    return m_bDoing && !m_bAuctionCreating && start <= now && now < start + duration;
}

void CAuctionNPC::CheckBroadcastingTime() {
    L2_GUARD_FUNCTION();
    if (!m_bBroadcastingNextItem) {
        const std::int32_t start = GetNextStartTime();
        const std::int32_t now = runtime::crt::Time32Now();
        if (start - m_tNextInfoOpenTime < now) {
            m_bBroadcastingNextItem = true;
            const NPCAuctionItem item = PickNextAuctionItem();
            CUserBroadcaster::inst().BroadcastToAllUser("chccd", 0x62, 0x1143, 1, 3, item.m_nItemType);
        }
    }
}

void CAuctionNPC::_CreateItemAuction(std::int32_t startTime, int itemIndex) {
    L2_GUARD_FUNCTION();
    m_bAuctionCreating = true;
    const std::int32_t duration = m_AuctionDateList[static_cast<std::size_t>(m_nNextAuctionDateIndex)].m_nDuration;
    {
        L2_GUARD(m_Lock);
        db.RequestCreateItemAuction(m_nNPCClassID, startTime, startTime + duration, itemIndex);
    }
}

void CAuctionNPC::CreateNextItemAuction() {
    L2_GUARD_FUNCTION();
    const int itemIndex = GetNextAuctionItemIndex();
    _CreateItemAuction(GetNextStartTime(), itemIndex);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d][ItemAuction] Create Item Auction On Time",
    //     kAuctionSourceWide,
    //     0x6a5)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d][ItemAuction] Create Item Auction On Time",
    //     kAuctionSourceWide,
    //     0x6a5)
    L2_LOG_INFO(Log, "Requested scheduled auction creation for NPC class %d, item index %d.", m_nNPCClassID, itemIndex);
}

void CAuctionNPC::CreateInitialItemAuction() {
    L2_GUARD_FUNCTION();
    const int itemIndex = static_cast<int>(m_AuctionItemList.size()) - 1;
    if (itemIndex == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][ItemAuction] item list empty[%d]",
        //     kAuctionSourceWide,
        //     0x6cc)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][ItemAuction] item list empty[%d]",
        //     kAuctionSourceWide,
        //     0x6cc)
        L2_LOG_ERROR(Log, "Cannot request the initial auction for NPC class %d: its item list is empty.", m_nNPCClassID);
        return;
    }
    _CreateItemAuction(runtime::crt::Time32Now() - 86400, itemIndex);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d][ItemAuction] Create Item Auction Initial",
    //     kAuctionSourceWide,
    //     0x6d2)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d][ItemAuction] Create Item Auction Initial",
    //     kAuctionSourceWide,
    //     0x6d2)
    L2_LOG_INFO(Log, "Requested initial auction creation for NPC class %d, item index %d.", m_nNPCClassID, itemIndex);
}

bool CAuctionNPC::BuilderCMD_CreateItemAuction(int itemIndex) {
    L2_GUARD_FUNCTION();
    if (itemIndex < 0 || static_cast<std::size_t>(itemIndex) >= m_AuctionItemList.size()) {
        return false;
    }
    m_nCurrentAuctionItemIndex = itemIndex;
    _CreateItemAuction(runtime::crt::Time32Now(), itemIndex);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d][ItemAuction] Create Item Auction By BuilderCMD",
    //     kAuctionSourceWide,
    //     0x6ba)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d][ItemAuction] Create Item Auction By BuilderCMD",
    //     kAuctionSourceWide,
    //     0x6ba)
    L2_LOG_INFO(
        Log, "Requested auction creation by builder command for NPC class %d, item index %d.", m_nNPCClassID, itemIndex
    );
    return true;
}

bool CAuctionNPC::NeedToUpdatePosition() const { return m_Position.IsZero(); }

void CAuctionNPC::UpdatePosition() {
    // Current L2Server 0x004982cc.
    L2_GUARD_FUNCTION();
    CNPC* const npc = GetUniqueNPC(m_nNPCClassID);
    if (npc != nullptr) {
        m_Position = npc->GetPos();
    }
}

CAuctionNPCDB::CAuctionNPCDB() = default;

CAuctionNPCDB::~CAuctionNPCDB() = default;

CAuctionNPC* CAuctionNPCDB::GetAuctionNPC(int npcClassId) const {
    L2_GUARD_FUNCTION();
    const auto position = m_mapAuctionNPC.find(npcClassId);
    return position == m_mapAuctionNPC.end() ? nullptr : position->second;
}

void CAuctionNPCDB::InitAllAuctionNPC() {
    // Current L2Server 0x0049e7ac logs a null mapped NPC and
    // then continues into the original dereference.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mapAuctionNPC) {
        CAuctionNPC* const npc = entry.second;
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] No AuctionNPC Object in AuctionNPCMap of AuctionNPCDB",
            //     kAuctionSourceWide,
            //     0x726)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] No AuctionNPC Object in AuctionNPCMap of AuctionNPCDB",
            //     kAuctionSourceWide,
            //     0x726)
            L2_LOG_ERROR(
                Log,
                "Auction-controller registry key %d contains a null object; initialization still attempts to access that "
                "object.",
                entry.first
            );
        }

        CAuction* const latest = CAuctionManager::inst().GetLatestAuction(npc->m_nNPCClassID);
        if (latest == nullptr) {
            npc->CreateInitialItemAuction();
        } else {
            const AuctionItem currentItem = latest->m_AuctionItem;
            npc->UpdateAuctionDate();
            npc->m_nCurrentAuctionItemIndex = currentItem.m_nItemIndex;
            const NPCAuctionItem nextItem = npc->PickNextAuctionItem();
            db.RequestUpdateItemAuctionInfoForL2Assist(
                latest->m_nAuctionDBID, 0, 0, 0, nextItem.m_nItemType, nextItem.m_nItemPrice, nextItem.m_nItemAmount, 0
            );
        }
        npc->AddTimer(1000, 0);
    }
}

void CAuctionNPCDB::BuilderCMD_SendAuctionNpcDBState(CUserSocket& socket) {
    // Current L2Server 0x00499ac4.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mapAuctionNPC) {
        entry.second->SendAuctionNpcState(socket);
    }
}

void CAuctionNPCDB::Load() {
    L2_GUARD_FUNCTION();

    CSecureStream input(kAuctionDataPath, false);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     kAuctionSourceWide,
        //     0x6ff,
        //     kAuctionDataPath)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     kAuctionSourceWide,
        //     0x6ff,
        //     kAuctionDataPath)
        L2_LOG_ERROR(Log, u"Cannot read auction definitions from '%ls'; loading returns without parsing.", kAuctionDataPath);
        return;
    }

    CParserForAuction parser;
    if (!parser.create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForAuction::create failed",
        //     kAuctionSourceWide,
        //     0x708)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForAuction::create failed",
        //     kAuctionSourceWide,
        //     0x708)
        L2_LOG_ERROR(Log, u"Cannot create the auction-data parser for '%ls'; definitions are not parsed.", kAuctionDataPath);
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Auction Data file failed after line[%d]",
        //     kAuctionSourceWide,
        //     0x714,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Auction Data file failed after line[%d]",
        //     kAuctionSourceWide,
        //     0x714,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, u"Parsing '%ls' failed at line %lld; previously published auction definitions are retained.",
            kAuctionDataPath, static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

void CAuctionHtmlCommandHandler::Handle(CNPC* const npc, User* const user, const char16_t* const command) {
    // Current L2Server 0x0049f490.
    L2_GUARD_FUNCTION();
    if (npc == nullptr || user == nullptr || command == nullptr) {
        return;
    }

    const int npcClassId = npc->GetClassId() - 1000000;
    if (target_utf16::CompareN(command, u"_bid", 4) == 0) {
        CAuction* const auction = CAuctionManager::inst().GetLatestAuction(npcClassId);
        if (auction != nullptr) {
            auction->ShowAuctionUIByNPC(npc, user);
            return;
        }
    } else if (target_utf16::CompareN(command, u"_withdraw", 9) == 0) {
        std::vector<CAuction*> auctions;
        CAuctionManager::inst().GetAuctions(npcClassId, auctions);
        for (CAuction* const auction : auctions) {
            const std::int64_t failureMoney = auction->GetBidFailureMoney(user->GetDBID());
            if (failureMoney <= 0) {
                continue;
            }

            bool canWithdraw;
            {
                L2_READ_GUARD(user->m_inv);
                canWithdraw =
                    runtime::AddInt64Wrapping(user->m_inv.GetAdenaAmount(), failureMoney) < INT64_C(1000000000000);
                if (!canWithdraw) {
                    user->SendSystemMessage(SystemMessageId::INVENTORY_ADENA_LIMIT_EXCEEDED);
                }
            }
            if (canWithdraw) {
                user->GetSocket()->Send("cdddQdd", 0xf3, 0x82d, 1, 6, failureMoney, 0, auction->GetAuctionID());
            }
            return;
        }
        user->SendSystemMessage(SystemMessageId::NO_UNCLAIMED_BID_FUNDS);
        return;
    } else if (target_utf16::CompareN(command, u"_nextinfo", 9) == 0) {
        CAuction* const auction = CAuctionManager::inst().GetLatestAuction(npcClassId);
        if (auction != nullptr) {
            auction->ShowNextAuctionUIByNPC(npc, user);
            return;
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid Auction Html [%s]",
        //     command)
        // L2_LOG_ERROR(Log,
        //     L"Invalid Auction Html [%s]",
        //     command)
        L2_LOG_WARN(
            Log, u"Unsupported auction HTML command '%ls' for NPC class %d; the command is ignored.", command, npcClassId
        );
        return;
    }
    user->SendSystemMessage(SystemMessageId::OUTSIDE_AUCTION_PERIOD);
}

CAuctionReleaseVerifier::CAuctionReleaseVerifier(CAuction* auction) : m_pAuction(auction) {
    L2_ADD_REF(AsAuctionIoObject(m_pAuction), ObjectRefType::VerifierRetain);
    AddTimer(10000, 1);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

void CAuctionReleaseVerifier::TimerExpired(int) {
    ::CIOObject* const auction = AsAuctionIoObject(m_pAuction);
    const std::int32_t remaining = L2_RELEASE(auction, ObjectRefType::VerifierRetain);
    if (remaining > 0) {
        auction->ReportOrt(kAuctionSource, 0x6a);
        g_ServerContext.NotReleasedAuction.fetch_add(1, std::memory_order_seq_cst);
    }
}

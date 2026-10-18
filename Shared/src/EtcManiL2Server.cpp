#include "Auction.h"
#include "CConfig.h"
#include "EnchantOption.h"
#include "EnsoulData.h"
#include "ItemAnnounceInfo.h"
#include "L2Server/src/Inventory.h"
#include "L2Server/src/Item.h"
#include "MultiSell.h"
#include "NPC.h"
#include "NpcServer.h"
#include "Pledge.h"
#include "PledgeContribution.h"
#include "PledgeMastery.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Skill.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"
#include "VipAttendance.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

const wchar_t* EtcManiAuctionAccountName(const std::array<char16_t, 15>& source, std::array<wchar_t, 15>& storage) {
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

void CopyEtcManiAuctionCharacterName(const wchar_t* const source, std::array<wchar_t, 25>& destination) {
    if (source == nullptr) {
        destination[0] = L'\0';
        return;
    }
    const std::size_t copyLength = std::min(std::wcslen(source), destination.size() - 1U);
    std::copy_n(source, copyLength, destination.data());
    destination[copyLength] = L'\0';
}

} // namespace

namespace ItemUtil {

void AssembleItemList(const std::vector<CreateItemStruct>& items, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00716054 serializes the legacy pledge-item list
    // as dQ. The in-memory Enchant member is not transmitted.
    for (const CreateItemStruct& item : items) {
        packet.Assemble("dQ", item.ItemID, item.Count);
    }
}

const unsigned char* DisassembleItemList(const unsigned char* packet, int count, std::vector<CreateItemStruct>& items) {
    // Current L2Server.exe 0x0071a3b8. The default constructor intentionally
    // leaves both padding lanes indeterminate. The legacy wire carries only
    // ItemID and Count.
    L2_GUARD_FUNCTION();
    while (count-- > 0) {
        CreateItemStruct item;
        packet = ::Disassemble(packet, "dQ", &item.ItemID, &item.Count);
        items.push_back(item);
    }
    return packet;
}

} // namespace ItemUtil

namespace L2EtcMani {

const unsigned char* ParseEtcMani(int count, const unsigned char* packet) {
    // Current L2Server.exe 0x0071acdc. A missing factory entry consumes only
    // its type lane. Each created manipulation executes immediately and
    // is destroyed regardless of its DoAtServer result.
    L2_GUARD_FUNCTION();
    if (count > 0) {
        do {
            std::int16_t typeValue = 0;
            packet = ::Disassemble(packet, "h", &typeValue);
            EtcMani* const etcMani = Singleton<EtcManiFactory>::inst().CreateObject(static_cast<ETC_MANI_TYPE>(typeValue));
            if (etcMani != nullptr) {
                packet = etcMani->Disassemble(packet);
                if (!etcMani->DoAtServer()) {
                    etcMani->DumpError();
                }
                etcMani->Destroy();
            }
            --count;
        } while (count != 0);
    }
    return packet;
}

} // namespace L2EtcMani

EtcMani::EtcMani() = default;

EtcManiCreateItemAuctionBid::EtcManiCreateItemAuctionBid() = default;

EtcManiWithdrawItemAuctionBid::EtcManiWithdrawItemAuctionBid() = default;

EtcManiSuccessfulItemAuctionBid::EtcManiSuccessfulItemAuctionBid() = default;

int EtcManiCreateItemAuctionBid::Assemble(char* const buffer, const int bufferSize) {
    // Current L2Server.exe 0x00711bac.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddQd", ETC_MANI_CREATE_ITEM_AUCTION_BID, static_cast<int>(m_nUserSid),
        static_cast<int>(m_nUserDBID), m_nAuctionID, m_nBidID, static_cast<long long>(m_nBiddingPrice), m_nTransactionID
    );
}

int EtcManiCreateItemAuctionBid::Assemble(IPacketHelper& packet) {
    // Current L2Server.exe 0x00711c9c.
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddddQd", ETC_MANI_CREATE_ITEM_AUCTION_BID, static_cast<int>(m_nUserSid), static_cast<int>(m_nUserDBID),
        m_nAuctionID, m_nBidID, static_cast<long long>(m_nBiddingPrice), m_nTransactionID
    );
}

const unsigned char* EtcManiCreateItemAuctionBid::Disassemble(const unsigned char* const packet) {
    // Current L2Server.exe 0x00711d7c.
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddddQd", &m_nUserSid, &m_nUserDBID, &m_nAuctionID, &m_nBidID, &m_nBiddingPrice, &m_nTransactionID
    );
}

bool EtcManiCreateItemAuctionBid::DoAtCacheInternal() {
    // Current L2Server.exe 0x00711e54 is a guarded false stub.
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiCreateItemAuctionBid::Rollback() {
    // Current L2Server.exe 0x00711ed4 is a guarded false stub.
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiCreateItemAuctionBid::DoAtServer() {
    // Current L2Server.exe 0x0071ade8.
    L2_GUARD_FUNCTION();

    CAuction* const auction = CAuctionManager::inst().GetAuction(m_nAuctionID);
    if (auction == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] cannot find auction.. user[%d] bid[%d]",
        //     L"..\\Shared\\EtcMani.cpp",
        //     0x5f2,
        //     static_cast<int>(m_nUserDBID),
        //     m_nBidID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[auction] Cannot apply bid %d for character DB ID %u: auction %d was not found.",
        //     m_nBidID,
        //     static_cast<unsigned int>(m_nUserDBID),
        //     m_nAuctionID)
        L2_LOG_ERROR(
            Log, u"[auction] Cannot apply bid %d for character DB ID %u: auction %d was not found.", m_nBidID,
            static_cast<unsigned int>(m_nUserDBID), m_nAuctionID
        );
        return false;
    }
    if (m_nBidID <= 0) {
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_nUserSid);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    auction->ShowAuctionUI(user, false);
    const bool result = auction->CreateBid(static_cast<int>(m_nUserDBID), m_nBiddingPrice, m_nTransactionID);
    runtime::raw_atomic::DecrementI32(&auction->m_nAuctionDBJobCount);
    return result;
}

void EtcManiCreateItemAuctionBid::DumpError() {
    // Current L2Server.exe 0x00711f54 formats the QWORD
    // bidding-price lane with %d.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"EtcManiCreateItemAuctionBid::DumpError() => "
    //     L"user=[%d], auction id=[%d], bidding price=[%d], bid id=[%d]",
    //     static_cast<int>(m_nUserDBID),
    //     m_nAuctionID,
    //     m_nBiddingPrice,
    //     m_nBidID)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[auction] Bid request context: character DB ID %u, auction %d, price %lld, bid %d.",
    //     static_cast<unsigned int>(m_nUserDBID),
    //     m_nAuctionID,
    //     static_cast<long long>(m_nBiddingPrice),
    //     m_nBidID)
    L2_LOG_ERROR(
        Log, u"[auction] Bid request context: character DB ID %u, auction %d, price %lld, bid %d.",
        static_cast<unsigned int>(m_nUserDBID), m_nAuctionID, static_cast<long long>(m_nBiddingPrice), m_nBidID
    );
}

int EtcManiWithdrawItemAuctionBid::Assemble(char* const buffer, const int bufferSize) {
    // Current L2Server.exe 0x00712020.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddQd", ETC_MANI_WITHDRAW_ITEM_AUCTION_BID, m_nBidID, static_cast<int>(m_nUserDBID),
        m_nAuctionID, static_cast<long long>(m_nAdenaAmount), m_bLog ? 1 : 0
    );
}

int EtcManiWithdrawItemAuctionBid::Assemble(IPacketHelper& packet) {
    // Current L2Server.exe 0x0071210c.
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddQd", ETC_MANI_WITHDRAW_ITEM_AUCTION_BID, m_nBidID, static_cast<int>(m_nUserDBID), m_nAuctionID,
        static_cast<long long>(m_nAdenaAmount), m_bLog ? 1 : 0
    );
}

const unsigned char* EtcManiWithdrawItemAuctionBid::Disassemble(const unsigned char* const packet) {
    // Current L2Server.exe 0x007121e8. The d decode writes through the
    // one-byte bool into its target padding exactly as the current image.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddQd", &m_nBidID, &m_nUserDBID, &m_nAuctionID, &m_nAdenaAmount, &m_bLog);
}

bool EtcManiWithdrawItemAuctionBid::DoAtCacheInternal() {
    // Current L2Server.exe 0x007122b8 is a guarded false stub.
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiWithdrawItemAuctionBid::Rollback() {
    // Current L2Server.exe 0x00712338 is a guarded false stub.
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiWithdrawItemAuctionBid::DoAtServer() {
    // Current L2Server.exe 0x0071af44.
    L2_GUARD_FUNCTION();

    CAuction* const auction = CAuctionManager::inst().GetAuction(m_nAuctionID);
    if (auction == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] cannot find auction.. auction[%d] user[%d]",
        //     L"..\\Shared\\EtcMani.cpp",
        //     0x679,
        //     m_nAuctionID,
        //     static_cast<int>(m_nUserDBID))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[auction] Cannot apply bid-withdrawal response: auction %d was not found for character DB ID %u.",
        //     m_nAuctionID,
        //     static_cast<unsigned int>(m_nUserDBID))
        L2_LOG_ERROR(
            Log, u"[auction] Cannot apply bid-withdrawal response: auction %d was not found for character DB ID %u.",
            m_nAuctionID, static_cast<unsigned int>(m_nUserDBID)
        );
        return false;
    }

    const bool deleted = auction->DeleteBid(static_cast<int>(m_nUserDBID));
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(m_nUserDBID)));
    if (!deleted || !user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] cannot withdraw bidding price. "
        //     L"User[%d] Auction[%d] Adena[%I64d]",
        //     L"..\\Shared\\EtcMani.cpp",
        //     0x686,
        //     static_cast<int>(m_nUserDBID),
        //     m_nAuctionID,
        //     m_nAdenaAmount)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[auction] Bid-withdrawal response incomplete for character DB ID %u, auction %d, amount %lld: local bid %ls.
        //     character %ls.", static_cast<unsigned int>(m_nUserDBID), m_nAuctionID, static_cast<long long>(m_nAdenaAmount),
        //     deleted ? L"removed" : L"not removed",
        //     user ? L"available" : L"unavailable")
        L2_LOG_ERROR(
            Log,
            u"[auction] Bid-withdrawal response incomplete for character DB ID %u, auction %d, amount %lld: local bid %ls; "
            u"character %ls.",
            static_cast<unsigned int>(m_nUserDBID), m_nAuctionID, static_cast<long long>(m_nAdenaAmount),
            deleted ? L"removed" : L"not removed", user ? L"available" : L"unavailable"
        );
        return false;
    }
    if (!m_bLog) {
        return false;
    }

    runtime::raw_atomic::DecrementI32(&auction->m_nAuctionDBJobCount);

    std::int64_t oldAdenaAmount;
    {
        L2_READ_GUARD(user->m_inv);
        oldAdenaAmount = user->m_inv.GetAdenaAmount();
    }

    std::array<wchar_t, 25> characterName{};
    CopyEtcManiAuctionCharacterName(Utf16ToWide(user->GetName()).c_str(), characterName);
    std::array<wchar_t, 15> accountNameStorage{};
    const wchar_t* const accountName = EtcManiAuctionAccountName(user->m_sAccountName, accountNameStorage);
    const FVector position = user->GetPos();

    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%s,%s,"
        u",,",
        static_cast<std::int64_t>(0x3de), static_cast<std::int64_t>(user->GetDBID()),
        static_cast<std::int64_t>(user->GetAccountID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
        static_cast<std::int64_t>(auction->m_nAuctionDBID), static_cast<std::int64_t>(auction->m_AuctionItem.m_nClassID),
        auction->m_AuctionItem.m_nAmount, m_nAdenaAmount, static_cast<std::int64_t>(auction->m_AuctionItem.m_nOwnerDBID),
        oldAdenaAmount + m_nAdenaAmount, static_cast<std::int64_t>(m_nBidID), characterName.data(), accountName
    );
    return true;
}

void EtcManiWithdrawItemAuctionBid::DumpError() {
    // Current L2Server.exe 0x007123b8 is a guarded no-op.
    L2_GUARD_FUNCTION();
}

int EtcManiSuccessfulItemAuctionBid::Assemble(char* const buffer, const int bufferSize) {
    // Current L2Server.exe 0x007131d4.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddQddd", ETC_MANI_SUCCESSFUL_ITEM_AUCTION_BID, m_nBidID, static_cast<int>(m_nUserDBID),
        m_nAuctionID, static_cast<long long>(m_nAdenaAmount), m_nItemClassId, m_nItemCount, m_nAuctionNPCId
    );
}

int EtcManiSuccessfulItemAuctionBid::Assemble(IPacketHelper& packet) {
    // Current L2Server.exe 0x007132c8.
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddQddd", ETC_MANI_SUCCESSFUL_ITEM_AUCTION_BID, m_nBidID, static_cast<int>(m_nUserDBID), m_nAuctionID,
        static_cast<long long>(m_nAdenaAmount), m_nItemClassId, m_nItemCount, m_nAuctionNPCId
    );
}

const unsigned char* EtcManiSuccessfulItemAuctionBid::Disassemble(const unsigned char* const packet) {
    // Current L2Server.exe 0x007133b0.
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "dddQddd", &m_nBidID, &m_nUserDBID, &m_nAuctionID, &m_nAdenaAmount, &m_nItemClassId, &m_nItemCount,
        &m_nAuctionNPCId
    );
}

bool EtcManiSuccessfulItemAuctionBid::DoAtCacheInternal() {
    // Current L2Server.exe 0x00713494 is a guarded false stub.
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiSuccessfulItemAuctionBid::Rollback() {
    // Current L2Server.exe 0x00713514 is a guarded false stub.
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiSuccessfulItemAuctionBid::DoAtServer() {
    // Current L2Server.exe 0x0071b46c.
    L2_GUARD_FUNCTION();

    CAuction* const auction = CAuctionManager::inst().GetAuction(m_nAuctionID);
    if (auction == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] cannot find auction.. auction[%d] user[%d]",
        //     L"..\\Shared\\EtcMani.cpp",
        //     0xb6d,
        //     m_nAuctionID,
        //     static_cast<int>(m_nUserDBID))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[auction] Cannot apply winning-bid response: auction %d was not found for character DB ID %u.",
        //     m_nAuctionID,
        //     static_cast<unsigned int>(m_nUserDBID))
        L2_LOG_ERROR(
            Log, u"[auction] Cannot apply winning-bid response: auction %d was not found for character DB ID %u.",
            m_nAuctionID, static_cast<unsigned int>(m_nUserDBID)
        );
        return false;
    }

    const bool deleted = auction->DeleteBid(static_cast<int>(m_nUserDBID));
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(m_nUserDBID)));
    if (deleted && user) {
        user->PledgeMissionCheckAndAddCount(static_cast<PledgeMission::GoalType>(0x28), 0, 1);
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] cannot delete Successful bid price. "
    //     L"User[%d] Bid[%d] Auction[%d] Adena[%I64d]",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xb79,
    //     static_cast<int>(m_nUserDBID),
    //     m_nBidID,
    //     m_nAuctionID,
    //     m_nAdenaAmount)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[auction] Winning-bid response incomplete for character DB ID %u, bid %d, auction %d, amount %lld: local bid
    //     %ls, character %ls.", static_cast<unsigned int>(m_nUserDBID), m_nBidID, m_nAuctionID, static_cast<long
    //     long>(m_nAdenaAmount), deleted ? L"removed" : L"not removed", user ? L"available" : L"unavailable")
    L2_LOG_ERROR(
        Log,
        u"[auction] Winning-bid response incomplete for character DB ID %u, bid %d, auction %d, amount %lld: local bid %ls; "
        u"character %ls.",
        static_cast<unsigned int>(m_nUserDBID), m_nBidID, m_nAuctionID, static_cast<long long>(m_nAdenaAmount),
        deleted ? L"removed" : L"not removed", user ? L"available" : L"unavailable"
    );
    return false;
}

void EtcManiSuccessfulItemAuctionBid::DumpError() {
    // Current L2Server.exe 0x00713594 is a guarded no-op.
    L2_GUARD_FUNCTION();
}

int EtcMani::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    const int previousLength = packet.GetLength();
    const int assembled = Assemble(packet.GetPacket() + previousLength, packet.GetBufferSize() - previousLength);
    packet.SetLength(previousLength + assembled);
    return assembled;
}

int EtcMani::Assemble(char*, int) { return 0; }

const unsigned char* EtcMani::Disassemble(const unsigned char* packet) { return packet; }

void EtcMani::DumpError() {}

void EtcMani::Destroy() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

bool EtcMani::DoAtCache() {
    m_isSuccess = DoAtCacheInternal();
    return m_isSuccess;
}

bool EtcMani::IsSuccess() { return m_isSuccess; }

EtcManiUpdateUserEventTime::EtcManiUpdateUserEventTime() = default;

int EtcManiUpdateUserEventTime::Assemble(char* const buffer, const int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddddQdQdd", ETC_MANI_UPDATE_USER_EVENT_TIME, m_userDBID, m_timeGap, m_eventTime, m_eventType,
        m_eventVariable, m_nDelItemClassID, m_nDelItemAmount, m_nAddItemClassID, m_nAddItemAmount, m_nAddItemEnchant, m_nRet
    );
}

int EtcManiUpdateUserEventTime::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddddddQdQdd", ETC_MANI_UPDATE_USER_EVENT_TIME, m_userDBID, m_timeGap, m_eventTime, m_eventType, m_eventVariable,
        m_nDelItemClassID, m_nDelItemAmount, m_nAddItemClassID, m_nAddItemAmount, m_nAddItemEnchant, m_nRet
    );
}

const unsigned char* EtcManiUpdateUserEventTime::Disassemble(const unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddddddQdQdd", &m_userDBID, &m_timeGap, &m_eventTime, &m_eventType, &m_eventVariable, &m_nDelItemClassID,
        &m_nDelItemAmount, &m_nAddItemClassID, &m_nAddItemAmount, &m_nAddItemEnchant, &m_nRet
    );
}

bool EtcManiUpdateUserEventTime::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiUpdateUserEventTime::Rollback() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiUpdateUserEventTime::DoAtServer() {
    // Current L2Server.exe 0x00719020.
    L2_GUARD_FUNCTION();
    if (m_nRet == 1) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, m_userDBID));
        if (user) {
            CLogFormatter formatter;
            formatter.WriteLog2(
                0x232a, user.get(), nullptr, m_nDelItemClassID, m_nDelItemAmount, m_nAddItemClassID, m_nAddItemAmount,
                m_nAddItemEnchant, m_timeGap, 0
            );
        }
    }
    return false;
}

void EtcManiUpdateUserEventTime::DumpError() { L2_GUARD_FUNCTION(); }

EtcManiCardUpDownGame::EtcManiCardUpDownGame() = default;

int EtcManiCardUpDownGame::Assemble(char* const buffer, const int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddddd", ETC_MANI_CARD_UPDOWN_GAME, m_nReqType, m_nResult, m_nGameId, m_nRollbackGameState,
        m_nUserSid, m_nUserDBID
    );
}

int EtcManiCardUpDownGame::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddddd", ETC_MANI_CARD_UPDOWN_GAME, m_nReqType, m_nResult, m_nGameId, m_nRollbackGameState, m_nUserSid, m_nUserDBID
    );
}

const unsigned char* EtcManiCardUpDownGame::Disassemble(const unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "dddddd", &m_nReqType, &m_nResult, &m_nGameId, &m_nRollbackGameState, &m_nUserSid, &m_nUserDBID
    );
}

bool EtcManiCardUpDownGame::DoAtCacheInternal() { return false; }

bool EtcManiCardUpDownGame::Rollback() { return false; }

bool EtcManiCardUpDownGame::DoAtServer() {
    // Current L2Server.exe 0x00717ee8.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSid));
    User* const rawUser = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (rawUser == nullptr) {
        return false;
    }
    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);

    if (m_nResult == 0) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->m_UpDownGame.m_GamePlayState = static_cast<CardUpDownGame::GamePlayState>(m_nRollbackGameState);
        return false;
    }

    if (m_nReqType == 1) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->StartCardUpDownGame(m_nGameId, false);
    } else if (m_nReqType == 2) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->m_UpDownGame.OnRewardFinish(*rawUser);
    } else if (m_nReqType == 3) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->StartCardUpDownGame(m_nGameId, true);
    }
    return true;
}

void EtcManiCardUpDownGame::DumpError() {}

EtcManiRemoveInzoneBindingByEvent::EtcManiRemoveInzoneBindingByEvent() = default;

int EtcManiRemoveInzoneBindingByEvent::Assemble(char* const buffer, const int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddd", ETC_MANI_REMOVE_INZONE_BINDING_BY_EVENT, m_nUserSid, m_nResult, m_nInzoneTypeID,
        m_isGroupRestriction
    );
}

int EtcManiRemoveInzoneBindingByEvent::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddd", ETC_MANI_REMOVE_INZONE_BINDING_BY_EVENT, m_nUserSid, m_nResult, m_nInzoneTypeID, m_isGroupRestriction
    );
}

const unsigned char* EtcManiRemoveInzoneBindingByEvent::Disassemble(const unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddd", &m_nUserSid, &m_nResult, &m_nInzoneTypeID, &m_isGroupRestriction);
}

bool EtcManiRemoveInzoneBindingByEvent::DoAtCacheInternal() { return false; }

bool EtcManiRemoveInzoneBindingByEvent::Rollback() { return false; }

bool EtcManiRemoveInzoneBindingByEvent::DoAtServer() {
    // Current L2Server.exe 0x00718014.
    // Noise: routine instance-binding response entry is not an error. Report a zero-result refusal at the existing early
    // return below. Log.Add(
    //     LOG_ERROR,
    //     L"EtcManiRemoveInzoneBindingByEvent(%d, %d, %d, %d)",
    //     m_nUserSid,
    //     m_nInzoneTypeID,
    //     m_isGroupRestriction,
    //     m_nResult)
    if (m_nResult == 0) {
        // Log.Add(
        //     LOG_WARN,
        //     L"[instance] Binding reset skipped for character SID %d, instance type %d, restriction mode %d: response
        //     result is zero, no local reset applied.", m_nUserSid, m_nInzoneTypeID, m_isGroupRestriction)
        L2_LOG_WARN(
            Log,
            u"[instance] Binding reset skipped for character SID %d, instance type %d, restriction mode %d: response result "
            u"is zero; no local reset applied.",
            m_nUserSid, m_nInzoneTypeID, m_isGroupRestriction
        );
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSid));
    User* const rawUser = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (rawUser == nullptr) {
        return false;
    }
    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);
    user->ResetInZoneRestriction(m_nInzoneTypeID, m_isGroupRestriction == 1);
    return true;
}

void EtcManiRemoveInzoneBindingByEvent::DumpError() {}

EtcManiUpdatePledgeNameValue::EtcManiUpdatePledgeNameValue() = default;

EtcManiUpdatePledgeNameValue::EtcManiUpdatePledgeNameValue(
    const int worldId, const int userSid, const unsigned int pledgeDbId, const int delta
) :
    m_worldId(worldId), m_userSid(userSid), m_pledgeDbId(pledgeDbId), m_delta(delta) {}

int EtcManiUpdatePledgeNameValue::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddd", ETC_MANI_UPDATE_PLEDGE_NAMEVALUE, m_worldId, m_userSid, static_cast<int>(m_pledgeDbId), m_delta
    );
}

int EtcManiUpdatePledgeNameValue::Assemble(char* buffer, const int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddd", ETC_MANI_UPDATE_PLEDGE_NAMEVALUE, m_worldId, m_userSid, static_cast<int>(m_pledgeDbId),
        m_delta
    );
}

const unsigned char* EtcManiUpdatePledgeNameValue::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddd", &m_worldId, &m_userSid, &m_pledgeDbId, &m_delta);
}

bool EtcManiUpdatePledgeNameValue::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiUpdatePledgeNameValue::Rollback() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiUpdatePledgeNameValue::DoAtServer() {
    // Current L2Server.exe 0x00716170.
    L2_GUARD_FUNCTION();
    CSPointer<CPledge> pledge = CPledge::FindPledge(m_worldId, static_cast<int>(m_pledgeDbId));
    if (!pledge) {
        return false;
    }

    const int appliedDelta = m_delta > 0 ? static_cast<int>(static_cast<double>(m_delta) * g_dPledgeNameValuePer) : m_delta;
    pledge->UpdateNameValue(appliedDelta);

    if (m_delta < 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user != nullptr) {
            user->GetSocket()->SendSystemMessage_d(0x6fb, -m_delta);
        }
    } else {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user != nullptr) {
            PledgeContributionSystem::inst().AddPoint(user, pledge.get(), m_delta);
            user->GetSocket()->SendSystemMessage_d(0x1257, m_delta);
        }
    }
    return true;
}

void EtcManiUpdatePledgeNameValue::DumpError() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"EtcManiUpdatePledgeNameValue::DumpError() => "
    //     L"PledgeID=[%d], NameValueDelta=[%d]",
    //     static_cast<int>(m_pledgeDbId),
    //     m_delta)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[clan] Reputation update context: clan DB ID %u, delta %d.",
    //     static_cast<unsigned int>(m_pledgeDbId),
    //     m_delta)
    L2_LOG_ERROR(
        Log, u"[clan] Reputation update context: clan DB ID %u, delta %d.", static_cast<unsigned int>(m_pledgeDbId), m_delta
    );
}

EtcManiUpdateUserItemBaseAttribute::EtcManiUpdateUserItemBaseAttribute() = default;

int EtcManiUpdateUserItemBaseAttribute::Assemble(IPacketHelper& packet) {
    // Current L2Server.exe 0x007113d4.
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddddhhhhhhhh", ETC_MANI_UPDATE_USER_ITEM_BASE_ATTRIBUTE, static_cast<int>(m_nUserSid), m_nUserDBID,
        m_nTargetItemDBID, m_nIntensiveItemClassId, m_nEnchantType, static_cast<int>(m_Attribute.m_nAttackType),
        static_cast<int>(m_Attribute.m_nAttackValue), static_cast<int>(m_Attribute.m_nDefendValue[0]),
        static_cast<int>(m_Attribute.m_nDefendValue[1]), static_cast<int>(m_Attribute.m_nDefendValue[2]),
        static_cast<int>(m_Attribute.m_nDefendValue[3]), static_cast<int>(m_Attribute.m_nDefendValue[4]),
        static_cast<int>(m_Attribute.m_nDefendValue[5])
    );
}

int EtcManiUpdateUserItemBaseAttribute::Assemble(char* buffer, const int bufferSize) {
    // Current L2Server.exe 0x0071129c.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddddhhhhhhhh", ETC_MANI_UPDATE_USER_ITEM_BASE_ATTRIBUTE, static_cast<int>(m_nUserSid),
        m_nUserDBID, m_nTargetItemDBID, m_nIntensiveItemClassId, m_nEnchantType, static_cast<int>(m_Attribute.m_nAttackType),
        static_cast<int>(m_Attribute.m_nAttackValue), static_cast<int>(m_Attribute.m_nDefendValue[0]),
        static_cast<int>(m_Attribute.m_nDefendValue[1]), static_cast<int>(m_Attribute.m_nDefendValue[2]),
        static_cast<int>(m_Attribute.m_nDefendValue[3]), static_cast<int>(m_Attribute.m_nDefendValue[4]),
        static_cast<int>(m_Attribute.m_nDefendValue[5])
    );
}

const unsigned char* EtcManiUpdateUserItemBaseAttribute::Disassemble(const unsigned char* packet) {
    // Current L2Server.exe 0x007114f8.
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "dddddhhhhhhhh", &m_nUserSid, &m_nUserDBID, &m_nTargetItemDBID, &m_nIntensiveItemClassId, &m_nEnchantType,
        &m_Attribute.m_nAttackType, &m_Attribute.m_nAttackValue, &m_Attribute.m_nDefendValue[0],
        &m_Attribute.m_nDefendValue[1], &m_Attribute.m_nDefendValue[2], &m_Attribute.m_nDefendValue[3],
        &m_Attribute.m_nDefendValue[4], &m_Attribute.m_nDefendValue[5]
    );
}

bool EtcManiUpdateUserItemBaseAttribute::Rollback() {
    // Current L2Server.exe 0x007116a8 is a guarded false stub.
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiUpdateUserItemBaseAttribute::DoAtCacheInternal() {
    // Current L2Server.exe 0x00711628 is a guarded false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiUpdateUserItemBaseAttribute::DumpError() {
    // Current L2Server.exe 0x00711728 is a guarded no-op.
    L2_GUARD_FUNCTION();
}

bool EtcManiUpdateUserItemBaseAttribute::DoAtServer() {
    // Current L2Server.exe 0x00716748.
    L2_GUARD_FUNCTION();

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_nUserSid);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }

    user->SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_ENCHANT);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);

    {
        L2_WRITE_GUARD(user->m_inv);
        CItem* const item = user->m_inv.GetByDBID(m_nTargetItemDBID);
        if (item != nullptr) {
            CUserSocket* const socket = user->GetSocket();
            if (item->m_itemBaseAttribute != m_Attribute) {
                char16_t oldAttribute[256]{};
                char16_t newAttribute[256]{};
                char16_t option[256]{};
                item->m_itemBaseAttribute.GetLogString(oldAttribute);
                m_Attribute.GetLogString(newAttribute);
                FormatUtf16To(
                    option, std::size(option), u"%d:%d:%d", item->m_itemOption.intensiveItemClassID,
                    item->m_itemOption.itemOptionKey.variationOption1, item->m_itemOption.itemOptionKey.variationOption2
                );

                const bool weaponForResult = item->IsWeapon();
                const int oldValue = weaponForResult ? item->m_itemBaseAttribute.m_nAttackValue
                                                     : item->m_itemBaseAttribute.m_nDefendValue[m_nEnchantType];
                const int newValue =
                    weaponForResult ? m_Attribute.m_nAttackValue : m_Attribute.m_nDefendValue[m_nEnchantType];
                socket->Send(
                    "chdchhhhh", 0xfe, 0x62, 0, static_cast<int>(item->IsWeapon()), m_nEnchantType, oldValue, newValue,
                    static_cast<int>(static_cast<std::int16_t>(user->nAttributeCount)),
                    static_cast<int>(static_cast<std::int16_t>(user->nAttributeFailCount))
                );

                {
                    L2_GUARD(*item->m_lock);
                    item->m_itemBaseAttribute = m_Attribute;
                }
                user->m_inv.SetInventoryChanged(item, IUT_UPDATE);

                const int enchanted = item->m_data->GetEnchanted();
                const int itemClassId = item->m_data->m_nClassID;
                if (enchanted > 0) {
                    if (item->IsWeapon()) {
                        socket->Send(
                            "chccccdcc", 0x62, 0x864, 3, 0x13, static_cast<int>(static_cast<std::int8_t>(enchanted)), 3,
                            itemClassId, 9, static_cast<int>(static_cast<std::int8_t>(m_nEnchantType))
                        );
                    } else {
                        socket->Send(
                            "chccccdcccc", 0x62, 0xc5b, 4, 0x13, static_cast<int>(static_cast<std::int8_t>(enchanted)), 3,
                            itemClassId, 9,
                            static_cast<int>(static_cast<std::int8_t>(CItem::GetOppositeAttribute(m_nEnchantType))), 9,
                            static_cast<int>(static_cast<std::int8_t>(m_nEnchantType))
                        );
                    }
                } else if (item->IsWeapon()) {
                    socket->Send(
                        "chccdcc", 0x62, 0x863, 2, 3, itemClassId, 9,
                        static_cast<int>(static_cast<std::int8_t>(m_nEnchantType))
                    );
                } else {
                    socket->Send(
                        "chccdcccc", 0x62, 0xc48, 3, 3, itemClassId, 9,
                        static_cast<int>(static_cast<std::int8_t>(CItem::GetOppositeAttribute(m_nEnchantType))), 9,
                        static_cast<int>(static_cast<std::int8_t>(m_nEnchantType))
                    );
                }

                const FVector position = user->GetPos();
                const std::wstring accountName = Utf16ToWide(std::u16string_view(user->m_sAccountName.data()));
                // Log.Add(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,"
                //     L"%I64d,%I64d,%I64d,%s,%s,%s,"
                //     L"%I64d,%I64d,%I64d,%I64d,%I64d,"
                //     L",,,,,%s,%s,,,%I64d",
                //     static_cast<std::int64_t>(0x3d6),
                //     static_cast<std::int64_t>(user->GetDBID()),
                //     static_cast<std::int64_t>(user->GetAccountID()),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.x)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.y)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.z)),
                //     oldAttribute,
                //     newAttribute,
                //     option,
                //     static_cast<std::int64_t>(itemClassId),
                //     static_cast<std::int64_t>(m_nIntensiveItemClassId),
                //     static_cast<std::int64_t>(enchanted),
                //     user->nAttributeCount,
                //     static_cast<std::int64_t>(
                //         item->m_data->GetShapeShiftedClassId()),
                //     user->GetName(),
                //     accountName.c_str(),
                //     static_cast<std::int64_t>(item->GetDBID()))
                Log.Add(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,"
                    u"%I64d,%I64d,%I64d,%s,%s,%s,"
                    u"%I64d,%I64d,%I64d,%I64d,%I64d,"
                    u",,,,,%s,%s,,,%I64d",
                    static_cast<std::int64_t>(0x3d6), static_cast<std::int64_t>(user->GetDBID()),
                    static_cast<std::int64_t>(user->GetAccountID()),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)), oldAttribute, newAttribute,
                    option, static_cast<std::int64_t>(itemClassId), static_cast<std::int64_t>(m_nIntensiveItemClassId),
                    static_cast<std::int64_t>(enchanted), user->nAttributeCount,
                    static_cast<std::int64_t>(item->m_data->GetShapeShiftedClassId()), user->GetName(), accountName.c_str(),
                    static_cast<std::int64_t>(item->GetDBID())
                );

                CLogFormatter formatter;
                formatter.WriteLog1(
                    0x2347, user, nullptr, oldAttribute, newAttribute, option, 0, 0, 0, 0, 0, 0, item->GetDBID()
                );
                g_npcserver.Send("cdddd", 0xc6, user->m_data->index, 0, 4, 0);
            } else {
                socket->Send(
                    "chdhh", 0xfe, 0x62, 1, 0, static_cast<int>(static_cast<std::int16_t>(user->nAttributeFailCount))
                );
                user->SendSystemMessage(0x865);

                char16_t attribute[256]{};
                char16_t ensoul[256]{};
                char16_t option[256]{};
                item->m_itemBaseAttribute.GetLogString(attribute);
                item->MakeEnsoulLogInfo(ensoul);
                FormatUtf16To(
                    option, std::size(option), u"%d:%d:%d", item->m_itemOption.intensiveItemClassID,
                    item->m_itemOption.itemOptionKey.variationOption1, item->m_itemOption.itemOptionKey.variationOption2
                );

                CLogFormatter formatter;
                formatter.WriteLog4(
                    0x3d7, user, nullptr, attribute, ensoul, option, item->m_data->m_nClassID, m_nIntensiveItemClassId,
                    item->m_data->GetEnchanted(), user->nAttributeFailCount, item->m_data->GetShapeShiftedClassId(), 0, 0, 0,
                    0, 0, item->GetDBID()
                );
            }
        }
    }
    user->ValidateChanged(VB_MAX);
    user->m_nParamChanged |= PCB_USER;
    user->SendItemList(true, true, nullptr);
    user->nAttributeCount = 0;
    user->nAttributeFailCount = 0;
    return true;
}

EtcManiGiveItemOnAccount::EtcManiGiveItemOnAccount() : m_nType(101) {}

EtcManiGiveItemOnAccount::EtcManiGiveItemOnAccount(
    const int userDbId,
    const int condition,
    const int deleteItemClassId,
    const std::int64_t deleteItemAmount,
    const int addItemCount,
    const int* const addItemClassIds,
    const std::int64_t* const addItemAmounts,
    const int* const addItemEnchants,
    const int type,
    const unsigned int accountId
) :
    m_userDBID(userDbId),
    m_nCond(condition),
    m_nAccountID(accountId),
    m_nDelItemClassID(deleteItemClassId),
    m_nDelItemAmount(deleteItemAmount),
    m_nAddItemCount(addItemCount),
    m_nType(type) {
    for (int index = 0; index < addItemCount; ++index) {
        const std::size_t offset = static_cast<std::size_t>(index);
        m_AddItemClassID[offset] = addItemClassIds[index];
        m_AddItemAmount[offset] = addItemAmounts[index];
        m_AddItemEnchant[offset] = addItemEnchants[index];
    }
}

EtcManiGiveItemOnAccount::EtcManiGiveItemOnAccount(
    const int userDbId,
    const int condition,
    const int deleteItemClassId,
    const std::int64_t deleteItemAmount,
    const int addItemClassId,
    const std::int64_t addItemAmount,
    const int addItemEnchant,
    const int type,
    const unsigned int accountId,
    const int npcId,
    const wchar_t* const eventName,
    const int eventStartTime
) :
    m_userDBID(userDbId),
    m_nCond(condition),
    m_nAccountID(accountId),
    m_nDelItemClassID(deleteItemClassId),
    m_nDelItemAmount(deleteItemAmount),
    m_nAddItemCount(1),
    m_nType(type),
    m_nNpcId(npcId),
    m_nEventStartTime(eventStartTime) {
    m_AddItemClassID[0] = addItemClassId;
    m_AddItemAmount[0] = addItemAmount;
    m_AddItemEnchant[0] = addItemEnchant;

    if (eventName == nullptr) {
        m_sEventName.fill(u'\0');
        return;
    }

    const std::u16string converted = WideToUtf16(std::wstring_view(eventName));
    const std::size_t count = std::min<std::size_t>(converted.size(), m_sEventName.size() - 1);
    std::copy_n(converted.begin(), count, m_sEventName.begin());
    m_sEventName[count] = u'\0';
}

int EtcManiGiveItemOnAccount::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    packet.Assemble("hdd", ETC_MANI_GIVE_ITEM_ON_ACCOUNT, m_userDBID, m_nCond);
    packet.Assemble("dQ", m_nDelItemClassID, static_cast<long long>(m_nDelItemAmount));
    packet.Assemble("d", m_nAddItemCount);
    for (int index = 0; index < m_nAddItemCount; ++index) {
        packet.Assemble(
            "dQd", m_AddItemClassID[static_cast<std::size_t>(index)],
            static_cast<long long>(m_AddItemAmount[static_cast<std::size_t>(index)]),
            m_AddItemEnchant[static_cast<std::size_t>(index)]
        );
    }
    packet.Assemble("d", static_cast<int>(m_nAccountID));
    packet.Assemble("dd", m_nRet, m_nType);
    return packet.Assemble("dSd", m_nNpcId, m_sEventName.data(), m_nEventStartTime);
}

int EtcManiGiveItemOnAccount::Assemble(char* const buffer, const int bufferSize) {
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> payload;
    payload.Assemble("dd", m_userDBID, m_nCond);
    payload.Assemble("dQ", m_nDelItemClassID, static_cast<long long>(m_nDelItemAmount));
    payload.Assemble("d", m_nAddItemCount);
    for (int index = 0; index < m_nAddItemCount; ++index) {
        payload.Assemble(
            "dQd", m_AddItemClassID[static_cast<std::size_t>(index)],
            static_cast<long long>(m_AddItemAmount[static_cast<std::size_t>(index)]),
            m_AddItemEnchant[static_cast<std::size_t>(index)]
        );
    }
    payload.Assemble("d", static_cast<int>(m_nAccountID));
    payload.Assemble("dd", m_nRet, m_nType);
    payload.Assemble("dSd", m_nNpcId, m_sEventName.data(), m_nEventStartTime);
    return ::Assemble(buffer, bufferSize, "hb", ETC_MANI_GIVE_ITEM_ON_ACCOUNT, payload.GetLength(), payload.GetPacket());
}

const unsigned char* EtcManiGiveItemOnAccount::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    packet = ::Disassemble(packet, "dddQd", &m_userDBID, &m_nCond, &m_nDelItemClassID, &m_nDelItemAmount, &m_nAddItemCount);
    for (int index = 0; index < m_nAddItemCount; ++index) {
        packet = ::Disassemble(
            packet, "dQd", &m_AddItemClassID[static_cast<std::size_t>(index)],
            &m_AddItemAmount[static_cast<std::size_t>(index)], &m_AddItemEnchant[static_cast<std::size_t>(index)]
        );
    }
    packet = ::Disassemble(packet, "d", &m_nAccountID);
    packet = ::Disassemble(packet, "dd", &m_nRet, &m_nType);
    return ::Disassemble(
        packet, "dSd", &m_nNpcId, static_cast<int>(m_sEventName.size() * sizeof(char16_t)), m_sEventName.data(),
        &m_nEventStartTime
    );
}

bool EtcManiGiveItemOnAccount::Rollback() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiGiveItemOnAccount::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiGiveItemOnAccount::DumpError() { L2_GUARD_FUNCTION(); }

bool EtcManiGiveItemOnAccount::DoAtServer() {
    L2_GUARD_FUNCTION();

    if (m_nType != 100) {
        if (m_nType == 0 && m_nRet == 1) {
            CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, m_userDBID));
            if (user) {
                user->RequestAccountStamp(m_nType, false, true);
            }
        } else if (m_nType == 2 && m_nRet == 1) {
            VipAttendanceSystem& attendance = VipAttendanceSystem::Inst();
            attendance.AddRewardLog(m_nAccountID, m_AddItemClassID.data(), m_AddItemAmount.data());

            CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, m_userDBID));
            if (user) {
                user->RequestSendVipAttendanceResult();
                attendance.ReleaseVIPAttendanceFlag(m_nAccountID);
                return true;
            }

            // Log.Add(
            //     LOG_ERROR,
            //     "FILE[%s] LINE[%d] VipAttendance Mani error Invalid DBID[%d]",
            //     L"..\\Shared\\EtcMani.cpp",
            //     0x806,
            //     m_userDBID)
            // Log.Add(
            //     LOG_WARN,
            //     L"[rewards] VIP attendance result not sent: character DB ID %d is unavailable (account %u). Releasing the
            //     attendance flag.", m_userDBID, m_nAccountID)
            L2_LOG_WARN(
                Log,
                u"[rewards] VIP attendance result not sent: character DB ID %d is unavailable (account %u); releasing the "
                u"attendance flag.",
                m_userDBID, m_nAccountID
            );
            attendance.ReleaseVIPAttendanceFlag(m_nAccountID);
        }
    }

    if (m_nNpcId > 0 && m_sEventName[0] != u'\0' && m_nRet == 1) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, m_userDBID));
        if (!user) {
            CSPointer<User> retry = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, m_userDBID));
            if (retry) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"EtcManiGiveItemOnAccount DoAtServer() Fail.")
                // Log.Add(
                //     LOG_WARN,
                //     L"[rewards] Account-item event audit skipped for character DB ID %d: the first lookup failed although
                //     the second lookup found the character.", m_userDBID)
                L2_LOG_WARN(
                    Log,
                    u"[rewards] Account-item event audit skipped for character DB ID %d: the first lookup failed although "
                    u"the second lookup found the character.",
                    m_userDBID
                );
            }
        } else {
            CSPointer<CNPC> npc = g_NPCMap.FindObject(m_nNpcId);
            CNPC* target = nullptr;
            if (npc && npc->IsNPC()) {
                target = npc->CastNPC();
            }
            const std::wstring eventName = Utf16ToWide(std::u16string_view(m_sEventName.data()));
            CLogFormatter formatter;
            formatter.WriteLog1(
                0x8c, user.get(), target, WideToUtf16(const_cast<wchar_t*>(eventName.c_str())), nullptr, nullptr,
                m_AddItemClassID[0], m_AddItemAmount[0], 0, 0, 0, 0, 0
            );
        }
    }
    return false;
}

CETC_MANI_PLEDGE_MASTERY_SET::CETC_MANI_PLEDGE_MASTERY_SET() :
    m_idPledge(0), m_idMastery(PledgeMasteryID::NONE), m_nPoint(0), m_idUser(0) {
    // Current L2Server.exe 0x007156a8. m_idWorld and m_nPrevPoint are
    // not initialized by the source constructor.
}

CETC_MANI_PLEDGE_MASTERY_SET::CETC_MANI_PLEDGE_MASTERY_SET(
    const int worldId, const int pledgeId, const PledgeMasteryID::Type masteryId, const int point, const unsigned int userId
) :
    m_idWorld(worldId), m_idPledge(pledgeId), m_idMastery(masteryId), m_nPoint(point), m_idUser(userId) {
    // Current L2Server.exe 0x007156d8. m_nPrevPoint is cache-side rollback
    // state and is not initialized here.
}

int CETC_MANI_PLEDGE_MASTERY_SET::Assemble(char* const buffer, const int bufferSize) {
    // Current L2Server.exe 0x00713c6c.
    return ::Assemble(
        buffer, bufferSize, "hddddd", ETC_MANI_PLEDGE_MASTERY_SET, m_idWorld, m_idPledge, static_cast<int>(m_idMastery),
        m_nPoint, static_cast<int>(m_idUser)
    );
}

const unsigned char* CETC_MANI_PLEDGE_MASTERY_SET::Disassemble(const unsigned char* const packet) {
    // Current L2Server.exe 0x00713cb8.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddd", &m_idWorld, &m_idPledge, &m_idMastery, &m_nPoint, &m_idUser);
}

bool CETC_MANI_PLEDGE_MASTERY_SET::Rollback() {
    // Current L2Server.exe 0x00713b24.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_PLEDGE_MASTERY_SET::DoAtCacheInternal() {
    // Current L2Server.exe 0x00713ba4.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_PLEDGE_MASTERY_SET::DoAtServer() {
    // Current L2Server.exe 0x0071811c.
    L2_GUARD_FUNCTION();

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_idUser);
    User* const rawUser = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    CSPointer<User> retainedUser(rawUser, ObjectRefType::FunctionChain);

    CSPointer<CPledge> pledge = CPledge::FindPledge(m_idWorld, m_idPledge);
    if (!pledge) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No pledge: %d",
        //     L"..\\Shared\\EtcMani.cpp",
        //     0xd3e,
        //     m_idPledge)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[clan] Cannot apply mastery update: clan %d in world %d was not found (mastery %d, points %d).",
        //     m_idPledge,
        //     m_idWorld,
        //     static_cast<int>(m_idMastery),
        //     m_nPoint)
        L2_LOG_ERROR(
            Log, u"[clan] Cannot apply mastery update: clan %d in world %d was not found (mastery %d, points %d).",
            m_idPledge, m_idWorld, static_cast<int>(m_idMastery), m_nPoint
        );
        return false;
    }

    pledge->GetMastery()->OnSetPoint(m_idMastery, m_nPoint);
    return true;
}

void CETC_MANI_PLEDGE_MASTERY_SET::DumpError() {
    // Current L2Server.exe 0x00713c24.
    // Noise: the only false result of this server operation is the missing clan reported above with the complete mastery
    // request. Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d]"
    //     L"CETC_MANI_PLEDGE_MASTERY_SET::"
    //     L"DumpError(): %d, %d, %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xd4e,
    //     m_idPledge,
    //     static_cast<int>(m_idMastery),
    //     m_nPoint)
}

CETC_MANI_PLEDGE_MASTERY_RESET::CETC_MANI_PLEDGE_MASTERY_RESET() : m_idWorld(0), m_idPledge(0), m_idUser(0) {
    // Current L2Server.exe 0x0071573c. m_anPrevPoint is cache-side rollback
    // state and is not initialized here.
}

CETC_MANI_PLEDGE_MASTERY_RESET::CETC_MANI_PLEDGE_MASTERY_RESET(
    const int worldId, const int pledgeId, const unsigned int userId
) :
    m_idWorld(worldId), m_idPledge(pledgeId), m_idUser(userId) {
    // Current L2Server.exe 0x0071576c.
}

int CETC_MANI_PLEDGE_MASTERY_RESET::Assemble(char* const buffer, const int bufferSize) {
    // Current L2Server.exe 0x00713d88.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddd", ETC_MANI_PLEDGE_MASTERY_RESET, m_idWorld, m_idPledge, static_cast<int>(m_idUser)
    );
}

const unsigned char* CETC_MANI_PLEDGE_MASTERY_RESET::Disassemble(const unsigned char* const packet) {
    // Current L2Server.exe 0x00713e60.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddd", &m_idWorld, &m_idPledge, &m_idUser);
}

bool CETC_MANI_PLEDGE_MASTERY_RESET::Rollback() {
    // Current L2Server.exe 0x00713f9c.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_PLEDGE_MASTERY_RESET::DoAtCacheInternal() {
    // Current L2Server.exe 0x00713f1c.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_PLEDGE_MASTERY_RESET::DoAtServer() {
    // Current L2Server.exe 0x007182f4.
    L2_GUARD_FUNCTION();

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_idUser);
    User* const rawUser = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    CSPointer<User> retainedUser(rawUser, ObjectRefType::FunctionChain);

    CSPointer<CPledge> pledge = CPledge::FindPledge(m_idWorld, m_idPledge);
    if (!pledge) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No pledge: %d",
        //     L"..\\Shared\\EtcMani.cpp",
        //     0xde8,
        //     m_idPledge)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[clan] Cannot apply mastery reset: clan %d in world %d was not found.",
        //     m_idPledge,
        //     m_idWorld)
        L2_LOG_ERROR(Log, u"[clan] Cannot apply mastery reset: clan %d in world %d was not found.", m_idPledge, m_idWorld);
        return false;
    }

    pledge->GetMastery()->OnResetPoint();
    return true;
}

void CETC_MANI_PLEDGE_MASTERY_RESET::DumpError() {
    // Current L2Server.exe 0x0071401c.
    // Noise: the only false result of this server operation is the missing clan already reported above.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]"
    //     L"CETC_MANI_PLEDGE_MASTERY_RESET::DumpError(): %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xdf8,
    //     m_idPledge)
}

CETC_MANI_POINT_UPDATE::CETC_MANI_POINT_UPDATE() : m_sidUser(0), m_dbidUser(0), m_pointType(PT_NONE), m_pointAmount(0) {
    // Current L2Server.exe constructor paired with 0x00714054.
}

CETC_MANI_POINT_UPDATE::CETC_MANI_POINT_UPDATE(
    const unsigned int userSid, const int userDbId, const UserPointType pointType, const int pointAmount
) :
    m_sidUser(userSid), m_dbidUser(userDbId), m_pointType(pointType), m_pointAmount(pointAmount) {}

int CETC_MANI_POINT_UPDATE::Assemble(char* const buffer, const int bufferSize) {
    // Current L2Server.exe 0x00714054.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddd", ETC_MANI_POINT_UPDATE, static_cast<int>(m_sidUser), m_dbidUser,
        static_cast<int>(m_pointType), m_pointAmount
    );
}

const unsigned char* CETC_MANI_POINT_UPDATE::Disassemble(const unsigned char* const packet) {
    // Current L2Server.exe 0x00714134.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddd", &m_sidUser, &m_dbidUser, &m_pointType, &m_pointAmount);
}

bool CETC_MANI_POINT_UPDATE::Rollback() {
    // Current L2Server.exe 0x00714278.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_POINT_UPDATE::DoAtCacheInternal() {
    // Current L2Server.exe 0x007141f8.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_POINT_UPDATE::DoAtServer() {
    // Current L2Server.exe 0x007192e8.
    L2_GUARD_FUNCTION();

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, m_dbidUser));
    if (!user) {
        return true;
    }

    const int pointType = static_cast<int>(m_pointType);
    if (m_pointType == PT_PVP_POINT || m_pointType == PT_RAID_POINT) {
        if (m_pointAmount < 1) {
            user->AddPoint(pointType, m_pointAmount, false);
        } else if ((m_pointType != PT_PVP_POINT || user->CanGetPVPPoint()) &&
            user->CheckPointAddable(pointType, m_pointAmount)) {
            user->AddPoint(pointType, m_pointAmount, false);
            user->SendSystemMessage_d(0xe1f, m_pointAmount);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Cannot get pvp or raid point: "
            //     L"%d, %d, %d",
            //     L"..\\Shared\\EtcMani.cpp",
            //     0xeb0,
            //     m_dbidUser,
            //     pointType,
            //     m_pointAmount)
            // Log.Add(
            //     LOG_WARN,
            //     L"[points] Increase skipped for character DB ID %d: type %d, amount %d. Eligibility or point-limit check
            //     failed.", m_dbidUser, pointType, m_pointAmount)
            L2_LOG_WARN(
                Log,
                u"[points] Increase skipped for character DB ID %d: type %d, amount %d; eligibility or point-limit check "
                u"failed.",
                m_dbidUser, pointType, m_pointAmount
            );
        }
    } else if (m_pointType == PT_PCCAFE_POINT) {
        user->AddPoint(pointType, m_pointAmount, false);
    }
    return true;
}

void CETC_MANI_POINT_UPDATE::DumpError() {
    // Current L2Server.exe 0x007142f8.
    // Noise: this server specialization always returns true. Its dispatcher never calls this failure dump.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]DumpError: %d, %d, %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xecc,
    //     m_dbidUser,
    //     static_cast<int>(m_pointType),
    //     m_pointAmount)
}

CETC_MANI_PLEDGE_MISSION_REWARD::CETC_MANI_PLEDGE_MISSION_REWARD() :
    nUserDbId_(0), nMissionId_(0), nCount_(0), tSaveRewardTime_(0), tRewardTime_(0) {}

CETC_MANI_PLEDGE_MISSION_REWARD::CETC_MANI_PLEDGE_MISSION_REWARD(
    const int userDbId, const int missionId, const int count, const std::int32_t saveRewardTime
) :
    nUserDbId_(userDbId), nMissionId_(missionId), nCount_(count), tSaveRewardTime_(saveRewardTime), tRewardTime_(0) {}

int CETC_MANI_PLEDGE_MISSION_REWARD::Assemble(char* const buffer, const int bufferSize) {
    // Current L2Server.exe 0x00714340.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddd", ETC_MANI_PLEDGE_MISSION_REWARD, nUserDbId_, nMissionId_, nCount_, tSaveRewardTime_,
        tRewardTime_
    );
}

const unsigned char* CETC_MANI_PLEDGE_MISSION_REWARD::Disassemble(const unsigned char* const packet) {
    // Current L2Server.exe 0x00714424.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddd", &nUserDbId_, &nMissionId_, &nCount_, &tSaveRewardTime_, &tRewardTime_);
}

bool CETC_MANI_PLEDGE_MISSION_REWARD::Rollback() {
    // Current L2Server.exe 0x00714574.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_PLEDGE_MISSION_REWARD::DoAtCacheInternal() {
    // Current L2Server.exe 0x007144f4.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_PLEDGE_MISSION_REWARD::DoAtServer() {
    // Current L2Server.exe 0x00719534.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, nUserDbId_));
    if (user) {
        user->PledgeMissionSuccessReward(nMissionId_, tRewardTime_);
    }
    return true;
}

void CETC_MANI_PLEDGE_MISSION_REWARD::DumpError() {
    // Current L2Server.exe 0x007145f4.
    // Noise: this server specialization always returns true. Its dispatcher never calls this failure dump.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d][Mission]DumpError: %d, %d, %d, %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xf53,
    //     nUserDbId_,
    //     nMissionId_,
    //     nCount_,
    //     tSaveRewardTime_)
}

CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::CETC_MANI_PLEDGE_CONTRIBUTION_REWARD() :
    m_sidUser(0), m_dbidUser(0), m_idPledge(0), m_nRewardStep(0), m_nMinPoint(0) {}

CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::CETC_MANI_PLEDGE_CONTRIBUTION_REWARD(
    const unsigned int userSid, const int userDbId, const int pledgeId, const int rewardStep, const int minPoint
) :
    m_sidUser(userSid), m_dbidUser(userDbId), m_idPledge(pledgeId), m_nRewardStep(rewardStep), m_nMinPoint(minPoint) {}

int CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::Assemble(char* const buffer, const int bufferSize) {
    // Current L2Server.exe 0x00714640.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddd", ETC_MANI_PLEDGE_CONTRIBUTION_REWARD, static_cast<int>(m_sidUser), m_dbidUser,
        m_idPledge, m_nRewardStep, m_nMinPoint
    );
}

const unsigned char* CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::Disassemble(const unsigned char* const packet) {
    // Current L2Server.exe 0x00714724.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddd", &m_sidUser, &m_dbidUser, &m_idPledge, &m_nRewardStep, &m_nMinPoint);
}

bool CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::Rollback() {
    // Current L2Server.exe 0x007147f4.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::DoAtCacheInternal() {
    // Current L2Server.exe 0x00714874.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::DoAtServer() {
    // Current L2Server.exe 0x007148f4 acknowledges success.
    L2_GUARD_FUNCTION();
    return true;
}

void CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::DumpError() {
    // Current L2Server.exe 0x00714974.
    // Noise: this server specialization always returns true. Its dispatcher never calls this failure dump.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]DumpError: %d, %d, %d, %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xfcc,
    //     m_idPledge,
    //     m_dbidUser,
    //     m_nRewardStep,
    //     m_nMinPoint)
}

CETC_MANI_USE_COUNT::CETC_MANI_USE_COUNT() : m_sidUser(0), m_dbidUser(0), m_itemId(0), m_useCount(0), m_maxUseCount(0) {
    // Current L2Server.exe 0x00715978.
}

CETC_MANI_USE_COUNT::CETC_MANI_USE_COUNT(
    const unsigned int userSid, const int userDbId, const unsigned int itemId, const short useCount, const short maxUseCount
) :
    m_sidUser(userSid), m_dbidUser(userDbId), m_itemId(itemId), m_useCount(useCount), m_maxUseCount(maxUseCount) {
    // Current L2Server.exe 0x007159a8.
}

int CETC_MANI_USE_COUNT::Assemble(char* const buffer, const int bufferSize) {
    // Current L2Server.exe 0x007149c0.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddhh", ETC_MANI_USE_COUNT, static_cast<int>(m_sidUser), m_dbidUser,
        static_cast<int>(m_itemId), static_cast<int>(m_useCount), static_cast<int>(m_maxUseCount)
    );
}

const unsigned char* CETC_MANI_USE_COUNT::Disassemble(const unsigned char* const packet) {
    // Current L2Server.exe 0x00714aa8.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddhh", &m_sidUser, &m_dbidUser, &m_itemId, &m_useCount, &m_maxUseCount);
}

bool CETC_MANI_USE_COUNT::Rollback() {
    // Current L2Server.exe 0x00714b78.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_USE_COUNT::DoAtCacheInternal() {
    // Current L2Server.exe 0x00714bf8.
    L2_GUARD_FUNCTION();
    return false;
}

bool CETC_MANI_USE_COUNT::DoAtServer() {
    // Current L2Server.exe 0x00719638.
    L2_GUARD_FUNCTION();

    CSPointer<User> user = g_UserMap.FindObject(m_sidUser);
    if (!user) {
        return true;
    }

    CItem* item;
    {
        L2_WRITE_GUARD(user->m_inv);
        item = user->m_inv.GetByDBID(static_cast<int>(m_itemId));
        if (item != nullptr) {
            item->m_data->SetUseCount(m_useCount);
            user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
        }
    }
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);

    if (item != nullptr) {
        CUserSocket* const socket = user->GetSocket();
        if (socket != nullptr) {
            if (m_useCount < m_maxUseCount) {
                socket->Send(
                    "chccdch", 0x62, 0x1414, 2, 3, item->GetClassId(), 0x15,
                    static_cast<int>(static_cast<std::int16_t>(m_maxUseCount - m_useCount))
                );
            } else {
                socket->Send("chccd", 0x62, 0x1415, 1, 3, item->GetClassId());
            }
        }
    }

    CLogFormatter formatter;
    formatter.WriteLog1(
        0x235d, user.get(), nullptr, nullptr, nullptr, nullptr, m_useCount, m_maxUseCount, 0, 0, 0, 0, m_itemId
    );
    return true;
}

void CETC_MANI_USE_COUNT::DumpError() {
    // Current L2Server.exe 0x00714c78. The source's extra file/line varargs
    // are kept even though the format string omits them.
    // Noise: this server specialization always returns true. Its dispatcher never calls this failure dump.
    // Log.AddT(
    //     LOG_ERROR,
    //     L"DumpError: %d, %d, %d, %c",
    //     "..\\Shared\\EtcMani.cpp",
    //     0x10a4,
    //     m_sidUser,
    //     m_dbidUser,
    //     m_itemId,
    //     m_useCount)
}

EtcManiUpdateUserItemCombination::EtcManiUpdateUserItemCombination() = default;

int EtcManiUpdateUserItemCombination::Assemble(char* const buffer, const int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddcddd", ETC_MANI_UPDATE_USER_ITEM_COMBINATION, static_cast<int>(m_nUserSid), m_nUserDBID,
        m_isSuccess ? 1 : 0, m_nItemID, m_nDel1ItemID, m_nDel2ItemID
    );
}

int EtcManiUpdateUserItemCombination::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddcddd", ETC_MANI_UPDATE_USER_ITEM_COMBINATION, static_cast<int>(m_nUserSid), m_nUserDBID, m_isSuccess ? 1 : 0,
        m_nItemID, m_nDel1ItemID, m_nDel2ItemID
    );
}

const unsigned char* EtcManiUpdateUserItemCombination::Disassemble(const unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddcddd", &m_nUserSid, &m_nUserDBID, &m_isSuccess, &m_nItemID, &m_nDel1ItemID, &m_nDel2ItemID
    );
}

bool EtcManiUpdateUserItemCombination::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiUpdateUserItemCombination::Rollback() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiUpdateUserItemCombination::DoAtServer() {
    // Current L2Server.exe 0x0071634c.
    L2_GUARD_FUNCTION();

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_nUserSid);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }

    user->SetPendingUseETCItem(nullptr, PUIT_ONE);
    user->SetPendingUseETCItem(nullptr, PUIT_TWO);
    if (!m_isSuccess) {
        user->GetSocket()->Send("chdd", 0xfe, 0x171, m_nItemID, m_nDel2ItemID);
        const FVector position = user->GetPos();
        const std::wstring accountName = Utf16ToWide(std::u16string_view(user->m_sAccountName.data()));
        // Log.Add(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,%s,%s,,,,",
        //     static_cast<std::int64_t>(0x1fbd),
        //     static_cast<std::int64_t>(user->GetDBID()),
        //     static_cast<std::int64_t>(user->GetAccountID()),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.z)),
        //     static_cast<std::int64_t>(user->m_data->m_Race),
        //     static_cast<std::int64_t>(user->m_data->m_Sex),
        //     static_cast<std::int64_t>(user->m_data->m_nClass),
        //     static_cast<std::int64_t>(user->m_data->m_nLevel),
        //     static_cast<std::int64_t>(m_nItemID),
        //     static_cast<std::int64_t>(m_nDel1ItemID),
        //     static_cast<std::int64_t>(m_nDel2ItemID),
        //     static_cast<std::int64_t>(0),
        //     user->GetName(),
        //     accountName.c_str())
        Log.Add(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,%s,%s,,,,",
            static_cast<std::int64_t>(0x1fbd), static_cast<std::int64_t>(user->GetDBID()),
            static_cast<std::int64_t>(user->GetAccountID()),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
            static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
            static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
            static_cast<std::int64_t>(m_nItemID), static_cast<std::int64_t>(m_nDel1ItemID),
            static_cast<std::int64_t>(m_nDel2ItemID), static_cast<std::int64_t>(0), user->GetName(), accountName.c_str()
        );
    } else {
        user->GetSocket()->Send("chd", 0xfe, 0x170, m_nItemID);
        const FVector position = user->GetPos();
        const std::wstring accountName = Utf16ToWide(std::u16string_view(user->m_sAccountName.data()));
        // Log.Add(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,%s,%s,,,,",
        //     static_cast<std::int64_t>(0x1fbd),
        //     static_cast<std::int64_t>(user->GetDBID()),
        //     static_cast<std::int64_t>(user->GetAccountID()),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.z)),
        //     static_cast<std::int64_t>(user->m_data->m_Race),
        //     static_cast<std::int64_t>(user->m_data->m_Sex),
        //     static_cast<std::int64_t>(user->m_data->m_nClass),
        //     static_cast<std::int64_t>(user->m_data->m_nLevel),
        //     static_cast<std::int64_t>(m_nItemID),
        //     static_cast<std::int64_t>(m_nDel1ItemID),
        //     static_cast<std::int64_t>(m_nDel2ItemID),
        //     static_cast<std::int64_t>(1),
        //     user->GetName(),
        //     accountName.c_str())
        Log.Add(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,%s,%s,,,,",
            static_cast<std::int64_t>(0x1fbd), static_cast<std::int64_t>(user->GetDBID()),
            static_cast<std::int64_t>(user->GetAccountID()),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
            static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
            static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
            static_cast<std::int64_t>(m_nItemID), static_cast<std::int64_t>(m_nDel1ItemID),
            static_cast<std::int64_t>(m_nDel2ItemID), static_cast<std::int64_t>(1), user->GetName(), accountName.c_str()
        );
        user->PledgeMissionCheckAndAddCount(static_cast<PledgeMission::GoalType>(0x26), m_nItemID, 1);
    }
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    user->PledgeMissionCheckAndAddCount(static_cast<PledgeMission::GoalType>(0x24), 0, 1);
    return true;
}

void EtcManiUpdateUserItemCombination::DumpError() { L2_GUARD_FUNCTION(); }

EtcManiRollbackUserItemBaseAttribute::EtcManiRollbackUserItemBaseAttribute() = default;

EtcManiRollbackUserItemBaseAttribute::EtcManiRollbackUserItemBaseAttribute(
    const unsigned int userSid, const int userDbId, const int targetItemDbId, const int removeAttribute
) :
    m_nUserSid(userSid),
    m_nUserDBID(userDbId),
    m_nTargetItemDBID(targetItemDbId),
    m_nResult(0),
    m_nRemoveAttribute(removeAttribute) {
    // Current L2Server 0x00b7863c.
}

int EtcManiRollbackUserItemBaseAttribute::Assemble(char* const buffer, const int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddd", ETC_MANI_ROLLBACK_USER_ITEM_BASE_ATTRIBUTE, static_cast<int>(m_nUserSid), m_nUserDBID,
        m_nTargetItemDBID, m_nRemoveAttribute, m_nResult
    );
}

int EtcManiRollbackUserItemBaseAttribute::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddddd", ETC_MANI_ROLLBACK_USER_ITEM_BASE_ATTRIBUTE, static_cast<int>(m_nUserSid), m_nUserDBID, m_nTargetItemDBID,
        m_nRemoveAttribute, m_nResult
    );
}

const unsigned char* EtcManiRollbackUserItemBaseAttribute::Disassemble(const unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddd", &m_nUserSid, &m_nUserDBID, &m_nTargetItemDBID, &m_nRemoveAttribute, &m_nResult);
}

bool EtcManiRollbackUserItemBaseAttribute::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiRollbackUserItemBaseAttribute::Rollback() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiRollbackUserItemBaseAttribute::DoAtServer() {
    // Current L2Server.exe 0x00717310. The target returns the low byte of
    // the final SendItemList result, which is always false in this path.
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_nUserSid);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }

    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    int itemServerId = 0;
    {
        L2_WRITE_GUARD(user->m_inv);
        CItem* const item = user->m_inv.GetByDBID(m_nTargetItemDBID);
        if (item != nullptr) {
            if (m_nResult == 1) {
                char16_t oldAttribute[256]{};
                char16_t newAttribute[256]{};
                char16_t ensoul[256]{};
                char16_t option[256]{};
                item->m_itemBaseAttribute.GetLogString(oldAttribute);
                {
                    L2_GUARD(*item->m_lock);
                    item->m_itemBaseAttribute.Remove(static_cast<AttributeTypeEnum>(m_nRemoveAttribute));
                }
                FormatUtf16To(
                    option, std::size(option), u"%d:%d:%d", item->m_itemOption.intensiveItemClassID,
                    item->m_itemOption.itemOptionKey.variationOption1, item->m_itemOption.itemOptionKey.variationOption2
                );
                item->m_itemBaseAttribute.GetLogString(newAttribute);
                item->MakeEnsoulLogInfo(ensoul);
                user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                itemServerId = static_cast<int>(item->Id());

                const int itemClassId = item->m_data->m_nClassID;
                const int enchanted = item->m_data->GetEnchanted();
                CUserSocket* const socket = user->GetSocket();
                if (!item->IsArmor()) {
                    if (enchanted < 1) {
                        socket->Send(
                            "chccdcc", 0x62, 0x880, 2, 3, itemClassId, 9,
                            static_cast<int>(static_cast<std::int8_t>(m_nRemoveAttribute))
                        );
                    } else {
                        socket->Send(
                            "chccccdcc", 0x62, 0x881, 3, 0x13, static_cast<int>(static_cast<std::int8_t>(enchanted)), 3,
                            itemClassId, 9, static_cast<int>(static_cast<std::int8_t>(m_nRemoveAttribute))
                        );
                    }
                } else if (enchanted < 1) {
                    socket->Send(
                        "chccdcccc", 0x62, 0xc50, 3, 3, itemClassId, 9,
                        static_cast<int>(static_cast<std::int8_t>(CItem::GetOppositeAttribute(m_nRemoveAttribute))), 9,
                        static_cast<int>(static_cast<std::int8_t>(m_nRemoveAttribute))
                    );
                } else {
                    socket->Send(
                        "chccccdcccc", 0x62, 0xc58, 4, 0x13, static_cast<int>(static_cast<std::int8_t>(enchanted)), 3,
                        itemClassId, 9,
                        static_cast<int>(static_cast<std::int8_t>(CItem::GetOppositeAttribute(m_nRemoveAttribute))), 9,
                        static_cast<int>(static_cast<std::int8_t>(m_nRemoveAttribute))
                    );
                }

                const FVector position = user->GetPos();
                const std::wstring accountName = Utf16ToWide(std::u16string_view(user->m_sAccountName.data()));
                // Log.Add(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,%s,%s,%I64d,,%I64d,,,,,,,,%s,%s,,,%I64d",
                //     static_cast<std::int64_t>(0x3d8),
                //     static_cast<std::int64_t>(user->GetDBID()),
                //     static_cast<std::int64_t>(user->GetAccountID()),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.x)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.y)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.z)),
                //     oldAttribute,
                //     newAttribute,
                //     option,
                //     static_cast<std::int64_t>(itemClassId),
                //     static_cast<std::int64_t>(enchanted),
                //     user->GetName(),
                //     accountName.c_str(),
                //     static_cast<std::int64_t>(item->GetDBID()))
                Log.Add(
                    LOG_BOTH, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,%s,%s,%I64d,,%I64d,,,,,,,,%s,%s,,,%I64d",
                    static_cast<std::int64_t>(0x3d8), static_cast<std::int64_t>(user->GetDBID()),
                    static_cast<std::int64_t>(user->GetAccountID()),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)), oldAttribute, newAttribute,
                    option, static_cast<std::int64_t>(itemClassId), static_cast<std::int64_t>(enchanted), user->GetName(),
                    accountName.c_str(), static_cast<std::int64_t>(item->GetDBID())
                );

                CLogFormatter formatter;
                formatter.WriteLog1(
                    0x2348, user, nullptr, oldAttribute, newAttribute, option, 0, 0, 0, 0, 0, 0, item->GetDBID()
                );
            } else {
                user->GetSocket()->SendSystemMessage(0x882);
            }
        }
    }
    user->ValidateChanged(VB_MAX);
    user->GetSocket()->Send("chddd", 0xfe, 0x76, m_nResult, itemServerId, m_nRemoveAttribute);
    user->m_nParamChanged |= PCB_USER;
    user->SendItemList(true, true, nullptr);
    return false;
}

void EtcManiRollbackUserItemBaseAttribute::DumpError() { L2_GUARD_FUNCTION(); }

EtcManiGiveItemByCastleSiegeCount::EtcManiGiveItemByCastleSiegeCount() = default;

int EtcManiGiveItemByCastleSiegeCount::Assemble(char* const buffer, const int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddd", ETC_MANI_GIVE_ITEM_BY_CASTLE_SIEGE_COUNT, static_cast<int>(m_UserSid), m_UserDBID,
        m_PledgeDBID, m_CastleSiegeCount, m_NpcId
    );
}

int EtcManiGiveItemByCastleSiegeCount::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddddd", ETC_MANI_GIVE_ITEM_BY_CASTLE_SIEGE_COUNT, static_cast<int>(m_UserSid), m_UserDBID, m_PledgeDBID,
        m_CastleSiegeCount, m_NpcId
    );
}

const unsigned char* EtcManiGiveItemByCastleSiegeCount::Disassemble(const unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddd", &m_UserSid, &m_UserDBID, &m_PledgeDBID, &m_CastleSiegeCount, &m_NpcId);
}

bool EtcManiGiveItemByCastleSiegeCount::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiGiveItemByCastleSiegeCount::Rollback() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiGiveItemByCastleSiegeCount::DoAtServer() {
    // Current L2Server.exe 0x00717b30.
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_UserSid);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    if (user == nullptr || pledge == nullptr || !user->IsPledgeMaster() ||
        static_cast<int>(pledge->GetDbId()) != m_PledgeDBID) {
        return false;
    }

    const int currentCount = pledge->GetCastleSiegeDefenceCount();
    if (m_CastleSiegeCount <= currentCount) {
        const bool result = pledge->SetCastleSiegeDefenceCount(static_cast<unsigned int>(m_CastleSiegeCount));
        g_npcserver.Send("cddddd", 0x6f, m_PledgeDBID, 0, m_NpcId, static_cast<int>(m_UserSid), result ? 1 : 0);
        return result;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"TRY to decrease siege defence count more than have. [%d][%d][%d]",
    //     m_PledgeDBID,
    //     currentCount,
    //     m_CastleSiegeCount)
    // Log.Add(
    //     LOG_WARN,
    //     L"[clan] Siege-defence count update rejected for clan %d: current count %d, requested count %d exceeds it.",
    //     m_PledgeDBID,
    //     currentCount,
    //     m_CastleSiegeCount)
    L2_LOG_WARN(
        Log, u"[clan] Siege-defence count update rejected for clan %d: current count %d, requested count %d exceeds it.",
        m_PledgeDBID, currentCount, m_CastleSiegeCount
    );
    return false;
}

void EtcManiGiveItemByCastleSiegeCount::DumpError() { L2_GUARD_FUNCTION(); }

EtcManiEnsoul::EtcManiEnsoul() = default;

EtcManiEnsoul::EtcManiEnsoul(
    const int targetItemDbId, const int userDbId, const int userSid, std::map<int, int> ensoulInfo[3]
) :
    m_nUserSid(userSid), m_nUserDBID(userDbId), m_nTargetItemDBID(targetItemDbId), m_bIsExtractionJob(false) {
    // Current L2Server.exe 0x0071a530.
    L2_GUARD_FUNCTION();
    for (int slotType = 1; slotType < 3; ++slotType) {
        for (const auto& option : ensoulInfo[slotType]) {
            m_mEnsoulInfo[slotType].emplace(option.first, option.second);
        }
    }
}

EtcManiEnsoul::EtcManiEnsoul(
    const int targetItemDbId, const int userDbId, const int userSid, std::map<int, int> ensoulInfo[3], const bool extraction
) :
    m_nUserSid(userSid), m_nUserDBID(userDbId), m_nTargetItemDBID(targetItemDbId), m_bIsExtractionJob(extraction) {
    // Current L2Server.exe 0x0071a688.
    L2_GUARD_FUNCTION();
    for (int slotType = 1; slotType < 3; ++slotType) {
        for (const auto& option : ensoulInfo[slotType]) {
            m_mEnsoulInfo[slotType].emplace(option.first, option.second);
        }
    }
}

int EtcManiEnsoul::Assemble(char* const buffer, const int bufferSize) {
    // Current L2Server.exe 0x00718c84 reuses the original
    // buffer and capacity for every free Assemble call.
    L2_GUARD_FUNCTION();
    int optionCount = 0;
    for (int slotType = 1; slotType < 3; ++slotType) {
        optionCount += static_cast<int>(m_mEnsoulInfo[slotType].size());
    }

    int assembled =
        ::Assemble(buffer, bufferSize, "hdddd", ETC_MANI_ENSOUL, m_nUserSid, m_nUserDBID, m_nTargetItemDBID, optionCount);
    assembled += ::Assemble(buffer, bufferSize, "c", m_bIsExtractionJob ? 1 : 0);
    for (int slotType = 1; slotType < 3; ++slotType) {
        for (const auto& option : m_mEnsoulInfo[slotType]) {
            assembled += ::Assemble(buffer, bufferSize, "ddd", slotType, option.first, option.second);
        }
    }
    return assembled;
}

int EtcManiEnsoul::Assemble(IPacketHelper& packet) {
    // Current L2Server.exe 0x00718e20.
    L2_GUARD_FUNCTION();
    int optionCount = 0;
    for (int slotType = 1; slotType < 3; ++slotType) {
        optionCount += static_cast<int>(m_mEnsoulInfo[slotType].size());
    }

    int assembled = packet.Assemble("hdddd", ETC_MANI_ENSOUL, m_nUserSid, m_nUserDBID, m_nTargetItemDBID, optionCount);
    assembled += packet.Assemble("c", m_bIsExtractionJob ? 1 : 0);
    for (int slotType = 1; slotType < 3; ++slotType) {
        for (const auto& option : m_mEnsoulInfo[slotType]) {
            assembled += packet.Assemble("ddd", slotType, option.first, option.second);
        }
    }
    return assembled;
}

const unsigned char* EtcManiEnsoul::Disassemble(const unsigned char* packet) {
    // Current L2Server.exe 0x00719184.
    L2_GUARD_FUNCTION();
    int optionCount = 0;
    char extractionJob = 0;
    packet = ::Disassemble(packet, "dddd", &m_nUserSid, &m_nUserDBID, &m_nTargetItemDBID, &optionCount);
    packet = ::Disassemble(packet, "c", &extractionJob);
    m_bIsExtractionJob = extractionJob == 1;
    for (int index = 0; index < optionCount; ++index) {
        int slotType = -1;
        int slotIndex = -1;
        int optionId = -1;
        packet = ::Disassemble(packet, "ddd", &slotType, &slotIndex, &optionId);
        m_mEnsoulInfo[slotType].emplace(slotIndex, optionId);
    }
    return packet;
}

bool EtcManiEnsoul::Rollback() {
    // Current L2Server.exe 0x00712cc4.
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiEnsoul::DoAtCacheInternal() {
    // Current L2Server.exe 0x00712dc0.
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiEnsoul::DoAtServer() {
    // Current L2Server.exe 0x00719ec4.
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    CPacketHelperEx<20480> packet;
    if (user == nullptr) {
        return false;
    }

    {
        L2_WRITE_GUARD(user->m_inv);
        CItem* const item = user->m_inv.GetByDBID(m_nTargetItemDBID);
        if (item != nullptr) {
            std::map<int, const CSkillInfo*> skills;
            item->GetEnsoulSkills(skills);
            for (const auto& skill : skills) {
                user->DeleteAcquireSkill(skill.second->m_nSkillId);
            }

            std::map<int, EnsoulOptionData*> options[3];
            for (int slotType = 1; slotType < 3; ++slotType) {
                for (const auto& option : m_mEnsoulInfo[slotType]) {
                    const auto found = g_mEnsoulOptionData.find(option.second);
                    if (found != g_mEnsoulOptionData.end()) {
                        options[slotType].emplace(option.first, found->second);
                    }
                }
            }
            item->SetEnsoulOptionData(options, false);
            item->AssembleEnsoul(packet);
            user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
        }
    }

    user->ValidateChanged(VB_MAX);
    user->m_nParamChanged |= std::int64_t{PCB_USER | PCB_CHAR_SKILL};
    CUserSocket* const socket = user->GetSocket();
    if (socket != nullptr) {
        socket->Send("chdb", 0xfe, m_bIsExtractionJob ? 0x1b5 : 0x181, 1, packet.GetLength(), packet.GetPacket());
    }
    user->SendItemList(true, true, nullptr);
    g_npcserver.Send("cdddd", 0xc6, static_cast<int>(user->m_data->m_nDBID), 0, 3, 0);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    return false;
}

void EtcManiEnsoul::DumpError() {
    // Current L2Server.exe 0x00712d44.
    L2_GUARD_FUNCTION();
}

EtcManiMultisellResult::EtcManiMultisellResult() = default;

int EtcManiMultisellResult::Assemble(char* const buffer, const int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddddc", ETC_MANI_MULTISELL, m_nUserSID, m_nUserDBID, m_ePointType, m_nPoint, m_nGroupId,
        m_bSuccess ? 1 : 0
    );
}

int EtcManiMultisellResult::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddddc", ETC_MANI_MULTISELL, m_nUserSID, m_nUserDBID, m_ePointType, m_nPoint, m_nGroupId, m_bSuccess ? 1 : 0
    );
}

const unsigned char* EtcManiMultisellResult::Disassemble(const unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddddc", &m_nUserSID, &m_nUserDBID, &m_ePointType, &m_nPoint, &m_nGroupId, &m_bSuccess);
}

bool EtcManiMultisellResult::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiMultisellResult::Rollback() {
    L2_GUARD_FUNCTION();
    return true;
}

bool EtcManiMultisellResult::DoAtServer() {
    // Current L2Server.exe 0x0071b648.
    L2_GUARD_FUNCTION();
    CCreature* creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSID));
    User* user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return true;
    }

    user->SendItemList(true, true, nullptr);
    int pointType = 0;
    switch (m_ePointType) {
        case 1:
            pointType = -100;
            break;
        case 2:
            pointType = -200;
            break;
        case 5:
            pointType = -300;
            break;
        case 8:
            pointType = -500;
            break;
        default:
            break;
    }
    user->GetSocket()->Send("chcdd", 0xfe, 0x182, m_bSuccess ? 1 : 0, pointType, m_nPoint);
    user->SendSystemMessage(m_bSuccess ? 0x1106 : 0x161);

    creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSID));
    user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr && user->IsAlive()) {
        CMultiSellList* const list = g_MultiSellDB.find(m_nGroupId);
        if (list == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]invalid group id[%d]",
            //     L"..\\Shared\\EtcMani.cpp",
            //     0xab0,
            //     m_nGroupId)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[multisell] Cannot refresh list %d after sending the result reply: list was not found.",
            //     m_nGroupId)
            L2_LOG_ERROR(
                Log, u"[multisell] Cannot refresh list %d after sending the result reply: list was not found.", m_nGroupId
            );
        } else {
            CNPC* npc = nullptr;
            if (!list->IsWithoutNPC()) {
                const std::uint32_t npcSid =
                    TalkChannelCache::inst().mChannels[1].Get(static_cast<std::uint32_t>(m_nUserSID));
                CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(npcSid);
                if (npcCreature != nullptr && npcCreature->IsNPC()) {
                    npc = npcCreature->CastNPC();
                }
                if (npc == nullptr) {
                    return true;
                }
            }
            list->SendList(user, npc, true);
        }
    }
    return true;
}

void EtcManiMultisellResult::DumpError() { L2_GUARD_FUNCTION(); }

EtcManiEnchantArtifact::EtcManiEnchantArtifact() :
    m_nUserSID(0), m_nUserDBID(0), m_bEnchantSuccess(false), m_nTargetItemDBID(0), m_nTargetNewEnchant(0) {
    // Current L2Server.exe 0x00715a10.
    m_nMaterialItemDBID.fill(0);
    // The current constructor leaves m_nTargetOrgEnchant
    // untouched. That lane is cache-side rollback state, not wire state.
}

int EtcManiEnchantArtifact::Assemble(IPacketHelper& packet) {
    // Current L2Server.exe 0x00714dc4.
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddcddddd", ETC_MANI_ENCHANT_ARTIFACT, m_nUserSID, m_nUserDBID, static_cast<int>(m_bEnchantSuccess),
        m_nTargetItemDBID, m_nTargetNewEnchant, m_nMaterialItemDBID[0], m_nMaterialItemDBID[1], m_nMaterialItemDBID[2]
    );
}

int EtcManiEnchantArtifact::Assemble(char* const buffer, const int bufferSize) {
    // Current L2Server.exe 0x00714cc8 keeps the eleven-field literal while
    // supplying only the nine values below. Preserve that shipping vararg
    // mismatch instead of normalizing the grammar.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddcddddddd", ETC_MANI_ENCHANT_ARTIFACT, m_nUserSID, m_nUserDBID,
        static_cast<int>(m_bEnchantSuccess), m_nTargetItemDBID, m_nTargetNewEnchant, m_nMaterialItemDBID[0],
        m_nMaterialItemDBID[1], m_nMaterialItemDBID[2]
    );
}

const unsigned char* EtcManiEnchantArtifact::Disassemble(const unsigned char* const packet) {
    // Current L2Server.exe 0x00714eb0.
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddcddddd", &m_nUserSID, &m_nUserDBID, reinterpret_cast<char*>(&m_bEnchantSuccess), &m_nTargetItemDBID,
        &m_nTargetNewEnchant, &m_nMaterialItemDBID[0], &m_nMaterialItemDBID[1], &m_nMaterialItemDBID[2]
    );
}

bool EtcManiEnchantArtifact::Rollback() {
    // Current L2Server.exe 0x00714f9c.
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiEnchantArtifact::DoAtCacheInternal() {
    // Current L2Server.exe 0x00715108.
    L2_GUARD_FUNCTION();
    return false;
}

bool EtcManiEnchantArtifact::DoAtServer() {
    // Current L2Server.exe 0x007184c4.
    L2_GUARD_FUNCTION();

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSID));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }

    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);

    {
        L2_WRITE_GUARD(user->m_inv);

        CItem* const target = user->m_inv.GetByDBID(m_nTargetItemDBID);
        if (target == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[ArtifactSystem] Can't find Enchant Target Item. "
            //     L"User[%s] TargetItemDBID[%d]",
            //     user->GetName(),
            //     m_nTargetItemDBID)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[artifact] Cannot apply enchantment result for %ls: target item DB ID %d was not found. Pending-job
            //     flags have already been cleared.", user->GetName(), m_nTargetItemDBID)
            // L2_LOG_ERROR(Log,
            //     L"[artifact] Cannot apply enchantment result for %ls: target item DB ID %d was not found. Pending-job
            //     flags have already been cleared.", user->GetName(), m_nTargetItemDBID)
            L2_LOG_ERROR(
                Log,
                u"[artifact] Cannot apply enchantment result for %ls: target item DB ID %d was not found; pending-job flags "
                u"have already been cleared.",
                user->GetName(), m_nTargetItemDBID
            );
            return false;
        }

        std::array<int, 3> materialClassIds{};
        std::array<int, 3> materialEnchants{};
        for (std::size_t index = 0; index < m_nMaterialItemDBID.size(); ++index) {
            CItem* const material = user->m_inv.GetByDBID(m_nMaterialItemDBID[index]);
            if (material != nullptr) {
                materialClassIds[index] = material->m_data->m_nClassID;
                materialEnchants[index] = material->m_data->GetEnchanted();
            }
        }

        char16_t materialLog[256]{};
        FormatUtf16To(
            materialLog, std::size(materialLog), u"%d:%d:%d:%d:%d:%d:%d:%d:%d", m_nMaterialItemDBID[0], materialClassIds[0],
            materialEnchants[0], m_nMaterialItemDBID[1], materialClassIds[1], materialEnchants[1], m_nMaterialItemDBID[2],
            materialClassIds[2], materialEnchants[2]
        );

        char16_t shapeShiftLog[256]{};
        char16_t ensoulLog[256]{};
        target->MakeShapeShiftLogInfo(shapeShiftLog);
        target->MakeEnsoulLogInfo(ensoulLog);

        const int targetClassId = target->m_data->m_nClassID;
        const int targetOldEnchant = target->m_data->GetEnchanted();
        const ItemOption& option = target->m_itemOption;
        CLogFormatter formatter;

        if (!m_bEnchantSuccess) {
            formatter.WriteLog4(
                0x235f, user, nullptr, materialLog, shapeShiftLog, ensoulLog, static_cast<int>(user->m_data->m_Sex),
                user->GetClass(), targetOldEnchant, targetClassId, option.intensiveItemClassID,
                option.itemOptionKey.variationOption1, option.itemOptionKey.variationOption2, 0, 0, 0, m_nTargetItemDBID
            );
            user->GetSocket()->Send("chddddd", 0xfe, 0x201, 1, 0, 0, 0, 0);
        } else {
            formatter.WriteLog4(
                0x235e, user, nullptr, materialLog, shapeShiftLog, ensoulLog, user->GetClass(), targetOldEnchant,
                m_nTargetNewEnchant, targetClassId, option.intensiveItemClassID, option.itemOptionKey.variationOption1,
                option.itemOptionKey.variationOption2, 0, 0, 0, m_nTargetItemDBID
            );

            {
                L2_GUARD(*target->m_lock);
                target->SetEnchanted(m_nTargetNewEnchant);
            }
            user->m_inv.SetInventoryChanged(target, IUT_UPDATE);

            std::array<int, 3> optionIds{};
            if (target->m_pEnchantOption != nullptr) {
                const EnchantOptionId& enchantOption = target->m_pEnchantOption->GetOptionId();
                optionIds[0] = enchantOption.at[0];
                optionIds[1] = enchantOption.at[1];
                optionIds[2] = enchantOption.at[2];
            }
            user->GetSocket()->Send(
                "chddddd", 0xfe, 0x201, 0, m_nTargetNewEnchant, optionIds[0], optionIds[1], optionIds[2]
            );
        }

        ItemEnchantAnnouncer announcer(&CUserBroadcaster::inst());
        if (announcer.NeedToAnnounce(*target)) {
            announcer.Announce(user->GetName(), *target);
        }
    }
    return true;
}

void EtcManiEnchantArtifact::DumpError() {
    // Current L2Server.exe 0x0071501c.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"EtcManiEnchantArtifact::DumpError() => "
    //     L"UserSID[%d], UserDBID[%d], isSuccess[%d]"
    //     L"TargetItemDBID[%d], TargetItemNewEnchant[%d], "
    //     L"MaterialDBID1[%d], MaterialDBID2[%d], "
    //     L"MaterialDBID3[%d]",
    //     m_nUserSID,
    //     m_nUserDBID,
    //     m_bEnchantSuccess ? 1 : 0,
    //     m_nTargetItemDBID,
    //     m_nTargetNewEnchant,
    //     m_nMaterialItemDBID[0],
    //     m_nMaterialItemDBID[1],
    //     m_nMaterialItemDBID[2])
    // Log.Add(
    //     LOG_ERROR,
    //     L"[artifact] Enchantment result context: character SID %d, character DB ID %d, success flag %d, target item DB ID
    //     %d, new enchant %d, material DB IDs %d, %d and %d.", m_nUserSID, m_nUserDBID, m_bEnchantSuccess ? 1 : 0,
    //     m_nTargetItemDBID,
    //     m_nTargetNewEnchant,
    //     m_nMaterialItemDBID[0],
    //     m_nMaterialItemDBID[1],
    //     m_nMaterialItemDBID[2])
    L2_LOG_ERROR(
        Log,
        u"[artifact] Enchantment result context: character SID %d, character DB ID %d, success flag %d, target item DB ID "
        u"%d, new enchant %d, material DB IDs %d, %d and %d.",
        m_nUserSID, m_nUserDBID, m_bEnchantSuccess ? 1 : 0, m_nTargetItemDBID, m_nTargetNewEnchant, m_nMaterialItemDBID[0],
        m_nMaterialItemDBID[1], m_nMaterialItemDBID[2]
    );
}

void EtcManiEnchantArtifact::Set(
    const int userSid,
    const int userDbId,
    const bool enchantSuccess,
    const int targetItemDbId,
    const int targetNewEnchant,
    const int materialItemDbId1,
    const int materialItemDbId2,
    const int materialItemDbId3
) {
    m_nUserSID = userSid;
    m_nUserDBID = userDbId;
    m_bEnchantSuccess = enchantSuccess;
    m_nTargetItemDBID = targetItemDbId;
    m_nTargetNewEnchant = targetNewEnchant;
    m_nMaterialItemDBID[0] = materialItemDbId1;
    m_nMaterialItemDBID[1] = materialItemDbId2;
    m_nMaterialItemDBID[2] = materialItemDbId3;
}

EtcMani* Create_EtcManiUpdatePledgeNameValue() { return new EtcManiUpdatePledgeNameValue(); }

EtcMani* Create_EtcManiUpdateUserEventTime() { return new EtcManiUpdateUserEventTime(); }

EtcMani* Create_EtcManiUpdateUserItemBaseAttribute() { return new EtcManiUpdateUserItemBaseAttribute(); }

EtcMani* Create_EtcManiRollbackUserItemBaseAttribute() { return new EtcManiRollbackUserItemBaseAttribute(); }

EtcMani* Create_EtcManiCreateItemAuctionBid() { return new EtcManiCreateItemAuctionBid(); }

EtcMani* Create_EtcManiWithdrawItemAuctionBid() { return new EtcManiWithdrawItemAuctionBid(); }

EtcMani* Create_EtcManiGiveItemByCastleSiegeCount() { return new EtcManiGiveItemByCastleSiegeCount(); }

EtcMani* Create_EtcManiGiveItemOnAccount() { return new EtcManiGiveItemOnAccount(); }

EtcMani* Create_EtcManiUpdateUserItemCombination() { return new EtcManiUpdateUserItemCombination(); }

EtcMani* Create_EtcManiEnsoul() { return new EtcManiEnsoul(); }

EtcMani* Create_EtcManiMultisellResult() { return new EtcManiMultisellResult(); }

EtcMani* Create_EtcManiSuccessfulItemAuctionBid() { return new EtcManiSuccessfulItemAuctionBid(); }

EtcMani* Create_EtcManiCardUpDownGame() { return new EtcManiCardUpDownGame(); }

EtcMani* Create_EtcManiRemoveInzoneBindingByEvent() { return new EtcManiRemoveInzoneBindingByEvent(); }

EtcMani* Create_CETC_MANI_PLEDGE_MASTERY_SET() { return new CETC_MANI_PLEDGE_MASTERY_SET(); }

EtcMani* Create_CETC_MANI_PLEDGE_MASTERY_RESET() { return new CETC_MANI_PLEDGE_MASTERY_RESET(); }

EtcMani* Create_CETC_MANI_POINT_UPDATE() { return new CETC_MANI_POINT_UPDATE(); }

EtcMani* Create_CETC_MANI_PLEDGE_MISSION_REWARD() { return new CETC_MANI_PLEDGE_MISSION_REWARD(); }

EtcMani* Create_CETC_MANI_PLEDGE_CONTRIBUTION_REWARD() { return new CETC_MANI_PLEDGE_CONTRIBUTION_REWARD(); }

EtcMani* Create_CETC_MANI_USE_COUNT() { return new CETC_MANI_USE_COUNT(); }

EtcMani* Create_EtcManiEnchantArtifact() { return new EtcManiEnchantArtifact(); }

namespace {

#define L2_REGISTER_ETC_MANI(TypeValue, CreatorName)                                                                        \
    const bool isReg_##CreatorName = Singleton<EtcManiFactory>::inst().Register(TypeValue, &CreatorName)

L2_REGISTER_ETC_MANI(ETC_MANI_UPDATE_PLEDGE_NAMEVALUE, Create_EtcManiUpdatePledgeNameValue);
L2_REGISTER_ETC_MANI(ETC_MANI_UPDATE_USER_EVENT_TIME, Create_EtcManiUpdateUserEventTime);
L2_REGISTER_ETC_MANI(ETC_MANI_UPDATE_USER_ITEM_BASE_ATTRIBUTE, Create_EtcManiUpdateUserItemBaseAttribute);
L2_REGISTER_ETC_MANI(ETC_MANI_ROLLBACK_USER_ITEM_BASE_ATTRIBUTE, Create_EtcManiRollbackUserItemBaseAttribute);
L2_REGISTER_ETC_MANI(ETC_MANI_CREATE_ITEM_AUCTION_BID, Create_EtcManiCreateItemAuctionBid);
L2_REGISTER_ETC_MANI(ETC_MANI_WITHDRAW_ITEM_AUCTION_BID, Create_EtcManiWithdrawItemAuctionBid);
L2_REGISTER_ETC_MANI(ETC_MANI_GIVE_ITEM_BY_CASTLE_SIEGE_COUNT, Create_EtcManiGiveItemByCastleSiegeCount);
L2_REGISTER_ETC_MANI(ETC_MANI_GIVE_ITEM_ON_ACCOUNT, Create_EtcManiGiveItemOnAccount);
L2_REGISTER_ETC_MANI(ETC_MANI_UPDATE_USER_ITEM_COMBINATION, Create_EtcManiUpdateUserItemCombination);
L2_REGISTER_ETC_MANI(ETC_MANI_ENSOUL, Create_EtcManiEnsoul);
L2_REGISTER_ETC_MANI(ETC_MANI_MULTISELL, Create_EtcManiMultisellResult);
L2_REGISTER_ETC_MANI(ETC_MANI_SUCCESSFUL_ITEM_AUCTION_BID, Create_EtcManiSuccessfulItemAuctionBid);
L2_REGISTER_ETC_MANI(ETC_MANI_CARD_UPDOWN_GAME, Create_EtcManiCardUpDownGame);
L2_REGISTER_ETC_MANI(ETC_MANI_REMOVE_INZONE_BINDING_BY_EVENT, Create_EtcManiRemoveInzoneBindingByEvent);
L2_REGISTER_ETC_MANI(ETC_MANI_PLEDGE_MASTERY_SET, Create_CETC_MANI_PLEDGE_MASTERY_SET);
L2_REGISTER_ETC_MANI(ETC_MANI_PLEDGE_MASTERY_RESET, Create_CETC_MANI_PLEDGE_MASTERY_RESET);
L2_REGISTER_ETC_MANI(ETC_MANI_POINT_UPDATE, Create_CETC_MANI_POINT_UPDATE);
L2_REGISTER_ETC_MANI(ETC_MANI_PLEDGE_MISSION_REWARD, Create_CETC_MANI_PLEDGE_MISSION_REWARD);
L2_REGISTER_ETC_MANI(ETC_MANI_PLEDGE_CONTRIBUTION_REWARD, Create_CETC_MANI_PLEDGE_CONTRIBUTION_REWARD);
L2_REGISTER_ETC_MANI(ETC_MANI_USE_COUNT, Create_CETC_MANI_USE_COUNT);
L2_REGISTER_ETC_MANI(ETC_MANI_ENCHANT_ARTIFACT, Create_EtcManiEnchantArtifact);

#undef L2_REGISTER_ETC_MANI

} // namespace

namespace EtcManiUtil {

void AssembleEtcManis(const std::vector<EtcMani*>& etcManis, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    // The current L2Server body has no defensive null branch.
    for (EtcMani* const etcMani : etcManis) {
        etcMani->Assemble(packet);
    }
}

void Destroy(std::vector<EtcMani*>& etcManis) {
    for (EtcMani* const etcMani : etcManis) {
        etcMani->Destroy();
    }
    etcManis.clear();
}

} // namespace EtcManiUtil

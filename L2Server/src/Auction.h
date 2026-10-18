#pragma once

#include "Object.h"

#include "Shared/src/IOC.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

class CAuction;
class CAuctionHtmlCommandHandler;
class CParserForAuction;
class CDBSocket;
class CNPC;
class CSocket;
class CUserSocket;
class User;
class EtcManiCreateItemAuctionBid;
class EtcManiSuccessfulItemAuctionBid;
class EtcManiWithdrawItemAuctionBid;

namespace UserPacketEx {

bool RequestItemAuctionStatus(CSocket* socket, const std::uint8_t* packet);

} // namespace UserPacketEx

namespace DBPacketExHandler {

bool ReplyCreateItemAuction(CDBSocket* socket, const unsigned char* packet);

} // namespace DBPacketExHandler

// The spelling is donor-selected source vocabulary. Current auction state
// transitions and the builder command close 0..2.
enum AuctionStatus : std::int32_t {
    AS_NONE = 0,
    AS_DOING = 1,
    AS_DONE = 2,
};


enum AuctionExtendStatus : std::int32_t {
    AES_NONE = 0,
    AES_FIRST_EXTEND = 1,
    AES_SECOND_EXTEND = 2,
};


// Exact current L2Server value layouts from Auction.cpp. The current image
// fixes every offset and copy width. TotalChecker supplies only the surviving
// member spellings and is not used as layout evidence.
struct AuctionItem {
    std::int32_t m_nOwnerDBID;
    std::int32_t m_nItemIndex;
    std::int32_t m_nClassID;
    std::byte padding_0c[4];
    std::int64_t m_nAmount;
};

struct AuctionDate {
    std::int32_t m_nStartHour;
    std::int32_t m_nStartMin;
    std::int32_t m_nDuration;
    std::int32_t m_nWeekDay;
};

struct NPCAuctionItem {
    std::int32_t m_nItemType;
    std::byte padding_04[4];
    std::int64_t m_nItemAmount;
    std::int64_t m_nItemPrice;
};

// Current L2Server CAuction.obj concrete owner. The direct CObject prefix,
// 0xd0 extent and every member offset are closed by the current constructor,
// teardown, live bid paths and both allocation sites.
class CAuction final : public CObject {
public:
    CAuction(
        int auctionDatabaseId,
        int npcClassId,
        std::int32_t startTime,
        int duration,
        AuctionStatus status,
        AuctionExtendStatus extendStatus,
        const AuctionItem& item,
        std::int64_t initialPrice
    );
    ~CAuction() override;

    L2_DECLARE_OBJECT_REGISTRATION(CAuction)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    int GetIndex() const override;
    CObject* Copy() override;

    void OnCreate();
    virtual void Delete();
    virtual FVector GetPos() const;

    bool CreateBid(int userDatabaseId, std::int64_t biddingPrice, int transactionId);
    bool Bid(User* user, std::int64_t biddingPrice);
    bool DeleteBid(int userDatabaseId);
    bool FinishAuction();
    int GetAuctionID() const { return m_nAuctionDBID; }
    AuctionItem GetAuctionItem() const { return m_AuctionItem; }
    int GetAuctionRemainTime() const;
    std::int64_t GetBidFailureMoney(int userDatabaseId) const;
    wchar_t* GetStateName() const;
    void SendAuctionState(CUserSocket& socket) const;
    bool WithdrawBiddingPrice(User* user);
    bool ShowAuctionUI(User* user, bool fromNpc);
    bool ShowAuctionUIByNPC(CNPC* npc, User* user);
    bool ShowNextAuctionUI(User* user);
    bool ShowNextAuctionUIByNPC(CNPC* npc, User* user);
    void LoadBid();
    void InitBid(int bidCount, const unsigned char* packet);
    void InitBidDone();
    void SetAuctionItemInfo(const AuctionItem& item, int initialPrice);
    void SetAuctionDuration(int duration);
    void SetAuctionStatus(AuctionStatus status);


protected:
    void TimerExpired(int id) override;

private:
    friend class CAuctionManager;
    friend class CAuctionNPCDB;
    friend class EtcManiCreateItemAuctionBid;
    friend class EtcManiSuccessfulItemAuctionBid;
    friend class EtcManiWithdrawItemAuctionBid;
    friend bool UserPacketEx::RequestItemAuctionStatus(CSocket* socket, const std::uint8_t* packet);
    friend bool DBPacketExHandler::ReplyCreateItemAuction(CDBSocket* socket, const unsigned char* packet);

    void ExpandAuctionDuration();

    mutable CYieldLock m_Lock; // +0x40
    std::int32_t m_nAuctionDBID; // +0x50
    std::int32_t m_nNPCClassID; // +0x54
    std::int32_t m_tStartTime; // +0x58
    std::int32_t m_nDuration; // +0x5c
    AuctionStatus m_eStatus; // +0x60
    AuctionExtendStatus m_eExtendStatus; // +0x64
    AuctionItem m_AuctionItem; // +0x68
    std::int64_t m_nInitPrice; // +0x80
    std::map<int, std::int64_t> m_mapBidder; // +0x88
    std::int32_t m_nMaxBidderDBID; // +0xa8
    std::int32_t m_nPrevMaxBidderDBID; // +0xac
    std::int64_t m_nCurrentPrice; // +0xb0
    volatile std::int32_t m_nAuctionDBJobCount; // +0xb8
    std::uint32_t m_nLastBiddingTick; // +0xbc
    volatile std::int32_t m_nRealDone; // +0xc0
    bool m_bHalfHourBroadcasting; // +0xc4
    std::byte m_paddingC5ToC8[3];
    std::int32_t m_nAuctionTransactionID; // +0xc8
    std::int32_t m_nReturnedTransactionID; // +0xcc

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CAuction)
};

class CAuctionManager final : public Singleton<CAuctionManager> {
public:
    CAuctionManager();
    ~CAuctionManager() override;

    void Load();
    bool RegisterAuction(CAuction* auction);
    bool DeleteAuction(CAuction* auction);
    CAuction* GetAuction(int auctionDatabaseId);
    CAuction* GetLatestAuction(int npcClassId);
    CAuction* GetActiveAuction(int npcClassId);
    void GetAuctions(int npcClassId, std::vector<CAuction*>& auctions);
    void InitAllAuctions(int auctionCount, const unsigned char* packet);
    int BuilderCMD_SendAuctionManagerState(CUserSocket& socket);
    void BuilderCMD_ClearAllItemAuctions(int npcClassId);


private:
    bool IsValidAuction(CAuction* auction);
    bool PushAuctionMap(CAuction* auction);

    std::map<int, CAuction*> m_mapAuctionMap; // +0x08
    std::multimap<int, CAuction*> m_mmapAuctionSellerMap; // +0x28
    CYieldLock m_Lock; // +0x48
};

// Exact current 0xc8 Auction.cpp owner. The target uses the 0x20 xvector
// carrier in both lists. Portable code owns standard vectors directly.
// member-offset comments below describe the shipping layout.
class CAuctionNPC : public CObject {
public:
    CAuctionNPC();
    ~CAuctionNPC() override;

    L2_DECLARE_OBJECT_REGISTRATION(CAuctionNPC)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    int GetIndex() const override;
    CObject* Copy() override;

    // Current CAuctionNPC adds these two slots after CObject's complete
    // 153-slot surface, in this order.
    virtual void Delete();
    virtual FVector GetPos() const;

    void UpdateAuctionDate();
    NPCAuctionItem PickNextAuctionItem();
    bool CanMakeAuction();
    void CheckBroadcastingTime();
    void CreateNextItemAuction();
    void CreateInitialItemAuction();
    bool BuilderCMD_CreateItemAuction(int itemIndex);
    void SendAuctionNpcState(CUserSocket& socket);
    void SetShowNextAuctionByBuilder(const bool show) { m_bShowNextAuctionByBuilder = show; }
    bool CanShowNextItemAuction() {
        return GetNextItemAuctionShowTime() < runtime::crt::Time32Now() || m_bShowNextAuctionByBuilder;
    }
    std::int32_t GetNextItemAuctionShowTime() {
        const std::uint32_t bits =
            static_cast<std::uint32_t>(GetNextStartTime()) - static_cast<std::uint32_t>(m_tNextInfoOpenTime);
        std::int32_t result = 0;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }
    std::int32_t GetNextStartTime();
    bool NeedToUpdatePosition() const;
    void UpdatePosition();
    NPCAuctionItem GetAuctionItem(int itemIndex) const;

    std::int32_t m_nNPCClassID; // +0x40
    bool m_bDoing; // +0x44


protected:
    void TimerExpired(int id) override;

private:
    friend class CParserForAuction;
    friend class CAuctionNPCDB;
    friend bool DBPacketExHandler::ReplyCreateItemAuction(CDBSocket* socket, const unsigned char* packet);

    void _CreateItemAuction(std::int32_t startTime, int itemIndex);
    int GetNextAuctionItemIndex() const;

    std::vector<AuctionDate> m_AuctionDateList; // +0x48
    std::int32_t m_nNextAuctionDateIndex; // +0x68
    std::int32_t m_tNextStartTime; // +0x6c
    std::int32_t m_tNextInfoOpenTime; // +0x70
    std::byte m_alignment74To78[4];
    std::vector<NPCAuctionItem> m_AuctionItemList; // +0x78
    std::int32_t m_nCurrentAuctionItemIndex; // +0x98
    bool m_bAuctionCreating; // +0x9c
    bool m_bShowNextAuctionByBuilder; // +0x9d
    bool m_bBroadcastingNextItem; // +0x9e
    std::byte m_alignment9fToA0[1];
    FVector m_Position; // +0xa0
    CYieldLock m_Lock; // +0xb8

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CAuctionNPC)
};

class CAuctionNPCDB : public Singleton<CAuctionNPCDB> {
public:
    CAuctionNPCDB();
    ~CAuctionNPCDB() override;

    void Load();
    CAuctionNPC* GetAuctionNPC(int npcClassId) const;
    void InitAllAuctionNPC();
    void BuilderCMD_SendAuctionNpcDBState(CUserSocket& socket);

    std::map<int, CAuctionNPC*> m_mapAuctionNPC; // +0x08
};

class CAuctionHtmlCommandHandler {
public:
    static void Handle(CNPC* npc, User* user, const char16_t* command);
};


// Exact current verifier object used by CAuction::Delete. Current RTTI
// proves that CAuction's CIOObject base is at displacement zero. The
// implementation therefore needs no partial CAuction declaration.
class CAuctionReleaseVerifier final : public ::CIOObject {
public:
    explicit CAuctionReleaseVerifier(CAuction* auction);
    ~CAuctionReleaseVerifier() override = default;

    void TimerExpired(int id) override;


private:
    CAuction* m_pAuction;
};

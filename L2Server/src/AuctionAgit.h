#pragma once

#include "Agit.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

// The spelling is donor-selected source vocabulary. Current construction and
// setter call sites create every operational state 0..8.
enum SaleState : std::int32_t {
    SALE_NONE = 0,
    SALE_AUTOSELL = 1,
    SALE_REQSELL = 2,
    SALE_OWNED = 3,
    SALE_NOTOWNED = 4,
    SALE_STEP1 = 5,
    SALE_STEP2 = 6,
    SALE_STEP3 = 7,
    SALE_ACT = 8,
    SALE_MAX = 9,
};


// Current Win64 AuctionInfo carrier. `long` is fixed to its target 32-bit
// width so the xvector element stride remains 0x18 on LP64 hosts.
struct AuctionInfo {
    AuctionInfo(int pledgeId, std::int64_t deposit, std::int32_t registerTime) noexcept;
    AuctionInfo() noexcept;

    int m_nPledgeId; // +0x00
    std::byte m_alignment04To08[4]; // +0x04..+0x07
    std::int64_t m_nDeposit; // +0x08
    std::int32_t m_tRegister; // +0x10
    std::byte m_alignment14To18[4]; // +0x14..+0x17
};


// Current concrete auction-agit owner. Its 57-slot vtable differs from
// CWorldAgit only in Release/destruction, TimerExpired and the auction
// query/cast pair. Every remaining slot is inherited from the real base.
class CAuctionAgit final : public CWorldAgit {
public:
    CAuctionAgit();
    ~CAuctionAgit() override;

    L2_DECLARE_OBJECT_TYPE(CAuctionAgit)

    bool IsAuctionAgit() const override;
    CAuctionAgit* CastAuctionAgit() const override;
    void TimerExpired(int id) override;

    SaleState GetSaleState();
    void SetLastPrice(std::int64_t price) noexcept { m_nLastPrice = price; }
    std::int64_t GetLastPrice() const noexcept { return m_nLastPrice; }
    void SetBidDay(int day) noexcept { m_nBidDay = day; }
    int GetBidDay() const noexcept { return m_nBidDay; }
    void SetAuctionDesc(wchar_t* description);
    wchar_t* GetDesc();
    void ResetAuction();
    void SetAuctionData(int auctionId, std::int64_t minimumBidPrice, wchar_t* description, int auctionEnd);
    bool CheckSendable(int bit);
    bool UnCheckSendable(int bit);
    void SetSaleState(SaleState state);
    bool InCancelPenalty();
    void StartAuctionTimer(int auctionEnd);
    void SetAuctionBeginTime(std::int32_t time) noexcept { m_tAuctionBegin = time; }
    void SetAuctionEndTime(std::int32_t time) noexcept { m_tAuctionEnd = time; }
    void SetAuctionLastCancel(std::int32_t time) noexcept { m_tLastCancel = time; }
    void SetNextCost(int nextCost, int costFail);
    std::int32_t GetNextCost() const noexcept { return m_tNextCost; }
    bool IsInAuction(int pledgeId);
    bool CanAttend(int pledgeId, User* user);
    void DelAuction(int pledgeId);
    void AddAuction(int pledgeId, std::int64_t deposit);
    void AddAuction2(int pledgeId, std::int64_t deposit, int registerTime);
    void GetAuction(std::vector<AuctionInfo>& auctions);
    int GetAuctionCount();
    void SetMinBidPrice(std::int64_t price) noexcept { m_nMinBidPrice = price; }
    std::int64_t GetMinBidPrice() const noexcept { return m_nMinBidPrice; }
    std::int64_t GetAuctionPrice(int pledgeId);
    bool CanDoAuction();
    void EnterDoAuction();
    void LeaveDoAuction();
    void ReplyDoAuction(int result, int auctionId, std::int64_t price, int winnerPledgeId);

    CYieldLock m_AuctionSect; // +0x498
    std::int32_t m_tAuctionBegin; // +0x4a8
    std::int32_t m_tAuctionEnd; // +0x4ac
    std::int32_t m_tLastCancel; // +0x4b0
    std::int32_t m_tNextCost; // +0x4b4
    std::int64_t m_nLastPrice; // +0x4b8
    int m_nBidPrice; // +0x4c0
    std::byte m_alignment4c4To4c8[4]; // +0x4c4..+0x4c7
    std::int64_t m_nMinBidPrice; // +0x4c8
    int m_nBidDay; // +0x4d0
    char16_t m_sDesc[255]; // +0x4d4..+0x6d1
    std::byte m_alignment6d2To6d8[6]; // +0x6d2..+0x6d7
    std::vector<AuctionInfo> m_Auctions; // +0x6d8
    SaleState m_SaleState; // +0x6f8
    int m_nAuctionId; // +0x6fc
    std::uint32_t m_nCheckSendable; // +0x700
    bool m_bDoAuction; // +0x704
    std::byte m_alignment705To708[3]; // +0x705..+0x707
};

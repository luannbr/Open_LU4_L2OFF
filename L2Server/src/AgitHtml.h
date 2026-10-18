#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CUserSocket;
class CWorldAgit;


// Current L2Server AgitHtml.cpp owner. The target class is standalone: its
// only virtual surface is the destructor and it does not derive CParseHTML.
class CAgitHTML {
public:
    using StrStrMapType = std::map<std::u16string, std::u16string, runtime::CStringLess<std::u16string>>;

    explicit CAgitHTML(CUserSocket* socket);
    virtual ~CAgitHTML();

    int SendHTML(std::u16string html);
    void AgitBidReRequest(int residenceId, int alreadyChecked);
    void AgitBid1Request(int residenceId, int alreadyChecked);
    void AgitBidRequest(int residenceId, std::int64_t price);
    bool AgitBidCancelRequest(int residenceId, int auctionId, int pledgeId);
    void AgitSale1Request();
    bool SellActionRequest(std::int64_t minimumBid, int bidDay, const char16_t* description);
    bool SaleCancelRequest();
    void AgitMaid();
    void AgitDealer();
    void BidderList(int residenceId, int page, int backMode);
    void AgitInfo(int mode);
    void AgitSale1Reply(int residenceId, int pledgeId, std::int64_t pledgeAdena);
    void AgitSale3(std::int64_t minimumBid, int bidDay, wchar_t* description);
    void AgitSale2();
    void SaleCancel();
    int AuctionList(int page);
    void AgitAuctionInfo(int residenceId);
    void AgitSaleInfo(int residenceId, int backMode);
    void AgitBidInfo(int residenceId);
    void AgitBid1ReReply(int residenceId, int alreadyChecked, std::int64_t pledgeAdena);
    void AgitBid1Reply(int residenceId, int alreadyChecked, std::int64_t pledgeAdena);
    void AgitBid2(int residenceId, std::int64_t price, int minimumPrice);
    void AgitCommand(int residenceId, int context, std::int64_t pledgeAdena, int command, int ignored);
    void AgitMaidInfo();
    bool SellActionReply(
        int auctionId, std::int64_t minimumBid, int bidDay, wchar_t* description, int auctionEnd, int success
    );
    bool SaleCancelReply(int ignored, int lastCancel);
    void AuctionMine(bool suppressNotFound);
    bool AgitBidReply(int residenceId, std::int64_t price, int success, int resultFlags);
    bool AgitBidCancelReply(int residenceId, int pledgeId, std::int64_t ignored);
    void AgitBidCancel(int residenceId);


private:
    CWorldAgit* GetPledgeWorldAgit(int flags, bool report);
    void AddStringMap(std::u16string_view name, std::u16string_view value);

    StrStrMapType m_mapParam; // +0x08
    CUserSocket* m_pUserSocket; // +0x28
};

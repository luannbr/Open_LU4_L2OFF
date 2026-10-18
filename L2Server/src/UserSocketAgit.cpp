#include "AgitHtml.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

std::size_t TargetAsciiLength(const char* const value) {
    std::size_t length = 0;
    while (value[length] != '\0') {
        ++length;
    }
    return length;
}

std::size_t TargetWideLength(const SharedUtilsWideChar* const value) {
    std::size_t length = 0;
    while (value[length] != 0) {
        ++length;
    }
    return length;
}

bool TargetStartsWith(const SharedUtilsWideChar* const value, const char* const prefix) {
    const std::size_t length = TargetAsciiLength(prefix);
    for (std::size_t index = 0; index < length; ++index) {
        if (value[index] != static_cast<SharedUtilsWideChar>(static_cast<unsigned char>(prefix[index]))) {
            return false;
        }
    }
    return true;
}

} // namespace

// MSVC puts top-level const of pointer parameters into the decorated name.
// Keep the definition free of it so it matches the declaration elsewhere.
bool AgitHTML(CSocket* socket, const SharedUtilsWideChar* command) {
    // Current L2Server 0x00c2d940.
    CUserSocket* const userSocket = static_cast<CUserSocket*>(socket);
    User* const user = userSocket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] No user... on Html CMD Packet (AgitHTML)",
        //     L"usersocket_agit.cpp",
        //     0x1a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] No user... on Html CMD Packet (AgitHTML)",
        //     L"usersocket_agit.cpp",
        //     0x1a)
        L2_LOG_WARN(Log, "Clan-hall HTML command ignored: socket %p has no bound player.", static_cast<void*>(socket));
        return true;
    }

    constexpr char infoPrefix[] = "_agitinfo";
    if (TargetStartsWith(command, infoPrefix)) {
        int mode = 0;
        CAgitHTML html(userSocket);
        ParseHTMLParam(command + TargetAsciiLength(infoPrefix) + 1, const_cast<char*>("d"), &mode);
        html.AgitInfo(mode);
        return true;
    }

    constexpr char maidInfoPrefix[] = "_agitminfo";
    if (TargetStartsWith(command, maidInfoPrefix)) {
        CAgitHTML html(userSocket);
        html.AgitMaidInfo();
        return true;
    }

    constexpr char sale1Prefix[] = "_agitsale1";
    if (TargetStartsWith(command, sale1Prefix)) {
        CAgitHTML html(userSocket);
        html.AgitSale1Request();
        return true;
    }

    constexpr char sale2Prefix[] = "_agitsale2";
    if (TargetStartsWith(command, sale2Prefix)) {
        CAgitHTML html(userSocket);
        html.AgitSale2();
        return true;
    }

    constexpr char sale3Prefix[] = "_agitsale3";
    if (TargetStartsWith(command, sale3Prefix)) {
        return true;
    }

    constexpr char sellActionPrefix[] = "_agitsellact";
    if (TargetStartsWith(command, sellActionPrefix)) {
        std::int64_t minimumBid = 0;
        int bidDay = 0;
        std::array<SharedUtilsWideChar, 0x100> description{};
        ParseHTMLParam(
            command + TargetAsciiLength(sellActionPrefix) + 1, const_cast<char*>("Qds"), &minimumBid, &bidDay, 0x1fe,
            description.data()
        );
        CAgitHTML html(userSocket);
        html.SellActionRequest(minimumBid, bidDay, reinterpret_cast<const char16_t*>(description.data()));
        return true;
    }

    constexpr char maidPrefix[] = "_agitmaid";
    if (TargetStartsWith(command, maidPrefix)) {
        CAgitHTML html(userSocket);
        html.AgitMaid();
        return true;
    }

    constexpr char saleCancelPrefix[] = "_agitsalecan";
    if (TargetStartsWith(command, saleCancelPrefix)) {
        CAgitHTML html(userSocket);
        html.SaleCancel();
        return true;
    }

    constexpr char cancelActionPrefix[] = "_agitcanact";
    if (TargetStartsWith(command, cancelActionPrefix)) {
        CAgitHTML html(userSocket);
        html.SaleCancelRequest();
        return true;
    }

    constexpr char dealerPrefix[] = "_agitdealer";
    if (TargetStartsWith(command, dealerPrefix)) {
        CAgitHTML html(userSocket);
        html.AgitDealer();
        return true;
    }

    constexpr char auctionListPrefix[] = "_agitauclist";
    if (TargetStartsWith(command, auctionListPrefix)) {
        CAgitHTML html(userSocket);
        int page = 1;
        if (TargetAsciiLength(auctionListPrefix) < TargetWideLength(command)) {
            ParseHTMLParam(command + TargetAsciiLength(auctionListPrefix) + 1, const_cast<char*>("d"), &page);
        }
        html.AuctionList(page);
        return true;
    }

    constexpr char auctionMinePrefix[] = "_agitaucmy";
    if (TargetStartsWith(command, auctionMinePrefix)) {
        CAgitHTML html(userSocket);
        html.AuctionMine(false);
        return true;
    }

    constexpr char auctionInfoPrefix[] = "_agitaucinfo";
    if (TargetStartsWith(command, auctionInfoPrefix)) {
        int residenceId = 0;
        ParseHTMLParam(command + TargetAsciiLength(auctionInfoPrefix) + 1, const_cast<char*>("d"), &residenceId);
        CAgitHTML html(userSocket);
        html.AgitAuctionInfo(residenceId);
        return true;
    }

    constexpr char bidAgainPrefix[] = "_agitbidre";
    if (TargetStartsWith(command, bidAgainPrefix)) {
        int residenceId = 0;
        int alreadyChecked = 0;
        ParseHTMLParam(
            command + TargetAsciiLength(bidAgainPrefix) + 1, const_cast<char*>("dd"), &residenceId, &alreadyChecked
        );
        CAgitHTML html(userSocket);
        html.AgitBidReRequest(residenceId, alreadyChecked);
        return true;
    }

    constexpr char bid1Prefix[] = "_agitbid1";
    if (TargetStartsWith(command, bid1Prefix)) {
        int residenceId = 0;
        int alreadyChecked = 0;
        ParseHTMLParam(command + TargetAsciiLength(bid1Prefix) + 1, const_cast<char*>("dd"), &residenceId, &alreadyChecked);
        CAgitHTML html(userSocket);
        html.AgitBid1Request(residenceId, alreadyChecked);
        return true;
    }

    constexpr char bid2Prefix[] = "_agitbid2";
    if (TargetStartsWith(command, bid2Prefix)) {
        return true;
    }

    constexpr char bidActionPrefix[] = "_agitattact";
    if (TargetStartsWith(command, bidActionPrefix)) {
        int residenceId = 0;
        std::int64_t price = 0;
        ParseHTMLParam(command + TargetAsciiLength(bidActionPrefix) + 1, const_cast<char*>("dQ"), &residenceId, &price);
        CAgitHTML html(userSocket);
        html.AgitBidRequest(residenceId, price);
        return true;
    }

    constexpr char bidderListPrefix[] = "_agitbidlist";
    if (TargetStartsWith(command, bidderListPrefix)) {
        int residenceId = 0;
        int page = 0;
        int backMode = 0;
        ParseHTMLParam(
            command + TargetAsciiLength(bidderListPrefix) + 1, const_cast<char*>("ddd"), &residenceId, &page, &backMode
        );
        CAgitHTML html(userSocket);
        html.BidderList(residenceId, page, backMode);
        return true;
    }

    constexpr char bidCancelPrefix[] = "_agitbidcancel";
    if (TargetStartsWith(command, bidCancelPrefix)) {
        int residenceId = 0;
        ParseHTMLParam(command + TargetAsciiLength(bidCancelPrefix) + 1, const_cast<char*>("d"), &residenceId);
        CAgitHTML html(userSocket);
        html.AgitBidCancel(residenceId);
        return true;
    }

    constexpr char bidCancelActionPrefix[] = "_agitbidcanact";
    if (TargetStartsWith(command, bidCancelActionPrefix)) {
        int residenceId = 0;
        int pledgeId = 0;
        int auctionId = 0;
        ParseHTMLParam(
            command + TargetAsciiLength(bidCancelActionPrefix) + 1, const_cast<char*>("ddd"), &residenceId, &pledgeId,
            &auctionId
        );
        CAgitHTML html(userSocket);
        html.AgitBidCancelRequest(residenceId, auctionId, pledgeId);
        return true;
    }

    return true;
}

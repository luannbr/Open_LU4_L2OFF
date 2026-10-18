#pragma once

#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "User.h"

namespace l2server::logging {

// Diagnostic context only. This does not change auction eligibility or replies.
enum class AuctionLogContext { Request, HtmlPage, DatabaseReply };

inline void ReportUnavailableAuctionClan(
    CLog& logger,
    User& user,
    const wchar_t* const operation,
    const char* const sourceFile,
    const AuctionLogContext context = AuctionLogContext::Request
) {
    const wchar_t* const effect = context == AuctionLogContext::DatabaseReply
        ? L"the database reply is not applied"
        : (context == AuctionLogContext::HtmlPage ? L"the page is not sent" : L"the request is rejected");
    const CSharedCreatureData* const data = user.m_data;
    if (data == nullptr) {
        runtime::logging::LogAt(
            logger, LOG_ERROR, sourceFile, L"Cannot %ls: character data is unavailable; %ls.", operation, effect
        );
        return;
    }
    const std::uint32_t clanDatabaseId = data->m_nPledgeID;
    if (clanDatabaseId == 0) {
        if (context != AuctionLogContext::DatabaseReply) {
            // Noise: No clan membership is an ordinary request/page eligibility refusal.
            return;
        }
        // A database reply may follow a completed write. Do not imply a rollback.
        runtime::logging::LogAt(
            logger, LOG_WARN, sourceFile, "Cannot %s: character '%s' has no clan; the database reply is not applied.",
            WideToUtf8(operation).c_str(), Utf16ToUtf8(user.GetName()).c_str()
        );
        return;
    }
    runtime::logging::LogAt(
        logger, LOG_WARN, sourceFile, "Cannot %s: clan lookup returned no object for character '%s' (clan DB ID %u); %s.",
        WideToUtf8(operation).c_str(), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned>(clanDatabaseId),
        WideToUtf8(effect).c_str()
    );
}

} // namespace l2server::logging

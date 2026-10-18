#include "AdminUtil.h"
#include "Shared/src/TemplateHelper.h"

#include "CacheSocket.h"
#include "User.h"
#include "UserDb.h"
#include "WareHouse.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/LogFormatter.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/platform/Runtime.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cwchar>

namespace {

struct CommissionTimestamp {
    std::array<wchar_t, 32> text{};
    int second = 0;
};

// Source-local carrier for the exact 16-column lin_LoadPostByPostId result
// consumed by current AdminUtil::CancelPostProc.
struct AdminPostRecord {
    int senderId = 0;
    int receiverId = 0;
    int trade = 0;
    std::array<char16_t, 61> title{};
    std::array<char16_t, 1001> contents{};
    int process = 0;
    int returnable = 0;
    int returned = 0;
    int senderDeleted = 0;
    int receiverDeleted = 0;
    int sentTime = 0;
    int itemCount = 0;
    long long tradeMoney = 0;
    long long postFee = 0;
    int senderType = 0;
    int recordedId = 0;
};

bool LoadAdminPost(DBConn& connection, int postId, AdminPostRecord& post) {
    connection.Bind(&post.senderId);
    connection.Bind(&post.receiverId);
    connection.Bind(&post.trade);
    connection.Bind(post.title.data(), 0x7a);
    connection.Bind(post.contents.data(), 0x7d2);
    connection.Bind(&post.process);
    connection.Bind(&post.returnable);
    connection.Bind(&post.returned);
    connection.Bind(&post.senderDeleted);
    connection.Bind(&post.receiverDeleted);
    connection.Bind(&post.sentTime);
    connection.Bind(&post.itemCount);
    connection.BindNullable(&post.tradeMoney, nullptr);
    connection.BindNullable(&post.postFee, nullptr);
    connection.BindNullable(&post.senderType, nullptr);
    connection.BindNullable(&post.recordedId, nullptr);
    constexpr const wchar_t* statement = L"{Call lin_LoadPostByPostId (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&postId);
    return connection.ExecutePrepared(statement) && connection.Fetch();
}

void WriteCancelPostAudit(
    const AdminPostRecord& post, int postId, const CUserSP& sender, const CUserSP& receiver, const wchar_t* auditText
) {
    // Current 0x0044ce68 constructs record 0x2a1 with the sender as actor
    // for player mail and the receiver as actor for system mail.
    const CUserSP& actor = post.senderId < 1 ? receiver : sender;
    const std::u16string title = std::u16string(post.title.data());
    const std::u16string receiverName = post.senderId < 1 ? std::u16string{} : std::u16string(receiver->GetCharName());
    const std::u16string actorName = std::u16string(actor->GetCharName());
    const std::u16string actorAccount = std::u16string(actor->GetAccountName());
    const std::u16string audit = WideToUtf16(auditText);

    CLogFormatter formatter;
    formatter.WriteLog(
        0x2a1, 0, 0, actor->GetId(), actor->GetAccountID(), actor->GetX(), actor->GetY(), actor->GetZ(), title.c_str(), u"",
        receiverName.c_str(), postId, post.trade, post.itemCount, post.tradeMoney, post.sentTime, 0, 0, 0, 0, 0,
        audit.c_str(), audit.c_str(), actorName.c_str(), actorAccount.c_str(), 0
    );
}

CommissionTimestamp CurrentCommissionTimestamp() {
    // Current AdminUtil.cpp 0x0044b8b0 calls GetLocalTime once. The seconds
    // value is used by wsprintfW and remains in the outgoing variadic stack
    // slot consumed by the shipping cddQdd format below.
    const runtime::SystemTime now = runtime::GetLocalTimeNow();
    CommissionTimestamp result;
    result.second = static_cast<int>(now.wSecond);
    std::swprintf(
        result.text.data(), result.text.size(), L"%d-%d-%d %d:%d:%d", static_cast<int>(now.wYear),
        static_cast<int>(now.wMonth), static_cast<int>(now.wDay), static_cast<int>(now.wHour), static_cast<int>(now.wMinute),
        result.second
    );
    return result;
}

} // namespace

namespace AdminUtil {

bool CancelPostProc(int postId, wchar_t* auditText, int* reason) {
    // Exact current Cached.exe 0x0044bc50. The procedure restores post
    // attachments from warehouse 4 to warehouse 1 before deleting both
    // relational post rows and writing the administrative audit.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    AdminPostRecord post;
    // if (!LoadAdminPost(connection, postId, post))
        // return false
    if (!LoadAdminPost(connection, postId, post)) {
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Cannot cancel mail %d: mail lookup returned no result (execution or fetch failed). Cancellation stopped.",
        //     postId)
        L2_LOG_ERROR(Log,
            "[admin mail] Cannot cancel mail %d: mail lookup returned no result (execution or fetch failed); cancellation stopped.",
            postId);
        return false;
    }
    connection.UnBind();

    std::array<int, 8> itemIds{};
    std::array<std::int64_t, 8> amounts{};
    int loadedItemCount = 0;
    int itemId = 0;
    long long amount = 0;
    connection.Bind(&itemId);
    connection.Bind(&amount);
    constexpr const wchar_t* loadItemsStatement = L"{Call lin_LoadPostItem (?)}";
    connection.Prepare(loadItemsStatement);
    connection.BindParameter(&postId);
    // if (!connection.ExecutePrepared(loadItemsStatement))
        // return false
    if (!connection.ExecutePrepared(loadItemsStatement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Cannot cancel mail %d: attachment query failed. No attachment return or mail deletion attempted.",
        //     postId)
        L2_LOG_ERROR(Log,
            "[admin mail] Cannot cancel mail %d: attachment query failed; no attachment return or mail deletion attempted.",
            postId);
        return false;
    }
    while (connection.Fetch()) {
        if (loadedItemCount >= static_cast<int>(itemIds.size())) {
            // Log.AddT(LOG_ERROR, L"[AdminSocket] Too many Item Count In Mail %d. User %d", postId, post.senderId)
            // Log.Add(LOG_ERROR,
            //     "[admin mail] Mail %d from sender %d has more than %zu attachment rows. Collected %d, declared %d. Remaining rows are not collected. Cancellation continues using the declared count.",
            //     postId, post.senderId, itemIds.size(), loadedItemCount, post.itemCount)
            L2_LOG_ERROR(Log,
                "[admin mail] Mail %d from sender %d has more than %zu attachment rows; collected %d, declared %d. Remaining rows are not collected; cancellation continues using the declared count.",
                postId, post.senderId, itemIds.size(), loadedItemCount, post.itemCount);
            break;
        }
        itemIds[static_cast<std::size_t>(loadedItemCount)] = itemId;
        amounts[static_cast<std::size_t>(loadedItemCount)] = amount;
        ++loadedItemCount;
    }
    if (loadedItemCount != post.itemCount) {
        // Log.AddT(LOG_ERROR, L"[AdminSocket] Invalid Item Count In Mail %d. User %d.", postId, post.senderId)
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Attachment count mismatch for mail %d from sender %d: declared %d, collected %d. Cancellation continues using the declared count.",
        //     postId, post.senderId, post.itemCount, loadedItemCount)
        L2_LOG_ERROR(Log,
            "[admin mail] Attachment count mismatch for mail %d from sender %d: declared %d, collected %d; cancellation continues using the declared count.",
            postId, post.senderId, post.itemCount, loadedItemCount);
    }
    connection.UnBind();

    CUserSP sender;
    if (post.senderId > 0) {
        sender = g_userDb.GetUser(post.senderId, true, false, runtime::process::CurrentDatabaseId());
        if (!sender) {
            // Log.Add(LOG_ERROR,
            //     "[admin mail] Cannot cancel mail %d: sender %d could not be loaded. Cancellation stopped with reason 4.",
            //     postId, post.senderId)
            L2_LOG_ERROR(Log,
                "[admin mail] Cannot cancel mail %d: sender %d could not be loaded; cancellation stopped with reason 4.",
                postId, post.senderId);
            *reason = 4;
            return false;
        }
        if (sender->IsLoggedIn()) {
            // Log.Add(LOG_WARN,
            //     "[admin mail] Cancellation of mail %d rejected: sender %d is online (reason 7).",
            //     postId, post.senderId)
            L2_LOG_WARN(Log,
                "[admin mail] Cancellation of mail %d rejected: sender %d is online (reason 7).",
                postId, post.senderId);
            *reason = 7;
            return false;
        }
    }

    CUserSP receiver = g_userDb.GetUser(post.receiverId, true, false, runtime::process::CurrentDatabaseId());
    if (!receiver) {
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Cannot cancel mail %d: receiver %d could not be loaded. Cancellation stopped with reason 4.",
        //     postId, post.receiverId)
        L2_LOG_ERROR(Log,
            "[admin mail] Cannot cancel mail %d: receiver %d could not be loaded; cancellation stopped with reason 4.",
            postId, post.receiverId);
        *reason = 4;
        return false;
    }
    if (receiver->IsLoggedIn()) {
        // Log.Add(LOG_WARN,
        //     "[admin mail] Cancellation of mail %d rejected: receiver %d is online (reason 7).",
        //     postId, post.receiverId)
        L2_LOG_WARN(Log,
            "[admin mail] Cancellation of mail %d rejected: receiver %d is online (reason 7).",
            postId, post.receiverId);
        *reason = 7;
        return false;
    }

    CUserSP postOwner = post.returned == 1 && post.senderId == -1 ? receiver : sender;
    if (!postOwner) {
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Cannot cancel mail %d: no attachment owner selected (sender %d, receiver %d, returned %d). Cancellation stopped with reason 4.",
        //     postId, post.senderId, post.receiverId, post.returned)
        L2_LOG_ERROR(Log,
            "[admin mail] Cannot cancel mail %d: no attachment owner selected (sender %d, receiver %d, returned %d); cancellation stopped with reason 4.",
            postId, post.senderId, post.receiverId, post.returned);
        *reason = 4;
        return false;
    }

    CWareHouseSP postWarehouse = postOwner->GetWareHouse(4, true);
    CWareHouseSP destinationWarehouse = postOwner->GetWareHouse(1, true);
    bool restored = true;
    if (!postWarehouse || !destinationWarehouse) {
        // The target logs this state but leaves its success byte set and
        // continues with relational deletion.
        // Log.AddT(LOG_ERROR, L"Error, Invalid sp.")
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Attachment return skipped for mail %d, owner %u: %s is unavailable. Mail and attachment-row deletion are still attempted.",
        //     postId, postOwner->GetId(), !postWarehouse ? "mail warehouse" : "destination warehouse")
        L2_LOG_ERROR(Log,
            "[admin mail] Attachment return skipped for mail %d, owner %u: %s is unavailable; mail and attachment-row deletion are still attempted.",
            postId, postOwner->GetId(), !postWarehouse ? "mail warehouse" : "destination warehouse");
    } else {
        const auto restore = [&]() {
            restored = postWarehouse->DeletePost(
                nullptr, destinationWarehouse.get(), post.itemCount, itemIds.data(), amounts.data(),
                static_cast<int>(postOwner->GetId())
            );
            if (!restored) {
                // Log.AddT(LOG_ERROR, L"Invalid item deleting in mail id %d(adminsocket)", postId)
                // Log.Add(LOG_ERROR,
                //     "[admin mail] Attachment return failed for mail %d, owner %u (declared %d, collected %d). Cancellation stopped with reason 1 before deleting mail records.",
                //     postId, postOwner->GetId(), post.itemCount, loadedItemCount)
                L2_LOG_ERROR(Log,
                    "[admin mail] Attachment return failed for mail %d, owner %u (declared %d, collected %d); cancellation stopped with reason 1 before deleting mail records.",
                    postId, postOwner->GetId(), post.itemCount, loadedItemCount);
                *reason = 1;
            }
        };

        const std::uint64_t postLock = postWarehouse->GetLockAddr();
        const std::uint64_t destinationLock = destinationWarehouse->GetLockAddr();
        if (postLock == destinationLock) {
            // Log.AddT(LOG_ERROR, L"Same warehouse double writelock?-_-,")
            // Log.Add(LOG_WARN,
            //     "[admin mail] Cancellation of mail %d for owner %u uses the same source and destination warehouse. Attachment return continues under one lock.",
            //     postId, postOwner->GetId())
            L2_LOG_WARN(Log,
                "[admin mail] Cancellation of mail %d for owner %u uses the same source and destination warehouse; attachment return continues under one lock.",
                postId, postOwner->GetId());
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                restore();
            }
        } else if (destinationLock < postLock) {
            L2_WRITE_GUARD(*destinationWarehouse.operator->());
            {
                L2_WRITE_GUARD(*postWarehouse.operator->());
                restore();
            }
        } else {
            L2_WRITE_GUARD(*postWarehouse.operator->());
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                restore();
            }
        }
    }
    if (!restored)
        return false;

    constexpr const wchar_t* deletePostStatement = L"{Call lin_DeletePost (?)}";
    connection.Prepare(deletePostStatement);
    connection.BindParameter(&postId);
    if (!connection.ExecutePrepared(deletePostStatement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Cannot delete mail %d after attachment handling (%s). Cancellation reports failure with reason 1.",
        //     postId, postWarehouse && destinationWarehouse ? "return processing completed" : "return was skipped because a warehouse was unavailable")
        // L2_LOG_ERROR(Log,
        //     "[admin mail] Cannot delete mail %d after attachment handling (%s). Cancellation reports failure with reason 1.",
        //     postId, postWarehouse && destinationWarehouse ? "return processing completed" : "return was skipped because a warehouse was unavailable")
        L2_LOG_ERROR(Log,
            "[admin mail] Cannot delete mail %d after attachment handling (%s); cancellation reports failure with reason 1.",
            postId, postWarehouse && destinationWarehouse
                ? "return helper reported success, but individual attachment commits may have failed"
                : "return was skipped because a warehouse was unavailable");
        *reason = 1;
        return false;
    }
    connection.UnBind();

    constexpr const wchar_t* deleteItemsStatement = L"{Call lin_DeletePostItemForAdmin (?)}";
    connection.Prepare(deleteItemsStatement);
    connection.BindParameter(&postId);
    if (!connection.ExecutePrepared(deleteItemsStatement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Cannot delete attachment records for mail %d: mail deletion already executed. Cancellation reports failure with reason 1, attachment records may remain.",
        //     postId)
        L2_LOG_ERROR(Log,
            "[admin mail] Cannot delete attachment records for mail %d: mail deletion already executed; cancellation reports failure with reason 1, attachment records may remain.",
            postId);
        *reason = 1;
        return false;
    }

    WriteCancelPostAudit(post, postId, sender, receiver, auditText);
    return true;
}

bool CancelCommissionProc(std::int64_t commissionId, wchar_t* auditText [[maybe_unused]], int* reason [[maybe_unused]]) {
    // Current Cached.exe 0x0044b208. The two trailing source parameters are
    // kept by the guard signature but are never read by this body.
    L2_GUARD_FUNCTION();

    int expiredType = 0;
    int firstType = -1;
    int characterId = 0;
    int itemId = 0;
    DBConn query(runtime::process::CurrentDatabaseId());
    query.Bind(&expiredType);
    query.Bind(&firstType);
    query.Bind(&characterId);
    query.Bind(&itemId);
    constexpr const wchar_t* loadStatement = L"{Call lin_GetSeizeCommissionData (?)}";
    query.Prepare(loadStatement);
    query.BindParameter(&commissionId);

    // The shipping function treats a missing/failed row as an already
    // completed cancellation and reaches its common true return.
    // if (!query.ExecutePrepared(loadStatement) || !query.Fetch()) {
    const bool queryExecuted = query.ExecutePrepared(loadStatement);
    if (!queryExecuted || !query.Fetch()) {
        if (!queryExecuted) {
            // Log.Add(LOG_ERROR,
            //     "[admin commission] Cannot query listing %lld in database %d. Cancellation still reports success and the caller continues to the requested item action. No cancellation changes were attempted.",
            //     static_cast<long long>(commissionId), runtime::process::CurrentDatabaseId())
            L2_LOG_ERROR(Log,
                "[admin commission] Cannot query listing %lld in database %d; cancellation still reports success and the caller continues to the requested item action. No cancellation changes were attempted.",
                static_cast<long long>(commissionId), runtime::process::CurrentDatabaseId());
        }
        return true;
    }

    CUserSP destinationUser = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP commissionUser = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!destinationUser || !commissionUser) {
        // Log.AddT(LOG_ERROR, L"CancelCommission User valid!!. CharDBID[%d].", characterId)
        // Log.Add(LOG_ERROR,
        //     "[admin commission] Cannot cancel listing %lld for character %d: %s could not be loaded. Cancellation reports failure.",
        //     static_cast<long long>(commissionId), characterId, !destinationUser ? "destination character" : "commission character")
        L2_LOG_ERROR(Log,
            "[admin commission] Cannot cancel listing %lld for character %d: %s could not be loaded; cancellation reports failure.",
            static_cast<long long>(commissionId), characterId, !destinationUser ? "destination character" : "commission character");
        return false;
    }

    CWareHouseSP destinationWarehouse = destinationUser->GetWareHouse(1, true);
    CWareHouseSP commissionWarehouse = commissionUser->GetWareHouse(6, true);
    bool cancelled = false;
    if (!destinationWarehouse || !commissionWarehouse) {
        // Noise: The cancellation failure below identifies the unavailable warehouse, listing, character and item.
        // Log.AddT(LOG_ERROR, L"Error, Invalid sp.")
    } else {
        const std::uint64_t destinationLock = destinationWarehouse->GetLockAddr();
        const std::uint64_t commissionLock = commissionWarehouse->GetLockAddr();
        auto cancel = [&]() {
            return commissionWarehouse->CancelCommissionInven(nullptr, destinationWarehouse.get(), characterId, itemId);
        };

        if (destinationLock == commissionLock) {
            // Log.AddT(LOG_ERROR, L"Same warehouse double writelock?-_-,")
            // Log.Add(LOG_WARN,
            //     "[admin commission] Cancellation of listing %lld for character %d uses the same source and destination warehouse. Item return continues under one lock.",
            //     static_cast<long long>(commissionId), characterId)
            L2_LOG_WARN(Log,
                "[admin commission] Cancellation of listing %lld for character %d uses the same source and destination warehouse; item return continues under one lock.",
                static_cast<long long>(commissionId), characterId);
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                cancelled = cancel();
            }
        } else if (destinationLock < commissionLock) {
            L2_WRITE_GUARD(*destinationWarehouse.operator->());
            {
                L2_WRITE_GUARD(*commissionWarehouse.operator->());
                cancelled = cancel();
            }
        } else {
            L2_WRITE_GUARD(*commissionWarehouse.operator->());
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                cancelled = cancel();
            }
        }
    }

    if (!cancelled) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s],[%d]Commission Delete Item Fail!! "
        //     L"CommissionID[%I64d], CharDBID[%d]",
        //     L"AdminUtil.cpp", 0x48, static_cast<long long>(commissionId), characterId
        // )
        // Log.Add(LOG_ERROR,
        //     "[admin commission] Cannot cancel listing %lld for character %d, item %d: %s. Cancellation reports failure.",
        //     static_cast<long long>(commissionId), characterId, itemId, !destinationWarehouse ? "destination warehouse is unavailable" : (!commissionWarehouse ? "commission warehouse is unavailable" : "item return failed"))
        L2_LOG_ERROR(Log,
            "[admin commission] Cannot cancel listing %lld for character %d, item %d: %s; cancellation reports failure.",
            static_cast<long long>(commissionId), characterId, itemId, !destinationWarehouse ? "destination warehouse is unavailable" : (!commissionWarehouse ? "commission warehouse is unavailable" : "item return failed"));
        return false;
    }

    const CommissionTimestamp deleteTime = CurrentCommissionTimestamp();
    {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* deleteStatement = L"{Call lin_DeleteCommission (?, ?, ?)}";
        connection.Prepare(deleteStatement);
        connection.BindParameter(&commissionId);
        connection.BindParameter(&characterId);
        connection.BindParameter(deleteTime.text.data());
        // connection.ExecutePrepared(deleteStatement)
        if (!connection.ExecutePrepared(deleteStatement)) {
            // Log.Add(LOG_ERROR,
            //     "[admin commission] Cannot delete listing %lld after item return processing for character %d, item %d. Cancellation still reports success and server notification is still attempted. Listing and item state require reconciliation.",
            //     static_cast<long long>(commissionId), characterId, itemId)
            L2_LOG_ERROR(Log,
                "[admin commission] Cannot delete listing %lld after item return processing for character %d, item %d; cancellation still reports success and server notification is still attempted. Listing and item state require reconciliation.",
                static_cast<long long>(commissionId), characterId, itemId);
        }
    }

    if (CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire)) {
        // Current call 0x0044b9df really uses cddQdd while supplying the
        // logical cdQdd values. VAssemble therefore serializes the low
        // commission-id dword, a Q made from firstType, expiredType, and the
        // seconds value kept at [rsp+0x38]. Supply every consumed slot
        // so the portable build reproduces that malformed
        // shipping wire deterministically instead of invoking missing-vararg
        // undefined behavior.
        server->Send(
            "cddQdd", 0x94, 0x1b, static_cast<int>(commissionId), static_cast<long long>(firstType), expiredType,
            deleteTime.second
        );
    } else {
        // Log.Add(LOG_WARN,
        //     "[admin commission] No active game-server connection to notify about listing %lld, character %d, item %d. Cancellation still reports success and the caller continues to the requested item action.",
        //     static_cast<long long>(commissionId), characterId, itemId)
        L2_LOG_WARN(Log,
            "[admin commission] No active game-server connection to notify about listing %lld, character %d, item %d; cancellation still reports success and the caller continues to the requested item action.",
            static_cast<long long>(commissionId), characterId, itemId);
    }
    return true;
}

} // namespace AdminUtil

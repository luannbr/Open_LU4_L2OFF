#include "ReservedAnnounce.h"

#include "CacheSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cwchar>
#include <string>

namespace {

std::string ReservedAnnounceLogText(const wchar_t* value) { return WideToUtf8(value == nullptr ? L"" : value); }

} // namespace

bool CReservedAnnounce::LoadReservedAnnounce(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x005282d8. The request payload is intentionally
    // unused by the shipping method.
    L2_GUARD_FUNCTION();

    int announceId = 0;
    int startTime = 0;
    int announceType = 0;
    int repeat = 0;
    int flag = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&announceId);
    connection.Bind(&startTime);
    connection.Bind(&announceType);
    connection.Bind(&repeat);
    connection.Bind(&flag);

    constexpr const wchar_t* statement = L"{Call lin_GetReservedAnnounceList}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement)) {
        CPacketHelperEx<20480> announcements;
        while (connection.Fetch()) {
            if (announcements.Assemble("ddddd", announceId, startTime, announceType, repeat, flag) <= 0) {
                // Previous: L2_LOG_ERROR(Log, "Cannot serialize scheduled announcement %d into the reply, entry skipped.", announceId)
                L2_LOG_ERROR(Log, "[announcements] Cannot serialize scheduled announcement %d into the reply; entry skipped.", announceId);
                continue;
            }

            int messageOrder = 0;
            std::array<char16_t, 32> message{};
            DBConn messageConnection(runtime::process::CurrentDatabaseId());
            messageConnection.Bind(&messageOrder);
            messageConnection.Bind(message.data(), 0x40);
            CPacketHelperEx<20480> messages;
            constexpr auto messageStatement = L"{Call lin_GetReservedAnnounceMsg (?)}";
            messageConnection.Prepare(messageStatement);
            messageConnection.BindParameter(&announceId);
            if (messageConnection.ExecutePrepared(messageStatement)) {
                while (messageConnection.Fetch()) {
                    messages.Assemble("dS", messageOrder, message.data());
                }
            } else {
                // Previous: L2_LOG_ERROR(Log, "Cannot load messages for scheduled announcement %d: database query failed, this announcement will be assembled with an empty message list.", announceId)
                L2_LOG_ERROR(Log, "[announcements] Cannot load messages for scheduled announcement %d: database query failed; this announcement will be assembled with an empty message list.", announceId);
            }

            // The current image calls Assemble twice per announcement. Its
            // reply count is so the helper's assembled-call count,
            // not a separately maintained row count.
            announcements.Assemble("db", messages.GetAssembledCount(), messages.GetLength(), messages.GetPacket());
        }

        if (announcements.GetAssembledCount() < 1) {
            socket->Send("chd", 0xc6, 0x9a, 0);
        } else {
            socket->Send(
                "chdb", 0xc6, 0x9a, announcements.GetAssembledCount(), announcements.GetLength(), announcements.GetPacket()
            );
        }
    } else {
        // Previous: L2_LOG_ERROR(Log, "Cannot load scheduled announcements in database %d: query failed, no reply sent.", runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log, "[announcements] Cannot load scheduled announcements in database %d: query failed; no reply sent.", runtime::process::CurrentDatabaseId());
    }
    return true;
}

bool CReservedAnnounce::AddReservedAnnounce(
    int announceId, short announceType, int year, int month, int day, int hour, int minute, int repeat
) {
    // Current Cached.exe 0x005286ac. Raw instructions prove that seconds are
    // fixed to zero and the sixth request dword is the repeat value.
    L2_GUARD_FUNCTION();

    std::array<wchar_t, 32> startTime{};
    std::swprintf(startTime.data(), startTime.size(), L"%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, 0);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddReservedAnnounce (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&announceId);
    connection.BindParameter(&announceType);
    connection.BindParameter(startTime.data());
    connection.BindParameter(&repeat);
    const bool result = connection.ExecutePrepared(statement);

    const std::string printableStartTime = ReservedAnnounceLogText(startTime.data());
    // Log.Add(
    //     result ? LOG_NORMAL : LOG_ERROR,
    //     result ? "AddReservedAnnounce [id:%d][type:%d][%s][rep:%d]"
    //            : "AddReservedAnnounce Error [id:%d][type:%d][%s][rep:%d]",
    //     announceId, static_cast<int>(announceType), printableStartTime.c_str(), repeat
    // )
    // Log.Add(result ? LOG_NORMAL : LOG_ERROR,
    //     "[announcements] Schedule write %s for announcement %d (type %d, start %s, repeat %d) in database %d.",
    //     result ? "executed" : "failed", announceId, static_cast<int>(announceType), printableStartTime.c_str(), repeat, runtime::process::CurrentDatabaseId())
    L2_LOG(Log, result ? LOG_NORMAL : LOG_ERROR,
        "[announcements] Schedule write %s for announcement %d (type %d, start %s, repeat %d) in database %d.",
        result ? "executed" : "failed", announceId, static_cast<int>(announceType), printableStartTime.c_str(), repeat, runtime::process::CurrentDatabaseId());
    return result;
}

bool CReservedAnnounce::DeleteReservedAnnounce(int announceId) {
    // Current Cached.exe 0x005288cc.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteReservedAnnounce (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&announceId);
    const bool result = connection.ExecutePrepared(statement);
    // Log.Add(
    //     result ? LOG_NORMAL : LOG_ERROR,
    //     result ? "DeleteReservedAnnounce [id:%d]" : "DeleteReservedAnnounce Error [id:%d]", announceId
    // )
    // Log.Add(result ? LOG_NORMAL : LOG_ERROR,
    //     "[announcements] Schedule deletion %s for announcement %d in database %d.",
    //     result ? "executed" : "failed", announceId, runtime::process::CurrentDatabaseId())
    L2_LOG(Log, result ? LOG_NORMAL : LOG_ERROR,
        "[announcements] Schedule deletion %s for announcement %d in database %d.",
        result ? "executed" : "failed", announceId, runtime::process::CurrentDatabaseId());
    return result;
}

bool CReservedAnnounce::AddReservedAnnounceMsg(int announceId, short messageOrder, wchar_t* message) {
    // Current Cached.exe 0x00528a20.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddReservedAnnounceMsg (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&announceId);
    connection.BindParameter(&messageOrder);
    connection.BindParameter(message);
    const bool result = connection.ExecutePrepared(statement);

    const std::string printableMessage = ReservedAnnounceLogText(message);
    // Log.Add(
    //     result ? LOG_NORMAL : LOG_ERROR,
    //     result ? "AddReservedAnnounceMsg [id:%d][order:%d][%s]" : "AddReservedAnnounceMsg Error [id:%d][order:%d][%s]",
    //     announceId, static_cast<int>(messageOrder), printableMessage.c_str()
    // )
    // Log.Add(result ? LOG_NORMAL : LOG_ERROR,
    //     "[announcements] Message write %s for announcement %d, message %d, in database %d. Text: %s",
    //     result ? "executed" : "failed", announceId, static_cast<int>(messageOrder), runtime::process::CurrentDatabaseId(), printableMessage.c_str())
    L2_LOG(Log, result ? LOG_NORMAL : LOG_ERROR,
        "[announcements] Message write %s for announcement %d, message %d, in database %d. Text: %s",
        result ? "executed" : "failed", announceId, static_cast<int>(messageOrder), runtime::process::CurrentDatabaseId(), printableMessage.c_str());
    return result;
}

bool CReservedAnnounce::DeleteReservedAnnounceMsg(int announceId, short messageOrder) {
    // Current Cached.exe 0x00528bb8.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteReservedAnnounceMsg (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&announceId);
    connection.BindParameter(&messageOrder);
    const bool result = connection.ExecutePrepared(statement);
    // Log.Add(
    //     result ? LOG_NORMAL : LOG_ERROR,
    //     result ? "DeleteReservedAnnounceMsg [id:%d][order:%d]" : "DeleteReservedAnnounceMsg Error [id:%d][order:%d]",
    //     announceId, static_cast<int>(messageOrder)
    // )
    // Log.Add(result ? LOG_NORMAL : LOG_ERROR,
    //     "[announcements] Message deletion %s for announcement %d, message %d, in database %d.",
    //     result ? "executed" : "failed", announceId, static_cast<int>(messageOrder), runtime::process::CurrentDatabaseId())
    L2_LOG(Log, result ? LOG_NORMAL : LOG_ERROR,
        "[announcements] Message deletion %s for announcement %d, message %d, in database %d.",
        result ? "executed" : "failed", announceId, static_cast<int>(messageOrder), runtime::process::CurrentDatabaseId());
    return result;
}

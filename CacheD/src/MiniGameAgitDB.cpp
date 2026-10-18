#include "MiniGameAgitDB.h"

#include "CacheSocket.h"
#include "Pledge.h"
#include "PledgeDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"

#include <array>
#include <cstdint>

bool CMiniGameAgitDB::RequestLoadMiniGameAgit(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x004f7390. Paired current L2Server consumer
    // CMiniGameAgitDBHandler::ReplyLoadMiniGameAgit is at 0x00840708.
    L2_GUARD_FUNCTION();

    int agitId = 0;
    ::Disassemble(packet, "d", &agitId);

    const int worldId = runtime::process::CurrentDatabaseId();
    DBConn connection(worldId);

    int elapsedTime = 0;
    connection.Bind(&elapsedTime);
    constexpr const wchar_t* loadStatus = L"{Call lin_LoadMiniGameagitStatus (?)}";
    connection.Prepare(loadStatus);
    connection.BindParameter(&agitId);
    if (connection.ExecutePrepared(loadStatus)) {
        connection.Fetch();
    }
    connection.UnBind();

    std::array<char, 0x5000> pledgePacket{};
    std::array<int, 4> topPledgeIds{};
    std::array<int, 4> topPledgePoints{};
    int pledgeId = 0;
    int point = 0;
    int pledgeCount = 0;
    int pledgePacketLength = 0;
    connection.Bind(&pledgeId);
    connection.Bind(&point);
    constexpr const wchar_t* loadPledges = L"{Call lin_LoadMiniGameagitPledge (?)}";
    connection.Prepare(loadPledges);
    connection.BindParameter(&agitId);
    if (connection.ExecutePrepared(loadPledges)) {
        while (connection.Fetch()) {
            for (std::size_t index = 0; index < topPledgeIds.size(); ++index) {
                if (topPledgeIds[index] == 0 || topPledgePoints[index] < point) {
                    for (std::size_t move = topPledgeIds.size() - 1; move > index; --move) {
                        topPledgeIds[move] = topPledgeIds[move - 1];
                        topPledgePoints[move] = topPledgePoints[move - 1];
                    }
                    topPledgeIds[index] = pledgeId;
                    topPledgePoints[index] = point;
                    break;
                }
            }

            pledgePacketLength += ::Assemble(
                pledgePacket.data() + pledgePacketLength, static_cast<int>(pledgePacket.size()) - pledgePacketLength, "dd",
                pledgeId, point
            );
            ++pledgeCount;
        }
    }
    connection.UnBind();

    // The target performs these four lookups even though the reply contains
    // every pledge row. They populate/validate the same process pledge cache
    // used by the later MiniGameAgit state transition.
    for (const int topPledgeId : topPledgeIds) {
        if (topPledgeId == 0)
            continue;
        CPledgeSP pledge = g_pledgeDb.GetPledge(topPledgeId, false, worldId);
        if (!pledge) {
            // Log.AddT(LOG_ERROR, L"invalid pledge for minigame agit.")
            // Log.Add(LOG_ERROR,
            //     "[residence minigame] Top clan %d could not be loaded for residence %d in world %d. Reply still includes the loaded database rows.",
            //     topPledgeId, agitId, worldId)
            L2_LOG_ERROR(Log,
                "[residence minigame] Top clan %d could not be loaded for residence %d in world %d; reply still includes the loaded database rows.",
                topPledgeId, agitId, worldId);
        }
    }

    std::array<char, 0x5000> memberPacket{};
    int memberPledgeId = 0;
    int characterId = 0;
    int memberCount = 0;
    int memberPacketLength = 0;
    connection.Bind(&memberPledgeId);
    connection.Bind(&characterId);
    constexpr const wchar_t* loadMembers = L"{Call lin_LoadMiniGameagitMember (?)}";
    connection.Prepare(loadMembers);
    connection.BindParameter(&agitId);
    if (connection.ExecutePrepared(loadMembers)) {
        while (connection.Fetch()) {
            memberPacketLength += ::Assemble(
                memberPacket.data() + memberPacketLength, static_cast<int>(memberPacket.size()) - memberPacketLength, "dd",
                memberPledgeId, characterId
            );
            ++memberCount;
        }
    }

    socket->Send(
        "chdddbdb", 0xc6, 0x28, agitId, elapsedTime, pledgeCount, pledgePacketLength, pledgePacket.data(), memberCount,
        memberPacketLength, memberPacket.data()
    );
    return false;
}

bool CMiniGameAgitDB::RequestSetMiniGameAgitStatus(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int agitId = 0;
    int elapsedTime = 0;
    ::Disassemble(packet, "dd", &agitId, &elapsedTime);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetMinigameagitStatus (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&elapsedTime);
    connection.ExecutePrepared(statement);
    return false;
}

bool CMiniGameAgitDB::RequestAddMiniGameAgitPledge(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int agitId = 0;
    int pledgeId = 0;
    int point = 0;
    ::Disassemble(packet, "dddd", &requestContext, &agitId, &pledgeId, &point);

    int result = -2;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_InsertIntoMinigameAgitPledge (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&point);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }

    socket->Send("chddddd", 0xc6, 0x29, result, requestContext, agitId, pledgeId, point);
    return false;
}

bool CMiniGameAgitDB::RequestDelMiniGameAgitPledge(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    // Paired L2Server declares "chdddd" but supplies only these three useful
    // dwords. Current Cached.exe correspondingly disassembles exactly "ddd".
    int requestContext = 0;
    int agitId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "ddd", &requestContext, &agitId, &pledgeId);
    static_cast<void>(requestContext);

    int result = -2;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_DeleteFromMinigameAgitPledge (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&pledgeId);
    if (connection.ExecutePrepared(statement)) {
        result = 0;
    }
    // Log.AddT(LOG_ERROR, L"[MiniGameAgitPledge] ErrorCode [%d]", result)
    // Noise: Successful deletion (result 0) is routine and must not be
    // reported as an error. Keep the original diagnostic above for reference.
    if (result != 0) {
        // Log.Add(LOG_ERROR,
        //     "[residence minigame] Cannot remove clan %d from residence %d in database %d (result %d).",
        //     pledgeId, agitId, runtime::process::CurrentDatabaseId(), result)
        L2_LOG_ERROR(Log,
            "[residence minigame] Cannot remove clan %d from residence %d in database %d (result %d).",
            pledgeId, agitId, runtime::process::CurrentDatabaseId(), result);
    }
    return false;
}

bool CMiniGameAgitDB::RequestAddMiniGameAgitMember(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int agitId = 0;
    int pledgeId = 0;
    int characterId = 0;
    ::Disassemble(packet, "dddd", &requestContext, &agitId, &pledgeId, &characterId);

    int result = -2;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_InsertIntoMinigameAgitMember (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement)) {
        result = 0;
    }

    socket->Send("chddddd", 0xc6, 0x2a, result, requestContext, agitId, pledgeId, characterId);
    return false;
}

bool CMiniGameAgitDB::RequestResetMiniGameAgit(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    int agitId = 0;
    ::Disassemble(packet, "d", &agitId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetMinigameAgit (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    const int result = connection.ExecutePrepared(statement) ? 0 : -1;
    socket->Send("chdd", 0xc6, 0x2b, result, agitId);
    return false;
}

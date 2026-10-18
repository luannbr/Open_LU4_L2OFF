#include "Shared/src/Log.h"

#include "CastleDb.h"

#include "CacheSocket.h"
#include "Cached.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <array>
#include <cstdint>
#include <string>

namespace {

constexpr int kLoadCastleReply = 0x14;
constexpr int kCastlePayloadCapacity = 0x2004;

} // namespace

// Current helper 0x0049fed8 has no guard literal or surviving source symbol.
// Its query ABI and the paired current L2Server decoder establish the three
// values as residence slot, deco-NPC ID and expiration time, in that order.
int AppendAgitDecoNpcs(char* output, int outputCapacity, std::uint32_t residenceId, int& npcCount) {
    int slot = 0;
    int decoNpcId = 0;
    int expireTime = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&slot);
    connection.Bind(&decoNpcId);
    connection.Bind(&expireTime);

    int assembledLength = 0;
    npcCount = 0;
    constexpr const wchar_t* statement = L"{Call lin_AgitDecoNpcLoad (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&residenceId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            assembledLength +=
                ::Assemble(output + assembledLength, outputCapacity - assembledLength, "ddd", slot, decoNpcId, expireTime);
            ++npcCount;
        }
    } else {
        // Log.Add(LOG_ERROR,
        //     "[residence] Cannot query decoration NPCs for residence %u in database %d. No NPC rows appended to the reply.",
        //     residenceId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[residence] Cannot query decoration NPCs for residence %u in database %d; no NPC rows appended to the reply.",
            residenceId, runtime::process::CurrentDatabaseId());
    }
    return assembledLength;
}

CCastleDb g_castleDb;

CCastleDb::CCastleDb() = default;
CCastleDb::~CCastleDb() = default;

void CCastleDb::Add(CCastle* castle) {
    L2_GUARD_FUNCTION();
    if (castle) {
        m_castles.Insert(castle->GetId(), castle);
    }
}

CCastleSP CCastleDb::LoadFromDb(int castleId) {
    L2_GUARD_FUNCTION();
    CCastle* const castle = new CCastle(castleId);
    CCastleSP result(castle, ObjectRefType::FunctionChain);
    Add(result.get());
    L2_RELEASE(castle, ObjectRefType::CreateDelete);
    return result;
}

CCastleSP CCastleDb::GetCastle(int castleId) {
    L2_GUARD_FUNCTION();
    CCastleSP castle = m_castles.FindObject(castleId);
    if (!castle)
        castle = LoadFromDb(castleId);
    if (!castle) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[CastleDb.cpp][%d] undefined (or unregistered) "
        //     "castle Id[%d]",
        //     0x2b8, castleId
        // )
        // Log.Add(LOG_ERROR,
        //     "[castle] Castle %d is unavailable after cache lookup and object creation. No castle returned.",
        //     castleId)
        L2_LOG_ERROR(Log,
            "[castle] Castle %d is unavailable after cache lookup and object creation; no castle returned.",
            castleId);
        runtime::process::ReportNamedCallStack(L"undefined castle", 2, false);
    }
    return castle;
}

void CCastleDb::ItemCheckOnAllCastle() {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity)
        return;

    struct CheckCastle {
        bool Do(CCastle* castle) {
            castle->ItemCheckOnUnload();
            return true;
        }
    } check;
    m_castles.FOR_ALL(&check, L"CastleDb.cpp", 0x2f3);
}

void CCastleDb::SendReplyToLoadCastle(CSocket* socket, std::uint32_t castleId, const wchar_t* castleName) {
    L2_GUARD_FUNCTION();

    int payloadLength = 0;
    int warCount = 0;
    int decoCount = 0;
    std::array<char, kCastlePayloadCapacity> payload{};

    std::uint32_t pledgeId = 0;
    std::uint32_t worldId = 0;
    std::uint32_t nextWarTime = 0;
    std::uint32_t taxRate = 0;
    std::uint32_t taxChildRate = 0;
    std::uint32_t status = 0;
    std::array<char16_t, 256> loadedCastleName{};
    int cropIncome = 0;
    long long shopIncome = 0;
    std::uint32_t siegeElapsedTime = 0;
    long long shopIncomeTemp = 0;
    int taxRateToChange = 0;
    std::uint32_t taxChildRateToChange = 0;
    std::uint32_t previousCastleOwnerId = 0;
    std::uint32_t oldWorldId = 0;
    int castleType = 0;
    int campaignSuccess = 0;
    std::int64_t previousOwnerIndicator = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    connection.Bind(&worldId);
    connection.Bind(&nextWarTime);
    connection.Bind(&taxRate);
    connection.Bind(&taxChildRate);
    connection.Bind(&status);
    connection.Bind(loadedCastleName.data(), 0x200);
    connection.Bind(&cropIncome);
    connection.Bind(&shopIncome);
    connection.Bind(&siegeElapsedTime);
    connection.Bind(&shopIncomeTemp);
    connection.Bind(&taxRateToChange);
    connection.Bind(&taxChildRateToChange);
    connection.BindNullable(&previousCastleOwnerId, &previousOwnerIndicator);
    connection.Bind(&oldWorldId);
    connection.Bind(&castleType);
    connection.Bind(&campaignSuccess);

    constexpr const wchar_t* loadStatement = L"{Call lin_LoadCastle (?, ?)}";
    int loadType = 1;
    connection.Prepare(loadStatement);
    connection.BindParameter(&castleId);
    connection.BindParameter(&loadType);
    bool loaded = connection.ExecutePrepared(loadStatement) && connection.Fetch();

    if (!loaded) {
        connection.UnBind();
        constexpr const wchar_t* createStatement = L"{Call lin_CreateCastle (?, ?)}";
        connection.Prepare(createStatement);
        connection.BindParameter(&castleId);
        connection.BindParameter(castleName);
        connection.ExecutePrepared(createStatement);

        connection.UnBind();
        connection.Bind(&pledgeId);
        connection.Bind(&worldId);
        connection.Bind(&nextWarTime);
        connection.Bind(&taxRate);
        connection.Bind(&taxChildRate);
        connection.Bind(&status);
        connection.Bind(loadedCastleName.data(), 0x200);
        connection.Bind(&cropIncome);
        connection.Bind(&shopIncome);
        connection.Bind(&siegeElapsedTime);
        connection.Bind(&shopIncomeTemp);
        connection.Bind(&taxRateToChange);
        connection.Bind(&taxChildRateToChange);
        connection.BindNullable(&previousCastleOwnerId, &previousOwnerIndicator);
        connection.Bind(&oldWorldId);
        connection.Bind(&castleType);
        connection.Bind(&campaignSuccess);

        connection.Prepare(loadStatement);
        connection.BindParameter(&castleId);
        connection.BindParameter(&loadType);
        loaded = connection.ExecutePrepared(loadStatement) && connection.Fetch();
        if (!loaded) {
            // Log.Add(LOG_ERROR,
            //     "[castle] Castle %u could not be loaded from database %d after a creation attempt. Sending an empty castle reply.",
            //     castleId, runtime::process::CurrentDatabaseId())
            L2_LOG_ERROR(Log,
                "[castle] Castle %u could not be loaded from database %d after a creation attempt; sending an empty castle reply.",
                castleId, runtime::process::CurrentDatabaseId());
            socket->Send(
                "cdddddddSddQdQddddddd", kLoadCastleReply, 0, 0, 0, 0, 0, 0, 0, u"", 0, 0, static_cast<std::int64_t>(0), 0,
                static_cast<std::int64_t>(0), 0, 0, 0, 0, 0, 0, 0
            );
            return;
        }
    }

    connection.UnBind();
    int warWorldId = 0;
    std::uint32_t warPledgeId = 0;
    std::uint32_t warType = 0;
    std::uint32_t proposeTime = 0;
    std::int64_t nullableIndicator = -1;
    connection.Bind(&warWorldId);
    connection.Bind(&warPledgeId);
    connection.Bind(&warType);
    connection.BindNullable(&proposeTime, &nullableIndicator);

    constexpr const wchar_t* warStatement = L"{Call lin_LoadCastleWarRelatedPledge (?)}";
    connection.Prepare(warStatement);
    connection.BindParameter(&castleId);
    if (connection.ExecutePrepared(warStatement)) {
        while (connection.Fetch()) {
            payloadLength += ::Assemble(
                payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "dddd", warWorldId,
                static_cast<std::int32_t>(warPledgeId), static_cast<std::int32_t>(warType),
                static_cast<std::int32_t>(proposeTime)
            );
            ++warCount;
        }
    } else {
        // Log.Add(LOG_ERROR,
        //     "[castle siege] Cannot query participating clans for castle %u in database %d. Castle loading continues without siege registration rows.",
        //     castleId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[castle siege] Cannot query participating clans for castle %u in database %d; castle loading continues without siege registration rows.",
            castleId, runtime::process::CurrentDatabaseId());
    }

    connection.UnBind();
    int decoType = 0;
    int decoId = 0;
    int decoLevel = 0;
    int decoExpire = 0;
    int decoActive = 0;
    connection.Bind(&decoType);
    connection.Bind(&decoId);
    connection.BindNullable(&decoLevel, &nullableIndicator);
    connection.BindNullable(&decoExpire, &nullableIndicator);
    connection.BindNullable(&decoActive, &nullableIndicator);

    constexpr const wchar_t* decoStatement = L"{Call lin_LoadAgitDeco (?)}";
    connection.Prepare(decoStatement);
    connection.BindParameter(&castleId);
    if (connection.ExecutePrepared(decoStatement)) {
        while (connection.Fetch()) {
            if (decoActive == 1) {
                payloadLength += ::Assemble(
                    payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "dddd", decoType,
                    decoId, decoLevel, decoExpire
                );
                ++decoCount;
            }
        }
    } else {
        // Log.Add(LOG_ERROR,
        //     "[residence] Cannot query decorations for castle %u in database %d. Castle loading continues without decoration rows.",
        //     castleId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[residence] Cannot query decorations for castle %u in database %d; castle loading continues without decoration rows.",
            castleId, runtime::process::CurrentDatabaseId());
    }

    if ((pledgeId != 0 && worldId == 0) || (previousCastleOwnerId != 0 && oldWorldId == 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[Wrong PledgeWorldID %d] "
        //     "[previousCastleOwnerWorldID %d]",
        //     static_cast<int>(worldId), static_cast<int>(oldWorldId)
        // )
        // Log.Add(LOG_ERROR,
        //     "[castle] Load reply for castle %u not sent: an owner clan has world ID zero (current clan %u, world %u. Previous clan %u, world %u).",
        //     castleId, pledgeId, worldId, previousCastleOwnerId, oldWorldId)
        L2_LOG_ERROR(Log,
            "[castle] Load reply for castle %u not sent: an owner clan has world ID zero (current clan %u, world %u; previous clan %u, world %u).",
            castleId, pledgeId, worldId, previousCastleOwnerId, oldWorldId);
        return;
    }

    int npcCount = 0;
    payloadLength += AppendAgitDecoNpcs(
        payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, castleId, npcCount
    );

    // The current method binds crop_income as result column eight but does
    // not place it in either current opcode-0x14 reply shape.

    if (warCount < 1 && decoCount < 1 && npcCount < 1) {
        socket->Send(
            "cdddddddSddQdQddddddd", kLoadCastleReply, static_cast<std::int32_t>(castleId),
            static_cast<std::int32_t>(pledgeId), static_cast<std::int32_t>(worldId), static_cast<std::int32_t>(nextWarTime),
            static_cast<std::int32_t>(taxRate), static_cast<std::int32_t>(taxChildRate), static_cast<std::int32_t>(status),
            loadedCastleName.data(), castleType, campaignSuccess, static_cast<std::int64_t>(shopIncome),
            static_cast<std::int32_t>(siegeElapsedTime), static_cast<std::int64_t>(shopIncomeTemp), taxRateToChange,
            static_cast<std::int32_t>(previousCastleOwnerId), static_cast<std::int32_t>(oldWorldId),
            static_cast<std::int32_t>(taxChildRateToChange), warCount, decoCount, npcCount
        );
    } else {
        socket->Send(
            "cdddddddSddQdQdddddddb", kLoadCastleReply, static_cast<std::int32_t>(castleId),
            static_cast<std::int32_t>(pledgeId), static_cast<std::int32_t>(worldId), static_cast<std::int32_t>(nextWarTime),
            static_cast<std::int32_t>(taxRate), static_cast<std::int32_t>(taxChildRate), static_cast<std::int32_t>(status),
            loadedCastleName.data(), castleType, campaignSuccess, static_cast<std::int64_t>(shopIncome),
            static_cast<std::int32_t>(siegeElapsedTime), static_cast<std::int64_t>(shopIncomeTemp), taxRateToChange,
            static_cast<std::int32_t>(previousCastleOwnerId), static_cast<std::int32_t>(oldWorldId),
            static_cast<std::int32_t>(taxChildRateToChange), warCount, decoCount, npcCount, payloadLength, payload.data()
        );
    }
}

void CCastleDb::SendReplyToViewSiegeList(
    CSocket* socket, std::uint32_t userServerId, std::uint32_t castleId, const wchar_t* returnHtml
) {
    // Current Cached.exe 0x0049f5e8.
    L2_GUARD_FUNCTION();

    std::uint32_t pledgeId = 0;
    std::array<char16_t, 24> pledgeName{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    connection.Bind(pledgeName.data(), 0x30);

    constexpr const wchar_t* statement = L"{Call lin_ViewSiegeList (?, ?)}";
    int registrationType = 1;
    connection.Prepare(statement);
    connection.BindParameter(&castleId);
    connection.BindParameter(&registrationType);

    const std::u16string targetReturnHtml = WideToUtf16(returnHtml);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[castle siege] Cannot query siege registrations for castle %u in database %d. Sending an empty list to user %u.",
        //     castleId, runtime::process::CurrentDatabaseId(), userServerId)
        L2_LOG_ERROR(Log,
            "[castle siege] Cannot query siege registrations for castle %u in database %d; sending an empty list to user %u.",
            castleId, runtime::process::CurrentDatabaseId(), userServerId);
        socket->Send(
            "cddSd", 0x43, static_cast<std::int32_t>(userServerId), static_cast<std::int32_t>(castleId),
            targetReturnHtml.c_str(), 0
        );
        return;
    }

    std::array<char, kCastlePayloadCapacity> payload{};
    int payloadLength = 0;
    int pledgeCount = 0;
    while (connection.Fetch()) {
        payloadLength += ::Assemble(
            payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "dS",
            static_cast<std::int32_t>(pledgeId), pledgeName.data()
        );
        ++pledgeCount;
    }
    socket->Send(
        "cddSdb", 0x43, static_cast<std::int32_t>(userServerId), static_cast<std::int32_t>(castleId),
        targetReturnHtml.c_str(), pledgeCount, payloadLength, payload.data()
    );
}

void CCastleDb::SendReplyToApproveBattle(
    CSocket* socket, std::uint32_t userServerId, std::uint32_t castleId, const wchar_t* returnHtml
) {
    // Current Cached.exe 0x0049f834.
    L2_GUARD_FUNCTION();

    std::uint32_t pledgeId = 0;
    std::array<char16_t, 24> pledgeName{};
    std::uint32_t registrationTypeResult = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    connection.Bind(pledgeName.data(), 0x30);
    connection.Bind(&registrationTypeResult);

    constexpr const wchar_t* statement = L"{Call lin_ApproveBattle (?, ?)}";
    int registrationType = 1;
    connection.Prepare(statement);
    connection.BindParameter(&castleId);
    connection.BindParameter(&registrationType);

    const std::u16string targetReturnHtml = WideToUtf16(returnHtml);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[castle siege] Cannot query battle approval data for castle %u in database %d. Sending an empty list to user %u.",
        //     castleId, runtime::process::CurrentDatabaseId(), userServerId)
        L2_LOG_ERROR(Log,
            "[castle siege] Cannot query battle approval data for castle %u in database %d; sending an empty list to user %u.",
            castleId, runtime::process::CurrentDatabaseId(), userServerId);
        socket->Send(
            "cddSd", 0x44, static_cast<std::int32_t>(userServerId), static_cast<std::int32_t>(castleId),
            targetReturnHtml.c_str(), 0
        );
        return;
    }

    std::array<char, kCastlePayloadCapacity> payload{};
    int payloadLength = 0;
    int pledgeCount = 0;
    while (connection.Fetch()) {
        payloadLength += ::Assemble(
            payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "dSd",
            static_cast<std::int32_t>(pledgeId), pledgeName.data(), static_cast<std::int32_t>(registrationTypeResult)
        );
        ++pledgeCount;
    }
    socket->Send(
        "cddSdb", 0x44, static_cast<std::int32_t>(userServerId), static_cast<std::int32_t>(castleId),
        targetReturnHtml.c_str(), pledgeCount, payloadLength, payload.data()
    );
}

int CCastleDb::GetRelatedCastleSiege(
    CPacketHelperEx<20480>& packet, std::uint32_t castleId, std::uint32_t pledgeId, int worldId
) {
    // Current Cached.exe 0x0049fa98.
    L2_GUARD_FUNCTION();

    if (worldId < 1) {
        worldId = GetCacheDConfiguration().worldId;
    }

    std::uint32_t relatedCastleId = 0;
    std::uint32_t nextWarTime = 0;
    std::uint32_t registrationType = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&relatedCastleId);
    connection.Bind(&nextWarTime);
    connection.Bind(&registrationType);

    if (castleId == 0) {
        constexpr auto statement = L"{Call lin_GetAllCastleSiege (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&worldId);
        connection.BindParameter(&pledgeId);
        if (!connection.ExecutePrepared(statement))
            return -1;
    } else {
        constexpr auto statement = L"{Call lin_GetRelatedCastleSiege (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&castleId);
        connection.BindParameter(&pledgeId);
        // The shipping method declares three placeholders but binds only
        // these two values. Preserve that current-image execution defect.
        if (!connection.ExecutePrepared(statement))
            return -1;
    }

    while (connection.Fetch()) {
        packet.Assemble("dd", static_cast<int>(relatedCastleId), static_cast<int>(nextWarTime));
    }
    return packet.GetAssembledCount();
}

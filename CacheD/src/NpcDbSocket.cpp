#include "NpcDbSocket.h"
#include "Shared/src/MemorySub.h"

#include "Cached.h"
#include "NpcServer.h"
#include "UserDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

#if defined(_MSC_VER)
#include <sys/timeb.h>
#endif

#if defined(_WIN32)
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#endif

void CNPCPacket::OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) {
    // Current Cached.exe virtual body 0x004e63a0 copies configured
    // DAT_0083d29c into request TLS +0x10, then tail-calls the shared
    // CPacketEx callback at 0x004814c4.
    runtime::process::SetCurrentDatabaseId(runtime::process::ConfiguredDatabaseId());
    NpcDbCPacketEx::OnIOCallback(success, transferred, overlapped);
}

namespace {


// Current Cached.exe 0x004fb1f8. The source name did not survive, but the
// ten integer bindings followed by one bigint binding exactly match both
// lin_CreateLottoGame and lin_UpdateLottoGame.
bool ExecuteLottoGameWrite(
    DBConn& connection,
    const wchar_t* statement,
    int roundNumber,
    int state,
    int leftTime,
    int chosenNumberFlag,
    int ruleNumber,
    int totalCount,
    int winner1Count,
    int winner2Count,
    int winner3Count,
    int winner4Count,
    std::int64_t carriedAdena
) {
    connection.Prepare(statement);
    connection.BindParameter(&roundNumber);
    connection.BindParameter(&state);
    connection.BindParameter(&leftTime);
    connection.BindParameter(&chosenNumberFlag);
    connection.BindParameter(&ruleNumber);
    connection.BindParameter(&totalCount);
    connection.BindParameter(&winner1Count);
    connection.BindParameter(&winner2Count);
    connection.BindParameter(&winner3Count);
    connection.BindParameter(&winner4Count);
    connection.BindParameter(&carriedAdena);
    return connection.ExecutePrepared(statement);
}

std::int64_t CurrentLocalEpochSeconds() {
    // Current 0x004fa87d uses _ftime64_s and subtracts timeb.timezone * 60.
    // it ignores timeb.dstflag. POSIX `timezone` is the same
    // seconds-west-of-UTC standard-time value.
#if defined(_MSC_VER)
    __timeb64 current{};
    ::_ftime64_s(&current);
    return current.time - static_cast<std::int64_t>(current.timezone) * 60;
#else
    const std::time_t current = std::time(nullptr);
    ::tzset();
    return static_cast<std::int64_t>(current) - static_cast<std::int64_t>(::timezone);
#endif
}

// Current Cached.exe 0x004fb2c0. The usable PDB does not keep this
// private helper's source name. Its complete parameter order is fixed by the
// twelve BindParameter calls and the lin_SaveNpcBoss call text.
bool SaveNpcBoss(
    DBConn& connection,
    const wchar_t* npcDbName,
    int alive,
    int hp,
    int mp,
    int posX,
    int posY,
    int posZ,
    int deathTime,
    int timeHigh,
    int npcId,
    const wchar_t* killerMpccMasterName,
    int killerMpccMemberCount
) {
    constexpr const wchar_t* statement = L"{Call lin_SaveNpcBoss (?,?,?,?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(npcDbName);
    connection.BindParameter(&alive);
    connection.BindParameter(&hp);
    connection.BindParameter(&mp);
    connection.BindParameter(&posX);
    connection.BindParameter(&posY);
    connection.BindParameter(&posZ);
    connection.BindParameter(&deathTime);
    connection.BindParameter(&timeHigh);
    connection.BindParameter(&npcId);
    connection.BindParameter(killerMpccMasterName);
    connection.BindParameter(&killerMpccMemberCount);
    return connection.ExecutePrepared(statement);
}

// Current Cached.exe 0x004f9ba4: the default ND_PROTOCOL callback performs
// only guard bookkeeping and returns false without consuming the packet.
bool DummyNpcDbPacket(CNpcDbSocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    return false;
}

// Current Cached.exe 0x004f9c24.
bool NpcDbVersion(CNpcDbSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int npcRevision = 0;
    ::Disassemble(packet, "d", &npcRevision);

    char revisionLabel[80]{};
    int cacheRevision = 0;
    std::sscanf("$Revision: #14 $", "%s #%d", revisionLabel, &cacheRevision);
    if (npcRevision != cacheRevision) {
        // Log.Add(LOG_ERROR, L"Invalid protocol [Npc:%d] [Cache:%d]..... disconnect", npcRevision, cacheRevision)
        // Log.Add(LOG_WARN,
        //     "[L2NPC] Protocol revision mismatch: peer %d, required %d. Closing the connection.",
        //     npcRevision, cacheRevision)
        L2_LOG_WARN(Log,
            "[L2NPC] Protocol revision mismatch: peer %d, required %d; closing the connection.",
            npcRevision, cacheRevision);
    }
    return npcRevision != cacheRevision;
}

// Current Cached.exe 0x004f9d54.
bool LoadLottoInfo(CNpcDbSocket* socket, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_LoadLottoGame}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, L"Load Lotto Info failed")
        // Log.Add(LOG_ERROR,
        //     "[lottery] Cannot load rounds from database %d. No round data or completion reply will be sent to L2NPC.",
        //     runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[lottery] Cannot load rounds from database %d; no round data or completion reply will be sent to L2NPC.",
            runtime::process::CurrentDatabaseId());
        return false;
    }

    int roundNumber = 0;
    int state = 0;
    int leftTime = 0;
    int chosenNumberFlag = 0;
    int ruleNumber = 0;
    int totalCount = 0;
    int winner1Count = 0;
    int winner2Count = 0;
    int winner3Count = 0;
    int winner4Count = 0;
    std::int64_t carriedAdena = 0;
    // The target binds results only after ExecutePrepared succeeds.
    connection.Bind(&roundNumber);
    connection.Bind(&state);
    connection.Bind(&leftTime);
    connection.Bind(&chosenNumberFlag);
    connection.Bind(&ruleNumber);
    connection.Bind(&totalCount);
    connection.Bind(&winner1Count);
    connection.Bind(&winner2Count);
    connection.Bind(&winner3Count);
    connection.Bind(&winner4Count);
    connection.Bind(&carriedAdena);

    int loadedRoundCount = 0;
    while (connection.Fetch()) {
        socket->Send(
            "cddddddddddddQ", 1, loadedRoundCount, 1, roundNumber, state, leftTime, chosenNumberFlag, ruleNumber, totalCount,
            winner1Count, winner2Count, winner3Count, winner4Count, static_cast<long long>(carriedAdena)
        );
        ++loadedRoundCount;
    }
    socket->Send("cdd", 1, loadedRoundCount, 0);
    return false;
}

// Current Cached.exe 0x004f9fc0 is a shipping decode-only stub.
bool LottoSellTicket(CNpcDbSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int first = 0;
    int second = 0;
    ::Disassemble(packet, "dd", &first, &second);
    return false;
}

// Current Cached.exe 0x004fb64c.
bool LottoDrawing(CNpcDbSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int roundNumber = 0;
    int serverTicketCount = 0;
    std::int64_t carriedAdena = 0;
    int chosenNumberFlag = 0;
    int leftTime = 0;
    std::int64_t presetReward = 0;
    ::Disassemble(
        packet, "ddQddQ", &roundNumber, &serverTicketCount, &carriedAdena, &chosenNumberFlag, &leftTime, &presetReward
    );
    // The second bigint is decoded by the target but has no consumer.

    DBConn loadConnection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* loadStatement = L"{Call lin_LoadLottoItems (?)}";
    loadConnection.Prepare(loadStatement);
    loadConnection.BindParameter(&roundNumber);
    if (!loadConnection.ExecutePrepared(loadStatement)) {
        // Log.Add(LOG_ERROR, L"[%s][%d] LottoDrawing failed", L"NpcDbSocket.cpp", 0x136)
        // Log.Add(LOG_ERROR,
        //     "[lottery] Cannot load tickets for round %d in database %d. Drawing stops without a winners reply.",
        //     roundNumber, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[lottery] Cannot load tickets for round %d in database %d; drawing stops without a winners reply.",
            roundNumber, runtime::process::CurrentDatabaseId());
        return false;
    }

    std::int64_t itemId = 0;
    std::uint32_t numberFlag = 0;
    loadConnection.Bind(&itemId);
    loadConnection.Bind(&numberFlag);
    int winner1Count = 0;
    int winner2Count = 0;
    int winner3Count = 0;
    int winner4Count = 0;
    int cachedTicketCount = 0;
    while (loadConnection.Fetch()) {
        int matchingNumbers = 0;
        const std::uint32_t intersection = numberFlag & static_cast<std::uint32_t>(chosenNumberFlag);
        for (unsigned bit = 0; bit < 20; ++bit) {
            if (((intersection >> bit) & 1U) != 0U) {
                ++matchingNumbers;
            }
        }
        if (matchingNumbers == 5) {
            ++winner1Count;
        } else if (matchingNumbers == 4) {
            ++winner2Count;
        } else if (matchingNumbers == 3) {
            ++winner3Count;
        } else if (matchingNumbers < 3) {
            ++winner4Count;
        }
        ++cachedTicketCount;
    }

    if (cachedTicketCount != serverTicketCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] lotto ticket count different between "
        //     L"cached[%d] and server[%d]",
        //     L"NpcDbSocket.cpp", 0x157, cachedTicketCount, serverTicketCount
        // )
        // Log.Add(LOG_ERROR,
        //     "[lottery] Ticket count mismatch for round %d: database has %d, L2NPC reported %d. Winner counts use database tickets. The round total still uses the L2NPC count.",
        //     roundNumber, cachedTicketCount, serverTicketCount)
        L2_LOG_ERROR(Log,
            "[lottery] Ticket count mismatch for round %d: database has %d, L2NPC reported %d. Winner counts use database tickets; the round total still uses the L2NPC count.",
            roundNumber, cachedTicketCount, serverTicketCount);
    }

    DBConn saveConnection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* saveStatement = L"{Call lin_UpdateLottoGame (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    // ExecuteLottoGameWrite(
    //     saveConnection, saveStatement, roundNumber, 2, leftTime, chosenNumberFlag, 0, serverTicketCount, winner1Count,
    //     winner2Count, winner3Count, winner4Count, carriedAdena
    // )
    if (!ExecuteLottoGameWrite(
        saveConnection, saveStatement, roundNumber, 2, leftTime, chosenNumberFlag, 0, serverTicketCount, winner1Count,
        winner2Count, winner3Count, winner4Count, carriedAdena
    )) {
        // Log.Add(LOG_ERROR,
        //     "[lottery] Cannot save drawing results for round %d in database %d. The winners reply still reports success.",
        //     roundNumber, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[lottery] Cannot save drawing results for round %d in database %d; the winners reply still reports success.",
            roundNumber, runtime::process::CurrentDatabaseId());
    }
    g_NpcServer.Send("cdddddd", 2, 1, roundNumber, winner1Count, winner2Count, winner3Count, winner4Count);
    return false;
}

// Current Cached.exe 0x004fb958.
bool LottoStartNextRound(CNpcDbSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int roundNumber = 0;
    int leftTime = 0;
    std::int64_t carriedAdena = 0;
    ::Disassemble(packet, "ddQ", &roundNumber, &leftTime, &carriedAdena);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_CreateLottoGame (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    if (!ExecuteLottoGameWrite(connection, statement, roundNumber, 1, leftTime, 0, 0, 0, 0, 0, 0, 0, carriedAdena)) {
        // Log.Add(LOG_ERROR, L"[%s][%d] LottoStartNextRound failed", L"NpcDbSocket.cpp", 0x189)
        // Log.Add(LOG_ERROR,
        //     "[lottery] Cannot create round %d in database %d (time remaining %d, carried adena %lld).",
        //     roundNumber, runtime::process::CurrentDatabaseId(), leftTime, static_cast<long long>(carriedAdena))
        L2_LOG_ERROR(Log,
            "[lottery] Cannot create round %d in database %d (time remaining %d, carried adena %lld).",
            roundNumber, runtime::process::CurrentDatabaseId(), leftTime, static_cast<long long>(carriedAdena));
    }
    return false;
}

// Current Cached.exe 0x004fbaf8.
bool LottoSaveRound(CNpcDbSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int roundNumber = 0;
    int state = 0;
    int leftTime = 0;
    int chosenNumberFlag = 0;
    int ruleNumber = 0;
    int totalCount = 0;
    int winner1Count = 0;
    int winner2Count = 0;
    int winner3Count = 0;
    int winner4Count = 0;
    std::int64_t carriedAdena = 0;
    ::Disassemble(
        packet, "ddddddddddQ", &roundNumber, &state, &leftTime, &chosenNumberFlag, &ruleNumber, &totalCount, &winner1Count,
        &winner2Count, &winner3Count, &winner4Count, &carriedAdena
    );
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateLottoGame (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    if (!ExecuteLottoGameWrite(
            connection, statement, roundNumber, state, leftTime, chosenNumberFlag, ruleNumber, totalCount, winner1Count,
            winner2Count, winner3Count, winner4Count, carriedAdena
        )) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] LottoSaveRound failed. "
        //     L"[%d][%d][%d][%d][%d][%d][%d][%d][%d][%d][%I64d]",
        //     L"NpcDbSocket.cpp", 0x1cb, roundNumber, state, leftTime, chosenNumberFlag, ruleNumber, totalCount, winner1Count,
        //     winner2Count, winner3Count, winner4Count, static_cast<long long>(carriedAdena)
        // )
        // Log.Add(LOG_ERROR,
        //     "[lottery] Cannot save round %d in database %d: state %d, time remaining %d, chosen-number mask %d, rule %d, tickets %d, winners by tier [%d, %d, %d, %d], carried adena %lld.",
        //     roundNumber, runtime::process::CurrentDatabaseId(), state, leftTime, chosenNumberFlag, ruleNumber, totalCount, winner1Count, winner2Count, winner3Count, winner4Count, static_cast<long long>(carriedAdena))
        L2_LOG_ERROR(Log,
            "[lottery] Cannot save round %d in database %d: state %d, time remaining %d, chosen-number mask %d, rule %d, tickets %d, winners by tier [%d, %d, %d, %d], carried adena %lld.",
            roundNumber, runtime::process::CurrentDatabaseId(), state, leftTime, chosenNumberFlag, ruleNumber, totalCount, winner1Count, winner2Count, winner3Count, winner4Count, static_cast<long long>(carriedAdena));
    }
    return false;
}

// Current Cached.exe 0x004fd764. The name is confirmed by the current guard
// literal and by the matching L2NPC sender.
bool LoadNpcRequest(CNpcDbSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 128> npcDbName{};
    std::int64_t spawnDefinition = 0;
    ::Disassemble(packet, "Sp", 0x100, npcDbName.data(), &spawnDefinition);
    const std::wstring hostName = Utf16ToWide(npcDbName.data());
    BossInfoSP info = Singleton<BossInfoDB>::inst().GetInfo(hostName.c_str());
    if (!info) {
        // Noise: BossInfoDB::GetInfo already reports the failed load/create boundary with the NPC name. This duplicate precedes the unchanged empty-state reply.
        // Log.Add(
        //     LOG_ERROR, L"[%s][%d] No record in table, npc dbname[%s]", L"NpcDbSocket.cpp", 0x4d, hostName.c_str()
        // )
        socket->Send("cSpcdd", 0, npcDbName.data(), static_cast<long long>(spawnDefinition), 0, 0, 0);
        return false;
    }
    if (info->m_alive == 0) {
        socket->Send("cSpcdd", 0, npcDbName.data(), static_cast<long long>(spawnDefinition), 0, info->m_deathTime, 0);
        return false;
    }
    socket->Send(
        "cSpcdddddd", 0, npcDbName.data(), static_cast<long long>(spawnDefinition), info->m_alive, info->m_dbValue,
        info->m_hp, info->m_mp, info->m_posX, info->m_posY, info->m_posZ
    );
    return false;
}

// Current Cached.exe 0x004fd994.
bool SaveNpcInfo(CNpcDbSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 128> npcDbName{};
    char alive = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "Sc", 0x100, npcDbName.data(), &alive);

    int hp = 0;
    int mp = 0;
    int posX = 0;
    int posY = 0;
    int posZ = 0;
    int deathTime = 0;
    int npcId = 0;
    std::array<char16_t, 25> killerMpccMasterName{};
    int killerMpccMemberCount = 0;
    if (alive == 0) {
        ::Disassemble(cursor, "dSd", &deathTime, 0x32, killerMpccMasterName.data(), &killerMpccMemberCount);
    } else {
        ::Disassemble(cursor, "dddddd", &hp, &mp, &posX, &posY, &posZ, &npcId);
    }

    const std::wstring hostName = Utf16ToWide(npcDbName.data());
    const BossInfoSP info = Singleton<BossInfoDB>::inst().GetInfo(hostName.c_str());
    if (!info)
        return false;

    L2_GUARD(info->m_lock);
    info->m_alive = static_cast<int>(alive);
    info->m_hp = hp;
    info->m_mp = mp;
    info->m_posX = posX;
    info->m_posY = posY;
    info->m_posZ = posZ;
    info->m_deathTime = deathTime;
    info->m_npcId = npcId;
    if (alive == 0) {
        info->m_killerMpccMasterName.assign(std::u16string_view(killerMpccMasterName.data()));
        info->m_killerMpccMemberCount = killerMpccMemberCount;
    }
    info->Save();
    return false;
}

// Current Cached.exe 0x004fde78.
bool UpdateBossNpcValue(CNpcDbSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 128> npcDbName{};
    int dbValue = 0;
    ::Disassemble(packet, "Sd", 0x100, npcDbName.data(), &dbValue);
    const std::wstring hostName = Utf16ToWide(npcDbName.data());
    BossInfoSP info = Singleton<BossInfoDB>::inst().GetInfo(hostName.c_str());
    if (!info)
        return false;

    L2_GUARD(info->m_lock);
    info->UpdateDBValue(dbValue);
    return false;
}

// Current Cached.exe 0x004fe05c. The private source name is recovered from
// its current guard literal.
bool LoadNpcRequestEx(CNpcDbSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 128> npcDbName{};
    std::int64_t spawnDefinition = 0;
    int argument = 0;
    ::Disassemble(packet, "Spd", 0x100, npcDbName.data(), &spawnDefinition, &argument);
    const std::wstring hostName = Utf16ToWide(npcDbName.data());
    BossInfoSP info = Singleton<BossInfoDB>::inst().GetInfo(hostName.c_str());
    if (!info) {
        // Noise: BossInfoDB::GetInfo already reports the failed load/create boundary with the NPC name. This duplicate precedes the unchanged empty-state reply.
        // Log.Add(
        //     LOG_ERROR, L"[%s][%d] No record in table, npc dbname[%s]", L"NpcDbSocket.cpp", 0x1fc, hostName.c_str()
        // )
        socket->Send("cSpdcdd", 3, npcDbName.data(), static_cast<long long>(spawnDefinition), argument, 0, 0, 0);
        return false;
    }
    if (info->m_alive == 0) {
        socket->Send(
            "cSpdcdd", 3, npcDbName.data(), static_cast<long long>(spawnDefinition), argument, 0, info->m_deathTime, 0
        );
        return false;
    }
    socket->Send(
        "cSpdcdddddd", 3, npcDbName.data(), static_cast<long long>(spawnDefinition), argument, info->m_alive,
        info->m_dbValue, info->m_hp, info->m_mp, info->m_posX, info->m_posY, info->m_posZ
    );
    return false;
}

} // namespace

// Current Cached.exe 0x004fa778. This callback is global in the target (not a
// private static helper), and directly reads CUserDb's +0x2c00f0 period.
bool GiveEventData(CNpcDbSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int characterId = 0;
    int creatureIndex = 0;
    int npcObjectId = 0;
    int eventId = 0;
    ::Disassemble(packet, "dddd", &characterId, &creatureIndex, &npcObjectId, &eventId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    const int period = g_userDb.m_eventDataPeriod;
    const std::int64_t currentLocalTime = CurrentLocalEpochSeconds();
    const int remainder = static_cast<int>(currentLocalTime % static_cast<std::int64_t>(period));
    const int periodStart = static_cast<int>(currentLocalTime) - remainder;
    int canGive = 0;
    if (user->GetEventData(eventId) < periodStart) {
        canGive = 1;
        user->SetEventData(eventId, static_cast<int>(currentLocalTime));
    }
    socket->Send("cdddddd", 6, characterId, creatureIndex, npcObjectId, canGive, eventId, period - remainder);
    return false;
}

// Current Cached.exe 0x004fbd28.
bool SaveDBSavingMap(CNpcDbSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int key = 0;
    int value = 0;
    ::Disassemble(packet, "dd", &key, &value);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveDBSavingMap(?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&key);
    connection.BindParameter(&value);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, L"[%s][%d] Save DBSavingMap failed. (%d:%d)", L"NpcDbSocket.cpp", 0x213, key, value)
        // Log.Add(LOG_ERROR,
        //     "[NPC state] Cannot save key %d with value %d in database %d.",
        //     key, value, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[NPC state] Cannot save key %d with value %d in database %d.",
            key, value, runtime::process::CurrentDatabaseId());
    }
    return false;
}

// Current Cached.exe 0x004fbecc.
bool DeleteDBSavingMap(CNpcDbSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int key = 0;
    ::Disassemble(packet, "d", &key);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteDBSavingMap(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&key);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, L"[%s][%d] Delete DBSavingMap failed. (%d)", L"NpcDbSocket.cpp", 0x227, key)
        // Log.Add(LOG_ERROR,
        //     "[NPC state] Cannot delete key %d from database %d.",
        //     key, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[NPC state] Cannot delete key %d from database %d.",
            key, runtime::process::CurrentDatabaseId());
    }
    return false;
}

// Current Cached.exe 0x004fc048.
bool GetEventRankerInfo(CNpcDbSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int eventId = 0;
    int characterId = 0;
    int creatureObjectId = 0;
    int npcObjectId = 0;
    int scoreLimit = 0;
    ::Disassemble(packet, "ddddd", &eventId, &characterId, &creatureObjectId, &npcObjectId, &scoreLimit);

    int adenaBonus = 0;
    int roundNumber = 0;
    int reward = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&adenaBonus);
    connection.Bind(&roundNumber);
    connection.Bind(&reward);
    constexpr const wchar_t* statement = L"{Call lin_BrGetEventRankerInfo (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&eventId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&scoreLimit);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    } else {
        // Log.Add(LOG_ERROR,
        //     "[NPC rewards] Cannot load the top-ranker reward for event %d, character %d, score threshold %d in database %d. Reply still contains zero round, reward and adena bonus.",
        //     eventId, characterId, scoreLimit, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[NPC rewards] Cannot load the top-ranker reward for event %d, character %d, score threshold %d in database %d; reply still contains zero round, reward and adena bonus.",
            eventId, characterId, scoreLimit, runtime::process::CurrentDatabaseId());
    }
    socket->Send("cddddddd", 5, eventId, roundNumber, characterId, creatureObjectId, reward, adenaBonus, npcObjectId);
    return false;
}

// Current Cached.exe 0x004fc268.
bool DecreaseEventTopRankerReward(CNpcDbSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int eventId = 0;
    int roundNumber = 0;
    int characterId = 0;
    ::Disassemble(packet, "ddd", &eventId, &roundNumber, &characterId);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_BrDecreaseEventTopRankerReward (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&eventId);
    connection.BindParameter(&roundNumber);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]DecreaseEventTopRankerReward fail, "
        //     L"eventId = %d, charId = %d",
        //     L"NpcDbSocket.cpp", 599, eventId, characterId
        // )
        // Log.Add(LOG_ERROR,
        //     "[NPC rewards] Cannot update the top-ranker reward for event %d, round %d, character %d in database %d.",
        //     eventId, roundNumber, characterId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[NPC rewards] Cannot update the top-ranker reward for event %d, round %d, character %d in database %d.",
            eventId, roundNumber, characterId, runtime::process::CurrentDatabaseId());
    }
    return false;
}

// Current Cached.exe 0x004fc41c.
bool LoadDBSavingMap(CNpcDbSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int npcObjectId = 0;
    int key = 0;
    ::Disassemble(packet, "dd", &npcObjectId, &key);
    int value = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&value);
    constexpr const wchar_t* statement = L"{Call lin_LoadDBSavingMap(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&key);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    } else {
        // Log.Add(LOG_ERROR,
        //     "[NPC state] Cannot load key %d for NPC object %d from database %d. Reply still returns value zero.",
        //     key, npcObjectId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[NPC state] Cannot load key %d for NPC object %d from database %d; reply still returns value zero.",
            key, npcObjectId, runtime::process::CurrentDatabaseId());
    }
    connection.UnBind();
    socket->Send("cddd", 4, npcObjectId, key, value);
    return false;
}

// Current Cached.exe 0x004fc5cc.
bool RequestCheckKamarokaReward(CNpcDbSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int roundNumber = 0;
    int npcObjectId = 0;
    int characterId = 0;
    int creatureObjectId = 0;
    ::Disassemble(packet, "dddd", &roundNumber, &npcObjectId, &characterId, &creatureObjectId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int ranking = 0;
    int checkFlag = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&ranking);
    connection.Bind(&checkFlag);
    constexpr const wchar_t* statement = L"{Call lin_CheckPremiumKamaReward(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&roundNumber);
    connection.BindParameter(&characterId);
    // if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
    //     ranking = -1
    //     checkFlag = 0
    // }
    const bool rewardQueryExecuted = connection.ExecutePrepared(statement);
    if (!rewardQueryExecuted || !connection.Fetch()) {
        if (!rewardQueryExecuted) {
            // Log.Add(LOG_ERROR,
            //     "[Kamaloka] Cannot check the reward for character %d, round %d in database %d. Reply still returns ranking -1 and reward flag zero.",
            //     characterId, roundNumber, runtime::process::CurrentDatabaseId())
            L2_LOG_ERROR(Log,
                "[Kamaloka] Cannot check the reward for character %d, round %d in database %d; reply still returns ranking -1 and reward flag zero.",
                characterId, roundNumber, runtime::process::CurrentDatabaseId());
        }
        ranking = -1;
        checkFlag = 0;
    }
    socket->Send("cddddd", 7, ranking, npcObjectId, characterId, creatureObjectId, checkFlag);
    return false;
}

// Current Cached.exe 0x004fc820.
bool RequestSetKamarokaReward(CNpcDbSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int roundNumber = 0;
    int characterId = 0;
    int rewardFlag = 0;
    ::Disassemble(packet, "ddd", &roundNumber, &characterId, &rewardFlag);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ReceivePremiumKamaReward(?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&roundNumber);
    connection.BindParameter(&characterId);
    connection.BindParameter(&rewardFlag);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]Kamaroka Reward Set fail, "
        //     L"nRound = %d, charId = %d",
        //     L"NpcDbSocket.cpp", 0x2e6, roundNumber, characterId
        // )
        // Log.Add(LOG_ERROR,
        //     "[Kamaloka] Cannot save reward flag %d for character %d, round %d in database %d.",
        //     rewardFlag, characterId, roundNumber, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[Kamaloka] Cannot save reward flag %d for character %d, round %d in database %d.",
            rewardFlag, characterId, roundNumber, runtime::process::CurrentDatabaseId());
    }
    return false;
}

BossInfo::BossInfo() :
    m_alive(0),
    m_hp(0),
    m_mp(0),
    m_posX(0),
    m_posY(0),
    m_posZ(0),
    m_deathTime(0),
    m_dbValue(0),
    m_npcId(0),
    m_killerMpccMemberCount(0) {
    // Cached.exe 0x004fafd4 leaves the two alignment words at +0x64 and
    // +0x94 untouched. They are layout carriers, not object state.
}

// Current Cached.exe 0x004fb080 destroys the +0x98 critical section and the
// UTF-16 strings at +0x68 and +0x18 before finishing MemoryObject.
BossInfo::~BossInfo() = default;

bool BossInfo::Save() {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const std::wstring npcDbName = Utf16ToWide(runtime::StringView(m_npcDbName));
    const std::wstring killerName = Utf16ToWide(runtime::StringView(m_killerMpccMasterName));
    if (SaveNpcBoss(
            connection, npcDbName.c_str(), m_alive, m_hp, m_mp, m_posX, m_posY, m_posZ, m_deathTime, 0, m_npcId,
            killerName.c_str(), m_killerMpccMemberCount
        )) {
        return true;
    }
    // Log.AddT(
    //     LOG_ERROR, L"[%s][%d] Save NpcBoss Failed! Npc DBName[%s]", L"NpcDbSocket.cpp", 0x4a3, npcDbName.c_str()
    // )
    // Log.Add(LOG_ERROR,
    //     "[NPC state] Cannot save boss \"%s\" (NPC %d, alive %d) in database %d. The in-memory state is retained.",
    //     WideToUtf8(npcDbName).c_str(), m_npcId, m_alive, runtime::process::CurrentDatabaseId())
    L2_LOG_ERROR(Log,
        "[NPC state] Cannot save boss \"%s\" (NPC %d, alive %d) in database %d; the in-memory state is retained.",
        WideToUtf8(npcDbName).c_str(), m_npcId, m_alive, runtime::process::CurrentDatabaseId());
    return false;
}

bool BossInfo::UpdateDBValue(int db_value) {
    L2_GUARD_FUNCTION();
    // Shipping code updates the live object before persistence and does not
    // roll the value back when the procedure fails.
    m_dbValue = db_value;
    DBConn connection(runtime::process::CurrentDatabaseId());
    const std::u16string npcDbName(runtime::StringView(m_npcDbName));
    constexpr const wchar_t* statement = L"{Call lin_UpdateNpcBossVariable (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(npcDbName.c_str());
    connection.BindParameter(&m_dbValue);
    if (connection.ExecutePrepared(statement))
        return true;

    // Log.AddT(
    //     LOG_ERROR, L"[%s][%d] UpdateBossNpcValue Failed! Npc DBName[%s]", L"NpcDbSocket.cpp", 0x4b6, npcDbName.c_str()
    // )
    // Log.Add(LOG_ERROR,
    //     "[NPC state] Cannot save variable %d for boss \"%s\" in database %d. The in-memory value remains %d.",
    //     db_value, WideToUtf8(npcDbName).c_str(), runtime::process::CurrentDatabaseId(), m_dbValue)
    // L2_LOG_ERROR(Log,
    //     "[NPC state] Cannot save variable %d for boss \"%s\" in database %d. The in-memory value remains %d.",
    //     db_value, WideToUtf8(npcDbName).c_str(), runtime::process::CurrentDatabaseId(), m_dbValue)
    L2_LOG_ERROR(Log,
        "[NPC state] Cannot save variable %d for boss \"%s\" in database %d; the in-memory value remains %d.",
        db_value, Utf16ToUtf8(npcDbName).c_str(), runtime::process::CurrentDatabaseId(), m_dbValue);
    return false;
}

BossInfoSP BossInfoDB::GetInfo(const wchar_t* sNpcDbName) {
    L2_GUARD_FUNCTION();
    const std::u16string key = WideToUtf16(sNpcDbName);
    BossInfoSP cached = m_infos.FindObject(key);
    if (cached)
        return cached;

    DBConn connection(runtime::process::CurrentDatabaseId());
    int alive = 0;
    int hp = 0;
    int mp = 0;
    int posX = 0;
    int posY = 0;
    int posZ = 0;
    int deathTime = 0;
    int ignoredTimeHigh = 0;
    int dbValue = 0;
    int npcId = 0;
    connection.Bind(&alive);
    connection.Bind(&hp);
    connection.Bind(&mp);
    connection.Bind(&posX);
    connection.Bind(&posY);
    connection.Bind(&posZ);
    connection.Bind(&deathTime);
    connection.Bind(&ignoredTimeHigh);
    connection.Bind(&dbValue);
    connection.Bind(&npcId);
    constexpr const wchar_t* statement = L"{Call lin_LoadNpcBoss (?)}";
    connection.Prepare(statement);
    connection.BindParameter(sNpcDbName);
    // const bool loaded = connection.ExecutePrepared(statement) && connection.Fetch()
    const bool bossQueryExecuted = connection.ExecutePrepared(statement);
    const bool loaded = bossQueryExecuted && connection.Fetch();

    if (!loaded) {
        // Noise: A missing boss row is normal on first use. The existing path creates default state. Query and creation failures are reported separately below.
        // Log.AddT(LOG_ERROR, L"[%s][%d] No record in table, npc dbname[%s]", L"NpcDbSocket.cpp", 0x473, sNpcDbName)
        if (!bossQueryExecuted) {
            // Log.Add(LOG_ERROR,
            //     "[NPC state] Cannot load boss \"%s\" from database %d. Default-state creation is still attempted.",
            //     WideToUtf8(sNpcDbName).c_str(), runtime::process::CurrentDatabaseId())
            L2_LOG_ERROR(Log,
                "[NPC state] Cannot load boss \"%s\" from database %d; default-state creation is still attempted.",
                WideToUtf8(sNpcDbName).c_str(), runtime::process::CurrentDatabaseId());
        }
        DBConn saveConnection(runtime::process::CurrentDatabaseId());
        if (!SaveNpcBoss(saveConnection, sNpcDbName, 0, 0, 0, 0, 0, 0, 0, 0, 0, L"", 0)) {
            // Log.AddT(
            //     LOG_ERROR, L"[%s][%d] Save NpcBoss Failed! Npc DBName[%s]", L"NpcDbSocket.cpp", 0x47b, sNpcDbName
            // )
            // Log.Add(LOG_ERROR,
            //     "[NPC state] Cannot create default state for boss \"%s\" in database %d. No boss state is returned to the caller.",
            //     WideToUtf8(sNpcDbName).c_str(), runtime::process::CurrentDatabaseId())
            L2_LOG_ERROR(Log,
                "[NPC state] Cannot create default state for boss \"%s\" in database %d; no boss state is returned to the caller.",
                WideToUtf8(sNpcDbName).c_str(), runtime::process::CurrentDatabaseId());
            return {};
        }
    }

    BossInfo* const info = new BossInfo;
    const std::u16string targetName = WideToUtf16(sNpcDbName);
    info->m_npcDbName.assign(std::u16string_view(targetName));
    if (loaded) {
        info->m_alive = alive;
        info->m_hp = hp;
        info->m_mp = mp;
        info->m_posX = posX;
        info->m_posY = posY;
        info->m_posZ = posZ;
        info->m_deathTime = deathTime;
        // Current 0x004fcda4 binds time_high but does not copy
        // it into BossInfo. +0x5c receives the following i0 column.
        info->m_dbValue = dbValue;
        info->m_npcId = npcId;
    }
    m_infos.Insert(key, info);
    return BossInfoSP(info, ObjectRefType::FunctionChain);
}

NpcDbCallbackMap gNpcDbPacketTable{DummyNpcDbPacket, u"ND_PROTOCOL"};

// Current CRT initializers 0x006a3204..0x006a34d0 bind every slot in this
// exact order. Slot zero is bound even though it equals the map's
// default callback.
const bool IsRegistered_ND_DUMMY = gNpcDbPacketTable.Bind(0, DummyNpcDbPacket);
const bool IsRegistered_ND_VERSION = gNpcDbPacketTable.Bind(1, NpcDbVersion);
const bool IsRegistered_ND_LOAD_NPC = gNpcDbPacketTable.Bind(2, LoadNpcRequest);
const bool IsRegistered_ND_SAVE_NPC = gNpcDbPacketTable.Bind(3, SaveNpcInfo);
const bool IsRegistered_ND_UPDATE_BOSS_VALUE = gNpcDbPacketTable.Bind(4, UpdateBossNpcValue);
const bool IsRegistered_ND_LOAD_LOTTO = gNpcDbPacketTable.Bind(5, LoadLottoInfo);
const bool IsRegistered_ND_LOTTO_DRAWING = gNpcDbPacketTable.Bind(6, LottoDrawing);
const bool IsRegistered_ND_LOTTO_START_NEXT_ROUND = gNpcDbPacketTable.Bind(7, LottoStartNextRound);
const bool IsRegistered_ND_LOTTO_SELL_TICKET = gNpcDbPacketTable.Bind(8, LottoSellTicket);
const bool IsRegistered_ND_LOTTO_SAVE_ROUND = gNpcDbPacketTable.Bind(9, LottoSaveRound);
const bool IsRegistered_ND_LOAD_NPC_EX = gNpcDbPacketTable.Bind(10, LoadNpcRequestEx);
const bool IsRegistered_ND_SAVE_DB_SAVING_MAP = gNpcDbPacketTable.Bind(11, SaveDBSavingMap);
const bool IsRegistered_ND_LOAD_DB_SAVING_MAP = gNpcDbPacketTable.Bind(12, LoadDBSavingMap);
const bool IsRegistered_ND_DELETE_DB_SAVING_MAP = gNpcDbPacketTable.Bind(13, DeleteDBSavingMap);
const bool IsRegistered_ND_GET_EVENT_RANKER_INFO = gNpcDbPacketTable.Bind(14, GetEventRankerInfo);
const bool IsRegistered_ND_DECREASE_EVENT_TOP_RANKER_REWARD = gNpcDbPacketTable.Bind(15, DecreaseEventTopRankerReward);
const bool IsRegistered_ND_GIVE_EVENT_DATA = gNpcDbPacketTable.Bind(16, GiveEventData);
const bool IsRegistered_ND_CHECK_KAMAROKA_REWARD = gNpcDbPacketTable.Bind(17, RequestCheckKamarokaReward);
const bool IsRegistered_ND_SET_KAMAROKA_REWARD = gNpcDbPacketTable.Bind(18, RequestSetKamarokaReward);

L2_DEFINE_OBJECT_CLASS_ID(CNpcDbSocket)

L2_DEFINE_OBJECT_CLASS_NAME(CNpcDbSocket, m_pClassName_CNpcDbSocket)

L2_DEFINE_OBJECT_NEW(CNpcDbSocket)

// Current allocator 0x004f9a9c publishes 0x188 bytes. The complete CRT
// table makes 0x006a34f8 the twenty-second MemoryObject class initializer.
L2_DEFINE_OBJECT_ALLOCATOR(CNpcDbSocket, m_pClassName_CNpcDbSocket)

L2_DEFINE_OBJECT_RELEASE(CNpcDbSocket)

CNpcDbSocket::CNpcDbSocket(std::uint64_t socket, int bufferSize) : CSocket(socket, bufferSize) {
    L2_GUARD_FUNCTION();
    m_packetTable = reinterpret_cast<CacheDPacketCallback*>(gNpcDbPacketTable.GetData());
}

CNpcDbSocket::~CNpcDbSocket() { L2_GUARD_FUNCTION(); }

CSocket* CNpcDbSocket::Allocate(std::uint64_t socket) {
    L2_GUARD_FUNCTION();
    return new CNpcDbSocket(socket, 0x5000);
}

void CNpcDbSocket::SetAddress(::in_addr address) {
    L2_GUARD_FUNCTION();
    CSocket::SetAddress(address);
}

void CNpcDbSocket::OnCreate() {
    L2_GUARD_FUNCTION();
    CacheDIOSocket::OnCreate();
}

void CNpcDbSocket::OnClose() {
    L2_GUARD_FUNCTION();
    const int error = GetSocketLastError();
    char address[64]{};
    const char* converted = ::inet_ntop(AF_INET, &m_address, address, static_cast<socklen_t>(sizeof(address)));
    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    const unsigned serverReadSize = server ? server->GetReadBufSize() : 0;
    const unsigned npcReadSize = GetReadBufSize();
    // Log.Add(
    //     LOG_ERROR,
    //     "npc server closed(%s) error: %d read buffer size: "
    //     "(server:%u npc:%u)",
    //     converted ? converted : "0.0.0.0", error, serverReadSize, npcReadSize
    // )
    // Log.Add(LOG_NORMAL,
    //     "[L2NPC] Connection from %s closed. Last socket error observed: %d. Read buffers: L2Server %u bytes, L2NPC %u bytes. The saved error may not identify the cause.",
    //     converted ? converted : "unknown address", error, serverReadSize, npcReadSize)
    L2_LOG_INFO(Log,
        "[L2NPC] Connection from %s closed. Last socket error observed: %d; read buffers: L2Server %u bytes, L2NPC %u bytes. The saved error may not identify the cause.",
        converted ? converted : "unknown address", error, serverReadSize, npcReadSize);
    runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_CLOSE);
    runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_socket), 0);
}

void CNpcDbSocket::OnRead() {
    L2_GUARD_FUNCTION();
    const auto* peerBytes = reinterpret_cast<const std::uint8_t*>(&m_address.s_addr);
    if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) == SocketMode::SM_CLOSE) {
        Close();
        return;
    }
    // if (m_nPendingWrite.load(std::memory_order_relaxed) >= 0x1000001) {
    const int pendingWrite = m_nPendingWrite.load(std::memory_order_relaxed);
    if (pendingWrite >= 0x1000001) {
        // Log.Add(LOG_ERROR,
        //     "[L2NPC] Closing connection %llu from %u.%u.%u.%u: pending writes reached %d bytes (limit 16777216).",
        //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), pendingWrite)
        L2_LOG_ERROR(Log,
            "[L2NPC] Closing connection %llu from %u.%u.%u.%u: pending writes reached %d bytes (limit 16777216).",
            static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), pendingWrite);
        Close();
        return;
    }

    const int readSize = static_cast<int>(m_pReadBuf->m_dwSize);
    int packetOffset = 0;
    while (packetOffset < readSize) {
        if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) == SocketMode::SM_READ_LEN) {
            if (readSize - packetOffset < 2) {
                Read(static_cast<std::uint32_t>(readSize - packetOffset));
                return;
            }
            const std::uint8_t* header = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_packetLength =
                static_cast<int>(static_cast<std::uint16_t>(header[0]) | (static_cast<std::uint16_t>(header[1]) << 8U)) - 2;
            if (m_packetLength < 1 || m_packetLength > 0x4ffe) {
                // Log.Add(
                //     LOG_ERROR, "socket %d: bad packet size %d",
                //     static_cast<int>(m_hSocket.load(std::memory_order_relaxed)), m_packetLength
                // )
                // Log.Add(LOG_WARN,
                //     "[L2NPC] Closing connection %llu from %u.%u.%u.%u: invalid incoming body length %d bytes (accepted range 1..20478).",
                //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), m_packetLength)
                L2_LOG_WARN(Log,
                    "[L2NPC] Closing connection %llu from %u.%u.%u.%u: invalid incoming body length %d bytes (accepted range 1..20478).",
                    static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), m_packetLength);
                runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_CLOSE);
                Close();
                return;
            }
            packetOffset += 2;
            runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_READ_BODY);
        }

        if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) != SocketMode::SM_READ_BODY) {
            // Log.Add(LOG_ERROR,
            //     "[L2NPC] Closing connection %llu from %u.%u.%u.%u: packet processing found an unexpected read state.",
            //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]))
            L2_LOG_ERROR(Log,
                "[L2NPC] Closing connection %llu from %u.%u.%u.%u: packet processing found an unexpected read state.",
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]));
            Close();
            return;
        }
        if (readSize - packetOffset < m_packetLength) {
            Read(static_cast<std::uint32_t>(readSize - packetOffset));
            return;
        }

        ++m_packetCount;
        if (m_packetCount == 0)
            m_createdTick = runtime::GetTickCount();
        std::uint8_t* packet = m_pReadBuf->m_Buffer.data() + packetOffset;
        Decrypt(packet, static_cast<std::int64_t>(m_key), m_packetLength);
        m_key += static_cast<std::uint64_t>(m_packetLength);

        const std::uint8_t protocol = packet[0];
        if (protocol >= 19) {
            // Log.Add(LOG_WARN,
            //     "[L2NPC] Closing connection %llu from %u.%u.%u.%u: unknown protocol %u (accepted range 0..18).",
            //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), static_cast<unsigned>(protocol))
            L2_LOG_WARN(Log,
                "[L2NPC] Closing connection %llu from %u.%u.%u.%u: unknown protocol %u (accepted range 0..18).",
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), static_cast<unsigned>(protocol));
            runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_CLOSE);
            Close();
            return;
        }

        auto* callback = new CNPCPacket;
        callback->m_pSocket = this;
        callback->m_pBuf = m_pReadBuf;
        callback->m_pFunc = gNpcDbPacketTable.GetData()[protocol];
        callback->SetPacketType(protocol);
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        m_pReadBuf->AddRef();
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        callback->PostObject(packetOffset);

        packetOffset += m_packetLength;
        runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_READ_LEN);
    }
    Read(0);
}

void CNpcDbSocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) == SocketMode::SM_CLOSE)
        return;

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* buffer = DefaultIOBuffer::Alloc();
    const int bodySize = ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments);
    va_end(arguments);
    if (bodySize == 0) {
        // Log.Add(
        //     LOG_ERROR, "%d: assemble too large packet. format %s",
        //     static_cast<int>(m_hSocket.load(std::memory_order_relaxed)), format
        // )
        // Log.Add(LOG_ERROR,
        //     "[L2NPC] Cannot assemble a reply on connection %llu within the 20478-byte body buffer (format %s). Discarding this reply.",
        //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), format)
        L2_LOG_ERROR(Log,
            "[L2NPC] Cannot assemble a reply on connection %llu within the 20478-byte body buffer (format %s); discarding this reply.",
            static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), format);
        buffer->Release();
        return;
    }

    const int packetSize = bodySize + 2;
    buffer->m_Buffer[0] = static_cast<std::uint8_t>(packetSize);
    buffer->m_Buffer[1] = static_cast<std::uint8_t>(static_cast<unsigned>(packetSize) >> 8U);
    {
        L2_GUARD(m_sendLock);
        buffer->m_dwSize = static_cast<std::uint32_t>(packetSize);
        L2_ADD_REF(this, ObjectRefType::IoSocketWrite);
        SyncWrite(buffer);
    }
}

void CNpcDbSocket::SendBuffer(const char* format, va_list arguments) {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) == SocketMode::SM_CLOSE)
        return;

    DefaultIOBuffer* buffer = DefaultIOBuffer::Alloc();
    const int bodySize = ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments);
    int packetSize = 0;
    if (bodySize == 0) {
        // Log.Add(
        //     LOG_ERROR, "%d: assemble too large packet. format %s",
        //     static_cast<int>(m_hSocket.load(std::memory_order_relaxed)), format
        // )
        // Log.Add(LOG_ERROR,
        //     "[L2NPC] Cannot assemble a buffered reply on connection %llu within the 20478-byte body buffer (format %s). The existing write path still receives a zero-length buffer.",
        //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), format)
        L2_LOG_ERROR(Log,
            "[L2NPC] Cannot assemble a buffered reply on connection %llu within the 20478-byte body buffer (format %s); the existing write path still receives a zero-length buffer.",
            static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), format);
    } else {
        packetSize = bodySize + 2;
        buffer->m_Buffer[0] = static_cast<std::uint8_t>(packetSize);
        buffer->m_Buffer[1] = static_cast<std::uint8_t>(static_cast<unsigned>(packetSize) >> 8U);
    }
    buffer->m_dwSize = static_cast<std::uint32_t>(packetSize);
    L2_ADD_REF(this, ObjectRefType::IoSocketWrite);
    SyncWrite(buffer);
}

#include "Shared/src/Log.h"

#include "SevenSignQuestDB.h"

#include "Cached.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/IOCPacket.h"
#include "Runtime.h"

#include <algorithm>
#include <cstdint>

namespace {

// constexpr const char* kSourceFile = "SevenSignQuestDB.cpp"
constexpr std::uint64_t kMaximumCollectedPoint = 1'000'000'000'000ULL;

int PartIndex(SSQPart::type part) { return static_cast<int>(part); }

} // namespace

SevenSignQuestDB::SevenSignQuestDB() {
    L2_GUARD_FUNCTION();

    m_loaded = false;
    m_roundNumber = 0;
    m_currentStatus = SSQStatus::NONE;
    m_winner = SSQPart::NONE;
    m_previousWinner = SSQPart::NONE;
    std::fill(std::begin(m_sealOwner), std::end(m_sealOwner), SSQPart::NONE);
    m_eventStartTime = 0;
    m_eventEndTime = 0;
    m_sealEffectTime = 0;
    m_sealEffectEndTime = 0;
    std::fill(std::begin(m_point), std::end(m_point), 0);
    std::fill(std::begin(m_collectedPoint), std::end(m_collectedPoint), 0);
    std::fill(std::begin(m_mainEventPoint), std::end(m_mainEventPoint), 0);
    std::fill(std::begin(m_memberCount), std::end(m_memberCount), 0);
    for (auto& counts : m_sealSelectionCount) {
        std::fill(std::begin(counts), std::end(counts), 0);
    }
    m_castleSnapshotTime = 0;
    m_canDropGuard = false;
}

SevenSignQuestDB::~SevenSignQuestDB() = default;

SevenSignQuestDB& SevenSignQuestDB::Instance() {
    static SevenSignQuestDB instance;
    return instance;
}

void SevenSignQuestDB::SetEventStartTime(time_l2_t time) {
    L2_GUARD_FUNCTION();
    m_eventStartTime = time;
}

bool SevenSignQuestDB::SetEventEndTime(time_l2_t time) {
    L2_GUARD_FUNCTION();
    m_eventEndTime = time;
    return true;
}

void SevenSignQuestDB::SetSealEffectTime(time_l2_t time) {
    L2_GUARD_FUNCTION();
    m_sealEffectTime = time;
}

bool SevenSignQuestDB::SetSealEffectEndTime(time_l2_t time) {
    L2_GUARD_FUNCTION();
    m_sealEffectEndTime = time;
    return true;
}

bool SevenSignQuestDB::SetCurrentStatus(SSQStatus::type status) {
    L2_GUARD_FUNCTION();
    m_currentStatus = status;
    return true;
}

bool SevenSignQuestDB::SetSealOwner(int sealNumber, SSQPart::type part) {
    L2_GUARD_FUNCTION();
    m_sealOwner[sealNumber] = part;
    return true;
}

bool SevenSignQuestDB::SetMainEventPoint(SSQPart::type part, std::int64_t point) {
    L2_GUARD_FUNCTION();
    m_mainEventPoint[PartIndex(part)] = point;
    return true;
}

bool SevenSignQuestDB::SetCollectedPoint(SSQPart::type part, std::int64_t point) {
    L2_GUARD_FUNCTION();

    // Current 0x00596418 performs an unsigned comparison. So both
    // values >= 10^12 and negative signed inputs are rejected.
    if (static_cast<std::uint64_t>(point) >= kMaximumCollectedPoint) {
        return false;
    }
    m_collectedPoint[PartIndex(part)] = point;
    return true;
}

bool SevenSignQuestDB::SetDropGuardStatus(bool canDropGuard) {
    L2_GUARD_FUNCTION();
    m_canDropGuard = canDropGuard;
    return true;
}

int SevenSignQuestDB::AssembleAll(char* outputBuffer, int bufferSize) {
    L2_GUARD_FUNCTION();

    int size = ::Assemble(outputBuffer, bufferSize, "d", m_roundNumber);
    size += ::Assemble(outputBuffer + size, bufferSize - size, "d", static_cast<int>(m_currentStatus));
    size += ::Assemble(outputBuffer + size, bufferSize - size, "d", static_cast<int>(m_winner));

    for (int seal = SSQSeal::range::min; seal <= SSQSeal::range::max; ++seal) {
        size += ::Assemble(outputBuffer + size, bufferSize - size, "d", static_cast<int>(m_sealOwner[seal]));
    }

    size += ::Assemble(
        outputBuffer + size, bufferSize - size, "dddd", m_eventStartTime, m_eventEndTime, m_sealEffectTime,
        m_sealEffectEndTime
    );

    for (int part = SSQPart::range::min; part <= SSQPart::range::max; ++part) {
        size += ::Assemble(
            outputBuffer + size, bufferSize - size, "QQQd", static_cast<long long>(m_point[part]),
            static_cast<long long>(m_collectedPoint[part]), static_cast<long long>(m_mainEventPoint[part]),
            m_memberCount[part]
        );
        for (int seal = SSQSeal::range::min; seal <= SSQSeal::range::max; ++seal) {
            size += ::Assemble(outputBuffer + size, bufferSize - size, "d", m_sealSelectionCount[part][seal]);
        }
    }

    size += ::Assemble(outputBuffer + size, bufferSize - size, "d", static_cast<int>(m_canDropGuard));
    size += ::Assemble(outputBuffer + size, bufferSize - size, "d", m_castleSnapshotTime);
    size += ::Assemble(outputBuffer + size, bufferSize - size, "d", static_cast<int>(m_previousWinner));
    return size;
}

bool SevenSignQuestDB::SaveSystemInfo() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    const int roundNumber = m_roundNumber;
    const SSQStatus::type status = m_currentStatus;
    const SSQPart::type winner = m_winner;
    const long eventStartTime = static_cast<long>(m_eventStartTime);
    const long eventEndTime = static_cast<long>(m_eventEndTime);
    const long sealEffectTime = static_cast<long>(m_sealEffectTime);
    const long sealEffectEndTime = static_cast<long>(m_sealEffectEndTime);
    SSQPart::type seals[7]{};
    for (int seal = SSQSeal::range::min; seal <= SSQSeal::range::max; ++seal) {
        seals[seal - 1] = m_sealOwner[seal];
    }
    const int castleSnapshotTime = m_castleSnapshotTime;
    const bool canDropGuard = m_canDropGuard;

    constexpr const wchar_t* statement = L"{Call lin_SaveSSQSystemInfo (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                                         L"?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&roundNumber);
    connection.BindParameter(&status);
    connection.BindParameter(&winner);
    connection.BindParameter(&eventStartTime);
    connection.BindParameter(&eventEndTime);
    connection.BindParameter(&sealEffectTime);
    connection.BindParameter(&sealEffectEndTime);
    for (const SSQPart::type& sealOwner : seals) {
        connection.BindParameter(&sealOwner);
    }
    connection.BindParameter(&castleSnapshotTime);
    connection.BindParameter(&canDropGuard);

    const bool saved = connection.ExecutePrepared(statement);
    if (!saved) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot save ssq system info. Round[%d]", kSourceFile, 0xe9, roundNumber
        // )
        // Log.Add(LOG_ERROR,
        //     "[Seven Signs] Cannot save event state for round %d in database %d.",
        //     roundNumber, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[seven signs] Cannot save event state for round %d in database %d.",
            roundNumber, runtime::process::CurrentDatabaseId());
    }
    return saved;
}

bool SevenSignQuestDB::SaveJoinInfo(SSQPart::type part) {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    const int partIndex = PartIndex(part);
    const int roundNumber = m_roundNumber;
    const std::int64_t point = m_point[partIndex];
    const std::int64_t collectedPoint = m_collectedPoint[partIndex];
    const std::int64_t mainEventPoint = m_mainEventPoint[partIndex];
    const int memberCount = m_memberCount[partIndex];
    int selectionCounts[7]{};
    for (int seal = SSQSeal::range::min; seal <= SSQSeal::range::max; ++seal) {
        selectionCounts[seal - 1] = m_sealSelectionCount[partIndex][seal];
    }

    constexpr const wchar_t* statement = L"{Call lin_SaveSSQJoinInfo (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                                         L"?)}";
    connection.Prepare(statement);
    connection.BindParameter(&roundNumber);
    connection.BindParameter(&part);
    connection.BindParameter(&point);
    connection.BindParameter(&collectedPoint);
    connection.BindParameter(&mainEventPoint);
    connection.BindParameter(&memberCount);
    for (const int& selectionCount : selectionCounts) {
        connection.BindParameter(&selectionCount);
    }

    const bool saved = connection.ExecutePrepared(statement);
    if (!saved) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot save ssq join info. Round[%d], Part[%d]", kSourceFile, 0x108, roundNumber,
        //     partIndex
        // )
        // Log.Add(LOG_ERROR,
        //     "[Seven Signs] Cannot save participation totals for faction %d, round %d, in database %d.",
        //     partIndex, roundNumber, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[seven signs] Cannot save participation totals for faction %d, round %d, in database %d.",
            partIndex, roundNumber, runtime::process::CurrentDatabaseId());
    }
    return saved;
}

bool SevenSignQuestDB::LoadJoinInfo(int roundNumber) {
    L2_GUARD_FUNCTION();

    std::int64_t point = 0;
    std::int64_t collectedPoint = 0;
    std::int64_t mainEventPoint = 0;
    int part = 0;
    int memberCount = 0;
    short selectionCounts[7]{};

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&point);
    connection.Bind(&collectedPoint);
    connection.Bind(&mainEventPoint);
    connection.Bind(&part);
    connection.Bind(&memberCount);
    for (short& selectionCount : selectionCounts) {
        connection.Bind(&selectionCount);
    }

    constexpr const wchar_t* statement = L"{Call lin_LoadSSQJoinInfo (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&roundNumber);
    // Original: if (!connection.ExecutePrepared(statement)) return false.
    if (!connection.ExecutePrepared(statement)) {
        // Previous: L2_LOG_ERROR(Log, "Cannot load Seven Signs participation totals for round %d in database %d: query execution failed, loading returns failure.", roundNumber, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[seven signs] Cannot load participation totals for round %d in database %d: query execution failed; loading returns failure.",
            roundNumber, runtime::process::CurrentDatabaseId());
        return false;
    }

    int rowCount = 0;
    while (connection.Fetch()) {
        m_point[part] = point;
        m_collectedPoint[part] = collectedPoint;
        m_mainEventPoint[part] = mainEventPoint;
        m_memberCount[part] = memberCount;
        for (int seal = SSQSeal::range::min; seal <= SSQSeal::range::max; ++seal) {
            m_sealSelectionCount[part][seal] = static_cast<int>(selectionCounts[seal - 1]);
        }
        ++rowCount;
    }
    if (rowCount != 2) {
        // Previous: L2_LOG_ERROR(Log, "Cannot finish loading Seven Signs participation totals for round %d in database %d: read %d rows, expected 2. Values already read remain in memory, loading returns failure.", roundNumber, runtime::process::CurrentDatabaseId(), rowCount)
        L2_LOG_ERROR(Log,
            "[seven signs] Cannot finish loading participation totals for round %d in database %d: read %d rows, expected 2. Values already read remain in memory; loading returns failure.",
            roundNumber, runtime::process::CurrentDatabaseId(), rowCount);
    }
    return rowCount == 2;
}

bool SevenSignQuestDB::CreateRound(
    int roundNumber,
    SSQStatus::type status,
    SSQPart::type winner,
    time_l2_t eventStartTime,
    time_l2_t eventEndTime,
    time_l2_t sealEffectTime,
    time_l2_t sealEffectEndTime,
    SSQPart::type seal1,
    SSQPart::type seal2,
    SSQPart::type seal3,
    SSQPart::type seal4,
    SSQPart::type seal5,
    SSQPart::type seal6,
    SSQPart::type seal7,
    time_l2_t castleSnapshotTime,
    bool canDropGuard
) {
    L2_GUARD_FUNCTION();

    DBConn createConnection(runtime::process::CurrentDatabaseId());
    const long eventStartParameter = static_cast<long>(eventStartTime);
    const long eventEndParameter = static_cast<long>(eventEndTime);
    const long sealEffectParameter = static_cast<long>(sealEffectTime);
    const long sealEffectEndParameter = static_cast<long>(sealEffectEndTime);
    const SSQPart::type seals[7] = {seal1, seal2, seal3, seal4, seal5, seal6, seal7};
    const int castleSnapshotParameter = castleSnapshotTime;
    const int canDropGuardParameter = canDropGuard ? 1 : 0;

    constexpr const wchar_t* createStatement = L"{Call lin_CreateSSQRound (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                                               L"?, ?, ?)}";
    createConnection.Prepare(createStatement);
    createConnection.BindParameter(&roundNumber);
    createConnection.BindParameter(&status);
    createConnection.BindParameter(&winner);
    createConnection.BindParameter(&eventStartParameter);
    createConnection.BindParameter(&eventEndParameter);
    createConnection.BindParameter(&sealEffectParameter);
    createConnection.BindParameter(&sealEffectEndParameter);
    for (const SSQPart::type& sealOwner : seals) {
        createConnection.BindParameter(&sealOwner);
    }
    createConnection.BindParameter(&castleSnapshotParameter);
    createConnection.BindParameter(&canDropGuardParameter);
    const bool created = createConnection.ExecutePrepared(createStatement);
    if (!created) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot create ssq round, Round[%d]", kSourceFile, 0x17b, roundNumber)
        // Log.Add(LOG_ERROR,
        //     "[Seven Signs] Cannot create round %d in database %d. The requested round state will still be applied in memory.",
        //     roundNumber, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[seven signs] Cannot create round %d in database %d; the requested round state will still be applied in memory.",
            roundNumber, runtime::process::CurrentDatabaseId());
    }

    // Current code publishes the requested state even when persistence fails.
    m_roundNumber = roundNumber;
    m_currentStatus = status;
    m_winner = winner;
    m_eventStartTime = eventStartTime;
    m_eventEndTime = eventEndTime;
    m_sealEffectTime = sealEffectTime;
    m_sealEffectEndTime = sealEffectEndTime;
    for (int seal = SSQSeal::range::min; seal <= SSQSeal::range::max; ++seal) {
        m_sealOwner[seal] = seals[seal - 1];
    }
    m_castleSnapshotTime = castleSnapshotTime;
    SetDropGuardStatus(canDropGuard);

    // Only the two active factions and the seven active seal slots are reset
    // at 0x00597491. Index zero is left untouched.
    for (int part = SSQPart::range::min; part <= SSQPart::range::max; ++part) {
        m_point[part] = 0;
        m_collectedPoint[part] = 0;
        m_mainEventPoint[part] = 0;
        m_memberCount[part] = 0;
        for (int seal = SSQSeal::range::min; seal <= SSQSeal::range::max; ++seal) {
            m_sealSelectionCount[part][seal] = 0;
        }
    }

    short previousWinner = 0;
    DBConn winnerConnection(runtime::process::CurrentDatabaseId());
    winnerConnection.Bind(&previousWinner);
    const int previousRound = roundNumber - 1;
    constexpr const wchar_t* winnerStatement = L"{Call lin_LoadSSQWinner (?)}";
    winnerConnection.Prepare(winnerStatement);
    winnerConnection.BindParameter(&previousRound);
    if (winnerConnection.ExecutePrepared(winnerStatement) && winnerConnection.Fetch()) {
        m_previousWinner = static_cast<SSQPart::type>(previousWinner);
    } else {
        m_previousWinner = SSQPart::NONE;
    }
    return created;
}

bool SevenSignQuestDB::StartEvent(int roundNumber) {
    L2_GUARD_FUNCTION();

    CreateRound(
        roundNumber, SSQStatus::EVENT, SSQPart::NONE, m_eventStartTime, m_eventEndTime, m_sealEffectTime,
        m_sealEffectEndTime, m_sealOwner[1], m_sealOwner[2], m_sealOwner[3], m_sealOwner[4], m_sealOwner[5], m_sealOwner[6],
        m_sealOwner[7], m_castleSnapshotTime, false
    );
    return true;
}

bool SevenSignQuestDB::Load() {
    L2_GUARD_FUNCTION();

    int roundNumber = 0;
    short status = 0;
    short winner = 0;
    time_l2_t eventStartTime = 0;
    time_l2_t eventEndTime = 0;
    time_l2_t sealEffectTime = 0;
    time_l2_t sealEffectEndTime = 0;
    short seals[7]{};
    time_l2_t castleSnapshotTime = 0;
    int canDropGuard = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&roundNumber);
    connection.Bind(&status);
    connection.Bind(&winner);
    connection.Bind(&eventStartTime);
    connection.Bind(&eventEndTime);
    connection.Bind(&sealEffectTime);
    connection.Bind(&sealEffectEndTime);
    for (short& sealOwner : seals)
        connection.Bind(&sealOwner);
    connection.Bind(&castleSnapshotTime);
    connection.Bind(&canDropGuard);

    constexpr const wchar_t* statement = L"{Call lin_LoadSSQSystemInfo}";
    connection.Prepare(statement);
    // Original: if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
    // Keep the original short-circuit: Fetch is not called after execution fails.
    const bool executed = connection.ExecutePrepared(statement);
    if (!executed || !connection.Fetch()) {
        // Previous: L2_LOG(Log, executed ? LOG_NORMAL : LOG_ERROR, "Seven Signs state could not be read from database %d: %s. Attempting to create initial round 0, loading continues even if creation fails.", runtime::process::CurrentDatabaseId(), executed ? "no first row was fetched" : "query execution failed")
        L2_LOG(Log, executed ? LOG_NORMAL : LOG_ERROR,
            "[seven signs] State could not be read from database %d: %s. Attempting to create initial round 0; loading continues even if creation fails.",
            runtime::process::CurrentDatabaseId(),
            executed ? "no first row was fetched" : "query execution failed");
        CreateRound(
            0, SSQStatus::NONE, SSQPart::NONE, 0, 0, 0, 0, SSQPart::NONE, SSQPart::NONE, SSQPart::NONE, SSQPart::NONE,
            SSQPart::NONE, SSQPart::NONE, SSQPart::NONE, 0, false
        );
        m_loaded = true;
        return true;
    }

    m_roundNumber = roundNumber;
    m_currentStatus = static_cast<SSQStatus::type>(status);
    m_winner = static_cast<SSQPart::type>(winner);
    m_eventStartTime = eventStartTime;
    m_eventEndTime = eventEndTime;
    m_sealEffectTime = sealEffectTime;
    m_sealEffectEndTime = sealEffectEndTime;
    for (int seal = SSQSeal::range::min; seal <= SSQSeal::range::max; ++seal) {
        m_sealOwner[seal] = static_cast<SSQPart::type>(seals[seal - 1]);
    }
    m_castleSnapshotTime = castleSnapshotTime;
    m_canDropGuard = canDropGuard != 0;

    if (!connection.Fetch() || roundNumber != m_roundNumber - 1) {
        m_previousWinner = SSQPart::NONE;
    } else {
        m_previousWinner = static_cast<SSQPart::type>(winner);
    }

    if (!LoadJoinInfo(m_roundNumber))
        return false;
    m_loaded = true;
    return true;
}

#include "Shared/src/QueryPacket.h"

#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)
#include "Shared/src/Log.h"
#endif

#if defined(L2_BUILD_CACHED)
#include "CacheSocket.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Runtime/ProcessRuntime.h"

#include <algorithm>
#endif

#if defined(L2_BUILD_L2SERVER)
#include "L2Server/src/DB.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Runtime/EventHandle.h"
#endif

#include <array>
#include <cstdint>
#include <cstring>

namespace {

#if defined(L2_BUILD_L2SERVER)
// Diagnostic labels only. Query IDs and registrations remain unchanged.
constexpr std::array<const char*, 4> kQueryOperationNames{
    {"hero rank", "hero profile", "world olympiad hero rank", "world olympiad hero profile"}};
#endif

#if defined(L2_BUILD_CACHED)

template <std::size_t HostCapacity, std::size_t TargetCapacity>
std::array<char16_t, HostCapacity> MakeUtf16ResultBuffer(const char16_t (&source)[TargetCapacity]) {
    std::array<char16_t, HostCapacity> result{};
    const std::u16string_view hostValue(
        source, static_cast<std::size_t>(std::find(std::begin(source), std::end(source), u'\0') - std::begin(source))
    );
    const std::size_t copied = std::min(hostValue.size(), HostCapacity - 1);
    std::copy_n(hostValue.data(), copied, result.data());
    result[copied] = u'\0';
    return result;
}

template <std::size_t TargetCapacity, std::size_t HostCapacity>
void CopyUtf16Result(
    char16_t (&destination)[TargetCapacity], const std::array<char16_t, HostCapacity>& source, std::size_t targetBufferBytes
) {
    const std::size_t targetUnits = std::min(TargetCapacity, targetBufferBytes / sizeof(char16_t));
    if (targetUnits == 0) {
        return;
    }

    const std::u16string_view targetValue(source.data());
    const std::size_t copied = std::min(targetValue.size(), targetUnits - 1);
    std::copy_n(targetValue.data(), copied, destination);
    destination[copied] = u'\0';
}

#endif

} // namespace

GetHeroRank::GetHeroRank() { std::memset(this, 0, sizeof(*this)); }

GetHeroById::GetHeroById(const int charId) {
    std::memset(this, 0, sizeof(*this));
    mCharId = charId;
}

CQUERY_WOLYMPIAD_LOAD_ALL_HERO::CQUERY_WOLYMPIAD_LOAD_ALL_HERO() { std::memset(this, 0, sizeof(*this)); }

CQUERY_WOLYMPIAD_GET_HERO_BY_ID::CQUERY_WOLYMPIAD_GET_HERO_BY_ID(const int worldId, const int charId) {
    std::memset(this, 0, sizeof(*this));
    m_nWorldID = worldId;
    mCharId = charId;
}

#if defined(L2_BUILD_CACHED)

QueryPacketCallbackMap g_QueryPacketTable(DummyQueryPacket, u"QueryProtocol");

bool DummyQueryPacket(QueryPacketSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyQueryPacket called in Query Packet")
    // Noise: All current dispatchers report the decoded protocol ID before calling this fallback. A second context-free
    // record adds no information. Log.Add(
    //     LOG_ERROR,
    //     "[database protocol] Unhandled query request, payload ignored.")
    return false;
}

template <class Packet, int Protocol>
bool TQuery<Packet, Protocol>::DoJob(QueryPacketSocket* socket, const std::uint8_t* packet) {
    static_assert(Protocol >= 0 && Protocol < 4);
    std::uint64_t requestHandle = 0;
    std::uint64_t eventHandle = 0;
    const std::uint8_t* const objectBytes = ::Disassemble(packet, "pp", &requestHandle, &eventHandle);
    Packet* const job = reinterpret_cast<Packet*>(const_cast<std::uint8_t*>(objectBytes));
    if (job->DoCacheDJob()) {
        socket->Send(
            "chppb", 0xc1, Protocol, static_cast<std::int64_t>(requestHandle), static_cast<std::int64_t>(eventHandle),
            static_cast<int>(sizeof(Packet)), job
        );
    }
    return false;
}

namespace {

// Current Cached.exe TQuery<GetHeroRank, 0>::DoJob: 0x00523418.
L2_BIND_PACKET(IsRegistered_QUERY_GET_HERO_RANK, g_QueryPacketTable, 0, TQuery<GetHeroRank, 0>::DoJob);
// Current Cached.exe TQuery<GetHeroById, 1>::DoJob: 0x00523a10.
L2_BIND_PACKET(IsRegistered_QUERY_GET_HERO_BY_ID, g_QueryPacketTable, 1, TQuery<GetHeroById, 1>::DoJob);
// Current Cached.exe
// TQuery<CQUERY_WOLYMPIAD_LOAD_ALL_HERO, 2>::DoJob: 0x0052349c.
L2_BIND_PACKET(
    IsRegistered_QUERY_WOLYMPIAD_LOAD_ALL_HERO, g_QueryPacketTable, 2, TQuery<CQUERY_WOLYMPIAD_LOAD_ALL_HERO, 2>::DoJob
);
// Current Cached.exe
// TQuery<CQUERY_WOLYMPIAD_GET_HERO_BY_ID, 3>::DoJob: 0x00523a98.
L2_BIND_PACKET(
    IsRegistered_QUERY_WOLYMPIAD_GET_HERO_BY_ID, g_QueryPacketTable, 3, TQuery<CQUERY_WOLYMPIAD_GET_HERO_BY_ID, 3>::DoJob
);

} // namespace

bool GetHeroRank::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    int heroClass = 0;
    int heroId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&heroClass);
    connection.Bind(&heroId);
    constexpr const wchar_t* statement = L"{Call lin_GetAllHeroes}";
    connection.Prepare(statement);
    // if (!connection.ExecutePrepared(statement)) return true.
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[olympiad] Cannot load the hero rank: database execution failed. The reply retains its existing hero count
        //     %d.", mHeroCount)
        L2_LOG_ERROR(
            Log,
            "[olympiad] Cannot load the hero rank: database execution failed; the reply retains its existing hero count %d.",
            mHeroCount
        );
        return true;
    }

    mHeroCount = 0;
    while (connection.Fetch()) {
        // Current 0x00523180 trusts the result cardinality and
        // has no 36-entry bounds check.
        mHeroClassList[mHeroCount] = heroClass;
        mHeroIdList[mHeroCount] = heroId;
        ++mHeroCount;
    }
    return true;
}

bool CQUERY_WOLYMPIAD_LOAD_ALL_HERO::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    int heroClass = 0;
    int worldId = 0;
    int heroId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&heroClass);
    connection.Bind(&worldId);
    connection.Bind(&heroId);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadLoadAllHeroes}";
    connection.Prepare(statement);
    // if (!connection.ExecutePrepared(statement)) return true.
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[world olympiad] Cannot load the hero rank: database execution failed. The reply retains its existing hero
        //     count %d.", mHeroCount)
        L2_LOG_ERROR(
            Log,
            "[world olympiad] Cannot load the hero rank: database execution failed; the reply retains its existing hero "
            "count %d.",
            mHeroCount
        );
        return true;
    }

    mHeroCount = 0;
    while (connection.Fetch()) {
        // Current 0x00523311 likewise performs no 36-entry bounds check.
        mHeroClassList[mHeroCount] = heroClass;
        mHeroWorldIDList[mHeroCount] = worldId;
        mHeroIdList[mHeroCount] = heroId;
        ++mHeroCount;
    }
    return true;
}

bool GetHeroById::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> characterName = MakeUtf16ResultBuffer<25>(mCharName);
    std::array<char16_t, 25> pledgeName = MakeUtf16ResultBuffer<25>(mPledgeName);
    std::array<char16_t, 25> allianceName = MakeUtf16ResultBuffer<25>(mAllianceName);
    // Current code binds only 0x80 target bytes despite the 129-unit field.
    std::array<char16_t, 64> words = MakeUtf16ResultBuffer<64>(mWords);
    int mainClass = mMainClass;
    int heroType = mHeroType;
    int pledgeId = mPledgeId;
    int pledgeCrestId = mPledgeCrestId;
    int allianceId = mAllianceId;
    int allianceCrestId = mAllianceCrestId;
    int winCount = mWinCount;
    int historyOpen = mHistoryOpen;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(characterName.data(), 0x32);
    connection.Bind(&mainClass);
    connection.Bind(&heroType);
    connection.Bind(&pledgeId);
    connection.Bind(pledgeName.data(), 0x32);
    connection.Bind(&pledgeCrestId);
    connection.Bind(&allianceId);
    connection.Bind(allianceName.data(), 0x32);
    connection.Bind(&allianceCrestId);
    connection.Bind(&winCount);
    connection.Bind(words.data(), 0x80);
    connection.Bind(&historyOpen);
    constexpr const wchar_t* statement = L"{Call lin_GetHeroById (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mCharId);

    // const bool fetched =
    //     connection.ExecutePrepared(statement) &&
    //     connection.Fetch()
    const bool executed = connection.ExecutePrepared(statement);
    const bool fetched = executed && connection.Fetch();
    mReturn = fetched;
    // if (!fetched) return true.
    if (!fetched) {
        if (!executed) {
            // Log.Add(LOG_ERROR,
            //     "[olympiad] Cannot load the hero profile for character DB ID %d: database execution failed. The reply
            //     reports no profile.", mCharId)
            L2_LOG_ERROR(
                Log,
                "[olympiad] Cannot load the hero profile for character DB ID %d: database execution failed; the reply "
                "reports no profile.",
                mCharId
            );
        }
        return true;
    }

    CopyUtf16Result(mCharName, characterName, 0x32);
    mMainClass = mainClass;
    mHeroType = heroType;
    mPledgeId = pledgeId;
    CopyUtf16Result(mPledgeName, pledgeName, 0x32);
    mPledgeCrestId = pledgeCrestId;
    mAllianceId = allianceId;
    CopyUtf16Result(mAllianceName, allianceName, 0x32);
    mAllianceCrestId = allianceCrestId;
    mWinCount = winCount;
    CopyUtf16Result(mWords, words, 0x80);
    mHistoryOpen = historyOpen;
    return true;
}

bool CQUERY_WOLYMPIAD_GET_HERO_BY_ID::DoCacheDJob() {
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> characterName = MakeUtf16ResultBuffer<25>(mCharName);
    std::array<char16_t, 25> pledgeName = MakeUtf16ResultBuffer<25>(mPledgeName);
    std::array<char16_t, 25> allianceName = MakeUtf16ResultBuffer<25>(mAllianceName);
    std::array<char16_t, 64> words = MakeUtf16ResultBuffer<64>(mWords);
    int mainClass = mMainClass;
    int heroType = mHeroType;
    int pledgeId = mPledgeId;
    int pledgeCrestId = mPledgeCrestId;
    int allianceId = mAllianceId;
    int allianceCrestId = mAllianceCrestId;
    int winCount = mWinCount;
    int historyOpen = mHistoryOpen;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(characterName.data(), 0x32);
    connection.Bind(&mainClass);
    connection.Bind(&heroType);
    connection.Bind(&pledgeId);
    connection.Bind(pledgeName.data(), 0x32);
    connection.Bind(&pledgeCrestId);
    connection.Bind(&allianceId);
    connection.Bind(allianceName.data(), 0x32);
    connection.Bind(&allianceCrestId);
    connection.Bind(&winCount);
    connection.Bind(words.data(), 0x80);
    connection.Bind(&historyOpen);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadGetHeroById (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nWorldID);
    connection.BindParameter(&mCharId);

    // const bool fetched =
    //     connection.ExecutePrepared(statement) &&
    //     connection.Fetch()
    const bool executed = connection.ExecutePrepared(statement);
    const bool fetched = executed && connection.Fetch();
    mReturn = fetched;
    // if (!fetched) return true.
    if (!fetched) {
        if (!executed) {
            // Log.Add(LOG_ERROR,
            //     "[world olympiad] Cannot load the hero profile for character DB ID %d from world %d: database execution
            //     failed, the reply reports no profile.", mCharId, m_nWorldID)
            L2_LOG_ERROR(
                Log,
                "[world olympiad] Cannot load the hero profile for character DB ID %d from world %d: database execution "
                "failed; the reply reports no profile.",
                mCharId, m_nWorldID
            );
        }
        return true;
    }

    CopyUtf16Result(mCharName, characterName, 0x32);
    mMainClass = mainClass;
    mHeroType = heroType;
    mPledgeId = pledgeId;
    CopyUtf16Result(mPledgeName, pledgeName, 0x32);
    mPledgeCrestId = pledgeCrestId;
    mAllianceId = allianceId;
    CopyUtf16Result(mAllianceName, allianceName, 0x32);
    mAllianceCrestId = allianceCrestId;
    mWinCount = winCount;
    CopyUtf16Result(mWords, words, 0x80);
    mHistoryOpen = historyOpen;
    return true;
}

#elif defined(L2_BUILD_L2SERVER)

QueryPacketCallbackMap g_QueryPacketTable(DummyQueryPacket, u"QueryProtocol");

template <class Packet, int Protocol>
bool TQuery<Packet, Protocol>::DoJob(QueryPacketSocket*, const std::uint8_t* packet) {
    static_assert(Protocol >= 0 && Protocol < 4);
    std::uint64_t signalHandle = 0;
    std::uint64_t destinationAddress = 0;
    const std::uint8_t* const objectBytes = ::Disassemble(packet, "pp", &signalHandle, &destinationAddress);
    std::memcpy(reinterpret_cast<void*>(destinationAddress), objectBytes, sizeof(Packet));
    // runtime::event_handle::Set(
    //     reinterpret_cast<void*>(signalHandle))
    const bool notified = runtime::event_handle::Set(reinterpret_cast<void*>(signalHandle));
    if (!notified) {
        // Log.Add(LOG_ERROR,
        //     "[database query] Received %s reply (query %d), but could not signal completion. Reply data was already
        //     copied.", kQueryOperationNames[Protocol], Protocol)
        L2_LOG_ERROR(
            Log,
            "[database query] Received %s reply (query %d), but could not signal completion; reply data was already copied.",
            kQueryOperationNames[Protocol], Protocol
        );
    }
    return false;
}

template <class Packet, int Protocol>
bool TQuery<Packet, Protocol>::Do(const int worldId, Packet* const packet) {
    static_assert(Protocol >= 0 && Protocol < 4);
    // if (!packet) return false.
    if (!packet) {
        // Log.Add(LOG_ERROR,
        //     "[database query] Cannot start %s query %d through world %d: request object is null. Request not sent.",
        //     kQueryOperationNames[Protocol], Protocol, worldId)
        L2_LOG_ERROR(
            Log, "[database query] Cannot start %s query %d through world %d: request object is null; request not sent.",
            kQueryOperationNames[Protocol], Protocol, worldId
        );
        return false;
    }

    runtime::event_handle::Handle signal = runtime::event_handle::Create(false, false);
    // if (!signal) return false.
    if (!signal) {
        // Log.Add(LOG_ERROR,
        //     "[database query] Cannot start %s query %d through world %d: completion event could not be created. Request
        //     not sent.", kQueryOperationNames[Protocol], Protocol, worldId)
        L2_LOG_ERROR(
            Log,
            "[database query] Cannot start %s query %d through world %d: completion event could not be created; request not "
            "sent.",
            kQueryOperationNames[Protocol], Protocol, worldId
        );
        return false;
    }

    db.RequestTQuery(worldId, static_cast<short>(Protocol), signal, static_cast<int>(sizeof(Packet)), *packet);
    const auto signaled = runtime::event_handle::WaitAny(&signal, 1, 30000);
    // runtime::event_handle::Close(signal)
    const bool closed = runtime::event_handle::Close(signal);
    if (!signaled.has_value()) {
        // Log.Add(LOG_ERROR,
        //     "[database query] %s query %d through world %d did not complete: wait timed out or failed (timeout 30000 ms)
        //     completion event %s.", kQueryOperationNames[Protocol], Protocol, worldId, closed ? "closed" : "could not be
        //     closed")
        L2_LOG_ERROR(
            Log,
            "[database query] %s query %d through world %d did not complete: wait timed out or failed (timeout 30000 ms); "
            "completion event %s.",
            kQueryOperationNames[Protocol], Protocol, worldId, closed ? "closed" : "could not be closed"
        );
    } else if (!closed) {
        // Log.Add(LOG_ERROR,
        //     "[database query] %s query %d through world %d completed, but its completion event could not be closed.",
        //     kQueryOperationNames[Protocol], Protocol, worldId)
        L2_LOG_ERROR(
            Log, "[database query] %s query %d through world %d completed, but its completion event could not be closed.",
            kQueryOperationNames[Protocol], Protocol, worldId
        );
    }
    return signaled.has_value();
}

bool DummyQueryPacket(QueryPacketSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyQueryPacket called in Query Packet")
    // Noise: All current dispatchers report the decoded protocol ID before calling this fallback. A second context-free
    // record adds no information. Log.Add(
    //     LOG_ERROR,
    //     "[database protocol] Unhandled query reply, payload ignored.")
    return false;
}

namespace {

L2_BIND_PACKET(IsRegistered_QUERY_GET_HERO_RANK, g_QueryPacketTable, 0, TQuery<GetHeroRank, 0>::DoJob);
L2_BIND_PACKET(IsRegistered_QUERY_GET_HERO_BY_ID, g_QueryPacketTable, 1, TQuery<GetHeroById, 1>::DoJob);
L2_BIND_PACKET(
    IsRegistered_QUERY_WOLYMPIAD_LOAD_ALL_HERO, g_QueryPacketTable, 2, TQuery<CQUERY_WOLYMPIAD_LOAD_ALL_HERO, 2>::DoJob
);
L2_BIND_PACKET(
    IsRegistered_QUERY_WOLYMPIAD_GET_HERO_BY_ID, g_QueryPacketTable, 3, TQuery<CQUERY_WOLYMPIAD_GET_HERO_BY_ID, 3>::DoJob
);

} // namespace

template class TQuery<GetHeroRank, 0>;
template class TQuery<GetHeroById, 1>;
template class TQuery<CQUERY_WOLYMPIAD_LOAD_ALL_HERO, 2>;
template class TQuery<CQUERY_WOLYMPIAD_GET_HERO_BY_ID, 3>;

#endif

#include "PledgeRanking.h"

#include "L2Core/src/inc/Guard.h"

#if defined(L2_BUILD_CACHED)
#include "CacheSocket.h"
#include "DBConn.h"
#include "PacketHelper.h"
#elif defined(L2_BUILD_L2SERVER)
#include "L2Server/src/DB.h"
#endif
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "RWLock.h"

#include <functional>
#include <map>
#include <set>

RWLock CPledgeRanking::s_lock;
std::map<std::uint32_t, int> CPledgeRanking::s_nameValueMap;
// Current Cached.exe 0x0051d964 is the compiled process-lifetime ordered-tree
// constructor for this descending-score multiset.
std::multiset<int, std::greater<int>> CPledgeRanking::s_rankingSet;

#if defined(L2_BUILD_CACHED)
bool CPledgeRanking::SendRankingList(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    if (!socket) {
        return false;
    }

    CPacketHelperEx<20480> payload;
    int rowCount = 0;
    {
        L2_READ_GUARD(s_lock);
        for (const auto& row : s_nameValueMap) {
            payload.Assemble("dd", row.first, row.second);
            if (rowCount < 0x3b6) {
                ++rowCount;
            } else {
                // Current 0x0051e77c appends before this comparison. Therefore
                // the flushing payload contains one more encoded row than the
                // transmitted count. Retain that shipping edge case.
                socket->Send("chdb", 0xc6, 0x30, rowCount, payload.GetLength(), payload.GetPacket());
                payload.Reset();
                rowCount = 0;
            }
        }
        if (rowCount > 0) {
            socket->Send("chdb", 0xc6, 0x30, rowCount, payload.GetLength(), payload.GetPacket());
        }
    }
    return false;
}
#endif

bool CPledgeRanking::IsInRanking(std::uint32_t pledgeId) {
    L2_GUARD_FUNCTION();
    bool found;
    {
        L2_READ_GUARD(s_lock);
        found = s_nameValueMap.find(pledgeId) != s_nameValueMap.end();
    }
    return found;
}

int CPledgeRanking::GetRanking(std::uint32_t pledgeId) {
    L2_GUARD_FUNCTION();
    int ranking = 0;
    {
        L2_READ_GUARD(s_lock);
        const auto pledge = s_nameValueMap.find(pledgeId);
        if (pledge == s_nameValueMap.end()) {
            return 0;
        }

        const auto score = s_rankingSet.find(pledge->second);
        ranking = score == s_rankingSet.end() ? 0 : static_cast<int>(std::distance(s_rankingSet.begin(), score)) + 1;
    }
    return ranking;
}

void CPledgeRanking::Remove(std::uint32_t pledgeId) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(s_lock);
        const auto found = s_nameValueMap.find(pledgeId);
        if (found != s_nameValueMap.end()) {
            const auto score = s_rankingSet.find(found->second);
            if (score != s_rankingSet.end()) {
                s_rankingSet.erase(score);
            }
            s_nameValueMap.erase(found);
        }
    }
}

void CPledgeRanking::Insert(std::uint32_t pledgeId, int score) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(s_lock);
        // map::operator[] followed by an unconditional multiset insertion is the
        // current order, including its duplicate-ID behavior.
        s_nameValueMap[pledgeId] = score;
        s_rankingSet.insert(score);
    }
}

void CPledgeRanking::Update(std::uint32_t pledgeId, int score) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(s_lock);
        const auto found = s_nameValueMap.find(pledgeId);
        if (found != s_nameValueMap.end()) {
            const auto previousScore = s_rankingSet.find(found->second);
            if (previousScore != s_rankingSet.end()) {
                s_rankingSet.erase(previousScore);
            }
            s_nameValueMap[pledgeId] = score;
            s_rankingSet.insert(score);
        }
    }
}

#if defined(L2_BUILD_CACHED)
void CPledgeRanking::Load() {
    L2_GUARD_FUNCTION();
    std::uint32_t pledgeId = 0;
    int score = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    connection.Bind(&score);
    constexpr const wchar_t* statement = L"{Call lin_LoadPledgeRanking}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        return;
    }
    while (connection.Fetch()) {
        Insert(pledgeId, score);
    }
}
#endif

#if defined(L2_BUILD_L2SERVER)
void CPledgeRanking::RequestLoadRanking() {
    // Current L2Server 0x009dda08.
    L2_GUARD_FUNCTION();
    db.RequestLoadPledgeRanking();
}

bool CPledgeRanking::LoadRanking(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x009de714, RP_EX 0x30.
    L2_GUARD_FUNCTION();
    int count = 0;
    packet = ::Disassemble(packet, "d", &count);
    for (int index = 0; index < count; ++index) {
        std::uint32_t pledgeId;
        int score;
        packet = ::Disassemble(packet, "dd", &pledgeId, &score);
        Insert(pledgeId, score);
    }
    return false;
}
#endif

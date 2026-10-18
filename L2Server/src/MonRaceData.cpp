#include "MonRaceData.h"
#include "MonRaceParser.h"
#include "Protocol/src/SystemMessageIds.h"

#include "CConfig.h"
#include "CWorld.h"
#include "DB.h"
#include "Item.h"
#include "NPC.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Residence.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/FHtml.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/net/Serialize.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>

namespace {


int SignedLowByte(std::int32_t value) noexcept {
    const std::uint8_t lowByte = static_cast<std::uint8_t>(static_cast<std::uint32_t>(value));
    std::int8_t signedByte = 0;
    std::memcpy(&signedByte, &lowByte, sizeof(signedByte));
    return signedByte;
}


template <typename... Args>
void BroadcastToRaceKeepers(const std::vector<CMonRaceDB::KeeperLoc>& keepers, const char* format, Args... args) {
    if (g_ptlUserMap == nullptr) {
        return;
    }
    for (const CMonRaceDB::KeeperLoc& keeper : keepers) {
        g_ptlUserMap->BroadcastToNeighborRect(nullptr, 0x80000, keeper.m_loc, keeper.m_radius, format, args...);
    }
}

} // namespace

CMonRaceDB g_MonRaceDB;

std::int32_t MONRACE_MONEY[9] = {
    0,
    100,
    500,
    1000,
    5000,
    10000,
    20000,
    50000,
    100000,
};

void CMonRaceData::Init() {
    // Current L2Server 0x008417f0. The current body does not
    // initialize condition at +0x16c.
    L2_GUARD_FUNCTION();
    runner_id = 0;
    npc_classid = 0;
    std::memset(runner_name, 0, sizeof(runner_name));
    sys_string = 0;
    min_speed = 0;
    max_speed = 0;
    std::memset(guts, 0, sizeof(guts));
    initial_cond = 0;
    initial_win = 0;
    m_runcount = 0;
    m_wincount = 0;
    m_totalruntime = 0.0;
}

void CMonRaceData::SetGuts(std::vector<std::int64_t>* values) {
    // Current L2Server 0x00842900.
    L2_GUARD_FUNCTION();
    if (values == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < values->size(); ++index) {
        guts[index] = static_cast<std::int32_t>(runtime::CheckedVectorAt((*values), index));
    }
}

void CMonRaceData::Debug() {
    // Current L2Server 0x00845a58.
    L2_GUARD_FUNCTION();
    g_MonRaceDB.Insert(this);
}

int CMonRaceDB::LoadMonRaceData() {
    // Current L2Server 0x008464d8.
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kMonRacePath = L"..\\Script\\monrace.txt";
#else
    constexpr const wchar_t* kMonRacePath = L"../script/monrace.txt";
#endif

    CSecureStream input(kMonRacePath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"MonRaceData.cpp",
        //     0xba,
        //     kMonRacePath)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"MonRaceData.cpp",
        //     0xba,
        //     kMonRacePath)
        L2_LOG_ERROR(Log, "Cannot read monrace.txt; monster race loading returns -1.");
        return -1;
    }

    CParserForMonRace parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(LOG_NORMAL, L"monrace create parser failed...")
        // L2_LOG_INFO(Log, L"monrace create parser failed...")
        L2_LOG_ERROR(Log, "Cannot initialize monrace.txt parser; monster race loading returns -1.");
        return -1;
    }

    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CMonRaceDB::LoadMonRaceData parser yyparse failed. "
        //     L"after line[%d]",
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"CMonRaceDB::LoadMonRaceData parser yyparse failed. "
        //     L"after line[%d]",
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse monrace.txt at script line %lld; registered runners retained, race-state initialization skipped "
            "and load returns -1.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
        return -1;
    }

    // Log.Add(LOG_NORMAL, L"read monrace...")
    // L2_LOG_INFO(Log, L"read monrace...")
    L2_LOG_INFO(Log, "Read monrace.txt: %zu registered runners; initializing monster race state.", m_racemon.size());
    ChangeRaceStatus(MR_CLEARED);
    InitData();
    return 0;
}

void CMonRaceDB::Insert(CMonRaceData* data) {
    // Current L2Server 0x00845014.
    L2_GUARD_FUNCTION();
    if (data != nullptr) {
        m_racemon.emplace(data->runner_id, data);
    }
}

void CMonRaceDB::SetTrackBegin(int trackId, int x, int y, int z) {
    // Current L2Server 0x00841aac.
    L2_GUARD_FUNCTION();
    m_track_begin[trackId] = FVector{
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z),
    };
}

void CMonRaceDB::SetTrackEnd(int trackId, int x, int y, int z) {
    // Current L2Server 0x00841b9c.
    L2_GUARD_FUNCTION();
    m_track_end[trackId] = FVector{
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z),
    };
}

void CMonRaceDB::AddRaceAreaInfo(int remoteFee, std::vector<FVector> points) {
    // Current L2Server 0x0084d580.
    L2_GUARD_FUNCTION();
    RaceAreaInfo info{remoteFee, points};
    m_RaceAreas.push_back(info);
}

CMonRaceDB::CMonRaceDB() {
    std::memset(m_szMusic, 0, sizeof(m_szMusic));
    std::memset(m_szSound, 0, sizeof(m_szSound));
    m_nTaxRate = 0;
    m_nReturnRate = 0;
    m_nResidenceID = 0;
    m_dwRequestCreateTick = 0;
    m_keeperloc.clear();
    m_remotefee = 0;
}

CMonRaceDB::~CMonRaceDB() = default;

void CMonRaceDB::Init() {
    // Current L2Server 0x00845b00. The initial cleared state is published
    // before the current script rows are replayed to CacheD, in map order.
    L2_GUARD_FUNCTION();
    m_nRaceId = 0;
    m_nServerId = 0;
    static_cast<void>(ChangeRaceStatus(MR_CLEARED));
    InitData();
    for (const auto& entry : m_racemon) {
        CMonRaceData* const data = entry.second;
        db.RequestCreateMonRaceMon(data->runner_id, data->initial_win, data->m_runcount, data->m_wincount);
    }
}

int CMonRaceDB::GetTaxRate() {
    // Current L2Server 0x0084199c.
    return m_nTaxRate;
}

int CMonRaceDB::GetRaceFeeRate(const FVector position) {
    // Current L2Server 0x00845e70. The extrema survive an
    // empty area entry, matching the target's locals outside the outer loop.
    L2_GUARD_FUNCTION();
    int minimumX = 0;
    int maximumX = 0;
    int minimumY = 0;
    int maximumY = 0;
    for (const RaceAreaInfo& area : m_RaceAreas) {
        const std::vector<FVector> points(area.m_points);
        if (!points.empty()) {
            minimumX = static_cast<int>(runtime::CheckedVectorAt(points, 0).x);
            maximumX = minimumX;
            minimumY = static_cast<int>(runtime::CheckedVectorAt(points, 0).y);
            maximumY = minimumY;
        }
        for (const FVector& point : points) {
            const int x = static_cast<int>(point.x);
            const int y = static_cast<int>(point.y);
            if (x < minimumX) {
                minimumX = x;
            }
            if (maximumX < x) {
                maximumX = x;
            }
            if (y < minimumY) {
                minimumY = y;
            }
            if (maximumY < y) {
                maximumY = y;
            }
        }
        const int x = static_cast<int>(position.x);
        const int y = static_cast<int>(position.y);
        if (minimumX <= x && x <= maximumX && minimumY <= y && y <= maximumY) {
            return area.m_nRemoteFee;
        }
    }
    return 0;
}

int CMonRaceDB::GetRaceFee(const FVector position, const int amount) {
    // Current L2Server 0x00846020.
    L2_GUARD_FUNCTION();
    return runtime::MultiplyInt32Wrapping(GetRaceFeeRate(position), amount) / 100;
}

bool CMonRaceDB::RequestCreateMonTicket(
    const int raceId,
    const int ticketType,
    const int firstLaneValue,
    const int secondLaneValue,
    const int,
    const int amount,
    User* const user
) {
    // Current L2Server 0x008462ac.
    L2_GUARD_FUNCTION();
    if (m_status != MR_SELL_TICKET || m_nRaceId < 1 || m_nRaceId != raceId || user == nullptr) {
        return false;
    }

    int tax = 0;
    if (m_nTaxRate != 0) {
        tax = runtime::MultiplyInt32Wrapping(m_nTaxRate, amount) / 100;
    }
    const unsigned int remoteFee = static_cast<unsigned int>(GetRaceFeeRate(user->GetPos()));
    unsigned int firstLane = static_cast<unsigned int>(firstLaneValue);
    const unsigned int secondLane = static_cast<unsigned int>(secondLaneValue);
    const int remoteCharge = runtime::MultiplyInt32Wrapping(runtime::Int32FromBits(remoteFee), amount) / 100;
    const int total = runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(tax, remoteCharge), amount);

    if (ticketType == 1) {
        if (user->m_inv.GetAdenaAmount() < total) {
            user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
            return false;
        }
        return user->BuyMonRaceTicket(0x115b, raceId, ((remoteFee & 0xffU) << 8U) | firstLane, amount);
    }
    if (ticketType != 2) {
        return false;
    }
    if (user->m_inv.GetAdenaAmount() < total) {
        user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
        return false;
    }
    unsigned int maximumLane = secondLane;
    if (static_cast<int>(secondLane) < static_cast<int>(firstLane)) {
        maximumLane = firstLane;
        firstLane = secondLane;
    }
    return user->BuyMonRaceTicket(0x115c, raceId, ((remoteFee & 0xffU) << 8U) | (firstLane * 0x10U + maximumLane), amount);
}

int CMonRaceDB::GetTotalReturnMoney(const int amount, const int type) {
    // Current L2Server 0x008419a4.
    L2_GUARD_FUNCTION();
    int tax = 0;
    if (type == 1) {
        m_total_bet1 = amount;
        if (amount > 0) {
            tax = (m_nReturnRate * amount) / 100;
        }
        m_total_del1 = amount - tax;
    } else if (type == 2) {
        m_total_bet2 = amount;
        if (amount > 0) {
            tax = (m_nReturnRate * amount) / 100;
        }
        m_total_del2 = amount - tax;
    }
    return tax;
}

int CMonRaceDB::GetConfirmTime() {
    L2_GUARD_FUNCTION();

    if (m_status == MR_RUN) {
        return static_cast<int>(m_runtime[m_rank[0]] * 1000.0);
    }
    return 0;
}

int CMonRaceDB::GetElapsedTime() {
    L2_GUARD_FUNCTION();

    if (m_status == MR_AFTER_SELL || m_status == MR_SELL_TICKET) {
        return -1;
    }
    if (m_dwBeginTick != 0) {
        return runtime::Int32FromBits(runtime::GetTickCount() - m_dwBeginTick);
    }
    return 0;
}

bool CMonRaceDB::MakeRacePacket() {
    L2_GUARD_FUNCTION();

    if (m_status != MR_CONFIRM && m_status != MR_SELL_TICKET && m_status != MR_AFTER_SELL && m_status != MR_RUN) {
        return false;
    }

    const std::uint32_t currentTick = runtime::GetTickCount();
    {
        L2_GUARD(mLock);

        if (!m_bStart) {
            m_dwBeginTick = currentTick;
            m_bStart = true;
            m_racebuffersize = 0;
            std::memset(m_racebuffer, 0, sizeof(m_racebuffer));

            int packetSize = 0;
            const int runnerCountSize =
                ::Assemble(m_racebuffer, static_cast<int>(sizeof(m_racebuffer)), "d", m_runner.size());
            if (runnerCountSize > 0) {
                packetSize = runnerCountSize;
            }

            for (std::size_t runnerIndex = 0; runnerIndex < m_runner.size(); ++runnerIndex) {
                CMonRaceData* const runner = runtime::CheckedVectorAt(m_runner, runnerIndex);
                const FVector trackBegin = m_track_begin[runnerIndex];
                const FVector trackEnd = m_track_end[runnerIndex];
                const std::int32_t npcClassId = runner->npc_classid + 1000000;
                CObject* const object = g_ObjectDB.GetObjectW(npcClassId, false);
                if (object == nullptr) {
                    return false;
                }

                CNPC* const npc = static_cast<CNPC*>(object);
                const int runnerSize = ::Assemble(
                    m_racebuffer + packetSize, static_cast<int>(sizeof(m_racebuffer)) - packetSize, "ddddddddffd",
                    m_serverid[runnerIndex], npcClassId, static_cast<int>(trackBegin.x), static_cast<int>(trackBegin.y),
                    static_cast<int>(trackBegin.z), static_cast<int>(trackEnd.x), static_cast<int>(trackEnd.y),
                    static_cast<int>(trackEnd.z), npc->m_data->m_dCollisionRadius, npc->m_data->m_dCollisionHeight,
                    npc->m_pClassConst->m_SpeedByME[1][0]
                );
                if (runnerSize > 0) {
                    packetSize += runnerSize;
                }

                for (std::size_t speedIndex = 0; speedIndex < 20; ++speedIndex) {
                    const int speedSize = ::Assemble(
                        m_racebuffer + packetSize, static_cast<int>(sizeof(m_racebuffer)) - packetSize, "c",
                        SignedLowByte(m_speed[runnerIndex][speedIndex])
                    );
                    if (speedSize > 0) {
                        packetSize += speedSize;
                    }
                }
            }

            m_racebuffersize = packetSize;
        }
    }
    return true;
}

bool CMonRaceDB::SendRacePacket(User* user) {
    L2_GUARD_FUNCTION();

    const int status = static_cast<int>(m_status);
    if (user == nullptr || !(status < 1 || (status > 2 && status != MR_END))) {
        return false;
    }

    const bool madePacket = MakeRacePacket();
    if (madePacket) {
        const int elapsedTime = GetElapsedTime();
        const int confirmTime = GetConfirmTime();
        const int raceBufferSize = m_racebuffersize;
        char raceBuffer[8192]{};
        std::memcpy(raceBuffer, m_racebuffer, sizeof(raceBuffer));

        // Log.Add(LOG_DEBUG, L"SendRacePacket")
        // L2_LOG_DEBUG(Log, L"SendRacePacket")
        // Noise: Per-recipient race-packet send notification adds no failure information. Packet contents and send behavior
        // remain unchanged.
        user->GetSocket()->Send("cddb", 0xe3, elapsedTime, confirmTime, raceBufferSize, raceBuffer);
    }
    return madePacket;
}

bool CMonRaceDB::CheckRaceManagerLoc(const FVector position) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(mLock);
        KeeperLoc keeper{};
        keeper.m_loc = position;
        keeper.m_radius = static_cast<int>(position.y) < 100000 ? 0x100 : 0x4000;

        bool append = m_keeperloc.empty();
        if (!append) {
            for (const KeeperLoc& existing : m_keeperloc) {
                const double dx = position.x - existing.m_loc.x;
                const double dy = position.y - existing.m_loc.y;
                const double dz = position.z - existing.m_loc.z;
                if (std::sqrt(dx * dx + dy * dy + dz * dz) > 32768.0) {
                    append = true;
                    break;
                }
            }
        }
        if (append) {
            m_keeperloc.push_back(keeper);
        }
    }
    return true;
}

int CMonRaceDB::GetLastTime() {
    // Current L2Server 0x00841ea0.
    L2_GUARD_FUNCTION();
    if (m_status == MR_RUN || m_status == MR_CONFIRM || m_status == MR_END) {
        return static_cast<int>(m_runtime[m_rank[7]] * 1000.0);
    }
    return 0;
}

int CMonRaceDB::GetValidTime() {
    // Current L2Server 0x00841df4.
    L2_GUARD_FUNCTION();
    if (m_status == MR_RUN) {
        return GetConfirmTime();
    }
    return m_status == MR_CONFIRM ? -1 : 0;
}

bool CMonRaceDB::RequestEndBuyTicket() {
    // Current L2Server 0x00842060.
    L2_GUARD_FUNCTION();
    if (m_status == MR_AFTER_SELL || m_nRaceId > 0) {
        db.RequestGetMonRaceBet(m_nRaceId);
    }
    return true;
}

bool CMonRaceDB::RequestEndRace2(
    int raceId,
    double runtime0,
    double runtime1,
    double runtime2,
    double runtime3,
    double runtime4,
    double runtime5,
    double runtime6,
    double runtime7,
    double firstRuntime,
    double secondRuntime,
    int firstRank,
    int secondRank,
    double firstWinRate,
    double pairWinRate
) {
    // Current L2Server 0x00842120 returns false even after it
    // queues the CacheD request.
    L2_GUARD_FUNCTION();
    if (m_status == MR_END && m_nRaceId == raceId && raceId > 0) {
        db.RequestEndMonRace(
            raceId, runtime0, runtime1, runtime2, runtime3, runtime4, runtime5, runtime6, runtime7, firstRuntime,
            secondRuntime, firstRank, secondRank, firstWinRate, pairWinRate
        );
    }
    return false;
}

bool CMonRaceDB::CheckRunnerCond() {
    // Current L2Server 0x008429d8.
    L2_GUARD_FUNCTION();
    if (m_runner.size() < 8) {
        return false;
    }
    for (CMonRaceData* const runner : m_runner) {
        const int condition = static_cast<int>(::genrand64_real2() * 3.0 + 1.0);
        if (condition == 1 || condition == 2 || condition == 3) {
            runner->condition = condition;
        }
    }
    return true;
}

bool CMonRaceDB::CalcRunnerSpeed() {
    // Current L2Server 0x00842b24.
    L2_GUARD_FUNCTION();
    for (std::size_t runnerIndex = 0; runnerIndex < m_runner.size(); ++runnerIndex) {
        CMonRaceData* const runner = runtime::CheckedVectorAt(m_runner, runnerIndex);
        double totalRuntime = 0.0;
        for (std::size_t section = 0; section < 20; ++section) {
            int minimum = runner->min_speed;
            int maximum = runner->max_speed;
            if (runner->condition == 1) {
                minimum -= 10;
            } else if (runner->condition == 3) {
                maximum += 10;
            }

            if (static_cast<int>(::genrand64_real2() * 100.0 + 1.0) <= runner->guts[section]) {
                m_guts[runnerIndex][section] = true;
            }

            int speed = 0;
            if (!m_guts[runnerIndex][section] && (section == 0 || !m_guts[runnerIndex][section - 1])) {
                const int lower = std::min(minimum, maximum);
                const int upper = std::max(minimum, maximum);
                speed = static_cast<int>(
                    ::genrand64_real2() * static_cast<double>(upper - lower + 1) + static_cast<double>(lower)
                );
            } else {
                speed = maximum + 20;
            }
            m_speed[runnerIndex][section] = speed;
            totalRuntime += 100.0 / static_cast<double>(speed);
        }
        m_runtime[runnerIndex] = totalRuntime;
        runner->m_totalruntime += totalRuntime;
        m_rank[runnerIndex] = static_cast<int>(runnerIndex);
    }
    return true;
}

bool CMonRaceDB::ModOrder(int first) {
    // Current L2Server 0x00842d8c.
    L2_GUARD_FUNCTION();
    const int second = first + 1;
    if (m_runtime[m_rank[first]] == m_runtime[m_rank[second]]) {
        int left = first;
        int right = second;
        if (static_cast<int>(::genrand64_real2() * 2.0 + 1.0) != 1) {
            left = second;
            right = first;
        }
        std::swap(m_rank[left], m_rank[right]);
        m_speed[m_rank[left]][19] += 10;

        for (std::size_t runnerIndex = 0; runnerIndex < m_runner.size(); ++runnerIndex) {
            double runtime = 0.0;
            for (int section = 0; section < 20; ++section) {
                runtime += 100.0 / static_cast<double>(m_speed[runnerIndex][section]);
            }
            m_runtime[runnerIndex] = runtime;
        }
        // Log.Add(
        //     LOG_NORMAL,
        //     L"MonRace nFirst(%d), same speed[%d, %f] == [%d, %f]",
        //     first,
        //     m_rank[right],
        //     m_runtime[m_rank[right]],
        //     m_rank[left],
        //     m_runtime[m_rank[left]])
        // L2_LOG_INFO(Log,
        //     L"MonRace nFirst(%d), same speed[%d, %f] == [%d, %f]",
        //     first,
        //     m_rank[right],
        //     m_runtime[m_rank[right]],
        //     m_rank[left],
        //     m_runtime[m_rank[left]])
        L2_LOG_INFO(
            Log,
            "Monster race tie at rank index %d adjusted: runner indices %d and %d now have runtimes %.17g and %.17g "
            "seconds; sorting again.",
            first, m_rank[right], m_rank[left], m_runtime[m_rank[right]], m_runtime[m_rank[left]]
        );
        SortRunner();
    }
    return true;
}

bool CMonRaceDB::SortRunner() {
    // Current L2Server 0x00842f90.
    L2_GUARD_FUNCTION();
    for (std::size_t first = 0; first < m_runner.size(); ++first) {
        for (std::size_t second = first; second < m_runner.size(); ++second) {
            const int firstRunner = m_rank[first];
            const int secondRunner = m_rank[second];
            if (m_runtime[secondRunner] <= m_runtime[firstRunner] && m_runtime[firstRunner] != m_runtime[secondRunner]) {
                std::swap(m_rank[first], m_rank[second]);
            }
        }
    }
    ModOrder(0);
    ModOrder(1);
    return true;
}

bool CMonRaceDB::RunRace() {
    // Current L2Server 0x008430cc.
    L2_GUARD_FUNCTION();
    m_bStart = false;
    CalcRunnerSpeed();
    SortRunner();
    return true;
}

bool CMonRaceDB::RequestEndRace() {
    // Current L2Server 0x00843698.
    L2_GUARD_FUNCTION();
    int first = m_rank[0];
    int second = m_rank[1];
    if (first >= 8 || second >= 8) {
        return false;
    }

    const double firstWinRate = m_winrate1[first];
    const int low = std::min(first, second);
    const int high = std::max(first, second);
    const double pairWinRate = m_winrate2[low][high];

    for (CMonRaceData* const runner : m_runner) {
        if (runner != nullptr) {
            ++runner->m_runcount;
        }
    }
    ++runtime::CheckedVectorAt(m_runner, low)->m_wincount;

    RequestEndRace2(
        m_nRaceId, m_runtime[0], m_runtime[1], m_runtime[2], m_runtime[3], m_runtime[4], m_runtime[5], m_runtime[6],
        m_runtime[7], m_runtime[m_rank[0]], m_runtime[m_rank[1]], m_rank[0] + 1, m_rank[1] + 1, firstWinRate, pairWinRate
    );
    return true;
}

bool CMonRaceDB::RequestUpdateMon() {
    // Current L2Server 0x008438b0.
    L2_GUARD_FUNCTION();
    if (m_runner.size() < 8 || m_status != MR_END) {
        return false;
    }
    for (CMonRaceData* const runner : m_runner) {
        if (runner == nullptr) {
            continue;
        }
        const int condition = runner->m_runcount < 1 ? 0 : runner->m_wincount / runner->m_runcount;
        runner->condition = condition;
        db.RequestUpdateMonRaceMon(runner->runner_id, condition, runner->m_runcount, runner->m_wincount);
    }
    return true;
}

void CMonRaceDB::BroadCastSound() {
    // Current L2Server 0x00844018.
    L2_GUARD_FUNCTION();
    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(const wchar_t* sound) : m_sound(sound) {}

        bool CanDo(CObject* object, double) override {
            User* const user = object->IsPC() ? object->CastUser() : nullptr;
            if (user != nullptr && !object->IsDeleting() && object->m_Deleted == 0) {
                const FVector position = user->GetPos();
                user->PlaySound(
                    m_sound, 1, static_cast<int>(user->Id()), static_cast<int>(position.x), static_cast<int>(position.y),
                    static_cast<int>(position.z)
                );
            }
            return true;
        }

    private:
        const wchar_t* m_sound;
    };

    const std::wstring sound = Utf16ToWide(m_szSound);
    TLMFunc sender(sound.c_str());
    for (const KeeperLoc& keeper : m_keeperloc) {
        if (keeper.m_loc.IsZero()) {
            return;
        }
        CTLMFilterRect filter(0, keeper.m_loc, keeper.m_radius);
        FOR_ALL_NEIGHBOR_USER_RECT(&filter, &sender);
    }
}

void CMonRaceDB::BroadCastMusic() {
    // Current L2Server 0x0084414c.
    L2_GUARD_FUNCTION();
    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(const wchar_t* music) : m_music(music) {}

        bool CanDo(CObject* object, double) override {
            User* const user = object->IsPC() ? object->CastUser() : nullptr;
            if (user != nullptr && !object->IsDeleting() && object->m_Deleted == 0) {
                user->PlayMusic(WideToUtf16(m_music).c_str());
            }
            return true;
        }

    private:
        const wchar_t* m_music;
    };

    const std::wstring music = Utf16ToWide(m_szMusic);
    TLMFunc sender(music.c_str());
    for (const KeeperLoc& keeper : m_keeperloc) {
        if (keeper.m_loc.IsZero()) {
            return;
        }
        CTLMFilterRect filter(0, keeper.m_loc, keeper.m_radius);
        FOR_ALL_NEIGHBOR_USER_RECT(&filter, &sender);
    }
}

bool CMonRaceDB::RunTrack() {
    // Current L2Server 0x00844340.
    L2_GUARD_FUNCTION();
    if (m_status != MR_RUN && m_status != MR_CONFIRM) {
        return true;
    }

    const int elapsed = GetElapsedTime();
    for (int runnerIndex = 0; runnerIndex < 8; ++runnerIndex) {
        const int serverIndex = m_serverindex[runnerIndex];
        const FVector begin = m_track_begin[runnerIndex];
        const FVector end = m_track_end[runnerIndex];
        const double duration = m_runtime[runnerIndex] * 1000.0;
        const double ratio = duration == 0.0 ? 0.0 : static_cast<double>(elapsed) / duration;
        if (serverIndex == 0) {
            continue;
        }

        // Current L2Server 0x0084447a..0x008444a1 passes the value from
        // m_serverindex[] to CCreatureFactory::FindObjectSP. It is a
        // factory index, not a smart ID accepted by CCreature::FindObjectSP.
        CSPointer<CCreature> retained = CCreatureFactory::Inst().FindObjectSP(serverIndex);
        CCreature* const creature = retained.get();
        if (creature == nullptr || !creature->IsNPC()) {
            continue;
        }

        FVector delta{
            (end.x - begin.x) * ratio,
            (end.y - begin.y) * ratio,
            (end.z - begin.z) * ratio,
        };
        if (duration < static_cast<double>(elapsed)) {
            delta = FVector{
                end.x - begin.x,
                end.y - begin.y,
                end.z - begin.z,
            };
        }
        const FVector position{
            begin.x + delta.x,
            begin.y + delta.y,
            begin.z + delta.z,
        };
        creature->m_data->SetPos(position);
        CWorld::Inst().MoveTo(creature, position, false);
    }
    return true;
}

void CMonRaceDB::InitData() {
    // Current L2Server 0x00844678.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(mLock);
        m_dwCreateTick = 0;
        m_dwBeginTick = 0;
        m_dwBroadCastTick = 0;
        m_runner.clear();
        std::memset(m_speed, 0, sizeof(m_speed));
        std::memset(m_guts, 0, sizeof(m_guts));
        std::memset(m_rank, 0, sizeof(m_rank));
        std::memset(m_serverindex, 0, sizeof(m_serverindex));
        std::memset(m_serverid, 0, sizeof(m_serverid));
        std::memset(m_racebuffer, 0, sizeof(m_racebuffer));
        std::memset(m_winrate1, 0, sizeof(m_winrate1));
        std::memset(m_winrate2, 0, sizeof(m_winrate2));
        std::memset(m_runtime, 0, sizeof(m_runtime));
        m_bStart = false;
        m_racebuffersize = 0;
        m_nBroadCast = 0;
        m_nCountDown = 0;
        m_nLastCountDown = 0;
        m_total_bet1 = 0;
        m_total_bet2 = 0;
        m_total_del1 = 0;
        m_total_del2 = 0;
        m_nTaxRate = 0;
    }
}

bool CMonRaceDB::EndRace() {
    // Current L2Server 0x00844988.
    L2_GUARD_FUNCTION();
    if (m_status != MR_CLEARED) {
        return false;
    }
    for (int runnerIndex = 0; runnerIndex < 8; ++runnerIndex) {
        if (m_serverindex[runnerIndex] == 0) {
            continue;
        }
        // Current L2Server 0x00844a49..0x00844a72 resolves each value from
        // m_serverindex[] through CCreatureFactory::FindObjectSP. Treating
        // the index as a smart ID makes the real runner lookup fail.
        CSPointer<CCreature> retained = CCreatureFactory::Inst().FindObjectSP(m_serverindex[runnerIndex]);
        CCreature* const creature = retained.get();
        if (creature == nullptr || !creature->IsNPC()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] not a creature",
            //     L"MonRaceData.cpp",
            //     0x5c0)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] not a creature",
            //     L"MonRaceData.cpp",
            //     0x5c0)
            L2_LOG_ERROR(
                Log,
                "Monster race cleanup stopped at runner index %d: creature index %u is missing or not an NPC; earlier "
                "deletions remain.",
                runnerIndex, static_cast<unsigned>(m_serverindex[runnerIndex])
            );
            return false;
        }
        creature->Delete();
    }
    m_dwCreateTick = 0;
    InitData();
    return true;
}

void CMonRaceDB::SetInitialWin(const int runnerId, const int initialWin, const int runCount, const int winCount) {
    // Current L2Server 0x00844870.
    L2_GUARD_FUNCTION();
    const auto found = m_racemon.find(runnerId);
    if (found != m_racemon.end()) {
        CMonRaceData* const runner = found->second;
        runner->initial_win = initialWin;
        runner->m_runcount = runCount;
        runner->m_wincount = winCount;
    }
}

bool CMonRaceDB::BroadCastRace(int elapsedTime) {
    // Current L2Server 0x00844d04.
    L2_GUARD_FUNCTION();
    const int status = static_cast<int>(m_status);
    if (!(status < 1 || (status > 2 && status != MR_END)) || !MakeRacePacket()) {
        return false;
    }

    int elapsed = GetElapsedTime();
    int valid = GetValidTime();
    if (m_status == MR_RUN) {
        valid -= 2000;
        elapsed = elapsedTime;
    }
    const int packetSize = m_racebuffersize;
    char packet[8192]{};
    std::memcpy(packet, m_racebuffer, sizeof(packet));
    // Log.Add(LOG_DEBUG, L"BroadCastRace ")
    // L2_LOG_DEBUG(Log, L"BroadCastRace ")
    // Noise: Routine race-state broadcasts repeat throughout the race and do not report success or failure of delivery.
    BroadcastToRaceKeepers(m_keeperloc, "cddb", 0xe3, elapsed, valid, packetSize, packet);
    return true;
}

bool CMonRaceDB::ChangeRaceStatus(MONRACESTATUS status) {
    // Current L2Server 0x008450e4.
    L2_GUARD_FUNCTION();
    switch (status) {
        case MR_PREPARE:
            m_status = MR_PREPARE;
            break;
        case MR_BEFORE_SELL:
            if (m_status != MR_PREPARE) {
                return false;
            }
            m_status = MR_BEFORE_SELL;
            break;
        case MR_SELL_TICKET:
            m_status = MR_SELL_TICKET;
            m_nBroadCast = 0;
            m_nCountDown = 0;
            m_nLastCountDown = 0;
            BroadCastRace(0);
            BroadcastToRaceKeepers(m_keeperloc, "chccd", 0x62, 0x330, 1, 1, m_nRaceId);
            break;
        case MR_AFTER_SELL:
            if (m_status != MR_SELL_TICKET) {
                return false;
            }
            m_status = MR_AFTER_SELL;
            m_nBroadCast = 0;
            m_nCountDown = 0;
            m_nLastCountDown = 0;
            RequestEndBuyTicket();
            BroadcastToRaceKeepers(m_keeperloc, "chccd", 0x62, 0x333, 1, 1, m_nRaceId);
            break;
        case MR_RUN:
            m_status = MR_RUN;
            m_dwBeginTick = runtime::GetTickCount();
            BroadcastToRaceKeepers(m_keeperloc, "chc", 0x62, 0x338, 0);
            BroadCastMusic();
            BroadCastSound();
            RunRace();
            BroadCastRace(0);
            break;
        case MR_CONFIRM:
            m_status = MR_CONFIRM;
            BroadCastRace(0);
            break;
        case MR_END:
            m_status = MR_END;
            BroadcastToRaceKeepers(
                m_keeperloc, "chccccc", 0x62, 0x33a, 2, 0x14, SignedLowByte(m_rank[0] + 1), 0x14,
                SignedLowByte(m_rank[1] + 1)
            );
            RequestEndRace();
            break;
        case MR_CLEARED:
            m_status = MR_CLEARED;
            if (m_nRaceId > 0) {
                EndRace();
            }
            break;
        default:
            break;
    }
    return true;
}

bool CMonRaceDB::CheckMonRaceKeeper(int serverId, int serverIndex) {
    // Current L2Server 0x00845c38.
    L2_GUARD_FUNCTION();
    if (g_bIsNonPKServer) {
        return false;
    }

    if (serverId == 0 && serverIndex == 0) {
        {
            L2_GUARD(mLock);
            m_nRaceId = 0;
            m_nServerId = 0;
            m_nServerIndex = 0;
        }
        m_keeperloc.clear();
        ChangeRaceStatus(MR_CLEARED);
        return false;
    }

    bool result = false;
    {
        L2_GUARD(mLock);
        if (m_nServerId == 0 && m_nServerIndex == 0 && g_npcserver.Connected()) {
            m_nServerId = serverId;
            m_nServerIndex = serverIndex;
            result = true;
        }
        if (m_nServerId == serverId && m_nServerIndex == serverIndex) {
            result = true;
        }
    }
    return result;
}

bool CMonRaceDB::GetRunnerList(std::vector<CMonRaceData*>& runners, bool randomize) {
    // Current L2Server 0x008466f0.
    L2_GUARD_FUNCTION();
    runners.clear();
    if (!randomize) {
        {
            L2_GUARD(mLock);
            if (m_runner.size() < 8) {
                return false;
            }
            for (std::size_t index = 0; index < 8; ++index) {
                runners.push_back(runtime::CheckedVectorAt(m_runner, index));
            }
        }
    } else {
        int selected = 0;
        while (selected < 8) {
            int high = static_cast<int>(m_racemon.size());
            int runnerId = 1;
            if (high < 1) {
                // Log.Add(LOG_ERROR, L"nHigh [%d] in Random", high)
                // L2_LOG_ERROR(Log, L"nHigh [%d] in Random", high)
                L2_LOG_ERROR(
                    Log,
                    "Monster race random runner selection has invalid candidate count %d; attempting fallback runner ID 1.",
                    high
                );
            } else {
                runnerId = static_cast<int>(::genrand64_real2() * static_cast<double>(high) + 1.0);
            }

            bool duplicate = false;
            for (CMonRaceData* const runner : runners) {
                if (runner != nullptr && runner->runner_id == runnerId) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            ++selected;
            const auto found = m_racemon.find(runnerId);
            if (found != m_racemon.end()) {
                runners.push_back(found->second);
            }
        }
    }

    if (runners.size() < 8) {
        runners.clear();
        return false;
    }
    return true;
}

bool CMonRaceDB::RequestCreateMonRace() {
    // Current L2Server 0x008469e8.
    L2_GUARD_FUNCTION();
    const std::uint32_t now = runtime::GetTickCount();
    if (now - m_dwRequestCreateTick < 60000U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RequestCreatMonrace crete interval is too small [%d]",
        //     static_cast<int>(now - m_dwRequestCreateTick))
        // L2_LOG_ERROR(Log,
        //     L"RequestCreatMonrace crete interval is too small [%d]",
        //     static_cast<int>(now - m_dwRequestCreateTick))
        L2_LOG_WARN(
            Log,
            "Monster race creation request ignored: only %u ms since the previous request; minimum interval is 60000 ms.",
            static_cast<unsigned>(now - m_dwRequestCreateTick)
        );
        return false;
    }

    m_dwRequestCreateTick = now;
    if (!ChangeRaceStatus(MR_CLEARED)) {
        return false;
    }
    m_dwBeginTick = 0;
    InitData();
    std::memset(m_speed, 0, sizeof(m_speed));
    std::memset(m_guts, 0, sizeof(m_guts));
    std::memset(m_rank, 0, sizeof(m_rank));
    if (!GetRunnerList(m_runner, true) || !CheckRunnerCond()) {
        return false;
    }

    SetTaxRate();
    db.RequestCreateMonRace(
        runtime::CheckedVectorAt(m_runner, 0)->runner_id, runtime::CheckedVectorAt(m_runner, 1)->runner_id,
        runtime::CheckedVectorAt(m_runner, 2)->runner_id, runtime::CheckedVectorAt(m_runner, 3)->runner_id,
        runtime::CheckedVectorAt(m_runner, 4)->runner_id, runtime::CheckedVectorAt(m_runner, 5)->runner_id,
        runtime::CheckedVectorAt(m_runner, 6)->runner_id, runtime::CheckedVectorAt(m_runner, 7)->runner_id, m_nTaxRate
    );
    return true;
}

bool CMonRaceDB::CreateRace(const int raceId) {
    // Current L2Server 0x0084559c.
    L2_GUARD_FUNCTION();
    m_dwCreateTick = runtime::GetTickCount();
    m_dwBeginTick = 0;
    m_nRaceId = raceId;

    for (std::size_t runnerIndex = 0; runnerIndex < m_runner.size(); ++runnerIndex) {
        CMonRaceData* const runner = runtime::CheckedVectorAt(m_runner, runnerIndex);
        CNPC* const npc = g_ObjectDB.CreateNPC(runner->npc_classid + 1000000, true);
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"There is no NPC at file[%s], line[%d]",
            //     L"MonRaceData.cpp",
            //     0x644)
            // L2_LOG_ERROR(Log,
            //     L"There is no NPC at file[%s], line[%d]",
            //     L"MonRaceData.cpp",
            //     0x644)
            L2_LOG_ERROR(
                Log,
                "Monster race %d cannot create NPC for runner %d at index %zu (NPC template offset %d); continuing with "
                "other runners.",
                raceId, runner->runner_id, runnerIndex, runner->npc_classid
            );
            continue;
        }

        m_serverid[runnerIndex] = static_cast<int>(npc->Id());
        m_serverindex[runnerIndex] = npc->GetIndex();
        const wchar_t* const aiName = g_ObjectDB.GetAI(npc->m_nClassId);
        if (aiName == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "AI does not specified [clsid=%d]",
            //     npc->m_nClassId)
            // L2_LOG_ERROR(Log,
            //     "AI does not specified [clsid=%d]",
            //     npc->m_nClassId)
            L2_LOG_ERROR(
                Log,
                "Monster race %d NPC class %d has no AI: runner index %zu is skipped after its object and creature indices "
                "were stored.",
                raceId, npc->m_nClassId, runnerIndex
            );
            continue;
        }

        std::array<char, 0x2400> parameterBuffer;
        int bufferLength = 0;
        int parameterCount = 0;
        for (const auto& parameter : npc->m_pClassConst->m_mapParameter) {
            const int assembled = ::Assemble(
                parameterBuffer.data() + bufferLength, static_cast<int>(parameterBuffer.size()) - bufferLength, "SS",
                parameter.first.c_str(), parameter.second.c_str()
            );
            ++parameterCount;
            if (assembled > 0) {
                bufferLength += assembled;
            }
        }

        const FVector trackBegin = m_track_begin[runnerIndex];
        const FVector trackEnd = m_track_end[runnerIndex];
        NpcServer::NPCCreateParam parameter{};
        parameter.pNPC = npc;
        parameter.pos = trackBegin;
        parameter.dir =
            static_cast<int>(
                std::atan2(trackEnd.y - trackBegin.y, trackEnd.x - trackBegin.x) * 360.0 / 6.283185307179586 + 360.0
            ) %
            360;
        parameter.aiName = const_cast<wchar_t*>(aiName);
        parameter.paramCount = parameterCount;
        parameter.bufLen = bufferLength;
        parameter.pBuf = parameterBuffer.data();
        g_npcserver.SendNPCCreatePacket(parameter);
        npc->OnCreated();
    }

    ChangeRaceStatus(MR_SELL_TICKET);
    return true;
}

void CMonRaceDB::SetTaxRate() {
    // Current L2Server 0x008418c4.
    L2_GUARD_FUNCTION();
    m_nTaxRate = 0;
    if (m_nResidenceID == 0) {
        return;
    }
    CResidence* const residence = g_ResidenceDB.GetResidence(m_nResidenceID);
    if (residence == nullptr) {
        return;
    }
    const int residenceType = residence->m_data->m_ResidenceType;
    m_nTaxRate = static_cast<int>(residence->m_data->m_TaxInfoData[static_cast<std::size_t>(residenceType)].m_nTaxRate);
}

bool CMonRaceDB::TimerCalled() {
    // Current L2Server 0x00848d40.
    L2_GUARD_FUNCTION();
    if (g_bIsNonPKServer || g_bCannotMonRace) {
        return false;
    }

    const MONRACESTATUS status = m_status;
    const int confirmTime = GetConfirmTime();
    const int elapsedTime = GetElapsedTime();
    const int lastTime = GetLastTime();
    const int afterCreate = m_dwCreateTick == 0 ? 0 : runtime::Int32FromBits(runtime::GetTickCount() - m_dwCreateTick);

    std::tm local{};
    const bool hasLocalTime = runtime::crt::LocalTime32(runtime::crt::Time32Now(), local);

    enum class RaceTransition { None, Create, EndTicketSales, Run };
    const RaceTransition transition = [&] {
        L2_GUARD(mLock);
        if (status == MR_CLEARED && hasLocalTime && (local.tm_min == 0 || local.tm_min == 20 || local.tm_min == 40)) {
            ChangeRaceStatus(MR_PREPARE);
            return RaceTransition::Create;
        }
        if (status == MR_SELL_TICKET && afterCreate > 900000) {
            return RaceTransition::EndTicketSales;
        }
        if (status == MR_AFTER_SELL && afterCreate > 0x107ac0) {
            return RaceTransition::Run;
        }
        return RaceTransition::None;
    }();
    switch (transition) {
        case RaceTransition::Create:
            RequestCreateMonRace();
            return true;
        case RaceTransition::EndTicketSales:
            ChangeRaceStatus(MR_AFTER_SELL);
            return true;
        case RaceTransition::Run:
            ChangeRaceStatus(MR_RUN);
            return true;
        case RaceTransition::None:
            break;
    }

    if (afterCreate == 0) {
        return false;
    }
    // Log.Add(
    //     LOG_DEBUG,
    //     L"CMonRaceDB::TimerCalled runstatus[%d] elapsed[%d], "
    //     L"last[%d], confirm[%d], nAfterCreate[%d]",
    //     static_cast<int>(status),
    //     elapsedTime,
    //     lastTime,
    //     confirmTime,
    //     afterCreate)
    // L2_LOG_DEBUG(Log,
    //     L"CMonRaceDB::TimerCalled runstatus[%d] elapsed[%d], "
    //     L"last[%d], confirm[%d], nAfterCreate[%d]",
    //     static_cast<int>(status),
    //     elapsedTime,
    //     lastTime,
    //     confirmTime,
    //     afterCreate)
    // Noise: Per-timer state snapshots repeat without a transition. Keep the transition diagnostics below instead.

    if (status == MR_SELL_TICKET) {
        const std::uint32_t now = runtime::GetTickCount();
        if (now - m_dwBroadCastTick > 30000U) {
            m_dwBroadCastTick = now;
            BroadcastToRaceKeepers(m_keeperloc, "chccd", 0x62, 0x331, 1, 1, m_nRaceId);
        }
        if (afterCreate > 300000 && m_nBroadCast == 0) {
            m_nBroadCast = 1;
            BroadcastToRaceKeepers(m_keeperloc, "chccc", 0x62, 0x1ff4, 1, 0x14, 10);
        }
        if (afterCreate > 600000 && m_nBroadCast == 1) {
            m_nBroadCast = 2;
            BroadcastToRaceKeepers(m_keeperloc, "chccc", 0x62, 0x332, 1, 0x14, 5);
        }
        if (afterCreate > 840000 && m_nBroadCast == 2) {
            m_nBroadCast = 3;
            BroadcastToRaceKeepers(m_keeperloc, "chccc", 0x62, 0x332, 1, 0x14, 1);
        }
        return true;
    }

    if (status == MR_AFTER_SELL) {
        int remaining = 0x107ac0 - afterCreate;
        if (m_nCountDown == 0) {
            if (remaining < 7000 && m_nBroadCast == 4) {
                m_nCountDown = 1;
            } else if (remaining < 10000 && m_nBroadCast == 3) {
                m_nBroadCast = 4;
                BroadcastToRaceKeepers(m_keeperloc, "chccd", 0x62, 0x336, 1, 1, m_nRaceId);
            } else if (remaining < 30000 && m_nBroadCast == 0) {
                m_nBroadCast = 3;
                BroadcastToRaceKeepers(m_keeperloc, "chccd", 0x62, 0x335, 1, 1, m_nRaceId);
            } else if (remaining > 60000 && runtime::GetTickCount() - m_dwBroadCastTick > 60000U) {
                m_dwBroadCastTick = runtime::GetTickCount();
                int rounded = remaining - remaining % 60000;
                if (remaining % 60000 > 30000) {
                    rounded += 60000;
                }
                BroadcastToRaceKeepers(
                    m_keeperloc, "chccccd", 0x62, 0x334, 2, 0x14, SignedLowByte(rounded / 60000), 1, m_nRaceId
                );
            }
            return true;
        }

        remaining /= 1000;
        if (remaining < 6 && m_nLastCountDown != remaining && remaining > 0) {
            m_nLastCountDown = remaining;
            BroadcastToRaceKeepers(m_keeperloc, "chccc", 0x62, 0x337, 1, 0x14, SignedLowByte(remaining));
        }
        return true;
    }

    if (status == MR_RUN) {
        if (elapsedTime > 0 && lastTime > elapsedTime) {
            RunTrack();
        }
        if (elapsedTime < lastTime && elapsedTime >= confirmTime - 4000) {
            // Log.Add(
            //     LOG_DEBUG,
            //     L"CMonRaceDB::TimerCalled change MR_CONFIRM")
            // L2_LOG_DEBUG(Log,
            //     L"CMonRaceDB::TimerCalled change MR_CONFIRM")
            L2_LOG_INFO(
                Log,
                "Monster race %d requests transition from running to confirming results (elapsed %d ms, confirmation %d "
                "ms).",
                m_nRaceId, elapsedTime, confirmTime
            );
            ChangeRaceStatus(MR_CONFIRM);
        } else if (lastTime <= elapsedTime) {
            // Log.Add(
            //     LOG_DEBUG,
            //     L"CMonRaceDB::TimerCalled change MR_END")
            // L2_LOG_DEBUG(Log,
            //     L"CMonRaceDB::TimerCalled change MR_END")
            L2_LOG_INFO(
                Log, "Monster race %d requests transition from running to ended (elapsed %d ms, last runner %d ms).",
                m_nRaceId, elapsedTime, lastTime
            );
            ChangeRaceStatus(MR_END);
        }
        return true;
    }

    if (status == MR_CONFIRM) {
        if (elapsedTime >= lastTime) {
            // Log.Add(
            //     LOG_DEBUG,
            //     L"CMonRaceDB::TimerCalled change MR_END")
            // L2_LOG_DEBUG(Log,
            //     L"CMonRaceDB::TimerCalled change MR_END")
            L2_LOG_INFO(
                Log,
                "Monster race %d requests transition from confirming results to ended (elapsed %d ms, last runner %d ms).",
                m_nRaceId, elapsedTime, lastTime
            );
            ChangeRaceStatus(MR_END);
        }
        return true;
    }

    if (status == MR_END) {
        const std::uint32_t now = runtime::GetTickCount();
        if (now - m_dwBroadCastTick > 3000U) {
            m_dwBroadCastTick = now;
        }
        if (elapsedTime >= 60000) {
            // Log.Add(
            //     LOG_DEBUG,
            //     L"CMonRaceDB::TimerCalled change MR_CLEARED")
            // L2_LOG_DEBUG(Log,
            //     L"CMonRaceDB::TimerCalled change MR_CLEARED")
            L2_LOG_INFO(
                Log, "Monster race %d requests transition from ended to cleared (elapsed %d ms).", m_nRaceId, elapsedTime
            );
            ChangeRaceStatus(MR_CLEARED);
        }
    }
    return true;
}

CMonRacePacket::CMonRacePacket(const int type) : m_nType(type) {
    // Current L2Server 0x008422c8.
    L2_GUARD_FUNCTION();
}

bool CMonRacePacket::BettingResult(
    int firstCount, const int firstTotal, int secondCount, const int secondTotal, const unsigned char* packet
) {
    // Current L2Server 0x00842368.
    L2_GUARD_FUNCTION();
    if (firstCount < 1 && secondCount < 1) {
        return false;
    }

    int firstLane = 0;
    int secondLane = 0;
    int amount = 0;
    const int firstReturn = g_MonRaceDB.GetTotalReturnMoney(firstTotal, 1);
    const int secondReturn = g_MonRaceDB.GetTotalReturnMoney(secondTotal, 2);
    while (firstCount > 0) {
        --firstCount;
        packet = ::Disassemble(packet, "dd", &firstLane, &amount);
        double rate = static_cast<double>(firstReturn) / static_cast<double>(amount);
        if (rate <= 1.25) {
            rate = 1.25;
        }
        g_MonRaceDB.m_winrate1[firstLane - 1] = rate;
    }
    while (secondCount > 0) {
        --secondCount;
        packet = ::Disassemble(packet, "ddd", &firstLane, &secondLane, &amount);
        double rate = static_cast<double>(secondReturn) / static_cast<double>(amount);
        if (rate <= 1.25) {
            rate = 1.25;
        }
        g_MonRaceDB.m_winrate2[firstLane - 1][secondLane - 1] = rate;
    }
    return true;
}

bool CMonRacePacket::IsAdenaAdddable(User* const user, const int adenaDatabaseId, const std::int64_t amount) {
    // Current L2Server 0x00842548 keeps the original three-d spelling.
    L2_GUARD_FUNCTION();
    CItem* const adena = user->m_inv.GetByDBID(adenaDatabaseId);
    return adena == nullptr || adena->IsAddableAmount(amount);
}

bool CMonRacePacket::DelMonRaceTicket(
    const int characterDatabaseId,
    const int userServerId,
    const int check,
    const int itemDatabaseId,
    const int adenaDatabaseId,
    const std::int64_t adena
) {
    // Current L2Server 0x00843a70.
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    if (creature == nullptr || !creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"MonRaceData.cpp",
        //     0x80a,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"MonRaceData.cpp",
        //     0x80a,
        //     userServerId)
        L2_LOG_WARN(
            Log,
            "Monster race ticket settlement for character %d stopped: player smart ID 0x%08x is unavailable or not a "
            "player.",
            characterDatabaseId, static_cast<unsigned>(userServerId)
        );
        return false;
    }

    User* const user = creature->CastUser();
    if (check == 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SETTLE_MONSTER_RACE_TICKET);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid DelMonRaceTicket. charid[%d], "
        //     L"check[%d], itemid[%d], adenadbid[%d], adena[%I64d]",
        //     L"MonRaceData.cpp",
        //     0x811,
        //     characterDatabaseId,
        //     check,
        //     itemDatabaseId,
        //     adenaDatabaseId,
        //     adena)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid DelMonRaceTicket. charid[%d], "
        //     L"check[%d], itemid[%d], adenadbid[%d], adena[%I64d]",
        //     L"MonRaceData.cpp",
        //     0x811,
        //     characterDatabaseId,
        //     check,
        //     itemDatabaseId,
        //     adenaDatabaseId,
        //     adena)
        L2_LOG_WARN(
            Log,
            "Monster race ticket settlement rejected for character %d: check result %d (ticket DBID %d, adena DBID %d, "
            "amount %lld).",
            characterDatabaseId, check, itemDatabaseId, adenaDatabaseId, static_cast<long long>(adena)
        );
        return false;
    }

    if (user->GetSocket() == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid usersocket[%x]",
        //     L"MonRaceData.cpp",
        //     0x819,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid usersocket[%x]",
        //     L"MonRaceData.cpp",
        //     0x819,
        //     userServerId)
        L2_LOG_WARN(
            Log, "Monster race ticket settlement for character %d stopped: player smart ID 0x%08x has no socket.",
            characterDatabaseId, static_cast<unsigned>(userServerId)
        );
        return false;
    }
    if (!IsAdenaAdddable(creature->CastUser(), adenaDatabaseId, adena)) {
        creature->CastUser()->SendSystemMessage(SystemMessageId::CANNOT_SETTLE_MONSTER_RACE_TICKET);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Too many adena. charid[%d], check[%d], "
        //     L"itemid[%d], adenadbid[%d], adena[%I64d]",
        //     L"MonRaceData.cpp",
        //     0x820,
        //     characterDatabaseId,
        //     check,
        //     itemDatabaseId,
        //     adenaDatabaseId,
        //     adena)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Too many adena. charid[%d], check[%d], "
        //     L"itemid[%d], adenadbid[%d], adena[%I64d]",
        //     L"MonRaceData.cpp",
        //     0x820,
        //     characterDatabaseId,
        //     check,
        //     itemDatabaseId,
        //     adenaDatabaseId,
        //     adena)
        L2_LOG_ERROR(
            Log,
            "Monster race ticket settlement cannot add adena for character %d: amount %lld rejected (check %d, ticket DBID "
            "%d, adena DBID %d); settlement stops.",
            characterDatabaseId, static_cast<long long>(adena), check, itemDatabaseId, adenaDatabaseId
        );
        return false;
    }

    db.RequestGetMonRaceTicket(creature->CastUser(), 1);
    {
        L2_WRITE_GUARD(creature->CastUser()->m_inv);
        CItem* const ticket = creature->CastUser()->m_inv.GetByDBID(itemDatabaseId);
        if (ticket == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] pItemInv == NULL, this situation must not "
            //     L"occur. dbid[%d]",
            //     "MonRaceData.cpp",
            //     0x82e,
            //     itemDatabaseId)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d] pItemInv == NULL, this situation must not "
            //     L"occur. dbid[%d]",
            //     "MonRaceData.cpp",
            //     0x82e,
            //     itemDatabaseId)
            L2_LOG_ERROR(
                Log,
                "Monster race ticket DBID %d is missing from character %d inventory; settlement stops after requesting the "
                "ticket list.",
                itemDatabaseId, characterDatabaseId
            );
            return false;
        }

        creature->CastUser()->m_inv.SetInventoryChanged(ticket, IUT_DELETE);
        creature->CastUser()->m_inv.Pop(ticket->m_data->index, false);
        if (adena > 0) {
            CItem* adenaItem = creature->CastUser()->m_inv.GetByDBID(adenaDatabaseId);
            if (adenaItem == nullptr) {
                adenaItem = g_ObjectDB.CreateItem(57);
                if (!adenaItem->AddAmount(adena)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "MonRaceData.cpp",
                    //     0x84b,
                    //     adenaItem->m_data->m_nCount,
                    //     adena)
                    // L2_LOG_ERROR(Log,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "MonRaceData.cpp",
                    //     0x84b,
                    //     adenaItem->m_data->m_nCount,
                    //     adena)
                    L2_LOG_ERROR(
                        Log,
                        "Monster race settlement for character %d cannot add %lld adena to a new stack (current amount "
                        "%lld, requested DBID %d); ticket removal was already requested, settlement continues.",
                        characterDatabaseId, static_cast<long long>(adena),
                        static_cast<long long>(adenaItem->m_data->m_nCount), adenaDatabaseId
                    );
                }
                adenaItem->SetDBID(static_cast<unsigned int>(adenaDatabaseId));
                if (!creature->CastUser()->m_inv.Push(adenaItem->m_data->index)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "inven push failed at file[%s], line[%d]",
                    //     "MonRaceData.cpp",
                    //     0x850)
                    // L2_LOG_ERROR(Log,
                    //     "inven push failed at file[%s], line[%d]",
                    //     "MonRaceData.cpp",
                    //     0x850)
                    L2_LOG_ERROR(
                        Log,
                        "Monster race settlement for character %d cannot insert new adena stack with DBID %d into "
                        "inventory; ticket removal was already requested; inventory-change notification and settlement "
                        "continue.",
                        characterDatabaseId, adenaDatabaseId
                    );
                }
                creature->CastUser()->m_inv.SetInventoryChanged(adenaItem, IUT_ADD);
            } else {
                {
                    L2_GUARD(*adenaItem->m_lock);
                    if (!adenaItem->AddAmount(adena)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                        //     "MonRaceData.cpp",
                        //     0x83e,
                        //     adenaItem->m_data->m_nCount,
                        //     adena)
                        // L2_LOG_ERROR(Log,
                        //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                        //     "MonRaceData.cpp",
                        //     0x83e,
                        //     adenaItem->m_data->m_nCount,
                        //     adena)
                        L2_LOG_ERROR(
                            Log,
                            "Monster race settlement for character %d cannot add %lld adena to stack DBID %d (current "
                            "amount %lld); ticket removal was already requested, settlement continues.",
                            characterDatabaseId, static_cast<long long>(adena), adenaDatabaseId,
                            static_cast<long long>(adenaItem->m_data->m_nCount)
                        );
                    }
                }
                creature->CastUser()->m_inv.SetInventoryChanged(adenaItem, IUT_UPDATE);
            }
        }

        ticket->Delete();
    }
    creature->CastUser()->SendItemList(true, true, nullptr);
    creature->CastUser()->GetSocket()->SendSystemMessage_Q(SystemMessageId::EARNED_ADENA, adena);
    return true;
}

CMonRaceHTML::CMonRaceHTML(CUserSocket* const socket) : CParseHTML(socket), m_nCountryCode(0) {
    // Current L2Server 0x00842618.
    L2_GUARD_FUNCTION();
    if (socket != nullptr) {
        User* const user = socket->GetUser();
        if (user != nullptr) {
            m_nCountryCode = user->m_data->m_CountryCode;
        }
    }
}

CMonRaceHTML::~CMonRaceHTML() = default;

bool CMonRaceHTML::CheckAge() {
    // Current L2Server 0x0084270c.
    L2_GUARD_FUNCTION();
    if (m_mySocket != nullptr && static_cast<unsigned int>(m_mySocket->GetAge()) - 1U < 0x12U) {
        return false;
    }
    return true;
}

double CMonRaceHTML::GetWinRate(
    const int raceId,
    const int resultType,
    const int selectedFirst,
    const int selectedSecond,
    const int winningFirst,
    const int winningSecond,
    double fallback
) {
    // Current L2Server 0x008427c4.
    L2_GUARD_FUNCTION();
    if (raceId == g_MonRaceDB.m_nRaceId && g_MonRaceDB.m_status < MR_END) {
        return 0.0;
    }
    if (resultType == 1) {
        if (std::min(selectedFirst, selectedSecond) == std::min(winningFirst, winningSecond) &&
            std::max(selectedFirst, selectedSecond) == std::max(winningFirst, winningSecond)) {
            return fallback;
        }
    } else if (resultType == 0 && raceId != g_MonRaceDB.m_nRaceId) {
        return 1.0;
    }
    fallback = 0.01;
    return fallback;
}

void CMonRaceHTML::MonRaceKeeper() {
    // Current L2Server 0x00844b34.
    L2_GUARD_FUNCTION();
    static_cast<void>(
        SendHTML(CFHTML::MakeHTMLFromStrMap(u"mr_keeper.htm", m_mapParam, static_cast<std::uint8_t>(m_nCountryCode)))
    );
}

void CMonRaceHTML::MonRaceError(const int messageId) {
    // Current L2Server 0x00844c4c.
    L2_GUARD_FUNCTION();
    if (m_mySocket != nullptr) {
        m_mySocket->SendSystemMessage(messageId);
        MonRaceKeeper();
    }
}

void CMonRaceHTML::MonRaceView20() {
    // Current L2Server 0x00846bd0.
    L2_GUARD_FUNCTION();
    if (g_MonRaceDB.m_nRaceId <= 0) {
        return;
    }

    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }

    int index = 1;
    for (CMonRaceData* const runner : runners) {
        if (runner != nullptr) {
            std::array<char16_t, 16> nameKey{};
            std::array<char16_t, 16> conditionKey{};
            std::array<char16_t, 16> winKey{};
            FormatUtf16To(nameKey.data(), nameKey.size(), u"NAME%d", index);
            FormatUtf16To(conditionKey.data(), conditionKey.size(), u"COND%d", index);
            FormatUtf16To(winKey.data(), winKey.size(), u"WIN%d", index);
            AddFormattedStringMap(std::u16string(nameKey.data()), u"&$%d;", runner->sys_string);
            int conditionStringId = 0x2ea;
            if (runner->condition == 1) {
                conditionStringId = 0x2eb;
            } else if (runner->condition == 3) {
                conditionStringId = 0x2e9;
            }
            AddFormattedStringMap(std::u16string(conditionKey.data()), u"&$%d;", conditionStringId);
            const int win = runner->m_runcount == 0
                ? 0
                : runtime::MultiplyInt32Wrapping(runner->m_wincount, 100) / runner->m_runcount;
            AddFormattedStringMap(std::u16string(winKey.data()), u"%d", win);
        }
        ++index;
    }
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(
        SendHTML(CFHTML::MakeHTMLFromStrMap(u"mr_vw20.htm", m_mapParam, static_cast<std::uint8_t>(m_nCountryCode)))
    );
}

void CMonRaceHTML::MonRaceBuy1(const int selectedLane) {
    // Current L2Server 0x00846f08.
    L2_GUARD_FUNCTION();
    if (!CheckAge()) {
        MonRaceError(0x432);
        return;
    }
    const int raceId = g_MonRaceDB.m_nRaceId;
    if (g_MonRaceDB.m_status != MR_SELL_TICKET) {
        MonRaceError(0x416);
        return;
    }
    if (raceId <= 0) {
        return;
    }

    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }
    int index = 1;
    for (CMonRaceData* const runner : runners) {
        if (runner != nullptr) {
            std::array<char16_t, 16> selectKey{};
            std::array<char16_t, 16> nameKey{};
            std::array<char16_t, 16> conditionKey{};
            std::array<char16_t, 16> winKey{};
            FormatUtf16To(selectKey.data(), selectKey.size(), u"SEL%d", index);
            FormatUtf16To(nameKey.data(), nameKey.size(), u"NAME%d", index);
            FormatUtf16To(conditionKey.data(), conditionKey.size(), u"COND%d", index);
            FormatUtf16To(winKey.data(), winKey.size(), u"WIN%d", index);
            AddFormattedStringMap(std::u16string(selectKey.data()), u"bypass %s_%d", u"_mrbu01", index);
            AddFormattedStringMap(std::u16string(nameKey.data()), u"&$%d;", runner->sys_string);
            int conditionStringId = 0x2ea;
            if (runner->condition == 1) {
                conditionStringId = 0x2eb;
            } else if (runner->condition == 3) {
                conditionStringId = 0x2e9;
            }
            AddFormattedStringMap(std::u16string(conditionKey.data()), u"&$%d;", conditionStringId, index);
            const int win = runner->m_runcount == 0
                ? 0
                : runtime::MultiplyInt32Wrapping(runner->m_wincount, 100) / runner->m_runcount;
            AddFormattedStringMap(std::u16string(winKey.data()), u"%d", win, index);
        }
        ++index;
    }
    AddFormattedStringMap(u"RACE_ID", u"%d", raceId);
    if (selectedLane < 1) {
        AddStringMap(u"SEL_ID", u"");
        AddFormattedStringMap(u"LINK_NEXT", u"bypass %s_0", u"_mrbu01");
    } else {
        AddFormattedStringMap(u"SEL_ID", u"%d", selectedLane);
        AddFormattedStringMap(u"LINK_NEXT", u"bypass %s_%d_0", u"_mrbu02", selectedLane);
    }
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(
        SendHTML(CFHTML::MakeHTMLFromStrMap(u"mr_buy1.htm", m_mapParam, static_cast<std::uint8_t>(m_nCountryCode)))
    );
}

void CMonRaceHTML::MonRaceBuy2(const int lane, const int amountIndex) {
    // Current L2Server 0x0084736c.
    L2_GUARD_FUNCTION();
    if (!CheckAge()) {
        MonRaceError(0x432);
        return;
    }
    const int raceId = g_MonRaceDB.m_nRaceId;
    if (g_MonRaceDB.m_status != MR_SELL_TICKET) {
        MonRaceError(0x416);
        return;
    }
    if (raceId < 1 || static_cast<unsigned int>(lane - 1) > 7U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"hack? nRaceId[%d], nLaneId[%d]...",
        //     raceId,
        //     lane)
        // L2_LOG_ERROR(Log,
        //     L"hack? nRaceId[%d], nLaneId[%d]...",
        //     raceId,
        //     lane)
        L2_LOG_WARN(
            Log,
            "Monster race ticket selection rejected: race ID %d must be positive and lane %d must be in 1..8; returning to "
            "the selection page.",
            raceId, lane
        );
        MonRaceBuy1(0);
        return;
    }

    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }
    CMonRaceData* const runner = runtime::CheckedVectorAt(runners, static_cast<std::size_t>(lane - 1));
    if (runner == nullptr) {
        return;
    }
    AddFormattedStringMap(u"NAME1", u"&$%d;", runner->sys_string);
    int conditionStringId = 0x2ea;
    if (runner->condition == 1) {
        conditionStringId = 0x2eb;
    } else if (runner->condition == 3) {
        conditionStringId = 0x2e9;
    }
    AddFormattedStringMap(u"COND1", u"&$%d;", conditionStringId);
    const int win =
        runner->m_runcount == 0 ? 0 : runtime::MultiplyInt32Wrapping(runner->m_wincount, 100) / runner->m_runcount;
    AddFormattedStringMap(u"WIN1", u"%d", win);
    AddFormattedStringMap(u"RACE_ID", u"%d", raceId);
    AddFormattedStringMap(u"LANE", u"%d", lane);
    AddFormattedStringMap(u"LINK_BUY_1", u"bypass %s_%d_1", u"_mrbu02", lane);
    AddFormattedStringMap(u"LINK_BUY_2", u"bypass %s_%d_2", u"_mrbu02", lane);
    AddFormattedStringMap(u"LINK_BUY_3", u"bypass %s_%d_3", u"_mrbu02", lane);
    AddFormattedStringMap(u"LINK_BUY_4", u"bypass %s_%d_4", u"_mrbu02", lane);
    AddFormattedStringMap(u"LINK_BUY_5", u"bypass %s_%d_5", u"_mrbu02", lane);
    AddFormattedStringMap(u"LINK_BUY_6", u"bypass %s_%d_6", u"_mrbu02", lane);
    AddFormattedStringMap(u"LINK_BUY_7", u"bypass %s_%d_7", u"_mrbu02", lane);
    AddFormattedStringMap(u"LINK_BUY_8", u"bypass %s_%d_8", u"_mrbu02", lane);
    AddFormattedStringMap(u"SEL_ID", u"%d", MONRACE_MONEY[amountIndex], lane);
    if (static_cast<unsigned int>(amountIndex - 1) < 8U) {
        AddFormattedStringMap(u"LINK_NEXT", u"bypass %s_%d_%d_1", u"_mrbu03", lane, amountIndex);
    } else {
        AddFormattedStringMap(u"LINK_NEXT", u"bypass %s_%d_0", u"_mrbu02", lane);
    }
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper", lane);
    static_cast<void>(
        SendHTML(CFHTML::MakeHTMLFromStrMap(u"mr_buy2.htm", m_mapParam, static_cast<std::uint8_t>(m_nCountryCode)))
    );
}

void CMonRaceHTML::MonRaceBuy4(const int lane, const int amountIndex, const int confirmation) {
    // Current L2Server 0x00847824.
    L2_GUARD_FUNCTION();
    if (!CheckAge()) {
        MonRaceError(0x432);
        return;
    }
    if (m_mySocket == nullptr) {
        return;
    }
    User* const user = m_mySocket->GetUser();
    const int raceId = g_MonRaceDB.m_nRaceId;
    if (user == nullptr) {
        return;
    }
    if (g_MonRaceDB.m_status != MR_SELL_TICKET) {
        MonRaceError(0x416);
        return;
    }
    if (raceId < 1 || static_cast<unsigned int>(lane - 1) > 7U || amountIndex < 1 || amountIndex > 8) {
        MonRaceBuy1(0);
        return;
    }

    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }
    CMonRaceData* const runner = runtime::CheckedVectorAt(runners, static_cast<std::size_t>(lane - 1));
    if (runner == nullptr || confirmation != 1) {
        return;
    }
    AddFormattedStringMap(u"NAME1", u"&$%d;", runner->sys_string);
    int conditionStringId = 0x2ea;
    if (runner->condition == 1) {
        conditionStringId = 0x2eb;
    } else if (runner->condition == 3) {
        conditionStringId = 0x2e9;
    }
    AddFormattedStringMap(u"COND1", u"&$%d;", conditionStringId);
    const int win =
        runner->m_runcount == 0 ? 0 : runtime::MultiplyInt32Wrapping(runner->m_wincount, 100) / runner->m_runcount;
    AddFormattedStringMap(u"WIN1", u"%d", win);
    static_cast<void>(g_MonRaceDB.RequestCreateMonTicket(raceId, 1, lane, 0, 0, MONRACE_MONEY[amountIndex], user));
    static_cast<void>(
        SendHTML(CFHTML::MakeHTMLFromStrMap(u"mr_keeper.htm", m_mapParam, static_cast<std::uint8_t>(m_nCountryCode)))
    );
}

void CMonRaceHTML::MonRaceBuy11(const int selectedLane) {
    // Current L2Server 0x00847b7c.
    L2_GUARD_FUNCTION();
    if (!CheckAge()) {
        MonRaceError(0x432);
        return;
    }
    const int raceId = g_MonRaceDB.m_nRaceId;
    if (g_MonRaceDB.m_status != MR_SELL_TICKET) {
        MonRaceError(0x416);
        return;
    }
    if (raceId < 1) {
        return;
    }

    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }
    int index = 1;
    for (CMonRaceData* const runner : runners) {
        if (runner != nullptr) {
            std::array<char16_t, 16> selectKey{};
            std::array<char16_t, 16> nameKey{};
            std::array<char16_t, 16> conditionKey{};
            std::array<char16_t, 16> winKey{};
            FormatUtf16To(selectKey.data(), selectKey.size(), u"SEL%d", index);
            FormatUtf16To(nameKey.data(), nameKey.size(), u"NAME%d", index);
            FormatUtf16To(conditionKey.data(), conditionKey.size(), u"COND%d", index);
            FormatUtf16To(winKey.data(), winKey.size(), u"WIN%d", index);
            AddFormattedStringMap(std::u16string(selectKey.data()), u"bypass %s_%d", u"_mrbu11", index);
            AddFormattedStringMap(std::u16string(nameKey.data()), u"&$%d;", runner->sys_string);
            int conditionStringId = 0x2ea;
            if (runner->condition == 1) {
                conditionStringId = 0x2eb;
            } else if (runner->condition == 3) {
                conditionStringId = 0x2e9;
            }
            AddFormattedStringMap(std::u16string(conditionKey.data()), u"&$%d;", conditionStringId, index);
            const int win = runner->m_runcount == 0
                ? 0
                : runtime::MultiplyInt32Wrapping(runner->m_wincount, 100) / runner->m_runcount;
            AddFormattedStringMap(std::u16string(winKey.data()), u"%d", win, index);
        }
        ++index;
    }
    AddFormattedStringMap(u"RACE_ID", u"%d", raceId);
    if (selectedLane < 1) {
        AddFormattedStringMap(u"SEL_ID", u"");
        AddFormattedStringMap(u"LINK_NEXT", u"bypass %s_0", u"_mrbu11");
    } else {
        AddFormattedStringMap(u"SEL_ID", u"%d", selectedLane);
        AddFormattedStringMap(u"LINK_NEXT", u"bypass %s_0_%d", u"_mrbu12", selectedLane);
    }
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(
        SendHTML(CFHTML::MakeHTMLFromStrMap(u"mr_buy11.htm", m_mapParam, static_cast<std::uint8_t>(m_nCountryCode)))
    );
}

void CMonRaceHTML::MonRaceBuy12(int firstLane, int secondLane) {
    // Current L2Server 0x00847fd8.
    L2_GUARD_FUNCTION();
    if (!CheckAge()) {
        MonRaceError(0x432);
        return;
    }
    const int raceId = g_MonRaceDB.m_nRaceId;
    if (g_MonRaceDB.m_status != MR_SELL_TICKET) {
        MonRaceError(0x416);
        return;
    }
    if (raceId < 1 || firstLane == secondLane) {
        MonRaceBuy11(0);
        return;
    }
    if (secondLane < firstLane) {
        std::swap(firstLane, secondLane);
    }

    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }
    int index = 1;
    for (CMonRaceData* const runner : runners) {
        if (runner != nullptr) {
            std::array<char16_t, 16> selectKey{};
            std::array<char16_t, 16> nameKey{};
            std::array<char16_t, 16> conditionKey{};
            std::array<char16_t, 16> winKey{};
            FormatUtf16To(selectKey.data(), selectKey.size(), u"SEL%d", index);
            FormatUtf16To(nameKey.data(), nameKey.size(), u"NAME%d", index);
            FormatUtf16To(conditionKey.data(), conditionKey.size(), u"COND%d", index);
            FormatUtf16To(winKey.data(), winKey.size(), u"WIN%d", index);
            AddFormattedStringMap(std::u16string(selectKey.data()), u"bypass %s_%d_%d", u"_mrbu12", secondLane, index);
            AddFormattedStringMap(std::u16string(nameKey.data()), u"&$%d;", runner->sys_string);
            int conditionStringId = 0x2ea;
            if (runner->condition == 1) {
                conditionStringId = 0x2eb;
            } else if (runner->condition == 3) {
                conditionStringId = 0x2e9;
            }
            AddFormattedStringMap(std::u16string(conditionKey.data()), u"&$%d;", conditionStringId, secondLane, index);
            const int win = runner->m_runcount == 0
                ? 0
                : runtime::MultiplyInt32Wrapping(runner->m_wincount, 100) / runner->m_runcount;
            AddFormattedStringMap(std::u16string(winKey.data()), u"%d", win, secondLane, index);
        }
        ++index;
    }
    AddFormattedStringMap(u"RACE_ID", u"%d", raceId);
    const wchar_t* nextCommand = L"_mrbu13";
    if (firstLane == 0) {
        AddFormattedStringMap(u"SEL_ID", u"%d", 0);
        AddFormattedStringMap(u"SEL_ID2", u"%d", secondLane);
        nextCommand = L"_mrbu12";
    } else {
        AddFormattedStringMap(u"SEL_ID", u"%d", firstLane);
        AddFormattedStringMap(u"SEL_ID2", u"%d", secondLane);
    }
    AddFormattedStringMap(u"LINK_NEXT", u"bypass %s_%d_%d_0", nextCommand, firstLane, secondLane);
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(
        SendHTML(CFHTML::MakeHTMLFromStrMap(u"mr_buy12.htm", m_mapParam, static_cast<std::uint8_t>(m_nCountryCode)))
    );
}

void CMonRaceHTML::MonRaceBuy13(const int firstLane, const int secondLane, const int amountIndex) {
    // Current L2Server 0x0084848c.
    L2_GUARD_FUNCTION();
    if (!CheckAge()) {
        MonRaceError(0x432);
        return;
    }
    const int raceId = g_MonRaceDB.m_nRaceId;
    if (g_MonRaceDB.m_status != MR_SELL_TICKET) {
        MonRaceError(0x416);
        return;
    }
    if (raceId < 1 || firstLane < 1 || secondLane < 1 || firstLane > 8 || secondLane > 8) {
        MonRaceBuy11(0);
        return;
    }

    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }
    CMonRaceData* const firstRunner = runtime::CheckedVectorAt(runners, static_cast<std::size_t>(firstLane - 1));
    CMonRaceData* const secondRunner = runtime::CheckedVectorAt(runners, static_cast<std::size_t>(secondLane - 1));
    if (firstRunner == nullptr || secondRunner == nullptr) {
        return;
    }

    AddFormattedStringMap(u"NAME1", u"&$%d;", firstRunner->sys_string);
    int conditionStringId = 0x2ea;
    if (firstRunner->condition == 1) {
        conditionStringId = 0x2eb;
    } else if (firstRunner->condition == 3) {
        conditionStringId = 0x2e9;
    }
    AddFormattedStringMap(u"COND1", u"&$%d;", conditionStringId);
    int win = firstRunner->m_runcount == 0
        ? 0
        : runtime::MultiplyInt32Wrapping(firstRunner->m_wincount, 100) / firstRunner->m_runcount;
    AddFormattedStringMap(u"WIN1", u"%d", win);

    AddFormattedStringMap(u"NAME2", u"&$%d;", secondRunner->sys_string);
    conditionStringId = 0x2ea;
    if (secondRunner->condition == 1) {
        conditionStringId = 0x2eb;
    } else if (secondRunner->condition == 3) {
        conditionStringId = 0x2e9;
    }
    AddFormattedStringMap(u"COND2", u"&$%d;", conditionStringId);
    win = secondRunner->m_runcount == 0
        ? 0
        : runtime::MultiplyInt32Wrapping(secondRunner->m_wincount, 100) / secondRunner->m_runcount;
    AddFormattedStringMap(u"WIN2", u"%d", win);
    AddFormattedStringMap(u"RACE_ID", u"%d", raceId);
    AddFormattedStringMap(u"LINK_BUY_1", u"bypass %s_%d_%d_1", u"_mrbu13", firstLane, secondLane);
    AddFormattedStringMap(u"LINK_BUY_2", u"bypass %s_%d_%d_2", u"_mrbu13", firstLane, secondLane);
    AddFormattedStringMap(u"LINK_BUY_3", u"bypass %s_%d_%d_3", u"_mrbu13", firstLane, secondLane);
    AddFormattedStringMap(u"LINK_BUY_4", u"bypass %s_%d_%d_4", u"_mrbu13", firstLane, secondLane);
    AddFormattedStringMap(u"LINK_BUY_5", u"bypass %s_%d_%d_5", u"_mrbu13", firstLane, secondLane);
    AddFormattedStringMap(u"LINK_BUY_6", u"bypass %s_%d_%d_6", u"_mrbu13", firstLane, secondLane);
    AddFormattedStringMap(u"LINK_BUY_7", u"bypass %s_%d_%d_7", u"_mrbu13", firstLane, secondLane);
    AddFormattedStringMap(u"LINK_BUY_8", u"bypass %s_%d_%d_8", u"_mrbu13", firstLane, secondLane);
    AddFormattedStringMap(u"SEL_ID", u"%d", MONRACE_MONEY[amountIndex]);
    if (amountIndex < 1) {
        AddFormattedStringMap(u"LINK_NEXT", u"bypass %s_%d_%d_0", u"_mrbu13", firstLane, secondLane);
    } else {
        AddFormattedStringMap(u"LINK_NEXT", u"bypass %s_%d_%d_%d", u"_mrbu14", firstLane, secondLane, amountIndex);
    }
    AddFormattedStringMap(u"LANE", u"%d", firstLane, firstLane, secondLane);
    AddFormattedStringMap(u"LANE2", u"%d", secondLane);
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(
        SendHTML(CFHTML::MakeHTMLFromStrMap(u"mr_buy13.htm", m_mapParam, static_cast<std::uint8_t>(m_nCountryCode)))
    );
}

void CMonRaceHTML::MonRaceBuy15(const int firstLane, const int secondLane, const int amountIndex, const int confirmation) {
    // Current L2Server 0x00848a14.
    L2_GUARD_FUNCTION();
    if (!CheckAge()) {
        MonRaceError(0x432);
        return;
    }
    if (m_mySocket == nullptr) {
        return;
    }
    User* const user = m_mySocket->GetUser();
    const int raceId = g_MonRaceDB.m_nRaceId;
    if (user == nullptr) {
        return;
    }
    if (g_MonRaceDB.m_status != MR_SELL_TICKET) {
        MonRaceError(0x416);
        return;
    }
    if (raceId < 1 || firstLane < 1 || secondLane < 1 || firstLane > 8 || secondLane > 8 || amountIndex < 1 ||
        amountIndex > 8) {
        MonRaceBuy11(0);
        return;
    }

    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }
    CMonRaceData* const firstRunner = runtime::CheckedVectorAt(runners, static_cast<std::size_t>(firstLane - 1));
    CMonRaceData* const secondRunner = runtime::CheckedVectorAt(runners, static_cast<std::size_t>(secondLane - 1));
    if (firstRunner == nullptr || secondRunner == nullptr || confirmation != 1) {
        return;
    }
    static_cast<void>(
        g_MonRaceDB.RequestCreateMonTicket(raceId, 2, firstLane, secondLane, 0, MONRACE_MONEY[amountIndex], user)
    );
    static_cast<void>(
        SendHTML(CFHTML::MakeHTMLFromStrMap(u"mr_keeper.htm", m_mapParam, static_cast<std::uint8_t>(m_nCountryCode)))
    );
}

void CMonRaceHTML::MonRaceView1() {
    // Current L2Server 0x0084989c.
    L2_GUARD_FUNCTION();
    if (g_MonRaceDB.m_status < MR_AFTER_SELL) {
        MonRaceError(0x414);
        return;
    }

    GetBodyFromFile(u"mr_vw1.htm", static_cast<int>(m_nCountryCode));
    std::u16string runnerList;
    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }

    for (std::size_t index = 0; index < runners.size(); ++index) {
        CMonRaceData* const runner = runtime::CheckedVectorAt(runners, index);
        if (runner == nullptr) {
            continue;
        }
        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"lane", u"%d", static_cast<int>(index) + 1);
        AddFormattedStringLocalMap(parameters, u"mon_name", u"&$%d;", runner->sys_string);
        const double winRate = g_MonRaceDB.m_winrate1[index];
        if (winRate == 0.0) {
            AddStringLocalMap(parameters, u"winrate", u"&$804;");
            runnerList += MakeHTMLFromStrTemplet(2, parameters);
        } else {
            AddFormattedStringLocalMap(parameters, u"winrate", u"%.1f", winRate);
            runnerList += MakeHTMLFromStrTemplet(1, parameters);
        }
    }

    AddFormattedStringMap(u"RACE_ID", u"%d", g_MonRaceDB.m_nRaceId);
    const std::u16string hostRunnerList(runnerList);
    AddStringMap(u"MON_LIST", hostRunnerList);
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam)));
}

void CMonRaceHTML::MonRaceView11() {
    // Current L2Server 0x00849d54.
    L2_GUARD_FUNCTION();
    if (g_MonRaceDB.m_status < MR_AFTER_SELL) {
        MonRaceError(0x414);
        return;
    }
    if (m_mySocket == nullptr || m_mySocket->GetUser() == nullptr) {
        return;
    }

    GetBodyFromFile(u"mr_vw11.htm", static_cast<int>(m_nCountryCode));
    std::u16string runnerList;
    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }

    const int runnerCount = static_cast<int>(runners.size());
    for (int first = 0; first < runnerCount - 1; ++first) {
        if (runtime::CheckedVectorAt(runners, static_cast<std::size_t>(first)) == nullptr) {
            continue;
        }
        {
            StrStrMapType parameters;
            AddFormattedStringLocalMap(parameters, u"first", u"%d", first + 1);
            runnerList += MakeHTMLFromStrTemplet(1, parameters);
        }
        for (int second = 1; second < runnerCount; ++second) {
            StrStrMapType parameters;
            if (first < second) {
                const double winRate = g_MonRaceDB.m_winrate2[first][second];
                if (winRate == 0.0) {
                    AddFormattedStringLocalMap(parameters, u"winrate", u"-");
                } else {
                    AddFormattedStringLocalMap(parameters, u"winrate", u"%.1f", winRate);
                }
                runnerList += MakeHTMLFromStrTemplet(2, parameters);
            } else {
                runnerList += MakeHTMLFromStrTemplet(3, parameters);
            }
        }
        StrStrMapType parameters;
        runnerList += MakeHTMLFromStrTemplet(4, parameters);
    }

    AddFormattedStringMap(u"RACE_ID", u"%d", g_MonRaceDB.m_nRaceId);
    const std::u16string hostRunnerList(runnerList);
    AddStringMap(u"MON_LIST", hostRunnerList);
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam)));
}

void CMonRaceHTML::MonRaceBuy3(const int lane, const int amountIndex, const int) {
    // Current L2Server 0x0084a31c.
    L2_GUARD_FUNCTION();
    if (!CheckAge()) {
        MonRaceError(0x432);
        return;
    }
    const int raceId = g_MonRaceDB.m_nRaceId;
    if (g_MonRaceDB.m_status != MR_SELL_TICKET) {
        MonRaceError(0x416);
        return;
    }
    if (raceId < 1 || static_cast<unsigned int>(lane - 1) > 7U || amountIndex < 1 || amountIndex > 8) {
        MonRaceBuy1(0);
        return;
    }

    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }
    User* const user = m_mySocket->GetUser();
    if (user == nullptr) {
        return;
    }
    GetBodyFromFile(u"mr_buy3.htm", static_cast<int>(m_nCountryCode));
    CMonRaceData* const runner = runtime::CheckedVectorAt(runners, static_cast<std::size_t>(lane - 1));
    if (runner == nullptr) {
        return;
    }

    AddFormattedStringMap(u"NAME1", u"&$%d;", runner->sys_string);
    int conditionStringId = 0x2ea;
    if (runner->condition == 1) {
        conditionStringId = 0x2eb;
    } else if (runner->condition == 3) {
        conditionStringId = 0x2e9;
    }
    AddFormattedStringMap(u"COND1", u"&$%d;", conditionStringId);
    const int win =
        runner->m_runcount == 0 ? 0 : runtime::MultiplyInt32Wrapping(runner->m_wincount, 100) / runner->m_runcount;
    AddFormattedStringMap(u"WIN1", u"%d", win);
    const int amount = MONRACE_MONEY[amountIndex];
    AddFormattedStringMap(u"LANE", u"%d", lane);
    AddFormattedStringMap(u"RACE_ID", u"%d", raceId);
    AddFormattedStringMap(u"BUY_AMOUNT", u"%d", amount);
    int tax = 0;
    if (g_MonRaceDB.m_nTaxRate != 0) {
        tax = runtime::MultiplyInt32Wrapping(g_MonRaceDB.m_nTaxRate, amount) / 100;
    }
    AddFormattedStringMap(u"TAX_AMOUNT", u"%d", tax);
    const int remoteFee = g_MonRaceDB.GetRaceFee(user->GetPos(), amount);
    if (remoteFee > 0) {
        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"FEE", u"%d", remoteFee);
        const std::u16string feeCharge = MakeHTMLFromStrTemplet(1, parameters);
        const std::u16string hostFeeCharge(feeCharge);
        AddStringMap(u"FEE_CHARGE", hostFeeCharge);
    }
    AddFormattedStringMap(
        u"SUM_AMOUNT", u"%d", runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(remoteFee, tax), amount)
    );
    AddFormattedStringMap(u"LINK_NEXT", u"bypass %s_%d_%d_1", u"_mrbu04", lane, amountIndex);
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam)));
}

void CMonRaceHTML::MonRaceBuy14(const int firstLane, const int secondLane, const int amountIndex, const int confirmation) {
    // Current L2Server 0x0084a928.
    L2_GUARD_FUNCTION();
    if (!CheckAge()) {
        MonRaceError(0x432);
        return;
    }
    const int raceId = g_MonRaceDB.m_nRaceId;
    if (g_MonRaceDB.m_status != MR_SELL_TICKET) {
        MonRaceError(0x416);
        return;
    }
    if (raceId < 1 || firstLane < 1 || secondLane < 1 || firstLane > 8 || secondLane > 8 || amountIndex < 1 ||
        amountIndex > 8) {
        MonRaceBuy11(0);
        return;
    }

    std::vector<CMonRaceData*> runners;
    if (!g_MonRaceDB.GetRunnerList(runners, false)) {
        MonRaceError(0x415);
        return;
    }
    User* const user = m_mySocket->GetUser();
    if (user == nullptr) {
        return;
    }
    GetBodyFromFile(u"mr_buy14.htm", static_cast<int>(m_nCountryCode));
    CMonRaceData* const firstRunner = runtime::CheckedVectorAt(runners, static_cast<std::size_t>(firstLane - 1));
    CMonRaceData* const secondRunner = runtime::CheckedVectorAt(runners, static_cast<std::size_t>(secondLane - 1));
    if (firstRunner == nullptr || secondRunner == nullptr) {
        return;
    }

    AddFormattedStringMap(u"NAME1", u"&$%d;", firstRunner->sys_string);
    int conditionStringId = 0x2ea;
    if (firstRunner->condition == 1) {
        conditionStringId = 0x2eb;
    } else if (firstRunner->condition == 3) {
        conditionStringId = 0x2e9;
    }
    AddFormattedStringMap(u"COND1", u"&$%d;", conditionStringId);
    int win = firstRunner->m_runcount == 0
        ? 0
        : runtime::MultiplyInt32Wrapping(firstRunner->m_wincount, 100) / firstRunner->m_runcount;
    AddFormattedStringMap(u"WIN1", u"%d", win);

    AddFormattedStringMap(u"NAME2", u"&$%d;", secondRunner->sys_string);
    conditionStringId = 0x2ea;
    if (secondRunner->condition == 1) {
        conditionStringId = 0x2eb;
    } else if (secondRunner->condition == 3) {
        conditionStringId = 0x2e9;
    }
    AddFormattedStringMap(u"COND2", u"&$%d;", conditionStringId);
    win = secondRunner->m_runcount == 0
        ? 0
        : runtime::MultiplyInt32Wrapping(secondRunner->m_wincount, 100) / secondRunner->m_runcount;
    AddFormattedStringMap(u"WIN2", u"%d", win);
    AddFormattedStringMap(u"LANE", u"%d", firstLane);
    AddFormattedStringMap(u"LANE2", u"%d", secondLane);
    AddFormattedStringMap(u"RACE_ID", u"%d", raceId);
    const int amount = MONRACE_MONEY[amountIndex];
    AddFormattedStringMap(u"BUY_AMOUNT", u"%d", amount);
    int tax = 0;
    if (g_MonRaceDB.m_nTaxRate != 0) {
        tax = runtime::MultiplyInt32Wrapping(g_MonRaceDB.m_nTaxRate, amount) / 100;
    }
    AddFormattedStringMap(u"TAX_AMOUNT", u"%d", tax);
    int remoteFee = g_MonRaceDB.GetRaceFee(user->GetPos(), amount);
    if (remoteFee > 0) {
        remoteFee = g_MonRaceDB.GetRaceFee(user->GetPos(), amount);
        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"FEE", u"%d", remoteFee);
        const std::u16string feeCharge = MakeHTMLFromStrTemplet(1, parameters);
        const std::u16string hostFeeCharge(feeCharge);
        AddStringMap(u"FEE_CHARGE", hostFeeCharge);
    }
    AddFormattedStringMap(
        u"SUM_AMOUNT", u"%d", runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(remoteFee, tax), amount)
    );
    AddFormattedStringMap(
        u"LINK_NEXT", u"bypass %s_%d_%d_%d_1", u"_mrbu15", firstLane, secondLane, amountIndex, confirmation
    );
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam)));
}

void CMonRaceHTML::MonRaceSell1(const int, const int, int rowCount, const int page, const unsigned char* packet) {
    // Current L2Server 0x0084b060.
    L2_GUARD_FUNCTION();
    if (rowCount > 10) {
        rowCount = 10;
    }

    std::u16string ticketList;
    GetBodyFromFile(u"mr_sell1.htm", static_cast<int>(m_nCountryCode));
    while (rowCount > 0) {
        --rowCount;
        int actionId = 0;
        int itemClassId = 0;
        int ignoredThird = 0;
        int raceId = 0;
        int adena = 0;
        int laneValue = 0;
        int ignoredSeventh = 0;
        int ignoredEighth = 0;
        packet = ::Disassemble(
            packet, "dddddddd", &actionId, &itemClassId, &ignoredThird, &raceId, &adena, &laneValue, &ignoredSeventh,
            &ignoredEighth
        );

        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"action_link", u"bypass %s_%d", u"_mrsl02", actionId);
        AddFormattedStringLocalMap(parameters, u"race_id", u"%d", raceId);
        const int lowLane = laneValue & 0xf;
        const int highLane = (laneValue & 0xf0) != 0 ? (laneValue & 0xf0) >> 4 : 0;
        if (itemClassId == 0x115b) {
            AddFormattedStringLocalMap(parameters, u"lane", u"%d", lowLane + highLane);
        } else {
            AddFormattedStringLocalMap(parameters, u"lane", u"%d, %d", highLane, lowLane);
        }
        AddFormattedStringLocalMap(parameters, u"adena", u"%d", adena);
        ticketList += MakeHTMLFromStrTemplet(1, parameters);
    }

    const std::u16string hostTicketList(ticketList);
    AddStringMap(u"TICKET_LIST", hostTicketList);
    const int previousPage = page > 1 ? page - 1 : 1;
    AddFormattedStringMap(u"LINK_PREV", u"bypass %s_%d", u"_mrsl01", previousPage);
    AddFormattedStringMap(u"CURR_PAGE", u"%d", page);
    AddFormattedStringMap(u"LINK_NEXT", u"bypass %s_%d", u"_mrsl01", page + 1);
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam)));
}

void CMonRaceHTML::MonRaceSell2(const int, const int, const unsigned char* packet) {
    // Current L2Server 0x0084b544.
    L2_GUARD_FUNCTION();
    int actionId = 0;
    int ignoredSecond = 0;
    int raceId = 0;
    int ticketType = 0;
    int firstLane = 0;
    int secondLane = 0;
    int ignoredSeventh = 0;
    int betAdena = 0;
    double firstFallback = 0.0;
    double secondFallback = 0.0;
    int firstWinner = 0;
    int secondWinner = 0;
    int resultType = 0;
    ::Disassemble(
        packet, "ddddddddffddd", &actionId, &ignoredSecond, &raceId, &ticketType, &firstLane, &secondLane, &ignoredSeventh,
        &betAdena, &firstFallback, &secondFallback, &firstWinner, &secondWinner, &resultType
    );

    GetBodyFromFile(u"mr_sell2.htm", static_cast<int>(m_nCountryCode));
    if (raceId != g_MonRaceDB.m_nRaceId || g_MonRaceDB.m_status > MR_CONFIRM) {
        AddFormattedStringMap(u"TICKET_ACTION", u"bypass %s_%d", u"_mrsl03", actionId);
    }

    std::u16string ticketInfo;
    if (ticketType == 1) {
        const double winRate = GetWinRate(raceId, resultType, firstLane, firstLane, firstWinner, firstWinner, firstFallback);
        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"raceid", u"%d", raceId);
        AddFormattedStringLocalMap(parameters, u"lane1", u"%d", firstLane);
        AddFormattedStringLocalMap(parameters, u"bet_adena", u"%d", betAdena);
        AddFormattedStringLocalMap(parameters, u"win1", u"%d", firstWinner);
        AddFormattedStringLocalMap(parameters, u"winrate", u"%.2f", winRate);
        ticketInfo += MakeHTMLFromStrTemplet(1, parameters);
        const std::u16string hostTicketInfo(ticketInfo);
        AddStringMap(u"TICKET_INFO", hostTicketInfo);
    } else if (ticketType == 2) {
        const double winRate =
            GetWinRate(raceId, resultType, firstLane, secondLane, firstWinner, secondWinner, secondFallback);
        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"raceid", u"%d", raceId);
        AddFormattedStringLocalMap(parameters, u"lane1", u"%d", firstLane);
        AddFormattedStringLocalMap(parameters, u"lane2", u"%d", secondLane);
        AddFormattedStringLocalMap(parameters, u"bet_adena", u"%d", betAdena);
        AddFormattedStringLocalMap(parameters, u"win1", u"%d", firstWinner);
        AddFormattedStringLocalMap(parameters, u"win2", u"%d", secondWinner);
        AddFormattedStringLocalMap(parameters, u"winrate", u"%.2f", winRate);
        ticketInfo += MakeHTMLFromStrTemplet(2, parameters);
        const std::u16string hostTicketInfo(ticketInfo);
        AddStringMap(u"TICKET_INFO", hostTicketInfo);
    }

    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam)));
}

void CMonRaceHTML::MonRaceView31(const int, const int, int rowCount, const int, const unsigned char* packet) {
    // Current L2Server 0x0084bbfc.
    L2_GUARD_FUNCTION();
    if (rowCount > 7) {
        rowCount = 7;
    }

    GetBodyFromFile(u"mr_vw31.htm", static_cast<int>(m_nCountryCode));
    std::u16string raceList;
    while (rowCount > 0) {
        --rowCount;
        int raceId = 0;
        int ignoredSecond = 0;
        int ignoredThird = 0;
        int ignoredFourth = 0;
        int ignoredFifth = 0;
        int ignoredSixth = 0;
        int ignoredSeventh = 0;
        int ignoredEighth = 0;
        int ignoredNinth = 0;
        int firstWinner = 0;
        int secondWinner = 0;
        double firstWinRate = 0.0;
        double secondWinRate = 0.0;
        int ignoredFourteenth = 0;
        packet = ::Disassemble(
            packet, "dddddddddddffd", &raceId, &ignoredSecond, &ignoredThird, &ignoredFourth, &ignoredFifth, &ignoredSixth,
            &ignoredSeventh, &ignoredEighth, &ignoredNinth, &firstWinner, &secondWinner, &firstWinRate, &secondWinRate,
            &ignoredFourteenth
        );

        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"raceid", u"%d", raceId);
        AddFormattedStringLocalMap(parameters, u"win1", u"%d", firstWinner);
        AddFormattedStringLocalMap(parameters, u"win2", u"%d", secondWinner);
        AddFormattedStringLocalMap(parameters, u"winrate1", u"%.2f", firstWinRate);
        AddFormattedStringLocalMap(parameters, u"winrate2", u"%.2f", secondWinRate);
        raceList += MakeHTMLFromStrTemplet(1, parameters);
    }

    // Current 0x0084bfda passes the generated value itself as the format.
    AddFormattedStringMap(u"RACE_LIST", raceList);
    AddFormattedStringMap(u"LINK_HOME", u"bypass %s", u"_mrkeeper");
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam)));
}

bool CMonRacePacket::DoReply(const unsigned char* packet) {
    // Current L2Server 0x0084c0b0.
    L2_GUARD_FUNCTION();
    if (m_nType == 1) {
        int runnerId = 0;
        int initialWin = 0;
        int runCount = 0;
        int winCount = 0;
        ::Disassemble(packet, "dddd", &runnerId, &initialWin, &runCount, &winCount);
        if (runnerId > 0) {
            g_MonRaceDB.SetInitialWin(runnerId, initialWin, runCount, winCount);
        }
    } else if (m_nType == 2) {
        int raceId = 0;
        ::Disassemble(packet, "d", &raceId);
        if (raceId > 0) {
            g_MonRaceDB.CreateRace(raceId);
        }
    } else if (m_nType == 4) {
        int firstCount = 0;
        int firstTotal = 0;
        int secondCount = 0;
        int secondTotal = 0;
        packet = ::Disassemble(packet, "dddd", &firstCount, &firstTotal, &secondCount, &secondTotal);
        BettingResult(firstCount, firstTotal, secondCount, secondTotal, packet);
    } else if (m_nType == 5) {
        int result = 0;
        int taxSum = 0;
        ::Disassemble(packet, "dd", &result, &taxSum);
        if (result > 0) {
            g_MonRaceDB.RequestUpdateMon();
            CResidence* const residence = g_ResidenceDB.GetResidence(g_MonRaceDB.m_nResidenceID);
            if (residence != nullptr) {
                residence->ReportShopTax(taxSum, static_cast<CastleType::Enum>(residence->m_data->m_ResidenceType), true);
            }
        }
    } else if (m_nType == 6) {
        int ignoredFirst = 0;
        int ignoredSecond = 0;
        int ignoredThird = 0;
        int ignoredFourth = 0;
        ::Disassemble(packet, "dddd", &ignoredFirst, &ignoredSecond, &ignoredThird, &ignoredFourth);
    } else if (m_nType == 7) {
        int first = 0;
        int userServerId = 0;
        int rowCount = 0;
        int page = 0;
        packet = ::Disassemble(packet, "dddd", &first, &userServerId, &rowCount, &page);
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user[%x]",
            //     L"MonRaceData.cpp",
            //     0x755,
            //     userServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid user[%x]",
            //     L"MonRaceData.cpp",
            //     0x755,
            //     userServerId)
            L2_LOG_WARN(
                Log,
                "Monster race ticket-list reply (type 7, page %d, %d rows) ignored: player smart ID 0x%08x is unavailable.",
                page, rowCount, static_cast<unsigned>(userServerId)
            );
            return false;
        }
        CUserSocket* const socket = user->GetSocket();
        if (socket == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid usersocket[%x]",
            //     L"MonRaceData.cpp",
            //     0x75d,
            //     userServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid usersocket[%x]",
            //     L"MonRaceData.cpp",
            //     0x75d,
            //     userServerId)
            L2_LOG_WARN(
                Log,
                "Monster race ticket-list reply (type 7, page %d, %d rows) ignored: player smart ID 0x%08x has no socket.",
                page, rowCount, static_cast<unsigned>(userServerId)
            );
            return false;
        }
        CMonRaceHTML html(socket);
        html.MonRaceSell1(first, userServerId, rowCount, page, packet);
    } else if (m_nType == 8) {
        int first = 0;
        int userServerId = 0;
        packet = ::Disassemble(packet, "dd", &first, &userServerId);
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user[%x]",
            //     L"MonRaceData.cpp",
            //     0x771,
            //     userServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid user[%x]",
            //     L"MonRaceData.cpp",
            //     0x771,
            //     userServerId)
            L2_LOG_WARN(
                Log, "Monster race ticket-detail reply (type 8) ignored: player smart ID 0x%08x is unavailable.",
                static_cast<unsigned>(userServerId)
            );
            return false;
        }
        CUserSocket* const socket = user->GetSocket();
        if (socket == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid usersocket[%x]",
            //     L"MonRaceData.cpp",
            //     0x779,
            //     userServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid usersocket[%x]",
            //     L"MonRaceData.cpp",
            //     0x779,
            //     userServerId)
            L2_LOG_WARN(
                Log, "Monster race ticket-detail reply (type 8) ignored: player smart ID 0x%08x has no socket.",
                static_cast<unsigned>(userServerId)
            );
            return false;
        }
        CMonRaceHTML html(socket);
        html.MonRaceSell2(first, userServerId, packet);
    } else if (m_nType == 9) {
        int characterDatabaseId = 0;
        int userServerId = 0;
        int check = 0;
        int itemDatabaseId = 0;
        int adenaDatabaseId = 0;
        std::int64_t adena = 0;
        ::Disassemble(
            packet, "dddddQ", &characterDatabaseId, &userServerId, &check, &itemDatabaseId, &adenaDatabaseId, &adena
        );
        DelMonRaceTicket(characterDatabaseId, userServerId, check, itemDatabaseId, adenaDatabaseId, adena);
    } else if (m_nType == 10) {
        int first = 0;
        int userServerId = 0;
        int page = 0;
        int rowCount = 0;
        packet = ::Disassemble(packet, "dddd", &first, &userServerId, &page, &rowCount);
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid user[%x]",
            //     L"MonRaceData.cpp",
            //     0x798,
            //     userServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid user[%x]",
            //     L"MonRaceData.cpp",
            //     0x798,
            //     userServerId)
            L2_LOG_WARN(
                Log,
                "Monster race history reply (type 10, page %d, %d rows) ignored: player smart ID 0x%08x is unavailable.",
                page, rowCount, static_cast<unsigned>(userServerId)
            );
            return false;
        }
        CUserSocket* const socket = user->GetSocket();
        if (socket == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid usersocket[%x]",
            //     L"MonRaceData.cpp",
            //     0x7a0,
            //     userServerId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid usersocket[%x]",
            //     L"MonRaceData.cpp",
            //     0x7a0,
            //     userServerId)
            L2_LOG_WARN(
                Log, "Monster race history reply (type 10, page %d, %d rows) ignored: player smart ID 0x%08x has no socket.",
                page, rowCount, static_cast<unsigned>(userServerId)
            );
            return false;
        }
        CMonRaceHTML html(socket);
        html.MonRaceView31(first, userServerId, rowCount, page, packet);
    }
    return true;
}

bool MonRaceHTML(CSocket* const socket, const SharedUtilsWideChar* const targetCommand) {
    // Current L2Server 0x0084c714.
    L2_GUARD_FUNCTION();
    const std::wstring portableCommand = Utf16ToWide(targetCommand);
    const wchar_t* const command = portableCommand.c_str();
    CUserSocket* const userSocket = static_cast<CUserSocket*>(socket);
    User* const user = userSocket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] No user... on Html CMD Packet (MonRaceHTML)",
        //     L"MonRaceData.cpp",
        //     0x889)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] No user... on Html CMD Packet (MonRaceHTML)",
        //     L"MonRaceData.cpp",
        //     0x889)
        L2_LOG_WARN(Log, "Monster race HTML command ignored: socket has no player.");
        return true;
    }

    const auto matches = [command](const wchar_t* const prefix) {
        return std::wcsncmp(command, prefix, std::wcslen(prefix)) == 0;
    };
    const auto arguments = [targetCommand](const wchar_t* const prefix) { return targetCommand + std::wcslen(prefix) + 1; };

    if (matches(L"_mrkeeper")) {
        CMonRaceHTML html(userSocket);
        html.MonRaceKeeper();
    } else if (matches(L"_mrvw01")) {
        CMonRaceHTML html(userSocket);
        html.MonRaceView1();
    } else if (matches(L"_mrvw11")) {
        CMonRaceHTML html(userSocket);
        html.MonRaceView11();
    } else if (matches(L"_mrvw20")) {
        CMonRaceHTML html(userSocket);
        html.MonRaceView20();
    } else if (matches(L"_mrbu01")) {
        int selectedLane = 0;
        ParseHTMLParam(arguments(L"_mrbu01"), const_cast<char*>("d"), &selectedLane);
        CMonRaceHTML html(userSocket);
        html.MonRaceBuy1(selectedLane);
    } else if (matches(L"_mrbu02")) {
        int lane = 0;
        int amountIndex = 0;
        ParseHTMLParam(arguments(L"_mrbu02"), const_cast<char*>("dd"), &lane, &amountIndex);
        CMonRaceHTML html(userSocket);
        html.MonRaceBuy2(lane, amountIndex);
    } else if (matches(L"_mrbu03")) {
        int lane = 0;
        int amountIndex = 0;
        int confirmation = 0;
        ParseHTMLParam(arguments(L"_mrbu03"), const_cast<char*>("ddd"), &lane, &amountIndex, &confirmation);
        CMonRaceHTML html(userSocket);
        html.MonRaceBuy3(lane, amountIndex, confirmation);
    } else if (matches(L"_mrbu04")) {
        int lane = 0;
        int amountIndex = 0;
        int confirmation = 0;
        ParseHTMLParam(arguments(L"_mrbu04"), const_cast<char*>("ddd"), &lane, &amountIndex, &confirmation);
        CMonRaceHTML html(userSocket);
        html.MonRaceBuy4(lane, amountIndex, confirmation);
    } else if (matches(L"_mrbu11")) {
        int selectedLane = 0;
        ParseHTMLParam(arguments(L"_mrbu11"), const_cast<char*>("d"), &selectedLane);
        CMonRaceHTML html(userSocket);
        html.MonRaceBuy11(selectedLane);
    } else if (matches(L"_mrbu12")) {
        int firstLane = 0;
        int secondLane = 0;
        ParseHTMLParam(arguments(L"_mrbu12"), const_cast<char*>("dd"), &firstLane, &secondLane);
        CMonRaceHTML html(userSocket);
        if (firstLane == secondLane) {
            html.MonRaceBuy11(0);
        } else {
            html.MonRaceBuy12(std::min(firstLane, secondLane), std::max(firstLane, secondLane));
        }
    } else if (matches(L"_mrbu13")) {
        int firstLane = 0;
        int secondLane = 0;
        int amountIndex = 0;
        ParseHTMLParam(arguments(L"_mrbu13"), const_cast<char*>("ddd"), &firstLane, &secondLane, &amountIndex);
        CMonRaceHTML html(userSocket);
        if (firstLane < 1 || secondLane < 1) {
            html.MonRaceBuy11(0);
        } else {
            html.MonRaceBuy13(firstLane, secondLane, amountIndex);
        }
    } else if (matches(L"_mrbu14")) {
        int firstLane = 0;
        int secondLane = 0;
        int amountIndex = 0;
        ParseHTMLParam(arguments(L"_mrbu14"), const_cast<char*>("ddd"), &firstLane, &secondLane, &amountIndex);
        CMonRaceHTML html(userSocket);
        if (firstLane < 1 || secondLane < 1 || amountIndex < 1 || amountIndex > 8) {
            html.MonRaceBuy11(0);
        } else {
            html.MonRaceBuy14(firstLane, secondLane, amountIndex, 0);
        }
    } else if (matches(L"_mrbu15")) {
        int firstLane = 0;
        int secondLane = 0;
        int amountIndex = 0;
        int confirmation = 0;
        ParseHTMLParam(
            arguments(L"_mrbu15"), const_cast<char*>("dddd"), &firstLane, &secondLane, &amountIndex, &confirmation
        );
        CMonRaceHTML html(userSocket);
        if (firstLane < 1 || secondLane < 1 || amountIndex < 1 || amountIndex > 8 || confirmation < 1) {
            html.MonRaceBuy11(0);
        } else {
            html.MonRaceBuy15(firstLane, secondLane, amountIndex, confirmation);
        }
    } else if (matches(L"_mrsl01")) {
        int page = 1;
        ParseHTMLParam(arguments(L"_mrsl01"), const_cast<char*>("d"), &page);
        db.RequestGetMonRaceTicket(user, page);
    } else if (matches(L"_mrsl02")) {
        int actionId = 0;
        ParseHTMLParam(arguments(L"_mrsl02"), const_cast<char*>("d"), &actionId);
        db.RequestGetMonRaceTicketInfo(user, actionId);
    } else if (matches(L"_mrsl03")) {
        int actionId = 0;
        ParseHTMLParam(arguments(L"_mrsl03"), const_cast<char*>("d"), &actionId);
        db.RequestDelMonRaceTicket(user, actionId, g_MonRaceDB.m_nRaceId, g_MonRaceDB.m_status > MR_CONFIRM);
    } else if (matches(L"_mrvw31")) {
        int page = 1;
        ParseHTMLParam(arguments(L"_mrvw31"), const_cast<char*>("d"), &page);
        db.RequestGetMonRaceResult(user, page);
    } else if (matches(L"_mrcreate")) {
        if (g_MonRaceDB.m_status == MR_CLEARED) {
            g_MonRaceDB.ChangeRaceStatus(MR_PREPARE);
            g_MonRaceDB.RequestCreateMonRace();
        }
    } else if (matches(L"_mrsellend")) {
        g_MonRaceDB.ChangeRaceStatus(MR_AFTER_SELL);
    } else if (matches(L"_mrrun") && (g_MonRaceDB.m_status == MR_SELL_TICKET || g_MonRaceDB.m_status == MR_AFTER_SELL)) {
        g_MonRaceDB.ChangeRaceStatus(MR_RUN);
    }
    return true;
}

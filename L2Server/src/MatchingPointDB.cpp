#include "MatchingPointDB.h"
#include "Shared/src/TextRuntime.h"

#include "LexerMatchingPoint.h"
#include "ParserMatchingPoint.h"
#include "PledgeGameSystem.h"
#include "User.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

CMatchingPointDB g_MatchingPointDB;

void OlympiadRatingConfig::SetS(const int grade, const float lose, const float win, const float draw, const float penalty) {
    aSs[grade].fLose = lose;
    aSs[grade].fWin = win;
    aSs[grade].fDraw = draw;
    aSs[grade].fPenalty = penalty;
}

CMatchingPointDB::CMatchingPointDB() :
    m_nConstK{}, m_fSaArray{}, m_nMatchingPointMax{}, m_nMatchingPointMin{}, m_Olympiad{} {
    for (int type = 0; type < MatchingPoint::MPT_MAX; ++type) {
        m_nMatchingPointMax[type] = std::numeric_limits<std::int64_t>::max();
        m_nMatchingPointMin[type] = 1;
    }
    m_Olympiad.nPointMin = 10;
    m_Olympiad.nPointMax = std::numeric_limits<std::int64_t>::max();
}

bool CMatchingPointDB::LoadLexYacc() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\ratingpoint.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/ratingpoint.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from file failed",
        //     kSource,
        //     0x6d)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from file failed",
        //     kSource,
        //     0x6d)
        L2_LOG_ERROR(Log, "Cannot read ratingpoint.txt; matching-point loading fails.");
        return false;
    }

    CLexerForMatchingPoint lexer;
    CParserForMatchingPoint parser;
    lexer.m_inputBuffer = input.GetRawString() + 1;
    lexer.m_inputBufferOffset = 0;
    lexer.m_inputBufferLength = static_cast<std::uint64_t>(input.GetRawStringLen() - 1);

    // The original lexer.yycreate(&parser) set up the old lexer runtime here.
    // The re2c scanner needs no setup, so that step and its error report are gone.
    if (!parser.Create(&lexer)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForMatchingPoint create failed",
        //     kSource,
        //     0x85)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForMatchingPoint create failed",
        //     kSource,
        //     0x85)
        L2_LOG_ERROR(Log, "Cannot initialize the ratingpoint.txt parser; matching-point loading fails.");
        return false;
    }
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing CParserForMatchingPoint file failed after line[%d]",
        //     kSource,
        //     0x8c,
        //     lexer.yylineno)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing CParserForMatchingPoint file failed after line[%d]",
        //     kSource,
        //     0x8c,
        //     lexer.yylineno)
        L2_LOG_ERROR(
            Log, "Cannot parse ratingpoint.txt at line %d; loading fails and already assigned ratings are retained.",
            lexer.yylineno
        );
        return false;
    }
    if (!ValidateScript()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Validate CParserForMatchingPoint file failed after",
        //     kSource,
        //     0x93)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Validate CParserForMatchingPoint file failed after",
        //     kSource,
        //     0x93)
        // Noise: ValidateScript already reports the first invalid value before returning false.
        return false;
    }
    return true;
}

bool CMatchingPointDB::ValidateScript() {
    L2_GUARD_FUNCTION();

    for (int type = 0; type < MatchingPoint::MPT_MAX; ++type) {
        if (type == MatchingPoint::MPT_OLYMPIAD) {
            continue;
        }
        const std::int64_t minimum = m_nMatchingPointMin[type];
        const std::int64_t maximum = m_nMatchingPointMax[type];
        if (minimum < 1 || maximum <= minimum || maximum > std::numeric_limits<std::int64_t>::max() - 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid value, check range, RateMin[%d]=%I64d, RateMax[%d]=%I64d",
            //     L"CMatchingPointDB::ValidateScript",
            //     0xad,
            //     type,
            //     static_cast<long long>(minimum),
            //     type,
            //     static_cast<long long>(maximum))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid value, check range, RateMin[%d]=%I64d, RateMax[%d]=%I64d",
            //     L"CMatchingPointDB::ValidateScript",
            //     0xad,
            //     type,
            //     static_cast<long long>(minimum),
            //     type,
            //     static_cast<long long>(maximum))
            L2_LOG_ERROR(
                Log,
                "Invalid matching-point range for type %d: minimum %lld, maximum %lld; require 1 <= minimum < maximum <= "
                "9223372036854775806.",
                type, static_cast<long long>(minimum), static_cast<long long>(maximum)
            );
            return false;
        }
        if (m_nConstK[type] < 1 || m_nConstK[type] >= 1000000000LL) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid value, check range, m_nConstK[%d]=%I64d",
            //     L"CMatchingPointDB::ValidateScript",
            //     0xb5,
            //     type,
            //     static_cast<long long>(m_nConstK[type]))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid value, check range, m_nConstK[%d]=%I64d",
            //     L"CMatchingPointDB::ValidateScript",
            //     0xb5,
            //     type,
            //     static_cast<long long>(m_nConstK[type]))
            L2_LOG_ERROR(
                Log, "Invalid matching-point K for type %d: %lld; expected 1 through 999999999.", type,
                static_cast<long long>(m_nConstK[type])
            );
            return false;
        }
        for (const float value : m_fSaArray[type]) {
            if (value < -0.01F || value > 1.01F) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] invalid value, check range, m_fsArray[%d]",
                //     L"CMatchingPointDB::ValidateScript",
                //     0xbf,
                //     type)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] invalid value, check range, m_fsArray[%d]",
                //     L"CMatchingPointDB::ValidateScript",
                //     0xbf,
                //     type)
                L2_LOG_ERROR(
                    Log, "Invalid matching-point outcome score for type %d: %.9g; expected -0.01 through 1.01.", type,
                    static_cast<double>(value)
                );
                return false;
            }
        }
    }

    if (m_Olympiad.nPointMin < 1 || m_Olympiad.nPointMax <= m_Olympiad.nPointMin ||
        m_Olympiad.nPointMax > std::numeric_limits<std::int64_t>::max() - 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid value, check range in olympiad, PointMin=%I64d, PointMax=%I64d",
        //     L"CMatchingPointDB::ValidateScript",
        //     0xce,
        //     static_cast<long long>(m_Olympiad.nPointMin),
        //     static_cast<long long>(m_Olympiad.nPointMax))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid value, check range in olympiad, PointMin=%I64d, PointMax=%I64d",
        //     L"CMatchingPointDB::ValidateScript",
        //     0xce,
        //     static_cast<long long>(m_Olympiad.nPointMin),
        //     static_cast<long long>(m_Olympiad.nPointMax))
        L2_LOG_ERROR(
            Log,
            "Invalid Olympiad point range: minimum %lld, maximum %lld; require 1 <= minimum < maximum <= "
            "9223372036854775806.",
            static_cast<long long>(m_Olympiad.nPointMin), static_cast<long long>(m_Olympiad.nPointMax)
        );
        return false;
    }
    for (int grade = 0; grade < 4; ++grade) {
        if (m_Olympiad.anKs[grade] < 1 || m_Olympiad.anKs[grade] >= 1000000000) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid value, check range in olympiad, m_Olympiad.anKs[%d]=%I64d",
            //     L"CMatchingPointDB::ValidateScript",
            //     0xd8,
            //     grade,
            //     m_Olympiad.anKs[grade])
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid value, check range in olympiad, m_Olympiad.anKs[%d]=%I64d",
            //     L"CMatchingPointDB::ValidateScript",
            //     0xd8,
            //     grade,
            //     m_Olympiad.anKs[grade])
            L2_LOG_ERROR(
                Log, "Invalid Olympiad K for grade %d: %d; expected 1 through 999999999.", grade, m_Olympiad.anKs[grade]
            );
            return false;
        }
    }
    for (int level = 0; level <= 120; ++level) {
        const std::int64_t point = m_Olympiad.anBasePoints[level];
        if (point < 0 || point >= m_Olympiad.nPointMax) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid value, check range, m_Olympiad.anBasePoints[%d]=%I64d",
            //     L"CMatchingPointDB::ValidateScript",
            //     0xe4,
            //     level,
            //     static_cast<long long>(point))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid value, check range, m_Olympiad.anBasePoints[%d]=%I64d",
            //     L"CMatchingPointDB::ValidateScript",
            //     0xe4,
            //     level,
            //     static_cast<long long>(point))
            L2_LOG_ERROR(
                Log, "Invalid Olympiad base points for level %d: %lld; expected at least 0 and less than %lld.", level,
                static_cast<long long>(point), static_cast<long long>(m_Olympiad.nPointMax)
            );
            return false;
        }
    }
    return true;
}

bool CMatchingPointDB::GetMatchingPointBase(
    const MatchingPoint::MATCHINGPOINT_TYPE type, const int level, std::int64_t& point
) {
    L2_GUARD_FUNCTION();
    point = type == MatchingPoint::MPT_OLYMPIAD ? m_Olympiad.anBasePoints[level] : DEFAULT_BASE_POINT;
    return true;
}

bool CMatchingPointDB::SortMembersWithMatchingPoint(
    const MatchingPoint::MATCHINGPOINT_TYPE type, std::vector<User*>& users
) {
    L2_GUARD_FUNCTION();
    if (type < MatchingPoint::MPT_BASE || type >= MatchingPoint::MPT_MAX) {
        return false;
    }
    const int index = static_cast<int>(type);
    std::sort(users.begin(), users.end(), [index](const User* left, const User* right) {
        return left->m_MatchingPoint.m_nMatchingPointTotal[index] < right->m_MatchingPoint.m_nMatchingPointTotal[index];
    });
    if (g_PledgeGameSystem.m_nDebugUserSID != 0) {
        g_PledgeGameSystem.DebugString(u"Reply LoadPoint");
        for (const User* const user : users) {
            g_PledgeGameSystem.DebugString(
                u"%s: base:%lld, diff:%lld, total:%lld", user->GetName(),
                static_cast<long long>(user->m_MatchingPoint.m_nMatchingPointBase[index]),
                static_cast<long long>(user->m_MatchingPoint.m_nMatchingPointDiff[index]),
                static_cast<long long>(user->m_MatchingPoint.m_nMatchingPointTotal[index])
            );
        }
    }
    return true;
}

bool CMatchingPointDB::GenerateWinningRate(std::vector<PledgeGamePlayerInfo>& players) {
    L2_GUARD_FUNCTION();
    if (players.size() <= 1) {
        return false;
    }

    std::int64_t totalPoint = 0;
    for (const PledgeGamePlayerInfo& player : players) {
        totalPoint += player.nMatchingPointTotal;
        if (player.nMatchingPointTotal == 0) {
            // const std::wstring name = target_utf16::ToHostCodeUnits(player.playerName)
            const std::string name = FixedUtf16ToUtf8(player.playerName);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[MatchPoint][%s][%d] iter->nMatchingPointTotal is zero: %s(%d)",
            //     kSource,
            //     0x2fd,
            //     name.c_str(),
            //     player.playerDBID)
            // L2_LOG_ERROR(Log,
            //     L"[MatchPoint][%s][%d] iter->nMatchingPointTotal is zero: %s(%d)",
            //     kSource,
            //     0x2fd,
            //     name.c_str(),
            //     player.playerDBID)
            L2_LOG_ERROR(
                Log,
                "Cannot calculate winning rates: player '%s' (DB ID %u) has zero total matching points; rates are left "
                "unchanged.",
                name.c_str(), static_cast<unsigned>(player.playerDBID)
            );
            return false;
        }
    }
    if (totalPoint == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MatchPoint][%s][%d] nTotalPoint is zero.",
        //     kSource,
        //     0x305)
        // L2_LOG_ERROR(Log,
        //     L"[MatchPoint][%s][%d] nTotalPoint is zero.",
        //     kSource,
        //     0x305)
        L2_LOG_ERROR(
            Log,
            "Cannot calculate winning rates for %zu players: their combined matching points are zero; rates are left "
            "unchanged.",
            static_cast<std::size_t>(players.size())
        );
        return false;
    }

    for (PledgeGamePlayerInfo& player : players) {
        player.fWinningRate = static_cast<float>(player.nMatchingPointTotal) / static_cast<float>(totalPoint);
        CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(player.playerSID));
        if (user) {
            user->m_MatchingPoint.m_fWinningRate[0] = std::clamp(player.fWinningRate, 0.0F, 1.0F);
        }
    }
    return true;
}

bool CMatchingPointDB::UpdateMatchingPoint(
    const PledgeGame::GAMERESULT_TYPE result, const unsigned int winnerDBID, std::vector<PledgeGamePlayerInfo>& players
) {
    L2_GUARD_FUNCTION();
    if (players.size() <= 1) {
        return false;
    }

    L2_GUARD(m_lock);
    for (PledgeGamePlayerInfo& player : players) {
        float score = 0.0F;
        if (result == PledgeGame::GRT_DRAW) {
            score = m_fSaArray[MatchingPoint::MPT_BASE][2];
        } else {
            score = m_fSaArray[MatchingPoint::MPT_BASE][winnerDBID == player.playerDBID ? 1 : 0];
        }
        player.nWinningPoint = static_cast<std::int64_t>(
            (score - player.fWinningRate) * static_cast<float>(m_nConstK[MatchingPoint::MPT_BASE])
        );
    }
    return true;
}

std::int64_t CMatchingPointDB::GuessMatchingPoint(
    const MatchingPoint::MATCHINGPOINT_TYPE type,
    const PledgeGame::GAMERESULT_TYPE result,
    const MatchingPoint::MatchingPointModel& point
) {
    if (type < MatchingPoint::MPT_BASE || type >= MatchingPoint::MPT_MAX || result < PledgeGame::GRT_BASE ||
        result >= PledgeGame::GRT_MAX) {
        return 0;
    }
    L2_GUARD(m_lock);
    const float score = result == PledgeGame::GRT_DRAW
        ? m_fSaArray[MatchingPoint::MPT_BASE][2]
        : m_fSaArray[MatchingPoint::MPT_BASE][result == PledgeGame::GRT_WIN ? 1 : 0];
    return static_cast<std::int64_t>(
        (score - point.m_fWinningRate[MatchingPoint::MPT_BASE]) * static_cast<float>(m_nConstK[MatchingPoint::MPT_BASE])
    );
}

std::int64_t CMatchingPointDB::ValidateMatchingPoint(
    const MatchingPoint::MATCHINGPOINT_TYPE type,
    const std::int64_t base,
    const std::int64_t diff,
    const std::int64_t additional
) {
    L2_GUARD_FUNCTION();
    if (type < MatchingPoint::MPT_BASE || type >= MatchingPoint::MPT_MAX) {
        return 0;
    }
    const int index = static_cast<int>(type);
    return std::min(std::max(base + diff + additional, m_nMatchingPointMin[index]), m_nMatchingPointMax[index]);
}

void CMatchingPointDB::GetOlympiadPoints(
    std::int64_t& point, std::int64_t& k, float& lose, float& win, float& draw, float& penalty
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const int grade = GetOlympiadGrade(static_cast<int>(point));
    k = m_Olympiad.anKs[grade];
    lose = m_Olympiad.aSs[grade].fLose;
    win = m_Olympiad.aSs[grade].fWin;
    draw = m_Olympiad.aSs[grade].fDraw;
    penalty = m_Olympiad.aSs[grade].fPenalty;
}

int CMatchingPointDB::GetOlympiadGrade(const int point) const {
    for (int grade = 3; grade > 0; --grade) {
        if (m_Olympiad.anGrades[grade] <= point) {
            return grade;
        }
    }
    return 0;
}

void MatchingPoint::MatchingPointModel::ValidateMatchingPoint(const MATCHINGPOINT_TYPE type) {
    if (type < MPT_BASE || type >= MPT_MAX) {
        return;
    }
    const int index = static_cast<int>(type);
    m_nMatchingPointTotal[index] =
        g_MatchingPointDB.ValidateMatchingPoint(type, m_nMatchingPointBase[index], m_nMatchingPointDiff[index], 0);
}

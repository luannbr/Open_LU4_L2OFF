#pragma once

#include "Shared/src/SharedType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class User;
struct PledgeGamePlayerInfo;

namespace PledgeGame {
enum GAMERESULT_TYPE : std::int32_t;
}

namespace MatchingPoint {
struct MatchingPointModel;
}

struct OlympiadRatingConfig {
    struct S {
        float fLose;
        float fWin;
        float fDraw;
        float fPenalty;
    };

    void SetS(int grade, float lose, float win, float draw, float penalty);

    std::int64_t nPointMin; // +0x000
    std::int64_t nPointMax; // +0x008
    std::int64_t anGrades[4]; // +0x010
    std::int32_t anKs[4]; // +0x030
    S aSs[4]; // +0x040
    std::int64_t anBasePoints[121]; // +0x080
};

class CMatchingPointDB {
public:
    static constexpr std::int64_t DEFAULT_BASE_POINT = 1000;
    static constexpr std::int64_t DEFAULT_CONST_K = 1;

    CMatchingPointDB();
    ~CMatchingPointDB() = default;

    bool LoadLexYacc();
    bool ValidateScript();
    bool GetMatchingPointBase(MatchingPoint::MATCHINGPOINT_TYPE type, int level, std::int64_t& point);
    bool SortMembersWithMatchingPoint(MatchingPoint::MATCHINGPOINT_TYPE type, std::vector<User*>& users);
    bool GenerateWinningRate(std::vector<PledgeGamePlayerInfo>& players);
    bool UpdateMatchingPoint(
        PledgeGame::GAMERESULT_TYPE result, unsigned int winnerDBID, std::vector<PledgeGamePlayerInfo>& players
    );
    std::int64_t GuessMatchingPoint(
        MatchingPoint::MATCHINGPOINT_TYPE type,
        PledgeGame::GAMERESULT_TYPE result,
        const MatchingPoint::MatchingPointModel& point
    );
    std::int64_t ValidateMatchingPoint(
        MatchingPoint::MATCHINGPOINT_TYPE type, std::int64_t base, std::int64_t diff, std::int64_t additional
    );
    void GetOlympiadPoints(std::int64_t& point, std::int64_t& k, float& lose, float& win, float& draw, float& penalty);
    int GetOlympiadGrade(int point) const;

    std::int64_t m_nConstK[2]; // +0x000
    float m_fSaArray[2][3]; // +0x010
    std::int64_t m_nMatchingPointMax[2]; // +0x028
    std::int64_t m_nMatchingPointMin[2]; // +0x038
    OlympiadRatingConfig m_Olympiad; // +0x048
    CYieldLock m_lock; // +0x490
};

extern CMatchingPointDB g_MatchingPointDB;

#pragma once

#include "Shared/src/DateTime.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>

struct XMLElement;

struct PledgeRewardInfo {
    PledgeRewardInfo() : item_amount(0), item_class(-1), name_value(0) {}

    bool LoadFromXML(const XMLElement* element);

    std::int64_t item_amount;
    int item_class;
    int name_value;
};


struct PledgeTotalGradeInfo {
    PledgeTotalGradeInfo() : point(0) {}

    bool LoadFromXML(const XMLElement* element);

    PledgeRewardInfo Reward;
    int point;
};


struct PledgePeriodRankInfo {
    PledgePeriodRankInfo() : rank(0) {}

    bool LoadFromXML(const XMLElement* element);

    PledgeRewardInfo Reward;
    int rank;
};


struct PledgePeriodGradeInfo {
    PledgePeriodGradeInfo() : ratio(0) {}

    bool LoadFromXML(const XMLElement* element);

    PledgeRewardInfo Reward;
    int ratio;
};


struct PledgePeriodRewardWeightInfo {
    PledgePeriodRewardWeightInfo() : member_count(0), item_factor(1.0F), namevalue_factor(1.0F) {}

    bool LoadFromXML(const XMLElement* element);

    int member_count;
    float item_factor;
    float namevalue_factor;
};


class PledgeContributionDB : public ::Singleton<PledgeContributionDB> {
public:
    struct Setting {
        struct Period {
            int weeks;
            L2DateTime::DayOfWeek weekday;
            int hour;
        } period;
    };

    struct Builder {
        static void Reload(PledgeContributionDB* database);
    };

    PledgeContributionDB();
    ~PledgeContributionDB() override;

    void Load();
    const PledgeTotalGradeInfo* FindTotalGrade(int grade) const;
    const PledgePeriodRankInfo* FindPeriodRank(int rank) const;
    const PledgePeriodGradeInfo* FindPeriodGrade(int ratio) const;
    const PledgePeriodRewardWeightInfo* FindPeriodWeight(int memberCount) const;

    // PledgeContribution.cpp consumes these values directly in the current
    // image. They are not parser-only private storage.
    Setting setting;
    int min_member_count;
    int min_point;


private:
    void LoadSetting(const XMLElement* element);
    void LoadTotalGrades(const XMLElement* element);
    void LoadPeriod(const XMLElement* element);
    void LoadPeriodGrades(const XMLElement* element);
    void LoadPeriodRanks(const XMLElement* element);
    void LoadPeriodRewardWeights(const XMLElement* element);

    std::vector<PledgeTotalGradeInfo*> TotalGrades;
    std::vector<PledgePeriodRankInfo*> PeriodRanks;
    std::vector<PledgePeriodGradeInfo*> PeriodGrades;
    std::vector<PledgePeriodRewardWeightInfo*> PeriodRewardWeights;
};

#pragma once

#include "PledgeInfo.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>

struct XMLElement;

struct PledgeMasteryPointInfo {
    bool LoadFromXML(const XMLElement* element, PledgeMasteryID::Type masteryID, int pointStep);

    PledgeCostInfo Cost;
};


struct PledgeMasteryRequirementInfo {
    PledgeMasteryRequirementInfo() : min_point(0) {}

    bool LoadFromXML(const XMLElement* element);

    std::vector<PledgeMasteryID::Type> MasteryID;
    int min_point;
};


struct PledgeMasteryResetInfo {
    bool LoadFromXML(const XMLElement* element);

    int penalty_day = 0;
    PledgeCostInfo Cost;
};


struct PledgeMasteryInfo {
    PledgeMasteryInfo();
    ~PledgeMasteryInfo();

    int GetMaxPointStep() const;
    bool IsValidPointStep(int pointStep) const;
    const PledgeCostInfo& GetCost(int pointStep) const;
    bool LoadFromXML(const XMLElement* element);
    void LoadRequirements(const XMLElement* element);

    PledgeMasteryID::Type id;
    std::vector<PledgeMasteryRequirementInfo> Require;
    std::vector<PledgeMasteryPointInfo> Points;

private:
};


class PledgeMasteryDB : public ::Singleton<PledgeMasteryDB> {
public:
    class Builder {
    public:
        static void Reload(PledgeMasteryDB* database);
    };

    PledgeMasteryDB();
    ~PledgeMasteryDB() override;

    const PledgeMasteryInfo* FindInfo(PledgeMasteryID::Type masteryID) const;
    bool HasPointStep(PledgeMasteryID::Type masteryID, int pointStep) const;
    const PledgeCostInfo& GetPointCost(PledgeMasteryID::Type masteryID, int pointStep) const;
    void Load();

    PledgeMasteryResetInfo Reset;
    std::map<PledgeMasteryID::Type, PledgeMasteryInfo*> Masteries;

private:
};

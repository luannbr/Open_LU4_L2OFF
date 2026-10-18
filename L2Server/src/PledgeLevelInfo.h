#pragma once

#include "PledgeInfo.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>

struct XMLElement;

struct PledgeLevelInfo {
    PledgeLevelInfo() : level(0) {}
    ~PledgeLevelInfo() = default;

    bool LoadFromXML(const XMLElement* element);

    int level;
    PledgeCostInfo Cost;
};


class PledgeLevelDB : public ::Singleton<PledgeLevelDB> {
public:
    using LevelMap = ::std::map<int, PledgeLevelInfo*>;

    struct Builder {
        static void Reload(PledgeLevelDB* database);
    };

    PledgeLevelDB();
    ~PledgeLevelDB() override;

    void Load();
    const PledgeLevelInfo* FindInfo(int level) const;


private:
    LevelMap Levels;
};

#pragma once

#include "PledgeInfo.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

struct XMLElement;

struct PledgeItemRefill {
    PledgeItemRefill() : interval(0), amount(0) {}

    bool LoadFromXML(const XMLElement* element);

    int interval;
    int amount;
};

static_assert(std::is_trivially_copyable_v<PledgeItemRefill>);
static_assert(std::is_trivially_destructible_v<PledgeItemRefill>);

struct PledgeItemInfo {
    PledgeItemInfo();

    bool LoadFromXML(const XMLElement* element);

    int class_id;
    PledgeAddType::Value add_type;
    int expiry_time;
    PledgeConditionInfo Condition;
    PledgeCostInfo ActiveCost;
    PledgeUserCostInfo SellCost;
    PledgeItemRefill Refill;
};


class PledgeItemIndexer {
public:
    const PledgeItemInfo* Find(int classId) const;
    void Setup(const std::vector<PledgeItemInfo*>& items);

private:
    std::map<int, const PledgeItemInfo*> m_items;
};


class PledgeItemDB : public ::Singleton<PledgeItemDB> {
public:
    struct Builder {
        static void Reload(PledgeItemDB* database);
    };

    PledgeItemDB();
    ~PledgeItemDB() override;

    const std::vector<PledgeItemInfo*>& GetInfos() const;
    const PledgeItemInfo* Find(int classId) const;
    void Load();

private:
    void MakeIndexer();

    std::vector<PledgeItemInfo*> Items;
    PledgeItemIndexer m_Indexer;
};

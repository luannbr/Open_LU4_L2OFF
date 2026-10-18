#pragma once

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

namespace protocol {
struct ItemInfo;
}

namespace AlchemyConditionStructures {

enum AlchemyConditionType : std::int32_t {
    ACT_LEVEL = 1,
    ACT_ROLLY_FIXED = 2,
    ACT_CONVERSION = 3,
};

struct AlchemyCondition {
    explicit AlchemyCondition(AlchemyConditionType type) : m_Type(type) {}

    AlchemyConditionType m_Type; // +0x00
};

struct AlchemyLevelCond final : AlchemyCondition {
    explicit AlchemyLevelCond(std::int32_t level) : AlchemyCondition(ACT_LEVEL), m_nLevel(level) {}

    std::int32_t m_nLevel; // +0x04
};

class AlchemyRollyFixedCond final : public AlchemyCondition {
public:
    AlchemyRollyFixedCond();

    bool IsOK(std::int32_t itemId);
    void AddFixedItem(std::int32_t itemId);

    std::map<std::int32_t, bool> m_mapFixedItem; // +0x08
};

using AlchemyMaterialKey = std::CDualKey<std::int32_t, std::int64_t>;

class AlchemyConvCond final : public AlchemyCondition {
public:
    AlchemyConvCond();

    bool IsOK(std::list<protocol::ItemInfo>& items);

    std::map<AlchemyMaterialKey, bool> m_mapMaterialCond; // +0x08
};

} // namespace AlchemyConditionStructures

namespace AlchemyResultStructures {

enum AlchemyOutputType : std::int32_t {
    AOT_ROLLY = 0,
    AOT_CONVERSION = 1,
};

struct AlchemyOutputBase {
    explicit AlchemyOutputBase(AlchemyOutputType type) : m_Type(type) {}

    AlchemyOutputType m_Type; // +0x00
};

struct AlchemyItemInfo {
    std::int32_t nItemClassId; // +0x00
    std::int64_t nAmount; // +0x08
};

struct AlchemyRollyOutput final : AlchemyOutputBase {
    AlchemyRollyOutput(std::int32_t itemClassId, std::int64_t amount, double probability) :
        AlchemyOutputBase(AOT_ROLLY), m_Item{itemClassId, amount}, m_dProb(probability) {}

    AlchemyItemInfo m_Item; // +0x08
    double m_dProb; // +0x18
};

struct AlchemyConvOutput final : AlchemyOutputBase {
    AlchemyConvOutput() : AlchemyOutputBase(AOT_CONVERSION), m_dProb(0.0), m_nSuccessEffect(0), m_nFailEffect(0) {}

    AlchemyItemInfo m_successItem; // +0x08
    AlchemyItemInfo m_failItem; // +0x18
    double m_dProb; // +0x28
    std::int32_t m_nSuccessEffect; // +0x30
    std::int32_t m_nFailEffect; // +0x34
};

} // namespace AlchemyResultStructures

namespace AlchemyRecipeStructures {

enum AlchemyRecipeType : std::int32_t {
    ART_ROLLY = 0,
    ART_CONVERSION = 1,
};

struct AlchemyRecipe {
    explicit AlchemyRecipe(AlchemyRecipeType type) : m_Type(type) {}

    AlchemyRecipeType m_Type; // +0x00
};

class AlchemyConversionRecipe final : public AlchemyRecipe {
public:
    AlchemyConversionRecipe();

    void AddOutputAndCond(
        AlchemyConditionStructures::AlchemyConvCond* condition, AlchemyResultStructures::AlchemyOutputBase* output
    );
    AlchemyResultStructures::AlchemyOutputBase* GetConversionOutput(std::list<protocol::ItemInfo>& items);

    std::map<AlchemyConditionStructures::AlchemyCondition*, AlchemyResultStructures::AlchemyOutputBase*>
        m_mapRewardMap; // +0x08
    bool m_bMaster; // +0x28
};

class AlchemyRollyRecipe final : public AlchemyRecipe {
public:
    AlchemyRollyRecipe();

    bool IsRewardItem(std::int32_t itemId);
    AlchemyResultStructures::AlchemyOutputBase* GetRollyOutput(std::int32_t fixedItemId, std::int32_t level);
    void AddFixedItem(std::int32_t itemId);
    void AddLevelCond(std::int32_t level);
    void AddOutput(std::int32_t itemClassId, std::int64_t amount, double probability);

    AlchemyConditionStructures::AlchemyRollyFixedCond* m_pFixedCond; // +0x08
    std::vector<AlchemyConditionStructures::AlchemyCondition*> m_vLevelCond; // +0x10
    std::vector<AlchemyResultStructures::AlchemyOutputBase*> m_vOutputList; // +0x30
    std::map<std::int32_t, bool> m_mapRollyReward; // +0x50
    std::int32_t m_nSuccessEffectId; // +0x70
};

} // namespace AlchemyRecipeStructures

class AlchemyDB {
public:
    using ConversionKey = std::CDualKey<std::int32_t, std::int32_t>;

    AlchemyDB();

    void SetRollyRecipe(AlchemyRecipeStructures::AlchemyRollyRecipe* recipe);
    void AddConversionRecipe(std::int32_t skillId, std::int32_t level, AlchemyRecipeStructures::AlchemyRecipe* recipe);
    bool IsRollyReward(std::int32_t itemId);
    AlchemyResultStructures::AlchemyOutputBase* GetCorrectRollyOutput(std::int32_t fixedItemId, std::int32_t level);
    AlchemyResultStructures::AlchemyOutputBase* GetCorrectConvOutput(
        std::int32_t skillId, std::int32_t level, std::list<protocol::ItemInfo>& items
    );
    bool LoadAlchemyData();

    AlchemyRecipeStructures::AlchemyRollyRecipe* m_pAlchemyRollyRecipe; // +0x00
    std::map<ConversionKey, AlchemyRecipeStructures::AlchemyRecipe*> m_mapConversionRecipe; // +0x08
    std::int32_t m_nRollyBaseItemDP; // +0x28
    std::int32_t m_nRollyBaseItemID; // +0x2c
    std::int64_t m_nRollyAmountCriterion; // +0x30
};

extern AlchemyDB g_AlchemyDB;

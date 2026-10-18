#include "AlchemySystem.h"

#include "ParserAlchemy.h"
#include "UserPacketEx.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

AlchemyDB g_AlchemyDB;

namespace AlchemyConditionStructures {

AlchemyRollyFixedCond::AlchemyRollyFixedCond() : AlchemyCondition(ACT_ROLLY_FIXED) {}

bool AlchemyRollyFixedCond::IsOK(const std::int32_t itemId) {
    L2_GUARD_FUNCTION();

    const auto inserted = m_mapFixedItem.emplace(itemId, false);
    return inserted.first->second;
}

void AlchemyRollyFixedCond::AddFixedItem(const std::int32_t itemId) {
    L2_GUARD_FUNCTION();

    m_mapFixedItem.emplace(itemId, true);
}

AlchemyConvCond::AlchemyConvCond() : AlchemyCondition(ACT_CONVERSION) {}

bool AlchemyConvCond::IsOK(std::list<protocol::ItemInfo>& items) {
    L2_GUARD_FUNCTION();

    std::map<AlchemyMaterialKey, bool> available;
    for (const protocol::ItemInfo& item : items) {
        available.emplace(AlchemyMaterialKey(item.nItemClassId, item.nAmount), false);
    }

    for (const auto& requirement : m_mapMaterialCond) {
        if (available.find(requirement.first) == available.end() || !requirement.second) {
            return false;
        }
    }
    return true;
}

} // namespace AlchemyConditionStructures

namespace AlchemyRecipeStructures {

AlchemyConversionRecipe::AlchemyConversionRecipe() : AlchemyRecipe(ART_CONVERSION), m_bMaster(false) {}

void AlchemyConversionRecipe::AddOutputAndCond(
    AlchemyConditionStructures::AlchemyConvCond* condition, AlchemyResultStructures::AlchemyOutputBase* output
) {
    L2_GUARD_FUNCTION();

    m_mapRewardMap.emplace(condition, output);
}

AlchemyResultStructures::AlchemyOutputBase* AlchemyConversionRecipe::GetConversionOutput(std::list<protocol::ItemInfo>& items) {
    L2_GUARD_FUNCTION();

    for (const auto& reward : m_mapRewardMap) {
        AlchemyConditionStructures::AlchemyCondition* const condition = reward.first;
        if (condition->m_Type != AlchemyConditionStructures::ACT_CONVERSION) {
            break;
        }
        auto* const conversionCondition = static_cast<AlchemyConditionStructures::AlchemyConvCond*>(condition);
        if (conversionCondition != nullptr && conversionCondition->IsOK(items)) {
            return reward.second;
        }
    }
    return nullptr;
}

AlchemyRollyRecipe::AlchemyRollyRecipe() :
    AlchemyRecipe(ART_ROLLY),
    m_pFixedCond(nullptr),
    m_nSuccessEffectId(0) {}

bool AlchemyRollyRecipe::IsRewardItem(const std::int32_t itemId) {
    L2_GUARD_FUNCTION();

    const auto found = m_mapRollyReward.find(itemId);
    return found != m_mapRollyReward.end() ? found->second : false;
}

AlchemyResultStructures::AlchemyOutputBase* AlchemyRollyRecipe::GetRollyOutput(
    const std::int32_t fixedItemId, const std::int32_t level
) {
    L2_GUARD_FUNCTION();

    if (m_pFixedCond == nullptr || !m_pFixedCond->IsOK(fixedItemId)) {
        return nullptr;
    }

    for (std::size_t index = 0; index < m_vLevelCond.size(); ++index) {
        AlchemyConditionStructures::AlchemyCondition* const condition = runtime::CheckedVectorAt(m_vLevelCond, index);
        if (condition->m_Type != AlchemyConditionStructures::ACT_LEVEL) {
            break;
        }
        const auto* const levelCondition = static_cast<const AlchemyConditionStructures::AlchemyLevelCond*>(condition);
        if (levelCondition->m_nLevel == level) {
            return runtime::CheckedVectorAt(m_vOutputList, index);
        }
    }
    return nullptr;
}

void AlchemyRollyRecipe::AddFixedItem(const std::int32_t itemId) {
    L2_GUARD_FUNCTION();

    if (m_pFixedCond == nullptr) {
        m_pFixedCond = new AlchemyConditionStructures::AlchemyRollyFixedCond;
    }
    m_pFixedCond->AddFixedItem(itemId);
}

void AlchemyRollyRecipe::AddLevelCond(const std::int32_t level) {
    L2_GUARD_FUNCTION();

    for (AlchemyConditionStructures::AlchemyCondition* const condition : m_vLevelCond) {
        if (condition->m_Type != AlchemyConditionStructures::ACT_LEVEL ||
            static_cast<AlchemyConditionStructures::AlchemyLevelCond*>(condition)->m_nLevel == level) {
            return;
        }
    }
    m_vLevelCond.push_back(new AlchemyConditionStructures::AlchemyLevelCond(level));
}

void AlchemyRollyRecipe::AddOutput(const std::int32_t itemClassId, const std::int64_t amount, const double probability) {
    L2_GUARD_FUNCTION();

    auto* const output = new AlchemyResultStructures::AlchemyRollyOutput(itemClassId, amount, probability);
    m_vOutputList.push_back(output);
    m_mapRollyReward.emplace(itemClassId, true);
}

} // namespace AlchemyRecipeStructures

AlchemyDB::AlchemyDB() :
    m_pAlchemyRollyRecipe(nullptr),
    m_nRollyBaseItemDP(5000),
    m_nRollyBaseItemID(0x9a25),
    m_nRollyAmountCriterion(100000000) {
    L2_GUARD_FUNCTION();
}

void AlchemyDB::SetRollyRecipe(AlchemyRecipeStructures::AlchemyRollyRecipe* const recipe) {
    L2_GUARD_FUNCTION();

    if (recipe != nullptr && recipe->m_Type == AlchemyRecipeStructures::ART_ROLLY) {
        m_pAlchemyRollyRecipe = recipe;
    }
}

void AlchemyDB::AddConversionRecipe(
    const std::int32_t skillId, const std::int32_t level, AlchemyRecipeStructures::AlchemyRecipe* const recipe
) {
    L2_GUARD_FUNCTION();

    m_mapConversionRecipe.emplace(ConversionKey(skillId, level), recipe);
}

bool AlchemyDB::IsRollyReward(const std::int32_t itemId) {
    L2_GUARD_FUNCTION();

    if (itemId == m_nRollyBaseItemID) {
        return true;
    }
    return m_pAlchemyRollyRecipe != nullptr && m_pAlchemyRollyRecipe->IsRewardItem(itemId);
}

AlchemyResultStructures::AlchemyOutputBase* AlchemyDB::GetCorrectRollyOutput(
    const std::int32_t fixedItemId, const std::int32_t level
) {
    L2_GUARD_FUNCTION();

    return m_pAlchemyRollyRecipe != nullptr ? m_pAlchemyRollyRecipe->GetRollyOutput(fixedItemId, level) : nullptr;
}

AlchemyResultStructures::AlchemyOutputBase* AlchemyDB::GetCorrectConvOutput(
    const std::int32_t skillId, const std::int32_t level, std::list<protocol::ItemInfo>& items
) {
    L2_GUARD_FUNCTION();

    const auto found = m_mapConversionRecipe.find(ConversionKey(skillId, level));
    if (found == m_mapConversionRecipe.end() || found->second->m_Type != AlchemyRecipeStructures::ART_CONVERSION) {
        return nullptr;
    }
    auto* const recipe = static_cast<AlchemyRecipeStructures::AlchemyConversionRecipe*>(found->second);
    return recipe != nullptr ? recipe->GetConversionOutput(items) : nullptr;
}

bool AlchemyDB::LoadAlchemyData() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\alchemydata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/alchemydata.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"AlchemySystem.cpp",
        //     0x27)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"AlchemySystem.cpp",
        //     0x27)
        L2_LOG_ERROR(Log, "Cannot read alchemydata.txt; alchemy loading stops.");
        return false;
    }

    CParserForAlchemy parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForAlchemy::create failed",
        //     L"AlchemySystem.cpp",
        //     0x30)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForAlchemy::create failed",
        //     L"AlchemySystem.cpp",
        //     0x30)
        L2_LOG_ERROR(Log, "Cannot initialize alchemydata.txt parser; alchemy loading stops.");
        return false;
    }

    parser.m_lexer.yybegin(4);
    const int result = parser.yyparse();
    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing CParserForAlchemy file failed after line[%d]",
        //     L"AlchemySystem.cpp",
        //     0x3c,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing CParserForAlchemy file failed after line[%d]",
        //     L"AlchemySystem.cpp",
        //     0x3c,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse alchemydata.txt at script line %lld; recipes registered before the error are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
    return result == 0;
}

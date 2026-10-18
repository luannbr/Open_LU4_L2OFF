#pragma once

#include "LexerAlchemy.h"

#include <cstddef>
#include <cstdint>

namespace AlchemyConditionStructures {
struct AlchemyCondition;
class AlchemyConvCond;
} // namespace AlchemyConditionStructures

namespace AlchemyRecipeStructures {
class AlchemyConversionRecipe;
class AlchemyRollyRecipe;
} // namespace AlchemyRecipeStructures

namespace AlchemyResultStructures {
struct AlchemyOutputBase;
struct AlchemyConvOutput;
} // namespace AlchemyResultStructures

class CParserForAlchemy {
public:
    CParserForAlchemy();
    ~CParserForAlchemy();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForAlchemy m_lexer; // +0xf0

private:
    [[maybe_unused]] void* m_pAlchemyOutputList; // +0x1e0, current-unused parser lane
    AlchemyResultStructures::AlchemyConvOutput* m_pConvResult; // +0x1e8
    [[maybe_unused]] AlchemyConditionStructures::AlchemyCondition* m_pCondition; // +0x1f0
    AlchemyConditionStructures::AlchemyConvCond* m_pConversionCondition; // +0x1f8
    AlchemyRecipeStructures::AlchemyConversionRecipe* m_pConversionRecipe; // +0x200
    AlchemyRecipeStructures::AlchemyRollyRecipe* m_pMixRecipe; // +0x208
    std::int32_t m_nKeyId; // +0x210
    std::int32_t m_nLevel; // +0x214
};

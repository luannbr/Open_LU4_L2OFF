#pragma once

#include "LexerTransform.h"
#include "Transform.h"

#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CParserForTransform final {
public:
    enum ErrorType : std::int32_t {
        ERROR_COLLISION = 0,
        ERROR_SPEED = 1,
        ERROR_NON_COMBAT = 2,
        ERROR_WEAPON = 3,
        ERROR_STAT = 4,
        ERROR_HP_REGEN = 5,
        ERROR_MP_REGEN = 6,
        ERROR_CP_REGEN = 7,
        ERROR_DEFEND = 8,
        ERROR_MAGIC_DEFEND = 9,
        ERROR_LEVEL_BONUS = 10,
        ERROR_STR_BONUS = 11,
        ERROR_INT_BONUS = 12,
        ERROR_CON_BONUS = 13,
        ERROR_DEX_BONUS = 14,
        ERROR_WIT_BONUS = 15,
        ERROR_MEN_BONUS = 16,
        ERROR_HP_TABLE = 17,
        ERROR_MP_TABLE = 18,
        ERROR_CP_TABLE = 19,
        ERROR_COMBAT = 20,
        ERROR_SKILL = 21,
        ERROR_CANCEL_SKILL = 22,
        ERROR_LUC_BONUS = 23,
        ERROR_CHA_BONUS = 24,
    };

    static constexpr std::size_t TRANSFORM_MAX_LEVEL = 122;

    CParserForTransform();
    ~CParserForTransform();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void ReportError(ErrorType error, std::uint64_t currentSize, std::uint64_t maximumSize, const char16_t* text);

    CLexerForTransform m_lexer; // +0x0f0
    std::int32_t id; // +0x1c8
    std::int32_t count; // +0x1cc
    std::vector<double> doubleList; // +0x1d0
    std::vector<std::int32_t> intList; // +0x1f0
    std::vector<const CSkillInfo*> skillList; // +0x210
    CTransformCommonData* pCommonData; // +0x230
    CTransformCombatData* pCombatData; // +0x238
    CTransform* pTransformFemale; // +0x240
    CTransform* pTransformMale; // +0x248
    bool forNonCombat; // +0x250
    [[maybe_unused]] std::array<std::byte, 0x07> m_alignment251To258;
};

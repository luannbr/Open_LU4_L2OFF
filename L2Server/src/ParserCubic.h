#pragma once

#include "Cubic.h"
#include "LexerCubic.h"

#include <cstddef>
#include <cstdint>

class CParserForCubic {
public:
    CParserForCubic();
    ~CParserForCubic();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForCubic m_lexer; // +0x0f0

private:
    bool m_needSkillTarget; // +0x1c8
    bool m_caseOfTargetHeal; // +0x1c9
    [[maybe_unused]] std::byte m_alignment1caTo1cc[2]; // +0x1ca
    std::int32_t m_id; // +0x1cc
    std::int32_t m_level; // +0x1d0
    std::int32_t m_slot; // +0x1d4
    std::int32_t m_prevProb; // +0x1d8
    bool m_bIsCubic; // +0x1dc
    [[maybe_unused]] std::byte m_alignment1ddTo1e0[3]; // +0x1dd
    std::int32_t m_duration; // +0x1e0
    std::int32_t m_delay; // +0x1e4
    std::int32_t m_maxCount; // +0x1e8
    [[maybe_unused]] std::byte m_alignment1ecTo1f0[4]; // +0x1ec
    double m_power; // +0x1f0
    bool m_bUseUp; // +0x1f8
    [[maybe_unused]] std::byte m_alignment1f9To1fc[3]; // +0x1f9
    std::int32_t m_agathionNpcClassId; // +0x1fc
    CCubic* m_pCubic; // +0x200
    [[maybe_unused]] SpriteTargetType m_targetType; // +0x208
    [[maybe_unused]] std::int32_t m_targetTypeNone; // +0x20c
    [[maybe_unused]] std::int32_t m_targetTypeHigh; // +0x210
    [[maybe_unused]] std::int32_t m_targetTypeMid; // +0x214
    [[maybe_unused]] std::int32_t m_targetTypeLow; // +0x218
    [[maybe_unused]] std::byte m_alignment21cTo220[4]; // +0x21c
    CubicSkillInfo* m_cubicSkillInfo; // +0x220
    CubicSkillInfo m_TimeSkillInfo; // +0x228
};

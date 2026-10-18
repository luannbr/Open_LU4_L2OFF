#pragma once

#include "LexerOption.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CSkillInfo;
class Option;
enum MaterialType : std::int32_t;

class CParserForOption {
public:
    CParserForOption();
    ~CParserForOption();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForOption m_lexer; // +0x0f0
    CSkillInfo* m_pSkillInfo; // +0x1d0
    bool m_AttackTypeArray[25]; // +0x1d8
    bool m_ArmorTypeArray[5]; // +0x1f1

private:
    [[maybe_unused]] std::byte m_targetPadding1f6[2]; // +0x1f6

public:
    std::vector<MaterialType> m_vMaterial; // +0x1f8
    char16_t* name; // +0x218
    std::int32_t id; // +0x220
    std::int32_t lvl; // +0x224
    std::int32_t nid; // +0x228
    std::int32_t _cast_range; // +0x22c
    std::int32_t hp; // +0x230
    std::int32_t mp; // +0x234
    std::int32_t _target_type; // +0x238
    std::int32_t _effect_point; // +0x23c
    std::int32_t _attribute; // +0x240
    std::int32_t _abnormal_type; // +0x244
    std::int32_t _abnormal_level; // +0x248

private:
    [[maybe_unused]] std::byte m_targetPadding24c[4]; // +0x24c

public:
    double _hit_time; // +0x250
    double _cool_time; // +0x258
    double _reuse_delay; // +0x260
    double _is_magic; // +0x268
    std::set<int> optionNoSet; // +0x270
    std::set<std::u16string> optionNameSet; // +0x290
    bool m_bOnlyPrecompiledHeader; // +0x2b0

private:
    [[maybe_unused]] std::byte m_targetPadding2b1[7]; // +0x2b1

public:
    std::set<int> m_sMagicType; // +0x2b8

private:
    Option* m_pTempOption; // +0x2d8
    std::u16string tempOptionName; // +0x2e0
};

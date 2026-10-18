#pragma once

#include "Creature.h"
#include "LexerSkill.h"
#include "PledgeRaidInfo.h"
#include "Skill.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CSkillEffect_i_call_skill_by_force;
class CSkillEffect_i_random_call_skill;
class CSkillEffect_i_shape_shift_am_fixed;
class CSkillEffect_i_shape_shift_item_auto_matching;
class CSkillEffect_i_shape_shift_wp_fixed;
class CSkillEffect_i_unlock;
struct CSkillRandomUnit;
class CTerritory;
enum MaterialType : std::int32_t;

// The grammar is ParserSkill.y. Bison generates yyparse as a member, so the
// grammar actions use these fields. The offset comments give the layout in the
// original image.
class CParserForSkill {
public:
    struct AbnormalStatus {
        const char16_t* m_Name;
        AbnormalStatusSlotType m_Type;
    };

    struct SummonStatBonus {
        SummonStatBonus() = default;
        SummonStatBonus(const SummonStatBonus& other) = default;
        SummonStatBonus(SummonStatBonus&& other) = default;
        ~SummonStatBonus() = default;

        std::u16string statName; // +0x00
        std::int32_t bonusRate; // +0x28
    };

    struct i_call_my_skillNode {
        using CNode = i_call_my_skillNode;

        explicit i_call_my_skillNode(int skillId);
        i_call_my_skillNode(int skillId, i_call_my_skillNode& from);
        i_call_my_skillNode(const i_call_my_skillNode& other) = default;
        i_call_my_skillNode(i_call_my_skillNode&& other) = default;
        ~i_call_my_skillNode() = default;

        void updateFromList(i_call_my_skillNode& from);

        std::int64_t id; // +0x00
        std::vector<i_call_my_skillNode*> fromList; // +0x08
        bool inStack; // +0x28
    };

    using AbnormalMap = std::map<std::u16string, AbnormalStatusSlotType>;
    using AlterSkillList = std::vector<CSkillInfo*>;
    using AbnormalAlterSkillMap = CSkillInfo::AbnormalAlterSkillMap;
    using TreeIterType = std::map<std::int64_t, i_call_my_skillNode>::iterator;

    CParserForSkill();
    CParserForSkill(const CParserForSkill& other);
    ~CParserForSkill();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    bool CheckAbnormalStatusSlotType();
    static void PostAlterSkillLoad();
    static AbnormalStatusSlotType FindAbnormalByName(const char16_t* name, int line);
    static bool IsLower(const char16_t* name, int line);
    CCreatureSkillMod::SummonStatBonusMod GetModIndexFromModName(const char16_t* name);
    bool hasCycle(i_call_my_skillNode* node);

    static const AbnormalStatus s_AbnormalTypes[ABNS_MAX];
    static AbnormalMap s_AbnormalMap;
    static AlterSkillList m_AlterSkillVector;
    static AlterSkillList m_TriggerAlterSkillVector;

private:
    // The current 72,824-byte key function is expressed in bounded private
    // ranges. These helpers are source-recovery scaffolding and do not add
    // virtual slots. The public yyaction calls all current IDs 0..1009.

public:
    CSkillInfo* m_pPrevSkillInfo; // +0x0f0
    CSkillInfo* m_pSkillInfo; // +0x0f8
    CSkillInfo::EffectActivateTiming m_EffectActivateTiming; // +0x100
    CSkillInfo::OperateConditionTarget m_ConditionTarget; // +0x104
    bool m_AttackTypeArray[25]; // +0x108
    bool m_ArmorTypeArray[5]; // +0x121
    bool m_AdditionalArmorTypeArray[5]; // +0x126
    bool m_SlotTypeArray[63]; // +0x12b

private:
    [[maybe_unused]] std::byte m_targetPadding16a[6]; // +0x16a

public:
    std::vector<MaterialType> m_vMaterial; // +0x170
    CTerritory* m_pTerritory; // +0x190
    CRestorationUnit* pRestorationUnit; // +0x198
    CRestorationUnitList* pRestorationUnitList; // +0x1a0
    CRestorationUnitGroupList* pRestorationUnitGroupList; // +0x1a8
    CRestorationUnitVector* m_vRestorationUnit; // +0x1b0
    CRestorationUnitListVector* m_vRestorationList; // +0x1b8
    CRestorationUnitGroupListVector* m_vRestorationGroupList; // +0x1c0
    CRestorationUnitVector* m_vRestorationListForRewardItem; // +0x1c8
    std::vector<CSkillRandomUnit*>* m_vSkillRandomUnit; // +0x1d0
    CSkillEffect_i_unlock* pUnlockEffect; // +0x1d8
    CSkillEffect_i_call_skill_by_force* m_pCSkillEffect_i_call_skill_by_force; // +0x1e0
    std::vector<SummonStatBonus> m_SummonStBonusVector; // +0x1e8
    std::int32_t m_nLineNumber; // +0x208
    bool m_bOnlyPrecompiledHeader; // +0x20c
    bool m_bReload; // +0x20d
    bool m_bNewSkill; // +0x20e

private:
    [[maybe_unused]] std::byte m_targetPadding20f[1]; // +0x20f

public:
    std::int32_t m_nToggleGroupId; // +0x210

private:
    [[maybe_unused]] std::byte m_targetPadding214[4]; // +0x214

public:
    std::vector<int>* m_pAttachGroupSkill; // +0x218
    CLexerForSkill m_lexer; // +0x220
    std::vector<ItemChanceType> m_vItemChanceType; // +0x308
    std::vector<WeaponBonusType> m_vWeaponBonusTypeList; // +0x328
    std::set<int> m_sMagicType; // +0x348
    std::vector<AbnormalDataForTrigger> m_vAbnormalData; // +0x368
    std::set<AbnormalStatusSlotType> m_setAbnormalSlotType; // +0x388
    std::set<std::u16string> m_setSkillName; // +0x3a8
    std::vector<ChangeBodyStruct> m_vChangeBody; // +0x3c8
    bool m_bAllBattleField; // +0x3e8

private:
    [[maybe_unused]] std::byte m_targetPadding3e9[7]; // +0x3e9

public:
    CSkillEffect_i_shape_shift_wp_fixed* pShapeShiftWpFixedEffect; // +0x3f0
    CSkillEffect_i_shape_shift_am_fixed* pShapeShiftAmFixedEffect; // +0x3f8
    CSkillEffect_i_shape_shift_item_auto_matching* pShapeShiftItemAutoMatchingEffect; // +0x400
    ShapeShiftAutoMatchingType::type eShapeShiftItemAutoMatchingType; // +0x408

private:
    [[maybe_unused]] std::byte m_targetPadding40c[4]; // +0x40c

public:
    CSkillEffect_i_random_call_skill* pRandomCallSkillEffect; // +0x410
    std::vector<int> m_vCondition; // +0x418
    std::map<std::int64_t, i_call_my_skillNode> m_CallTree; // +0x438
};

// The dependency-closed prefix was published before the complete parser
// declaration. Keep these wrappers for existing callers while the exact
// class static surface is created alongside the full semantic switch.
namespace skill_parser_detail {

bool CheckAbnormalStatusSlotType();
void InitializeAbnormalStatusMap();
AbnormalStatusSlotType FindAbnormalByName(const char16_t* name, int line);

} // namespace skill_parser_detail

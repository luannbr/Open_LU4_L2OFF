#pragma once

#include "TransformConst.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CSkillInfo;
class CTransform;
class IPacketHelper;
class User;
enum AttackType : std::int32_t;

enum TransformLevelStat : std::int32_t {
    TR_HP_REGEN = 0,
    TR_MP_REGEN = 1,
    TR_CP_REGEN = 2,
    TR_LEVEL_BONUS = 3,
    TR_STR_BONUS = 4,
    TR_INT_BONUS = 5,
    TR_CON_BONUS = 6,
    TR_DEX_BONUS = 7,
    TR_WIT_BONUS = 8,
    TR_MEN_BONUS = 9,
    TR_HP_TABLE = 10,
    TR_MP_TABLE = 11,
    TR_CP_TABLE = 12,
    TR_LUC_BONUS = 13,
    TR_CHA_BONUS = 14,
    TR_MAX = 15,
};


class CTransformCombatData {
public:
    void Init();
    void SetStat(std::vector<int>& stats);
    void SetStatByLevel(TransformLevelStat stat, std::vector<double>& values);
    void SetPDefend(std::vector<double>& values);
    void SetMDefend(std::vector<double>& values);

    CStats m_Stat;
    double basePDefend[14];
    double baseMDefend[14];
    double orgHPRegen[122];
    double orgMPRegen[122];
    double orgCPRegen[122];
    double levelBonus[122];
    double strBonus[201];
    double intBonus[201];
    double conBonus[201];
    double dexBonus[201];
    double witBonus[201];
    double menBonus[201];
    double hpTable[122];
    double mpTable[122];
    double cpTable[122];
    double lucBonus[201];
    double chaBonus[201];
};


class CTransformCommonData {
public:
    CTransformCommonData();

    void Init();
    void SetSpeed(std::vector<double>& speed);
    void SetSkill(std::vector<const CSkillInfo*>& skills);
    void SetAdditionalSkill(int level, std::vector<const CSkillInfo*>& skills);
    void SetAction(std::vector<int>& actions);
    void SetItemCheck(bool allow, const std::vector<int>& items);
    bool CanUseItem(int itemId) const;
    void GetPacketForUseItem(IPacketHelper& packet) const;

    double collisionHeight;
    double collisionRadius;
    double orgSpeed[2][4];
    std::vector<const CSkillInfo*> skillList;
    std::map<int, std::vector<const CSkillInfo*>*> additionalSkillMap;
    std::vector<int> actionList;
    AttackType baseAttackType;
    struct {
        int attackRange;
        int randomDamage;
        int attackSpeed;
        int criticalProb;
        int pAttack;
        int mAttack;
    } bareHandsData;
    bool m_bItemAllow;
    std::array<std::byte, 0x03> m_alignment00cdTo00d0;
    std::set<int> m_ItemCheck;
};


class CTransformDB {
public:
    CTransformDB();

    bool Load();
    bool Load(char16_t* fileBuffer, int fileSize);
    void Add(int transformId, SexType sex, CTransform* transform);
    CTransform* Find(int transformId, SexType sex);

    static bool HaveSameSkillBySkillID(const std::vector<const CSkillInfo*>* skills, const CSkillInfo* skill);
    static void UpdateSkillIfLevelHigher(std::vector<const CSkillInfo*>& skills, const CSkillInfo* skill);

private:
    void PostLoad();

    std::map<std::pair<int, SexType>, CTransform*> m_transformMap; // +0x00

public:
    CTransform* m_pLastInsertedMaleTransform; // +0x20
    CTransform* m_pLastInsertedFemaleTransform; // +0x28
};


extern CTransformDB g_TransformDB;

// Exact current-L2Server base envelope. The inline virtuals below are the
// exact shared no-op/false/zero implementations installed in the current
// base vtable.
class CTransform {
public:

    explicit CTransform(int transformId);

    virtual void SetCombatData(CTransformCombatData*) {}
    virtual bool IsCombat() const { return false; }
    virtual bool IsNonCombat() const { return false; }
    virtual bool IsCursed() const { return false; }
    virtual bool IsModeChange() const { return false; }
    virtual bool IsRiding() const { return false; }
    virtual bool IsFlying() const { return false; }
    virtual bool IsPureState() const { return false; }
    virtual double OrgHP(int) { return 0.0; }
    virtual double OrgMP(int) { return 0.0; }
    virtual double OrgCP(int) { return 0.0; }
    virtual double OrgHPRegen(int) { return 0.0; }
    virtual double OrgMPRegen(int) { return 0.0; }
    virtual double OrgCPRegen(int) { return 0.0; }
    virtual double LevelBonus(int) { return 0.0; }
    virtual double StrBonus(int) { return 0.0; }
    virtual double IntBonus(int) { return 0.0; }
    virtual double ConBonus(int) { return 0.0; }
    virtual double DexBonus(int) { return 0.0; }
    virtual double WitBonus(int) { return 0.0; }
    virtual double MenBonus(int) { return 0.0; }
    virtual double LucBonus(int) { return 0.0; }
    virtual double ChaBonus(int) { return 0.0; }
    virtual bool SetCollision(User* user, int radius, int height, bool restore);
    virtual void OnCancelTransform(User*) {}

    bool CanTransform(User* user);
    bool SetTransformData(User* user);
    void SendActionList(User* user);
    static void ConvertTransformTypes(const std::vector<wchar_t*>& transformTypes, std::vector<int>& convertedTypes);
    static int GetTransformType(const wchar_t* transformType);
    void GetPacketForUseItem(IPacketHelper& packet) const;
    bool IsValidAction(int action);
    void PostLoad();
    bool CanUseItem(int itemId) const;
    const std::vector<const CSkillInfo*>* AdditionalSkillList(int level);
    void GetSkillList(int level, std::vector<const CSkillInfo*>& skills);
    int GetSkillLevel(int level, int skillId);

protected:
    virtual bool _SetCommonData(User* user);
    virtual void _SetCombatData(User*) {}

public:
    std::int32_t m_TransformId; // +0x0008
    bool m_swimable; // +0x000c
    std::array<std::byte, 0x03> m_alignment000dTo0010; // +0x000d..+0x000f
    CTransformCommonData* m_commonData; // +0x0010
    std::array<char, 0x4f9c> m_buf; // +0x0018
    std::int32_t m_bufLen; // +0x4fb4
    std::int32_t m_actionCount; // +0x4fb8
    TransformType::Enum m_eTransformType; // +0x4fbc
    std::int32_t m_SpawnHeight; // +0x4fc0
    bool m_bNormalAttakable; // +0x4fc4
    bool m_bCanMove; // +0x4fc5
    std::array<std::byte, 0x02> m_alignment4fc6To4fc8; // +0x4fc6..+0x4fc7
};


class CTransformForNonCombat : public CTransform {
public:
    explicit CTransformForNonCombat(int transformId) : CTransform(transformId) {
        m_eTransformType = TransformType::NonCombat;
    }

    bool IsNonCombat() const override { return true; }
};


class CTransformForRiding : public CTransformForNonCombat {
public:
    explicit CTransformForRiding(int transformId) : CTransformForNonCombat(transformId) {
        m_eTransformType = TransformType::Riding;
    }

    bool IsRiding() const override { return true; }
};


class CTransformForModeChange : public CTransform {
public:
    explicit CTransformForModeChange(int transformId) : CTransform(transformId) {
        m_eTransformType = TransformType::ModeChange;
    }

    bool IsModeChange() const override { return true; }
    bool SetCollision(User*, int, int, bool) override { return true; }

protected:
    bool _SetCommonData(User*) override { return true; }
};


class CTransformForCombat : public CTransform {
public:
    explicit CTransformForCombat(int transformId) : CTransform(transformId), m_combatData(nullptr) {
        m_eTransformType = TransformType::Combat;
    }

    void SetCombatData(CTransformCombatData* combatData) override { m_combatData = combatData; }
    bool IsCombat() const override { return true; }
    double OrgHP(int level) override { return m_combatData->hpTable[level < 1 ? 0 : level - 1]; }
    double OrgMP(int level) override { return m_combatData->mpTable[level < 1 ? 0 : level - 1]; }
    double OrgCP(int level) override { return m_combatData->cpTable[level < 1 ? 0 : level - 1]; }
    double OrgHPRegen(int level) override { return m_combatData->orgHPRegen[level < 1 ? 0 : level - 1]; }
    double OrgMPRegen(int level) override { return m_combatData->orgMPRegen[level < 1 ? 0 : level - 1]; }
    double OrgCPRegen(int level) override { return m_combatData->orgCPRegen[level < 1 ? 0 : level - 1]; }
    double LevelBonus(int level) override { return m_combatData->levelBonus[level < 1 ? 0 : level - 1]; }
    double StrBonus(int level) override { return m_combatData->strBonus[level < 1 ? 0 : level - 1]; }
    double IntBonus(int level) override { return m_combatData->intBonus[level < 1 ? 0 : level - 1]; }
    double ConBonus(int level) override { return m_combatData->conBonus[level < 1 ? 0 : level - 1]; }
    double DexBonus(int level) override { return m_combatData->dexBonus[level < 1 ? 0 : level - 1]; }
    double WitBonus(int level) override { return m_combatData->witBonus[level < 1 ? 0 : level - 1]; }
    double MenBonus(int level) override { return m_combatData->menBonus[level < 1 ? 0 : level - 1]; }
    // The current vtable folds LucBonus with LevelBonus and reads the same
    // levelBonus table. Preserve that shipped behavior.
    double LucBonus(int level) override { return m_combatData->levelBonus[level < 1 ? 0 : level - 1]; }
    double ChaBonus(int level) override { return m_combatData->chaBonus[level < 1 ? 0 : level - 1]; }

protected:
    void _SetCombatData(User* user) override;

public:
    CTransformCombatData* m_combatData;
};


class CTransformForCursed : public CTransformForCombat {
public:
    explicit CTransformForCursed(int transformId) : CTransformForCombat(transformId) {
        m_eTransformType = TransformType::Cursed;
    }

    bool IsCursed() const override { return true; }
};


class CTransformForFlying : public CTransformForCombat {
public:
    explicit CTransformForFlying(int transformId) : CTransformForCombat(transformId) {
        m_eTransformType = TransformType::Flying;
    }

    bool IsFlying() const override { return true; }
    void OnCancelTransform(User* user) override;

protected:
    bool _SetCommonData(User* user) override;
};


class CTransformForPureStat : public CTransformForCombat {
public:
    explicit CTransformForPureStat(int transformId) : CTransformForCombat(transformId) {
        m_eTransformType = TransformType::PureStat;
    }

    bool IsPureState() const override { return true; }
};

#pragma once

#include "Creature.h"
#include "PledgeRaidInfo.h"
#include "Shared/src/StandardContainers.h"
#include "Skill.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

class CCreature;
class CItem;
class CNPC;
class CParty;
class CParserForSkill;
class CParserForOption;
class CSkillAction2;
class CSkillInfo;
class CStaticObject;
class CTerritory;
class CWorldObject;
class User;
enum CrystalType : std::int32_t;
enum ItemType : std::int32_t;
enum RestartPoint : std::int32_t;
enum SkillOperationTargetType : std::int32_t;

enum SkillEnlargeStorageType : std::int32_t {
    SEST_INVENTORY_NORMAL = 0,
    SEST_STORAGE_PRIVATE = 1,
    SEST_STORAGE_CLAN = 2,
    SEST_TRADE_SELL = 3,
    SEST_TRADE_BUY = 4,
    SEST_RECIPE_DWARVEN = 5,
    SEST_RECIPE_COMMON = 6,
    SEST_INVENTORY_WAIST = 7,
    SEST_MAX = 8,
};


namespace VitalityRestore {
enum Type : std::int32_t;
}

class CSkillOperateCondition {
public:
    virtual ~CSkillOperateCondition();
    virtual bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CObject* target);
    virtual bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target);
};


class CSkillOperateCondition_account_type : public CSkillOperateCondition {
public:
    enum UserAccountType : int {
        UAT_PCCAFE = 0,
        UAT_ALL = 1,
    };

    explicit CSkillOperateCondition_account_type(UserAccountType accountType) : m_UserAccountType(accountType) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    UserAccountType m_UserAccountType;
};

class CSkillOperateCondition_check_level : public CSkillOperateCondition {
public:
    CSkillOperateCondition_check_level(int lowLevel, int highLevel, SkillTargetTypeEnum targetType) :
        m_nLowLevel(lowLevel), m_nHighLevel(highLevel), m_TargetType(targetType) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nLowLevel;
    int m_nHighLevel;
    SkillTargetTypeEnum m_TargetType;
};

class CSkillOperateCondition_check_sex : public CSkillOperateCondition {
public:
    CSkillOperateCondition_check_sex() = default;

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    SexType m_nSex;
};

class CSkillOperateCondition_energy_saved : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_energy_saved(int energyCount) : m_nEnergyCount(energyCount) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nEnergyCount;
};

class CSkillOperateCondition_soul_saved : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_soul_saved(int soulCount) : m_nSoulCount(soulCount) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nSoulCount;
};

class CSkillOperateCondition_mainjob : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_subjob : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_not_in_underwater : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_2h_weapon : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_2h_weapon() = default;

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    bool bWeaponTypeArray[AT_MAX];
};

class CSkillOperateCondition_op_target_weapon_attack_type : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_target_weapon_attack_type();

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    bool bWeaponTypeArray[AT_MAX];
    bool bWeaponSlotTypeArray[ST_MAX];
    int nMagicWeaponCheckType;
};

class CSkillOperateCondition_op_target_armor_type : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_target_armor_type();

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    bool bArmorTypeArray[AMT_MAX];
    bool bArmorSlotTypeArray[ST_MAX];
};

class CSkillOperateCondition_op_target_all_item_type : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_target_all_item_type();

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    bool bWeaponTypeArray[AT_MAX];
    bool bArmorTypeArray[AMT_MAX];
    bool bSlotTypeArray[ST_MAX];
};

class CSkillOperateCondition_op_alignment : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_alignment(int isChao, SkillTargetTypeEnum targetType) :
        m_nIsChao(isChao), m_TargetType(targetType) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nIsChao;
    SkillTargetTypeEnum m_TargetType;
};

class CSkillOperateCondition_op_check_flag : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_check_nobless : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_pkcount : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_pkcount(SkillTargetTypeEnum targetType) : m_TargetType(targetType) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    SkillTargetTypeEnum m_TargetType;
};

class CSkillOperateCondition_race : public CSkillOperateCondition {
public:
    CSkillOperateCondition_race() = default;

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    RaceType m_RaceType;
};

class CSkillOperateCondition_equip_shield : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_equip_weapon : public CSkillOperateCondition {
public:
    CSkillOperateCondition_equip_weapon();

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    bool bWeaponTypeArray[AT_MAX];
};


class CSkillOperateCondition_equip_armor : public CSkillOperateCondition {
public:
    CSkillOperateCondition_equip_armor();

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    bool bArmorTypeArray[AMT_MAX];
};


class CSkillOperateCondition_remain_hp_per : public CSkillOperateCondition {
public:
    CSkillOperateCondition_remain_hp_per(CSkillInfo::OperateConditionTarget targetType, int remainHpPer, bool lower) :
        m_RemainHpPer(remainHpPer), m_Lower(lower), m_TargetType(targetType) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_RemainHpPer;
    bool m_Lower;
    CSkillInfo::OperateConditionTarget m_TargetType;
};

class CSkillOperateCondition_remain_mp_per : public CSkillOperateCondition {
public:
    CSkillOperateCondition_remain_mp_per(int remainMpPer, bool lower) : m_RemainMpPer(remainMpPer), m_Lower(lower) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_RemainMpPer;
    bool m_Lower;
};

class CSkillOperateCondition_remain_cp_per : public CSkillOperateCondition {
public:
    CSkillOperateCondition_remain_cp_per(int remainCpPer, bool lower) : m_RemainCpPer(remainCpPer), m_Lower(lower) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_RemainCpPer;
    bool m_Lower;
};


class CSkillOperateCondition_op_can_not_use_airship : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_cannot_use_target_with_private_store : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_check_honors : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_check_not_registered_pledgegame : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_companion : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    int type;
};

class CSkillOperateCondition_op_energy_max : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_energy_max(int maximumEnergy) : m_nMaxEnergy(maximumEnergy) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nMaxEnergy;
};

class CSkillOperateCondition_op_have_summon : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_have_summoned_npc : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_have_summoned_npc(std::u16string npcName) : m_szNPCName(std::move(npcName)) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    std::u16string m_szNPCName;
};

class CSkillOperateCondition_op_have_not_summoned_npc : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_have_not_summoned_npc(std::u16string npcName) : m_szNPCName(std::move(npcName)) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    std::u16string m_szNPCName;
};

class CSkillOperateCondition_op_need_peacezone : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_not_olympiad : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_check_cast_range : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_check_cast_range(double castRange) : m_CastRange(castRange) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    double m_CastRange;
};

class CSkillOperateCondition_target_my_party : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_target_my_party(bool includeMe) : m_IncludeMe(includeMe) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    bool m_IncludeMe;
};

class CSkillOperateCondition_target_my_pledge : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_target_my_pledge(bool includeMe) : m_IncludeMe(includeMe) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    bool m_IncludeMe;
};

class CSkillOperateCondition_target_my_mentor : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_target_my_mentee : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_target_my_pledge_academy : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_in_siege_time : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_in_siege_time(int residenceId) : m_nResidenceId(residenceId) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nResidenceId;
};

class CSkillOperateCondition_op_soul_max : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_wyvern : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_not_instantzone : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_target_npc : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    std::vector<int> m_vTargetNPC;
};

class CSkillOperateCondition_op_social_class : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_social_class(int socialClass) : m_nSocialClass(socialClass) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nSocialClass;
};

class CSkillOperateCondition_op_check_relative_height : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_check_relative_height(int height) : m_nHeight(height) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nHeight;
};

class CSkillOperateCondition_op_not_cursed : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_possess_holything : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_can_untransform : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_can_use_in_transform : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_can_use_in_transform(int transformId = 0) : m_transformID(transformId) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_transformID;
};

class CSkillOperateCondition_cannot_use_in_transform : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_cannot_use_in_transform(int transformId = 0) : m_transformID(transformId) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_transformID;
};

class CSkillOperateCondition_op_fishing_cast : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_fishing_pumping : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_fishing_reeling : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_restart_point : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_restart_point(int restartPoint = 0) : m_nRestartPoint(restartPoint) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nRestartPoint;
};

class CSkillOperateCondition_agathion_energy : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_agathion_energy(int energy = 0) : m_nEnergy(energy) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nEnergy;
};

class CSkillOperateCondition_can_stop_consume_vital_point : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_can_use_in_dragon_lair : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_pledge : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_pledge(int pledgeLevel = 0) : m_nPledgeLevel(pledgeLevel) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nPledgeLevel;
};

class CSkillOperateCondition_op_check_pcbang_point : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_check_pcbang_point(int amount = 0) : nAmount(amount) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    const int nAmount;
};

class CSkillOperateCondition_op_check_crt_effect : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_check_skill : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_check_skill(int skillId, SkillTargetTypeEnum targetType) :
        m_SkillID(skillId), m_TargetType(targetType) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_SkillID;
    SkillTargetTypeEnum m_TargetType;
};

class CSkillOperateCondition_op_call_pc : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_call_pc(bool includeDead, CSkillInfo* skillInfo);

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    bool m_bIncludeDead;
};

class CSkillOperateCondition_op_check_class_list : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_check_class_list(
        const std::vector<std::int64_t>* classIds, SkillTargetTypeEnum targetType, bool exist
    );

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    std::vector<ClassType> m_ClassIds;
    SkillTargetTypeEnum m_TargetType;
    bool m_bExist;
};

class CSkillOperateCondition_op_check_residence : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_check_residence(const std::vector<std::int64_t>* residenceIds, bool pledgeMaster);

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    bool CheckResidence(std::uint32_t residenceId) const;

    std::vector<std::uint32_t> m_ResidenceIds;
    bool m_bPledgeMaster;
};

class CSkillOperateCondition_op_check_on_going_event_campaign : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_check_on_going_event_campaign(const std::vector<std::int64_t>* campaignIds);

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    std::vector<int> m_EventCampaignIds;
};

class CSkillOperateCondition_op_exist_npc : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_exist_npc(int distance, bool exist) : m_nDist(distance), m_bExist(exist) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    int m_nDist;
    bool m_bExist;
    std::set<int> m_vTargetNPC;
};

class CSkillOperateCondition_op_need_summon : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_need_summon() = default;

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    std::set<int> m_SummonClassIds;
};

class CSkillOperateCondition_op_need_summon_or_pet : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_need_summon_or_pet() = default;

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    std::set<int> m_SummonOrPetClassIds;
};

class CSkillOperateCondition_op_need_agathion : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_need_agathion(int agathionId = 0) : m_nAgathionId(agathionId) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nAgathionId;
};

class CSkillOperateCondition_can_add_max_entrance_inzone : public CSkillOperateCondition {
public:
    CSkillOperateCondition_can_add_max_entrance_inzone(int groupId, int delta) : m_groupId(groupId), m_delta(delta) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_groupId;
    int m_delta;
};

class CSkillOperateCondition_can_bookmark_add_slot : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_can_bookmark_add_slot(int slotNum) : m_slotNum(slotNum) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_slotNum;
};

class CSkillOperateCondition_can_restore_vital_point : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_can_use_vitality_consume_item : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_can_change_vital_item_count : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_check_abnormal : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_check_abnormal(AbnormalStatusSlotType abnormalType, int level, int enable) :
        m_AbnormalType(abnormalType), m_Level(level), m_Enable(enable) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    AbnormalStatusSlotType m_AbnormalType;
    int m_Level;
    int m_Enable;
};

class CSkillOperateCondition_op_encumbered : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_encumbered(int freeWeight, int freeSlot) : m_nFreeWeight(freeWeight), m_nFreeSlot(freeSlot) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nFreeWeight;
    int m_nFreeSlot;
};

class CSkillOperateCondition_op_use_firecracker : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_use_firecracker(int height) : m_nHeight(height) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nHeight;
};

class CSkillOperateCondition_can_enchant_attribute : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_use_praseed : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_use_praseed(int energy) : m_nEnergy(energy) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nEnergy;
};

class CSkillOperateCondition_op_check_equip_slot : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_check_equip_slot(int sysMsgNo) : m_nSysMsgNo(sysMsgNo), m_bEquipSlotTypeArray{} {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    int m_nSysMsgNo;
    bool m_bEquipSlotTypeArray[ST_MAX];
};

class CSkillOperateCondition_op_instantzone : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_instantzone();
    explicit CSkillOperateCondition_op_instantzone(std::vector<std::int64_t>* inZoneIds);

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    std::vector<int> m_InZoneIds;
};

class CSkillOperateCondition_op_territory : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_territory(CTerritory* territory, SkillOperationTargetType targetType) :
        m_pTerritory(territory), m_TargetType(targetType) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    CTerritory* m_pTerritory;
    SkillOperationTargetType m_TargetType;
};

class CSkillOperateCondition_op_not_territory : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_not_territory(CTerritory* territory, SkillOperationTargetType targetType) :
        m_pTerritory(territory), m_TargetType(targetType) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    CTerritory* m_pTerritory;
    SkillOperationTargetType m_TargetType;
};

class CSkillOperateCondition_op_can_escape : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_can_disarm : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_check_stuck_npc : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_siege_hammer : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_equip_item : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_equip_item(CSkillInfo::OperateConditionTarget targetType, int itemClassId) :
        m_ItemClassId(itemClassId), m_TargetType(targetType) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_ItemClassId;
    CSkillInfo::OperateConditionTarget m_TargetType;
};

class CSkillOperateCondition_can_refuel_airship : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_can_refuel_airship(int amount) : m_Amount(amount) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_Amount;
};

class CSkillOperateCondition_op_home : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_home(RestartPoint homePoint) : m_HomePoint(homePoint) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    RestartPoint m_HomePoint;
};

class CSkillOperateCondition_op_can_go : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_op_can_go(double distance) : m_Dist(distance) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    [[maybe_unused]] double m_Dist;
};

class CSkillOperateCondition_op_change_weapon : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_consume_body : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    static bool CanConsumeBody(CCreature* creature, CNPC* npc);
    static bool SetAsCorpse(CNPC* npc);
};

class CSkillOperateCondition_op_unlock : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_enchant : public CSkillOperateCondition {
public:
    CSkillOperateCondition_enchant() : m_CrystalType(static_cast<CrystalType>(-1)), m_nItemEnchantTypeOp(0) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    CrystalType m_CrystalType;
    int m_nItemEnchantTypeOp;
    std::vector<ItemType> m_vItemType;
};

class CSkillOperateCondition_enchant_divide : public CSkillOperateCondition {
public:
    CSkillOperateCondition_enchant_divide() :
        m_CrystalType(static_cast<CrystalType>(-1)), m_nItemEnchantTypeOp(0) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

    CrystalType m_CrystalType;
    int m_nItemEnchantTypeOp;
    std::vector<ItemType> m_vItemType;
};

class CSkillOperateCondition_enchant_range : public CSkillOperateCondition {
public:
    CSkillOperateCondition_enchant_range(int minimumEnchant, int maximumEnchant, int type) :
        m_nMinEnchant(minimumEnchant), m_nMaxEnchant(maximumEnchant), m_nType(type) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
    bool CheckEnchantRange(int enchant);

private:
    int m_nMinEnchant;
    int m_nMaxEnchant;
    int m_nType;
};

class CSkillOperateCondition_op_check_class : public CSkillOperateCondition {
public:
    CSkillOperateCondition_op_check_class(ClassType classId, SkillTargetTypeEnum targetType, bool exist) :
        m_ClassID(classId), m_TargetType(targetType), m_bExist(exist) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    ClassType m_ClassID;
    SkillTargetTypeEnum m_TargetType;
    bool m_bExist;
};

class CSkillOperateCondition_can_summon_cubic : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_build_camp : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_build_advance_base : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_can_summon : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_can_summon_multi : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_can_summon_multi(const char16_t* npcName) : m_sNPCName(npcName) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    std::u16string m_sNPCName;
};

class CSkillOperateCondition_can_summon_pet : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_can_summon_siege_golem : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_can_use_swoop_cannon : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_can_mount_for_event : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_can_mount_for_event(const char16_t* npcName) : m_npcName(npcName) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    std::u16string m_npcName;
};

class CSkillOperateCondition_can_transform : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_can_transform(int transformId) : m_transformID(transformId) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_transformID;
};

class CSkillOperateCondition_can_transform_in_dominion : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_can_transform_in_dominion(int dominionId) : m_nDominionID(dominionId) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_nDominionID;
};

class CSkillOperateCondition_can_use_in_battlefield : public CSkillOperateCondition {
public:
    explicit CSkillOperateCondition_can_use_in_battlefield(bool anyBattlefield) : m_bAnyBattlefield(anyBattlefield) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    bool m_bAnyBattlefield;
};

class CSkillOperateCondition_op_harvest : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_collecting : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_sweeper : public CSkillOperateCondition {
public:
    static bool CanSweep(User* user, CNPC* npc);

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_equip_agathion_main_slot : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};

class CSkillOperateCondition_op_resurrection : public CSkillOperateCondition {
public:
    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};


class CSkillEffectByWeaponType : public CSkillEffect {
public:
    CSkillEffectByWeaponType();

    void SetAttackTypeArray(bool values[AT_MAX]);
    void SetAttackType(AttackType type, bool value) { m_bWeaponTypeArray[static_cast<int>(type)] = value; }
    void SetAllAttackType(bool value);
    bool CheckPumpCondition(CCreature* creature) override;

protected:
    bool m_bWeaponTypeArray[AT_MAX];
    std::uint8_t m_alignment0049To0050[7];
};

class CSkillEffectAddBonusByWeaponType : public CSkillEffectByWeaponType {
public:
    CSkillEffectAddBonusByWeaponType(double bonus, SkillFxChangeType changeType, CCreatureSkillMod::ModName modName);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


protected:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
    CCreatureSkillMod::ModName m_ModName;
};

class CSkillEffectByArmorType : public CSkillEffect {
public:
    CSkillEffectByArmorType();

    void SetArmorTypeArray(bool values[AMT_MAX]);
    void SetArmorType(ArmorType type, bool value) { m_bArmorTypeArray[static_cast<int>(type)] = value; }
    void SetAllArmorType(bool value);
    bool CheckPumpCondition(CCreature* creature) override;

protected:
    bool m_bArmorTypeArray[AMT_MAX];
};

class CSkillEffectAddBonusByArmorType : public CSkillEffectByArmorType {
public:
    CSkillEffectAddBonusByArmorType(double bonus, SkillFxChangeType changeType, CCreatureSkillMod::ModName modName);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


protected:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
    CCreatureSkillMod::ModName m_ModName;
};

class CSkillEffectByAdditionalArmorType : public CSkillEffect {
public:
    CSkillEffectByAdditionalArmorType();

    void SetAdditionalArmorTypeArray(bool values[AAMT_MAX]);
    void SetAdditionalArmorType(AdditionalArmorType type, bool value) {
        m_bAdditionalArmorTypeArray[static_cast<int>(type)] = value;
    }
    void SetAllAdditionalArmorType(bool value);
    bool CheckPumpCondition(CCreature* creature) override;

protected:
    bool m_bAdditionalArmorTypeArray[AAMT_MAX];
};

class CSkillEffectAddBonusByAdditionalArmorType : public CSkillEffectByAdditionalArmorType {
public:
    CSkillEffectAddBonusByAdditionalArmorType(
        double bonus, SkillFxChangeType changeType, CCreatureSkillMod::ModName modName
    );

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


protected:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
    CCreatureSkillMod::ModName m_ModName;
};

class CSkillEffectByShield : public CSkillEffect {
public:
    CSkillEffectByShield() = default;

    bool CheckPumpCondition(CCreature* creature) override;
};

class CSkillEffectAddBonusByShield : public CSkillEffectByShield {
public:
    CSkillEffectAddBonusByShield(double bonus, SkillFxChangeType changeType, CCreatureSkillMod::ModName modName);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


protected:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
    CCreatureSkillMod::ModName m_ModName;
};


class CSkillEffect_p_trigger_skill_by_dmg : public CSkillEffectByArmorType {
public:
    CSkillEffect_p_trigger_skill_by_dmg(
        EnemyType enemyType,
        int attackerLevelMin,
        int attackerLevelMax,
        int attackedDamage,
        int probability,
        SkillFxChangeType changeType,
        const char16_t* skillName,
        SkillTargetTypeEnum targetType
    );

    void OnPostLoad() override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    EnemyType m_eEnemyType;
    int m_nAttackerLevelMin;
    int m_nAttackerLevelMax;
    int m_nAttackedDamage;
    int m_nProbability;
    SkillFxChangeType m_Sfct;
    char16_t m_SkillName[256];
    SkillTargetTypeEnum m_eTargetType;
    CSkillInfo* m_pSkillInfo;
};


class CSkillEffect_p_trigger_skill_by_attack : public CSkillEffectByWeaponType {
public:
    CSkillEffect_p_trigger_skill_by_attack(
        EnemyType enemyType,
        int targetLevelMin,
        int targetLevelMax,
        CriticalType::Enum isCritical,
        int minimumDamage,
        int probability,
        SkillFxChangeType changeType,
        const char16_t* skillName,
        SkillTargetTypeEnum targetType
    );

    void OnPostLoad() override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    EnemyType m_eEnemyType;
    int m_nTargetLevelMin;
    int m_nTargetLevelMax;
    CriticalType::Enum m_IsCritical;
    int m_MinimumDamage;
    int m_Prob;
    SkillFxChangeType m_Sfct;
    char16_t m_SkillName[256];
    SkillTargetTypeEnum m_eTargetType;
    CSkillInfo* m_pSkillInfo;
};


class CSkillEffectTrigger : public CSkillEffect {
public:
    CSkillEffectTrigger(int probability, const char16_t* calledSkillName, SkillTargetTypeEnum targetType);
    ~CSkillEffectTrigger() override;

    void OnPostLoad() override;


protected:
    int m_Prob;
    std::u16string m_CalledSkillName;
    SkillTargetTypeEnum m_eTargetType;
    CSkillInfo* m_pSkillInfo;
};


class CSkillEffect_p_trigger_skill_by_kill : public CSkillEffect {
public:
    CSkillEffect_p_trigger_skill_by_kill(EnemyType enemyType, int probability, const char16_t* skillName);

    void OnPostLoad() override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    EnemyType m_eEnemyType;
    int m_Prob;
    char16_t m_SkillName[256];
    CSkillInfo* m_pSkillInfo;
};


class CSkillEffect_p_trigger_skill_by_avoid : public CSkillEffectTrigger {
public:
    CSkillEffect_p_trigger_skill_by_avoid(int probability, const char16_t* calledSkillName, SkillTargetTypeEnum targetType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};


class CSkillEffect_p_trigger_skill_by_block : public CSkillEffectTrigger {
public:
    CSkillEffect_p_trigger_skill_by_block(
        EnemyType enemyType,
        int attackerLevelMin,
        int attackerLevelMax,
        int shieldDefence,
        int probability,
        const char16_t* calledSkillName,
        SkillTargetTypeEnum targetType
    );

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    EnemyType m_eEnemyType;
    int m_nAttackerLevelMin;
    int m_nAttackerLevelMax;
    int m_nShieldDefence;
};


class CSkillEffect_p_trigger_skill_by_death : public CSkillEffectTrigger {
public:
    CSkillEffect_p_trigger_skill_by_death(
        EnemyType enemyType,
        int attackerLevelMin,
        int attackerLevelMax,
        int probability,
        const char16_t* calledSkillName,
        SkillTargetTypeEnum targetType
    );

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    EnemyType m_eEnemyType;
    int m_nAttackerLevelMin;
    int m_nAttackerLevelMax;
};


class CSkillEffect_p_trigger_skill_by_skill : public CSkillEffectTrigger {
public:
    CSkillEffect_p_trigger_skill_by_skill(
        std::vector<std::int64_t>* skillIds, int probability, const char16_t* calledSkillName, SkillTargetTypeEnum targetType
    );
    CSkillEffect_p_trigger_skill_by_skill(
        std::vector<std::int64_t>* skillIds,
        int probability,
        const char16_t* calledSkillName,
        SkillTargetTypeEnum targetType,
        EnemyType enemyType,
        int targetLevelMin,
        int targetLevelMax
    );

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    std::vector<int> m_skillIDList;
    bool m_bCheckCritical;
    EnemyType m_EnemyType;
    int m_TargetMinLevel;
    int m_TargetMaxLevel;
};


class CSkillEffect_p_intercept_skill : public CSkillEffect {
public:
    CSkillEffect_p_intercept_skill(
        int beforeSkillId, int beforeSkillLevel, int afterSkillId, int afterSkillLevel, double probability
    );

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_nBeforeSkillId;
    int m_nBeforeSkillLevel;
    int m_nAfterSkillId;
    int m_nAfterSkillLevel;
    double m_dProbability;
};


class CSkillEffect_p_replace_skill : public CSkillEffect {
public:
    CSkillEffect_p_replace_skill(int beforeSkillId, int afterSkillId, double probability);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_nBeforeSkillId;
    int m_nAfterSkillId;
    double m_dProbability;
};


class CSkillEffect_p_trigger_skill_by_magic_type : public CSkillEffectTrigger {
public:
    CSkillEffect_p_trigger_skill_by_magic_type(
        std::set<int> magicTypes,
        BadSKillType::Enum goodSkill,
        int probability,
        const char16_t* calledSkillName,
        SkillTargetTypeEnum targetType
    );

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    std::set<int> m_sMagicType;
    BadSKillType::Enum m_nGoodSkill;
};


class CSkillEffect_p_trigger_skill_by_abnormal_slot : public CSkillEffectTrigger {
public:
    CSkillEffect_p_trigger_skill_by_abnormal_slot(
        EnemyType enemyType,
        int targetLevelMin,
        int targetLevelMax,
        int probability,
        const char16_t* calledSkillName,
        SkillTargetTypeEnum targetType,
        std::vector<AbnormalDataForTrigger> abnormalData
    );

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    EnemyType m_eEnemyType;
    int m_nTargetLevelMin;
    int m_nTargetLevelMax;
    std::vector<AbnormalDataForTrigger> m_vAbnormalData;
};


class CSkillEffect_p_trigger_skill_by_attacked_skill : public CSkillEffectTrigger {
public:
    CSkillEffect_p_trigger_skill_by_attacked_skill(
        int probability, std::set<std::u16string> skillNames, const char16_t* calledSkillName, SkillTargetTypeEnum targetType
    );

    void OnPostLoad() override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    std::set<int> m_setSkillId;
    std::set<std::u16string> m_setSkillName;
};


class CSkillEffect_i_hp_self : public CSkillEffect {
public:
    explicit CSkillEffect_i_hp_self(double amount) : m_dAmount(amount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dAmount;
};

class CSkillEffect_i_hp_down_self : public CSkillEffect {
public:
    explicit CSkillEffect_i_hp_down_self(int amount) : m_Amount(-amount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_Amount;
};

class CSkillEffect_i_hp_down : public CSkillEffect {
public:
    explicit CSkillEffect_i_hp_down(int amount) : m_Amount(-amount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_Amount;
};

class CSkillEffect_i_hp_by_level_self : public CSkillEffect {
public:
    explicit CSkillEffect_i_hp_by_level_self(int amount) : m_Amount(amount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_Amount;
};

class CSkillEffect_i_m_attack : public CSkillEffect {
public:
    explicit CSkillEffect_i_m_attack(double power) : m_dPower(power) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
};

class CSkillEffect_i_m_attack_debuff_dmg : public CSkillEffect {
public:
    CSkillEffect_i_m_attack_debuff_dmg(double basePower, double powerBonus) :
        m_dBasePower(basePower), m_dPowerBonus(powerBonus) {
        // Current L2Server 0x00ca2d98.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dBasePower;
    double m_dPowerBonus;
};


class CSkillEffect_i_m_attack_mp : public CSkillEffect {
public:
    CSkillEffect_i_m_attack_mp(double power, int checkMaxMP, int baseMP);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
    int m_nCheckMaxMP;
    int m_nBaseMP;
};


class CSkillEffect_i_p_attack : public CSkillEffect {
public:
    CSkillEffect_i_p_attack(double power, int specialProb, int ignoreDefence, int physicalProb) :
        m_Power(power), m_SpecialProb(specialProb), m_IgnoreDefence(ignoreDefence), m_PhysicalProb(physicalProb) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Power;
    int m_SpecialProb;
    int m_IgnoreDefence;
    int m_PhysicalProb;
};

class CSkillEffect_i_p_attack_abnormal : public CSkillEffect {
public:
    CSkillEffect_i_p_attack_abnormal(
        double power, int specialProb, int ignoreDefence, int physicalProb, char16_t* slotType, double additionalBonus
    );

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Power;
    int m_SpecialProb;
    int m_IgnoreDefence;
    int m_PhysicalProb;
    AbnormalStatusSlotType m_nAbnormalType;
    double m_dAdditionalBonus;
};


class CSkillEffect_i_p_attack_save_hp : public CSkillEffect {
public:
    CSkillEffect_i_p_attack_save_hp(double power, int specialProb, int ignoreDefence, int physicalProb, int saveHP);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Power;
    int m_SpecialProb;
    int m_IgnoreDefence;
    int m_PhysicalProb;
    double m_SaveHP;
};


class CSkillEffect_i_physical_attack_hp_link : public CSkillEffect {
public:
    CSkillEffect_i_physical_attack_hp_link(double power, int specialProb, int ignoreDefence, int physicalProb);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
    int m_nSpecialProb;
    int m_nIgnoreDefence;
    int m_PhysicalProb;
};


class CSkillEffect_i_p_attack_by_dist : public CSkillEffect {
public:
    CSkillEffect_i_p_attack_by_dist(double power, int specialProb, int ignoreDefence, int physicalProb) :
        m_Power(power), m_SpecialProb(specialProb), m_IgnoreDefence(ignoreDefence), m_PhysicalProb(physicalProb) {
        // Current L2Server 0x00ca2574.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Power;
    int m_SpecialProb;
    int m_IgnoreDefence;
    int m_PhysicalProb;
};

class CSkillEffect_i_p_percent_attack : public CSkillEffect {
public:
    CSkillEffect_i_p_percent_attack(double powerPercent, int specialProb, int ignoreDefence, int physicalProb) :
        m_dPowerPercent(powerPercent),
        m_SpecialProb(specialProb),
        m_IgnoreDefence(ignoreDefence),
        m_PhysicalProb(physicalProb) {
        // Current L2Server 0x00ca2be8.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPowerPercent;
    int m_SpecialProb;
    int m_IgnoreDefence;
    int m_PhysicalProb;
};


class CSkillEffect_i_p_attack_weapon_bonus : public CSkillEffect {
public:
    CSkillEffect_i_p_attack_weapon_bonus(double power, int specialProb, int ignoreDefence, int physicalProb);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    friend class CParserForSkill;

    double CalcPower(CCreature* creature);

    double m_Power;
    int m_SpecialProb;
    int m_IgnoreDefence;
    int m_PhysicalProb;
    std::vector<WeaponBonusType> m_vWeaponBonusTypeList;
};


class CSkillEffect_i_energy_attack : public CSkillEffect {
public:
    CSkillEffect_i_energy_attack(
        double power, int specialProbability, int energy, int ignoreDefence, int physicalProbability
    ) :
        m_dPower(power),
        m_nSpecialProb(specialProbability),
        m_nIgnoreDefence(ignoreDefence),
        m_PhysicalProb(physicalProbability),
        m_nEnergy(energy) {
        // Current L2Server 0x00ca3f4c.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;


private:
    double m_dPower;
    int m_nSpecialProb;
    int m_nIgnoreDefence;
    int m_PhysicalProb;
    [[maybe_unused]] int m_nEnergy;
};


class CSkillEffect_i_add_event_contribute : public CSkillEffect {
public:
    CSkillEffect_i_add_event_contribute(int minimumCount, int maximumCount) :
        m_nMinCount(minimumCount), m_nMaxCount(maximumCount) {
        // Current layout: the two parser-supplied counts occupy +0x30/+0x34.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nMinCount; // +0x30
    int m_nMaxCount; // +0x34
};


class CSkillEffect_i_restoration : public CSkillEffect {
public:
    CSkillEffect_i_restoration(std::u16string name, int count, int enchant);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string xwsItemName; // +0x30
    int nCount; // +0x58
    int nEnchant; // +0x5c
};


class CSkillEffect_i_restoration_random : public CSkillEffect {
public:
    CSkillEffect_i_restoration_random() : m_vRestorationList(nullptr), nConsumeItemCount(0) {
        // Current L2Server 0x00ca4474.
    }

    CSkillEffect_i_restoration_random(std::u16string consumeItemName, int consumeItemCount) :
        m_vRestorationList(nullptr), nConsumeItemName(std::move(consumeItemName)), nConsumeItemCount(consumeItemCount) {
        // Current L2Server 0x00ca44fc.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    friend class CParserForSkill;

    CRestorationUnitListVector* m_vRestorationList; // +0x30
    std::u16string nConsumeItemName; // +0x38
    int nConsumeItemCount; // +0x60
};


class CSkillEffect_i_restoration_AddRandom : public CSkillEffect {
public:
    CSkillEffect_i_restoration_AddRandom() {
        // The generated parser owns both
        // pointer assignments. The target constructor leaves them unset.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    friend class CParserForSkill;

    CRestorationUnitListVector* m_vRestorationList; // +0x30
    CRestorationUnitVector* m_vRewardItemList; // +0x38
};


class CSkillEffect_i_restoration_random_group : public CSkillEffect {
public:
    CSkillEffect_i_restoration_random_group() {
        // The generated parser owns the
        // pointer assignment. The target constructor leaves it unset.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    friend class CParserForSkill;

    CRestorationUnitGroupListVector* m_vRestorationGroupList; // +0x30
};


class CSkillEffect_i_event_attack : public CSkillEffect {
public:
    explicit CSkillEffect_i_event_attack(double damage) : m_dDamage(damage) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    double m_dDamage;
};

class CSkillEffect_i_p_soul_attack : public CSkillEffect {
public:
    CSkillEffect_i_p_soul_attack(double power, int specialProb, int ignoreDefence, int physicalProb) :
        m_Power(power), m_SpecialProb(specialProb), m_IgnoreDefence(ignoreDefence), m_PhysicalProb(physicalProb) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Power;
    int m_SpecialProb;
    int m_IgnoreDefence;
    int m_PhysicalProb;
};

class CSkillEffect_i_heal : public CSkillEffect {
public:
    explicit CSkillEffect_i_heal(double power) : m_dPower(power) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
};

class CSkillEffect_i_heal_special : public CSkillEffect {
public:
    explicit CSkillEffect_i_heal_special(double power);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
    CSkillEffect_i_heal m_heal_effect;
};

class CSkillEffect_i_heal_link : public CSkillEffect {
public:
    CSkillEffect_i_heal_link(double power, SkillFxChangeType changeType, int decreaseAmount) :
        m_Sfct(changeType), m_Power(power), m_DecreaseAmt(decreaseAmount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double CalcIHeal(CCreature* creature, const CSkillInfo* skillInfo, CSPointer<CCreature> target);

    SkillFxChangeType m_Sfct;
    double m_Power;
    int m_DecreaseAmt;
};


class CSkillEffect_i_hp : public CSkillEffect {
public:
    CSkillEffect_i_hp(double amount, SkillFxChangeType changeType) : m_dAmount(amount), m_Sfct(changeType) {}

    bool InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dAmount;
    SkillFxChangeType m_Sfct;
};


class CSkillEffect_i_hp_special : public CSkillEffect {
public:
    CSkillEffect_i_hp_special(double amount, SkillFxChangeType changeType) : m_dAmount(amount), m_Sfct(changeType) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dAmount;
    SkillFxChangeType m_Sfct;
};


class CSkillEffect_i_mp : public CSkillEffect {
public:
    CSkillEffect_i_mp(double amount, SkillFxChangeType changeType);

    bool InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dAmount;
    SkillFxChangeType m_Sfct;
};


class CSkillEffect_i_mp_by_hp : public CSkillEffect {
public:
    CSkillEffect_i_mp_by_hp(double amount, SkillFxChangeType changeType) : m_dAmount(amount), m_Sfct(changeType) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dAmount;
    SkillFxChangeType m_Sfct;
};


class CSkillEffect_i_hp_drain : public CSkillEffect {
public:
    CSkillEffect_i_hp_drain(double power, double ratio) : m_dPower(power), m_dRatio(ratio) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
    double m_dRatio;
};

class CSkillEffect_i_hp_per_max : public CSkillEffect {
public:
    explicit CSkillEffect_i_hp_per_max(double amount) : m_dAmount(amount) {}

    bool InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    static bool InstantEffect(CCreature* creature, CCreature& target, double amount, const CSkillInfo* skillInfo);

private:
    double m_dAmount;
};

class CSkillEffect_i_mp_per_max : public CSkillEffect {
public:
    explicit CSkillEffect_i_mp_per_max(double amount) : m_dAmount(amount) {}

    bool InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dAmount;
};

class CSkillEffect_i_mp_by_level : public CSkillEffect {
public:
    explicit CSkillEffect_i_mp_by_level(int amount) : nAmount(amount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nAmount;
};

class CSkillEffect_i_mp_by_level_self : public CSkillEffect {
public:
    explicit CSkillEffect_i_mp_by_level_self(int amount) : nAmount(amount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nAmount;
};

class CSkillEffect_i_cp_per_max : public CSkillEffect {
public:
    explicit CSkillEffect_i_cp_per_max(double amount) : m_dAmount(amount) {}

    bool InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    static bool InstantEffect(CCreature* creature, CCreature& target, double amount, const CSkillInfo* skillInfo);

private:
    double m_dAmount;
};

class CSkillEffect_i_backstab : public CSkillEffect {
public:
    CSkillEffect_i_backstab(double power, double bonus, int specialProb);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
    double m_dBonus;
    int m_nSpecialProb;
};


class CSkillEffect_i_fatal_blow : public CSkillEffect {
public:
    CSkillEffect_i_fatal_blow(double power, double bonus, int specialProb) :
        m_dBonus(bonus), m_dPower(power), m_nSpecialProb(specialProb), m_TargetAlignmentPadding(0) {}

    void CalcSuccess(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction
    ) override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
    bool ApplyBlowEffect(
        CCreature* creature,
        CObject* target,
        const CSkillInfo* skillInfo,
        CSkillAction2* skillAction,
        double distance,
        bool useSoul
    );

protected:
    double m_dBonus;
    double m_dPower;
    int m_nSpecialProb;
    // MSVC leaves this alignment lane between the complete base and the
    // first field of i_fatal_blow_abnormal. Keep it explicit so Itanium
    // cannot reuse the tail padding.
    std::uint32_t m_TargetAlignmentPadding;
};

class CSkillEffect_i_fatal_blow_abnormal : public CSkillEffect_i_fatal_blow {
public:
    CSkillEffect_i_fatal_blow_abnormal(
        double power, double bonus, int specialProb, char16_t* abnormalType, double additionalBonus
    ) :
        CSkillEffect_i_fatal_blow(power, bonus, specialProb),
        m_dAdditionalBonus(additionalBonus),
        m_dDefaultPower(power),
        m_nAbnormalType(GetAbnormalStatusSlotTypeByName(abnormalType)) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dAdditionalBonus;
    double m_dDefaultPower;
    AbnormalStatusSlotType m_nAbnormalType;
};

class CSkillEffect_i_soul_blow : public CSkillEffect_i_fatal_blow {
public:
    CSkillEffect_i_soul_blow(double power, double bonus, int specialProb) :
        CSkillEffect_i_fatal_blow(power, bonus, specialProb) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_Enchant_Weapon : public CSkillEffect {
public:
    CSkillEffect_i_Enchant_Weapon(SkillFxChangeType type, double value) : m_Value(value), m_Type(type) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Value;
    SkillFxChangeType m_Type;
};

class CSkillEffect_i_Enchant_Armor : public CSkillEffect {
public:
    CSkillEffect_i_Enchant_Armor(SkillFxChangeType type, double value) : m_Value(value), m_Type(type) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Value;
    SkillFxChangeType m_Type;
};

class CSkillEffect_i_Enchant_Weapon_Rate : public CSkillEffect {
public:
    explicit CSkillEffect_i_Enchant_Weapon_Rate(int rate) : m_nRate(rate) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nRate;
};

class CSkillEffect_i_Enchant_Armor_Rate : public CSkillEffect {
public:
    explicit CSkillEffect_i_Enchant_Armor_Rate(int rate) : m_nRate(rate) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nRate;
};

class CSkillEffect_i_Enchant_Item_MultiLevel : public CSkillEffect {
public:
    CSkillEffect_i_Enchant_Item_MultiLevel(
        double level1Prob, double level2Prob, double level3Prob, SkillFxChangeType type, double value
    ) :
        m_Level1Prob(level1Prob), m_Level2Prob(level2Prob), m_Level3Prob(level3Prob), m_Value(value), m_Type(type) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Level1Prob;
    double m_Level2Prob;
    double m_Level3Prob;
    double m_Value;
    SkillFxChangeType m_Type;
};

class CSkillEffect_i_Enchant_Attribute : public CSkillEffect {
public:
    CSkillEffect_i_Enchant_Attribute(
        AttributeTypeEnum attackAttributeType,
        AttributeTypeEnum defendAttributeType,
        int grade,
        int additionalProbability,
        int additionalValue
    ) :
        m_eAttackAttrType(attackAttributeType),
        m_eDefendAttrType(defendAttributeType),
        m_nGrade(grade),
        m_nAdditionalProb(additionalProbability),
        m_nAdditionalValue(additionalValue) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    AttributeTypeEnum m_eAttackAttrType;
    AttributeTypeEnum m_eDefendAttrType;
    int m_nGrade;
    int m_nAdditionalProb;
    int m_nAdditionalValue;
};

class CSkillEffect_i_Enchant_Attribution_Item : public CSkillEffect {
public:
    CSkillEffect_i_Enchant_Attribution_Item(
        AttributeTypeEnum attackAttributeType,
        AttributeTypeEnum defendAttributeType,
        int grade,
        int additionalProbability,
        int additionalValue
    ) :
        m_eAttackAttrType(attackAttributeType),
        m_eDefendAttrType(defendAttributeType),
        m_nGrade(grade),
        m_nAdditionalProb(additionalProbability),
        m_nAdditionalValue(additionalValue) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    AttributeTypeEnum m_eAttackAttrType;
    AttributeTypeEnum m_eDefendAttrType;
    int m_nGrade;
    int m_nAdditionalProb;
    int m_nAdditionalValue;
};

class CSkillEffect_i_add_enchant_weapon_level_inc : public CSkillEffect {
public:
    CSkillEffect_i_add_enchant_weapon_level_inc(
        int level1, int level2, int level3, double level1Prob, double level2Prob, double level3Prob
    ) :
        m_nLevel1(level1),
        m_nLevel2(level2),
        m_nLevel3(level3),
        m_Level1Prob(level1Prob),
        m_Level2Prob(level2Prob),
        m_Level3Prob(level3Prob) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nLevel1;
    int m_nLevel2;
    int m_nLevel3;
    double m_Level1Prob;
    double m_Level2Prob;
    double m_Level3Prob;
};

class CSkillEffect_i_add_enchant_armor_level_inc : public CSkillEffect {
public:
    CSkillEffect_i_add_enchant_armor_level_inc(
        int level1, int level2, int level3, double level1Prob, double level2Prob, double level3Prob
    ) :
        m_nLevel1(level1),
        m_nLevel2(level2),
        m_nLevel3(level3),
        m_Level1Prob(level1Prob),
        m_Level2Prob(level2Prob),
        m_Level3Prob(level3Prob) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nLevel1;
    int m_nLevel2;
    int m_nLevel3;
    double m_Level1Prob;
    double m_Level2Prob;
    double m_Level3Prob;
};

class CSkillEffect_i_sub_enchant_weapon_level_dec : public CSkillEffect {
public:
    explicit CSkillEffect_i_sub_enchant_weapon_level_dec(int subLevel) : m_nSubLevel(subLevel) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nSubLevel;
};

class CSkillEffect_i_sub_enchant_armor_level_dec : public CSkillEffect {
public:
    explicit CSkillEffect_i_sub_enchant_armor_level_dec(int subLevel) : m_nSubLevel(subLevel) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nSubLevel;
};

class CSkillEffect_i_change_face : public CSkillEffect {
public:
    explicit CSkillEffect_i_change_face(int type) : nType(type) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nType;
};

class CSkillEffect_i_change_hair_style : public CSkillEffect {
public:
    explicit CSkillEffect_i_change_hair_style(int type) : nType(type) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nType;
};

class CSkillEffect_i_change_hair_color : public CSkillEffect {
public:
    explicit CSkillEffect_i_change_hair_color(int type) : nType(type) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nType;
};

class CSkillEffect_i_change_class : public CSkillEffect {
public:
    explicit CSkillEffect_i_change_class(int subJobId) : m_SubJobID(subJobId) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_SubJobID;
};


class CSkillEffect_i_get_exp : public CSkillEffect {
public:
    CSkillEffect_i_get_exp(std::int64_t amount, int standardLevel, double ratio) :
        nAmount(amount), nStandardLevel(standardLevel), m_TargetAlignmentPadding(0), nRatio(ratio) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::int64_t nAmount;
    int nStandardLevel;
    [[maybe_unused]] std::uint32_t m_TargetAlignmentPadding;
    double nRatio;
};

class CSkillEffect_i_pk_count : public CSkillEffect {
public:
    explicit CSkillEffect_i_pk_count(int delta) : nDelta(delta) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    int nDelta;
};

class CSkillEffect_i_confuse : public CSkillEffect {
public:
    CSkillEffect_i_confuse(int probability, int time) : nProb(probability), nTime(time) { eEffectType = EFT_CONFUSE; }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nProb;
    int nTime;
};

class CSkillEffect_i_distrust : public CSkillEffect {
public:
    CSkillEffect_i_distrust(int probability, int time) : nProb(probability), nTime(time) { eEffectType = EFT_DISTRUST; }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nProb;
    int nTime;
};

class CSkillEffect_i_agathion_energy : public CSkillEffect {
public:
    CSkillEffect_i_agathion_energy(double amount, SkillFxChangeType changeType) : m_dAmount(amount), m_Sfct(changeType) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dAmount;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_i_call_party : public CSkillEffect {
public:
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_cp : public CSkillEffect {
public:
    CSkillEffect_i_cp(int amount, SkillFxChangeType changeType) : m_nAmount(amount), m_sfct(changeType) {}

    bool InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nAmount;
    SkillFxChangeType m_sfct;
};

class CSkillEffect_i_sp : public CSkillEffect {
public:
    CSkillEffect_i_sp(int amount, SkillFxChangeType changeType) : m_nAmount(amount), m_sfct(changeType) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nAmount;
    SkillFxChangeType m_sfct;
};

class CSkillEffect_i_inform : public CSkillEffect {
public:
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_show_reuse_delay : public CSkillEffect {
public:
    explicit CSkillEffect_i_show_reuse_delay(std::vector<std::int64_t>& skillIds);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::vector<int> m_SkillIds;
};

class CSkillEffect_i_karma_count : public CSkillEffect {
public:
    CSkillEffect_i_karma_count(SkillFxChangeType changeType, int delta) : m_sfct(changeType), m_nDelta(delta) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    SkillFxChangeType m_sfct;
    int m_nDelta;
};

class CSkillEffect_i_despawn : public CSkillEffect {
public:
    explicit CSkillEffect_i_despawn(double probability) : m_dProbability(probability) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dProbability;
};

class CSkillEffect_i_food_for_pet : public CSkillEffect {
public:
    CSkillEffect_i_food_for_pet(int delta, int userDelta, int wyvernDelta) :
        nDelta(delta), nUserDelta(userDelta), nWyvernDelta(wyvernDelta) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nDelta;
    int nUserDelta;
    int nWyvernDelta;
};

class CSkillEffect_i_mount_for_event : public CSkillEffect {
public:
    explicit CSkillEffect_i_mount_for_event(const char16_t* npcName) : m_npcName(npcName == nullptr ? u"" : npcName) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string m_npcName;
};

class CSkillEffect_i_dismount_for_event : public CSkillEffect {
public:
    explicit CSkillEffect_i_dismount_for_event(const char16_t* npcName) : m_npcName(npcName == nullptr ? u"" : npcName) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string m_npcName;
};

class CSkillEffect_i_add_max_entrance_inzone : public CSkillEffect {
public:
    CSkillEffect_i_add_max_entrance_inzone(int groupId, int delta) : m_groupId(groupId), m_delta(delta) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_groupId;
    int m_delta;
};

class CSkillEffect_i_restore_vital_point : public CSkillEffect {
public:
    CSkillEffect_i_restore_vital_point(int restoreAmount, VitalityRestore::Type type) :
        m_RestoreAmount(restoreAmount), m_nType(type) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_RestoreAmount;
    VitalityRestore::Type m_nType;
};

class CSkillEffect_i_summon_trap : public CSkillEffect {
public:
    explicit CSkillEffect_i_summon_trap(std::u16string pcTrapName) :
        m_PCTrapName(std::move(pcTrapName)), m_PCTrapClassId(-1) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string m_PCTrapName; // +0x30
    int m_PCTrapClassId; // +0x58
};

class CSkillEffect_i_summon_npc : public CSkillEffect {
public:
    CSkillEffect_i_summon_npc(std::u16string npcName, int count) : xwsNPCName(std::move(npcName)), nCount(count) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string xwsNPCName; // +0x30
    int nCount; // +0x58
};

class CSkillEffect_i_summon_npc_position : public CSkillEffect {
public:
    explicit CSkillEffect_i_summon_npc_position(std::u16string npcName) : xwsNPCName(std::move(npcName)) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string xwsNPCName; // +0x30
};

class CSkillEffect_i_summon_npc_near : public CSkillEffect {
public:
    CSkillEffect_i_summon_npc_near(std::u16string npcName, int count, int distance) :
        xwsNPCName(std::move(npcName)), nCount(count), nDistance(distance) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string xwsNPCName; // +0x30
    int nCount; // +0x58
    int nDistance; // +0x5c
};

class CSkillEffect_i_summon_npc_dying : public CSkillEffect {
public:
    CSkillEffect_i_summon_npc_dying(std::u16string npcName, int count) : xwsNPCName(std::move(npcName)), nCount(count) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string xwsNPCName; // +0x30
    int nCount; // +0x58
};

class CSkillEffect_i_call_pc : public CSkillEffect {
public:
    CSkillEffect_i_call_pc() = default;
    CSkillEffect_i_call_pc(int itemClassId, int itemCount) : m_nItemClassId(itemClassId), m_nItemCount(itemCount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nItemClassId{}; // +0x30
    int m_nItemCount{}; // +0x34
};

class CSkillEffect_i_summon : public CSkillEffect {
public:
    CSkillEffect_i_summon(
        const char16_t* npcName, int masterExpRatio, const char16_t* itemName, int itemCount, int duration
    );

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string m_sNPCName; // +0x30
    int m_nMasterExpRatio; // +0x58
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment005cTo0060{}; // +0x5c..+0x5f
    std::u16string m_sItemName; // +0x60
    int m_nItemCount; // +0x88
    int m_nDuration; // +0x8c
};


class CSkillEffect_i_summon_multi : public CSkillEffect {
public:
    CSkillEffect_i_summon_multi(
        const char16_t* npcName, int masterExpRatio, const char16_t* consumeItemName, int consumeItemCount
    ) :
        m_NpcName(npcName == nullptr ? u"" : npcName),
        m_nMasterExpRatio(masterExpRatio),
        m_ConsumeItemName(consumeItemName == nullptr ? u"" : consumeItemName),
        m_nConsumeItemCount(consumeItemCount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string m_NpcName; // +0x30
    int m_nMasterExpRatio; // +0x58
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment005cTo0060{}; // +0x5c..+0x5f
    std::u16string m_ConsumeItemName; // +0x60
    int m_nConsumeItemCount; // +0x88
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment008cTo0090{}; // +0x8c..+0x8f
};

class CSkillEffect_i_summon_pledge : public CSkillEffect {
public:
    explicit CSkillEffect_i_summon_pledge(const char16_t* npcName) : m_NpcName(npcName == nullptr ? u"" : npcName) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string m_NpcName; // +0x30
};

class CSkillEffect_i_summon_unique_npc : public CSkillEffect {
public:
    explicit CSkillEffect_i_summon_unique_npc(std::u16string name) : m_NpcName(std::move(name)) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string m_NpcName; // +0x30
};

class CSkillEffect_i_teleport_to_npc : public CSkillEffect {
public:
    explicit CSkillEffect_i_teleport_to_npc(std::u16string name) : m_NpcName(std::move(name)) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string m_NpcName; // +0x30
};

class CSkillEffect_i_party_teleport_to_npc : public CSkillEffect {
public:
    explicit CSkillEffect_i_party_teleport_to_npc(std::u16string name) : m_NpcName(std::move(name)) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string m_NpcName; // +0x30
};

class CSkillEffect_i_blink_party : public CSkillEffect {
public:
    CSkillEffect_i_blink_party(double distance, SkillDirectionType direction, double effectRange) :
        m_Distance(distance), m_Direction(direction), m_EffectRange(effectRange) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Distance; // +0x30
    SkillDirectionType m_Direction; // +0x38
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment003cTo0040{}; // +0x3c..+0x3f
    double m_EffectRange; // +0x40
};


class CSkillEffect_cub_m_attack : public CSkillEffect {
public:
    explicit CSkillEffect_cub_m_attack(double power) : m_dPower(power) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    double m_dPower;
};

class CSkillEffect_cub_hp_drain : public CSkillEffect {
public:
    CSkillEffect_cub_hp_drain(double power, double ratio) : m_dPower(power), m_dRatio(ratio) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    double m_dPower;
    double m_dRatio;
};

class CSkillEffect_cub_heal : public CSkillEffect {
public:
    explicit CSkillEffect_cub_heal(int healHp) : m_nHealHP(healHp) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    int m_nHealHP;
};

class CSkillEffect_i_m_attack_by_abnormal_slot : public CSkillEffect {
public:
    CSkillEffect_i_m_attack_by_abnormal_slot(char16_t* slotName, double power) :
        m_dPower(power), m_SlotType(GetAbnormalStatusSlotTypeByName(slotName)) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
    AbnormalStatusSlotType m_SlotType;
};

class CSkillEffect_i_abnormal_time_change : public CSkillEffect {
public:
    CSkillEffect_i_abnormal_time_change(
        char16_t* slotName, DebuffType slotType, double timeBonus, SkillFxChangeType changeType, int ignoreSpecialLevel
    );
    CSkillEffect_i_abnormal_time_change(
        char16_t* slotName, DebuffType slotType, SkillFxChangeType changeType, int ignoreSpecialLevel
    );

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    bool CheckSlotType(const CSkillInfo& skillInfo) const;

    AbnormalStatusSlotType m_AbnormalType;
    double dTimeBonus;
    SkillFxChangeType sfct;
    DebuffType m_SlotType;
    int m_nIgnoreSpecialLevel;
};


class CSkillEffect_i_dispel_by_name : public CSkillEffect {
public:
    CSkillEffect_i_dispel_by_name() = default;

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    // The generated skill parser appends directly to this list.
    std::vector<wchar_t*> m_vDispelSkillName;
};


class CSkillEffect_i_dispel_by_slot : public CSkillEffect {
public:
    CSkillEffect_i_dispel_by_slot(AbnormalStatusSlotType abnormalType, int level) :
        m_nAbnormalType(abnormalType), m_nLevel(level) {}

    bool InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo) override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    AbnormalStatusSlotType m_nAbnormalType;
    int m_nLevel;
};


class CSkillEffect_i_dispel_by_slot_probability : public CSkillEffect {
public:
    CSkillEffect_i_dispel_by_slot_probability(char16_t* slotName, int probability);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    AbnormalStatusSlotType m_nAbnormalType;
    int nProb;
};


class CSkillEffect_i_dispel_by_slot_myself : public CSkillEffect {
public:
    explicit CSkillEffect_i_dispel_by_slot_myself(char16_t* slotName);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    AbnormalStatusSlotType m_nAbnormalType;
};


class CSkillEffect_i_dispel_by_category : public CSkillEffect {
public:
    CSkillEffect_i_dispel_by_category(int probability, int maximumSlots) :
        eDeBuffType(DB_MAX), nProb(probability), nMaxSlot(maximumSlots) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    DebuffType eDeBuffType;
    int nProb;
    int nMaxSlot;
};


class CSkillEffect_i_set_skill : public CSkillEffect {
public:
    CSkillEffect_i_set_skill(int skillId, int skillLevel);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_SkillId;
    int m_SkillLevel;
};


class CSkillEffect_i_skill_turning : public CSkillEffect {
public:
    CSkillEffect_i_skill_turning(int magicType, int probability);
    CSkillEffect_i_skill_turning(std::vector<std::int64_t>* magicTypes, int probability);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nProb;
    std::vector<int> m_MagicTypeList;
};


class CSkillEffect_i_fly_away : public CSkillEffect {
public:
    CSkillEffect_i_fly_away() : nDist(600) {}
    explicit CSkillEffect_i_fly_away(int distance) : nDist(distance) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    // The generated parser writes both flags after using the one-argument
    // constructor. The current no-argument constructor leaves them as the
    // allocator supplied them, so they have no initializer.
    bool bIsCurve;
    bool bIsPush;
    int nDist;
};


class CSkillEffect_i_fly_self : public CSkillEffect {
public:
    explicit CSkillEffect_i_fly_self(int distance) : nDist(distance) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    // The generated parser publishes both flags after construction. The
    // current constructor initializes only nDist, exactly like i_fly_away.
    bool bIsCurve;
    bool bIsPush;
    int nDist;
};

class CSkillEffect_i_register_siege_golem : public CSkillEffect {
public:
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_summon_hallucination : public CSkillEffect {
public:
    CSkillEffect_i_summon_hallucination(int classId, int count) : m_nClassId(classId), m_nCount(count) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nClassId;
    int m_nCount;
};

class CSkillEffect_i_show_usable_daily_skill_count : public CSkillEffect {
public:
    CSkillEffect_i_show_usable_daily_skill_count(
        std::vector<std::int64_t>* skillIds, std::vector<std::int64_t>* skillLevels
    ) :
        m_SkillIds(*skillIds), m_SkillLevels(*skillLevels) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::vector<std::int64_t> m_SkillIds;
    std::vector<std::int64_t> m_SkillLevels;
};


class CSkillEffect_i_blink : public CSkillEffect {
public:
    CSkillEffect_i_blink(double distance, SkillDirectionType direction) : m_Dist(distance), m_Direction(direction) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Dist;
    SkillDirectionType m_Direction;
};


class CSkillEffect_i_teleport_to_target : public CSkillEffect {
public:
    CSkillEffect_i_teleport_to_target() = default;

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};


class CSkillEffect_i_random_target : public CSkillEffect {
public:
    CSkillEffect_i_random_target() = default;

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};


class CSkillEffect_i_set_reuse_delay : public CSkillEffect {
public:
    CSkillEffect_i_set_reuse_delay(int skillId, int reuseTimeSec, int updateStyle) :
        m_nSkillId(skillId), m_nReuseTimeSec(reuseTimeSec), m_nUpdateStyle(updateStyle) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nSkillId;
    int m_nReuseTimeSec;
    int m_nUpdateStyle;
};


class CSkillEffect_i_reuse_skill_reset : public CSkillEffect {
public:
    CSkillEffect_i_reuse_skill_reset() : m_dProb(0.0), m_nDelayLimitMSec(0) {}

    bool Init(const std::vector<std::int64_t>& skillIds, double probability, int delayLimitMSec);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::vector<int> m_resetSkillList;
    double m_dProb;
    int m_nDelayLimitMSec;
};


class CSkillEffect_i_reuse_skill_reduce : public CSkillEffect {
public:
    CSkillEffect_i_reuse_skill_reduce() : m_dProb(0.0), m_nReduceMSec(0), m_nDelayLimitMSec(0) {}

    bool Init(const std::vector<std::int64_t>& skillIds, double probability, int reduceMSec, int delayLimitMSec);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::deque<int> m_reduceSkillList;
    double m_dProb;
    int m_nReduceMSec;
    int m_nDelayLimitMSec;
};


class CSkillEffect_i_reset_quest : public CSkillEffect {
public:
    explicit CSkillEffect_i_reset_quest(const std::vector<std::int64_t>& questIds) : m_QuestIds(questIds) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::vector<std::int64_t> m_QuestIds;
};


class CSkillEffect_i_betray : public CSkillEffect {
public:
    CSkillEffect_i_betray(int probability, int duration) : m_nProb(probability), m_nDuration(duration) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nProb;
    int m_nDuration;
};


class CSkillEffect_i_sidestep_to_target : public CSkillEffect {
public:
    explicit CSkillEffect_i_sidestep_to_target(SkillFxPositionType direction) : m_eDirection(direction) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    SkillFxPositionType m_eDirection;
};


class CSkillEffect_i_goto_target : public CSkillEffect {
public:
    CSkillEffect_i_goto_target() = default;

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};


class CSkillEffect_i_teleport_to_summon : public CSkillEffect {
public:
    explicit CSkillEffect_i_teleport_to_summon(int distance) : m_Distance(distance) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_Distance;
};


class CSkillEffect_i_bookmark_teleport : public CSkillEffect {
public:
    CSkillEffect_i_bookmark_teleport() = default;

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};


class CSkillEffect_i_ensoul : public CSkillEffect {
public:
    explicit CSkillEffect_i_ensoul(double probability) : m_dProb(probability) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dProb;
};


class CSkillEffect_i_convert_item : public CSkillEffect {
public:
    CSkillEffect_i_convert_item() = default;

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};


class CSkillEffect_i_ev_ink_party : public CSkillEffect {
public:
    explicit CSkillEffect_i_ev_ink_party(int skillInkEnergy) : m_nSkillInkEnergy(skillInkEnergy) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nSkillInkEnergy;
};


class CSkillEffect_i_create_item_random : public CSkillEffect {
public:
    CSkillEffect_i_create_item_random() = default;

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};


struct FixedShapeShiftExtractionItemInfo {
    std::u16string xwsItemName;
    int nProb;
};


class CSkillEffect_i_shape_shift_item_auto_matching : public CSkillEffect {
public:
    CSkillEffect_i_shape_shift_item_auto_matching();
    explicit CSkillEffect_i_shape_shift_item_auto_matching(int period);
    ~CSkillEffect_i_shape_shift_item_auto_matching() override;

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    ShapeShiftAutoMatchingType::type FindType(CItem* item);

    // The generated skill parser fills the selected matching bucket
    // directly before publishing the effect.
    std::vector<FixedShapeShiftExtractionItemInfo> m_vExtractionItems[ShapeShiftAutoMatchingType::SSAMT_MAX];
    int m_nPeriod;
};


class CSkillEffect_i_shape_shift_wp_fixed : public CSkillEffect {
public:
    CSkillEffect_i_shape_shift_wp_fixed();
    explicit CSkillEffect_i_shape_shift_wp_fixed(int period);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    std::vector<FixedShapeShiftExtractionItemInfo> m_vExtractionItems;
    int m_nPeriod;
};


class CSkillEffect_i_shape_shift_wp : public CSkillEffect {
public:
    CSkillEffect_i_shape_shift_wp() : m_nPeriod(-1) {}
    explicit CSkillEffect_i_shape_shift_wp(int period) : m_nPeriod(period) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nPeriod;
};

class CSkillEffect_i_bless_shape_shift_wp : public CSkillEffect {
public:
    CSkillEffect_i_bless_shape_shift_wp() : m_nPeriod(-1) {}
    explicit CSkillEffect_i_bless_shape_shift_wp(int period) : m_nPeriod(period) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nPeriod;
};

class CSkillEffect_i_restore_shape_shift_item : public CSkillEffect {
public:
    CSkillEffect_i_restore_shape_shift_item() = default;

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};


class CSkillEffect_i_shape_shift_am_fixed : public CSkillEffect {
public:
    CSkillEffect_i_shape_shift_am_fixed();
    explicit CSkillEffect_i_shape_shift_am_fixed(int period);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    std::vector<FixedShapeShiftExtractionItemInfo> m_vExtractionItems;
    int m_nPeriod;
};


class CSkillEffect_i_shape_shift_am : public CSkillEffect {
public:
    CSkillEffect_i_shape_shift_am() : m_nPeriod(-1) {}
    explicit CSkillEffect_i_shape_shift_am(int period) : m_nPeriod(period) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nPeriod;
};

class CSkillEffect_i_bless_shape_shift_am : public CSkillEffect {
public:
    CSkillEffect_i_bless_shape_shift_am() : m_nPeriod(-1) {}
    explicit CSkillEffect_i_bless_shape_shift_am(int period) : m_nPeriod(period) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nPeriod;
};


class CSkillEffect_i_position_change : public CSkillEffect {
public:
    explicit CSkillEffect_i_position_change(double probability) : m_dProbability(probability) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dProbability;
};


class CSkillEffect_i_knockback : public CSkillEffect {
public:
    CSkillEffect_i_knockback(int distance, int speed, int acceleration, int mode);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_Dist;
    int m_Speed;
    int m_Acceleration;
    FLY_TO_LOCATION_TYPE m_Mode;
};


class CSkillEffect_i_pull : public CSkillEffect {
public:
    CSkillEffect_i_pull(int speed, int acceleration) : m_Speed(speed), m_Acceleration(acceleration) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_Speed;
    int m_Acceleration;
};


class CSkillEffect_i_t_hp_critical : public CSkillEffect {
public:
    explicit CSkillEffect_i_t_hp_critical(int amount) : m_Amount(amount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_Amount;
};


class CSkillEffect_i_target_cancel : public CSkillEffect {
public:
    explicit CSkillEffect_i_target_cancel(int probability) : nProb(probability) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nProb;
};


class CSkillEffect_i_target_me : public CSkillEffect {
public:
    CSkillEffect_i_target_me(int probability, bool ignoreProbability) : nProb(probability), bIgnoreProb(ignoreProbability) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nProb;
    bool bIgnoreProb;
};


class CSkillEffect_i_death : public CSkillEffect {
public:
    CSkillEffect_i_death(double probability, double secondaryProbability) :
        m_Prob(probability), m_Prob2(secondaryProbability) {
        // Current L2Server 0x00ca4928.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double CalculateProb(CCreature* creature, CCreature* target, double probability) const;

    double m_Prob;
    double m_Prob2;
};


class CSkillEffect_i_float_away : public CSkillEffect {
public:
    CSkillEffect_i_float_away(int flyType, int flyHeight);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;


private:
    const int m_nFlyType;
    const int m_nFlyHeight;
};


class CSkillEffect_i_npc_kill : public CSkillEffect {
public:
    explicit CSkillEffect_i_npc_kill(double probability);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    double dProb;
};


class CSkillEffect_i_teleport : public CSkillEffect {
public:
    explicit CSkillEffect_i_teleport(FVector position);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    FVector vPos;
};


class CSkillEffect_p_ability_change : public CSkillEffect {
public:
    CSkillEffect_p_ability_change();

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
    virtual void SetEnableStat(int index, int rate);


private:
    bool m_EnableStatBonus[13];
    int m_BonusRate[13];
};


class CSkillEffect_p_change_slot : public CSkillEffectTrigger {
public:
    CSkillEffect_p_change_slot(std::vector<AbnormalDataForTrigger> abnormalData, const char16_t* skillName);
    ~CSkillEffect_p_change_slot() override;

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    std::vector<AbnormalDataForTrigger> m_vAbnormalData;
};


struct CSkillRandomUnit {
    CSkillRandomUnit(std::u16string name, double probability);

    std::u16string xwsSkillName; // +0x00
    double dProb; // +0x28
};


class CSkillEffect_i_call_my_skill_list : public CSkillEffect {
public:
    explicit CSkillEffect_i_call_my_skill_list(const std::vector<std::int64_t>& skillIds);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::deque<int> m_calledSkillIdList; // +0x30
};


class CSkillEffect_i_call_random_skill_consume_item : public CSkillEffect {
public:
    CSkillEffect_i_call_random_skill_consume_item();

    void SetRandomUnitList(std::vector<CSkillRandomUnit*>* randomUnits);
    void OnPostLoad() override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    friend class CParserForSkill;

    // The generated parser sets this pointer immediately after construction.
    // the current constructor leaves it indeterminate.
    std::vector<CSkillRandomUnit*>* m_vSkillRandomUnit; // +0x30
    std::vector<const CSkillInfo*> m_vSkillList; // +0x38
};


class CSkillEffect_i_random_call_skill : public CSkillEffect {
public:
    CSkillEffect_i_random_call_skill();

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    friend class CParserForSkill;

    std::vector<std::u16string> m_vRandomSkillNameList; // +0x30
    std::vector<double> m_nRandomSkillProbList; // +0x50
};


class CSkillEffect_i_change_skill_level : public CSkillEffect {
public:
    CSkillEffect_i_change_skill_level(int targetSkillId, int downLevel) :
        nTargetSkillID(targetSkillId), nDownLevel(downLevel) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nTargetSkillID; // +0x30
    int nDownLevel; // +0x34
};


class CSkillEffect_i_call_skill : public CSkillEffect {
public:
    explicit CSkillEffect_i_call_skill(std::u16string skillName);

    CSkillInfo* GetCallSkillInfo() const { return m_pSkillInfo; }

    void OnPostLoad() override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    CSkillInfo* m_pSkillInfo;
    std::u16string m_SkillName;
};


class CSkillEffect_i_call_skill_by_target : public CSkillEffect {
public:
    explicit CSkillEffect_i_call_skill_by_target(std::u16string skillName);

    void OnPostLoad() override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    CSkillInfo* m_pSkillInfo; // +0x30
    std::u16string m_SkillName; // +0x38
};


class CSkillEffect_i_call_skill_by_target_divide : public CSkillEffect {
public:
    CSkillEffect_i_call_skill_by_target_divide(std::u16string firstSkillName, std::u16string secondSkillName);

    void OnPostLoad() override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    CSkillInfo* m_pSkillInfo[2]; // +0x30
    std::u16string m_SkillName[2]; // +0x40
};


class CSkillEffect_i_call_skill_by_abnormal_slot : public CSkillEffect {
public:
    CSkillEffect_i_call_skill_by_abnormal_slot(std::set<AbnormalStatusSlotType> abnormalSlotTypes, std::u16string skillName);

    void OnPostLoad() override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    CSkillInfo* m_pSkillInfo; // +0x30
    std::u16string m_SkillName; // +0x38
    std::set<AbnormalStatusSlotType> m_setAbnormalSlotType; // +0x60
};


class CSkillEffect_i_call_skill_by_force : public CSkillEffect {
public:
    struct ForSkillData {
        int activateRatio; // +0x00
        std::uint32_t m_TargetAlignmentPadding0;
        CSkillInfo* pSkillInfo; // +0x08
        SkillTargetTypeEnum targetType; // +0x10
        std::uint32_t m_TargetAlignmentPadding1;
    };

    CSkillEffect_i_call_skill_by_force();

    void AddForceSkill(int energyLevel, int activateRatio, std::u16string skillName, SkillTargetTypeEnum targetType);
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::map<int, ForSkillData> m_ForceSkillMap; // +0x30
};


class CSkillEffect_i_call_skill_by_targetdirection : public CSkillEffect {
public:
    CSkillEffect_i_call_skill_by_targetdirection(std::u16string skillName, int direction, int range);

    void OnPostLoad() override;
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    std::u16string m_SkillName; // +0x30
    int m_nDirection; // +0x58
    int m_nRange; // +0x5c
    CSkillInfo* m_pSkillInfo; // +0x60
};


class CSkillEffect_i_m_soul_attack : public CSkillEffect {
public:
    CSkillEffect_i_m_soul_attack(double power, double percent, bool forceHit) :
        m_dPower(power), m_dPer(percent), m_ForceHit(forceHit) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
    double m_dPer;
    bool m_ForceHit;
};

class CSkillEffect_i_m_attack_by_hp : public CSkillEffect {
public:
    explicit CSkillEffect_i_m_attack_by_hp(double power) : m_dPower(power) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
};

class CSkillEffect_i_m_attack_by_dist : public CSkillEffect {
public:
    explicit CSkillEffect_i_m_attack_by_dist(double power) : m_dPower(power) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
};

class CSkillEffect_i_m_attack_by_abnormal : public CSkillEffect {
public:
    explicit CSkillEffect_i_m_attack_by_abnormal(int power) : m_Power(static_cast<double>(power)) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Power;
};

class CSkillEffect_i_m_attack_link : public CSkillEffect {
public:
    CSkillEffect_i_m_attack_link(double power, SkillFxChangeType changeType, int bonus) :
        m_dPower(power), m_Sfct(changeType), m_Bonus(bonus) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
    SkillFxChangeType m_Sfct;
    int m_Bonus;
};

class CSkillEffect_i_m_attack_over_hit : public CSkillEffect {
public:
    explicit CSkillEffect_i_m_attack_over_hit(double power) : m_dPower(power) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
};

class CSkillEffect_i_m_attack_range : public CSkillEffect {
public:
    CSkillEffect_i_m_attack_range(int power, int percent, bool forceHit) :
        m_dPower(static_cast<double>(power)), m_dPer(static_cast<double>(percent)), m_ForceHit(forceHit) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
    double m_dPer;
    bool m_ForceHit;
};

class CSkillEffect_i_m_attack_over_hit_range : public CSkillEffect {
public:
    CSkillEffect_i_m_attack_over_hit_range(int power, int percent, bool forceHit) :
        m_dPower(static_cast<double>(power)), m_dPer(static_cast<double>(percent)), m_ForceHit(forceHit) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
    double m_dPer;
    bool m_ForceHit;
};

class CSkillEffect_i_focus_energy : public CSkillEffect {
public:
    CSkillEffect_i_focus_energy() : m_MaxEnergy(0) {}

    explicit CSkillEffect_i_focus_energy(int maxEnergy) : m_MaxEnergy(maxEnergy) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_MaxEnergy;
};

class CSkillEffect_i_focus_max_energy : public CSkillEffect {
public:
    CSkillEffect_i_focus_max_energy() : m_MaxEnergy(0) {}

    explicit CSkillEffect_i_focus_max_energy(int maxEnergy) : m_MaxEnergy(maxEnergy) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_MaxEnergy;
};

class CSkillEffect_i_remove_energy : public CSkillEffect {
public:
    explicit CSkillEffect_i_remove_energy(int energy) : m_nEnergy(energy) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    int m_nEnergy;
};

class CSkillEffect_i_add_hate : public CSkillEffect {
public:
    explicit CSkillEffect_i_add_hate(int amount) : m_nAmount(amount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nAmount;
};

class CSkillEffect_i_delete_hate : public CSkillEffect {
public:
    explicit CSkillEffect_i_delete_hate(int probability) : nProb(probability) { eEffectType = EFT_DELETE_HATE; }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nProb;
};

class CSkillEffect_i_delete_hate_of_me : public CSkillEffect {
public:
    explicit CSkillEffect_i_delete_hate_of_me(int probability) : nProb(probability) { eEffectType = EFT_DELETE_HATE_OF_ME; }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nProb;
};

class CSkillEffect_i_run_away : public CSkillEffect {
public:
    CSkillEffect_i_run_away(int probability, int time) : nProb(probability), nTime(time) { eEffectType = EFT_RUN_AWAY; }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nProb;
    int nTime;
};

class CSkillEffect_i_delete_topagro : public CSkillEffect {
public:
    explicit CSkillEffect_i_delete_topagro(int probability) : nProb(probability) { eEffectType = EFT_DELETE_TOPAGRO; }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nProb;
};

class CSkillEffect_i_consume_body : public CSkillEffect {
public:
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_plunder : public CSkillEffect {
public:
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    void BroadCastToPartyMember(User* user, CParty* party, int itemClassId, int amount);
    void GiveItem(User* user, CParty* party, CNPC* npc);
    bool CheckInven(User* user, CNPC* npc);
    void MakeDropList(User* user, CNPC* npc);
};

class CSkillEffect_i_sweeper : public CSkillEffect {
public:
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    bool IsSpoilRouting(CParty* party);
    void BroadCastToPartyMember(User* user, CParty* party, int itemClassId, int amount);
    void GiveItem(User* user, CParty* party, CNPC* npc);
    bool CheckInven(User* user, CNPC* npc);
};

class CSkillEffect_i_sowing : public CSkillEffect {
public:
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_summon_pet : public CSkillEffect {
public:
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_transfer_hate : public CSkillEffect {
public:
    explicit CSkillEffect_i_transfer_hate(double probability) : m_Prob(probability) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_Prob;
};

class CSkillEffect_i_detect_object : public CSkillEffect {
public:
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_unlock : public CSkillEffect {
public:
    CSkillEffect_i_unlock(bool useItem, int levelOneProbability, int levelTwoProbability, int levelThreeProbability);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    int SuccessProb[7];
    std::vector<std::u16string> m_vDoorNames;
    std::vector<int> m_vProbs;
    bool m_bUseItem;
};

class CSkillEffect_i_spoil : public CSkillEffect {
public:
    CSkillEffect_i_spoil() { eEffectType = EFT_SPOIL; }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

    bool CheckCond(CCreature* creature, CNPC* npc);
    bool SetSpoilUserInfoToNPC(CCreature* creature, CNPC* npc);
    void MakeDropList(User* user, CNPC* npc);
};

class CSkillEffect_i_defuse_trap : public CSkillEffect {
public:
    explicit CSkillEffect_i_defuse_trap(int trapLevel) : m_TrapLevel(trapLevel) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_TrapLevel;
};

class CSkillEffect_i_detect_trap : public CSkillEffect {
public:
    explicit CSkillEffect_i_detect_trap(int trapLevel) : m_TrapLevel(trapLevel) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_TrapLevel;
};

class CSkillEffect_i_align_direction : public CSkillEffect {
public:
    explicit CSkillEffect_i_align_direction(int probability) : nProb(probability) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nProb;
};

class CSkillEffect_i_collecting : public CSkillEffect {
public:
    explicit CSkillEffect_i_collecting(int skillLevel) : m_nSkillLev(skillLevel) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nSkillLev;
};


class CSkillEffect_i_resurrection : public CSkillEffect {
public:
    explicit CSkillEffect_i_resurrection(int restoreRate) : m_nRestoreRate(restoreRate) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nRestoreRate;
};

class CSkillEffect_i_escape : public CSkillEffect {
public:
    CSkillEffect_i_escape() {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    friend class CParserForSkill;

    RestartPoint m_RestartPoint;
};

class CSkillEffect_i_real_damage : public CSkillEffect {
public:
    CSkillEffect_i_real_damage(int power, int criticalProb, int criticalBonus);

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_Power;
    int m_CriticalProb;
    int m_CriticalBonus;
};


class CSkillEffect_i_rebalance_hp : public CSkillEffect {
public:
    CSkillEffect_i_rebalance_hp() {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_rebalance_hp_summon : public CSkillEffect {
public:
    CSkillEffect_i_rebalance_hp_summon() {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_death_link : public CSkillEffect {
public:
    explicit CSkillEffect_i_death_link(double power) : m_dPower(power) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
};

class CSkillEffect_i_death_link_mp : public CSkillEffect {
public:
    explicit CSkillEffect_i_death_link_mp(double power) : m_dPower(power) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPower;
};

class CSkillEffect_i_soul_shot : public CSkillEffect {
public:
    explicit CSkillEffect_i_soul_shot(double bonus) : m_dBonus(bonus) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dBonus;
};

class CSkillEffect_i_summon_soul_shot : public CSkillEffect {
public:
    explicit CSkillEffect_i_summon_soul_shot(double bonus) : m_dBonus(bonus) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dBonus;
};

class CSkillEffect_i_summon_spirit_shot : public CSkillEffect {
public:
    CSkillEffect_i_summon_spirit_shot(double powerBonus, double speedBonus, double multiBonus) :
        m_dPowerBonus(powerBonus), m_dSpeedBonus(speedBonus), m_dMultiBonus(multiBonus) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPowerBonus;
    double m_dSpeedBonus;
    double m_dMultiBonus;
};


class CSkillEffect_i_spirit_shot : public CSkillEffect {
public:
    CSkillEffect_i_spirit_shot(double powerBonus, double speedBonus, double multiBonus) :
        m_dPowerBonus(powerBonus), m_dSpeedBonus(speedBonus), m_dMultiBonus(multiBonus) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double m_dPowerBonus;
    double m_dSpeedBonus;
    double m_dMultiBonus;
};

class CSkillEffect_i_give_contribution : public CSkillEffect {
public:
    explicit CSkillEffect_i_give_contribution(int bonus) : m_nBonus(bonus) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nBonus;
};

class CSkillEffect_i_randomize_hate : public CSkillEffect {
public:
    explicit CSkillEffect_i_randomize_hate(int probability) : m_nProbability(probability) {
        eEffectType = EFT_RANDOMIZE_HATE;
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nProbability;
};

class CSkillEffect_i_add_hate_mymaster : public CSkillEffect {
public:
    explicit CSkillEffect_i_add_hate_mymaster(int amount) : m_nAmount(amount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nAmount;
};

class CSkillEffect_i_summon_cubic : public CSkillEffect {
public:
    CSkillEffect_i_summon_cubic(int cubicId, int cubicLevel) : m_cubicID(cubicId), m_cubicLevel(cubicLevel) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_cubicID;
    int m_cubicLevel;
};


class CSkillEffect_i_get_agro : public CSkillEffect {
public:
    CSkillEffect_i_get_agro() = default;

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_change_vital_item_count : public CSkillEffect {
public:
    explicit CSkillEffect_i_change_vital_item_count(int count) : nCount(count) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nCount;
};

class CSkillEffect_i_acc_change : public CSkillEffect {
public:
    CSkillEffect_i_acc_change() = default;

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};


class CSkillEffect_i_m_attack_by_range : public CSkillEffect {
public:
    enum AttackRangePosition : std::int32_t {
        ARP_NEAR = 0,
        ARP_FAR = 1,
    };

    CSkillEffect_i_m_attack_by_range(double power, AttackRangePosition position) : m_Power(power), m_Position(position) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    [[maybe_unused]] double m_Power;
    [[maybe_unused]] AttackRangePosition m_Position;
};

class CSkillEffect_i_fishing_cast : public CSkillEffect {
public:
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_fishing_pumping : public CSkillEffect {
public:
    explicit CSkillEffect_i_fishing_pumping(double bonus) : m_Bonus(bonus) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    [[maybe_unused]] double m_Bonus;
};

class CSkillEffect_i_fishing_reeling : public CSkillEffect {
public:
    explicit CSkillEffect_i_fishing_reeling(double bonus) : dBonus(bonus) {
        // Current L2Server 0x00ca24b0.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    [[maybe_unused]] double dBonus;
};

class CSkillEffect_i_fishing_shot : public CSkillEffect {
public:
    explicit CSkillEffect_i_fishing_shot(double bonus) : dBonus(bonus) {
        // Current L2Server 0x00ca24d8.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    double dBonus;
};


class CSkillEffect_i_get_ultimate_skill_gauge : public CSkillEffect {
public:
    explicit CSkillEffect_i_get_ultimate_skill_gauge(int point) : m_Point(point) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_Point;
};

class CSkillEffect_i_pledge_send_system_message : public CSkillEffect {
public:
    explicit CSkillEffect_i_pledge_send_system_message(int messageId) : nMsgNum(messageId) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nMsgNum;
};

class CSkillEffect_i_compulsion_social_action : public CSkillEffect {
public:
    explicit CSkillEffect_i_compulsion_social_action(int actionId) : m_ActionId(actionId) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_ActionId;
};

class CSkillEffect_i_holything_possess : public CSkillEffect {
public:
    CSkillEffect_i_holything_possess() : m_nIsGood(0) {}
    explicit CSkillEffect_i_holything_possess(int isGood) : m_nIsGood(isGood) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nIsGood;
};

class CSkillEffect_i_bookmark_add_slot : public CSkillEffect {
public:
    explicit CSkillEffect_i_bookmark_add_slot(int slotNum) : m_slotNum(slotNum) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_slotNum;
};

class CSkillEffect_i_summon_agathion : public CSkillEffect {
public:
    CSkillEffect_i_summon_agathion(int agathionClassId, int level) : m_agathionClassID(agathionClassId), m_level(level) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_agathionClassID;
    int m_level;
};

class CSkillEffect_i_unsummon_agathion : public CSkillEffect {
public:
    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;
};

class CSkillEffect_i_bonuscount_up : public CSkillEffect {
public:
    explicit CSkillEffect_i_bonuscount_up(int bonusCount) : m_BonusCount(bonusCount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_BonusCount;
};

class CSkillEffect_i_ev_ink : public CSkillEffect {
public:
    explicit CSkillEffect_i_ev_ink(int skillInkEnergy) : nSkillInkEnergy(skillInkEnergy) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nSkillInkEnergy;
};

#define L2_DECLARE_SOUL_INSTANT_EFFECT(class_name)                                                                          \
    class class_name : public CSkillEffect {                                                                                \
    public:                                                                                                                 \
        explicit class_name(int count) : m_NumSoul(count) {}                                                                \
                                                                                                                            \
        bool Instant(                                                                                                       \
            CCreature* creature,                                                                                            \
            CWorldObject* target,                                                                                           \
            const CSkillInfo* skillInfo,                                                                                    \
            CSkillAction2* skillAction,                                                                                     \
            double distance                                                                                                 \
        ) override;                                                                                                         \
                                                                                                                            \
    private:                                                                                                                \
        int m_NumSoul;                                                                                                      \
    };

L2_DECLARE_SOUL_INSTANT_EFFECT(CSkillEffect_i_focus_soul);
L2_DECLARE_SOUL_INSTANT_EFFECT(CSkillEffect_i_remove_soul);

#undef L2_DECLARE_SOUL_INSTANT_EFFECT

class CSkillEffect_i_transfer_abnormal : public CSkillEffect {
public:
    CSkillEffect_i_transfer_abnormal(AbnormalStatusSlotType abnormalType, int maximumSlots) :
        m_nAbnormalType(abnormalType), m_MaxSlot(maximumSlots) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    AbnormalStatusSlotType m_nAbnormalType;
    int m_MaxSlot;
};


class CSkillEffect_i_set_return_pos : public CSkillEffect {
public:
    CSkillEffect_i_set_return_pos(int skillId, int duration) : nSkillID(skillId), nDuration(duration) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nSkillID;
    int nDuration;
};


class CSkillEffect_i_pcbang_point_up : public CSkillEffect {
public:
    explicit CSkillEffect_i_pcbang_point_up(int amount) : nAmount(amount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int nAmount;
};


class CSkillEffect_i_steal_abnormal : public CSkillEffect {
public:
    enum SlotGroup : std::int32_t {
        SG_BUFF = 0,
        SG_DEBUFF = 1,
        SG_ALL = 2,
    };

    CSkillEffect_i_steal_abnormal(SlotGroup slotGroup, int maximumSlots, int probability) :
        m_SlotGroup(slotGroup), m_MaxSlot(maximumSlots), m_Prob(probability) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    const SlotGroup m_SlotGroup;
    const int m_MaxSlot;
    const int m_Prob;
};


class CSkillEffect_i_refuel_airship : public CSkillEffect {
public:
    explicit CSkillEffect_i_refuel_airship(int amount) : m_Amount(amount) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_Amount;
};


#define L2_DECLARE_SIMPLE_INSTANT_EFFECT(class_name)                                                                        \
    class class_name : public CSkillEffect {                                                                                \
    public:                                                                                                                 \
        bool Instant(                                                                                                       \
            CCreature* creature,                                                                                            \
            CWorldObject* target,                                                                                           \
            const CSkillInfo* skillInfo,                                                                                    \
            CSkillAction2* skillAction,                                                                                     \
            double distance                                                                                                 \
        ) override;                                                                                                         \
    };

L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_set_visible);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_force_sitdown);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_block_act);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_my_summon_kill);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_suicide);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_install_camp);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_install_advance_base);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_uninstall_advance_base);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_remote_control_trap);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_open_dwarf_recipebook);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_open_common_recipebook);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_dispel_all);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_capture_flag_start);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_capture_flag);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_capture_ownthing);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_capture_ownthing_start);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_summon_target_to_me);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_harvesting);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_rebalance_mp);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_call_target_party);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_install_camp_ex);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_Locked_Item);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_UnLocked_Item);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_change_attribute);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_event_agathion_reuse_delay);
L2_DECLARE_SIMPLE_INSTANT_EFFECT(CSkillEffect_i_consume_summon);

#undef L2_DECLARE_SIMPLE_INSTANT_EFFECT


class CSkillEffect_p_physical_attack : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_physical_attack(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_physical_attack_shield : public CSkillEffectAddBonusByShield {
public:
    CSkillEffect_p_physical_attack_shield(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_physical_attack_by_defence : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_physical_attack_by_defence(int ignoreDefence, double bonus);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    int m_IgnoreDefence;
};

class CSkillEffect_p_physical_attack_by_material : public CSkillEffect {
public:
    CSkillEffect_p_physical_attack_by_material(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    friend class CParserForSkill;
    friend class CParserForOption;
    MaterialType m_MaterialType;
    double m_Bonus;
    SkillFxChangeType m_ChangeType;
};

class CSkillEffect_p_physical_defence : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_physical_defence(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_physical_defence_pvp : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_physical_defence_pvp(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_physical_defence_by_material : public CSkillEffect {
public:
    CSkillEffect_p_physical_defence_by_material(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    friend class CParserForSkill;
    friend class CParserForOption;
    MaterialType m_MaterialType;
    double m_Bonus;
    SkillFxChangeType m_ChangeType;
};

class CSkillEffect_p_pve_physical_defence : public CSkillEffect {
public:
    CSkillEffect_p_pve_physical_defence(double amount, SkillFxChangeType changeType, std::vector<std::int64_t>* npcIds);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    std::vector<int> m_vNpcID;
    double m_dAmount;
    SkillFxChangeType m_eSkillFxChangeType;
};

class CSkillEffect_p_pve_magical_defence : public CSkillEffect {
public:
    CSkillEffect_p_pve_magical_defence(double amount, SkillFxChangeType changeType, std::vector<std::int64_t>* npcIds);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    std::vector<int> m_vNpcID;
    double m_dAmount;
    SkillFxChangeType m_eSkillFxChangeType;
};

class CSkillEffect_p_pve_defence_attribute : public CSkillEffect {
public:
    CSkillEffect_p_pve_defence_attribute(EnemyType enemyType, AttributeTypeEnum attributeType, double difference);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    EnemyType m_eEnemyType;
    AttributeTypeEnum m_Attribute;
    double m_dDiff;
};

class CSkillEffect_p_transmute_attr_attack : public CSkillEffect {
public:
    explicit CSkillEffect_p_transmute_attr_attack(AttributeTypeEnum attributeType);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    AttributeTypeEnum m_eAttrAttack;
};

class CSkillEffect_p_transmute_attr_defence : public CSkillEffect {
public:
    CSkillEffect_p_transmute_attr_defence(AttributeTypeEnum originalAttribute, AttributeTypeEnum transmutedAttribute);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    AttributeTypeEnum m_eOriginalAttribute;
    AttributeTypeEnum m_eTransmutedAttribute;
};

class CSkillEffect_p_magical_defence : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_magical_defence(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_speed : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_speed(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_speed_pvp : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_speed_pvp(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_attack_speed : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_attack_speed(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_magic_speed : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_magic_speed(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magic_speed_by_magic_type : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_magic_speed_by_magic_type(double bonus, SkillFxChangeType changeType, std::set<int> magicTypes);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    std::set<int> m_sMagicType;
};

class CSkillEffect_p_attack_speed_by_weapon : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_attack_speed_by_weapon(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_attack_range : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_attack_range(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_attack_speed_shield : public CSkillEffectAddBonusByShield {
public:
    CSkillEffect_p_attack_speed_shield(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magic_speed_by_weapon : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_magic_speed_by_weapon(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magical_attack : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_magical_attack(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magical_attack_add : public CSkillEffectAddBonusByAdditionalArmorType {
public:
    CSkillEffect_p_magical_attack_add(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magical_attack_by_weapon : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_magical_attack_by_weapon(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magical_defence_add : public CSkillEffectAddBonusByAdditionalArmorType {
public:
    CSkillEffect_p_magical_defence_add(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_hp_regen : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_hp_regen(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_mp_regen : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_mp_regen(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_mp_regen_add : public CSkillEffectAddBonusByAdditionalArmorType {
public:
    CSkillEffect_p_mp_regen_add(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_avoid : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_avoid(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_defence_critical_rate : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_defence_critical_rate(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_defence_critical_damage : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_defence_critical_damage(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_hit : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_hit(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_physical_armor_hit : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_physical_armor_hit(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_hit_number : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_hit_number(int bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_critical_damage : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_critical_damage(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_critical_damage_shield : public CSkillEffectAddBonusByShield {
public:
    CSkillEffect_p_critical_damage_shield(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_critical_rate : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_critical_rate(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_critical_rate_shield : public CSkillEffectAddBonusByShield {
public:
    CSkillEffect_p_critical_rate_shield(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magic_critical_rate : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_magic_critical_rate(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magic_critical_dmg : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_magic_critical_dmg(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magic_defence_critical_rate : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_magic_defence_critical_rate(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magic_defence_critical_dmg : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_magic_defence_critical_dmg(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magic_defence_critical_rate_add : public CSkillEffectAddBonusByAdditionalArmorType {
public:
    CSkillEffect_p_magic_defence_critical_rate_add(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magic_defence_critical_dmg_add : public CSkillEffectAddBonusByAdditionalArmorType {
public:
    CSkillEffect_p_magic_defence_critical_dmg_add(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_magic_hit : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_magic_hit(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_physical_critical_rate_maxup : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_physical_critical_rate_maxup(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    double m_dBonus;
};

class CSkillEffect_p_magical_critical_rate_maxup : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_magical_critical_rate_maxup(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    double m_dBonus;
};


class CSkillEffect_p_limit : public CSkillEffect {
public:
    enum RegenLimit : std::int32_t {
        HP = 0,
        MP = 1,
        CP = 2,
    };

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


protected:
    CSkillEffect_p_limit(RegenLimit regenLimit, int limit, SkillFxChangeType changeType) :
        m_RegenLimit(regenLimit), m_Limit(limit), m_Sfct(changeType) {}

private:
    RegenLimit m_RegenLimit;
    int m_Limit;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_limit_hp : public CSkillEffect_p_limit {
public:
    CSkillEffect_p_limit_hp(int limit, SkillFxChangeType changeType);
};

class CSkillEffect_p_limit_mp : public CSkillEffect_p_limit {
public:
    CSkillEffect_p_limit_mp(int limit, SkillFxChangeType changeType);
};

class CSkillEffect_p_limit_cp : public CSkillEffect_p_limit {
public:
    CSkillEffect_p_limit_cp(int limit, SkillFxChangeType changeType);
};

class CSkillEffect_p_get_damage_limit : public CSkillEffect {
public:
    explicit CSkillEffect_p_get_damage_limit(double damageLimit);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    double m_DamageLimit;
};


class CSkillEffect_p_block_getdamage_distance : public CSkillEffect {
public:
    CSkillEffect_p_block_getdamage_distance(double distance, wchar_t* blockType);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    double m_dDistance;
    bool m_bIsBlockHP;
};

class CSkillEffect_p_hate_attack : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_hate_attack(double bonus, SkillFxChangeType changeType);
};

class CSkillEffect_p_ignore_skill : public CSkillEffect {
public:
    explicit CSkillEffect_p_ignore_skill(std::vector<std::int64_t>* skillIds);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    std::vector<int> m_vIgnoreSkillId;
};

class CSkillEffect_p_passive_bonus_skill : public CSkillEffect {
public:
    CSkillEffect_p_passive_bonus_skill(int requireSkillId, int requireSkillLevel, int acquireSkillId, int acquireSkillLevel);

    void OnPostLoad() override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    [[maybe_unused]] bool m_isSkillAcquired;
    CSkillKey require;
    CSkillKey acquire;
};

class CSkillEffect_p_play_music : public CSkillEffect {
public:
    explicit CSkillEffect_p_play_music(int musicNumber);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

    int musicNumber;
};

class CSkillEffect_p_resist_abnormal_by_category : public CSkillEffect {
public:
    CSkillEffect_p_resist_abnormal_by_category(int bonus, SkillFxChangeType changeType) :
        eDeBuffType(DB_MAX), nBonus(bonus), sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

    DebuffType eDeBuffType;
    int nBonus;
    SkillFxChangeType sfct;
};

class CSkillEffect_p_resist_dispel_by_category : public CSkillEffect {
public:
    CSkillEffect_p_resist_dispel_by_category(int bonus, SkillFxChangeType changeType) :
        eDeBuffType(DB_MAX), nBonus(bonus), sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

    DebuffType eDeBuffType;
    const int nBonus;
    const SkillFxChangeType sfct;
};

class CSkillEffect_p_resurrection_special : public CSkillEffect {
public:
    CSkillEffect_p_resurrection_special(
        int hpProbability, int mpProbability, int cpProbability, const char16_t* resurrectionSkillName
    );
    CSkillEffect_p_resurrection_special(
        int hpProbability,
        int mpProbability,
        int cpProbability,
        const char16_t* resurrectionSkillName,
        std::vector<std::int64_t>* inZoneIds
    );

    void OnPostLoad() override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

    int nHPProb;
    int nMPProb;
    int nCPProb;
    char16_t sResurrectionSkillName[256];
    std::vector<int> nInzoneId;
    CSkillInfo* m_pSkillInfo;
};


class CSkillEffect_p_heal_effect : public CSkillEffectByArmorType {
public:
    CSkillEffect_p_heal_effect(double bonus, SkillFxChangeType changeType) : dBonus(bonus), sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double dBonus;
    SkillFxChangeType sfct;
};

class CSkillEffect_p_max_hp : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_max_hp(double bonus, SkillFxChangeType changeType, bool charge);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;

private:
    bool m_bCharge;
};

class CSkillEffect_p_max_mp : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_max_mp(double bonus, SkillFxChangeType changeType, bool charge);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;

private:
    bool m_bCharge;
};

class CSkillEffect_p_max_mp_add : public CSkillEffectAddBonusByAdditionalArmorType {
public:
    CSkillEffect_p_max_mp_add(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_max_cp : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_max_cp(double bonus, SkillFxChangeType changeType, bool charge);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;

private:
    bool m_bCharge;
};


class CSkillEffect_p_condition_block_act_skill : public CSkillEffect {
public:
    explicit CSkillEffect_p_condition_block_act_skill(std::vector<std::int64_t>* skillIds);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    std::vector<int> m_vCanCastSkillId;
};

class CSkillEffect_p_condition_block_act_item : public CSkillEffect {
public:
    explicit CSkillEffect_p_condition_block_act_item(std::vector<std::int64_t>* itemClassIds);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    std::vector<int> m_vCanUseItemClass;
};

class CSkillEffect_p_block_use_skill : public CSkillEffect {
public:
    explicit CSkillEffect_p_block_use_skill(std::vector<std::int64_t>* skillIds);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    std::vector<int> m_vBlockSkillId;
};


class CSkillEffect_p_disarm : public CSkillEffect {
public:
    explicit CSkillEffect_p_disarm(SlotType disarmType) : m_eDisarmType(disarmType) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    SlotType m_eDisarmType;
};


class CSkillEffect_p_transform : public CSkillEffect {
public:
    explicit CSkillEffect_p_transform(int transformId) : m_transformID(transformId) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_transformID;
};


class CSkillEffect_p_changebody : public CSkillEffect {
public:
    explicit CSkillEffect_p_changebody(std::vector<ChangeBodyStruct> changeBodies) : m_vChangeBody(changeBodies) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    std::vector<ChangeBodyStruct> m_vChangeBody;
};


class CSkillEffect_p_attack_attribute : public CSkillEffect {
public:
    CSkillEffect_p_attack_attribute(AttributeTypeEnum attributeType, double bonus);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    BaseAttribute m_Attribute;
};

class CSkillEffect_p_attack_attribute_add : public CSkillEffect {
public:
    explicit CSkillEffect_p_attack_attribute_add(int bonus);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_nBonus;
};

class CSkillEffect_p_cp_regen : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_cp_regen(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_exp_modify : public CSkillEffect {
public:
    explicit CSkillEffect_p_exp_modify(double expModify);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    double expModify;
};

class CSkillEffect_p_sp_modify : public CSkillEffect {
public:
    explicit CSkillEffect_p_sp_modify(double spModify);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    double spModify;
};

class CSkillEffect_p_trade : public CSkillEffect {
public:
    CSkillEffect_p_trade(int bonus0, int bonus1, int bonus2, int bonus3, int bonus4, int bonus5);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


    double dBonus[12];
};

class CSkillEffect_p_crystal_grade_modify : public CSkillEffect {
public:
    explicit CSkillEffect_p_crystal_grade_modify(double bonus);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


    double crystalGradeModify;
};

class CSkillEffect_p_exp_sp_fishing : public CSkillEffect {
public:
    explicit CSkillEffect_p_exp_sp_fishing(double valuePercent);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


    double m_valuePercent;
};

class CSkillEffect_p_get_item_by_exp : public CSkillEffect {
public:
    CSkillEffect_p_get_item_by_exp(std::int64_t expUnit, int itemType, std::int64_t itemAmount);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    std::int64_t m_nExpUnit;
    int m_nItemType;
    std::int64_t m_nItemAmount;
};

class CSkillEffect_p_lucky_enchant_rate : public CSkillEffect {
public:
    CSkillEffect_p_lucky_enchant_rate(
        int increaseRate, int maxIncreaseRate, int enchantRateOption, SkillFxChangeType changeType
    );

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    int m_nWeaponEnchantIncreaseRate;
    int m_nArmorEnchantIncreaseRate;
    int m_nMaxEnchantIncreaseRate;
    int m_nEnchantRateOption;
    SkillFxChangeType m_sfct;
};

class CSkillEffect_p_raid_point_modify : public CSkillEffect {
public:
    explicit CSkillEffect_p_raid_point_modify(double pointModifier);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


    double pointModPer;
};

class CSkillEffect_p_reduce_drop_penalty : public CSkillEffect {
public:
    CSkillEffect_p_reduce_drop_penalty(EnemyType enemyType, double itemDropBonus, double expDropBonus);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


    EnemyType ent;
    double dItemDropBonus;
    double dExpDropBonus;
};


class CSkillEffect_p_avoid_by_move_mode : public CSkillEffect {
public:
    CSkillEffect_p_avoid_by_move_mode(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    friend class CParserForSkill;
    friend class CParserForOption;

    double dBonus;
    SkillFxChangeType sfct;
    MoveModeEnum eMoveModeType;
};

class CSkillEffect_p_hp_regen_by_move_mode : public CSkillEffect {
public:
    CSkillEffect_p_hp_regen_by_move_mode(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    friend class CParserForSkill;
    friend class CParserForOption;

    double dBonus;
    SkillFxChangeType sfct;
    MoveModeEnum eMoveModeType;
};

class CSkillEffect_p_mp_regen_by_move_mode : public CSkillEffect {
public:
    CSkillEffect_p_mp_regen_by_move_mode(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    friend class CParserForSkill;
    friend class CParserForOption;

    double dBonus;
    SkillFxChangeType sfct;
    MoveModeEnum eMoveModeType;
};

class CSkillEffect_p_cp_regen_by_move_mode : public CSkillEffect {
public:
    CSkillEffect_p_cp_regen_by_move_mode(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    friend class CParserForSkill;
    friend class CParserForOption;

    MoveModeEnum eMoveModeType;
    double dBonus;
    SkillFxChangeType sfct;
};

class CSkillEffect_p_hit_at_night : public CSkillEffect {
public:
    CSkillEffect_p_hit_at_night(double bonus, SkillFxChangeType changeType);

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double dBonus;
    SkillFxChangeType sfct;
    int nMsg;
};

class CSkillEffect_p_critical_rate_by_hp1 : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_critical_rate_by_hp1(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_critical_rate_by_hp2 : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_critical_rate_by_hp2(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_physical_attack_by_hp1 : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_physical_attack_by_hp1(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_physical_attack_by_hp2 : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_physical_attack_by_hp2(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_physical_defence_by_hp1 : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_physical_defence_by_hp1(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_physical_defence_by_hp2 : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_physical_defence_by_hp2(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_avoid_rate_by_hp1 : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_avoid_rate_by_hp1(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_avoid_rate_by_hp2 : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_avoid_rate_by_hp2(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_attack_speed_by_hp1 : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_attack_speed_by_hp1(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_attack_speed_by_hp2 : public CSkillEffectAddBonusByWeaponType {
public:
    CSkillEffect_p_attack_speed_by_hp2(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};


class CSkillEffect_p_defence_attribute : public CSkillEffect {
public:
    CSkillEffect_p_defence_attribute(AttributeTypeEnum attributeType, double bonus);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    BaseAttribute m_Attribute;
};


class CSkillEffect_p_attack_trait : public CSkillEffectByWeaponType {
public:
    CSkillEffect_p_attack_trait(TraitTypeEnum traitType, double bonus);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    TraitTypeEnum m_TraitType;
    double m_Bonus;
};

class CSkillEffect_p_defence_trait : public CSkillEffect {
public:
    CSkillEffect_p_defence_trait(TraitTypeEnum traitType, double bonus);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    TraitTypeEnum m_TraitType;
    double m_Bonus;
    bool m_PerfectDefence;
};


class CSkillEffect_p_weight_limit : public CSkillEffect {
public:
    CSkillEffect_p_weight_limit(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_cub_physical_attack : public CSkillEffect {
public:
    CSkillEffect_cub_physical_attack(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_cub_physical_defence : public CSkillEffect {
public:
    CSkillEffect_cub_physical_defence(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_cub_attack_speed : public CSkillEffect {
public:
    CSkillEffect_cub_attack_speed(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_reflect_skill : public CSkillEffect {
public:
    CSkillEffect_p_reflect_skill(
        double probability0,
        double probability1,
        double probability2,
        double probability3,
        double probability4,
        double probability5
    ) :
        m_Probability{probability0, probability1, probability2, probability3, probability4, probability5} {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_Probability[6];
};

class CSkillEffect_cub_block_act : public CSkillEffect {
public:
    CSkillEffect_cub_block_act() = default;

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};


class CSkillEffect_p_weight_penalty : public CSkillEffect {
public:
    CSkillEffect_p_weight_penalty(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_vital_add : public CSkillEffect {
public:
    explicit CSkillEffect_p_vital_add(double vitalityBonus) : m_VitalityBonus(vitalityBonus) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    double m_VitalityBonus;
};

class CSkillEffect_p_vital_sub : public CSkillEffect {
public:
    explicit CSkillEffect_p_vital_sub(int vitalitySub) : m_nVitalitySub(vitalitySub) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_nVitalitySub;
};

class CSkillEffect_p_exp_add : public CSkillEffect {
public:
    CSkillEffect_p_exp_add(std::int32_t expBonus, SkillFxChangeType changeType) : m_ExpBonus(expBonus), m_Sfct(changeType) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    std::int32_t m_ExpBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_stat_up : public CSkillEffect {
public:
    CSkillEffect_p_stat_up(int type, int bonus) : nType(type), nBonus(bonus) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

    const int nType;
    const int nBonus;
};

class CSkillEffect_p_stat_up_limited : public CSkillEffect {
public:
    CSkillEffect_p_stat_up_limited(int type, int bonus) : nType(type), nBonus(bonus) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

    const int nType;
    const int nBonus;
};

class CSkillEffect_p_statbonus_skillcritical : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_p_statbonus_skillcritical(double amount, ParameterType parameterType) :
        CSkillEffectAddBonusByArmorType(amount, SFCT_DIFF, CCreatureSkillMod::PhysicalSkillCriticalProbByStat),
        m_eParamType(parameterType) {
        // Current L2Server 0x00ca28a8.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    ParameterType m_eParamType;
};


class CSKillEffect_p_statbonus_speed : public CSkillEffectAddBonusByArmorType {
public:
    CSKillEffect_p_statbonus_speed(double amount, ParameterType parameterType) :
        CSkillEffectAddBonusByArmorType(amount, SFCT_DIFF, CCreatureSkillMod::Speed), m_eParamType(parameterType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    ParameterType m_eParamType;
};


class CSkillEffect_p_instant_kill_resist : public CSkillEffect {
public:
    explicit CSkillEffect_p_instant_kill_resist(double resist) : m_dResist(resist) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    double m_dResist;
};

class CSkillEffect_p_hp_special_add : public CSkillEffect {
public:
    explicit CSkillEffect_p_hp_special_add(double amount) : m_dAmount(amount) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    double m_dAmount;
};

class CSkillEffect_p_reflect_dd_resist : public CSkillEffect {
public:
    explicit CSkillEffect_p_reflect_dd_resist(double resistRatio) : m_resisteRatio(resistRatio) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    double m_resisteRatio;
};

class CSkillEffect_p_damage_shield_resist : public CSkillEffect {
public:
    explicit CSkillEffect_p_damage_shield_resist(int resist) : m_nResist(resist) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_nResist;
};

class CSkillEffect_p_damage_shield_resist_castle_siege : public CSkillEffect {
public:
    explicit CSkillEffect_p_damage_shield_resist_castle_siege(int resist) : m_nResist(resist) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_nResist;
};

class CSkillEffect_p_add_vital_bonus : public CSkillEffect {
public:
    explicit CSkillEffect_p_add_vital_bonus(int bonusPercent) : m_nBonusPer(bonusPercent) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    const int m_nBonusPer;
};

class CSkillEffect_p_vital_rate : public CSkillEffect {
public:
    explicit CSkillEffect_p_vital_rate(int bonusPercent) : m_nBonusPer(bonusPercent) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    const int m_nBonusPer;
};

class CSkillEffect_p_inc_spoil_rate : public CSkillEffect {
public:
    explicit CSkillEffect_p_inc_spoil_rate(int increasePercent) : m_nIncPer(increasePercent) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    const int m_nIncPer;
};

class CSkillEffect_p_anti_cubic : public CSkillEffect {
public:
    CSkillEffect_p_anti_cubic() = default;

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};

class CSkillEffect_p_block_chat : public CSkillEffect {
public:
    CSkillEffect_p_block_chat() = default;

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};

class CSkillEffect_p_ignore_death : public CSkillEffect {
public:
    CSkillEffect_p_ignore_death() = default;

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};

class CSkillEffect_p_weak_death_penalty : public CSkillEffect {
public:
    CSkillEffect_p_weak_death_penalty() = default;

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};

class CSkillEffect_p_stop_consume_vital_point : public CSkillEffect {
public:
    explicit CSkillEffect_p_stop_consume_vital_point(VitalityRestore::Type type) : m_nType(type) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    VitalityRestore::Type m_nType;
};

class CSkillEffect_p_stop_vitality_effect : public CSkillEffect {
public:
    explicit CSkillEffect_p_stop_vitality_effect(VitalityRestore::Type type) : m_nType(type) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    VitalityRestore::Type m_nType;
};

class CSkillEffect_p_recharge_vital_point : public CSkillEffect {
public:
    explicit CSkillEffect_p_recharge_vital_point(VitalityRestore::Type type) : m_nType(type) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    VitalityRestore::Type m_nType;
};

class CSkillEffect_p_vital_recharge_boost : public CSkillEffect {
public:
    explicit CSkillEffect_p_vital_recharge_boost(int boostPercent) : m_nBoostPer(boostPercent) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    const int m_nBoostPer;
};

class CSkillEffect_p_reduce_vital_consume_point : public CSkillEffect {
public:
    explicit CSkillEffect_p_reduce_vital_consume_point(int reducePercent) : m_nReducePer(reducePercent) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    const int m_nReducePer;
};

class CSkillEffect_p_protect_death_penalty : public CSkillEffect {
public:
    CSkillEffect_p_protect_death_penalty() = default;

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};

class CSkillEffect_p_defence_fatal_blow_rate : public CSkillEffect {
public:
    CSkillEffect_p_defence_fatal_blow_rate(double bonus, SkillFxChangeType changeType) : dBonus(bonus), sfct(changeType) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

    double dBonus;
    SkillFxChangeType sfct;
};

class CSkillEffect_p_enlarge_abnormal_slot : public CSkillEffect {
public:
    explicit CSkillEffect_p_enlarge_abnormal_slot(int buffSlotBonus) : m_nBuffSlotBonus(buffSlotBonus) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_nBuffSlotBonus;
};

class CSkillEffect_p_enlarge_storage : public CSkillEffect {
public:
    CSkillEffect_p_enlarge_storage(SkillEnlargeStorageType storageType, int bonus) : sesType(storageType), nBonus(bonus) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    const SkillEnlargeStorageType sesType;
    const int nBonus;
};

class CSkillEffect_p_reflect_dd : public CSkillEffect {
public:
    explicit CSkillEffect_p_reflect_dd(double reflectRatio) : m_refectRatio(reflectRatio) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    double m_refectRatio;
};

class CSkillEffect_p_reuse_delay : public CSkillEffect {
public:
    CSkillEffect_p_reuse_delay(int isMagic, double bonus, SkillFxChangeType changeType) :
        dBonus(bonus), sfct(changeType), nIsMagic(isMagic) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

    double dBonus;
    SkillFxChangeType sfct;
    int nIsMagic;
};

class CSkillEffect_p_magic_mp_cost : public CSkillEffect {
public:
    CSkillEffect_p_magic_mp_cost(int isMagic, double bonus, SkillFxChangeType changeType) :
        dBonus(bonus), sfct(changeType), nIsMagic(isMagic) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

    double dBonus;
    SkillFxChangeType sfct;
    int nIsMagic;
};


class CSkillEffect_p_after_mp_cost : public CSkillEffect {
public:
    CSkillEffect_p_after_mp_cost(int afterEffectType, double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
    int m_nAfterEffectType;
};


class CSkillEffect_p_block_getdamage : public CSkillEffect {
public:
    explicit CSkillEffect_p_block_getdamage(wchar_t* blockType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    bool bIsBlockHP;
};


class CSkillEffect_p_block_getdamage_except : public CSkillEffect {
public:
    CSkillEffect_p_block_getdamage_except() = default;

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};

class CSkillEffect_p_block_getdamage_except_each_other : public CSkillEffect {
public:
    CSkillEffect_p_block_getdamage_except_each_other() = default;

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};

class CSkillEffect_p_block_getdamage_target_only : public CSkillEffect {
public:
    CSkillEffect_p_block_getdamage_target_only() = default;

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};

class CSkillEffect_p_disappear_target : public CSkillEffect {
public:
    CSkillEffect_p_disappear_target() = default;

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};


class CSkillEffect_p_resist_dd_magic : public CSkillEffect {
public:
    CSkillEffect_p_resist_dd_magic(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_reduce_cancel : public CSkillEffect {
public:
    CSkillEffect_p_reduce_cancel(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_safe_fall_height : public CSkillEffect {
public:
    CSkillEffect_p_safe_fall_height(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_breath : public CSkillEffect {
public:
    CSkillEffect_p_breath(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_area_damage : public CSkillEffect {
public:
    CSkillEffect_p_area_damage(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_fatal_blow_rate : public CSkillEffect {
public:
    CSkillEffect_p_fatal_blow_rate(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_physical_shield_defence : public CSkillEffect {
public:
    CSkillEffect_p_physical_shield_defence(double bonus, SkillFxChangeType changeType) :
        m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_critical_rate_position_bonus : public CSkillEffect {
public:
    CSkillEffect_p_critical_rate_position_bonus(SkillFxPositionType position, double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    SkillFxPositionType m_Position;
    double m_Bonus;
    SkillFxChangeType m_ChangeType;
};


class CSkillEffect_p_direction_damage : public CSkillEffect {
public:
    CSkillEffect_p_direction_damage(SkillFxPositionType position, double bonus, SkillFxChangeType changeType) :
        m_Position(position), m_Bonus(bonus), m_ChangeType(changeType) {
        // Current L2Server 0x00ca1ecc.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    SkillFxPositionType m_Position;
    double m_Bonus;
    SkillFxChangeType m_ChangeType;
};


class CSkillEffect_p_damage_by_attack : public CSkillEffect {
public:
    CSkillEffect_p_damage_by_attack(EnemyType enemyType, double amount, SkillFxChangeType changeType) :
        m_EnemyType(enemyType), m_Amount(amount), m_ChangeType(changeType) {
        // Current L2Server 0x00ca1fb8.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    EnemyType m_EnemyType;
    double m_Amount;
    SkillFxChangeType m_ChangeType;
};


class CSkillEffect_p_position_damage : public CSkillEffect {
public:
    CSkillEffect_p_position_damage(SkillFxPositionType position, double bonus, SkillFxChangeType changeType) :
        m_Position(position), m_Bonus(bonus), m_ChangeType(changeType) {
        // Current L2Server 0x00ca1fe4.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    SkillFxPositionType m_Position;
    double m_Bonus;
    SkillFxChangeType m_ChangeType;
};


class CSkillEffect_p_physical_skill_critical_rate : public CSkillEffect {
public:
    CSkillEffect_p_physical_skill_critical_rate(double bonus, SkillFxChangeType changeType) :
        m_ChangeType(changeType), m_Bonus(bonus) {
        // Current L2Server 0x00ca2068.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    SkillFxChangeType m_ChangeType;
    double m_Bonus;
};


class CSkillEffect_p_physical_skill_critical_damage : public CSkillEffect {
public:
    CSkillEffect_p_physical_skill_critical_damage(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    SkillFxChangeType m_ChangeType;
    double m_Bonus;
};


class CSkillEffect_p_hit_summon : public CSkillEffect {
public:
    CSkillEffect_p_hit_summon(double bonus, SkillFxChangeType changeType) : m_ChangeType(changeType), m_Bonus(bonus) {
        // Current L2Server 0x00ca2094.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    SkillFxChangeType m_ChangeType;
    double m_Bonus;
};


class CSkillEffect_p_dispel_by_realtarget : public CSkillEffect {
public:
    explicit CSkillEffect_p_dispel_by_realtarget(double probability) : m_Probability(probability) {
        // Current L2Server 0x00ca30a0.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_Probability;
};


class CSkillEffect_p_heal_effect_add : public CSkillEffectByAdditionalArmorType {
public:
    CSkillEffect_p_heal_effect_add(double bonus, SkillFxChangeType changeType) : m_Bonus(bonus), m_ChangeType(changeType) {
        // Current L2Server 0x00ca4cd0.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_Bonus;
    SkillFxChangeType m_ChangeType;
};


class CSkillEffect_p_absorb_shield : public CSkillEffect {
public:
    CSkillEffect_p_absorb_shield(int shieldHP, bool limitDamage) :
        m_ShieldHP(static_cast<double>(shieldHP)), m_LimitDamage(limitDamage) {
        // Current L2Server 0x00ca1de4.
    }

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    double m_ShieldHP;
    bool m_LimitDamage;
};


class CSkillEffect_p_absorb_shield_status : public CSkillEffect {
public:
    CSkillEffect_p_absorb_shield_status(VALIDATE::Enum statType, double rate) : m_StatType(statType), m_Rate(rate) {
        // Current L2Server 0x00ca1e18.
    }

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    VALIDATE::Enum m_StatType;
    double m_Rate;
};


class CSkillEffect_p_mp_vampiric_attack : public CSkillEffect {
public:
    explicit CSkillEffect_p_mp_vampiric_attack(double ratio) : m_VampiricRatio(ratio), m_Probability(100.0) {
        // Current L2Server 0x00ca1e40.
    }

    CSkillEffect_p_mp_vampiric_attack(double ratio, double probability) :
        m_VampiricRatio(ratio), m_Probability(probability) {
        // Current L2Server 0x00ca1e74.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_VampiricRatio;
    double m_Probability;
};


class CSkillEffect_p_alter_receive_normal_damage : public CSkillEffect {
public:
    CSkillEffect_p_alter_receive_normal_damage(ActionTargetType targetType, int ratio) :
        m_Ratio(ratio), m_TargetType(targetType) {
        // Current L2Server 0x00ca29cc.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_Ratio;
    ActionTargetType m_TargetType;
};


class CSkillEffect_p_energy_skill_power : public CSkillEffect {
public:
    CSkillEffect_p_energy_skill_power(double bonus, SkillFxChangeType changeType) :
        m_ChangeType(changeType), m_Bonus(bonus) {
        // Current L2Server 0x00ca2bbc.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    SkillFxChangeType m_ChangeType;
    double m_Bonus;
};


class CSkillEffect_p_droprate_modify : public CSkillEffect {
public:
    CSkillEffect_p_droprate_modify(int normalRate, int bossRate) : m_DropRate{normalRate, bossRate} {
        // Current L2Server 0x00ca2b6c.
    }

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_DropRate[2];
};


class CSkillEffect_p_dropamount_modify : public CSkillEffect {
public:
    CSkillEffect_p_dropamount_modify(int normalAmount, int bossAmount) : m_DropAmount{normalAmount, bossAmount} {
        // Current L2Server 0x00ca2b94.
    }

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_DropAmount[2];
};


class CSkillEffect_p_mp_friendly_payback : public CSkillEffect {
public:
    CSkillEffect_p_mp_friendly_payback(double paybackPercent, double probability) :
        m_Probability(probability), m_PaybackPercent(paybackPercent) {
        // Current L2Server 0x00ca3008.
    }

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    double m_Probability;
    double m_PaybackPercent;
};


class CSkillEffect_p_prevention_death : public CSkillEffect {
public:
    CSkillEffect_p_prevention_death(bool preventHPDeath, bool preventMPDeath, bool preventCPDeath) :
        m_PreventDeath{preventHPDeath, preventMPDeath, preventCPDeath} {
        // Current L2Server 0x00ca30c8.
    }

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    bool m_PreventDeath[CCreatureSkillMod::PDP_MAX];
};


class CSkillEffect_p_get_damage_limit_status : public CSkillEffect {
public:
    CSkillEffect_p_get_damage_limit_status(VALIDATE::Enum statType, int constant, int addedMinimum) :
        m_StatType(statType), m_Constant(constant), m_AddedMinimum(addedMinimum) {
        // Current L2Server 0x00ca1ef8.
    }

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    VALIDATE::Enum m_StatType;
    int m_Constant;
    int m_AddedMinimum;
};


class CSkillEffect_p_sub_exp : public CSkillEffect {
public:
    CSkillEffect_p_sub_exp(int expBonus, int expPenalty, SkillFxChangeType changeType) :
        m_ExpBonus(expBonus), m_ExpPenalty(expPenalty), m_ChangeType(changeType) {
        // Current L2Server 0x00ca27d8.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_ExpBonus;
    int m_ExpPenalty;
    SkillFxChangeType m_ChangeType;
};


class CSkillEffect_p_sub_vp : public CSkillEffect {
public:
    CSkillEffect_p_sub_vp(int vitalityBonus, int vitalityPenalty, SkillFxChangeType changeType) :
        m_VitalityBonus(vitalityBonus), m_VitalityPenalty(vitalityPenalty), m_ChangeType(changeType) {
        // Current L2Server 0x00ca2804.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_VitalityBonus;
    int m_VitalityPenalty;
    SkillFxChangeType m_ChangeType;
};


class CSkillEffect_p_physical_penetrate : public CSkillEffectByWeaponType {
public:
    CSkillEffect_p_physical_penetrate(int defenceType, int penetrationRate) :
        m_DefenceType(defenceType), m_PenetrationRate(static_cast<double>(penetrationRate) / 100.0) {
        // Current L2Server 0x00ca30f4.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_DefenceType;
    double m_PenetrationRate;
};


class CSkillEffect_p_magical_penetrate : public CSkillEffectByWeaponType {
public:
    explicit CSkillEffect_p_magical_penetrate(int penetrationRate) :
        m_PenetrationRate(static_cast<double>(penetrationRate) / 100.0) {
        // Current L2Server 0x00ca3148.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_PenetrationRate;
};


class CSkillEffect_p_damage_by_attack_castle_siege : public CSkillEffect {
public:
    CSkillEffect_p_damage_by_attack_castle_siege(EnemyType enemyType, double amount, SkillFxChangeType changeType) :
        m_EnemyType(enemyType), m_Amount(amount), m_ChangeType(changeType) {
        // Current L2Server 0x00ca2f74.
    }

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    EnemyType m_EnemyType;
    double m_Amount;
    SkillFxChangeType m_ChangeType;
};


class CSkillEffect_p_abnormal_time_change_to_abType : public CSkillEffect {
public:
    CSkillEffect_p_abnormal_time_change_to_abType(
        double amount, SkillFxChangeType changeType, int abnormalType, int magicType
    ) :
        m_nAbnormalType(abnormalType), m_nMagicType(magicType), m_dAmount(amount), m_sfct(changeType) {
        // Current L2Server 0x00ca2fa0.
    }

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_nAbnormalType;
    int m_nMagicType;
    double m_dAmount;
    SkillFxChangeType m_sfct;
};


class CSkillEffect_p_abnormal_time_change_to_debuff : public CSkillEffect {
public:
    CSkillEffect_p_abnormal_time_change_to_debuff(
        double amount, SkillFxChangeType changeType, DebuffType debuffType, int magicType
    ) :
        m_eDeBuffType(debuffType), m_nMagicType(magicType), m_dAmount(amount), m_sfct(changeType) {
        // Current L2Server 0x00ca2fd4.
    }

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    DebuffType m_eDeBuffType;
    int m_nMagicType;
    double m_dAmount;
    SkillFxChangeType m_sfct;
};


class CSkillEffect_p_death_attack : public CSkillEffect {
public:
    CSkillEffect_p_death_attack(int magicalAttack, int hpRatio, int amount, SkillFxChangeType changeType) :
        m_SkillMod(magicalAttack != 0 ? CCreatureSkillMod::MAttack : CCreatureSkillMod::PAttack),
        m_HPRatio(hpRatio),
        m_Amount(amount),
        m_Sfct(changeType) {
        // Current L2Server 0x00ca2830.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    CCreatureSkillMod::ModName m_SkillMod;
    int m_HPRatio;
    int m_Amount;
    SkillFxChangeType m_Sfct;
};


class CSkillEffect_p_item_chance : public CSkillEffect {
public:
    explicit CSkillEffect_p_item_chance(ItemChanceNpcType npcType) : m_NpcType(npcType) {
        // Current L2Server 0x00ca5c50.
    }

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    friend class CParserForSkill;

    ItemChanceNpcType m_NpcType;
    std::vector<ItemChanceType> m_vItemChanceType;
};


class CSkillEffect_p_stat_up_at_night : public CSkillEffect {
public:
    CSkillEffect_p_stat_up_at_night(
        int dexterity, int intelligence, int mentality, int constitution, int strength, int wit
    ) {
        // Current L2Server 0x00ca1f24.
        m_Bonus.m_N[2] = dexterity;
        m_Bonus.m_N[1] = intelligence;
        m_Bonus.m_N[4] = mentality;
        m_Bonus.m_N[3] = constitution;
        m_Bonus.m_N[0] = strength;
        m_Bonus.m_N[5] = wit;
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    CStats m_Bonus;
};


class CSkillEffect_p_block_act_bonus : public CSkillEffect {
public:
    CSkillEffect_p_block_act_bonus(int targetType, double bonus, SkillFxChangeType changeType) :
        m_Sfct(changeType), m_TargetType(targetType), m_Bonus(bonus) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

    static double CalcDamage(CCreature& attacker, CCreature& target, double damage);

private:
    SkillFxChangeType m_Sfct; // +0x30
    int m_TargetType; // +0x34
    double m_Bonus; // +0x38
};


class CSkillEffect_p_potion_bonus : public CSkillEffect {
public:
    CSkillEffect_p_potion_bonus(double bonus, SkillFxChangeType changeType) : sfct(changeType), m_nBonus(bonus) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    static int CalcHealBonus(CCreature* creature, int amount);

    SkillFxChangeType sfct;

private:
    double m_nBonus;
};

class CSkillEffect_p_skill_critical : public CSkillEffectByWeaponType {
public:
    CSkillEffect_p_skill_critical();

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    SetL2ParamEnum m_eParamType;

    friend class CParserForSkill;
    friend class CParserForOption;
};


class CSkillEffect_p_skill_critical_probability : public CSkillEffect {
public:
    CSkillEffect_p_skill_critical_probability(int bonus, SkillFxChangeType changeType) :
        m_Bonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_Bonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_skill_critical_rate_fix : public CSkillEffect {
public:
    explicit CSkillEffect_p_skill_critical_rate_fix(int probability);
    ~CSkillEffect_p_skill_critical_rate_fix() override;

    void SetSkillList(std::vector<std::int64_t>* skillIds);

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    std::vector<int> m_vId;
    int m_nProb;
};


class CSkillEffect_p_skill_power : public CSkillEffect {
public:
    CSkillEffect_p_skill_power(double bonus, SkillFxChangeType changeType) : m_dBonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_spell_power : public CSkillEffect {
public:
    CSkillEffect_p_spell_power(double bonus, SkillFxChangeType changeType);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_critical_damage_position : public CSkillEffect {
public:
    CSkillEffect_p_critical_damage_position(SkillFxPositionType position, int bonus, SkillFxChangeType changeType) :
        m_Position(position), m_Bonus(bonus), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    SkillFxPositionType m_Position;
    int m_Bonus;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_physical_abnormal_resist : public CSkillEffect {
public:
    CSkillEffect_p_physical_abnormal_resist(int amount, SkillFxChangeType changeType) :
        m_Amount(amount), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_Amount;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_magic_abnormal_resist : public CSkillEffect {
public:
    CSkillEffect_p_magic_abnormal_resist(int amount, SkillFxChangeType changeType) : m_Amount(amount), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_Amount;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_magic_cancel_resist : public CSkillEffect {
public:
    CSkillEffect_p_magic_cancel_resist(int amount, SkillFxChangeType changeType) : m_Amount(amount), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_Amount;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_magic_avoid : public CSkillEffect {
public:
    CSkillEffect_p_magic_avoid(int amount, SkillFxChangeType changeType) : m_Amount(amount), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_Amount;
    SkillFxChangeType m_Sfct;
};


class CSkillEffect_p_abnormal_remove_by_hit : public CSkillEffect {
public:
    CSkillEffect_p_abnormal_remove_by_hit(int hitCount, int probability) :
        m_HitCount(hitCount), m_Probability(probability) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    [[maybe_unused]] int m_HitCount;
    [[maybe_unused]] int m_Probability;
};

class CSkillEffect_p_abnormal_remove_by_dmg : public CSkillEffect {
public:
    explicit CSkillEffect_p_abnormal_remove_by_dmg(int damageSum) : m_DamageSum(damageSum) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    [[maybe_unused]] int m_DamageSum;
};

class CSkillEffect_p_shield_defence_rate : public CSkillEffect {
public:
    explicit CSkillEffect_p_shield_defence_rate(double bonus) : m_dBonus(bonus) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dBonus;
};

class CSkillEffect_p_avoid_agro : public CSkillEffect {
public:
    explicit CSkillEffect_p_avoid_agro(int probability) : nProb(probability) {}

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nProb;
};

class CSkillEffect_p_cubic_mastery : public CSkillEffect {
public:
    explicit CSkillEffect_p_cubic_mastery(int count) : nCount(count) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nCount;
};

class CSkillEffect_p_mana_charge : public CSkillEffect {
public:
    explicit CSkillEffect_p_mana_charge(int bonus) : nBonus(bonus) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nBonus;
};

class CSkillEffect_p_vampiric_attack : public CSkillEffect {
public:
    CSkillEffect_p_vampiric_attack(double bonus, double probability);

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double dBonus;
    double dProb;
};

class CSkillEffect_p_vampiric_defence : public CSkillEffect {
public:
    explicit CSkillEffect_p_vampiric_defence(double defence) : dDefence(defence) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double dDefence;
};

class CSkillEffect_p_nopkcount : public CSkillEffect {
public:
    explicit CSkillEffect_p_nopkcount(int probability) : m_nProb(probability) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_nProb;
};

class CSkillEffect_p_avoid_skill : public CSkillEffect {
public:
    CSkillEffect_p_avoid_skill(int magicType, int probability) : nMagicType(magicType), nProb(probability) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nMagicType;
    int nProb;
};

class CSkillEffect_p_counter_skill : public CSkillEffect {
public:
    CSkillEffect_p_counter_skill(int magicType, int probability) : nMagicType(magicType), nProb(probability) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nMagicType;
    int nProb;
};

class CSkillEffect_p_avoid_counter_skill : public CSkillEffect {
public:
    CSkillEffect_p_avoid_counter_skill(int magicType, int probability) : nMagicType(magicType), nProb(probability) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nMagicType;
    int nProb;
};

class CSkillEffect_p_focus_energy : public CSkillEffect {
public:
    explicit CSkillEffect_p_focus_energy(int maxEnergy) : m_MaxEnergy(maxEnergy) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_MaxEnergy;
};

class CSkillEffect_p_create_item_critical : public CSkillEffect {
public:
    explicit CSkillEffect_p_create_item_critical(double ratio) : m_dRatio(ratio) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dRatio;
};

class CSkillEffect_p_Fishing_Monster_prob : public CSkillEffect {
public:
    explicit CSkillEffect_p_Fishing_Monster_prob(double ratio) : m_dRatio(ratio) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dRatio;
};

class CSkillEffect_p_Fishing_start_time : public CSkillEffect {
public:
    explicit CSkillEffect_p_Fishing_start_time(double ratio) : m_dRatio(ratio) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dRatio;
};

class CSkillEffect_p_Fishing_instantkill_prob : public CSkillEffect {
public:
    explicit CSkillEffect_p_Fishing_instantkill_prob(int ratio) : m_nRatio(ratio) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_nRatio;
};

class CSkillEffect_i_Fishing_instantkill : public CSkillEffect {
public:
    explicit CSkillEffect_i_Fishing_instantkill(int probability) : m_nProb(probability) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_nProb;
};

class CSkillEffect_p_fishing_mastery : public CSkillEffect {
public:
    explicit CSkillEffect_p_fishing_mastery(double bonus) : dBonus(bonus) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double dBonus;
};

class CSkillEffect_p_change_fishing_mastery : public CSkillEffect {
public:
    CSkillEffect_p_change_fishing_mastery(int level, double bonus) : nLevel(level), dBonus(bonus) {}

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nLevel;
    [[maybe_unused]] std::uint32_t m_alignment34To38{};
    double dBonus;
};

class CSkillEffect_p_Fishing_Area_buff : public CSkillEffect {
public:
    CSkillEffect_p_Fishing_Area_buff() = default;

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};

class CSkillEffect_p_fishing_damage : public CSkillEffect {
public:
    explicit CSkillEffect_p_fishing_damage(double damage) : dDamage(damage) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    double dDamage;
};

class CSkillEffect_p_damage_shield : public CSkillEffect {
public:
    explicit CSkillEffect_p_damage_shield(double reversePercent) : dRevPer(reversePercent) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double dRevPer;
};

class CSkillEffect_p_block_debuff : public CSkillEffect {
public:
    CSkillEffect_p_block_debuff() : m_dDist(0.0) {}
    explicit CSkillEffect_p_block_debuff(double distance) : m_dDist(distance) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dDist;
};

class CSkillEffect_p_block_add_slot : public CSkillEffect {
public:
    CSkillEffect_p_block_add_slot() = default;

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

    // The generated skill parser appends the resolved abnormal-slot values
    // directly to this original public carrier.
    std::vector<AbnormalStatusSlotType> m_AbnormalSlots;
};

class CSkillEffect_p_block_buff_slot : public CSkillEffect {
public:
    CSkillEffect_p_block_buff_slot() : m_bNotDispel(false) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;

    // Both fields are populated directly by CParserForSkill::yyaction.
    std::vector<AbnormalStatusSlotType> m_AbnormalSlots;
    bool m_bNotDispel;
};


class CSkillEffect_p_block_skill : public CSkillEffect {
public:
    explicit CSkillEffect_p_block_skill(SkillIsMagicType magicType) : m_nMagicType(magicType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    SkillIsMagicType m_nMagicType;
};


class CSkillEffect_p_mp_shield : public CSkillEffect {
public:
    CSkillEffect_p_mp_shield(int percent, int costPercent) : m_nMPShieldPer(percent), m_nMPShieldCostPer(costPercent) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_nMPShieldPer;
    int m_nMPShieldCostPer;
};

class CSkillEffect_p_add_ensoul_prob : public CSkillEffect {
public:
    explicit CSkillEffect_p_add_ensoul_prob(double probability) : m_dProb(probability) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_dProb;
};

class CSkillEffect_p_charge_vitality : public CSkillEffect {
public:
    explicit CSkillEffect_p_charge_vitality(int amount) : m_nParam(amount) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_nParam;
};

class CSkillEffect_p_create_item : public CSkillEffect {
public:
    explicit CSkillEffect_p_create_item(int level) : nLevel(level) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nLevel;
};

class CSkillEffect_p_create_common_item : public CSkillEffect {
public:
    explicit CSkillEffect_p_create_common_item(int level) : nLevel(level) {}

    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nLevel;
};

class CSkillEffect_p_summon_point : public CSkillEffect {
public:
    explicit CSkillEffect_p_summon_point(int point) : m_nPoint(point) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_nPoint;
};

class CSkillEffect_p_defence_damage : public CSkillEffect {
public:
    CSkillEffect_p_defence_damage(int rate, SkillFxChangeType changeType) : m_Rate(rate), m_Sfct(changeType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_Rate;
    SkillFxChangeType m_Sfct;
};

class CSkillEffect_p_speed_out_of_night : public CSkillEffect {
public:
    explicit CSkillEffect_p_speed_out_of_night(double bonus) : m_Bonus(bonus) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    double m_Bonus;
};

class CSkillEffect_p_transfer_damage_pc : public CSkillEffect {
public:
    explicit CSkillEffect_p_transfer_damage_pc(double rate) : m_Rate(rate) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int transferTargetId) override;

private:
    double m_Rate;
};

class CSkillEffect_p_transfer_damage_to : public CSkillEffect {
public:
    explicit CSkillEffect_p_transfer_damage_to(double rate) : m_Rate(rate) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int transferTargetId) override;

private:
    double m_Rate;
};


class CSkillEffect_p_transfer_damage_summon : public CSkillEffect {
public:
    explicit CSkillEffect_p_transfer_damage_summon(int bonus) : nBonus(bonus) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nBonus;
};

class CSkillEffect_p_soul_eating : public CSkillEffect {
public:
    CSkillEffect_p_soul_eating(int standardExp, int maximumSoul) : m_StandardExp(standardExp), m_MaxSoul(maximumSoul) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_StandardExp;
    int m_MaxSoul;
};

class CSkillEffect_p_pve_bonus : public CSkillEffect {
public:
    CSkillEffect_p_pve_bonus(double bonus, SkillFxChangeType changeType, EnemyType enemyType) :
        nBonus(bonus), sfct(changeType), eEnemyType(enemyType) {}

    const double nBonus;
    const SkillFxChangeType sfct;
    const EnemyType eEnemyType;
};


#define L2_DECLARE_PVE_BONUS_EFFECT(class_name)                                                                             \
    class class_name : public CSkillEffect_p_pve_bonus {                                                                    \
    public:                                                                                                                 \
        class_name(double bonus, SkillFxChangeType changeType, EnemyType enemyType) :                                       \
            CSkillEffect_p_pve_bonus(bonus, changeType, enemyType) {}                                                       \
                                                                                                                            \
        bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;                                  \
    };

L2_DECLARE_PVE_BONUS_EFFECT(CSkillEffect_p_pve_physical_attack_dmg_bonus);
L2_DECLARE_PVE_BONUS_EFFECT(CSkillEffect_p_pve_physical_skill_dmg_bonus);
L2_DECLARE_PVE_BONUS_EFFECT(CSkillEffect_p_pve_magical_skill_dmg_bonus);

#undef L2_DECLARE_PVE_BONUS_EFFECT

class CSkillEffect_p_pvp_bonus : public CSkillEffect {
public:
    CSkillEffect_p_pvp_bonus(double bonus, SkillFxChangeType changeType) : nBonus(bonus), sfct(changeType) {}

    const double nBonus;
    const SkillFxChangeType sfct;
};


#define L2_DECLARE_PVP_BONUS_EFFECT(class_name)                                                                             \
    class class_name : public CSkillEffect_p_pvp_bonus {                                                                    \
    public:                                                                                                                 \
        class_name(double bonus, SkillFxChangeType changeType) : CSkillEffect_p_pvp_bonus(bonus, changeType) {}             \
                                                                                                                            \
        bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;                                  \
    };

L2_DECLARE_PVP_BONUS_EFFECT(CSkillEffect_p_pvp_physical_attack_dmg_bonus);
L2_DECLARE_PVP_BONUS_EFFECT(CSkillEffect_p_pvp_physical_skill_dmg_bonus);
L2_DECLARE_PVP_BONUS_EFFECT(CSkillEffect_p_pvp_magical_skill_dmg_bonus);
L2_DECLARE_PVP_BONUS_EFFECT(CSkillEffect_p_pvp_physical_attack_defence_bonus);
L2_DECLARE_PVP_BONUS_EFFECT(CSkillEffect_p_pvp_physical_skill_defence_bonus);
L2_DECLARE_PVP_BONUS_EFFECT(CSkillEffect_p_pvp_magical_skill_defence_bonus);

#undef L2_DECLARE_PVP_BONUS_EFFECT

#define L2_DECLARE_SIMPLE_PUMP_EFFECT(class_name)                                                                           \
    class class_name : public CSkillEffect {                                                                                \
    public:                                                                                                                 \
        bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;                                  \
    };

L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_block_attack);
L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_block_act);
L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_transform_hangover);

class CSkillEffect_p_luck : public CSkillEffect {
public:
    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_block_spell);

class CSkillEffect_p_passive : public CSkillEffect {
public:
    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_physical_shield_defence_angle_all);
L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_physical_polarm_target_single);
L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_abnormal_rate_limit);
L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_block_skill_physical);
L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_block_skill_special);
L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_block_buff);
L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_channel_clan);
L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_pk_protect);
L2_DECLARE_SIMPLE_PUMP_EFFECT(CSkillEffect_p_block_skill_trigger);

#undef L2_DECLARE_SIMPLE_PUMP_EFFECT

class CSkillEffect_p_expand_deco_slot : public CSkillEffect {
public:
    explicit CSkillEffect_p_expand_deco_slot(int count) : nCount(count) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nCount;
};


class CSkillEffect_p_expand_jewel_slot : public CSkillEffect {
public:
    explicit CSkillEffect_p_expand_jewel_slot(int count) : nCount(count) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int nCount;
};

class CSkillEffect_p_expand_agathion_main_slot : public CSkillEffect {
public:
    explicit CSkillEffect_p_expand_agathion_main_slot(int count) : m_nCount(count) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_nCount;
};

class CSkillEffect_p_expand_agathion_sub_slot : public CSkillEffect {
public:
    explicit CSkillEffect_p_expand_agathion_sub_slot(int count) : m_nCount(count) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_nCount;
};


class CSkillEffect_p_crystallize : public CSkillEffect {
public:
    CSkillEffect_p_crystallize() : eCrystalType(static_cast<CrystalType>(0)) {}
    explicit CSkillEffect_p_crystallize(CrystalType crystalType) : eCrystalType(crystalType) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

    CrystalType eCrystalType;
};


#define L2_DECLARE_TOGGLE_PUMP_EFFECT(class_name)                                                                           \
    class class_name : public CSkillEffect {                                                                                \
    public:                                                                                                                 \
        bool PumpStart(                                                                                                     \
            CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type                    \
        ) const override;                                                                                                   \
        bool PumpEnd(                                                                                                       \
            CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type                      \
        ) const override;                                                                                                   \
    };

L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_block_get_energy);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_block_elixer_premium);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_cheapshot);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_block_resurrection);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_block_controll);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_block_escape);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_block_move);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_block_target);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_fear);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_dominion_transform);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_psychic_power);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_berserker);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_balthus_buff);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_kalie_buff);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_double_summon);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_violet_boy);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_block_party);
L2_DECLARE_TOGGLE_PUMP_EFFECT(CSkillEffect_p_ability_change_itemskill);

#undef L2_DECLARE_TOGGLE_PUMP_EFFECT

class CSKillEffetct_p_acquire_alchemy_skill : public CSkillEffect {
public:
    CSKillEffetct_p_acquire_alchemy_skill(int type, bool master) : m_bMaster(master), m_nType(type) {
        // Current L2Server 0x00ca2dec.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    bool m_bMaster; // +0x30
    [[maybe_unused]] std::array<std::byte, 0x03> m_alignment0031To0034{};
    int m_nType; // +0x34
};


class CSkillEffect_p_knockdown : public CSkillEffect {
public:
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_speed_out_of_fight : public CSkillEffect {
public:
    explicit CSkillEffect_p_speed_out_of_fight(double bonus) : m_Bonus(bonus) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    double m_Bonus;
};

class CSkillEffect_p_Worldchatpoint_Add : public CSkillEffect {
public:
    explicit CSkillEffect_p_Worldchatpoint_Add(int addCount) : m_nAddCount(addCount) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_nAddCount;
};

class CSkillEffect_p_set_collected : public CSkillEffect {
public:
    bool CheckPumpCondition(CCreature* creature) override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
};

class CSkillEffect_p_energy_heal : public CSkillEffect {
public:
    explicit CSkillEffect_p_energy_heal(bool enableRecovery) : m_EnableRecovery(enableRecovery) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    bool m_EnableRecovery;
};

class CSkillEffect_p_alter_skill_by_event : public CSkillEffect {
public:
    explicit CSkillEffect_p_alter_skill_by_event(AlterSkill::Event event) : m_Event(event) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    AlterSkill::Event m_Event;
};

class CSkillEffect_p_expand_artifact_slot_group : public CSkillEffect {
public:
    explicit CSkillEffect_p_expand_artifact_slot_group(int level) : m_nLevel(level) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;

private:
    int m_nLevel;
};

class CSkillEffect_i_blink_return : public CSkillEffect {
public:
    explicit CSkillEffect_i_blink_return(int distance) : m_Dist(distance) {}

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;

private:
    int m_Dist;
};


class CSkillEffect_p_bonustimelimit_up : public CSkillEffect {
public:
    explicit CSkillEffect_p_bonustimelimit_up(int bonusTimeLimit) : m_BonusTimeLimit(bonusTimeLimit) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    [[maybe_unused]] int m_BonusTimeLimit;
};

class CSkillEffect_p_physical_attack_pc_level : public CSkillEffect {
public:
    explicit CSkillEffect_p_physical_attack_pc_level(int amount) : m_Amount(amount) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_Amount;
};

class CSkillEffect_p_magical_attack_pc_level : public CSkillEffect {
public:
    explicit CSkillEffect_p_magical_attack_pc_level(int amount) : m_Amount(amount) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    int m_Amount;
};


class CSkillEffect_p_align_direction : public CSkillEffect {
public:
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
};


class CSkillEffect_p_remove_grade_penalty : public CSkillEffect {
public:
    explicit CSkillEffect_p_remove_grade_penalty(CrystalType crystalType) : m_eCrystalType(crystalType) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

private:
    CrystalType m_eCrystalType;
};


class CSkillEffect_p_remove_equip_penalty : public CSkillEffect {
public:
    CSkillEffect_p_remove_equip_penalty() : eCrystalType(static_cast<CrystalType>(0)) {}
    explicit CSkillEffect_p_remove_equip_penalty(CrystalType crystalType) : eCrystalType(crystalType) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;

    CrystalType eCrystalType;
};


class CSkillEffect_p_betray : public CSkillEffect {
public:
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};


class CSkillEffect_p_target_me : public CSkillEffect {
public:
    CSkillEffect_p_target_me() = default;

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};

class CSkillEffect_p_hide : public CSkillEffect {
public:
    explicit CSkillEffect_p_hide(bool preserveByDamage) : m_PreserveByDamage(preserveByDamage) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    bool m_PreserveByDamage;
};

class CSkillEffect_p_hide_no_inherit : public CSkillEffect {
public:
    explicit CSkillEffect_p_hide_no_inherit(bool preserveByDamage) : m_PreserveByDamage(preserveByDamage) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    [[maybe_unused]] bool m_PreserveByDamage;
};

class CSkillEffect_p_wrong_casting : public CSkillEffect {
public:
    CSkillEffect_p_wrong_casting(int magicType, int probability) : m_nMagicType(magicType), m_nProb(probability) {}

    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    int m_nMagicType;
    int m_nProb;
};


#define L2_DECLARE_INT_TOGGLE_PUMP_EFFECT(class_name)                                                                       \
    class class_name : public CSkillEffect {                                                                                \
    public:                                                                                                                 \
        explicit class_name(int value) : m_Value(value) {}                                                                  \
        bool PumpStart(                                                                                                     \
            CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type                    \
        ) const override;                                                                                                   \
        bool PumpEnd(                                                                                                       \
            CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type                      \
        ) const override;                                                                                                   \
                                                                                                                            \
    private:                                                                                                                \
        int m_Value;                                                                                                        \
    };

L2_DECLARE_INT_TOGGLE_PUMP_EFFECT(CSkillEffect_p_preserve_abnormal);
L2_DECLARE_INT_TOGGLE_PUMP_EFFECT(CSkillEffect_p_abnormal_shield);

#undef L2_DECLARE_INT_TOGGLE_PUMP_EFFECT


class CSkillEffect_p_aura : public CSkillEffect {
public:
    explicit CSkillEffect_p_aura(char16_t* skillName);

    void OnPostLoad() override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    char16_t m_SkillName[256];
    CSkillInfo* m_pSkillInfo;
};

class CSkillEffect_a_speed : public CSkillEffectAddBonusByArmorType {
public:
    CSkillEffect_a_speed(double bonus, SkillFxChangeType changeType, char16_t* skillName, SkillTargetTypeEnum targetType);

    void OnPostLoad() override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    char16_t m_SkillName[256];
    SkillTargetTypeEnum m_eTargetType;
    CSkillInfo* m_pSkillInfo;
};

class CSkillEffect_a_synergy_skill : public CSkillEffect {
public:
    CSkillEffect_a_synergy_skill(int synergyId, SkillTargetTypeEnum targetType);

    void OnPostLoad() override;
    bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const override;
    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;


private:
    int m_nSynergeId;
    SkillTargetTypeEnum m_eTargetType;
};


class CEffectCond_p_castle_siege : public CEffectCondition {
public:
    CEffectCond_p_castle_siege() = default;

    bool CheckEffectCondition(CCreature* creature) override;
};

class CEffectCond_p_olympiad_pledge : public CEffectCondition {
public:
    CEffectCond_p_olympiad_pledge() = default;

    bool CheckEffectCondition(CCreature* creature) override;
};

class CEffectCond_p_over_hp : public CEffectCondition {
public:
    explicit CEffectCond_p_over_hp(double percent) : m_dPer(percent) {}

    bool CheckEffectCondition(CCreature* creature) override;


private:
    double m_dPer;
};

class CSKillEffetct_p_attack_position_change : public CSkillEffect {
public:
    explicit CSKillEffetct_p_attack_position_change(SkillFxPositionType direction) : m_eDirection(direction) {}

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    SkillFxPositionType m_eDirection;
};

class CSKillEffetct_p_real_resist : public CSkillEffect {
public:
    explicit CSKillEffetct_p_real_resist(double power) : m_dPower(power) {
        // Current L2Server 0x00ca2e7c.
    }

    bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown) override;


private:
    double m_dPower;
};

class CSKillEffetct_i_summon_territory : public CSkillEffect {
public:
    CSKillEffetct_i_summon_territory(int radius, int time) : m_nRadius(radius), m_nTime(time) {
        // Current L2Server 0x00ca2ea4.
    }

    bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    ) override;


private:
    int m_nRadius;
    int m_nTime;
};


class CSkillEffect_op_target_pc : public CSkillOperateCondition {
public:
    CSkillEffect_op_target_pc() = default;

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
};


class CSkillEffect_op_combat : public CSkillOperateCondition {
public:
    explicit CSkillEffect_op_combat(bool combat);

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    const bool m_Combat;
};

class CSkillEffect_op_skill : public CSkillOperateCondition {
public:
    CSkillEffect_op_skill(int skillId, int skillLevel, bool exists);

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    const int m_SkillId;
    const int m_SkillLevel;
    const bool m_Exist;
};

class CSkillEffect_op_skill_acquire : public CSkillOperateCondition {
public:
    CSkillEffect_op_skill_acquire(int skillId, int use);

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    const int m_SkillId;
    const int m_Use;
};


class CSkillEffect_op_blink : public CSkillOperateCondition {
public:
    CSkillEffect_op_blink(const double distance, const SkillDirectionType direction) :
        m_Dist(distance), m_Direction(direction) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    double m_Dist;
    SkillDirectionType m_Direction;
};

class CSkillEffect_op_charge_self : public CSkillOperateCondition {
public:
    CSkillEffect_op_charge_self(const double distance, const SkillDirectionType direction) :
        m_Dist(distance), m_Direction(direction) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;
    static bool GetDestPos(CCreature* creature, double distance, SkillDirectionType direction, FVector& destination);

private:
    double m_Dist;
    SkillDirectionType m_Direction;
};

class CSkillEffect_op_return_pos : public CSkillOperateCondition {
public:
    explicit CSkillEffect_op_return_pos(int distance) : m_Dist(distance > 1500 ? 1500 : distance) {}

    bool CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject* target) override;

private:
    int m_Dist;
};


class CSkillEffect_c_mp : public CSkillConsumeEffect {
public:
    CSkillEffect_c_mp(double amount, int unitTick) : CSkillConsumeEffect(amount) { nUnitTick = unitTick; }

    bool Consume(CObject* target, double amount) override;
};

class CSkillEffect_c_hp : public CSkillConsumeEffect {
public:
    CSkillEffect_c_hp(double amount, int unitTick) : CSkillConsumeEffect(amount) { nUnitTick = unitTick; }

    bool Consume(CObject* target, double amount) override;
};

class CSkillEffect_c_rest : public CSkillConsumeEffect {
public:
    CSkillEffect_c_rest(double amount, int unitTick) : CSkillConsumeEffect(amount) { nUnitTick = unitTick; }

    bool Consume(CObject* target, double amount) override;
};

class CSkillEffect_c_chameleon_rest : public CSkillConsumeEffect {
public:
    CSkillEffect_c_chameleon_rest(double amount, int unitTick) : CSkillConsumeEffect(amount) { nUnitTick = unitTick; }

    bool Consume(CObject* target, double amount) override;
};

class CSkillEffect_c_mp_by_level : public CSkillConsumeEffect {
public:
    CSkillEffect_c_mp_by_level(double amount, int unitTick) : CSkillConsumeEffect(amount) { nUnitTick = unitTick; }

    bool Consume(CObject* target, double amount) override;
};

class CSkillEffect_c_fake_death : public CSkillConsumeEffect {
public:
    CSkillEffect_c_fake_death(double amount, int unitTick) : CSkillConsumeEffect(amount) { nUnitTick = unitTick; }

    bool Consume(CObject* target, double amount) override;
    bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const override;
};

class CSkillEffect_c_agathion_energy : public CSkillConsumeEffect {
public:
    CSkillEffect_c_agathion_energy(double amount, int unitTick) : CSkillConsumeEffect(amount) { nUnitTick = unitTick; }

    bool Consume(CObject* target, double amount) override;
    bool IsConsumeAgathionEnergyEffect() const override;
};


class CSkillEffect_t_hp_in_invincible : public CSkillTickEffect {
public:
    CSkillEffect_t_hp_in_invincible(double amount, int unitTick, SkillFxChangeType changeType, bool cpEnable) :
        CSkillTickEffect(amount, changeType), m_CpEnable(cpEnable) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 1; }

private:
    [[maybe_unused]] bool m_CpEnable;
};

class CSkillEffect_t_hp : public CSkillTickEffect {
public:
    CSkillEffect_t_hp(double amount, int unitTick, SkillFxChangeType changeType, bool cpEnable) :
        CSkillTickEffect(amount, changeType), m_CpEnable(cpEnable) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 1; }

private:
    bool m_CpEnable;
};

class CSkillEffect_t_hp_by_hp : public CSkillTickEffect {
public:
    CSkillEffect_t_hp_by_hp(double amount, int unitTick, SkillFxChangeType changeType, double hpRatio) :
        CSkillTickEffect(amount, changeType), m_dHPratio(hpRatio), m_effect(amount, unitTick, changeType, true) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 1; }

private:
    double m_dHPratio;
    CSkillEffect_t_hp m_effect;
};

class CSkillEffect_t_hp_fatal : public CSkillTickEffect {
public:
    CSkillEffect_t_hp_fatal(double amount, int unitTick, SkillFxChangeType changeType) :
        CSkillTickEffect(amount, changeType) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 1; }
};

class CSkillEffect_t_hp_magical_attack : public CSkillTickEffect {
public:
    CSkillEffect_t_hp_magical_attack(double amount, int unitTick, SkillFxChangeType changeType, int baseMagicPower) :
        CSkillTickEffect(amount, changeType), m_nBaseMagicPower(baseMagicPower) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 1; }

private:
    int m_nBaseMagicPower;
};

class CSkillEffect_t_cub_hp : public CSkillTickEffect {
public:
    CSkillEffect_t_cub_hp(double amount, int unitTick, SkillFxChangeType changeType) : CSkillTickEffect(amount, changeType) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 1; }
};

class CSkillEffect_t_mp : public CSkillTickEffect {
public:
    CSkillEffect_t_mp(double amount, int unitTick, SkillFxChangeType changeType) : CSkillTickEffect(amount, changeType) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 2; }
};

class CSkillEffect_t_cp : public CSkillTickEffect {
public:
    CSkillEffect_t_cp(double amount, int unitTick, SkillFxChangeType changeType) : CSkillTickEffect(amount, changeType) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 2; }
};

class CSkillEffect_t_get_energy : public CSkillTickEffect {
public:
    CSkillEffect_t_get_energy(double amount, int unitTick, SkillFxChangeType changeType) :
        CSkillTickEffect(amount, changeType) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 2; }
};

class CSkillEffect_t_energy : public CSkillTickEffect {
public:
    CSkillEffect_t_energy(double amount, int unitTick, SkillFxChangeType changeType, int energyVar) :
        CSkillTickEffect(amount, changeType), m_EnergyVar(energyVar) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 2; }

private:
    int m_EnergyVar;
};

class CSkillEffect_t_pull : public CSkillTickEffect {
public:
    CSkillEffect_t_pull(double amount, int unitTick, SkillFxChangeType changeType, double distance) :
        CSkillTickEffect(amount, changeType), m_Dist(distance) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 2; }

private:
    double m_Dist;
};

class CSkillEffect_t_hp_to_owner : public CSkillTickEffect {
public:
    enum BonusStat : int {
        HP = 0,
        MP = 1,
        CP = 2,
    };

    CSkillEffect_t_hp_to_owner(double amount, int unitTick, SkillFxChangeType changeType, double rate) :
        CSkillTickEffect(amount, changeType), m_Rate(rate) {
        nUnitTick = unitTick;
    }

    bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo) override;
    int GetTickEffectType() override { return 1; }

    std::vector<BonusStat> m_BonusStat;

private:
    void ApplyHPBonus(CCreature& creature, double amount);
    void ApplyMPBonus(CCreature& creature, double amount);
    void ApplyCPBonus(CCreature& creature, double amount);

    double m_Rate;
};


namespace L2SkillFunc {

void AddBonus(double* percent, double* difference, SkillFxChangeType changeType, double bonus);
void RemoveBonus(double* percent, double* difference, SkillFxChangeType changeType, double bonus);
void AddBonus(CCreature* creature, CCreatureSkillMod::ModName modName, SkillFxChangeType changeType, double bonus);
bool pump_rate_by_hp(
    CCreature* creature,
    const CSkillInfo* skillInfo,
    bool showMessage,
    double threshold,
    double& percent,
    double& difference,
    double bonus,
    SkillFxChangeType changeType
);
bool pump_rate_by_hp(
    CCreature* creature,
    const CSkillInfo* skillInfo,
    bool showMessage,
    double threshold,
    CCreatureSkillMod::ModName modName,
    double bonus,
    SkillFxChangeType changeType
);
void _IncrementMapRef(std::map<int, int>* values, int key);
void _DecrementMapRef(std::map<int, int>* values, int key);

CCreature* GetCheckedCreature(CCreature* creature, CObject* target, SkillTargetTypeEnum targetType);
void CancelAllActionForMove(CCreature* creature);
void BlockAction(CCreature* creature, bool success);
bool IsAttackable(CObject* object);
bool CanFlyingTransform(User* user);
bool CanSummon(CCreature* creature, SkillTargetTypeEnum targetType, bool summon, bool pet);
bool CheckMultiSummonable(CCreature* creature, int npcClassId);
double IncreaseCP(CCreature* creature, unsigned int objectId, SkillFxChangeType changeType, double amount, int reason);
double DecreaseCP(CCreature* creature, unsigned int objectId, SkillFxChangeType changeType, double amount, int reason);
double IncreaseHP(
    CCreature* creature, unsigned int objectId, SkillFxChangeType changeType, double amount, int reason, bool broadcast
);
double IncreaseMP(CCreature* creature, unsigned int objectId, SkillFxChangeType changeType, double amount);

struct MagicAttackerInfo {
    MagicAttackerInfo() : m_MAttack(0.0), m_MagicPower(0.0), m_WitBonus(0.0), m_Penalty(false) {}

    double m_MAttack;
    double m_MagicPower;
    double m_WitBonus;
    bool m_Penalty;
};

struct MagicTargetInfo {
    MagicTargetInfo() :
        m_pTargetCreature(nullptr),
        m_pTargetStaticObject(nullptr),
        m_MDefend(0.0),
        m_AttrResist(0.0),
        m_WitBonus(0.0),
        m_Level(0) {}

    CCreature* m_pTargetCreature;
    CStaticObject* m_pTargetStaticObject;
    double m_MDefend;
    double m_AttrResist;
    double m_WitBonus;
    int m_Level;
};

struct ResistanceInfo {
    bool highLevel;
    int nVictimLevel;
    double dVictimWitBonus;
    int nAttackerLevel;
    double dAttackerWitBonus;
    bool bAttackerPenalty;
    double dSkillBonusPer;
    double dSkillBonusDiff;
    bool bPC2NPC;
};

MagicAttackerInfo GetMagicAttackerInfo(CCreature& creature);
MagicTargetInfo GetMagicTargetInfo(CCreature& creature, CObject& target, const CSkillInfo* skillInfo);
double ApplyMagicCriticalToDamage(double damage, CCreature& attacker, CObject& target);
bool ApplyShieldDefense(CCreature& attacker, CObject* target, double probability, double& shieldDefense, bool notify);
double ApplyBonusToAttackDamage(double damage, CCreature& attacker, const CSkillInfo& skillInfo);
double GetSoulBonus(CCreature* creature, const CSkillInfo* skillInfo);
double CalcHealingAmount(CCreature* creature, const CSkillInfo* skillInfo, CCreature* target, double power);
void SendHpMpChangedSystemMessage(CCreature* source, CCreature& target, int amount, int messageId);
double _CalcPysicalSkillCriticalBonus(CCreature& creature, double specialProbability);
double CalcPhysicalSkillCritcalBonus(CCreature& creature, int skillId, bool applyDexBonus, double specialProbability);
double CalcPysicalSkillCriticalDamage(CCreature& creature, double damage);
double CalcCriticalPhysicalAttack(CCreature& creature, CSkillAction2* action, double damage, int specialProbability);
void CheckActionIsCritical(CSkillAction2* action);
double GetMagicResisted(bool fullResist, const ResistanceInfo& info);
bool CheckIsMagicHit(CCreature* attacker, CObject* target);
double ApplyDamageToTarget(CCreature* attacker, CObject* target, double damage, bool reflected);
bool CheckResisted(bool fullResist, const ResistanceInfo& info);
int CheckMagicResisted(
    CCreature& attacker,
    CObject& target,
    const MagicAttackerInfo& attackerInfo,
    const MagicTargetInfo& targetInfo,
    double magicLevel,
    double power,
    double& damage
);
double CalcFinalDamage(
    CCreature& attacker,
    CObject& target,
    const CSkillInfo* skillInfo,
    const MagicAttackerInfo& attackerInfo,
    const MagicTargetInfo& targetInfo,
    double power,
    double distance,
    double damage,
    double critical,
    bool checkResist
);
double GetMagicCriticalProb(CCreature& attacker, CObject& target, const CSkillInfo* skillInfo, double witBonus);
double GetMagicCritical(CCreature& attacker, CObject& target, const CSkillInfo* skillInfo, double witBonus);
double CalculateProbability(double probability, CCreature* creature, CCreature* target, const CSkillInfo* skillInfo);
double CalculateProbability(double probability, CCreature* creature, CNPC* target, const CSkillInfo* skillInfo);
double CalcFinalDamage(
    CCreature& attacker,
    CObject& target,
    const CSkillInfo* skillInfo,
    const MagicAttackerInfo& attackerInfo,
    const MagicTargetInfo& targetInfo,
    double power,
    double distance,
    double damage,
    bool checkResist
);
bool CalcAndApplyMagicAttackDamage(
    CCreature* attacker,
    CObject* target,
    const CSkillInfo* skillInfo,
    double distance,
    double power,
    bool useSoul,
    bool reflected,
    bool forceHit
);
bool CalcAndApplyMagicAttackRangeDamage(
    CCreature* attacker,
    CObject* target,
    const CSkillInfo* skillInfo,
    double distance,
    double power,
    double percent,
    bool reflected,
    bool useSoul,
    bool forceHit
);

struct CalculateDamageParam {
    CalculateDamageParam(
        CCreature* pSkillUser,
        CWorldObject* pTarget,
        CSkillAction2* pAction,
        const CSkillInfo* pSkillInfo,
        const double power,
        const double powerPercent,
        const int specialProb,
        const int ignoreDefence,
        const int physicalprob,
        const bool bUseSoul,
        const int saveHP
    ) :
        pSkillUser(pSkillUser),
        pTarget(pTarget),
        pAction(pAction),
        pSkillInfo(pSkillInfo),
        power(power),
        powerPercent(powerPercent),
        specialProb(specialProb),
        ignoreDefence(ignoreDefence),
        physicalprob(physicalprob),
        bUseSoul(bUseSoul),
        saveHP(saveHP) {}

    CCreature* pSkillUser;
    CWorldObject* pTarget;
    CSkillAction2* pAction;
    const CSkillInfo* pSkillInfo;
    double power;
    double powerPercent;
    int specialProb;
    int ignoreDefence;
    int physicalprob;
    bool bUseSoul;
    int saveHP;
};


static_assert(std::is_trivially_copyable_v<MagicAttackerInfo>);
static_assert(std::is_trivially_destructible_v<MagicAttackerInfo>);

static_assert(std::is_trivially_copyable_v<MagicTargetInfo>);
static_assert(std::is_trivially_destructible_v<MagicTargetInfo>);


static_assert(std::is_trivially_copyable_v<CalculateDamageParam>);
static_assert(std::is_trivially_destructible_v<CalculateDamageParam>);

double CalculateDamage(const CalculateDamageParam& parameters, bool useLevelBonus);

} // namespace L2SkillFunc

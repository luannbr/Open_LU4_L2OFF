#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CCreature;
class CItem;

namespace CheckEquipConditionItemType {

enum Enum : std::int32_t {
    CECIT_NORMAL = 0,
    CECIT_SHAPESHIFT = 1,
};

} // namespace CheckEquipConditionItemType

// The spelling is donor-selected source vocabulary. The current item parser
// and concrete condition constructors bind every type 1..27.
// zero is the same-source no-condition sentinel.
enum EquipConditionType : std::int32_t {
    ECT_NONE = 0,
    ECT_SEX = 1,
    ECT_RACE = 2,
    ECT_CLASSID = 3,
    ECT_CATEGORY = 4,
    ECT_AGIT = 5,
    ECT_SOME_AGIT = 6,
    ECT_CASTLE = 7,
    ECT_SOME_CASTLE = 8,
    ECT_CLAN_LEADER = 9,
    ECT_CLAN_LEVEL = 10,
    ECT_HERO = 11,
    ECT_NOBLESS = 12,
    ECT_CHAO = 13,
    ECT_ACADEMY = 14,
    ECT_SOCIAL_CLASS = 15,
    ECT_FORTRESS = 16,
    ECT_SOME_FORTRESS = 17,
    ECT_SUBJOB = 18,
    ECT_INZONE = 19,
    ECT_INZONE_NUM = 20,
    ECT_REQUIREDLEVEL = 21,
    ECT_TRANSMODE_INCLUDE = 22,
    ECT_TRANSMODE_EXCLUDE = 23,
    ECT_CASTLE_TYPE = 24,
    ECT_PLEDGE = 25,
    ECT_MAIN_DUAL_LEVEL = 26,
    ECT_JOB_GROUP = 27,
};

enum UseConditionType : std::int32_t {
    UCT_NONE = 0,
    UCT_IS_IN_RESIDENCE_SIEGE_FIELD = 1,
    UCT_RACE = 2,
    UCT_CLASSID = 3,
    UCT_CATEGORY = 4,
    UCT_INZONE = 5,
    UCT_INZONE_NUM = 6,
    UCT_REQUIREDLEVEL = 7,
    UCT_TRANSMODE_INCLUDE = 8,
    UCT_TRANSMODE_EXCLUDE = 9,
    UCT_LEVEL = 10,
    UCT_RESTART_POINT = 11,
    UCT_TRANSFORMID = 12,
    UCT_RESTORE_VITALITY = 13,
    UCT_SP = 14,
    UCT_ADENA = 15,
    UCT_PEACEZONE = 16,
    UCT_SUBJOB = 17,
    UCT_VITAL_ITEM_COUNT = 18,
    UCT_VITAL_POINT_REMAIN = 19,
};


// Exact ItemCondition.h equip-condition base prefix. The generated object
// parser owns the concrete condition families. This declaration keeps their
// common type discriminator at the current +0x08 boundary without emitting
// a partial base vtable.
class CItemEquipCondition {
public:
    CItemEquipCondition() : m_eType(ECT_NONE) {}

    explicit CItemEquipCondition(EquipConditionType type) : m_eType(type) {}

    virtual ~CItemEquipCondition();

    EquipConditionType GetConditionType() const { return m_eType; }

    bool CanEquip(CCreature* creature, CItem* item, CheckEquipConditionItemType::Enum itemType);
    virtual bool CheckPrivate(CCreature* creature, CItem* item);
    virtual void GetConditionList(std::vector<int>& conditions);

protected:
    bool CheckCommon(CCreature* creature, CItem* item);
    virtual bool CheckShapeShifted(CCreature* creature, CItem* item);

    EquipConditionType m_eType;
    std::byte m_targetAlignment0cTo10[0x04]{};
};


// Exact current ItemCondition.h base. Current vtable 0x0102b690 has three
// slots: scalar destruction, an always-true CheckPrivate default and a no-op
// GetConditionList default. The generated parser owns the concrete condition
// families. Keeping that separate does not change this runtime dispatch
// contract.
class CItemUseCondition {
public:
    CItemUseCondition() : m_eType(UCT_NONE) {}

    explicit CItemUseCondition(UseConditionType type) : m_eType(type) {}

    virtual ~CItemUseCondition();

    bool CanUse(CCreature* creature, CItem* item);

    UseConditionType GetConditionType() const { return m_eType; }

    virtual bool CheckPrivate(CCreature* creature, CItem* item);
    virtual void GetConditionList(std::vector<int>& conditions);

protected:
    bool CheckCommon(CCreature* creature, CItem* item);

    UseConditionType m_eType;
    std::byte m_targetAlignment0cTo10[0x04]{};
};


#define L2_DECLARE_USE_EMPTY(Name, Type)                                                                                    \
    class CItemUseCondition_##Name final : public CItemUseCondition {                                                       \
    public:                                                                                                                 \
        CItemUseCondition_##Name() : CItemUseCondition(Type) {}                                                             \
        bool CheckPrivate(CCreature* creature, CItem* item) override;                                                       \
    }

#define L2_DECLARE_USE_INT(Name, Type, Field)                                                                               \
    class CItemUseCondition_##Name final : public CItemUseCondition {                                                       \
    public:                                                                                                                 \
        explicit CItemUseCondition_##Name(int value) : CItemUseCondition(Type), Field(value) {}                             \
        bool CheckPrivate(CCreature* creature, CItem* item) override;                                                       \
                                                                                                                            \
    private:                                                                                                                \
        std::int32_t Field;                                                                                                 \
    }

#define L2_DECLARE_USE_INT_LIST(Name, Type, Field)                                                                          \
    class CItemUseCondition_##Name final : public CItemUseCondition {                                                       \
    public:                                                                                                                 \
        explicit CItemUseCondition_##Name(int value) : CItemUseCondition(Type), Field(value) {}                             \
        bool CheckPrivate(CCreature* creature, CItem* item) override;                                                       \
        void GetConditionList(std::vector<int>& conditions) override;                                                       \
                                                                                                                            \
    private:                                                                                                                \
        std::int32_t Field;                                                                                                 \
    }

#define L2_DECLARE_USE_VECTOR(Name, Type, Field)                                                                            \
    class CItemUseCondition_##Name final : public CItemUseCondition {                                                       \
    public:                                                                                                                 \
        explicit CItemUseCondition_##Name(std::vector<int>* values);                                                        \
        bool CheckPrivate(CCreature* creature, CItem* item) override;                                                       \
        void GetConditionList(std::vector<int>& conditions) override;                                                       \
                                                                                                                            \
    private:                                                                                                                \
        std::vector<int> Field;                                                                                             \
    }

L2_DECLARE_USE_EMPTY(is_in_residence_siege_field, UCT_IS_IN_RESIDENCE_SIEGE_FIELD);
L2_DECLARE_USE_VECTOR(race, UCT_RACE, m_vRace);
L2_DECLARE_USE_INT_LIST(classid, UCT_CLASSID, m_nClassID);
L2_DECLARE_USE_VECTOR(category, UCT_CATEGORY, m_vCategory);

class CItemUseCondition_inzone final : public CItemUseCondition {
public:
    explicit CItemUseCondition_inzone(bool value) : CItemUseCondition(UCT_INZONE), m_bIsInInZone(value) {}
    bool CheckPrivate(CCreature* creature, CItem* item) override;
    void GetConditionList(std::vector<int>& conditions) override;

private:
    bool m_bIsInInZone;
};

L2_DECLARE_USE_VECTOR(inzone_num, UCT_INZONE_NUM, m_vInZoneType);
L2_DECLARE_USE_INT_LIST(requiredlevel, UCT_REQUIREDLEVEL, m_nRequiredLevel);

class CItemUseCondition_transmode_include final : public CItemUseCondition {
public:
    CItemUseCondition_transmode_include();
    void SetData(std::vector<int>* values);
    bool CheckPrivate(CCreature* creature, CItem* item) override;
    void GetConditionList(std::vector<int>& conditions) override;

private:
    std::vector<int> m_vTransformMode;
};

class CItemUseCondition_transmode_exclude final : public CItemUseCondition {
public:
    CItemUseCondition_transmode_exclude();
    void SetData(std::vector<int>* values);
    bool CheckPrivate(CCreature* creature, CItem* item) override;
    void GetConditionList(std::vector<int>& conditions) override;

private:
    std::vector<int> m_vTransformMode;
};

class CItemUseCondition_level final : public CItemUseCondition {
public:
    CItemUseCondition_level(int startLevel, int endLevel) :
        CItemUseCondition(UCT_LEVEL), m_nStartLevel(startLevel), m_nEndLevel(endLevel) {}
    bool CheckPrivate(CCreature* creature, CItem* item) override;

private:
    std::int32_t m_nStartLevel;
    std::int32_t m_nEndLevel;
};

L2_DECLARE_USE_INT_LIST(restart_point, UCT_RESTART_POINT, m_nRestartPoint);
L2_DECLARE_USE_VECTOR(transformid, UCT_TRANSFORMID, m_vTransformID);
L2_DECLARE_USE_EMPTY(vital_restore, UCT_RESTORE_VITALITY);

class CItemUseCondition_sp final : public CItemUseCondition {
public:
    explicit CItemUseCondition_sp(std::int64_t value) : CItemUseCondition(UCT_SP), m_nSP(value) {}
    bool CheckPrivate(CCreature* creature, CItem* item) override;

private:
    std::int64_t m_nSP;
};

L2_DECLARE_USE_INT(adena, UCT_ADENA, m_nAdena);
L2_DECLARE_USE_EMPTY(peacezone, UCT_PEACEZONE);
L2_DECLARE_USE_EMPTY(subjob, UCT_SUBJOB);
L2_DECLARE_USE_EMPTY(vital_item_count, UCT_VITAL_ITEM_COUNT);
L2_DECLARE_USE_EMPTY(vital_point_remain, UCT_VITAL_POINT_REMAIN);

class CItemUseCondtion_Maxhp final : public CItemUseCondition {
public:
    CItemUseCondtion_Maxhp() : CItemUseCondition(UCT_NONE) {}
    bool CheckPrivate(CCreature* creature, CItem* item) override;
};

class CItemUseCondtion_Maxmp final : public CItemUseCondition {
public:
    CItemUseCondtion_Maxmp() : CItemUseCondition(UCT_NONE) {}
    bool CheckPrivate(CCreature* creature, CItem* item) override;
};

class CItemUseCondtion_Maxcp final : public CItemUseCondition {
public:
    CItemUseCondtion_Maxcp() : CItemUseCondition(UCT_NONE) {}
    bool CheckPrivate(CCreature* creature, CItem* item) override;
};

class CItemUseCondtion_PreventDuplicationABNS final : public CItemUseCondition {
public:
    CItemUseCondtion_PreventDuplicationABNS();
    void SetData(std::vector<int>* values);
    bool CheckPrivate(CCreature* creature, CItem* item) override;

private:
    std::vector<int> m_vUniqueABNS;
    [[maybe_unused]] std::int32_t m_nOption;
    [[maybe_unused]] bool m_bCurrentState;
};

class CItemUseCondtion_KalieEventBuff final : public CItemUseCondition {
public:
    CItemUseCondtion_KalieEventBuff() : CItemUseCondition(UCT_NONE), m_bCurrentState(false) {}
    bool CheckPrivate(CCreature* creature, CItem* item) override;

private:
    [[maybe_unused]] bool m_bCurrentState;
};

class CItemUseCondtion_VitalRestore final : public CItemUseCondition {
public:
    explicit CItemUseCondtion_VitalRestore(int value) : CItemUseCondition(UCT_NONE), m_nRestorepoint(value) {}
    bool CheckPrivate(CCreature* creature, CItem* item) override;

private:
    std::int32_t m_nRestorepoint;
};

class CItemUseCondtion_VitalRestore_Down final : public CItemUseCondition {
public:
    explicit CItemUseCondtion_VitalRestore_Down(int value) : CItemUseCondition(UCT_NONE), m_nRestorepointDown(value) {}
    bool CheckPrivate(CCreature* creature, CItem* item) override;

private:
    std::int32_t m_nRestorepointDown;
};

#define L2_DECLARE_EQUIP_INT(Name, Type, Field, WithList)                                                                   \
    class CItemEquipCondition_##Name final : public CItemEquipCondition {                                                   \
    public:                                                                                                                 \
        explicit CItemEquipCondition_##Name(int value) : CItemEquipCondition(Type), Field(value) {}                         \
        bool CheckPrivate(CCreature* creature, CItem* item) override;                                                       \
    WithList private : std::int32_t Field;                                                                                  \
    }

#define L2_EQUIP_LIST_DECL void GetConditionList(std::vector<int>& conditions) override;
#define L2_EQUIP_NO_LIST

#define L2_DECLARE_EQUIP_BOOL(Name, Type, Field)                                                                            \
    class CItemEquipCondition_##Name final : public CItemEquipCondition {                                                   \
    public:                                                                                                                 \
        explicit CItemEquipCondition_##Name(bool value) : CItemEquipCondition(Type), Field(value) {}                        \
        bool CheckPrivate(CCreature* creature, CItem* item) override;                                                       \
        void GetConditionList(std::vector<int>& conditions) override;                                                       \
                                                                                                                            \
    private:                                                                                                                \
        bool Field;                                                                                                         \
    }

#define L2_DECLARE_EQUIP_VECTOR(Name, Type, Field)                                                                          \
    class CItemEquipCondition_##Name final : public CItemEquipCondition {                                                   \
    public:                                                                                                                 \
        explicit CItemEquipCondition_##Name(std::vector<int>* values);                                                      \
        bool CheckPrivate(CCreature* creature, CItem* item) override;                                                       \
        void GetConditionList(std::vector<int>& conditions) override;                                                       \
                                                                                                                            \
    private:                                                                                                                \
        std::vector<int> Field;                                                                                             \
    }

L2_DECLARE_EQUIP_INT(sex, ECT_SEX, m_nSex, L2_EQUIP_LIST_DECL);
L2_DECLARE_EQUIP_VECTOR(race, ECT_RACE, m_vRace);
L2_DECLARE_EQUIP_INT(classid, ECT_CLASSID, m_nClassID, L2_EQUIP_LIST_DECL);
L2_DECLARE_EQUIP_VECTOR(category, ECT_CATEGORY, m_vCategory);
L2_DECLARE_EQUIP_BOOL(agit, ECT_AGIT, m_bHasAgit);
L2_DECLARE_EQUIP_VECTOR(some_agit, ECT_SOME_AGIT, m_vAgit);
L2_DECLARE_EQUIP_BOOL(castle, ECT_CASTLE, m_bHasCastle);
L2_DECLARE_EQUIP_VECTOR(some_castle, ECT_SOME_CASTLE, m_vCastle);
L2_DECLARE_EQUIP_BOOL(clan_leader, ECT_CLAN_LEADER, m_bIsClanLeader);
L2_DECLARE_EQUIP_INT(clan_level, ECT_CLAN_LEVEL, m_nClanLevel, L2_EQUIP_LIST_DECL);
L2_DECLARE_EQUIP_BOOL(hero, ECT_HERO, m_bIsHero);
L2_DECLARE_EQUIP_BOOL(nobless, ECT_NOBLESS, m_bIsNobless);
L2_DECLARE_EQUIP_BOOL(honors, ECT_NOBLESS, m_bIsNobless);
L2_DECLARE_EQUIP_BOOL(chao, ECT_CHAO, m_bIsChao);

class CItemEquipCondition_academy final : public CItemEquipCondition {
public:
    explicit CItemEquipCondition_academy(bool value) : CItemEquipCondition(ECT_ACADEMY), m_bIsAcademyMember(value) {}
    bool CheckPrivate(CCreature* creature, CItem* item) override;
    void GetConditionList(std::vector<int>& conditions) override;

protected:
    bool CheckShapeShifted(CCreature* creature, CItem* item) override;

private:
    bool m_bIsAcademyMember;
};

L2_DECLARE_EQUIP_INT(social_class, ECT_SOCIAL_CLASS, m_nSocialClass, L2_EQUIP_LIST_DECL);
L2_DECLARE_EQUIP_BOOL(fortress, ECT_FORTRESS, m_bHasFortress);
L2_DECLARE_EQUIP_VECTOR(some_fortress, ECT_SOME_FORTRESS, m_vFortress);
L2_DECLARE_EQUIP_INT(subjob, ECT_SUBJOB, m_nSubjobId, L2_EQUIP_LIST_DECL);
L2_DECLARE_EQUIP_BOOL(inzone, ECT_INZONE, m_bIsInInZone);
L2_DECLARE_EQUIP_VECTOR(inzone_num, ECT_INZONE_NUM, m_vInZoneType);
L2_DECLARE_EQUIP_INT(requiredlevel, ECT_REQUIREDLEVEL, m_nRequiredLevel, L2_EQUIP_LIST_DECL);

class CItemEquipCondition_transmode_include final : public CItemEquipCondition {
public:
    CItemEquipCondition_transmode_include();
    void SetData(std::vector<int>* values);
    bool CheckPrivate(CCreature* creature, CItem* item) override;
    void GetConditionList(std::vector<int>& conditions) override;

private:
    std::vector<int> m_vTransformMode;
};

class CItemEquipCondition_transmode_exclude final : public CItemEquipCondition {
public:
    CItemEquipCondition_transmode_exclude();
    void SetData(std::vector<int>* values);
    bool CheckPrivate(CCreature* creature, CItem* item) override;
    void GetConditionList(std::vector<int>& conditions) override;

private:
    std::vector<int> m_vTransformMode;
};

L2_DECLARE_EQUIP_INT(castle_type, ECT_CASTLE_TYPE, m_nCastleType, L2_EQUIP_LIST_DECL);
L2_DECLARE_EQUIP_INT(pledge_member, ECT_PLEDGE, m_nPledgeLevel, L2_EQUIP_LIST_DECL);
L2_DECLARE_EQUIP_INT(mainduallevel, ECT_MAIN_DUAL_LEVEL, m_MainDualLevel, L2_EQUIP_NO_LIST);
L2_DECLARE_EQUIP_INT(jobgroup, ECT_JOB_GROUP, m_JopGroup, L2_EQUIP_NO_LIST);

#undef L2_DECLARE_USE_EMPTY
#undef L2_DECLARE_USE_INT
#undef L2_DECLARE_USE_INT_LIST
#undef L2_DECLARE_USE_VECTOR
#undef L2_DECLARE_EQUIP_INT
#undef L2_DECLARE_EQUIP_BOOL
#undef L2_DECLARE_EQUIP_VECTOR
#undef L2_EQUIP_LIST_DECL
#undef L2_EQUIP_NO_LIST

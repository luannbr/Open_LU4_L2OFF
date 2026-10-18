#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include "Skill.h"

#include <cstddef>
#include <cstdint>

class CCreature;
class CItem;
class CUserSocket;
class User;
enum ObjectFieldType : std::int32_t;

namespace BuilderCmd {
bool set_henna_period(CUserSocket*, User*, char16_t*);
}

class CHennaCondition {
public:
    virtual ~CHennaCondition();
    virtual bool CheckCondition(CCreature* creature);
};

class CHennaCondition_is_in_battle_field final : public CHennaCondition {
public:
    bool CheckCondition(CCreature* creature) override;
};

class CHennaCondition_is_in_castle_war final : public CHennaCondition {
public:
    bool CheckCondition(CCreature* creature) override;
};

class CHennaCondition_hc_is_in_agit_war final : public CHennaCondition {
public:
    bool CheckCondition(CCreature* creature) override;
};

class CHennaCondition_is_in_fortress_siege final : public CHennaCondition {
public:
    bool CheckCondition(CCreature* creature) override;
};

class CHennaCondition_is_in_olympiad final : public CHennaCondition {
public:
    bool CheckCondition(CCreature* creature) override;
};

class CHennaCondition_is_in_pledge_game final : public CHennaCondition {
public:
    bool CheckCondition(CCreature* creature) override;
};


// Physical current henna definition. CHenna's only virtual owner is its
// deleting-destructor envelope. Parser/condition methods that are not needed
// by the lookup/use path remain honest declarations.
class CHenna {
public:
    CHenna();
    virtual ~CHenna();

    bool CanUse(int classId, CItem* item);
    bool CanWearClass(int classId);
    bool HasItem(CItem* item);
    bool IsPremiumHenna() const { return m_bIsPremiumHenna; }
    bool IsPeriodicHenna() const { return m_nPeriod >= 0; }
    bool CheckHennaCondition(CCreature* creature);
    void Set(ObjectFieldType field, int value);
    void Set(ObjectFieldType field, std::vector<int>* values);
    void Set(ObjectFieldType field, const wchar_t* value);

    void AddHennaCondition(CHennaCondition* condition) { m_vHennaCondition.push_back(condition); }

private:
    friend class CHennaDB;
    friend class User;
    friend bool BuilderCmd::set_henna_period(CUserSocket*, User*, char16_t*);

public:
    std::int32_t m_nClassId; // +0x08

private:
    std::vector<std::int32_t> m_vCanWearClass; // +0x10
    std::int32_t m_nItemClassId; // +0x30
    std::int32_t m_nHennaLevel; // +0x34
    CStats m_HennaBonus; // +0x38
    std::int64_t m_NeedCount; // +0x58
    std::int64_t m_WearFee; // +0x60
    std::int64_t m_CancelCount; // +0x68
    std::int64_t m_CancelFee; // +0x70
    bool m_bIsPremiumHenna; // +0x78
    std::uint8_t m_alignment79To7c[3]; // +0x79
    std::int32_t m_nPeriod; // +0x7c
    std::vector<CSkillKey> m_HennaUnequipSkillList; // +0x80
    std::vector<CHennaCondition*> m_vHennaCondition; // +0xa0
    std::vector<CSkillKey> m_HennaSkillList; // +0xc0
};

class CHennaDB {
public:
    CHennaDB();
    ~CHennaDB();

    void Add(CHenna* henna);
    CHenna* Find(int classId);
    void Load();

private:
    std::map<std::int32_t, CHenna*> m_mapHennaInfo; // +0x00
    std::map<std::int32_t, CHenna*> m_mapHennaInfoByItem; // +0x20
};

extern CHennaDB g_HennaDB;

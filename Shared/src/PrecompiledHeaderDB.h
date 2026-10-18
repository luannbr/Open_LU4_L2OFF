#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <source_location>
#include <string>


// Current L2Server.exe 0x009e7f44 and the matching same-source declarations.
int GetIntEx(std::FILE* stream);

// Current L2Server.exe 0x009e813c and current L2NPC.exe 0x0050acb4. Both
// shipping images use the widened PCH reader at their active Load boundary.
std::int64_t GetInt64Ex(std::FILE* stream);

// L2NPC.exe loads this exact 0x38-byte revision from skill_pch2.txt. The
// adjacent PDB describes the older 0x30-byte prefix. The shipping image
// widened m_skill_name_id, inserted m_is_hate and moved m_attribute.
struct CSkillInfoForL2Npc {
    std::int64_t m_skill_name_id = 0; // +0x00
    int m_cast_range = 0; // +0x08
    int m_hp = 0; // +0x0c
    int m_mp = 0; // +0x10
    int m_target_type = 0; // +0x14
    int m_effect_point = 0; // +0x18
    int m_abnormal_type = 0; // +0x1c
    int m_abnormal_level = 0; // +0x20
    int m_hit_cool_time = 0; // +0x24
    int m_reuse_delay = 0; // +0x28
    int m_is_magic = 0; // +0x2c
    int m_is_hate = 0; // +0x30
    int m_attribute = 0; // +0x34
};


// Shared/precompiledheaderdb.h. Current L2Server creates only the
// 0x20-byte name -> int64 map. Current L2NPC constructor 0x0050b718 appends
// the 0x20-byte skill-name-id -> raw CSkillInfoForL2Npc* map. The pointed
// L2NPC skill records have process lifetime. Neither duplicate insertion nor
// teardown deletes them.
class CPrecompiledHeaderDB {
public:
    CPrecompiledHeaderDB();
    ~CPrecompiledHeaderDB();

    std::int64_t Search(const char16_t* name) const;
    void Set(const char16_t* name, std::int64_t value);
    void Load(const wchar_t* directory, std::FILE* reportFile);
#if defined(L2_BUILD_L2NPC)
    void InsertPdb(CSkillInfoForL2Npc* skill);
    CSkillInfoForL2Npc* FindSkillPdb(
        std::int64_t skillNameId, std::source_location location = std::source_location::current()
    );

    int Skill_GetCastRange(std::int64_t skillNameId, bool& found);
    int Skill_GetConsumeHP(std::int64_t skillNameId, bool& found);
    int Skill_GetConsumeMP(std::int64_t skillNameId, bool& found);
    int Skill_GetTargetType(std::int64_t skillNameId, bool& found);
    int Skill_GetEffectPoint(std::int64_t skillNameId, bool& found);
    bool Skill_HaveAttribute(std::int64_t skillNameId, int attribute, bool& found);
    int Skill_GetAbnormalType(std::int64_t skillNameId, bool& found);
    int Skill_GetAbnormalLevel(std::int64_t skillNameId, bool& found);
    int Skill_GetReuseDelay(std::int64_t skillNameId, bool& found);
    int Skill_GetHitCoolTime(std::int64_t skillNameId, bool& found);
    int Skill_GetIsMagic(std::int64_t skillNameId, bool& found);
    int Skill_GetIsHate(std::int64_t skillNameId, bool& found);
#endif

private:
    using NameDB = std::map<std::u16string, std::int64_t>;
#if defined(L2_BUILD_L2NPC)
    using SkillInfoDB = std::map<std::int64_t, CSkillInfoForL2Npc*>;
#endif


    NameDB m_NameMap; // +0x00
#if defined(L2_BUILD_L2NPC)
    SkillInfoDB m_SkillInfoDB; // +0x20
#endif
};


// PDB S_GDATA32 and current PE initializer:
//   CPrecompiledHeaderDB g_PrecompiledHeaderDB
extern CPrecompiledHeaderDB g_PrecompiledHeaderDB;

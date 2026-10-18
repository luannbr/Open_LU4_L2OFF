#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

// Current AbilityData.xml record. The physical field order is fixed by the
// consumers in user_skill.cpp. Load() keeps the current producer's
// observable RequireSkillID/RequirePP assignment swap.
class PotentialSkillAcquireInfo {
public:
    std::int32_t nSkillID; // +0x00
    std::int32_t nSkillLevel; // +0x04
    std::int32_t nRequirePP; // +0x08
    std::int32_t nDepth; // +0x0c
    std::int32_t nRequireLev; // +0x10
    std::int32_t nRequireSkillId; // +0x14
    std::int32_t nRequireiSkillLev; // +0x18
    std::int32_t nRequireCount; // +0x1c
    std::int32_t etype; // +0x20
};

class CPotentialSkillDB {
public:
    using PotentialSkillAcquireInfos = std::map<std::int32_t, PotentialSkillAcquireInfo*>;

    static CPotentialSkillDB& Inst();

    PotentialSkillAcquireInfo* IsExistPotential(int skillId);
    void Load();

    PotentialSkillAcquireInfos m_mapPotentailSkillAcquireInfos; // +0x00
    std::int64_t m_nResetSP; // +0x20

    static CPotentialSkillDB* m_PotentialSkillDB;

private:
    CPotentialSkillDB() : m_nResetSP(0) {}
};

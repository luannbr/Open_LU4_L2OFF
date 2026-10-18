#pragma once

#include "BaseSpawnDefine.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

namespace l2npc {


// Recovered from l2npc/DelayedCreator.cpp. The original one-shot CIOObject
// keeps mpDefine while a delayed SendCreateNpcPacket is pending.
class DelayedCreator : public CIOObject {
public:
    DelayedCreator(
        BaseSpawnDefine* define,
        const char16_t* name,
        int count,
        int x,
        int y,
        int z,
        int direction,
        int hp,
        int mp,
        int dbValue,
        std::uint32_t inZoneId,
        int makerParam1,
        int makerParam2,
        int makerParam3
    );
    ~DelayedCreator() override;

    void TimerExpired(int timerId) override;

private:
    // The adjacent L2NPC PDB names the fields through m_nInZoneID. The
    // current executable extends that layout with the three maker parameters
    // at +0x6c, +0x70 and +0x74.
    BaseSpawnDefine* mpDefine = nullptr;
    std::u16string mName;
    int mCount = 0;
    int mX = 0;
    int mY = 0;
    int mZ = 0;
    int mDir = 0;
    int mHP = 0;
    int mMP = 0;
    int mDBValue = 0;
    std::uint32_t m_nInZoneID = 0;
    int m_nMakerParam1 = 0;
    int m_nMakerParam2 = 0;
    int m_nMakerParam3 = 0;
};


} // namespace l2npc

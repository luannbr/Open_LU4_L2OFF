#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>


class CSharedItemData;
class CSharedStaticObjectData;

// PhysicalState values stored in CDesire::m_PState.
enum PhysicalState : int {
    DESIRE_NONE = 0,
    DESIRE_MOVE_AROUND = 1,
    DESIRE_DO_NOTHING = 2,
    DESIRE_ATTACK = 3,
    DESIRE_CHASE = 4,
    DESIRE_FLEE = 5,
    DESIRE_GET_ITEM = 6,
    DESIRE_FOLLOW = 7,
    DESIRE_DECAYING = 8,
    DESIRE_MOVE_WAYPOINT = 9,
    DESIRE_USE_SKILL = 10,
    DESIRE_MOVE_TO = 11,
    DESIRE_EFFECT_ACTION = 12,
    DESIRE_EFFECT_ACTION2 = 13,
    DESIRE_PET_DEFAULT = 14,
    DESIRE_MOVE_TO_TARGET = 15,
    DESIRE_MOVE_SUPER_PT = 16,
    DESIRE_MOVE_FREEWAY = 17,
    DESIRE_MOVE_FORMATION = 18,
};

// Exact desire.h enum from the adjacent PDB. Current machine code still
// stores it as a four-byte value and emits this type in factory signatures.
enum NpcAttackMoveType : int {
    AMT_STAND = 0,
    AMT_MOVE_TO_TARGET = 1,
};

enum DesireFlag : std::uint32_t {
    DF_DEFAULT = 0,
    DF_ONE_TIME_METHOD = 0x20000000U,
    DF_NOT_INTERRUPTABLE = 0x40000000U,
};

inline constexpr float MAX_DESIRE = 3.3e30F;

// Current desire.cpp uses a 200-byte CDesire. The adjacent donor PDB describes
// the older 192-byte revision. The shipping binary added the field at +0x58
// and shifted every following field by eight bytes.
class alignas(8) CDesire {
public:
    struct MoveSuperPointContext {
        int m_Revision; // +0x00
        int m_StartIndex; // +0x04
        int m_DestIndex; // +0x08
        int m_DestPointIndex; // +0x0c
        int m_WaitedTime; // +0x10
        int m_Direction; // +0x14
        int m_Order; // +0x18
    };

    explicit CDesire(PhysicalState state = DESIRE_NONE);
    CDesire(const CDesire& other);
    CDesire& operator=(const CDesire& other);
    bool operator==(const CDesire& other) const;
    bool operator!=(const CDesire& other) const;
    bool operator<(const CDesire& other) const;

    [[nodiscard]] bool IsInterruptable() const { return (flags & DF_NOT_INTERRUPTABLE) == 0; }

    [[nodiscard]] bool IsOneTimeAction() const { return (flags & DF_ONE_TIME_METHOD) != 0; }

    void Report(std::u16string& output) const;

    PhysicalState type; // +0x00 m_PState
    int creatureId; // +0x04
    CSharedStaticObjectData* staticObject; // +0x08 m_spStaticObject
    CSharedItemData* item; // +0x10 m_spItem
    int targetId; // +0x18
    int levelDifference; // +0x1c
    const char16_t* name; // +0x20
    MoveSuperPointContext // +0x28
        m_MoveSuperPointContext;
    int id; // +0x44
    int uniqueId; // +0x48
    int formationId; // +0x4c
    int formationIndex; // +0x50
    // m_nMethod is not initialized by the target constructor
    // and is skipped by CDesire::operator=.
    int method; // +0x54 m_nMethod
    std::int64_t contextValue; // +0x58
    std::uint32_t deadlineTick; // +0x60
    NpcAttackMoveType attackMoveType; // +0x64 m_amt
    std::vector<FVector>* waypoints; // +0x68 m_pWayPoints
    std::vector<int>* waypointDelays; // +0x70 m_pWayPointDelays
    int waypointIndex; // +0x78
    float boost; // +0x7c
    bool boosted; // +0x80
    std::array<std::uint8_t, 3> pad81;
    int boostCount; // +0x84
    bool force; // +0x88
    bool firedByServer; // +0x89
    std::array<std::uint8_t, 6> pad8a;
    FVector position; // +0x90
    float value; // +0xa8
    std::uint32_t flags; // +0xac
    int range; // +0xb0
    bool absolute; // +0xb4
    std::array<std::uint8_t, 3> padb5;
    int followType; // +0xb8
    int followData1; // +0xbc
    int followData2; // +0xc0
    int targetCreatureId; // +0xc4
};

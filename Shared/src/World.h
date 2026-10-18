#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/Log.h"
#include "Shared/src/Obj.h"

#include <cstddef>
#include <cstdint>

// The spelling is donor-selected source vocabulary. The current CWorld
// dispatcher has one distinct branch for each value in the complete domain.
enum ReportType : std::int32_t {
    RPT_CREATURE = 0,
    RPT_USER = 1,
    RPT_NPC = 2,
    RPT_ITEM = 3,
};


class CCreature;
class CGeoData;
class User;
struct ChargeData;


namespace l2geo {

// Shared/world.h. The current executable keeps the MemoryObject base at
// +0x00, m_IsTestWorld at +0x18 and m_geo at +0x20.
class CWorld : public ::MemoryObject {
public:
    explicit CWorld(bool isTestWorld);
    ~CWorld() override;

    static CWorld& Inst();
    void InitGeo();
    bool InRange(FVector origin, FVector destination, int range);

    // This method is inline in the original world.h and occupies vtable slot
    // +0x20 after MemoryObject's four virtual entries.
    virtual CGeoData* GetGeo() {
        if (!m_geo) {
            // Log.Add(
            //     LOG_ERROR,
            //     "NULL m_geo at file[%s], line[%d]",
            //     "c:\\work\\lineage2build\\server\\main\\shared\\world.h",
            //     0xd4)
            // Log.Add(
            //     LOG_ERROR,
            //     "[geodata] Geodata was requested but no geodata object is available. Returning null. Check geodata
            //     initialization.")
            L2_LOG_ERROR(
                Log,
                "[geodata] Geodata was requested but no geodata object is available; returning null. Check geodata "
                "initialization."
            );
        }
        return m_geo;
    }

    bool IsValidPosition(int x, int y);
    bool IsValidPosition(const FVector& position);
    bool CanSummonNpc(const FVector& position, User* user);
    bool CanGo(
        const FVector& source,
        FVector& destination,
        FVector& collision,
        bool& collided,
        double& travelledDistance,
        int layer,
        bool flying
    );
    bool CanGo(const FVector& source, FVector& destination, int layer, bool flying);
    bool GetValidJumpPos(
        const FVector& source,
        const FVector& destination,
        ChargeData& chargeData,
        const double distance,
        int layer,
        bool flying
    );
    bool FindAcceptableDestZPos(const FVector& source, FVector& destination, bool flying);
    bool GetValidPositionAroundPos(
        const CCreature& creature,
        const int radius,
        const double collisionRadius,
        const FVector& source,
        FVector& destination
    );
    static bool IsInGracia(int positionX);
    static bool IsNotInGracia(int positionX);
    bool IsTestWorld();

protected:
    bool m_IsTestWorld;
    CGeoData* m_geo;
};


} // namespace l2geo

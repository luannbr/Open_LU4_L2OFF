#pragma once

#include "CMoveToTargetShareData.h"
#include "Object.h"

#include <cstddef>

class CMoveRelativeAction;


// Exact current L2Server world-object prefix. It is distinct from the
// main Shared l2geo::CWorld and owns only the relative-position vector.
class CWorldObject : public CObject {
public:
    CWorldObject();
    CWorldObject(const CWorldObject& other);
    ~CWorldObject() override;

    L2_DECLARE_OBJECT_REGISTRATION(CWorldObject)

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool SetTargetable(bool targetable, bool flag) override;
    bool IsTargetable() override;
    bool IsShowNametag() override;
    double GetCollisionRadius() const override;
    bool IsWorldObject() override;
    CWorldObject* CastWorldObject() override;

    virtual void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId);
    virtual FVector ConvRelToAbs(FVector position) const;
    virtual FVector ConvAbsToRel(FVector position) const;
    virtual void SetCollisionHeight(double height);
    virtual double GetCollisionHeight() const;
    virtual void SetCollisionRadius(double radius);
    virtual bool IsActive();
    virtual FVector GetPos() const = 0;
    virtual void SetDir(const FAngle& direction);
    virtual FVector GetPosAsFollowTarget() const;

    bool IsInRange(CWorldObject* object, double range);
    bool IsInRange(CWorldObject* object, int range, bool ignoreZone);
    FVector GetRelPos() const;
    void SetRelPos(const FVector& position);


protected:
    FVector m_RelPos;
};


// Exact current no-state carrier layer shared by CVehicle, CShuttle and the
// CAirShip family. Current vtable 0109a868 differs from CWorldObject only in
// Release/deleting teardown, the query/cast pair and the new pure slot 163.
class CAbstractCarrier : public CWorldObject {
public:
    CAbstractCarrier();
    CAbstractCarrier(const CAbstractCarrier& other);
    ~CAbstractCarrier() override = default;

    L2_DECLARE_OBJECT_REGISTRATION(CAbstractCarrier)

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool IsAbstractCarrier() override;
    CAbstractCarrier* CastAbstractCarrier() override;

    virtual bool SetMovingIntoCarrier(CMoveRelativeAction* action) = 0;
};

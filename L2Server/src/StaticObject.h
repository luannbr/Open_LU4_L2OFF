#pragma once

#include "CWorldObject.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>


class CUserSocket;
class CPendingStaticObjects;

// Exact current L2Server static-object envelope. The shared record and lock
// stripe are borrowed from the product CStaticObjectFactory. Neither pointer
// owns storage. RefHelpers is the main Shared 36-record owner.
class CStaticObject : public CWorldObject {
public:
    CStaticObject();
    CStaticObject(const CStaticObject& other);
    ~CStaticObject() override;

    L2_DECLARE_OBJECT_REGISTRATION(CStaticObject)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    bool SetTargetable(bool targetable, bool flag) override;
    bool IsTargetable() override;
    double GetCollisionRadius() const override;
    FAngle GetDir() const override;
    int GetIndex() const override;
    CObject* Copy() override;
    bool IsStaticObject() override;
    CStaticObject* CastStaticObject() override;
    bool IsInVehicle() override;
    bool IsInAirShip() override;
    bool IsInShuttle() override;
    unsigned int GetMyAirShipID() override;
    FVector GetMeleeAttackPosition(CCreature* attacker) override;
    void SetCollisionHeight(double height) override;
    double GetCollisionHeight() const override;
    void SetCollisionRadius(double radius) override;
    FVector GetPos() const override;
    void SetDir(const FAngle& direction) override;

    virtual void Delete();
    virtual void OnCreate();
    virtual void SetPos(const FVector& position);
    virtual double AttackedBy(CCreature* attacker, double damage);
    virtual double GotDamageBy(CCreature* attacker, double damage);
    virtual void SetHP(double hp);
    virtual double GetHP() const;
    virtual double GetMDefend();
    virtual double GetPDefend();
    virtual void SetBreakable(bool breakable);
    virtual void Interact(CCreature* creature);

    void DecideDistanceAttackable(CMoveToTargetShareData* data, CSmartID creatureId, int distance);
    void SetOrgTargetable(bool targetable);
    void SendStaticObjectInfo(CUserSocket* socket);
    void PushToThreadLocalMap();
    void PopFromThreadLocalMap();

    static CStaticObject* GetObjectW(std::uint32_t smartId);
    static CSPointer<CStaticObject> FindObjectSP(std::uint32_t smartId);


    // Current CWorld and DB receiver bodies access these original public
    // members directly. TotalChecker's field records keep
    // the same public access boundary.
    RefHelpers m_RefHelpers;
    CSharedStaticObjectData* m_data;
    ::CYieldLock* m_lock;

private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CStaticObject)
};

using CStaticObjectSmartIdManager = CSmartIdManager<CStaticObject, 10001, 16>;

// TotalChecker supplies only this source spelling. The current stripped
// image proves the single process-global specialization and
// all of its parameters/layout.
extern CStaticObjectSmartIdManager g_StaticObjectSmartIdManager;

// Current StaticObject.cpp process-global pending publication queue. Static
// objects created while world data is loading are registered here and are
// published in one locked pass when loading completes.
class CPendingStaticObjects {
public:
    CPendingStaticObjects();
    ~CPendingStaticObjects();

    void AppearInWorld();
    void RegisterAsPending(CStaticObject* object);

private:
    bool m_bLoading; // +0x00
    [[maybe_unused]] std::byte m_alignment01To04[0x03];
    CYieldLock m_Lock; // +0x04
    [[maybe_unused]] std::byte m_alignment14To18[0x04];
    std::vector<CStaticObject*> m_Pendings; // +0x18
};

extern CPendingStaticObjects g_PendingStaticObjects;

#include "AirShipPart.h"
#include "Shared/src/MemorySub.h"

#include "AirShip.h"
#include "CWorld.h"

#include "Shared/src/Exception.h"

#include <atomic>


void CAirShipPart::LeaveWorld(CAirShip* airShip) {
    L2_GUARD_FUNCTION();
    m_bLeft = true;
    CWorld::Inst().UnLoadStaticObject(this, airShip);
}

CAirShipSteering::CAirShipSteering(CAirShip* airShip) : m_spAirShip(static_cast<CObject*>(airShip)), m_alignment4ecTo4f0{} {
    L2_GUARD_FUNCTION();
    m_data->m_dCollisionRadius = 8.0;
    m_data->m_dCollisionHeight = 25.0;
    m_data->m_nEditorID = 5;
    CStaticObject::OnCreate();
}

CAirShipSteering::~CAirShipSteering() { g_ServerContext.AirShipSteering.fetch_sub(1, std::memory_order_relaxed); }

L2_DEFINE_OBJECT_CLASS_ID(CAirShipSteering)

L2_DEFINE_OBJECT_CLASS_NAME(CAirShipSteering, m_pClassName_CAirShipSteering)

L2_DEFINE_OBJECT_ALLOCATOR(CAirShipSteering, __GetClassName())

L2_DEFINE_OBJECT_NEW(CAirShipSteering)

L2_DEFINE_OBJECT_RELEASE(CAirShipSteering)

bool CAirShipSteering::IsAirShipSteering() { return true; }

CAirShipSteering* CAirShipSteering::CastAirShipSteering() { return this; }

void CAirShipSteering::Delete() {
    L2_GUARD_FUNCTION();
    m_bDeleting = true;
    CStaticObject::Delete();
}

void CAirShipSteering::Interact(CCreature*) { L2_GUARD_FUNCTION(); }

CAirShip* CAirShipSteering::GetAirShip() const { return m_spAirShip.GetAirShip(); }

#include "EventController.h"
#include "Shared/src/MemorySub.h"

#include "CWorld.h"
#include "UserSocket.h"

#include "Shared/src/Exception.h"

#include <atomic>


CEventController::CEventController() {
    m_data->m_bIsOpen = false;
    CStaticObject::OnCreate();
}

CEventController::CEventController(const CEventController& other) : CStaticObject(other), m_bIsOn(other.m_bIsOn) {
    SetPos(other.GetPos());
    CStaticObject::OnCreate();
}

CEventController::~CEventController() { g_ServerContext.EventController.fetch_sub(1, std::memory_order_relaxed); }

L2_DEFINE_OBJECT_CLASS_ID(CEventController)

L2_DEFINE_OBJECT_CLASS_NAME(CEventController, m_pClassName_CEventController)

L2_DEFINE_OBJECT_ALLOCATOR(CEventController, __GetClassName())

L2_DEFINE_OBJECT_NEW(CEventController)

L2_DEFINE_OBJECT_TYPED_RELEASE(CEventController)

CObject* CEventController::Copy() { return new CEventController(*this); }

bool CEventController::IsEventController() { return true; }

CEventController* CEventController::CastEventController() { return this; }

void CEventController::Delete() {
    L2_GUARD_FUNCTION();
    SetOnOff(false);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
    CStaticObject::Delete();
}

void CEventController::EstablishEventController(int eventId, FVector position) {
    L2_GUARD_FUNCTION();
    m_data->m_nEditorID = eventId;
    SetPos(position);
}

void CEventController::SetOnOff(bool on) {
    L2_GUARD_FUNCTION();
    if (m_data->m_bIsOpen == on) {
        return;
    }

    m_data->m_bIsOpen = on;
    if (on) {
        CWorld::Inst().StaticObjectAppear(this, GetPos());
    } else {
        CWorld::Inst().StaticObjectDisappear(this);
    }
}

void CEventController::SendEventControllerInfo(CUserSocket* socket, bool isInsight) {
    L2_GUARD_FUNCTION();
    if (socket != nullptr) {
        socket->Send("cdc", 0xcf, m_data->m_nEditorID, isInsight && m_data->m_bIsOpen);
    }
}

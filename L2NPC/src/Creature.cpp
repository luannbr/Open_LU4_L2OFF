#include "L2NPC/src/Creature.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/TemplateHelper.h"


// Current L2NPC.exe data object 0x007239e0. The exact NASC64_2015 PDB places
// the same global at l2npc/Creature.cpp:7.
CSmartIdManager<CCreature, 120000, 8> g_CreatureSmartIdManager;

CCreature::CCreature() : m_data(nullptr) { L2_GUARD_FUNCTION(); }

CCreature::~CCreature() {
    L2_GUARD_FUNCTION();
    m_data = nullptr;
}

bool CCreature::Delete() {
    L2_GUARD_FUNCTION();
    if (!CObject::Delete()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid CCreautre::Delete",
        //     __WFILE__,
        //     0x28)
        // Log.Add(LOG_ERROR,
        //     "[object lifecycle] Duplicate deletion of creature %u. This call will not remove it from the object registry.
        //     Dumping call stack.", static_cast<unsigned int>(Id()))
        L2_LOG_ERROR(
            Log,
            "[object lifecycle] Duplicate deletion of creature %u; this call will not remove it from the object registry. "
            "Dumping call stack.",
            static_cast<unsigned int>(Id())
        );
        ReportCallStack(LOG_ERROR, false);
        return false;
    }
    g_CreatureSmartIdManager.DeleteObject(m_nId);
    return true;
}

bool CCreature::IsVisible() const { return !m_data->m_bGMInvisible && m_data->m_StatusFlag[2]; }

int CCreature::IsTargetable() const {
    L2_GUARD_FUNCTION();
    if (!m_data->m_StatusFlag[0]) {
        return 0;
    }
    return IsVisible();
}

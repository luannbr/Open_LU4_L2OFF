#include "TeamBattleFlag.h"
#include "Shared/src/MemorySub.h"

#include "CWorld.h"
#include "NPC.h"
#include "ObjectDB.h"
#include "ThreadLocalWorldMap.h"
#include "UserSocket.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#include <atomic>
#include <cstdint>
#include <string>


CTeamBattleFlag::CTeamBattleFlag(wchar_t* className, FVector position, FAngle direction) {
    L2_GUARD_FUNCTION();

    const std::u16string targetClassName = target_utf16::FromRecoveredWindowsWide(className);
    m_nClassId = g_ObjectDB.GetClassIdFromName(targetClassName.c_str(), false);

    CObject* const prototype = g_ObjectDB.GetObjectW(m_nClassId, false);
    if (!prototype) {
        // Log.Add(LOG_ERROR, "Flag not found %d", m_nClassId)
        // L2_LOG_ERROR(Log, "Flag not found %d", m_nClassId)
        L2_LOG_ERROR(
            Log, "Team battle flag template '%s' (class ID %d) is missing; flag initialization stops.",
            Utf16ToUtf8(targetClassName).c_str(), m_nClassId
        );
        return;
    }

    if (prototype->IsNPC()) {
        CNPC* const npc = prototype->CastNPC();
        if (npc) {
            m_data->m_dCollisionRadius = npc->m_data->m_dCollisionRadius;
            m_data->m_dCollisionHeight = npc->m_data->m_dCollisionHeight;
        }
    }

    m_data->SetPos(position);
    m_data->SetDir(direction);
    CStaticObject::OnCreate();
}

CTeamBattleFlag::CTeamBattleFlag(const CTeamBattleFlag& other) : CStaticObject(other) {
    L2_GUARD_FUNCTION();
    m_nClassId = other.m_nClassId;
    CStaticObject::OnCreate();
}

CTeamBattleFlag::~CTeamBattleFlag() { g_ServerContext.TeamBattleFlag.fetch_sub(1, std::memory_order_relaxed); }

L2_DEFINE_OBJECT_CLASS_ID(CTeamBattleFlag)

L2_DEFINE_OBJECT_CLASS_NAME(CTeamBattleFlag, m_pClassName_CTeamBattleFlag)

L2_DEFINE_OBJECT_ALLOCATOR(CTeamBattleFlag, __GetClassName())

L2_DEFINE_OBJECT_NEW(CTeamBattleFlag)

L2_DEFINE_OBJECT_TYPED_RELEASE(CTeamBattleFlag)

CStaticObject* CTeamBattleFlag::Copy() { return new CTeamBattleFlag(*this); }

bool CTeamBattleFlag::IsTeamBattleFlag() { return true; }

CTeamBattleFlag* CTeamBattleFlag::CastTeamBattleFlag() { return this; }

void CTeamBattleFlag::Appear() {
    L2_GUARD_FUNCTION();
    CWorld::Inst().StaticObjectAppear(this, GetPos());
}

void CTeamBattleFlag::Disappear() {
    L2_GUARD_FUNCTION();
    CWorld::Inst().StaticObjectDisappear(this);
}

void CTeamBattleFlag::SendTeamBattleFlagInfo(CUserSocket* socket) {
    L2_GUARD_FUNCTION();

    constexpr const char* format = "cddSdddddffffdddd";
    if (!socket) {
        g_ptlUserMap->BroadcastToNeighbor(
            *this, 0x80000, GetPos(), format, 0x92, m_nId, m_nClassId, u"", 0, static_cast<int>(GetPos().x),
            static_cast<int>(GetPos().y), static_cast<int>(GetPos().z), m_data->GetDirByInt() & 0xffff, 1.0, 1.0,
            m_data->m_dCollisionRadius, m_data->m_dCollisionHeight, 0, 0, 0, 0
        );
        return;
    }

    socket->Send(
        format, 0x92, m_nId, m_nClassId, u"", 0, static_cast<int>(GetPos().x), static_cast<int>(GetPos().y),
        static_cast<int>(GetPos().z), m_data->GetDirByInt() & 0xffff, 1.0, 1.0, m_data->m_dCollisionRadius,
        m_data->m_dCollisionHeight, 0, 0, 0, 0
    );
}

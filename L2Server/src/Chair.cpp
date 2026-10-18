#include "Chair.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/TextRuntime.h"

#include "CWorld.h"
#include "Creature.h"
#include "Party.h"
#include "Setting.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/SharedFactory.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace {


std::int32_t SignedSmartId(CSmartID smartId) noexcept {
    const std::uint32_t bits = smartId.Sid();
    std::int32_t result = 0;
    static_assert(sizeof(result) == sizeof(bits));
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::uint32_t UnsignedSmartId(std::int32_t smartId) noexcept {
    std::uint32_t result = 0;
    static_assert(sizeof(result) == sizeof(smartId));
    std::memcpy(&result, &smartId, sizeof(result));
    return result;
}

} // namespace

CChairManager g_ChairManager;

CChairManager::CChairManager() : m_GroupLocks{} {}

CChairManager::~CChairManager() = default;

void CChairManager::RegisterGroupChair(const int groupId, CChair* const chair) {
    L2_GUARD_FUNCTION();
    if (chair == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GroupChair][%s][%d] invalid chair try regist, id[%d]",
        //     L"Chair.cpp",
        //     0x91,
        //     groupId)
        // L2_LOG_ERROR(Log,
        //     L"[GroupChair][%s][%d] invalid chair try regist, id[%d]",
        //     L"Chair.cpp",
        //     0x91,
        //     groupId)
        L2_LOG_ERROR(Log, "Cannot register a null chair in group %d; registration is skipped.", groupId);
        return;
    }

    const auto found = m_ChairGroupMap.find(groupId);
    if (found == m_ChairGroupMap.end()) {
        auto* const chairs = new ChairList;
        chairs->push_back(chair);
        m_ChairGroupMap[groupId] = chairs;
        return;
    }
    found->second->push_back(chair);
}

bool CChairManager::AssignChairOwnerGroup(const int groupId, const int ownerGroupId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_GroupLocks[groupId % 4]);
    const auto group = m_ChairGroupMap.find(groupId);
    if (group == m_ChairGroupMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GroupChair][%s][%d] invalid assign [%d]",
        //     L"Chair.cpp",
        //     0xcc,
        //     groupId)
        // L2_LOG_ERROR(Log,
        //     L"[GroupChair][%s][%d] invalid assign [%d]",
        //     L"Chair.cpp",
        //     0xcc,
        //     groupId)
        L2_LOG_ERROR(
            Log, "Cannot assign owner group %d to chair group %d: the chair group is not registered; assignment failed.",
            ownerGroupId, groupId
        );
        return false;
    }
    if (group->second == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GroupChair][%s][%d] null group [%d]",
        //     L"Chair.cpp",
        //     0xc6,
        //     groupId)
        // L2_LOG_ERROR(Log,
        //     L"[GroupChair][%s][%d] null group [%d]",
        //     L"Chair.cpp",
        //     0xc6,
        //     groupId)
        L2_LOG_ERROR(
            Log, "Cannot assign owner group %d to chair group %d: the registered chair list is null; assignment failed.",
            ownerGroupId, groupId
        );
        return false;
    }

    for (CChair* const chair : *group->second) {
        if (chair == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[GroupChair][%s][%d] null [%d]",
            //     L"Chair.cpp",
            //     0xbe,
            //     groupId)
            // L2_LOG_ERROR(Log,
            //     L"[GroupChair][%s][%d] null [%d]",
            //     L"Chair.cpp",
            //     0xbe,
            //     groupId)
            L2_LOG_ERROR(
                Log,
                "Assigning owner group %d to chair group %d stopped at a null chair; assignment failed and earlier "
                "assignments are retained.",
                ownerGroupId, groupId
            );
            return false;
        }
        chair->m_nOccupiedGroupId = ownerGroupId;
    }
    return true;
}

void CChairManager::ClearChairGroup(const int groupId, const int x, const int y, const int z) {
    L2_GUARD_FUNCTION();
    if (groupId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GroupChair][%s][%d] inalid groupid [%d]",
        //     L"Chair.cpp",
        //     0xdb,
        //     groupId)
        // L2_LOG_ERROR(Log,
        //     L"[GroupChair][%s][%d] inalid groupid [%d]",
        //     L"Chair.cpp",
        //     0xdb,
        //     groupId)
        L2_LOG_ERROR(Log, "Cannot clear chair group %d: the group ID must be positive; clearing is skipped.", groupId);
        return;
    }

    L2_GUARD(m_GroupLocks[groupId % 4]);
    const auto group = m_ChairGroupMap.find(groupId);
    if (group == m_ChairGroupMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GroupChair][%s][%d] invalid assign [%d]",
        //     L"Chair.cpp",
        //     0x116,
        //     groupId)
        // L2_LOG_ERROR(Log,
        //     L"[GroupChair][%s][%d] invalid assign [%d]",
        //     L"Chair.cpp",
        //     0x116,
        //     groupId)
        L2_LOG_ERROR(Log, "Cannot clear chair group %d: the group is not registered; clearing is skipped.", groupId);
        return;
    }
    if (group->second == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GroupChair][%s][%d] null group [%d]",
        //     L"Chair.cpp",
        //     0x111,
        //     groupId)
        // L2_LOG_ERROR(Log,
        //     L"[GroupChair][%s][%d] null group [%d]",
        //     L"Chair.cpp",
        //     0x111,
        //     groupId)
        L2_LOG_ERROR(Log, "Cannot clear chair group %d: the registered chair list is null; clearing is skipped.", groupId);
        return;
    }

    for (CChair* const chair : *group->second) {
        if (chair == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[GroupChair][%s][%d] null [%d]",
            //     L"Chair.cpp",
            //     0x10a,
            //     groupId)
            // L2_LOG_ERROR(Log,
            //     L"[GroupChair][%s][%d] null [%d]",
            //     L"Chair.cpp",
            //     0x10a,
            //     groupId)
            L2_LOG_ERROR(
                Log,
                "Clearing chair group %d stopped at a null chair; remaining chairs are not processed and earlier changes "
                "are retained.",
                groupId
            );
            break;
        }

        L2_GUARD(*chair->m_lock);
        chair->m_nOccupiedGroupId = -1;

        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(UnsignedSmartId(chair->m_nOccupiedBy));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            continue;
        }
        if (!user->IsInRealWorld() || !user->IsAlive()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[GroupChair] User is not realworld [%s]",
            //     user->GetName())
            // Noise: Dead or out-of-world occupants are expected during group cleanup. They are skipped without teleporting.
            // L2_LOG_ERROR(Log,
            //     L"[GroupChair] User is not realworld [%s]",
            //     user->GetName())
            continue;
        }

        FVector position(x, y, z);
        if ((x == 0 && y == 0 && z == 0) || !CWorld::Inst().IsValidPosition(x, y)) {
            position = Singleton<CSetting>::inst().GetRestartPoint(
                user->m_data->m_Race, user->GetPos(),
                user->IsEinhasadeTransformed() || user->m_data->m_nKarma < 0, user->m_data->m_nPKCount
            );
        }
        if (!user->TeleportToLocation(
                runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                runtime::Win64CvttDoubleToInt32(position.z), 0, 0x10000, TeleportSourceType::InzoneFailToEnter, nullptr, 0
            )) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[GroupChair] User Teleport fail [%s]",
            //     user->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[GroupChair] User Teleport fail [%s]",
            //     user->GetName())
            // L2_LOG_ERROR(Log,
            //     L"Clearing chair group %d: teleport failed for player '%ls' to (%d, %d, %d). Processing continues with the
            //     next chair.", groupId, user->GetName(), runtime::Win64CvttDoubleToInt32(position.x),
            //     runtime::Win64CvttDoubleToInt32(position.y),
            //     runtime::Win64CvttDoubleToInt32(position.z))
            L2_LOG_ERROR(
                Log,
                u"Clearing chair group %d: teleport failed for player '%ls' to (%d, %d, %d); processing continues with the "
                u"next chair.",
                groupId, user->GetName(), runtime::Win64CvttDoubleToInt32(position.x),
                runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z)
            );
        }
    }
}

bool CChairManager::CheckInzoneCondition(User* const user, CChair* const chair, const int type) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || chair == nullptr || static_cast<std::int32_t>(user->GetServerId()) != chair->m_nOccupiedBy) {
        return false;
    }
    if (type != 1) {
        return false;
    }

    CParty* const party = user->GetParty();
    if (party == nullptr) {
        return false;
    }

    const int groupId = chair->m_nGroupId;
    L2_GUARD(m_GroupLocks[groupId % 4]);
    const auto group = m_ChairGroupMap.find(groupId);
    if (group == m_ChairGroupMap.end() || group->second == nullptr) {
        return false;
    }

    for (CChair* const candidate : *group->second) {
        if (candidate == nullptr || candidate->m_nOccupiedGroupId != static_cast<std::int32_t>(party->Id()) ||
            candidate->m_nGroupType != 1) {
            return false;
        }
        if (candidate->Id() == chair->Id()) {
            return true;
        }
    }
    return false;
}

CChair::CChair() : m_nGroupId(-1), m_nGroupType(0), m_nOccupiedBy(0), m_nOccupiedGroupId(0), m_dSitRange(100.0) {
    CStaticObject::OnCreate();
}

CChair::~CChair() { g_ServerContext.Chair.fetch_sub(1, std::memory_order_relaxed); }

L2_DEFINE_OBJECT_CLASS_ID(CChair)

L2_DEFINE_OBJECT_CLASS_NAME(CChair, m_pClassName_CChair)

L2_DEFINE_OBJECT_ALLOCATOR(CChair, __GetClassName())

L2_DEFINE_OBJECT_NEW(CChair)

L2_DEFINE_OBJECT_TYPED_RELEASE(CChair)

bool CChair::IsChair() { return true; }

CChair* CChair::CastChair() { return this; }

bool CChair::IsAlive() { return true; }

void CChair::DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) {
    DecideDistanceAttackable(data, creatureId, 100);
}

void CChair::ClearBy(CSmartID sittingUser) {
    L2_GUARD_FUNCTION();

    runtime::raw_atomic::CompareExchangeI32(&m_nOccupiedBy, 0, SignedSmartId(sittingUser));
}

bool CChair::Occupy(CSmartID sittingUser) {
    L2_GUARD_FUNCTION();

    const std::int32_t desired = SignedSmartId(sittingUser);
    const std::int32_t occupied = runtime::raw_atomic::CompareExchangeI32(&m_nOccupiedBy, desired, 0);
    if (occupied == 0) {
        return true;
    }

    CCreature* const occupant = CSmartID(UnsignedSmartId(occupied)).GetCreature();
    if (occupant != nullptr && occupant->IsAlive()) {
        return false;
    }

    return runtime::raw_atomic::CompareExchangeI32(&m_nOccupiedBy, desired, occupied) == occupied;
}

void CChair::SendChairInfo(CUserSocket* socket) {
    if (socket == nullptr) {
        return;
    }

    socket->Send("cddddddddddd", 0x9f, m_data->m_nEditorID, static_cast<std::int32_t>(m_nId), 2, 1, 0, 0, 0, 0, 0, 0, 0);

    const std::int32_t occupied = runtime::raw_atomic::LoadI32Relaxed(&m_nOccupiedBy);
    if (occupied != 0) {
        socket->Send("cdd", 0xed, occupied, m_data->m_nEditorID);
    }
}

void CChair::BroadcastSittingUser() {
    L2_GUARD_FUNCTION();

    g_ptlUserMap->BroadcastToNeighbor(
        *this, 0x80000, GetPos(), "cdd", 0xed, runtime::raw_atomic::LoadI32Relaxed(&m_nOccupiedBy), m_data->m_nEditorID
    );
}

#include "ReservationNPCSpawnTimer.h"

#include "Shared/src/TextRuntime.h"

#include "NPC.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TemplateHelper.h"

#include <cstdint>
#include <cstring>

namespace {



CNPC* FindReservationNpc(const std::uint32_t npcId) {
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(npcId);
    return creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
}

} // namespace

CReservationInfo::CReservationInfo() :
    m_eReservationType(RESERVATION_MAX_TYPE),
    m_tReservationDate(0),
    m_nAliveHour(0),
    m_nNPCId(0),
    m_nYaw(0),
    m_nInZoneId(0) {}

CReservationSpawnInfo::CReservationSpawnInfo() = default;

bool CReservationSpawnInfo::_UpdateReservationSpawn(const std::int32_t now) {
    // Current L2Server 0x00a2f748.
    L2_GUARD_FUNCTION();
    const bool expired = m_tReservationDate <= now;
    if (expired) {
        const char16_t* const npcName = g_ObjectDB.GetName(static_cast<std::int32_t>(m_nNPCId));
        NpcServerUtil::SendRequestReservNpcSpawn(npcName, m_vPos, static_cast<int>(m_nInZoneId), u"", m_nYaw, m_nAliveHour);
        // Log.Add(
        //     LOG_NORMAL,
        //     L"UpdateReservationSpawn NPC[%s] ReservationDate : %d",
        //     npcName,
        //     m_tReservationDate)
        // L2_LOG_INFO(Log,
        //     L"UpdateReservationSpawn NPC[%s] ReservationDate : %d",
        //     npcName,
        //     m_tReservationDate)
        // L2_LOG_INFO(Log,
        //     "Requested scheduled spawn of NPC template %u ('%s'), scheduled Unix time %d. Reservation is consumed.",
        //     static_cast<unsigned>(m_nNPCId),
        //     Utf16ToUtf8(target_utf16::FromHostCodeUnits(npcName)).c_str(),
        //     m_tReservationDate)
        L2_LOG_INFO(
            Log, "Requested scheduled spawn of NPC template %u ('%s'), scheduled Unix time %d; reservation is consumed.",
            static_cast<unsigned>(m_nNPCId), Utf16ToUtf8(std::u16string(npcName)).c_str(), m_tReservationDate
        );
    }
    return expired;
}

bool CReservationSpawnInfo::_UpdateReservationDespawn(const std::int32_t now) {
    // Current L2Server 0x00a2f924.
    L2_GUARD_FUNCTION();
    if (now < m_tReservationDate) {
        return false;
    }

    CNPC* const npc = FindReservationNpc(m_nNPCId);
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Cannot Search NPC[%d] Delete Reservation!",
        //     kReservationSourceWide,
        //     0x2e,
        //     m_nNPCId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Cannot Search NPC[%d] Delete Reservation!",
        //     kReservationSourceWide,
        //     0x2e,
        //     m_nNPCId)
        L2_LOG_ERROR(
            Log, "Cannot find NPC smart ID %u for scheduled despawn at Unix time %d; reservation is discarded.",
            static_cast<unsigned>(m_nNPCId), m_tReservationDate
        );
        return true;
    }

    npc->Despawn(true, true);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"UpdateReservationDespawn NPC[%s][%d], ReservationDate : %d",
    //     npc->GetName(),
    //     npc->Id(),
    //     m_tReservationDate)
    // L2_LOG_INFO(Log,
    //     L"UpdateReservationDespawn NPC[%s][%d], ReservationDate : %d",
    //     npc->GetName(),
    //     npc->Id(),
    //     m_tReservationDate)
    L2_LOG_INFO(
        Log, "Requested scheduled despawn of NPC smart ID %u at Unix time %d; reservation is consumed.",
        static_cast<unsigned>(m_nNPCId), m_tReservationDate
    );
    return true;
}

CReservationNPCSpawnTimer::CReservationNPCSpawnTimer() {}

CReservationNPCSpawnTimer::~CReservationNPCSpawnTimer() = default;

void CReservationNPCSpawnTimer::Init() {
    // Current L2Server 0x00a2f858.
    L2_GUARD_FUNCTION();
    AddTimer(60000, 0);
}

std::int32_t CReservationNPCSpawnTimer::GetReservationDate(const int aliveHour) {
    // Current L2Server 0x00a2f904. Preserve the target's 32-bit wrap.
    const std::uint32_t now = static_cast<std::uint32_t>(runtime::crt::Time32Now());
    const std::uint32_t delta = static_cast<std::uint32_t>(aliveHour) * 0xe10U;
    return runtime::Int32FromBits(now + delta);
}

bool CReservationNPCSpawnTimer::_CheckValidRegisterInfo(
    const RESERVATION_TYPE reservationType, const std::uint32_t npcId, const int aliveHour
) {
    // Current L2Server 0x00a2fa88.
    L2_GUARD_FUNCTION();
    if (reservationType == RESERVATION_NPC_SPAWN) {
        if (npcId < 1'000'001U) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Wrong NPC ID! : %d",
            //     kReservationSourceWide,
            //     0x127,
            //     npcId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Wrong NPC ID! : %d",
            //     kReservationSourceWide,
            //     0x127,
            //     npcId)
            L2_LOG_ERROR(
                Log, "Cannot schedule NPC spawn: template ID %u is below minimum 1000001; reservation is rejected.",
                static_cast<unsigned>(npcId)
            );
            return false;
        }

        CObject* const object = g_ObjectDB.GetObjectW(static_cast<std::int32_t>(npcId), false);
        if (object == nullptr || !object->IsNPC()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Cannot Search NPC ID! : %d",
            //     kReservationSourceWide,
            //     0x12d,
            //     npcId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Cannot Search NPC ID! : %d",
            //     kReservationSourceWide,
            //     0x12d,
            //     npcId)
            L2_LOG_ERROR(
                Log, "Cannot schedule NPC spawn: template %u is missing or is not an NPC; reservation is rejected.",
                static_cast<unsigned>(npcId)
            );
            return false;
        }
        if (object->IsSummonOrPet()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Wrong NPC ID SummonOrPet! : %d",
            //     kReservationSourceWide,
            //     0x133,
            //     npcId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Wrong NPC ID SummonOrPet! : %d",
            //     kReservationSourceWide,
            //     0x133,
            //     npcId)
            L2_LOG_ERROR(
                Log, "Cannot schedule NPC spawn: template %u is a summon or pet; reservation is rejected.",
                static_cast<unsigned>(npcId)
            );
            return false;
        }
    } else if (reservationType == RESERVATION_NPC_DESPAWN) {
        if (FindReservationNpc(npcId) == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Wrong NPC smartID! %d",
            //     kReservationSourceWide,
            //     0x13d,
            //     npcId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Wrong NPC smartID! %d",
            //     kReservationSourceWide,
            //     0x13d,
            //     npcId)
            L2_LOG_ERROR(
                Log, "Cannot schedule NPC despawn: smart ID %u does not resolve to an NPC; reservation is rejected.",
                static_cast<unsigned>(npcId)
            );
            return false;
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Wrong reservationType! %d",
        //     kReservationSourceWide,
        //     0x11f,
        //     reservationType)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Wrong reservationType! %d",
        //     kReservationSourceWide,
        //     0x11f,
        //     reservationType)
        L2_LOG_ERROR(
            Log, "Cannot schedule NPC operation: unsupported reservation type %d; reservation is rejected.",
            static_cast<int>(reservationType)
        );
        return false;
    }

    if (aliveHour < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Wrong aliveHour! %d",
        //     kReservationSourceWide,
        //     0x144,
        //     aliveHour)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Wrong aliveHour! %d",
        //     kReservationSourceWide,
        //     0x144,
        //     aliveHour)
        L2_LOG_ERROR(
            Log, "Cannot schedule NPC operation for ID %u: lifetime %d hours is negative; reservation is rejected.",
            static_cast<unsigned>(npcId), aliveHour
        );
        return false;
    }
    return true;
}

bool CReservationNPCSpawnTimer::ModifyReservationAliveHourInfo(
    const std::uint32_t npcId, const int aliveHour, const std::int32_t reservationDate
) {
    // Current L2Server 0x00a2fcec.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"ModifyReservationInfo NPCId[%d] Set aliveHour : %d",
    //     npcId,
    //     aliveHour)
    // L2_LOG_INFO(Log,
    //     L"ModifyReservationInfo NPCId[%d] Set aliveHour : %d",
    //     npcId,
    //     aliveHour)
    L2_LOG_INFO(
        Log,
        "Updating reservation for NPC ID %u: lifetime %d hours, scheduled Unix time %d (zero lifetime removes the first "
        "matching reservation).",
        static_cast<unsigned>(npcId), aliveHour, reservationDate
    );

    for (auto iterator = m_vInfo.begin(); iterator != m_vInfo.end(); ++iterator) {
        CReservationInfo* const info = *iterator;
        if (info->m_nNPCId != npcId) {
            continue;
        }
        if (aliveHour == 0) {
            L2_RELEASE(info, ObjectRefType::CreateDelete);
            m_vInfo.erase(iterator);
        } else {
            info->m_nAliveHour = aliveHour;
            info->m_tReservationDate = reservationDate;
        }
        return true;
    }
    return false;
}

void CReservationNPCSpawnTimer::DeleteReservationInfo(const RESERVATION_TYPE reservationType, const std::uint32_t npcId) {
    // Current L2Server 0x00a2fe78.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"DeleteReservationInfo NPCId[%d]",
    //     npcId)
    // L2_LOG_INFO(Log,
    //     L"DeleteReservationInfo NPCId[%d]",
    //     npcId)
    L2_LOG_INFO(
        Log, "Removing reservations of type %d for NPC ID %u, if present.", static_cast<int>(reservationType),
        static_cast<unsigned>(npcId)
    );

    for (auto iterator = m_vInfo.begin(); iterator != m_vInfo.end();) {
        CReservationInfo* const info = *iterator;
        if (info->m_nNPCId == npcId && info->m_eReservationType == reservationType) {
            L2_RELEASE(info, ObjectRefType::CreateDelete);
            iterator = m_vInfo.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void CReservationNPCSpawnTimer::_ProcReservationInfo() {
    // Current L2Server 0x00a2ffbc.
    L2_GUARD_FUNCTION();
    if (m_vInfo.empty()) {
        return;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    for (auto iterator = m_vInfo.begin(); iterator != m_vInfo.end();) {
        CReservationInfo* const info = *iterator;
        bool expired = false;
        if (info->m_eReservationType == RESERVATION_NPC_SPAWN) {
            expired = info->_UpdateReservationSpawn(now);
        } else if (info->m_eReservationType == RESERVATION_NPC_DESPAWN) {
            expired = info->_UpdateReservationDespawn(now);
        }

        if (expired) {
            L2_RELEASE(info, ObjectRefType::CreateDelete);
            iterator = m_vInfo.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void CReservationNPCSpawnTimer::RegisterReservationInfo(
    const RESERVATION_TYPE reservationType,
    const std::uint32_t npcId,
    const std::int32_t reservationDate,
    const FVector position,
    const int yaw,
    const std::uint32_t inZoneId,
    const int aliveHour
) {
    // Current L2Server 0x00a300cc.
    L2_GUARD_FUNCTION();
    if (!_CheckValidRegisterInfo(reservationType, npcId, aliveHour)) {
        return;
    }

    auto* const info = new CReservationSpawnInfo;
    info->m_eReservationType = reservationType;
    info->m_nNPCId = npcId;
    info->m_tReservationDate = reservationDate;
    info->m_vPos = position;
    info->m_nYaw = yaw;
    info->m_nInZoneId = inZoneId;
    info->m_nAliveHour = aliveHour;

    // Log.Add(
    //     LOG_NORMAL,
    //     L"RegisterReservationInfo ReservationType : %d, NPCID : %d, ReservationDate : %d, posX : %d, posY : %d, posZ : %d,
    //     yaw : %d, ZoneID : %d, AliveHour : %d", reservationType, npcId, reservationDate,
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     yaw,
    //     inZoneId,
    //     aliveHour)
    // L2_LOG_INFO(Log,
    //     L"RegisterReservationInfo ReservationType : %d, NPCID : %d, ReservationDate : %d, posX : %d, posY : %d, posZ : %d,
    //     yaw : %d, ZoneID : %d, AliveHour : %d", reservationType, npcId, reservationDate,
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     yaw,
    //     inZoneId,
    //     aliveHour)
    L2_LOG_INFO(
        Log,
        "Registering NPC reservation: type %d, NPC ID %u, scheduled Unix time %d, position (%.17g, %.17g, %.17g), heading "
        "%d, zone %u, lifetime %d hours.",
        static_cast<int>(reservationType), static_cast<unsigned>(npcId), reservationDate, position.x, position.y, position.z,
        yaw, static_cast<unsigned>(inZoneId), aliveHour
    );

    L2_GUARD(m_lock);
    m_vInfo.push_back(info);
}

void CReservationNPCSpawnTimer::TimerExpired(const int id) {
    // Current L2Server 0x00a30304.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (id == 0) {
        _ProcReservationInfo();
    }
    AddTimer(60000, 0);
}

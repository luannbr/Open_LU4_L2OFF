#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

enum RESERVATION_TYPE : std::int32_t {
    RESERVATION_NPC_SPAWN = 0,
    RESERVATION_NPC_DESPAWN = 1,
    RESERVATION_MAX_TYPE = 2,
};


class CReservationInfo : public MemoryObject {
public:
    CReservationInfo();
    ~CReservationInfo() override = default;

    virtual bool _UpdateReservationSpawn(std::int32_t now) = 0;
    virtual bool _UpdateReservationDespawn(std::int32_t now) = 0;

    RESERVATION_TYPE m_eReservationType; // +0x18
    std::int32_t m_tReservationDate; // +0x1c
    std::int32_t m_nAliveHour; // +0x20
    std::uint32_t m_nNPCId; // +0x24
    FVector m_vPos; // +0x28
    std::int32_t m_nYaw; // +0x40
    std::uint32_t m_nInZoneId; // +0x44

private:
    L2_DECLARE_OBJECT_REGISTRATION(CReservationInfo)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CReservationInfo)
};

class CReservationSpawnInfo final : public CReservationInfo {
public:
    CReservationSpawnInfo();
    ~CReservationSpawnInfo() override = default;

    L2_DECLARE_OBJECT_TYPE(CReservationSpawnInfo)

    bool _UpdateReservationSpawn(std::int32_t now) override;
    bool _UpdateReservationDespawn(std::int32_t now) override;
};


class CReservationNPCSpawnTimer final : public CIOObject, public Singleton<CReservationNPCSpawnTimer> {
public:
    CReservationNPCSpawnTimer();
    ~CReservationNPCSpawnTimer() override;

    void Init();
    static std::int32_t GetReservationDate(int aliveHour);
    bool ModifyReservationAliveHourInfo(std::uint32_t npcId, int aliveHour, std::int32_t reservationDate);
    void DeleteReservationInfo(RESERVATION_TYPE reservationType, std::uint32_t npcId);
    void RegisterReservationInfo(
        RESERVATION_TYPE reservationType,
        std::uint32_t npcId,
        std::int32_t reservationDate,
        FVector position,
        int yaw,
        std::uint32_t inZoneId,
        int aliveHour
    );
    void TimerExpired(int id) override;

private:
    bool _CheckValidRegisterInfo(RESERVATION_TYPE reservationType, std::uint32_t npcId, int aliveHour);
    void _ProcReservationInfo();

    std::list<CReservationInfo*> m_vInfo; // +0x20
    CYieldLock m_lock; // +0x38
};

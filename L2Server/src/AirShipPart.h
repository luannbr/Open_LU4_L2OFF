#pragma once

#include "StaticObject.h"

#include <cstddef>
#include <cstdint>

class CAirShip;

// Exact current abstract airship-part carrier. The current image keeps
// LeaveWorld out of line. The remaining lifecycle/behavior surface belongs
// to the header contract and its concrete part implementations.
class CAirShipPart : public CStaticObject {
public:
    explicit CAirShipPart(CAirShip* airShip);
    CAirShipPart(const CAirShipPart& other);
    ~CAirShipPart() override = default;

    L2_DECLARE_OBJECT_REGISTRATION(CAirShipPart)
    static constexpr int TIMER_TICK = 1000;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    bool IsAirShipPart() override { return true; }
    CAirShipPart* CastAirShipPart() override { return this; }
    void Delete() override;
    virtual void SendMySelfInfoTo(CUserSocket* socket) = 0;
    void TimerExpired(int timerId) override;
    virtual void OnDestructed() = 0;
    virtual void OnTimerExpired() = 0;

    void EnterWorld(CAirShip* airShip);
    void LeaveWorld(CAirShip* airShip);
    bool IsLeft() const noexcept { return m_bLeft; }
    void SetLeft(bool left) noexcept { m_bLeft = left; }
    void SetClassID(int classId) noexcept { m_nClassId = classId; }
    int GetClassID() const noexcept { return m_nClassId; }
    void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) override;
    double GotDamageBy(CCreature* attacker, double damage) override;
    double AttackedBy(CCreature* attacker, double damage) override;
    CAirShip* GetAirShip() const;

private:
    CSmartID m_sAirShipId;
    bool m_bDestructed;
    bool m_bLeft;
    std::byte m_alignment4eeTo4f0[2];
    std::int32_t m_nClassId;
    std::byte m_alignment4f4To4f8[4];

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CAirShipPart)
};


// Exact current steering object from AirShipPart.cpp. It is a complete
// CStaticObject-derived owner: the target adds only the parent-airship smart
// ID to CStaticObject's 0x4e8-byte envelope.
class CAirShipSteering : public CStaticObject {
public:
    explicit CAirShipSteering(CAirShip* airShip);
    ~CAirShipSteering() override;

    L2_DECLARE_OBJECT_REGISTRATION(CAirShipSteering)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    bool IsAirShipSteering() override;
    CAirShipSteering* CastAirShipSteering() override;
    void Delete() override;
    void Interact(CCreature* creature) override;

    CAirShip* GetAirShip() const;

private:
    CSmartID m_spAirShip;
    [[maybe_unused]] std::byte m_alignment4ecTo4f0[4];

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CAirShipSteering)
};

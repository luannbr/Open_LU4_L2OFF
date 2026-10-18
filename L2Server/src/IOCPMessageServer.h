#pragma once

#include "Creature.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOCPMessage.h"

#include <cstddef>
#include <cstdint>

class CCreature;
class CCampaign;

class CShutDownMessage final : public CIOCPMessage {
public:
    explicit CShutDownMessage(bool stopLoader = false) noexcept : m_nTime(0), m_StopLoader(stopLoader) {}

    void OnReceive(int argument) override;

private:
    void StopLoader();

    std::int32_t m_nTime;
    bool m_StopLoader;
};


class CPledgeGameLoadingMessage final : public CIOCPMessage {
public:
    CPledgeGameLoadingMessage() = default;
    void OnReceive(int argument) override;
};

class COlympiadLoadingMessage final : public CIOCPMessage {
public:
    COlympiadLoadingMessage() = default;
    void OnReceive(int argument) override;
};

class CRegularAirShipStartMessage final : public CIOCPMessage {
public:
    CRegularAirShipStartMessage() = default;
    void OnReceive(int argument) override;
};

class CVehicleEraseMessage final : public CIOCPMessage {
public:
    CVehicleEraseMessage() = default;
    void OnReceive(int argument) override;
};

class CVehicleStartMessage final : public CIOCPMessage {
public:
    CVehicleStartMessage() = default;
    void OnReceive(int argument) override;
};

class CRegisterPendedStaticObjects final : public CIOCPMessage {
public:
    CRegisterPendedStaticObjects() = default;
    void OnReceive(int argument) override;
};

class COlympiadObserverChangeFieldMessage final : public CIOCPMessage {
public:
    COlympiadObserverChangeFieldMessage(std::uint32_t userSid, std::int32_t fieldId) noexcept;
    void OnReceive(int argument) override;

private:
    std::uint32_t m_UserSid;
    std::int32_t m_FieldId;
};


class CPledgeGameObserverChangeFieldMessage final : public CIOCPMessage {
public:
    CPledgeGameObserverChangeFieldMessage(std::int32_t userSid, std::int32_t fieldId) noexcept;
    void OnReceive(int argument) override;

private:
    std::int32_t m_nUserSid;
    std::int32_t m_nFieldId;
};


class CDelayedCampaignStarter final : public CIOCPMessage {
public:
    CDelayedCampaignStarter(CCampaign* previousCampaign, CCampaign* nextCampaign) noexcept;
    void OnReceive(int argument) override;

private:
    CCampaign* m_pPrevCampaign; // +0x18
    CCampaign* m_pNextCampaign; // +0x20
};


class CInstantAgitPostLoadMessage final : public CIOCPMessage {
public:
    CInstantAgitPostLoadMessage() = default;
    void OnReceive(int argument) override;
};


class CGetOnVehicleIOCPMessage final : public CIOCPMessage {
public:
    CGetOnVehicleIOCPMessage(CCreature* actor, std::int32_t vehicleId, FVector relativePosition) :
        m_pActor(actor), m_nVehicleID(vehicleId), vRelPos(relativePosition) {
        L2_ADD_REF(m_pActor, ObjectRefType::IocpMessageRetain);
    }

    void OnReceive(int argument) override;

private:
    CCreature* m_pActor;
    std::int32_t m_nVehicleID;
    FVector vRelPos;
};


class CGetOnShuttleIOCPMessage final : public CIOCPMessage {
public:
    CGetOnShuttleIOCPMessage(CCreature* actor, std::int32_t shuttleId, FVector relativePosition) :
        m_pActor(actor), m_nShuttleID(shuttleId), vRelPos(relativePosition) {
        L2_ADD_REF(m_pActor, ObjectRefType::IocpMessageRetain);
    }

    void OnReceive(int argument) override;

private:
    CCreature* m_pActor;
    std::int32_t m_nShuttleID;
    FVector vRelPos;
};


class CGetOffVehicleIOCPMessage final : public CIOCPMessage {
public:
    CGetOffVehicleIOCPMessage(CCreature* creature, std::uint32_t vehicleId, FVector position);
    void OnReceive(int argument) override;

    CCreature* m_pCreature;
    std::uint32_t m_nVehicleId;
    std::uint32_t m_alignment24;
    FVector m_vPosition;
};

class CGetOffShuttleIOCPMessage final : public CIOCPMessage {
public:
    CGetOffShuttleIOCPMessage(CCreature* creature, std::uint32_t shuttleId, FVector position);
    void OnReceive(int argument) override;

    CCreature* m_pCreature;
    std::uint32_t m_nShuttleId;
    std::uint32_t m_alignment24;
    FVector m_vPosition;
};


class CNPSendCriticalMailIOCPMessage final : public CIOCPMessage {
public:
    CNPSendCriticalMailIOCPMessage() noexcept;
    void OnReceive(int argument) override;

    char m_szMessage[0x800];
    bool m_isNeedToCloseSocket;
    std::uint32_t m_nAccountID;
};


template <typename Function>
class CLambdaIOCPMessage final : public CIOCPMessage {
public:
    explicit CLambdaIOCPMessage(Function function) : m_function(function) {}

    void OnReceive(int) override { m_function(); }

private:
    Function m_function;
};

template <typename Function>
void PostLambdaMessage(Function function) {
    (new CLambdaIOCPMessage<Function>(function))->Post(0);
}

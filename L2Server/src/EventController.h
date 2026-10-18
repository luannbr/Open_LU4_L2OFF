#pragma once

#include "StaticObject.h"

#include <cstddef>
#include <cstdint>

class CUserSocket;

class CEventController : public CStaticObject {
public:
    CEventController();
    CEventController(const CEventController& other);
    ~CEventController() override;

    L2_DECLARE_OBJECT_REGISTRATION(CEventController)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    CObject* Copy() override;
    bool IsEventController() override;
    CEventController* CastEventController() override;
    void Delete() override;

    void EstablishEventController(int eventId, FVector position);
    void SetOnOff(bool on);
    void SendEventControllerInfo(CUserSocket* socket = nullptr, bool isInsight = false);

private:
    bool m_bIsOn;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CEventController)
};

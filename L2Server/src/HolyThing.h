#pragma once

#include "StaticObject.h"

#include <cstddef>
#include <cstdint>

class CResidence;
class CSkillInfo;
class CUserSocket;
class User;

// Current L2Server CHolyThing envelope. Its vtable has the same 174-slot
// extent as CStaticObject and overrides exactly the nine slots declared
// below. The final 0x20 bytes are the current residence/possession state.
class CHolyThing : public CStaticObject {
public:
    CHolyThing();
    CHolyThing(const CHolyThing& other);
    ~CHolyThing() override;

    L2_DECLARE_OBJECT_REGISTRATION(CHolyThing)

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()
    static bool AddHolyThing(const wchar_t* className, CHolyThing* holyThing);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;
    CObject* Copy() override;
    bool IsHolyThing() override;
    CHolyThing* CastHolyThing() override;
    bool IsEnemyTo(CCreature* creature) override;
    bool IsAlive() override;
    void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) override;
    double AttackedBy(CCreature* attacker, double damage) override;

    void SendHolyThingInfo(CUserSocket* socket, bool useRequestedStatus, int status);
    void SendWarningMessage(const CSkillInfo* skillInfo);
    void SendCountDownMessage(unsigned int messageId, int argument);
    bool AddPossessingMaster(User* user);
    bool PossessedBy(CCreature* creature, int isGood);
    void RemovePossessingMaster(User* user);

    std::int32_t m_nOwningResidenceID;
    std::int32_t m_residenceAlignment;
    CResidence* m_pOwningResidence;
    std::uint32_t m_nOwningPledgeID;
    std::int32_t m_nPossessingMasterCount;
    std::int32_t m_nStatus;
    // The original class shadows CObject::m_nClassId. This is
    // the static-object template id sent by SendHolyThingInfo at +0x504.
    std::int32_t m_nClassId;

private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CHolyThing)
};

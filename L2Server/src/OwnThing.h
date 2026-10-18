#pragma once

#include "StaticObject.h"

#include <cstddef>
#include <cstdint>

class CDominion;
class CResidence;
class CUserSocket;
class User;
enum eSMsgType : std::int32_t;

enum OwnThingStatus : std::int32_t {
    OTS_ENABLED = 0,
    OTS_BROKEN = 1,
};

class COwnThing : public CStaticObject {
public:
    enum OwnThingBroadcastType : std::int32_t {
        OTBT_BROKEN = 0,
        OTBT_DEAD = 1,
        OTBT_RESTARTED = 2,
        OTBT_TAKEN = 3,
    };

    L2_DECLARE_OBJECT_REGISTRATION(COwnThing)

    COwnThing();
    COwnThing(const COwnThing& other);
    ~COwnThing() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;
    CStaticObject* Copy() override;
    bool IsOwnThing() override;
    COwnThing* CastOwnThing() override;
    bool IsEnemyTo(CCreature* creature) override;
    bool IsAlive() override;
    void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) override;
    double AttackedBy(CCreature* attacker, double damage) override;
    double GotDamageBy(CCreature* attacker, double damage) override;

    static bool AddOwnThing(const wchar_t* className, COwnThing* ownThing);

    void InsertOwnThingToDominion(CDominion* dominion);
    void Move(FVector position);
    void HandleChangeSiegeState(CDominion* dominion, bool appear);
    void SetOriginalResidenceID(int residenceId);
    int GetOriginalResidenceID() const noexcept { return m_nOriginalResidenceID; }
    void SetOwningResidenceID(int residenceId);
    int GetOwningResidenceID() const noexcept { return m_nOwningResidenceID; }
    void SetOwningResidence(CResidence* residence);
    CResidence* GetOwningResidence() const noexcept { return m_pOwningResidence; }
    void SetOwningPledgeID(int pledgeId) noexcept { m_nOwningPledgeID = pledgeId; }
    int GetOwningPledgeID() const noexcept { return m_nOwningPledgeID; }
    void OnDestructed(CCreature* attacker);
    bool GiveToUser(User* user, bool rejectOwningDominion);
    void SendOwnThingInfo(CUserSocket* socket);
    void SendOwnThingInfoImpl(User* user);
    int GetClassID() const noexcept { return m_nClassId; }
    void Appear();
    void Disappear();
    void Regenerate(CDominion* dominion, FVector position);
    void BroadcastMessage(OwnThingBroadcastType type, eSMsgType argumentType, const char16_t* argument, bool argumentFirst);
    void SetStatus(OwnThingStatus status) noexcept { m_nStatus = status; }
    int GetStatus() const noexcept { return m_nStatus; }
    void SetUser(User* user) noexcept { m_pUser = user; }
    User* GetUser() const noexcept { return m_pUser; }

protected:
    std::int32_t m_nOriginalResidenceID; // +0x4e8
    std::int32_t m_nOwningResidenceID; // +0x4ec
    CResidence* m_pOwningResidence; // +0x4f0
    std::int32_t m_nOwningPledgeID; // +0x4f8
    std::int32_t m_nStatus; // +0x4fc
    char16_t m_wName[100]; // +0x500
    CYieldLock m_BindLock; // +0x5c8
    User* m_pUser; // +0x5d8

public:
    std::int32_t m_nClassId; // +0x5e0

private:
    [[maybe_unused]] std::byte m_alignment5E4To5E8[0x04];
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(COwnThing)
};

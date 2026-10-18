#pragma once

#include "StaticObject.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CUserSocket;
class CSitAction;
class CParserForDoor;
class CChairManager;
class CInstantZoneEntranceCondition_check_party_chair;
class User;

class CChair : public CStaticObject {
public:
    CChair();
    ~CChair() override;

    L2_DECLARE_OBJECT_REGISTRATION(CChair)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    bool IsChair() override;
    CChair* CastChair() override;
    bool IsAlive() override;
    void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) override;

    void ClearBy(CSmartID sittingUser);
    bool Occupy(CSmartID sittingUser);
    void SendChairInfo(CUserSocket* socket);
    void BroadcastSittingUser();

    int GetGroupType() const noexcept { return m_nGroupType; }

    double GetSitRange() const noexcept { return m_dSitRange; }

private:
    friend class CSitAction;
    friend class CParserForDoor;
    friend class CChairManager;
    friend class CInstantZoneEntranceCondition_check_party_chair;

    std::int32_t m_nGroupId;
    std::int32_t m_nGroupType;
    std::int32_t m_nOccupiedBy;
    std::int32_t m_nOccupiedGroupId;
    double m_dSitRange;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CChair)
};


// Current Chair.cpp process-global group registry. The four locks precede
// the 0x20 target map exactly. Grouped-chair vectors are process-owned.
class CChairManager {
public:
    using ChairList = std::vector<CChair*>;
    using ChairGroupMap = std::map<int, ChairList*>;

    CChairManager();
    ~CChairManager();

    void RegisterGroupChair(int groupId, CChair* chair);
    bool AssignChairOwnerGroup(int groupId, int ownerGroupId);
    void ClearChairGroup(int groupId, int x, int y, int z);
    bool CheckInzoneCondition(User* user, CChair* chair, int type);

private:
    CYieldLock m_GroupLocks[4]; // +0x00
    ChairGroupMap m_ChairGroupMap; // +0x40
};


extern CChairManager g_ChairManager;

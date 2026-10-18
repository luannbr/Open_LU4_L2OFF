#pragma once

#include "InstantZone.h"
#include "StaticObject.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>


class CEventController;
class CFieldCycle;
class CDoorReleaseVerifier;
class DoorEventMap;
class CMoveToTargetAction;
class CSkillOperateCondition_op_unlock;
class CSkillEffect_i_detect_object;
class CSkillEffect_i_unlock;
class CResidence;
class CUserSocket;
class CParserForDoor;
class CWorld;
class NpcSocket;
class User;

bool BuilderCmd_door_levelup(CUserSocket*, User*, char16_t*);
bool BuilderCmd_show_castle_door_hp(CUserSocket*, User*, char16_t*);
bool BuilderCmd_show_door_hp(CUserSocket*, User*, char16_t*);
bool BuilderCmd_SetStaticMeshStatus(CUserSocket*, User*, char16_t*);

namespace BuilderCmd {
bool CSCharacterInfoPacket(CUserSocket*, char16_t*);
}

// The enum spellings are donor-selected source vocabulary. Every numeric
// representation consumed by this class is proven by the
// current L2Server image.
enum DoorOpenMethodEnum : std::int32_t {
    DOOROPEN_NPC = 0,
    DOOROPEN_SKILL = 1,
    DOOROPEN_CLICK = 2,
    DOOROPEN_TIME = 3,
    DOOROPEN_SUNRISE = 4,
    DOOROPEN_TIME_SKILL = 5,
    DOOROPEN_CYCLE = 6,
    DOOROPEN_SKILL_ITEM = 7,
    DOOROPEN_ITEM = 8,
};

enum DoorTypeEnum : std::int32_t {
    DOORT_NORMAL = 0,
    DOORT_PARENT = 1,
    DOORT_CHILD = 2,
    DOORT_WALL = 3,
};

enum DoorActionTypeEnum : std::int32_t {
    DOOR_ACT_NONE = 0,
    DOOR_ACT_OPEN = 1,
    DOOR_ACT_CLOSE = 2,
};

class CTerritory;

// Exact current L2Server door envelope. Residence/door graph pointers and
// graph elements are borrowed. Both xvectors own only their backing arrays.
// the event controller follows the current deep-copy/Delete lifecycle.
class CDoor : public CStaticObject {
    friend class CDB;
    friend class CDoorDB;
    friend class CDoorReleaseVerifier;
    friend class DoorEventMap;
    friend class CParserForDoor;
    friend class CInstantZone;
    friend class CFieldCycle;
    friend class CMoveToTargetAction;
    friend class CSkillOperateCondition_op_unlock;
    friend class CSkillEffect_i_detect_object;
    friend class CSkillEffect_i_unlock;
    friend class CWorld;
    friend bool NpcCastleGateOpenClose2(NpcSocket* socket, const unsigned char* packet);
    friend bool NpcSetDoorHpLevel(NpcSocket* socket, const unsigned char* packet);
    friend bool NpcGetDoorHpLevel(NpcSocket* socket, const unsigned char* packet);
    friend bool NpcSetStaticMeshStatus(NpcSocket* socket, const unsigned char* packet);
    friend bool BuilderCmd::CSCharacterInfoPacket(CUserSocket* socket, char16_t* command);
    friend bool BuilderCmd_door_levelup(CUserSocket* socket, User* user, char16_t* command);
    friend bool BuilderCmd_show_castle_door_hp(CUserSocket* socket, User* user, char16_t* command);
    friend bool BuilderCmd_show_door_hp(CUserSocket* socket, User* user, char16_t* command);
    friend bool BuilderCmd_SetStaticMeshStatus(CUserSocket* socket, User* user, char16_t* command);

public:
    CDoor();
    CDoor(const CDoor& other);
    ~CDoor() override;

    L2_DECLARE_OBJECT_REGISTRATION(CDoor)

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;
    void TimerExpired(int id) override;

    CObject* Copy() override;
    bool IsDoor() override;
    CDoor* CastDoor() override;
    bool IsEnemyTo(CCreature* creature) override;
    bool IsAlive() override;
    void SendDB(const char* format, ...) const override;
    void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) override;
    void Delete() override;
    double AttackedBy(CCreature* attacker, double damage) override;
    double GotDamageBy(CCreature* attacker, double damage) override;
    void SetHP(double hp) override;
    double GetMDefend() override;
    double GetPDefend() override;

    // The current CDoor vtable appends this one slot after the complete
    // 174-slot CStaticObject surface.
    virtual void ValidatePlaneCollision();

    const char16_t* GetDoorName() const;
    void CalcCenterPos();
    void GetTerritory(::CTerritory& territory) const;
    void SetOwningResidence(CResidence* residence);
    bool CanBeOpenedBy(CCreature* creature);
    void SendDoorStatus(CUserSocket* socket);
    void ValidateDamageGrade();
    void OnTimerHpChanged(bool force);
    void Open(bool direct, bool force = false);
    void Close(bool direct, bool force = false);
    bool IsOpened() const noexcept {
        // Current inline consumers, including OpenDoors@0072d30c,
        // dereference the shared record after checking only
        // the CDoor pointer.
        return m_data->m_bIsOpen;
    }
    DoorOpenMethodEnum GetDoorOpenMethod() const noexcept { return m_eDoorOpenMethod; }
    void DoDoorAction(DoorActionTypeEnum action);
    void ResetDoorState(bool forceClose, double hpRatio);
    bool IsSiegeProof() const noexcept { return m_bSiegeProof; }
    void SetSiegeProof(const bool siegeProof) noexcept { m_bSiegeProof = siegeProof; }


protected:
    volatile std::int32_t m_nDoorDeleteCount;
    std::int32_t m_DamageGrade;
    FVector m_HingePos;
    std::int32_t m_Height;
    std::byte padding_50c[4];
    FVector m_Range[4];
    std::int32_t m_nOwningResidenceID;
    std::byte padding_574[4];
    CResidence* m_pOwningResidence;
    char16_t m_sDoorName[50];
    std::byte padding_5e4[4];
    double m_dPreviousHp;
    DoorOpenMethodEnum m_eDoorOpenMethod;
    DoorTypeEnum m_eType;
    DoorActionTypeEnum m_eMasterOpenEvent;
    DoorActionTypeEnum m_eMasterCloseEvent;
    CDoor* m_pParent;
    std::vector<CDoor*> m_vChilds;
    CDoor* m_pMaster;
    std::vector<CDoor*> m_vSlaves;
    std::int32_t m_nCloseTime;
    std::int32_t m_nOpenTime;
    std::int32_t m_nRandomTime;
    std::uint32_t m_dwLastSendTime;
    double m_dBaseHp;
    bool m_bSiegeProof;
    bool m_bDefaultOpened;
    std::byte padding_66a[2];
    InstantZoneKey m_InstantZoneKey;
    std::int32_t m_nRemainTime;
    bool m_bNeedToLog;
    bool m_bOpenByDoorNameSkill;
    std::byte padding_67a[2];
    std::int32_t m_nHpShowable;
    std::int32_t m_nInactiveTime;
    bool m_isCheckCollisiion;
    std::byte padding_685[3];
    CEventController* m_pEventController;

private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CDoor)
};


// Current process-global door database. The generated door/trap parsers are
// separate owners, but the lookup/storage envelope is needed by Shuttle and
// other already recovered world systems.
class CDoorDB {
    friend class CParserForDoor;
    friend bool BuilderCmd_door_levelup(CUserSocket* socket, User* user, char16_t* command);

public:
    CDoorDB();
    ~CDoorDB();

    CDoor* GetDoor(const char16_t* name, std::uint32_t inZoneId = 0);
    CDoor* GetDoor(int doorId);
    void SetDoorBreakable(bool breakable);
    void InitializeDoors();
    void SetDoorOpen(bool opened);
    void AddDoor(const char16_t* name, CDoor* door);
    bool LoadData(char16_t* fileBuffer, int fileSize, bool firstStep);
    bool LoadDoor(bool firstStep);
    bool LoadTrap();
    void StartDoorTimer();
    void OnSunrise(bool isNight);


private:
    ::std::map<std::u16string, CDoor*, runtime::CStringLess<std::u16string>> m_DoorNameMap; // +0x00
    ::std::map<int, CDoor*> m_DoorIdMap; // +0x20
    ::std::vector<CDoor*> m_vSunriseDoors; // +0x40
    CParserForDoor* m_pParser; // +0x60
    CDoor* m_pLastDoor; // +0x68
};


extern CDoorDB g_DoorDB;

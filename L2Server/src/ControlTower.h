#pragma once

#include "StaticObject.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>


class CArea;
class CCastle;
class CDBSocket;
class CMoveToTargetAction;
class CParserForDoor;
class CResidence;
class CUserSocket;
class NpcSocket;
class User;

bool ReplyLoadControlTower(CDBSocket* socket, const unsigned char* packet);
bool BuilderCmd_set_controltower_status(CUserSocket* socket, User* user, char16_t* command);
bool BuilderCmd_controltower_levelup(CUserSocket* socket, User* user, char16_t* command);

// The spellings are donor-selected source vocabulary. The current image
// proves the 32-bit representation and every numeric value.
enum class ControlTowerStatusEnum : std::int32_t {
    Working = 0,
    Destroyed = 1,
    Idle = 2,
    Max = 3,
};

enum class ControlTowerTypeEnum : std::int32_t {
    Undefined = 0,
    Life = 1,
    Trap = 2,
    Support = 3,
    Max = 4,
};

// Exact current L2Server control-tower envelope. The residence and CArea
// pointers are borrowed. The standard vector owns only its pointer storage.
// its CArea elements are never released by this object.
class CControlTower : public CStaticObject {
    friend class CCastle;
    friend class CDB;
    friend class CMoveToTargetAction;
    friend class CParserForDoor;
    friend class CParserForResidence;
    friend class CControlTowerDB;
    friend bool ReplyLoadControlTower(CDBSocket* socket, const unsigned char* packet);
    friend bool NpcSetControlTowerLevel(NpcSocket* socket, const unsigned char* packet);
    friend bool NpcGetControlTowerLevel(NpcSocket* socket, const unsigned char* packet);
    friend bool BuilderCmd_set_controltower_status(CUserSocket* socket, User* user, char16_t* command);
    friend bool BuilderCmd_controltower_levelup(CUserSocket* socket, User* user, char16_t* command);

public:
    CControlTower();
    CControlTower(const CControlTower& other);
    ~CControlTower() override;

    L2_DECLARE_OBJECT_REGISTRATION(CControlTower)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    CObject* Copy() override;
    bool IsControlTower() override;
    CControlTower* CastControlTower() override;
    bool IsEnemyTo(CCreature* creature) override;
    bool IsAlive() override;
    void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) override;
    bool IsActive() override;
    double AttackedBy(CCreature* attacker, double damage) override;
    double GotDamageBy(CCreature* attacker, double damage) override;
    virtual bool IsEnabled();

    void SendControlTowerInfoImpl(User* user);
    int GetDominionSiegeID();
    void SendControlTowerInfo(CUserSocket* socket);
    void SetStatus(ControlTowerStatusEnum status);
    void ChangeToReservedStatus();
    void OnSiegeStarted(bool resumed);
    void OnSiegeEnded();
    void OnMidVictory();
    void OnSiegePaused();
    void OnSiegeResumed();


protected:
    std::int32_t m_nDisplayNPCID_Working;
    std::int32_t m_nDisplayNPCID_NotWorking;
    std::int32_t m_nControlLevel;
    bool m_bTogglable;
    std::byte padding_4f5[3];
    ControlTowerStatusEnum m_eStatus;
    ControlTowerStatusEnum m_eReservedStatus;
    CResidence* m_pOwningResidence;
    volatile std::int32_t m_bCanChangeStatus;
    std::byte padding_50c[4];
    std::vector<CArea*> m_lstControlArea;
    ControlTowerTypeEnum m_eControlTowerType;
    char16_t m_sControlTowerName[64];
    std::byte padding_5b4[4];

private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CControlTower)
};


class CControlTowerDB final {
    friend bool BuilderCmd_controltower_levelup(CUserSocket* socket, User* user, char16_t* command);

public:
    using NameMap = std::map<std::u16string, CControlTower*, runtime::CStringLess<std::u16string>>;
    using IdMap = std::map<int, CControlTower*>;

    CControlTowerDB();
    ~CControlTowerDB() = default;

    static CControlTowerDB& Inst();

    CControlTower* GetControlTower(const char16_t* name);
    void AddControlTower(const char16_t* name, CControlTower* tower);

private:
    NameMap m_NameMap; // +0x00
    IdMap m_IdMap; // +0x20
    CYieldLock m_Lock; // +0x40
};

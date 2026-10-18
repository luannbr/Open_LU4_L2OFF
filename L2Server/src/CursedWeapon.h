#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CDBSocket;
class CItem;
class CNPC;
class CUserSocket;
class User;

// The spellings are donor-selected source vocabulary. Every numeric value is
// produced or consumed by the current L2Server image.
enum CW_DELETE_REASON : std::int32_t {
    DR_NOT_DELETE = 0,
    DR_EXPIRE_AFTER_DROP = 1,
    DR_DYING = 2,
    DR_END_LIFE_TIME = 3,
    DR_NOT_KILL_PC = 4,
    DR_BY_BUILDER = 5,
    DR_PICKED_BY_ANOTHER_CURSED_USER = 6,
};


struct CursedWeaponInfo {
    int nItemClassId; // +0x00
    int nCreateRatePer10Min; // +0x04
    int nCreateRatePerNPC; // +0x08
    int nDropRateOnDying; // +0x0c
    int nLifeTime; // +0x10
    int nExpireByNonpk; // +0x14
    int nExpireByDrop; // +0x18
    int transformID; // +0x1c
};


class CCursedWeaponConstInfo {
public:
    void Add(const CursedWeaponInfo* info);
    const CursedWeaponInfo* Find(int itemClassId) const;
    void BuilderCmd_SettingView(CUserSocket* socket);
    int AssembleCursedWeaponList(char* buffer, int bufferSize, int* count) const;
    void SendCursedWeaponList(CUserSocket* socket) const;

private:
    std::map<int, const CursedWeaponInfo*> m_mapCursedWeapon;
};


struct CCursedWeaponGenerator {
    bool TestGenEnable();

    const CursedWeaponInfo* m_pConstInfo; // +0x00
    std::int32_t m_nExist; // +0x08
    std::int32_t m_tLastCheck; // +0x0c
    bool m_b10Min; // +0x10
    std::byte m_padding11To14[3]; // +0x11
    volatile std::int32_t m_GeneratorLock; // +0x14
};


class CCursedWeapon {
public:
    ~CCursedWeapon() { m_pConstInfo = nullptr; }

    int CalcLastAlarmSec() const;
    void ChangeOwner(User* oldOwner, User* newOwner);
    int GetLevel() const;
    std::int32_t RecalcExpiredDateAfterDrop() const;
    void Drop(User* owner);
    void Destroy(User* owner);
    void KillPC(User* killer, User* victim);
    CItem* GetItem() const;
    User* GetOwner() const;
    CW_DELETE_REASON TimerJob(int timerCount);
    void PickUp(CItem* item, User* owner);
    void EnterWorld(CItem* item, User* owner);
    void BuilderCmd_View(CUserSocket* socket) const;
    void BuildCmd_SetKillPoint(int killPoint);
    void ReplyPickCursedWeaponFromDB(CItem* item, User* owner, const unsigned char* packet);
    void PickAnotherCursedWeapon(User* owner, CItem* item);

    std::int32_t m_nItemClassId; // +0x00
    std::int32_t m_tCreateDate; // +0x04
    std::int32_t m_tLastKillDate; // +0x08
    std::int32_t m_tExpiredDate; // +0x0c
    double m_dKillPoint; // +0x10
    const CursedWeaponInfo* m_pConstInfo; // +0x18
    std::int32_t m_nItemDbId; // +0x20
    std::int32_t m_nCharId; // +0x24
    std::int32_t m_nOriginalPK; // +0x28
    std::int32_t m_nTotalPK; // +0x2c
    std::int32_t m_nItemSId; // +0x30
    std::int32_t m_tDroppedDate; // +0x34
    std::int32_t m_nLastAlarmSec; // +0x38
    bool m_bDrop; // +0x3c
    std::byte m_padding3dTo40[3]; // +0x3d
};


class CCursedWeaponMgr : public CIOObject, public Singleton<CCursedWeaponMgr> {
public:
    CCursedWeaponMgr();
    ~CCursedWeaponMgr() override;

    static void Init();
    static void LoadFromScript();

    void TimerExpired(int id) override;
    void Add(CursedWeaponInfo* info);
    CCursedWeaponGenerator* GetGenerator(int itemClassId);
    CCursedWeapon* GetCursedWeapon(int itemClassId);
    int GetWeaponLevel(int itemClassId);
    const CursedWeaponInfo* FindConst(int itemClassId) const { return m_ConstInfo.Find(itemClassId); }
    int GetOriginalPKCount(int itemClassId);
    bool IsCursedUser(User* user);
    void KillPC(User* killer, User* victim);
    bool DestroyWeapon(CItem* item, User* owner);
    bool BuilderMake(CNPC* npc, int itemClassId);
    void RandomMake(CNPC* npc);
    bool AddWeapon(CItem* item, User* owner);
    bool AddWeapon(
        int itemDbId,
        int createDate,
        int lastKillDate,
        int expiredDate,
        double killPoint,
        int charId,
        const CursedWeaponInfo* info,
        int originalPk,
        int totalPk
    );
    void OnPickUp(User* owner, CItem* item);
    void OnEnterWorld(User* owner);
    void OnDeleted(CItem* item, User* owner, CW_DELETE_REASON reason);
    bool DropWeapon(CItem* item, User* owner);
    void OnDropByNPC(CItem* item);
    bool OnDropByUser(User* owner, CItem* item);
    void DeleteOriginalCursedWeapon(CItem* item);
    void LoadFromDB(
        int itemDbId,
        int createDate,
        int lastKillDate,
        int expiredDate,
        double killPoint,
        int charId,
        int itemClassId,
        int originalPk,
        int totalPk
    );
    void ReplyPickCursedWeaponFromDB(CItem* item, User* owner, const unsigned char* packet);
    void BuilderCmd_SettingView(CUserSocket* socket);
    void SendCursedWeaponList(CUserSocket* socket);
    void BuilderCmd_Delete(User* owner, int itemClassId);
    void BuilderCmd_View(CUserSocket* socket);
    void BuilderCmd_SetKillPoint(int itemClassId, int killPoint);
    void ShowCursedWeaponStatus(User* owner, int itemClassId);
    int AssembleExistingCursedWeaponLocation(char* buffer, int bufferSize, int* count);
    void SendExistingCursedWeapon(CUserSocket* socket);

private:
    CYieldLock m_EntrySect; // +0x20
    std::map<int, CCursedWeapon*> m_mapCursedItem; // +0x30
    std::map<int, CCursedWeaponGenerator*> m_mapGenerator; // +0x50
    CCursedWeaponConstInfo m_ConstInfo; // +0x70
    std::int32_t m_nTimerCount; // +0x90
    [[maybe_unused]] std::byte m_padding94To98[4];
};


namespace CCursedWeaponDBHandler {

bool ReplyLoadCursedWeaponList(CDBSocket* socket, const unsigned char* packet);

} // namespace CCursedWeaponDBHandler

namespace CCursedWeaponBuilderCmd {

bool Delete(CUserSocket* socket, User* user, char16_t* command);
bool View(CUserSocket* socket, User* user, char16_t* command);
bool SettingView(CUserSocket* socket, User* user, char16_t* command);
bool GiveCursedWeapon(CUserSocket* socket, User* user, char16_t* command);
bool KillPoint(CUserSocket* socket, User* user, char16_t* command);

} // namespace CCursedWeaponBuilderCmd

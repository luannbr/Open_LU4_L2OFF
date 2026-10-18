#pragma once

#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"
#include "StaticObject.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>


class CPledge;
class CResidence;
class CUserSocket;
class User;
class CBattleCampDb;

// Exact current L2Server battle-camp envelope. Both object pointers are
// borrowed. The UTF-16 name and every scalar are embedded values. Teardown
// clears the borrowed pointers and owns no member allocation.
class CBattleCamp : public CStaticObject {
public:
    CBattleCamp(
        std::uint32_t databaseId,
        CResidence* residence,
        CPledge* pledge,
        FVector position,
        int maximumHp,
        int hp,
        int baseType
    );
    CBattleCamp(const CBattleCamp& other);
    ~CBattleCamp() override;

    L2_DECLARE_OBJECT_REGISTRATION(CBattleCamp)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    CObject* Copy() override;
    User* GetUserOrMaster() override;
    bool IsBattleCamp() override;
    bool IsAdvanceBase() override;
    CBattleCamp* CastBattleCamp() override;
    bool IsEnemyTo(CCreature* creature) override;
    bool IsAlive() override;
    void SendDB(const char* format, ...) const override;
    void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) override;
    bool IsActive() override;
    double AttackedBy(CCreature* attacker, double damage) override;
    double GotDamageBy(CCreature* attacker, double damage) override;
    virtual bool IsEnabled();

    FVector GetTeleportPos();
    void Appear();
    void Disappear();
    std::uint32_t GetOwningPledgeID();
    std::uint32_t GetOwningPledgeWorldID();
    void SendBattleCampInfo(CUserSocket* socket);
    void SendBattleCampInfoImpl(User* user, double firstValue, double secondValue);
    static double GetStaticMaxHP();
    int GetBaseResidenceId();
    int GetBaseType();
    int GetWorldId() const;


protected:
    void TimerExpired(int id) override;

    static double s_dMaxHP;
    static double s_dPDefend;
    static double s_dMDefend;

    CPledge* m_pOwningPledge;
    std::uint32_t m_nOwningPledgeId;
    std::uint32_t m_nOwningPledgeWorld;
    CResidence* m_pBaseResidence;
    std::int32_t m_nBaseResidenceId;
    std::int32_t m_tAttacked;
    double m_dPreviousHp;
    double m_dPreviousSavedHp;
    std::int32_t m_nDbSaveTick;
    std::int32_t m_nBaseType;
    std::int32_t m_nWorldId;
    char16_t m_sBattleCampName[24];
    std::byte padding_554[4];

private:
    friend class CBattleCampDb;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CBattleCamp)
};

// Current BattleCamp.cpp/BattleCamp.h owner. The current executable uses
// the 2018 0x20-byte map carriers and the 0x60-byte CObjectMap, expanding
// this class from the older donor's 0x70 bytes to its physical 0x98-byte
// extent. SetDisable remains inline in BattleCamp.h.
class CBattleCampDb : public Singleton<CBattleCampDb> {
public:
    CBattleCampDb();
    ~CBattleCampDb() override;

    void AddBattleCamp(const char16_t* className, CBattleCamp* battleCamp);
    void RemoveBattleCamp(std::uint32_t databaseId);
    CSPointer<CBattleCamp> GetBattleCamp(std::uint32_t databaseId);
    bool ReserveBattleCamp(std::uint32_t pledgeId, const FVector& position);
    static void InitBattleCamp();
    void SetDisable(std::uint32_t residenceId) {
        // Current L2Server 0x005936d0.
        FOR_ALL_OBJECT_IN_OBJECT_MAP(m_BattleCampMap, battleCamp, {
            if (battleCamp->m_nBaseResidenceId == static_cast<std::int32_t>(residenceId)) {
                battleCamp->m_data->m_bIsActive = false;
            }
        });
    }
    void Load();
    void Dump();

private:
    CObjectMap<CBattleCamp, 1, 5, RWLock, int> m_BattleCampMap; // +0x08..+0x67
    CYieldLock m_BattleCampDbSect; // +0x68..+0x77
    std::map<std::uint32_t, std::pair<FVector, std::int32_t>> m_mapBattleCampPos; // +0x78..+0x97
};

// The stripped image proves a zero-initialized 0x54-byte array and indexed
// 32-bit reads/writes. This donor-selected spelling is kept only as
// source vocabulary. The storage extent comes from the current image.
extern std::array<std::int32_t, 21> g_aHadMidVictory;

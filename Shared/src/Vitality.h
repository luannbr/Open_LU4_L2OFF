#pragma once

#include "Shared/src/Obj.h"

#include <cstddef>
#include <cstdint>

class CUser;
class CUserSocket;
class CDB;
class CCreature;
class User;
class IPacketHelper;
template <typename TObject>
class CSPointer;

namespace VitalityRestore {

enum Type : std::int32_t {
    BY_BUILDER = 0,
    BY_SKILL = 1,
    BY_SKILL_NONCOUNT = 2,
    ETC = 3,
};

} // namespace VitalityRestore

// Shipping owner: Shared/Vitality.{h,cpp}. The four names and their order
// survive in the matching 2015 Shared type information. Current Cached.exe
// independently fixes the same 0x10-byte layout and "dddd" wire order.
struct CVitalData {
    volatile std::int32_t vitalPoint = 0;
    volatile std::int32_t restoreCount = 0;
    volatile std::int32_t latestResetDate_VP = 0;
    volatile std::int32_t latestResetDate_RC = 0;

    CVitalData() = default;
    CVitalData(const CVitalData& other) noexcept;
    CVitalData& operator=(const CVitalData& other) noexcept;

    void Init(int vitalPoint, int restoreCount, int latestResetDateVP, int latestResetDateRC);
#if defined(L2_BUILD_L2SERVER)
    void Reset(long latestResetDateVP, long latestResetDateRC);
#endif
    void Assemble(IPacketHelper& packet);
    const unsigned char* Disassemble(const unsigned char* packet);
};


class CVitality : public MemoryObject {
public:
    // The PDB exposes the enum type but not its enumerator names. Value 12
    // is fixed by CUser::CheckVitalityReset. 12 and 13 are the only values
    // accepted by the shipping log body.
    enum VitalityGetType : int {
        VitalityReset = 12,
        VitalityLogType13 = 13,
    };

    CVitality(int charDbId, int worldId);
    ~CVitality() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

#if defined(L2_BUILD_L2SERVER)
    void WriteVitalityLog(User* user, VitalityGetType type);
#else
    void WriteVitalityLog(CUser* user, VitalityGetType type);
#endif
    void AssembleForLoading(IPacketHelper& packet);
    void Init(int vitalPoint, int restoreCount, int latestResetDateVP, int latestResetDateRC);
    void Init(CVitalData& data);
    static const unsigned char* SaveVitality(const unsigned char* packet);
    void Update(CVitalData& data);
    void UpdateByAdmin(CVitalData& data, int charDbId, int subjobId);
    bool CheckReset();

#if defined(L2_BUILD_L2SERVER)
    int GetWorldID();
    bool OnTick(User* user);
    void OnLeaveWorld();
    bool IncRestoreCount(VitalityRestore::Type type, CUserSocket* socket, int option, std::int64_t amount);
    bool DecRestoreCount(int count, CUserSocket* socket, int option, int limit);
    bool Restore(int point, VitalityRestore::Type type, CUserSocket* socket, int option, int limit);
    double GetItemDropBonus();
    int GetConsumePoint(
        CCreature* creature,
        int experience,
        double bonus,
        int userLevel,
        double consumeRate,
        int reducePercent,
        bool directConsume
    );
    double GetExpBonus(CSPointer<User>& user);
    double GetPremiumExpBonus(CSPointer<User>& user);
    bool Consume(CSPointer<User>& user, int point);
    int Recharge(CSPointer<User>& user, int point, bool ignoreBonus);
    void SetRestoreCount(int restoreCount);
    bool CanUseConsumeItem();
    bool CanUseConsumeItmeForPremiumUser();
    void SetVitalConsumePointReducePer(CSPointer<User>& user, const int reducePercent);
    bool CanSuspend(CSPointer<User>& user);
    bool IsSuspended(CSPointer<User>& user);
    bool SuspendConsume(CSPointer<User>& user, VitalityRestore::Type type, CUserSocket* socket);
    bool StopSuspend(CSPointer<User>& user);
    void GetExpBonusValue(CSPointer<User>& user, int& expBonus, int& addedBonus) const;
    void GetPremiumExpBonusValue(CSPointer<User>& user, int& expBonus, int& addedBonus) const;
    bool SuspendVitalityEffect(VitalityRestore::Type type, CUserSocket* socket);
    bool StopSuspendVitalityEffect();
    bool IsSuspendedVitalityEffect() const;
    bool CheckSendEffectInfo();
    void ResetForBuilder();
#endif

private:
    friend class CDB;
    friend class CUser;
    friend class User;

    void _Init(int vitalPoint, int restoreCount, int latestResetDateVP, int latestResetDateRC);
    void _Assemble(IPacketHelper& packet);
    void _Save();
    void _Save(int charDbId, int subjobId);
    void _Reset_VitalPoint(long now, long dueTime);
    void _Reset_RestoreCount(long now, long dueTime);
    bool _CheckReset();
#if defined(L2_BUILD_L2SERVER)
    void OnChanged();
#endif

#if defined(L2_BUILD_L2SERVER)
    L2_DECLARE_OBJECT_REGISTRATION(CVitality)
#else
    L2_DECLARE_OBJECT_REGISTRATION(CVitality)
#endif
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CVitality)

    // Current CacheD initializes the four configuration slots to zero. Their
    // original Shared names survive in the matching 2015 type information.
    int EXP_BONUS = 0;
    int PREMIUM_EXP_BONUS = 0;
    int MAX_RESTORE_COUNT = 0;
    int PREMIUM_MAX_RESTORE_COUNT = 0;
    CVitalData m_Data;
    int m_nCharDBId = 0;
    int m_nWorldId = 0;
    int m_nPreviousVitalityPoint = 0;
    int m_nPreviousRestoreCount = 0;
#if defined(L2_BUILD_L2SERVER)
    volatile std::int32_t m_SomethingChanged = 0;
    volatile std::int32_t m_SuspendVitalityEffect = 0;
    volatile std::int32_t m_VitalEffectInfoReset = 0;
    volatile std::int32_t m_SendVitalEffectInfo = 0;
#else
    bool m_SomethingChanged = false;
#endif
};

#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Obj.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CDBSocket;
class CInstantAgit;
class CInstantAgitPostLoadMessage;
class CInstantZone;
class CPacketHelperExBase;
class CPledge;
class CResidenceDeco;
class CResidenceDecoVisible;
class CUserSocket;
class CParserForInstantAgit;
class User;

template <typename T>
class CSPointer;

template <std::size_t BufferSize>
class CPacketHelperEx;

namespace InstantAgitConst {
extern std::int32_t _TIMER_PERIOD;
}

namespace InstantAgitApplyResponse {

enum Code : std::int32_t {
    SUCCESS = 0,
    NOT_APPLY_PERIOD = 1,
    DUPLICATED_AGIT = 2,
    NO_PLEDGE = 3,
    INVALID_PLEDGE_LEVEL = 4,
    NO_PLEDGE_POWER = 5,
    NO_ADENA = 6,
    DUPLICATED_AGIT_SIEGE = 7,
    CODE_MAX = 8,
};

} // namespace InstantAgitApplyResponse


class CInstantAgitData : public MemoryObject {
public:
    explicit CInstantAgitData(int id);
    ~CInstantAgitData() override;

    L2_DECLARE_OBJECT_REGISTRATION(CInstantAgitData)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    void Init(int maxChannel, FVector& position);
    void Delete();
    void SetNextLotteryDate(std::int32_t lotteryDate);
    bool CanUse();
    void ShowAgitStatus(CUserSocket* socket);
    void SetChooseOwner(std::int32_t currentTime, CPacketHelperEx<20480>& packet);
    bool CanChooseOwner(std::int32_t currentTime);
    void AddDecoNpcSlot(FVector& position, int yaw);
    void PostLoad();
    void InitNPC();
    void OnApply(int pledgeId);
    void _TryChooseOwner(std::int32_t currentTime);
    InstantAgitApplyResponse::Code CanApply(User& user, bool builder);
    void ResetAgitAll();
    void OnChooseOwner(const unsigned char* packet);
    bool TryApply(int agitId, User& user, bool builder);
    void TryChooseOwner(std::int32_t currentTime);
    void OnTick(std::int32_t currentTime);


private:
    friend class CInstantAgit;
    friend class CInstantAgitManager;
    friend class CParserForInstantAgit;

    std::int32_t m_Id; // +0x018
    std::byte m_padding1c[4]; // +0x01c
    std::int64_t m_RentalFee; // +0x020
    double m_Commission; // +0x028
    std::int32_t m_RetalPeriod; // +0x030
    std::int32_t m_ApplyPeriod; // +0x034
    std::int32_t m_MinPledgeLevel; // +0x038
    std::int32_t m_InstantZoneTypeId; // +0x03c
    std::int32_t m_MaxChannel; // +0x040
    std::byte m_padding44[4]; // +0x044
    FVector m_InstantAgitPos; // +0x048
    std::int32_t m_nResidenceGrade; // +0x060
    std::int32_t m_nDecoNpcSlotCount; // +0x064
    FVector m_aDecoNpcSlotPositions[5]; // +0x068
    std::int32_t m_aDecoNpcSlotYaws[5]; // +0x0e0
    CYieldLock m_Lock; // +0x0f4
    std::byte m_padding104[4]; // +0x104
    std::set<int> m_ApplicantSet; // +0x108
    std::int32_t m_NextLotteryDate; // +0x128
    std::byte m_padding12c[4]; // +0x12c
    CInstantAgit* m_pFirstInstantAgit; // +0x130
    SharedInstantAgitApplyData* m_SharedInstantAgitApplyData; // +0x138

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CInstantAgitData)
};

class CInstantAgit : public MemoryObject {
public:
    explicit CInstantAgit(CInstantAgitData* data);
    ~CInstantAgit() override;

    L2_DECLARE_OBJECT_REGISTRATION(CInstantAgit)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    double GetHpBonus();
    double GetMpBonus();
    double GetExpRestore();
    bool IsUsingNow(User& user);
    bool CanUse(User& user);
    CInstantZone* GetMyInstantZone();
    int GetInstantZoneTypeId();
    bool TryEnter(User& user);
    void TryLeave(User& user);
    bool OnSetAgitDecoNpcFromNPCServer(int slot, int npcSid);
    void ResetDecoNpc(int slot);
    void SetDecoNpcExpireTime(int slot, int expireTime);
    bool OnResetAgitDecoNpc(CSPointer<User> user, int slot, int& npcClassId, std::int32_t& expireTime);
    void SendDecoNpcInfoAsMsg(CSPointer<User> user);
    void ExpelNotPledgeMember(User& user);
    void ExpelInstantAgit(User& user);
    void SetOwner(int pledgeId);
    bool ResetOwner(User& user);
    bool ResetOwner();
    void OnExpireAgitDecoNpc(const int* slots, const int* results, int count);
    void SendDecoNpcInfo(CSPointer<User> user);
    void TryAddAgitDeco(int npcIndex, int userIndex, int decoId);
    void OnAddAgitDeco(int decoId);
    void TryAddAgitDecoNpc(CSPointer<User> user, int npcClassId, int slot);
    bool OnAddAgitDecoNpc(CSPointer<User> user, int npcClassId, int slot, std::int32_t expireTime);
    void TryResetAgitDecoNpc(CSPointer<User> user, int slot);
    void DoDecoNpcCheck(std::int32_t currentTime);

    int OwnerPledgeId() const noexcept { return m_OwnerPledgeId; }
    int InstantZoneSid() const noexcept { return m_InstantZoneSId; }
    CInstantAgitData* Data() const noexcept { return m_pData; }
    CInstantAgit* Next() const noexcept { return m_pNextInstantAgit; }

private:
    friend class CInstantAgitData;
    friend class CInstantAgitManager;

    void _SetAgitDecoVisible(int depth, int visible);
    void _ValidateAgitDecoEffect(CResidenceDeco& deco);
    bool _SetAgitDeco(int decoId);
    bool _HasDecoNpc(int subType, int factionType) const;

    CInstantAgit* m_pNextInstantAgit; // +0x18
    CInstantAgitData* m_pData; // +0x20
    std::int32_t m_InstantZoneSId; // +0x28
    std::int32_t m_OwnerPledgeId; // +0x2c
    CYieldLock m_Lock; // +0x30
    SharedInstantAgitDeco* m_pDecoList; // +0x40
    CResidenceDecoVisible* m_pResidenceDecoVisible; // +0x48

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CInstantAgit)
};

class CInstantAgitManager : public CIOObject, public Singleton<CInstantAgitManager> {
public:
    CInstantAgitManager();
    ~CInstantAgitManager() override;

    L2_DECLARE_OBJECT_REGISTRATION(CInstantAgitManager)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;

    void Init();
    void Load();
    void OnLoadSummary(const unsigned char* packet);
    void CheckLoadingCompleted();
    void EnterAgitManager(int pledgeId);
    void PostLoad();
    static void SendApplyResponse(
        int userSid,
        int agitId,
        InstantAgitApplyResponse::Code response,
        int firstApplyDate,
        int lastApplyDate,
        std::int64_t rentalFee
    );
    void MakerFinished();
    CInstantAgitData* GetInstantAgitData(int agitId);
    CInstantAgit* GetInstantAgit(int pledgeId);
    CInstantAgit* GetInstantAgit(User& user);
    bool IsAlreadyAppliedPledge(int pledgeId);
    void OnLoadLotteryDate(const unsigned char* packet);
    bool SetRentalPeriod(int agitId, int rentalPeriod);
    bool SetApplyPeriod(int agitId, int applyPeriod);
    bool OnSetDecoNpcFromNPCServer(int pledgeId, int slot, int npcSid);
    void ResetDecoNpc(int pledgeId, int slot);
    bool OnResetInstantAgitDecoNpc(CSPointer<User> user, int pledgeId, int slot, int& npcClassId, std::int32_t& expireTime);
    void SetDecoNpcExpireTime(int pledgeId, int slot, int expireTime);
    void OnLoadApplicant(const unsigned char* packet);
    bool AddInstantAgit(CInstantAgitData* data);
    void AddApplicant(CInstantAgitData& data, int pledgeId);
    void OnApplyInstantAgit(int agitId, int pledgeId);
    bool TryEnterInstantAgit(User& user);
    bool TryLeaveInstantAgit(User& user);
    void ResetInstantAgit(int pledgeId);
    void ExpelNotPledgeMember(User& user);
    void ExpelInstantAgit(User& user);
    CInstantAgit* GetUsingInstantAgit(User& user);
    bool TryReturnInstantAgit(int userSid, User& user);
    void OnExpireInstantAgitDecoNpc(int pledgeId, const int* slots, const int* results, int count);
    void OnLoadInstantAgit(const unsigned char* packet);
    void OnChooseOwner(const unsigned char* packet);
    bool TryReturnInstantAgit(int pledgeId);
    bool ResetLotteryDate(int agitId, int lotteryDate);
    bool SetAgitOwner(int agitId, CPledge* pledge);
    bool TryApplyInstantAgit(int userSid, User& user, int agitId, bool builder);
    void CheckLettery();
    void OnLoadDeco(const unsigned char* packet);
    void OnLoadDecoNpc(const unsigned char* packet);
    void TryAddInstantAgitDeco(int npcIndex, User& user, int decoId);
    void TryAddInstantAgitDeco(int npcIndex, int pledgeId, int decoId);
    void OnAddInstantAgitDeco(int pledgeId, int decoId);
    void NpcPacketHandler(char opcode, int userSid, User& user, int value);
    static bool DBPacketHandler(CDBSocket* socket, const unsigned char* packet);
    void TryAddInstantAgitDecoNpc(CSPointer<User> user, int npcClassId, int slot);
    bool OnAddInstantAgitDecoNpc(CSPointer<User> user, int pledgeId, int npcClassId, int slot, std::int32_t expireTime);
    void TryRemoveInstantAgitDecoNpc(CSPointer<User> user, int slot);
    void DoDecoNpcCheck();


private:
    friend class CInstantAgitData;

    bool _IsAlreadyAppliedPledge(int pledgeId);
    void _DeleteApplicant(int pledgeId);
    void _AddApplicant(CInstantAgitData& data, int pledgeId);
    bool _AddPledgeInstantAgit(int pledgeId, CInstantAgit* agit);

    std::int32_t m_LotteryCountToLoad; // +0x20
    std::int32_t m_LotteryCountLoaded; // +0x24
    std::int32_t m_AgitCountToLoad; // +0x28
    std::int32_t m_AgitCountLoaded; // +0x2c
    std::int32_t m_ApplicantCountToLoad; // +0x30
    std::int32_t m_ApplicantCountLoaded; // +0x34
    std::map<int, CInstantAgitData*> m_InstantAgitDataMap; // +0x38
    CYieldLock m_Lock; // +0x58
    std::map<int, CInstantAgit*> m_PledgeInstantAgitMap; // +0x68
    std::set<int> m_ApplicantSet; // +0x88
    std::int32_t m_AgitManagerIndex; // +0xa8
    [[maybe_unused]] std::byte m_paddingac[4]; // +0xac

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CInstantAgitManager)
};

namespace InstantAgitUtil {

bool IsInInstantAgit(CSPointer<User> user);

} // namespace InstantAgitUtil

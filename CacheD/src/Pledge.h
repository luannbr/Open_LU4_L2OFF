#pragma once

#include "PledgeItemInCache.h"
#include "PledgeMasteryInCache.h"
#include "WareHouse.h"

#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Obj.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/PledgeType.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CETC_MANI_PLEDGE_MASTERY_RESET;
class CETC_MANI_PLEDGE_MASTERY_SET;

class CPledge;
class CPledgeDb;
class CAdminSocket;
class FileServerManager;
class CSocket;
class CUser;
class CDX_PLEDGE_MASTERY_LOAD;
bool RequestGraduateAcademy(CSocket* socket, const unsigned char* packet);
bool RequestLoadTransferredPledgeMaster(CSocket* socket, const unsigned char* packet);
bool DelPledgeEmblemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestSaveDominionRegistry(CSocket* socket, const unsigned char* packet);
namespace RQExHandler {
bool RequestLoadDroidNPCAppearance(CSocket* socket, const unsigned char* packet);
}
using CPledgeSP = CSPointer<CPledge>;

// Pledge.cpp's current load-reply path derives this one-field counter from
// the shipping CPacketHelperEx<20480>. The extra count is at +0x5010. The
// final four bytes are target alignment instead of portable sidecar state.
class CPledgeMemberListPacket final : public CPacketHelperEx<20480> {
public:
    int GetMemberCount() const noexcept;
    void IncrementMemberCount() noexcept;

private:
    int m_memberCount = 0; // +0x5010
};


// Current Pledge.cpp assembler hierarchy. Each concrete assembler is a
// fieldless one-vptr object and the base's immediate carrier keeps the exact
// 0x38-byte layout consumed by both current CacheD and L2Server.
class CPledgeMemberListPacketAssembler {
public:
    struct PledgeMemberInfo {
        CPledge* pledge; // +0x00
        std::uint32_t memberId; // +0x08
        std::uint32_t targetPadding0c; // +0x0c, not written
        const char16_t* characterName; // +0x10
        const char16_t* nickname; // +0x18
        int level; // +0x20
        int characterClass; // +0x24
        int gender; // +0x28
        int race; // +0x2c
        int gradeId; // +0x30
        int pledgeBonus; // +0x34
    };

    virtual int AssembleInternal(CPledgeMemberListPacket& packet, PledgeMemberInfo& member) = 0;

    bool Assemble(CPledge* pledge, CPledgeMemberListPacket& packet, std::set<std::uint32_t>& members);

protected:
    bool AssembleTemplate(CPledgeMemberListPacket& packet, PledgeMemberInfo& member);
    bool SimpleLoadUserAndAssemble(CPledge* pledge, CPledgeMemberListPacket& packet, std::uint32_t memberId);
};


class CPledgeMemberPacketAssembler final : public CPledgeMemberListPacketAssembler {
public:
    int AssembleInternal(CPledgeMemberListPacket& packet, PledgeMemberInfo& member) override;
};

class CAcademyMemberPacketAssembler final : public CPledgeMemberListPacketAssembler {
public:
    int AssembleInternal(CPledgeMemberListPacket& packet, PledgeMemberInfo& member) override;
};

class CNullAssembler final : public CPledgeMemberListPacketAssembler {
public:
    int AssembleInternal(CPledgeMemberListPacket& packet, PledgeMemberInfo& member) override;
};


// Current allocation sites and CPledge accessors fix this complete 0x0c-byte
// value. The class name survives in CPledge::GetAcademyMember's guard.
class CAcademyMember {
public:
    CAcademyMember(std::uint32_t memberId, int joinLevel, std::uint32_t academyMasterId) noexcept;

    std::uint32_t MemberId() const noexcept;
    int JoinLevel() const noexcept;
    std::uint32_t AcademyMasterId() const noexcept;
    void SetAcademyMasterId(std::uint32_t academyMasterId) noexcept;

private:
    std::uint32_t m_memberId; // +0x00
    int m_joinLevel; // +0x04
    std::uint32_t m_academyMasterId; // +0x08
};


struct PledgeSkillValue {
    int level;
    std::int32_t expirationTime;
};


// TotalChecker2015.pdb keeps this exact shared L2Server value name and its
// four source members. Current Cached.exe fixes the same 0x10-byte member
// layout at CPledge+0x2a8 and CPledge::SetPledgeBonus's assignment order.
struct PledgeBonusInfo {
    int m_nCurrentBonusPoint;
    int m_nMaxBonusPoint;
    int m_nLastSaveTime;
    int m_nTodayRewardLev;
};


// No surviving current symbol names this private 0x38-byte allocation. The
// semantic recovery name is local to Pledge.h. Field order is
// fixed by AddSubPledge/CreateSubPledge and every current reader.
struct RecoveredSubPledgeInfo {
    RecoveredSubPledgeInfo(PledgeType pledgeType, std::uint32_t masterId, const wchar_t* name, int memberLimit);

    PledgeType pledgeType; // +0x00
    std::uint32_t masterId; // +0x04
    std::u16string name; // +0x08
    int memberLimit; // +0x30
    std::uint32_t targetPadding; // +0x34, not written
};



// Shipping owner: CacheD/Pledge.{h,cpp}. Portable members use standard
// strings and containers. Offset comments describe the shipping layout.
// SQL, logging and OS synchronization use portable adapters.
class CPledge : public MemoryObject {
public:
    CPledge(
        std::uint32_t pledgeId,
        const char16_t* pledgeName,
        std::uint32_t rulerId,
        const char16_t* rulerName,
        std::uint32_t allianceId,
        std::uint32_t challengeTime,
        std::uint32_t nowWarId,
        std::uint32_t nameValue,
        std::uint32_t oustTime,
        std::uint32_t skillLevel,
        std::uint32_t privateFlag,
        int status,
        int rank,
        std::uint32_t castleId,
        std::uint32_t agitId,
        std::uint32_t fortressId,
        int rootNameValue,
        std::uint32_t legacyUnused,
        std::uint32_t crestId,
        std::uint32_t isGuilty,
        std::uint32_t dismissReservedTime,
        std::uint32_t allianceOustedTime,
        std::uint32_t allianceWithdrawTime,
        std::uint32_t allianceDismissTime,
        std::uint32_t newEmblemId,
        std::uint32_t castleSiegeDefenceCount,
        std::uint32_t masteryResetTime,
        int worldId
    );
    CPledge(std::uint32_t pledgeId, const char16_t* pledgeName, std::uint32_t rulerId, const char16_t* rulerName, int worldId);
    ~CPledge() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void SetPowerGrade(int gradeId, std::uint32_t power);
    std::uint32_t GetPowerByGradeId(int gradeId) const;
    void ReadLock(const wchar_t* file, int line);
    void ReadUnlock();
    static int GetAcademyGraduatedRewardByJoinLevel(int joinLevel, int currentLevel, int pledgeClass);
    int GetAcademyGraduatedReward(std::uint32_t memberId, int pledgeClass);
    void SetPledgeBonus(int bonusIndex, int currentBonusPoint, int maxBonusPoint, int lastSaveTime, int todayRewardLevel);

    CWareHouseSP GetWareHouse();
    PledgeItemInCache* GetPledgeItem() const { return m_items; }
    void SetCheckSumOnDB();
    int _GetMemberCount() const;
    std::uint32_t GetMemberCount() const;
    bool IsSubPledgeMaster(std::uint32_t memberId) const;
    PledgeType GetSubMasterPledgeType(std::uint32_t memberId) const;
    CAcademyMember* GetAcademyMember(std::uint32_t memberId) const;
    bool UpdateSubPledgeMaster(PledgeType pledgeType, std::uint32_t masterId);
    bool SetAcademyMaster(std::uint32_t memberId, std::uint32_t academyMasterId);
    CSPointer<class CUser> GetSubpledgeMaster(PledgeType pledgeType) const;
    const char16_t* GetSubpledgeName(PledgeType pledgeType) const;
    bool SetSubpledgeName(PledgeType pledgeType, const wchar_t* name);

    void AddAcademyMember(std::uint32_t memberId, int joinLevel, std::uint32_t academyMasterId);
    void AddSubPledge(PledgeType pledgeType, std::uint32_t masterId, const wchar_t* name, int memberLimit);
    void AddMember(std::uint32_t memberId, PledgeType pledgeType);
    void SetDawnRoundToMember(int dawnRound);
    void RemoveMember(std::uint32_t memberId, PledgeType pledgeType);
    void ReorganizeMember(std::uint32_t memberId, PledgeType oldPledgeType, PledgeType newPledgeType);
    bool GraduateAcademyMember(std::uint32_t memberId);

    bool DeleteCrest();
    bool DeleteEmblem();
    bool CreateAcademy(const wchar_t* academyName);
    bool CreateSubPledge(PledgeType pledgeType, const wchar_t* name, std::uint32_t masterId);
    bool AddPledgeSkill(std::uint32_t skillId, int level, std::int32_t expirationTime, bool forceUpdate);
    bool DelPledgeSkill(std::uint32_t skillId);
    bool AddPledgeNameValueLog(std::uint32_t pledgeId, int logId, int oldValue, int newValue, int delta);
    bool UpdateNameValue(int requestedDelta, int& updatedValue, int logId);
    int rootNameValue() const noexcept;
    bool CanApplyNameValueDelta(int requestedDelta) const noexcept;
    bool UpdateNameValue(int requestedDelta, int logId);
    void LoadMergedInfo();
    bool SetCastleSiegeDefenceCount(int count);
    void SetLoadTime();
    void SetUnloadTime();
    void ItemCheckOnLoad();
    void ItemCheckOnUnload();
    void LoadPledgeBonus();

    bool AssembleMemberPacket(CPledgeMemberListPacket& packet, PledgeType pledgeType);
    void SendReplyToLoadPledge(CSocket* socket, bool checkPendingWrite);
    void SendReplyToLoadSubPledge(CSocket* socket, unsigned long pledgeTypeMask);
    void SendReplyToLoadSkill(CSocket* socket);

    std::uint32_t GetId() const noexcept;
    std::uint32_t rullerId() const noexcept;
    const char16_t* rullerName() const noexcept;
    std::uint32_t allianceId() const noexcept;
    void setAllianceId(std::uint32_t allianceId) noexcept;
    void setAllianceOustedTime(std::uint32_t allianceOustedTime) noexcept;
    void setAllianceWithdrawTime(std::uint32_t allianceWithdrawTime) noexcept;
    void setAllianceDismissTime(std::uint32_t allianceDismissTime) noexcept;
    std::uint32_t crestId() const noexcept;
    int skillLevel() const noexcept;
    std::uint32_t castleId() const noexcept;
    std::uint32_t agitId() const noexcept;
    void setCastleId(std::uint32_t castleId) noexcept;
    void setAgitId(std::uint32_t agitId) noexcept;
    int GetWorldId() const noexcept;
    const char16_t* GetName() const noexcept;

private:
    friend class CPledgeDb;
    // Current AQ 98 reads CPledge+0xb8 directly before invoking the two
    // existing deletion owners. Keep that direct owner access without
    // manufacturing a public emblem-ID accessor.
    friend bool DelPledgeEmblemPacket(CAdminSocket* socket, const std::uint8_t* packet);
    // Current FileServerManager::TimerExpired reads the name and ruler while
    // holding CPledge+0x120. Friendship keeps that direct shipping
    // access without inventing another public lock/name snapshot API.
    friend class FileServerManager;
    // Both current ServerPacketEx handlers read fields directly from the
    // target CPledge object. The PDB does not keep whether the original
    // source exposed those fields or used friendship. Keep the direct access
    // without inventing public production accessors.
    friend bool RequestGraduateAcademy(CSocket* socket, const unsigned char* packet);
    friend bool RequestLoadTransferredPledgeMaster(CSocket* socket, const unsigned char* packet);
    friend bool RequestChangeMergedName(CSocket* socket, const unsigned char* packet);
    // Current RequestSaveDominionRegistry 0x00559908 locks CPledge+0x120
    // directly. Friendship keeps that access without inventing a public
    // lock method whose source spelling is not present in the PDB.
    friend bool RequestSaveDominionRegistry(CSocket* socket, const unsigned char* packet);
    friend bool RQExHandler::RequestLoadDroidNPCAppearance(CSocket* socket, const unsigned char* packet);
    // Current EtcMani machine code accesses CPledge +0x2d0 directly. PDBs
    // cannot keep whether the source used friendship or an inline accessor.
    // friendship keeps that access without inventing a production API.
    friend class CETC_MANI_PLEDGE_MASTERY_RESET;
    friend class CETC_MANI_PLEDGE_MASTERY_SET;
    // Current DXPacket.cpp 0x004ae778 reads CPledge+0x2d0 directly.
    friend class CDX_PLEDGE_MASTERY_LOAD;

    template <class Function>
    void ServeForAllMember(Function& function);

    void InitializeWareHouse();
    void OnItemCheckFailed(int checkFailType);
    void _AddPledgeSkillWithoutSql(std::uint32_t skillId, int level, std::int32_t expirationTime, bool forceUpdate);
    void SetPledgeTypePresent(PledgeType pledgeType);

    static void AssignFixedName(char16_t (&destination)[24], const char16_t* source, const wchar_t* parameterName);

    L2_DECLARE_OBJECT_REGISTRATION(CPledge)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPledge)

    std::uint32_t m_pledgeId; // +0x018
    char16_t m_name[24]; // +0x01c
    std::uint32_t m_rulerId; // +0x04c
    char16_t m_rulerName[24]; // +0x050
    std::uint32_t m_allianceId; // +0x080
    std::uint32_t m_challengeTime; // +0x084
    std::uint32_t m_nowWarId; // +0x088
    std::uint32_t m_nameValue; // +0x08c
    std::uint32_t m_oustTime; // +0x090
    std::uint32_t m_skillLevel; // +0x094
    std::uint32_t m_privateFlag; // +0x098
    int m_status; // +0x09c
    int m_rank; // +0x0a0
    std::uint32_t m_castleId; // +0x0a4
    std::uint32_t m_agitId; // +0x0a8
    std::uint32_t m_fortressId; // +0x0ac
    int m_rootNameValue; // +0x0b0
    std::uint32_t m_crestId; // +0x0b4
    std::uint32_t m_newEmblemId; // +0x0b8
    std::uint32_t m_isGuilty; // +0x0bc
    std::uint32_t m_dismissReservedTime; // +0x0c0
    std::uint32_t m_allianceOustedTime; // +0x0c4
    std::uint32_t m_allianceWithdrawTime; // +0x0c8
    std::uint32_t m_allianceDismissTime; // +0x0cc
    int m_worldId; // +0x0d0
    std::uint32_t m_masteryResetTime; // +0x0d4

    // Current constructors zero +0xd8 and +0xe0, but no surviving symbol or
    // closed use gives either semantic field an original name. The adjacent
    // alignment regions are not written by either constructor.
    std::uint64_t m_targetUnknownD8; // +0x0d8
    bool m_targetUnknownE0; // +0x0e0
    std::uint8_t m_targetPaddingE1; // +0x0e1, not written
    char16_t m_academyName[24]; // +0x0e2
    std::uint8_t m_targetPadding112[2]; // +0x112, not written
    std::uint32_t m_graduateAcademyCount; // +0x114

    CObjectStorage<CWareHouse> m_warehouse; // +0x118
    mutable RWLock m_pledgeLock; // +0x120
    mutable RWLock m_memberLock; // +0x158
    std::map<PledgeType,
        std::set<std::uint32_t>> m_members; // +0x190
    std::map<std::uint32_t,
        CAcademyMember*> m_academyMembers; // +0x1b0
    std::map<PledgeType,
        RecoveredSubPledgeInfo*> m_subPledges; // +0x1d0
    mutable RWLock m_nameValueLock; // +0x1f0
    bool m_academyExists; // +0x228
    std::uint8_t m_targetPadding229[3]; // +0x229, not written
    std::uint32_t m_pledgeTypeMask; // +0x22c
    std::map<std::uint32_t,
        PledgeSkillValue> m_pledgeSkills; // +0x230
    std::array<std::uint32_t, 9> m_powers; // +0x250
    std::uint32_t m_mergedPledgeId; // +0x274
    char16_t m_mergedPledgeName[24]; // +0x278

public:
    // The paired PDB marks the pledge-bonus carrier public, and current
    // RequestUpdateUserPledgeBonus reads it directly before SetPledgeBonus.
    PledgeBonusInfo m_pledgeBonus[2]; // +0x2a8

private:
    std::uint32_t m_castleSiegeDefenceCount; // +0x2c8
    std::uint32_t m_targetPadding2cc; // +0x2cc, not written
    PledgeMasteryInCache* m_mastery; // +0x2d0
    PledgeItemInCache* m_items; // +0x2d8
};

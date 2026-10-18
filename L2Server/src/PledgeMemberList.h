#pragma once

#include "PledgeMember.h"
#include "Shared/src/Obj.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeObjectMap.h"

#include <cstddef>
#include <cstdint>

class CPledgeMemberService;
class MemberInfo;

class CPledgeMemberList : public MemoryObject {
public:
    struct AcademyMemberInfo final {
        int m_nJoinLevel;
        std::uint32_t m_nMasterDbId;
    };

    CPledgeMemberList();
    ~CPledgeMemberList() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool Remove(std::uint32_t characterDatabaseId);
    bool Add(
        std::uint32_t characterDatabaseId,
        const char16_t* characterName,
        const char16_t* nickname,
        int level,
        int classId,
        int gender,
        int race,
        std::uint32_t serverId,
        PledgeType pledgeType,
        int gradeId,
        int missionCheckTime
    );
    bool ChangeMemberName(std::uint32_t characterDatabaseId, wchar_t* name);
    void ChangeMemberNickName(std::uint32_t characterDatabaseId, wchar_t* nickname);
    void Online(User* user);
    void Offline(User* user);
    void AddAcademyMember(std::uint32_t characterDatabaseId, int joinLevel, std::uint32_t masterDatabaseId);
    std::uint32_t GetAcademyMasterDbId(std::uint32_t characterDatabaseId);
    std::uint32_t GetAcademyStudentDbId(std::uint32_t masterDatabaseId);
    bool SetAcademyMaster(std::uint32_t characterDatabaseId, std::uint32_t masterDatabaseId);
    bool ResetAcademyMaster(std::uint32_t characterDatabaseId);
    bool SetAcademyMaster(const char16_t* characterName, const char16_t* masterName);
    int GetOnlineMemberCount();
    void AcquireSkillForAcademy(bool acquire, std::uint32_t characterDatabaseId, bool notify);
    int Count(PledgeType pledgeType);
    int Size();
    int GetTotalLevel();
    std::uint32_t SetPledgePowerGrade(wchar_t* characterName, int gradeId);
    std::uint32_t GetCharDbId(wchar_t* characterName);
    bool CanBeAcademyMaster(std::uint32_t characterDatabaseId);
    int GetGradeByCharName(wchar_t* characterName);
    void GetGradeIdCountPacket(CPacketHelperEx<20480>& packet);
    wchar_t* GetCharName(std::uint32_t characterDatabaseId);
    PledgeType GetPledgeType(std::uint32_t characterDatabaseId);
    PledgeType ChangeMemberPledgeType(std::uint32_t characterDatabaseId, PledgeType pledgeType);
    bool CheckValidMasterCandidate(const char16_t* masterName);
    void GatherMemberInfo(MemberInfo& memberInfo, PledgeType pledgeType);
    bool GatherMemberInfo(std::uint32_t characterDatabaseId, CPacketHelperEx<20480>& packet);
    bool GetMemberInfo(CPacketHelperEx<20480>& packet, wchar_t* characterName);
    bool GetMemberInfo(CPacketHelperEx<20480>& packet, PledgeType pledgeType, wchar_t* characterName);
    bool HasAcademyMaster(std::uint32_t characterDatabaseId);
    bool HasAcademyStudent(std::uint32_t characterDatabaseId);
    std::uint32_t GetAnyOnlineElite();
    void SetPledgeMissionCheck(std::uint32_t characterDatabaseId, int missionCheckTime);
    void RefreshMemberList();
    void ServeForAllMember(CPledgeMemberService& service);
    void ResetAllNickName(int worldId);
    void ClearAllSkillForAcademy();
    void SelectMemberIDs(std::vector<int>& memberIds) const;
    void SetPledge(std::uint32_t pledgeId) {
        L2_GUARD_FUNCTION();
        m_nPledgeId = pledgeId;
    }
    void SetWorldId(int worldId) {
        L2_GUARD_FUNCTION();
        m_nWorldId = worldId;
    }

private:
    template <typename TFunctor>
    void Update(User* user, TFunctor& functor);

    std::uint32_t _FindAnyOnlineElite() const;

    L2_DECLARE_OBJECT_REGISTRATION(CPledgeMemberList)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPledgeMemberList)

    using MemberIdMapType = std::map<std::uint32_t, CSPointer<CPledgeMember>>;
    using MemberNameMapType = std::map<std::u16string, std::uint32_t, runtime::CStringLess<std::u16string>>;
    using MemberPledgeTypeMultiMapType = std::multimap<PledgeType, std::uint32_t>;
    using AcademyMemberIdMapType = std::map<std::uint32_t, AcademyMemberInfo>;

    int m_nWorldId;
    std::uint32_t m_nPledgeId;
    std::uint32_t m_idAnyElite;
    bool m_bDirtyForAnyElite;
    [[maybe_unused]] std::byte m_alignment25To28[3]{};
    MemberIdMapType m_memberIdMap;
    MemberNameMapType m_memberNameMap;
    MemberPledgeTypeMultiMapType m_memberPledgeTypeMap;
    AcademyMemberIdMapType m_academyMemberIdMap;
    mutable RWLock m_lock;
};

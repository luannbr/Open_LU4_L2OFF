#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/PledgeType.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class User;

namespace PledgeBonus {
enum Activity : std::int32_t;
}


class CPledgeMember : public MemoryObject {
public:
    CPledgeMember(
        std::uint32_t databaseId,
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
    ~CPledgeMember() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void Update(const CPledgeMember& member);
    void Update(User* user);
    void SetOffline();
    void SetName(wchar_t* name);
    void SetNickName(wchar_t* nickname);
    void SetGradeId(int gradeId);
    void SetPledgeType(PledgeType pledgeType);

    std::uint32_t GetDBID() const;
    int GetLevel() const;
    int GetClass() const;
    int GetGender() const;
    int GetRace() const;
    std::uint32_t GetServerId() const;
    std::u16string& GetCharName();
    PledgeType GetPledgeType() const;
    int GetGradeId() const;
    std::u16string& GetNickName();
    bool IsOnline() const;
    void SetMissionCheckTime(int missionCheckTime);
    int GetMissionCheckTime() const;
    PledgeBonus::Activity GetMissionCheck();

private:
    L2_DECLARE_OBJECT_REGISTRATION(CPledgeMember)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPledgeMember)

    std::uint32_t m_dbId;
    int m_level;
    int m_class;
    int m_gender;
    int m_race;
    std::uint32_t m_serverId;
    PledgeType m_pledgeType;
    int m_gradeId;
    std::u16string m_charName;
    std::u16string m_nickName;
    int m_MissionCheckTime;
    [[maybe_unused]] std::uint32_t m_alignment8cTo90{};
};

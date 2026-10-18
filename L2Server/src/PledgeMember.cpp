#include "PledgeMember.h"
#include "Shared/src/MemorySub.h"

#include "Pledge.h"
#include "User.h"

#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/TextRuntime.h"

#include <ctime>
#include <string_view>

namespace {


std::u16string MakeTargetString(const wchar_t* value) {
    if (!value) {
        return std::u16string();
    }
#if defined(_WIN32)
    return std::u16string(reinterpret_cast<const char16_t*>(value));
#else
    return WideToUtf16(std::wstring_view(value));
#endif
}

} // namespace

// static_assert(L2ServerMemory::kCPledgeMemberClassId == 233)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CPledgeMember) == 233);

L2_DEFINE_OBJECT_CLASS_ID(CPledgeMember)

L2_DEFINE_OBJECT_CLASS_NAME(CPledgeMember, m_pClassName_CPledgeMember)

L2_DEFINE_OBJECT_NEW(CPledgeMember)

L2_DEFINE_OBJECT_ALLOCATOR(CPledgeMember, m_pClassName_CPledgeMember)

L2_DEFINE_OBJECT_ADD_REF(CPledgeMember)

L2_DEFINE_OBJECT_RELEASE(CPledgeMember)

CPledgeMember::CPledgeMember(
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
) :
    m_dbId(databaseId),
    m_level(level),
    m_class(classId),
    m_gender(gender),
    m_race(race),
    m_serverId(serverId),
    m_pledgeType(pledgeType),
    m_gradeId(gradeId),
    m_charName(characterName != nullptr ? characterName : u""),
    m_nickName(nickname != nullptr ? nickname : u""),
    m_MissionCheckTime(missionCheckTime) {}

CPledgeMember::~CPledgeMember() = default;

void CPledgeMember::Update(const CPledgeMember& member) {
    L2_GUARD_FUNCTION();
    m_level = member.m_level;
    m_class = member.m_class;
    m_gender = member.m_gender;
    m_race = member.m_race;
    m_serverId = member.m_serverId;
    m_pledgeType = member.m_pledgeType;
    m_gradeId = member.m_gradeId;
    m_nickName = member.m_nickName;
    m_MissionCheckTime = member.m_MissionCheckTime;
}

void CPledgeMember::Update(User* const user) {
    // Current L2Server 0x009d24b4.
    m_level = user->m_data->m_nLevel;
    m_class = user->m_data->m_nClass;
    m_gender = static_cast<int>(user->m_data->m_Sex);
    m_race = static_cast<int>(user->m_data->m_Race);
    m_serverId = user->GetServerId();
    m_pledgeType = static_cast<PledgeType>(user->m_data->m_nPledgeType);
    m_gradeId = user->m_data->m_nGradeId;
    const int missionCheckTime = user->tPledgeBonusTimeStamp[PledgeBonus::ACCESS_MISSION_COMPLETE];
    m_MissionCheckTime = missionCheckTime < 0 ? missionCheckTime : 0;
}

void CPledgeMember::SetOffline() { m_serverId = 0; }
void CPledgeMember::SetName(wchar_t* name) { m_charName = MakeTargetString(name); }
void CPledgeMember::SetNickName(wchar_t* nickname) { m_nickName = MakeTargetString(nickname); }
void CPledgeMember::SetGradeId(int gradeId) { m_gradeId = gradeId; }
void CPledgeMember::SetPledgeType(PledgeType pledgeType) { m_pledgeType = pledgeType; }
std::uint32_t CPledgeMember::GetDBID() const { return m_dbId; }
int CPledgeMember::GetLevel() const { return m_level; }
int CPledgeMember::GetClass() const { return m_class; }
int CPledgeMember::GetGender() const { return m_gender; }
int CPledgeMember::GetRace() const { return m_race; }
std::uint32_t CPledgeMember::GetServerId() const { return m_serverId; }
std::u16string& CPledgeMember::GetCharName() { return m_charName; }
PledgeType CPledgeMember::GetPledgeType() const { return m_pledgeType; }
int CPledgeMember::GetGradeId() const { return m_gradeId; }
std::u16string& CPledgeMember::GetNickName() { return m_nickName; }
bool CPledgeMember::IsOnline() const { return m_serverId != 0; }
void CPledgeMember::SetMissionCheckTime(int value) { m_MissionCheckTime = value; }
int CPledgeMember::GetMissionCheckTime() const { return m_MissionCheckTime; }

PledgeBonus::Activity CPledgeMember::GetMissionCheck() {
    if (m_MissionCheckTime == 0) {
        return PledgeBonus::NOTHING;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    if (m_MissionCheckTime < 0) {
        m_MissionCheckTime = -m_MissionCheckTime;
    }
    if (now < m_MissionCheckTime) {
        return PledgeBonus::NEW_MEMBER;
    }

    const std::time_t adjusted = static_cast<std::time_t>(now - 0x5b68);
    const std::tm* const local = std::localtime(&adjusted);
    const std::int32_t boundary = (((0x186 - local->tm_hour * 0x3c) - local->tm_min) * 0x3c - local->tm_sec) -
        m_MissionCheckTime + static_cast<std::int32_t>(adjusted);
    return boundary < 0 ? PledgeBonus::MISSION_COMPLETE : PledgeBonus::NOTHING;
}

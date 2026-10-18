#include "PledgeMemberList.h"
#include "DB.h"
#include "Pledge.h"
#include "PledgeMemberService.h"
#include "Shared/src/Config.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/UserConst.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cwctype>
#include <optional>
#include <string>

namespace {


#if !defined(_WIN32)
// Compatibility getter storage: valid until the next member-name conversion
// on this thread. TextRuntime performs the conversion without owning the pointer.
wchar_t* TargetMemberNameToHostWide(const std::u16string& name) {
    thread_local std::wstring hostName;
    hostName = Utf16ToWide(runtime::StringView(name));
    return hostName.data();
}
#endif

int CompareMemberNameIgnoreCase(const wchar_t* left, const wchar_t* right) noexcept {
#if defined(_WIN32)
    return ::_wcsicmp(left, right);
#else
    while (*left != L'\0' && *right != L'\0') {
        const wint_t foldedLeft = std::towlower(*left);
        const wint_t foldedRight = std::towlower(*right);
        if (foldedLeft != foldedRight) {
            return foldedLeft < foldedRight ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right) {
        return 0;
    }
    return *left == L'\0' ? -1 : 1;
#endif
}

int CompareMemberNameIgnoreCase(const char16_t* left, const char16_t* right) noexcept {
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return ::_wcsicmp(reinterpret_cast<const wchar_t*>(left), reinterpret_cast<const wchar_t*>(right));
#else
    while (*left != u'\0' && *right != u'\0') {
        const wint_t foldedLeft = std::towlower(static_cast<wint_t>(*left));
        const wint_t foldedRight = std::towlower(static_cast<wint_t>(*right));
        if (foldedLeft != foldedRight) {
            return foldedLeft < foldedRight ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right) {
        return 0;
    }
    return *left == u'\0' ? -1 : 1;
#endif
}

} // namespace

// static_assert(L2ServerMemory::kCPledgeMemberListClassId == 234)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CPledgeMemberList) == 234);

L2_DEFINE_OBJECT_CLASS_ID(CPledgeMemberList)

L2_DEFINE_OBJECT_CLASS_NAME(CPledgeMemberList, m_pClassName_CPledgeMemberList)

L2_DEFINE_OBJECT_NEW(CPledgeMemberList)

L2_DEFINE_OBJECT_ALLOCATOR(CPledgeMemberList, m_pClassName_CPledgeMemberList)

L2_DEFINE_OBJECT_ADD_REF(CPledgeMemberList)

L2_DEFINE_OBJECT_RELEASE(CPledgeMemberList)

CPledgeMemberList::CPledgeMemberList() :
    m_nWorldId(g_nWorldId),
    m_nPledgeId(0),
    m_idAnyElite(0),
    m_bDirtyForAnyElite(false) {}

CPledgeMemberList::~CPledgeMemberList() = default;

void CPledgeMemberList::SelectMemberIDs(std::vector<int>& memberIds) const {
    // Current L2Server 0x009d6dd8.
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_lock);
        memberIds.reserve(m_memberIdMap.size());
        for (const auto& member : m_memberIdMap) {
            memberIds.push_back(static_cast<int>(member.first));
        }
    }
}

bool CPledgeMemberList::Add(
    const std::uint32_t characterDatabaseId,
    const char16_t* const characterName,
    const char16_t* const nickname,
    const int level,
    const int classId,
    const int gender,
    const int race,
    const std::uint32_t serverId,
    const PledgeType pledgeType,
    const int gradeId,
    const int missionCheckTime
) {
    // Current L2Server 0x009d6f44.
    L2_GUARD_FUNCTION();
    CSPointer<CPledgeMember> retained;
    std::optional<CPledgeMember> replacementStorage;
    {
        L2_WRITE_GUARD(m_lock);
        m_bDirtyForAnyElite = true;

        const auto found = m_memberIdMap.find(characterDatabaseId);
        if (found == m_memberIdMap.end()) {
            CPledgeMember* const member = new CPledgeMember(
                characterDatabaseId, characterName, nickname, level, classId, gender, race, serverId, pledgeType, gradeId,
                missionCheckTime
            );
            retained = CSPointer<CPledgeMember>(member, ObjectRefType::FunctionChain);
            m_memberIdMap.emplace(characterDatabaseId, retained);
            m_memberNameMap.emplace(member->GetCharName(), characterDatabaseId);
            m_memberPledgeTypeMap.emplace(pledgeType, characterDatabaseId);
            return true;
        }

        CPledgeMember& replacement = replacementStorage.emplace(
            characterDatabaseId, characterName, nickname, level, classId, gender, race, serverId, pledgeType, gradeId,
            missionCheckTime
        );
        found->second->Update(replacement);
    }
    return false;
}

bool CPledgeMemberList::ChangeMemberName(const std::uint32_t characterDatabaseId, wchar_t* const name) {
    // Current L2Server 0x009d72e0.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock);

        const auto member = m_memberIdMap.find(characterDatabaseId);
        if (member == m_memberIdMap.end()) {
            return false;
        }

        const auto oldName = m_memberNameMap.find(member->second->GetCharName());
        if (oldName != m_memberNameMap.end()) {
            m_memberNameMap.erase(oldName);
            m_memberNameMap.emplace(target_utf16::FromRecoveredWindowsWide(name), characterDatabaseId);
        }
        member->second->SetName(name);
    }
    return true;
}

void CPledgeMemberList::ChangeMemberNickName(const std::uint32_t characterDatabaseId, wchar_t* const nickname) {
    // Current L2Server 0x009d4388.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock);

        const auto member = m_memberIdMap.find(characterDatabaseId);
        if (member != m_memberIdMap.end()) {
            member->second->SetNickName(nickname);
        }
    }
}

void CPledgeMemberList::Online(User* const user) {
    // Current L2Server 0x009d3e98.
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);

    struct OnlineJob final {
        explicit OnlineJob(User* const user) : m_user(user) {}

        void operator()(const CSPointer<CPledgeMember>& member) const { member->Update(m_user); }

        User* m_user;
    } functor(user);

    Update(user, functor);
    m_bDirtyForAnyElite = true;
}

void CPledgeMemberList::Offline(User* const user) {
    // Current L2Server 0x009d3dc8.
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);

    struct OfflineJob final {
        explicit OfflineJob(User* const user) : m_user(user) {}

        void operator()(const CSPointer<CPledgeMember>& member) const {
            member->Update(m_user);
            member->SetOffline();
        }

        User* m_user;
    } functor(user);

    Update(user, functor);
    m_bDirtyForAnyElite = true;
}

template <typename TFunctor>
void CPledgeMemberList::Update(User* const user, TFunctor& functor) {
    // Current L2Server 0x009d3ac4/0x009d3c44.
    L2_GUARD_FUNCTION();
    const auto found = m_memberIdMap.find(user->GetDBID());
    if (found != m_memberIdMap.end()) {
        functor(found->second);
    }
}

void CPledgeMemberList::RefreshMemberList() {
    // Current L2Server 0x009d63f8.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock);
        for (auto& member : m_memberIdMap) {
            CSPointer<User> user =
                g_UserDBIDMap.FindObject(std::CDualKey<int, int>(m_nWorldId, static_cast<int>(member.first)));
            if (user) {
                member.second->Update(user.get());
            } else {
                member.second->SetOffline();
            }
        }
    }
}

void CPledgeMemberList::ResetAllNickName(const int worldId) {
    // Current L2Server 0x009d31dc.
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_lock);
        for (const auto& member : m_memberIdMap) {
            db.RequestResetUserNickname(worldId, member.first);
        }
    }
}

int CPledgeMemberList::Count(const PledgeType pledgeType) {
    // Current L2Server 0x009d3300.
    L2_GUARD_FUNCTION();
    const std::uint64_t count = [&]() -> std::uint64_t {
        L2_READ_GUARD(m_lock);
        const auto range = m_memberPledgeTypeMap.equal_range(pledgeType);
        std::uint64_t count = 0;
        for (auto member = range.first; member != range.second; ++member) {
            ++count;
        }
        return count;
    }();
    return static_cast<int>(count);
}

void CPledgeMemberList::ClearAllSkillForAcademy() {
    // Current L2Server 0x009d6238. The current owner does not
    // acquire m_lock while walking the academy relationship map.
    L2_GUARD_FUNCTION();
    for (const auto& academy : m_academyMemberIdMap) {
        const std::uint32_t masterDatabaseId = academy.second.m_nMasterDbId;
        if (static_cast<std::int32_t>(masterDatabaseId) > 0) {
            AcquireSkillForAcademy(false, masterDatabaseId, false);
        }
    }
}

void CPledgeMemberList::ServeForAllMember(CPledgeMemberService& service) {
    // Current L2Server 0x009d7d38.
    L2_GUARD_FUNCTION();
    std::optional<MemberIdMapType> membersStorage;
    {
        L2_READ_GUARD(m_lock);
        membersStorage.emplace(m_memberIdMap);
    }
    MemberIdMapType& members = *membersStorage;

    for (const auto& member : members) {
        if (!service.CanServe()) {
            break;
        }
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(m_nWorldId, static_cast<int>(member.first)));
        service.Serve(user.get());
    }
}

void CPledgeMemberList::AddAcademyMember(
    const std::uint32_t characterDatabaseId, const int joinLevel, const std::uint32_t masterDatabaseId
) {
    // Current L2Server 0x009d5c4c.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock);
        m_academyMemberIdMap.emplace(characterDatabaseId, AcademyMemberInfo{joinLevel, masterDatabaseId});
    }
}

std::uint32_t CPledgeMemberList::GetAcademyMasterDbId(const std::uint32_t characterDatabaseId) {
    // Current L2Server 0x009d4268.
    L2_GUARD_FUNCTION();
    const std::uint32_t masterDatabaseId = [&]() -> std::uint32_t {
        L2_READ_GUARD(m_lock);
        const auto found = m_academyMemberIdMap.find(characterDatabaseId);
        const std::uint32_t masterDatabaseId = found != m_academyMemberIdMap.end() ? found->second.m_nMasterDbId : 0;
        return masterDatabaseId;
    }();
    return masterDatabaseId;
}

std::uint32_t CPledgeMemberList::GetAcademyStudentDbId(const std::uint32_t masterDatabaseId) {
    // Current L2Server 0x009d2f58.
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_lock);
        for (const auto& academy : m_academyMemberIdMap) {
            if (academy.second.m_nMasterDbId == masterDatabaseId) {
                const std::uint32_t characterDatabaseId = academy.first;
                return characterDatabaseId;
            }
        }
    }
    return 0;
}

int CPledgeMemberList::GetOnlineMemberCount() {
    // Current L2Server 0x009d3748.
    L2_GUARD_FUNCTION();
    const int online = [&]() -> int {
        L2_READ_GUARD(m_lock);
        int online = 0;
        for (const auto& member : m_memberIdMap) {
            if (member.second && member.second->IsOnline()) {
                ++online;
            }
        }
        return online;
    }();
    return online;
}

void CPledgeMemberList::AcquireSkillForAcademy(
    const bool acquire, const std::uint32_t characterDatabaseId, const bool notify
) {
    // Current L2Server 0x009d5d68.
    L2_GUARD_FUNCTION();
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(characterDatabaseId)));
    if (!user) {
        return;
    }

    bool changed = false;
    const bool pledgeMaster = user->IsPledgeMaster();
    if (acquire) {
        changed = user->AcquireSkill(0x4a82, pledgeMaster ? 2 : 1, 0, true, false);
        if (changed) {
            changed = user->AcquireSkill(0x4a83, 1, 0, true, false);
        }
    } else if (!pledgeMaster) {
        changed = user->DeleteAcquireSkill(0x4a82);
        if (changed) {
            changed = user->DeleteAcquireSkill(0x4a83);
        }
    }

    if (changed) {
        user->ValidateChanged(VB_MAX);
    }
    user->SetMaxMagicLevel();
    user->m_nParamChanged |= PCB_USER;
    user->SendSkillList(user->GetSocket(), false, 0);
    if (acquire && notify && user->GetSocket() != nullptr) {
        user->GetSocket()->Send("chdddddddddddS", 0xfe, 0x39, 0, 0xded, 2, 0, 0, 0, 1, 0, 5000, 0, -1, u"");
    }
}

bool CPledgeMemberList::SetAcademyMaster(const std::uint32_t characterDatabaseId, const std::uint32_t masterDatabaseId) {
    // Current L2Server 0x009d65f8. The wchar_t overload owns the lock.
    L2_GUARD_FUNCTION();
    if (masterDatabaseId != 0) {
        for (const auto& academy : m_academyMemberIdMap) {
            if (academy.second.m_nMasterDbId == masterDatabaseId) {
                return false;
            }
        }
    }

    const auto found = m_academyMemberIdMap.find(characterDatabaseId);
    if (found == m_academyMemberIdMap.end() || found->second.m_nMasterDbId != 0 || masterDatabaseId == 0) {
        return false;
    }

    found->second.m_nMasterDbId = masterDatabaseId;
    AcquireSkillForAcademy(true, masterDatabaseId, true);
    return true;
}

bool CPledgeMemberList::ResetAcademyMaster(const std::uint32_t characterDatabaseId) {
    // Current L2Server 0x009d676c. The wchar_t overload owns the lock.
    L2_GUARD_FUNCTION();
    const auto found = m_academyMemberIdMap.find(characterDatabaseId);
    if (found == m_academyMemberIdMap.end() || found->second.m_nMasterDbId == 0) {
        return false;
    }

    const std::uint32_t masterDatabaseId = found->second.m_nMasterDbId;
    found->second.m_nMasterDbId = 0;
    AcquireSkillForAcademy(false, masterDatabaseId, false);
    return true;
}

bool CPledgeMemberList::SetAcademyMaster(const char16_t* const characterName, const char16_t* const masterName) {
    // Current L2Server 0x009d6c78.
    L2_GUARD_FUNCTION();
    std::optional<std::u16string> targetCharacterNameStorage;
    std::optional<std::u16string> targetMasterNameStorage;
    const bool changed = [&] {
        L2_WRITE_GUARD(m_lock);

        const std::u16string& targetCharacterName =
            targetCharacterNameStorage.emplace(characterName != nullptr ? characterName : u"");
        const auto character = m_memberNameMap.find(targetCharacterName);
        if (character == m_memberNameMap.end()) {
            return false;
        }

        const std::u16string& targetMasterName = targetMasterNameStorage.emplace(masterName != nullptr ? masterName : u"");
        const auto master = m_memberNameMap.find(targetMasterName);
        const bool changed = master == m_memberNameMap.end() ? ResetAcademyMaster(character->second)
                                                             : SetAcademyMaster(character->second, master->second);
        return changed;
    }();
    return changed;
}

bool CPledgeMemberList::HasAcademyMaster(std::uint32_t characterDatabaseId) {
    L2_GUARD_FUNCTION();
    const bool result = [&]() -> bool {
        L2_READ_GUARD(m_lock);
        const auto found = m_academyMemberIdMap.find(characterDatabaseId);
        const bool result = found != m_academyMemberIdMap.end() && found->second.m_nMasterDbId != 0;
        return result;
    }();
    return result;
}

bool CPledgeMemberList::HasAcademyStudent(std::uint32_t characterDatabaseId) {
    L2_GUARD_FUNCTION();
    const bool result = [&]() -> bool {
        L2_READ_GUARD(m_lock);
        bool result = false;
        for (const auto& academy : m_academyMemberIdMap) {
            if (academy.second.m_nMasterDbId == characterDatabaseId) {
                result = true;
                break;
            }
        }
        return result;
    }();
    return result;
}

bool CPledgeMemberList::Remove(std::uint32_t characterDatabaseId) {
    L2_GUARD_FUNCTION();
    const bool removed = [&]() -> bool {
        L2_WRITE_GUARD(m_lock);
        bool removed = false;
        const auto member = m_memberIdMap.find(characterDatabaseId);
        if (member != m_memberIdMap.end()) {
            CPledgeMember* const value = member->second.get();
            if (value) {
                m_memberNameMap.erase(value->GetCharName());
                const auto range = m_memberPledgeTypeMap.equal_range(value->GetPledgeType());
                for (auto current = range.first; current != range.second; ++current) {
                    if (current->second == characterDatabaseId) {
                        m_memberPledgeTypeMap.erase(current);
                        break;
                    }
                }
            }
            m_bDirtyForAnyElite = true;
            m_memberIdMap.erase(member);
            removed = true;
        }

        const auto academy = m_academyMemberIdMap.find(characterDatabaseId);
        if (academy != m_academyMemberIdMap.end()) {
            m_academyMemberIdMap.erase(academy);
        } else {
            for (auto& relationship : m_academyMemberIdMap) {
                if (relationship.second.m_nMasterDbId == characterDatabaseId) {
                    relationship.second.m_nMasterDbId = 0;
                    break;
                }
            }
        }
        return removed;
    }();
    return removed;
}

int CPledgeMemberList::Size() {
    L2_GUARD_FUNCTION();
    const int result = [&]() -> int {
        L2_READ_GUARD(m_lock);
        const int result = static_cast<int>(m_memberIdMap.size());
        return result;
    }();
    return result;
}

int CPledgeMemberList::GetTotalLevel() {
    L2_GUARD_FUNCTION();
    const int total = [&]() -> int {
        L2_READ_GUARD(m_lock);
        int total = 0;
        for (const auto& member : m_memberIdMap) {
            if (member.second) {
                total += member.second->GetLevel();
            }
        }
        return total;
    }();
    return total;
}

std::uint32_t CPledgeMemberList::SetPledgePowerGrade(wchar_t* const characterName, const int gradeId) {
    // Current L2Server 0x009d4684.
    L2_GUARD_FUNCTION();
    const std::uint32_t characterDatabaseId = [&]() -> std::uint32_t {
        L2_WRITE_GUARD(m_lock);
        std::uint32_t characterDatabaseId = 0;
        const auto namedMember = m_memberNameMap.find(target_utf16::FromRecoveredWindowsWide(characterName));
        if (namedMember != m_memberNameMap.end()) {
            const auto member = m_memberIdMap.find(namedMember->second);
            if (member != m_memberIdMap.end()) {
                member->second->SetGradeId(gradeId);
                characterDatabaseId = member->second->GetDBID();
            }
        }
        return characterDatabaseId;
    }();
    return characterDatabaseId;
}

std::uint32_t CPledgeMemberList::GetCharDbId(wchar_t* const characterName) {
    // Current L2Server 0x009d3608.
    L2_GUARD_FUNCTION();
    std::optional<std::wstring> requestedNameStorage;
    const std::uint32_t characterDatabaseId = [&] {
        L2_READ_GUARD(m_lock);
        std::uint32_t characterDatabaseId = 0;
        const std::wstring& requestedName = requestedNameStorage.emplace(characterName);
        for (const auto& namedMember : m_memberNameMap) {
#if defined(_WIN32)
            static_assert(sizeof(wchar_t) == sizeof(char16_t));
            wchar_t* const candidateName = reinterpret_cast<wchar_t*>(const_cast<char16_t*>(namedMember.first.c_str()));
#else
            wchar_t* const candidateName = TargetMemberNameToHostWide(namedMember.first);
#endif
            if (CompareMemberNameIgnoreCase(candidateName, requestedName.c_str()) == 0) {
                characterDatabaseId = namedMember.second;
                break;
            }
        }
        return characterDatabaseId;
    }();
    return characterDatabaseId;
}

bool CPledgeMemberList::CanBeAcademyMaster(const std::uint32_t characterDatabaseId) {
    // Current L2Server 0x009d4090 takes no list lock.
    L2_GUARD_FUNCTION();
    const auto found = m_memberIdMap.find(characterDatabaseId);
    if (found == m_memberIdMap.end()) {
        return false;
    }

    CSPointer<CPledgeMember> member = found->second;
    if (member->GetLevel() <= 84) {
        return false;
    }
    return !User::CanJoinAcademyClass(static_cast<ClassType>(member->GetClass()));
}

wchar_t* CPledgeMemberList::GetCharName(const std::uint32_t characterDatabaseId) {
    // Current L2Server 0x009d486c.
    L2_GUARD_FUNCTION();
    wchar_t* const result = [&]() -> wchar_t* {
        L2_READ_GUARD(m_lock);
        const auto found = m_memberIdMap.find(characterDatabaseId);
        if (found == m_memberIdMap.end() || !found->second) {
            return const_cast<wchar_t*>(L"");
        }

        std::u16string& name = found->second->GetCharName();
#if defined(_WIN32)
        static_assert(sizeof(wchar_t) == sizeof(char16_t));
        wchar_t* const result = reinterpret_cast<wchar_t*>(name.data());
#else
        wchar_t* const result = TargetMemberNameToHostWide(name);
#endif
        return result;
    }();
    return result;
}

PledgeType CPledgeMemberList::GetPledgeType(const std::uint32_t characterDatabaseId) {
    // Current L2Server 0x009d4d44.
    L2_GUARD_FUNCTION();
    const PledgeType pledgeType = [&]() -> PledgeType {
        L2_READ_GUARD(m_lock);
        const auto found = m_memberIdMap.find(characterDatabaseId);
        const PledgeType pledgeType = found != m_memberIdMap.end() ? found->second->GetPledgeType() : MAX_PLEDGE_TYPE;
        return pledgeType;
    }();
    return pledgeType;
}

PledgeType CPledgeMemberList::ChangeMemberPledgeType(const std::uint32_t characterDatabaseId, const PledgeType pledgeType) {
    // Current L2Server 0x009d6870.
    L2_GUARD_FUNCTION();
    const PledgeType previousType = [&]() -> PledgeType {
        L2_WRITE_GUARD(m_lock);
        const auto member = m_memberIdMap.find(characterDatabaseId);
        if (member == m_memberIdMap.end()) {
            return ACADEMY;
        }

        const PledgeType previousType = member->second->GetPledgeType();
        member->second->SetPledgeType(pledgeType);
        m_bDirtyForAnyElite = true;
        const auto range = m_memberPledgeTypeMap.equal_range(previousType);
        for (auto current = range.first; current != range.second; ++current) {
            if (current->second == characterDatabaseId) {
                m_memberPledgeTypeMap.erase(current);
                m_memberPledgeTypeMap.emplace(pledgeType, characterDatabaseId);
                break;
            }
        }
        return previousType;
    }();
    return previousType;
}

bool CPledgeMemberList::CheckValidMasterCandidate(const char16_t* const masterName) {
    // Current L2Server 0x009d4eac.
    L2_GUARD_FUNCTION();
    const bool valid = [&]() -> bool {
        L2_READ_GUARD(m_lock);
        bool valid = false;
        for (const auto& namedMember : m_memberNameMap) {
            if (CompareMemberNameIgnoreCase(namedMember.first.c_str(), masterName) != 0) {
                continue;
            }

            const auto member = m_memberIdMap.find(namedMember.second);
            if (member != m_memberIdMap.end()) {
                const PledgeType pledgeType = member->second->GetPledgeType();
                valid = pledgeType == MASTER_PLEDGE || pledgeType == SUB_1;
            }
            break;
        }
        return valid;
    }();
    return valid;
}

void CPledgeMemberList::GatherMemberInfo(MemberInfo& memberInfo, const PledgeType pledgeType) {
    // Current L2Server 0x009d5404.
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_lock);
        const auto range = m_memberPledgeTypeMap.equal_range(pledgeType);
        for (auto current = range.first; current != range.second; ++current) {
            const auto found = m_memberIdMap.find(current->second);
            if (found == m_memberIdMap.end()) {
                continue;
            }

            CSPointer<CPledgeMember> member = found->second;
            const std::uint32_t serverId = member->GetServerId();
            const bool related = pledgeType == ACADEMY ? HasAcademyMaster(serverId) : HasAcademyStudent(serverId);
            memberInfo.Assemble(
                "Sddddddc", member->GetCharName().c_str(), member->GetLevel(), member->GetClass(), member->GetGender(),
                member->GetRace(), serverId, related ? 1 : 0, member->GetMissionCheck()
            );
            memberInfo.IncreaseMemberCount();
        }
    }
}

bool CPledgeMemberList::GatherMemberInfo(std::uint32_t characterDatabaseId, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_lock);
        const auto found = m_memberIdMap.find(characterDatabaseId);
        if (found == m_memberIdMap.end() || !found->second) {
            return false;
        }

        CPledgeMember* const member = found->second.get();
        const PledgeType type = member->GetPledgeType();
        const bool related =
            type == ACADEMY ? HasAcademyMaster(characterDatabaseId) : HasAcademyStudent(characterDatabaseId);
        packet.Assemble(
            "Sdddddddc", member->GetCharName().c_str(), member->GetLevel(), member->GetClass(), member->GetGender(),
            member->GetRace(), member->GetServerId(), type, related ? 1 : 0, member->GetMissionCheck()
        );
    }
    return true;
}

int CPledgeMemberList::GetGradeByCharName(wchar_t* const characterName) {
    // Current L2Server 0x009d44ec.
    L2_GUARD_FUNCTION();
    std::optional<std::u16string> targetCharacterNameStorage;
    const int gradeId = [&] {
        L2_READ_GUARD(m_lock);
        int gradeId = 0;
        const std::u16string& targetCharacterName =
            targetCharacterNameStorage.emplace(target_utf16::FromRecoveredWindowsWide(characterName));
        const auto namedMember = m_memberNameMap.find(targetCharacterName);
        if (namedMember != m_memberNameMap.end()) {
            const auto member = m_memberIdMap.find(namedMember->second);
            if (member != m_memberIdMap.end()) {
                gradeId = member->second->GetGradeId();
            }
        }
        return gradeId;
    }();
    return gradeId;
}

void CPledgeMemberList::GetGradeIdCountPacket(CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x009d3450.
    L2_GUARD_FUNCTION();
    std::array<int, 9> gradeCounts{};
    {
        L2_READ_GUARD(m_lock);
        for (const auto& member : m_memberIdMap) {
            const int gradeId = member.second->GetGradeId();
            const std::uint32_t gradeIndex = static_cast<std::uint32_t>(gradeId) - 1U;
            if (gradeIndex < 9U) {
                int& count = gradeCounts[gradeIndex];
                count = runtime::AddInt32Wrapping(count, 1);
            }
        }
        for (int gradeId = 1; gradeId < 10; ++gradeId) {
            packet.Assemble("dd", gradeId, gradeCounts[static_cast<std::size_t>(gradeId - 1)]);
        }
    }
}

bool CPledgeMemberList::GetMemberInfo(CPacketHelperEx<20480>& packet, wchar_t* const characterName) {
    // Current L2Server 0x009d49e0.
    L2_GUARD_FUNCTION();
    std::optional<std::u16string> targetCharacterNameStorage;
    std::optional<std::u16string> targetRelatedNameStorage;
    std::optional<std::u16string> targetSubPledgeNameStorage;
    {
        L2_READ_GUARD(m_lock);

        // Snapshot the target string before GetCharName reuses its portable
        // thread-local wchar_t conversion buffer.
        const std::u16string& targetCharacterName =
            targetCharacterNameStorage.emplace(target_utf16::FromRecoveredWindowsWide(characterName));
        const auto namedMember = m_memberNameMap.find(targetCharacterName);
        if (namedMember == m_memberNameMap.end()) {
            return false;
        }

        const auto found = m_memberIdMap.find(namedMember->second);
        if (found == m_memberIdMap.end()) {
            return false;
        }

        CSPointer<CPledgeMember>& member = found->second;
        const PledgeType relationshipType = member->GetPledgeType();
        const std::uint32_t relatedDatabaseId = relationshipType == ACADEMY ? GetAcademyMasterDbId(namedMember->second)
                                                                            : GetAcademyStudentDbId(namedMember->second);
        wchar_t* const relatedName = GetCharName(relatedDatabaseId);
        const std::u16string& targetRelatedName = targetRelatedNameStorage.emplace(target_utf16::FromRecoveredWindowsWide(relatedName));

        CPledge* const pledge = CPledge::GetObjectW(m_nPledgeId);
        if (pledge == nullptr) {
            return false;
        }

        std::u16string& nickname = member->GetNickName();
        const PledgeType subPledgeType = member->GetPledgeType();
        wchar_t* const subPledgeName = pledge->GetSubPledgeName(subPledgeType);
        const std::u16string& targetSubPledgeName =
            targetSubPledgeNameStorage.emplace(target_utf16::FromRecoveredWindowsWide(subPledgeName));
        const int gradeId = member->GetGradeId();
        const PledgeType packetPledgeType = member->GetPledgeType();
        packet.Assemble(
            "dSSdSS", packetPledgeType, targetCharacterName.c_str(), nickname.c_str(), gradeId, targetSubPledgeName.c_str(),
            targetRelatedName.c_str()
        );
    }
    return true;
}

bool CPledgeMemberList::GetMemberInfo(
    CPacketHelperEx<20480>& packet, const PledgeType pledgeType, wchar_t* const characterName
) {
    // Current L2Server 0x009d6340 ignores the decoded type.
    L2_GUARD_FUNCTION();
    static_cast<void>(pledgeType);
    return GetMemberInfo(packet, characterName);
}

std::uint32_t CPledgeMemberList::_FindAnyOnlineElite() const {
    // Current L2Server 0x009d38ac. The current range is literally
    // [SUB_1, 101), so only the first royal-guard branch participates.
    L2_GUARD_FUNCTION();
    const auto end = m_memberPledgeTypeMap.lower_bound(static_cast<PledgeType>(101));
    auto current = m_memberPledgeTypeMap.lower_bound(SUB_1);
    while (current != end) {
        const auto member = m_memberIdMap.find(current->second);
        if (member != m_memberIdMap.end() && member->second->IsOnline()) {
            return member->second->GetServerId();
        }
        ++current;
    }
    return 0;
}

std::uint32_t CPledgeMemberList::GetAnyOnlineElite() {
    // Current L2Server 0x009d50ac refreshes this cache while
    // holding the original read lock.
    L2_GUARD_FUNCTION();
    const std::uint32_t result = [&]() -> std::uint32_t {
        L2_READ_GUARD(m_lock);
        if (m_bDirtyForAnyElite) {
            m_bDirtyForAnyElite = false;
            m_idAnyElite = _FindAnyOnlineElite();
        }
        const std::uint32_t result = m_idAnyElite;
        return result;
    }();
    return result;
}

void CPledgeMemberList::SetPledgeMissionCheck(const std::uint32_t characterDatabaseId, const int missionCheckTime) {
    // Current L2Server 0x009d517c.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock);
        if (characterDatabaseId == 0) {
            for (auto& member : m_memberIdMap) {
                member.second->SetMissionCheckTime(missionCheckTime);
            }
        } else {
            const auto found = m_memberIdMap.find(characterDatabaseId);
            if (found != m_memberIdMap.end()) {
                found->second->SetMissionCheckTime(missionCheckTime);
            }
        }
    }
}

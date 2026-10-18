#include "UserUtil.h"

#include "Shared/src/TextRuntime.h"

#include "Skill.h"
#include "User.h"

#include "Shared/src/Log.h"
#include "Shared/src/StandardContainers.h"

#include <cstdlib>

namespace {

CYieldLock g_RandomUserLock;
std::vector<int> g_RandomUser;


} // namespace

namespace UserUtil {

void RegisterToRandomUser(const int userId) {
    // Current L2Server 0x00b714b8, original owner UserUtil.cpp.
    L2_GUARD_FUNCTION();
    L2_GUARD(g_RandomUserLock);
    if (g_RandomUser.capacity() == 0) {
        g_RandomUser.reserve(10000);
    }
    g_RandomUser.push_back(userId);
}

void UnregisterFromRandomUser(const int userId) {
    // Current L2Server 0x00b70ee8, original owner UserUtil.cpp.
    L2_GUARD_FUNCTION();
    L2_GUARD(g_RandomUserLock);
    for (auto current = g_RandomUser.begin(); current != g_RandomUser.end(); ++current) {
        if (*current == userId) {
            g_RandomUser.erase(current);
            break;
        }
    }
}

CSPointer<User> FindRandomUser() {
    // Current L2Server 0x00b70ff0, original owner UserUtil.cpp.
    L2_GUARD_FUNCTION();
    L2_GUARD(g_RandomUserLock);
    const int count = static_cast<int>(g_RandomUser.size());
    if (count < 1) {
        return {};
    }

    int index = std::rand() % count;
    for (int attempt = 0; attempt < count; ++attempt) {
        if (index >= count) {
            index = 0;
        }
        CSPointer<User> user = g_UserMap.FindObject(g_RandomUser[index]);
        ++index;
        if (user) {
            return user;
        }
    }
    return {};
}

CSPointer<User> CastBuffToRandomUser(
    CCreature* const caster, const int skillId, const int skillLevel, const int skillSubLevel
) {
    // Current L2Server 0x00b711c0, original owner UserUtil.cpp.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = FindRandomUser();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No random user at AtomicCastBuffToRandomUser.")
        // report an unavailable target once, with cast context.
        // L2_LOG_ERROR(Log,
        //     L"No random user at AtomicCastBuffToRandomUser.")
        // L2_LOG_WARN(Log,
        //     L"[random buff] Cast skipped: no available user was found in the "
        //     L"random-user registry. Caster '%ls' (object ID %u), "
        //     L"skill ID %d, level %d, sublevel %d.",
        //     caster != nullptr ? caster->GetName() : L"<none>",
        //     caster != nullptr ? static_cast<unsigned int>(caster->Id()) : 0U,
        //     skillId,
        //     skillLevel,
        //     skillSubLevel)
        // L2_LOG_WARN(Log,
        //     "Random buff skipped: no available user. Caster '%s' (object %u), skill %d level %d sublevel %d.",
        //     caster ? WideToUtf8(caster->GetName()).c_str() : "<none>", caster ? static_cast<unsigned>(caster->Id()) : 0U,
        //     skillId, skillLevel, skillSubLevel)
        L2_LOG_WARN(
            Log, "Random buff skipped: no available user. Caster '%s' (object %u), skill %d level %d sublevel %d.",
            caster ? Utf16ToUtf8(caster->GetName()).c_str() : "<none>", caster ? static_cast<unsigned>(caster->Id()) : 0U,
            skillId, skillLevel, skillSubLevel
        );
        return {};
    }

    const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, skillSubLevel), caster);
    if (skillInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No skill info in CastBuffToRandomUser")
        // keep a skill lookup failure as an error and identify it.
        // L2_LOG_ERROR(Log,
        //     L"No skill info in CastBuffToRandomUser")
        // L2_LOG_ERROR(Log,
        //     L"[random buff] Cast failed: skill lookup returned no definition. "
        //     L"Caster '%ls' (object ID %u), skill ID %d, level %d, sublevel %d.",
        //     caster != nullptr ? caster->GetName() : L"<none>",
        //     caster != nullptr ? static_cast<unsigned int>(caster->Id()) : 0U,
        //     skillId,
        //     skillLevel,
        //     skillSubLevel)
        // L2_LOG_ERROR(Log,
        //     "Random buff failed: skill definition is missing. Caster '%s' (object %u), skill %d level %d sublevel %d.",
        //     caster ? WideToUtf8(caster->GetName()).c_str() : "<none>", caster ? static_cast<unsigned>(caster->Id()) : 0U,
        //     skillId, skillLevel, skillSubLevel)
        L2_LOG_ERROR(
            Log, "Random buff failed: skill definition is missing. Caster '%s' (object %u), skill %d level %d sublevel %d.",
            caster ? Utf16ToUtf8(caster->GetName()).c_str() : "<none>", caster ? static_cast<unsigned>(caster->Id()) : 0U,
            skillId, skillLevel, skillSubLevel
        );
        return {};
    }

    skillInfo->ActivateSkill(caster, user->CastWorldObject(), 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    user->ValidateParameters(VB_SKILLMOD, false);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR]Casting buff to random user is succeed: "
    //     L"%d, %d, %d, %d, %d",
    //     static_cast<int>(caster->Id()),
    //     skillId,
    //     skillLevel,
    //     skillSubLevel,
    //     user->GetDBID())
    // a completed cast is informational. Label the reported IDs.
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR]Casting buff to random user is succeed: "
    //     L"%d, %d, %d, %d, %d",
    //     static_cast<int>(caster->Id()),
    //     skillId,
    //     skillLevel,
    //     skillSubLevel,
    //     user->GetDBID())
    // L2_LOG_INFO(Log,
    //     L"[random buff] Cast completed: caster '%ls' (object ID %u), "
    //     L"skill ID %d, level %d, sublevel %d. Target user database ID %d.",
    //     caster->GetName(),
    //     static_cast<unsigned int>(caster->Id()),
    //     skillId,
    //     skillLevel,
    //     skillSubLevel,
    //     user->GetDBID())
    // L2_LOG_INFO(Log,
    //     "Random buff activation returned: caster '%s' (object %u), skill %d level %d sublevel %d, target character %u.",
    //     caster ? WideToUtf8(caster->GetName()).c_str() : "<none>", caster ? static_cast<unsigned>(caster->Id()) : 0U,
    //     skillId, skillLevel, skillSubLevel, static_cast<unsigned>(user->GetDBID()))
    L2_LOG_INFO(
        Log, "Random buff activation returned: caster '%s' (object %u), skill %d level %d sublevel %d, target character %u.",
        caster ? Utf16ToUtf8(caster->GetName()).c_str() : "<none>", caster ? static_cast<unsigned>(caster->Id()) : 0U,
        skillId, skillLevel, skillSubLevel, static_cast<unsigned>(user->GetDBID())
    );
    return user;
}

} // namespace UserUtil

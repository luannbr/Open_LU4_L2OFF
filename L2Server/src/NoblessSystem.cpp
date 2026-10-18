#include "NoblessSystem.h"
#include "AccountJobFinishSignal.h"
#include "CConfig.h"
#include "DB.h"
#include "OlympiadServiceProxy.h"
#include "OlympiadStuff.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "User.h"
#include "UserSocket.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

SharedNoblessInfoMap::SharedNoblessInfoMap() {}

int SharedNoblessInfoMap::GetNoblessIndex(const int characterDatabaseId) const {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const auto position = m_NoblessIndexMap.find(characterDatabaseId);
    return position == m_NoblessIndexMap.end() ? -1 : position->second;
}

const SharedNoblessInfo* SharedNoblessInfoMap::GetNoblessInfo(const int characterDatabaseId) const {
    L2_GUARD_FUNCTION();
    const int index = GetNoblessIndex(characterDatabaseId);
    return index == -1 ? nullptr : SharedNoblessInfo::GetInfo(index);
}

void NoblessSystemUtil::SetNoblessDataToUser(CSPointer<User> user, const int noblessIndex) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"NoblessSystem.cpp",
        //     0x15a,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"NoblessSystem.cpp",
        //     0x15a,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot assign Noblesse data at index %d: player is missing.", noblessIndex);
        return;
    }

    user->m_data->SetNoblessIndex(noblessIndex);
    if (noblessIndex == -1) {
        user->m_data->SetNoblessType(0);
        user->m_data->SetHeroType(0);
    } else {
        const SharedNoblessInfo* const info = SharedNoblessInfo::GetInfo(noblessIndex);
        user->m_data->SetNoblessType(info->nNoblessType);
        user->m_data->SetHeroType(info->nHeroType);
    }
    user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_NOBLESS});
}

bool NoblessSystemUtil::SetNoblessSkillToUser(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"NoblessSystem.cpp",
        //     0x187,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"NoblessSystem.cpp",
        //     0x187,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_ERROR(Log, "Cannot assign Noblesse skills: player is missing.");
        return false;
    }

    static constexpr std::array<std::pair<int, int>, 7> kNoblessSkills{{
        {325, 1},
        {326, 1},
        {327, 1},
        {1323, 1},
        {1324, 1},
        {1326, 1},
        {1327, 1},
    }};
    bool changed = false;
    for (const auto& [skillId, skillLevel] : kNoblessSkills) {
        if (!user->HaveSkill(skillId, skillLevel)) {
            changed = true;
        }
        user->AcquireSkill(skillId, skillLevel, 0, false, true);
    }
    return changed;
}

void SharedNoblessInfoMap::SetNoblessInfo(
    const int characterDatabaseId,
    const int noblessType,
    const int heroType,
    const int winCount,
    const int previousPoint,
    const int olympiadPoint,
    const int matchCount,
    const int olympiadWinCount,
    const int olympiadLoseCount,
    const int olympiadTradePoint,
    const bool newSeason
) {
    L2_GUARD_FUNCTION();

    CSPointer<User> user;
    SharedNoblessInfo* info = nullptr;
    int noblessIndex = 0;
    int previousHeroType = 0;
    {
        L2_GUARD(m_Lock);
        auto position = m_NoblessIndexMap.find(characterDatabaseId);
        if (position == m_NoblessIndexMap.end()) {
            noblessIndex = static_cast<int>(m_NoblessIndexMap.size());
            position = m_NoblessIndexMap.emplace(characterDatabaseId, noblessIndex).first;
        } else {
            noblessIndex = position->second;
        }

        info = SharedNoblessInfo::GetInfo(noblessIndex);
        user = WOlympiadStuff::FindUserByKey(std::CDualKey<int, int>(g_nWorldId, characterDatabaseId));
        info->nCharSid = user ? static_cast<int>(user->GetServerId()) : 0;
        info->nCharDbId = characterDatabaseId;
        info->nNoblessType = noblessType;
        previousHeroType = info->nHeroType;
        info->nHeroType = heroType;
        info->nWinCount = winCount;
        info->nPreviousPoint = previousPoint;
        info->nOlympiadPoint = olympiadPoint;
        info->nMatchCount = matchCount;
        info->nOlympiadWinCount = olympiadWinCount;
        info->nOlympiadLoseCount = olympiadLoseCount;
        info->nOlympiadTradePoint = olympiadTradePoint;
    }

    if (!user) {
        return;
    }

    NoblessSystemUtil::SetNoblessDataToUser(user, noblessIndex);
    if (user->m_data->m_nNoblessType == 1 || user->m_data->m_nNoblessType == 2) {
        if (NoblessSystemUtil::SetNoblessSkillToUser(user)) {
            user->ValidateChanged(VB_SKILLLIST);
        }
    }

    if (previousHeroType != heroType) {
        if (!user->IsHero()) {
            user->ImpoundHeroWeapon();
        }
        user->ValidateHeroSkills();
    }

    if (newSeason) {
        user->ImpoundHeroEntryItem(false);
        user->ImpoundHeroEntryItem(true);
        user->ValidateHeroSkills();
    }

    // if (previousHeroType != heroType && info->nHeroType == 0) {
    if (previousHeroType != heroType && info->nHeroType == HERO_NONE) {
        if (CPledge* const pledge = user->GetPledge()) {
            pledge->ApplyPledgeSkillsTo(user.get(), false, false);
            user->m_nParamChanged |= std::int64_t{PCB_USER};
        }
    }
}

NoblessSystem::NoblessSystem() {}

NoblessSystem::~NoblessSystem() = default;

void NoblessSystem::Init() {
    L2_GUARD_FUNCTION();
    db.RequestLoadAllNobless(true, false);
    if (g_JobFinishSignal.m_nMustLoadNobless <= 0) {
        return;
    }
    if (!g_JobFinishSignal.WaitFinish(JFT_LOAD_NOBLESS, 90)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Problem occurred in Loading "
        //     L"Nobless info",
        //     L"NoblessSystem.cpp",
        //     0xc2)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Problem occurred in Loading "
        //     L"Nobless info",
        //     L"NoblessSystem.cpp",
        //     0xc2)
        L2_LOG_ERROR(
            Log, "Noblesse loading did not receive its completion signal (wait limit 90 seconds); initialization returns."
        );
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Load Success[Nobless]",
    //     L"NoblessSystem.cpp",
    //     0xc6)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Load Success[Nobless]",
    //     L"NoblessSystem.cpp",
    //     0xc6)
    L2_LOG_INFO(Log, "Noblesse loading completion signal received.");
}

void NoblessSystem::LinkNobless(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"NoblessSystem.cpp",
        //     0x11d,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"NoblessSystem.cpp",
        //     0x11d,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot link Noblesse data: player is missing.");
        return;
    }

    const int noblessIndex = m_NoblessInfoMap.GetNoblessIndex(static_cast<int>(user->GetDBID()));
    NoblessSystemUtil::SetNoblessDataToUser(user, noblessIndex);
    if (!WOlympiadStuff::IsAllowedUser(user)) {
        return;
    }

    const SharedNoblessInfo* const info = m_NoblessInfoMap.GetNoblessInfo(static_cast<int>(user->GetDBID()));
    WOlympiadServiceProxy& proxy = Singleton<WOlympiadServiceProxy>::inst();
    if (info == nullptr || info->nPreviousPoint == -1 || HeroType_IsHero(info->nHeroType)) {
        proxy.RegisterUser(user);
    } else {
        proxy.RequestNoblessInfo(user);
    }
}

const SharedNoblessInfo* NoblessSystem::GetNoblessInfo(const int characterDatabaseId) const {
    L2_GUARD_FUNCTION();
    return m_NoblessInfoMap.GetNoblessInfo(characterDatabaseId);
}

void NoblessSystem::SetNoblessInfoFromDB(const int characterDatabaseId, const int noblessType) {
    L2_GUARD_FUNCTION();
    const SharedNoblessInfo* const info = m_NoblessInfoMap.GetNoblessInfo(characterDatabaseId);
    m_NoblessInfoMap.SetNoblessInfo(
        characterDatabaseId, noblessType, info ? info->nHeroType : 0, info ? info->nWinCount : 0,
        info ? info->nPreviousPoint : -1, info ? info->nOlympiadPoint : 0, info ? info->nMatchCount : 0,
        info ? info->nOlympiadWinCount : 0, info ? info->nOlympiadLoseCount : 0, info ? info->nOlympiadTradePoint : 0, false
    );
}

void NoblessSystem::SetNoblessInfoFromOlympiad(
    const int characterDatabaseId,
    const int heroType,
    const int winCount,
    const int previousPoint,
    const int olympiadPoint,
    const int matchCount,
    const int olympiadWinCount,
    const int olympiadLoseCount,
    const int olympiadTradePoint,
    const bool newSeason
) {
    L2_GUARD_FUNCTION();
    const SharedNoblessInfo* const info = m_NoblessInfoMap.GetNoblessInfo(characterDatabaseId);
    m_NoblessInfoMap.SetNoblessInfo(
        characterDatabaseId, info ? info->nNoblessType : 0, heroType, winCount, previousPoint, olympiadPoint, matchCount,
        olympiadWinCount, olympiadLoseCount, olympiadTradePoint, newSeason
    );
}

void NoblessSystemUtil::SendAddOlympiadPointToUser(int userDatabaseId, int point) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = WOlympiadStuff::FindUserByKey(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"NoblessSystem.cpp",
        //     0x1ca)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"NoblessSystem.cpp",
        //     0x1ca)
        L2_LOG_WARN(
            Log, "Cannot notify character %u in world %d of Olympiad point change %d: player was not found.",
            static_cast<unsigned>(userDatabaseId), g_nWorldId, point
        );
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    const std::uint32_t magnitude = point < 0 ? 0U - static_cast<std::uint32_t>(point) : static_cast<std::uint32_t>(point);
    const std::int16_t pointLane = static_cast<std::int16_t>(magnitude);
    const std::u16string targetName = std::u16string(user->GetName());
    socket->Send(
        "chccSch", 0x62, (point < 1) + SystemMessageId::CHARACTER_GAINED_OLYMPIAD_POINTS, 2, 0x0c, targetName.c_str(), 0x15,
        static_cast<int>(pointLane)
    );
}

void NoblessSystemUtil::BroadcastAddOlympiadPoint(int point) {
    L2_GUARD_FUNCTION();
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        if (user->IsInRealWorld() && WOlympiadStuff::IsAllowedUser(CSPointer<User>(user, ObjectRefType::FunctionChain))) {
            CUserSocket* const socket = user->GetSocket();
            const std::uint32_t magnitude =
                point < 0 ? 0U - static_cast<std::uint32_t>(point) : static_cast<std::uint32_t>(point);
            const std::int16_t pointLane = static_cast<std::int16_t>(magnitude);
            const std::u16string targetName = std::u16string(user->GetName());
            socket->Send(
                "chccSch", 0x62, (point < 1) + SystemMessageId::CHARACTER_GAINED_OLYMPIAD_POINTS, 2, 0x0c,
                targetName.c_str(), 0x15, static_cast<int>(pointLane)
            );
        }
    });
}

#include "RespawnTimer.h"
#include "Shared/src/TemplateHelper.h"

#include "Config.h"

#include "BaseSpawnDefine.h"
#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Time32.h"
#include "Runtime.h"
#include "Shared/src/Log.h"
#include "Shared/src/ThreadManager.h"
#include "TextRuntime.h"

namespace l2npc {

std::map<std::u16string, CRespawnTimer*, runtime::CStringLess<std::u16string>> CRespawnTimer::s_StaticMap;
CIOCriticalSection CRespawnTimer::s_Lock;

CRespawnTimer::CRespawnTimer(BaseSpawnDefine* spawnDefinition, const std::u16string& dbName, int absoluteUnixSeconds) :
    mpSpawnDefine(spawnDefinition), mAbsSpawnTime(absoluteUnixSeconds), m_bRespawnByBuilder(false) {
    L2_GUARD_FUNCTION();
    if (mpSpawnDefine) {
        L2_ADD_REF(mpSpawnDefine, ::ObjectRefType::NpcRelated);
    }
    if (mAbsSpawnTime < 0) {
        mAbsSpawnTime = 0;
    }
    mDBName = dbName;

    {
        L2_GUARD(s_Lock);
        s_StaticMap[mDBName] = this;
    }

    AddTimer(25000U, 0);
}

CRespawnTimer::~CRespawnTimer() {
    if (mpSpawnDefine) {
        L2_RELEASE(mpSpawnDefine, ::ObjectRefType::NpcRelated);
    }
}

void CRespawnTimer::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    const std::int32_t unixNow = runtime::crt::Time32Now();
    if (mAbsSpawnTime < unixNow || m_bRespawnByBuilder) {
        mpSpawnDefine->SpawnNpc(true);
        L2_RELEASE(this, ::ObjectRefType::CreateDelete);

        // Noise: Routine respawn details require [Setting] SpawnLog. Failures remain unconditional.
        // if (!mDBName.empty()) {
        if (!mDBName.empty() && g_bSpawnLog) {
            const std::string dbName = Utf16ToUtf8(runtime::StringView(mDBName));
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] DbNpcCreate "
            //     L"[%s][%d][%d][%d][%d]",
            //     dbName.c_str(),
            //     mAbsSpawnTime,
            //     unixNow,
            //     m_bRespawnByBuilder ? 1 : 0,
            //     static_cast<int>(
            //         runtime::GetTickCount()))
            // Log.Add(LOG_NORMAL,
            //     "[NPC spawn] Respawn requested for database NPC '%s': %s. Scheduled Unix time %d, timer Unix time %d.",
            //     WideToUtf8(dbName).c_str(),
            //     m_bRespawnByBuilder ? "builder override" : "scheduled time reached",
            //     mAbsSpawnTime, unixNow)
            L2_LOG_INFO(
                Log, "[NPC spawn] Respawn requested for database NPC '%s': %s; scheduled Unix time %d, timer Unix time %d.",
                dbName.c_str(), m_bRespawnByBuilder ? "builder override" : "scheduled time reached",
                mAbsSpawnTime, unixNow
            );
        }

        {
            L2_GUARD(s_Lock);
            s_StaticMap.erase(mDBName);
        }
        return;
    }

    AddTimer(25000U, 0);
}

void CRespawnTimer::ResetRespawn(char16_t* dbName) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(s_Lock);
        const auto found = s_StaticMap.find(dbName);
        if (found != s_StaticMap.end()) {
            found->second->m_bRespawnByBuilder = true;
        }
    }
}

} // namespace l2npc

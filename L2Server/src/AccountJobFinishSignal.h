#pragma once

#include "Protocol/src/DBProtocol.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CJobFinishSignal {
public:
    CJobFinishSignal();
    ~CJobFinishSignal();

    void Init();
    bool WaitFinish(JobFinishType type, unsigned int timeoutSeconds);
    bool NotifyFinish(JobFinishType type);

    bool SetAllianceLoaded();
    bool SetCastleLoaded();
    bool SetAgitLoaded();
    bool SetFortressLoaded();
    void SetDominionLoaded();
    bool SetWarLoaded();
    void SetNoblessLoaded();
    void SetMatchCountLoaded();
    void SetAllPledgeLoaded();
    bool SetDismissReservedLoaded();
    void SetResevedFinish();
    void SetWOlympiadUserLoaded();

    void* m_hJobFinishSignals[16];
    std::int32_t m_nMustLoadAlliance;
    std::int32_t m_nLoadedAlliance;
    std::int32_t m_nMustLoadCastle;
    std::int32_t m_nLoadedCastle;
    std::int32_t m_nMustLoadAgit;
    std::int32_t m_nLoadedAgit;
    std::int32_t m_nMustLoadDismissReserved;
    std::int32_t m_nLoadedDismissReserved;
    std::int32_t m_nMustLoadWar;
    std::int32_t m_nLoadedWar;
    std::int32_t m_nMustLoadNobless;
    std::int32_t m_nLoadedNobless;
    std::int32_t m_nMustLoadFortress;
    std::int32_t m_nLoadedFortress;
    std::int32_t m_nMustLoadDominion;
    std::int32_t m_nLoadedDominion;
    std::int32_t m_nMustLoadMatchCount;
    std::int32_t m_nLoadedMatchCount;
    std::int32_t m_nMustLoadWOlympiadUser;
    std::int32_t m_nLoadedWOlympiadUser;
    RWLock m_lock;
};

extern CJobFinishSignal g_JobFinishSignal;

class SingleJobFinishSignal {
public:
    explicit SingleJobFinishSignal(const char* jobName);
    ~SingleJobFinishSignal();

    bool NotifyFinish();
    bool WaitFinish(unsigned int timeoutSeconds);

private:
    std::basic_string<char> m_JobName;
    void* m_hSignal;
};

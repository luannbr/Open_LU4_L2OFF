#pragma once

#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>


class CSharedCreatureData;

class AdminRewardSystem {
public:
    AdminRewardSystem();

    void Set(
        bool enabled,
        bool rewardItemEnabled,
        std::int64_t rewardItemCount,
        int npcExistTimeHour,
        bool resetInzoneBindingEnabled,
        int resetInzoneBindingStartUnixSec,
        int resetInzoneBindingEndUnixSec
    );

    bool GiveRewardItem(CSharedCreatureData* talker, int itemClassId);
    bool GiveRewardItem(CSharedCreatureData* talker, int itemClassId, std::int64_t count, int rewardNpcExistTimeHour);
    bool ResetInzoneBinding(CSharedCreatureData* talker);
    bool ResetInzoneBinding(CSharedCreatureData* talker, int instantZoneId);

    bool IsRewardItemEnabled() const;
    bool IsResetInzoneBindingEnabled() const;
    int GetRewardItemCount() const;
    int GetRewardNpcExistTimeHour() const;

private:
    CYieldLock m_lock;
    bool m_enabled = false;
    bool m_rewardItemEnabled = false;
    std::int64_t m_rewardItemCount = 0;
    int m_npcExistTimeHour = 0;
    bool m_resetInzoneBindingEnabled = false;
    int m_resetInzoneBindingStartUnixSec = 0;
    int m_resetInzoneBindingEndUnixSec = 0;
};

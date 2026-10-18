#include "L2NPC/src/AdminRewardSystem.h"

#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/NPCServer.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"

#include <cstring>

AdminRewardSystem::AdminRewardSystem() = default;

void AdminRewardSystem::Set(
    bool enabled,
    bool rewardItemEnabled,
    std::int64_t rewardItemCount,
    int npcExistTimeHour,
    bool resetInzoneBindingEnabled,
    int resetInzoneBindingStartUnixSec [[maybe_unused]],
    int resetInzoneBindingEndUnixSec
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    runtime::raw_atomic::StoreBool(&m_enabled, enabled);
    runtime::raw_atomic::StoreBool(&m_rewardItemEnabled, rewardItemEnabled);
    runtime::raw_atomic::StoreI64(&m_rewardItemCount, rewardItemCount);
    runtime::raw_atomic::StoreI32(&m_npcExistTimeHour, npcExistTimeHour);
    runtime::raw_atomic::StoreBool(&m_resetInzoneBindingEnabled, resetInzoneBindingEnabled);
    // Shipping bug: the sixth argument is ignored and the reset-enabled
    // boolean is zero-extended into the start field.
    runtime::raw_atomic::StoreI32(&m_resetInzoneBindingStartUnixSec, resetInzoneBindingEnabled ? 1 : 0);
    runtime::raw_atomic::StoreI32(&m_resetInzoneBindingEndUnixSec, resetInzoneBindingEndUnixSec);
}

bool AdminRewardSystem::GiveRewardItem(CSharedCreatureData* talker, int itemClassId) {
    L2_GUARD_FUNCTION();
    // The target loads +0x20 first and +0x18 second.
    const int rewardNpcExistTimeHour = runtime::raw_atomic::LoadI32(&m_npcExistTimeHour);
    const std::int64_t count = runtime::raw_atomic::LoadI64(&m_rewardItemCount);
    return GiveRewardItem(talker, itemClassId, count, rewardNpcExistTimeHour);
}

bool AdminRewardSystem::GiveRewardItem(
    CSharedCreatureData* talker, int itemClassId, std::int64_t count, int rewardNpcExistTimeHour
) {
    L2_GUARD_FUNCTION();
    if (!talker) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"AdminRewardSystem:GiveRewardItem : "
        //     L"talker==NULL!!!")
        // Log.Add(LOG_ERROR,
        //     "[NPC reward] Cannot request item %d (count %lld, NPC lifetime %d hours): recipient data is missing.",
        //     itemClassId, static_cast<long long>(count), rewardNpcExistTimeHour)
        L2_LOG_ERROR(
            Log, "[NPC reward] Cannot request item %d (count %lld, NPC lifetime %d hours): recipient data is missing.",
            itemClassId, static_cast<long long>(count), rewardNpcExistTimeHour
        );
        return false;
    }
    if (!runtime::raw_atomic::LoadBool(&m_enabled)) {
        // Noise: A disabled reward system normally rejects optional reward requests. NpcPosEventWithReward reports the
        // enable/disable request once. Log.Add(
        //     LOG_ERROR,
        //     L"AdminRewardSystem:GiveRewardItem : "
        //     L"[m_enabled] not enabled")
        return false;
    }
    if (!runtime::raw_atomic::LoadBool(&m_rewardItemEnabled)) {
        // Noise: Item rewards are disabled. Rejecting each script request is expected and does not need a
        // repeated error. Log.Add(
        //     LOG_ERROR,
        //     L"AdminRewardSystem:GiveRewardItem : "
        //     L"[m_rewardItemEnabled] not enabled")
        return false;
    }

    CPacketHelperEx<20480> packet;
    {
        L2_GUARD(m_lock);
        if (rewardNpcExistTimeHour < 0 || itemClassId < 0 || count < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"AdminRewardSystem:ResetInzoneBinding"
            //     L"(%d, %d, %d, %d)",
            //     talker->m_nId,
            //     rewardNpcExistTimeHour,
            //     itemClassId,
            //     runtime::Int32FromBits(static_cast<std::uint32_t>(count)))
            // Log.Add(LOG_ERROR,
            //     "[NPC reward] Item request for recipient %d was not sent: item ID %d, count %lld and NPC lifetime %d hours
            //     must all be non-negative.", talker->m_nId, itemClassId, static_cast<long long>(count),
            //     rewardNpcExistTimeHour)
            L2_LOG_ERROR(
                Log,
                "[NPC reward] Item request for recipient %d was not sent: item ID %d, count %lld and NPC lifetime %d hours "
                "must all be non-negative.",
                talker->m_nId, itemClassId, static_cast<long long>(count), rewardNpcExistTimeHour
            );
            return false;
        }
        if (count > 4) {
            count = 4;
        }
        packet.Assemble(
            "ddQdQdddd", talker->m_nId, 0, static_cast<long long>(0), itemClassId, static_cast<long long>(count), 0,
            rewardNpcExistTimeHour, 12, 0
        );
    }

    // Current L2NPC.exe 0x00408974, format string at 0x00595f7c.
    g_ns.Send("cb", 0x99, packet.GetLength(), packet.GetPacket());
    return true;
}

bool AdminRewardSystem::ResetInzoneBinding(CSharedCreatureData* talker) {
    L2_GUARD_FUNCTION();
    const int instantZoneId = runtime::raw_atomic::LoadI32(&m_npcExistTimeHour);
    return ResetInzoneBinding(talker, instantZoneId);
}

bool AdminRewardSystem::ResetInzoneBinding(CSharedCreatureData* talker, int instantZoneId) {
    L2_GUARD_FUNCTION();
    if (!talker) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"AdminRewardSystem:ResetInzoneBinding : "
        //     L"talker==NULL!!!")
        // Log.Add(LOG_ERROR,
        //     "[NPC reward] Cannot request a binding reset for instance zone %d: recipient data is missing.",
        //     instantZoneId)
        L2_LOG_ERROR(
            Log, "[NPC reward] Cannot request a binding reset for instance zone %d: recipient data is missing.",
            instantZoneId
        );
        return false;
    }
    if (!runtime::raw_atomic::LoadBool(&m_enabled)) {
        // Noise: A disabled reward system normally rejects optional binding-reset requests. NpcPosEventWithReward reports
        // the enable/disable request once. Log.Add(
        //     LOG_ERROR,
        //     L"AdminRewardSystem:GiveRewardItem : "
        //     L"[m_enabled] not enabled")
        return false;
    }
    if (!runtime::raw_atomic::LoadBool(&m_resetInzoneBindingEnabled)) {
        // Noise: Binding resets are disabled. Rejecting each script request is expected and does not need a
        // repeated error. Log.Add(
        //     LOG_ERROR,
        //     L"AdminRewardSystem:ResetInzoneBinding : "
        //     L"[m_resetInzoneBindingEnabled] not enabled")
        return false;
    }

    CPacketHelperEx<20480> packet;
    {
        L2_GUARD(m_lock);
        const bool resetInzoneBindingEnabled = runtime::raw_atomic::LoadBool(&m_resetInzoneBindingEnabled);
        const int resetInzoneBindingStartUnixSec = runtime::raw_atomic::LoadI32(&m_resetInzoneBindingStartUnixSec);
        const int resetInzoneBindingEndUnixSec = runtime::raw_atomic::LoadI32(&m_resetInzoneBindingEndUnixSec);
        if (instantZoneId < 0 || resetInzoneBindingStartUnixSec < 0 ||
            resetInzoneBindingEndUnixSec < resetInzoneBindingStartUnixSec) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"AdminRewardSystem:ResetInzoneBinding"
            //     L"(%d, %d, %d, %d, %d)",
            //     talker->m_nId,
            //     instantZoneId,
            //     resetInzoneBindingEnabled ? 1 : 0,
            //     resetInzoneBindingStartUnixSec,
            //     resetInzoneBindingEndUnixSec)
            // Log.Add(LOG_ERROR,
            //     "[NPC reward] Binding-reset request for recipient %d was not sent: instance zone %d and start %d Unix
            //     seconds must be non-negative, and end %d must be at least the start. Reset flag is %s.", talker->m_nId,
            //     instantZoneId, resetInzoneBindingStartUnixSec, resetInzoneBindingEndUnixSec, resetInzoneBindingEnabled ?
            //     "enabled" : "disabled")
            L2_LOG_ERROR(
                Log,
                "[NPC reward] Binding-reset request for recipient %d was not sent: instance zone %d and start %d Unix "
                "seconds must be non-negative, and end %d must be at least the start; reset flag is %s.",
                talker->m_nId, instantZoneId, resetInzoneBindingStartUnixSec, resetInzoneBindingEndUnixSec,
                resetInzoneBindingEnabled ? "enabled" : "disabled"
            );
            return false;
        }
        packet.Assemble(
            "ddddddd", talker->m_nId, resetInzoneBindingEnabled ? 1 : 0, resetInzoneBindingStartUnixSec,
            resetInzoneBindingEndUnixSec, 12, 2, instantZoneId
        );
    }

    // Current L2NPC.exe 0x00408bd4, format string at 0x00596264.
    g_ns.Send("chb", 0xfe, 0x2e, packet.GetLength(), packet.GetPacket());
    return true;
}

bool AdminRewardSystem::IsRewardItemEnabled() const {
    // Current L2NPC.exe 0x00488d04.
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::LoadBool(&m_rewardItemEnabled);
}

bool AdminRewardSystem::IsResetInzoneBindingEnabled() const {
    // Current L2NPC.exe 0x00488d90.
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::LoadBool(&m_resetInzoneBindingEnabled);
}

int AdminRewardSystem::GetRewardItemCount() const {
    // Current L2NPC.exe 0x00488e1c.
    L2_GUARD_FUNCTION();
    return runtime::Int32FromBits(static_cast<std::uint32_t>(runtime::raw_atomic::LoadI64(&m_rewardItemCount)));
}

int AdminRewardSystem::GetRewardNpcExistTimeHour() const {
    // Current L2NPC.exe 0x00488ea8.
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::LoadI32(&m_npcExistTimeHour);
}

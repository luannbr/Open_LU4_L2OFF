#include "Boss.h"
#include "Creature.h"
#include "Doppelganger.h"
#include "Pet.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TimeAnalysis.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

struct BuffSortByStartTime {
    bool operator()(const InfectedSkillType& left, const InfectedSkillType& right) const noexcept {
        return left.nStartTime < right.nStartTime;
    }
};


int PackedSkillLevel(const CSkillInfo& skill) noexcept {
    const std::uint32_t bits =
        (static_cast<std::uint32_t>(skill.m_nSubLevel) << 16U) | static_cast<std::uint16_t>(skill.m_nLevel);
    std::int32_t result = 0;
    static_assert(sizeof(result) == sizeof(bits));
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

} // namespace

bool CCreature::IsPledgeBossNPC() {
    // Current L2Server 0x005e374c.
    L2_GUARD_FUNCTION();

    if (!IsNPC()) {
        return false;
    }
    CNPC* const npc = CastNPC();
    if (npc == nullptr || !npc->IsBoss()) {
        return false;
    }
    CBoss* const boss = npc->CastBoss();
    return boss != nullptr && boss->GetPledgeId() > 0;
}

int CCreature::_MakeAbnormalStatusPacket(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();

    std::stable_sort(m_InfectedSkills.begin(), m_InfectedSkills.end(), BuffSortByStartTime{});

    int count = static_cast<int>(m_InfectedSkills.size());
    if (count > 0x6c) {
        // Log.Add(
        //     LOG_ERROR,
        //     "abnormal status exceeds MAX_ABNORMAL_STATUS "
        //     "at file[%s], line[%d]",
        //     "creature_new.cpp",
        //     0x32)
        // L2_LOG_ERROR(Log,
        //     "abnormal status exceeds MAX_ABNORMAL_STATUS "
        //     "at file[%s], line[%d]",
        //     "creature_new.cpp",
        //     0x32)
        L2_LOG_ERROR(
            Log,
            "Creature object %u has %d abnormal-status entries above packet limit 108; only the first 108 sorted entries "
            "are considered, stored effects are retained.",
            static_cast<unsigned>(m_nId), count
        );
        count = 0x6c;
    }

    std::int32_t now = 0;
    CIOObject::SetTimeStamp(&now);
    for (int index = 0; index < count; ++index) {
        InfectedSkillType& infected = runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index));
        CSkillInfo* const skill = infected.pSkillInfo;
        if (IsLockedBuff(skill->m_nSkillId)) {
            continue;
        }

        int remaining = -1;
        if (skill->m_nAbnormalTime != -1 && skill->m_OperateType != SKILL_AURA_ACTIVITY &&
            skill->m_OperateType != SKILL_AURA_ACTIVITY2) {
            const int elapsed = runtime::SubtractInt32Wrapping(now, infected.nStartTime);
            if (skill->m_nAbnormalTime == -2) {
                remaining = infected.nDuration;
            } else {
                remaining = runtime::SubtractInt32Wrapping(GetAbnormalTime(skill, false), elapsed);
            }
            if (remaining < 0) {
                remaining = 0;
            }
        }

        const int abnormalType = static_cast<int>(static_cast<std::int16_t>(skill->m_nAbnormalType));
        if (remaining > 0x7fff) {
            packet.Assemble(
                "ddhhdd", skill->m_nSkillId, PackedSkillLevel(*skill), abnormalType, 0x7fff, remaining,
                infected.nSourceCreatureId
            );
            continue;
        }
        packet.Assemble(
            "ddhhd", skill->m_nSkillId, PackedSkillLevel(*skill), abnormalType,
            static_cast<int>(static_cast<std::int16_t>(remaining)), infected.nSourceCreatureId
        );
    }
    return packet.GetAssembledCount();
}

int CCreature::MakeAbnormalStatusPacket(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InfectedSkillsSect);
    return _MakeAbnormalStatusPacket(packet);
}

int CCreature::MakeAbnormalVisualEffectPacket(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InfectedSkillsSect);

    for (const int visualEffect : m_AbnormalVisualEffectMap) {
        packet.Assemble("h", visualEffect);
    }

    if (IsDoppelganger()) {
        CDoppelganger* const summon = CastDoppelganger();
        User* const master = summon->GetMaster();
        if (master != nullptr && master->m_PledgeGameUser.IsPlayer() &&
            m_AbnormalVisualEffectMap.find(0x4d) == m_AbnormalVisualEffectMap.end()) {
            packet.Assemble("h", 0x4d);
        }
    }

    return packet.GetAssembledCount();
}

void CCreature::SendAbnormalStatusToUserTargetsMe(User& user) {
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    const std::int16_t count = static_cast<std::int16_t>(MakeAbnormalStatusPacket(packet));
    user.GetSocket()->Send(
        "chdhb", 0xfe, 0xe6, static_cast<int>(GetSmartID().Sid()), static_cast<int>(count), packet.GetLength(),
        packet.GetPacket()
    );
}

void CCreature::_BroadCastAbnormalStatusToUsersTargetMe() {
    // Current L2Server 0x005e5860.
    L2_GUARD_FUNCTION();
    if (m_UsersTargetMe.empty()) {
        return;
    }

    CPacketHelperEx<20480> packet;
    const std::int16_t count = static_cast<std::int16_t>(_MakeAbnormalStatusPacket(packet));
    DefaultIOBuffer* const buffer = ::MakeConstBuffer(
        "chdhb", 0xfe, 0xe6, static_cast<int>(GetSmartID().Sid()), static_cast<int>(count), packet.GetLength(),
        packet.GetPacket()
    );
    if (buffer == nullptr) {
        return;
    }

    g_Timer.BeginJob(JOB_AsyncSendConstBuffer);
    auto target = m_UsersTargetMe.begin();
    while (target != m_UsersTargetMe.end()) {
        CSPointer<User> user = g_UserMap.FindObject(*target);
        if (!user) {
            target = m_UsersTargetMe.erase(target);
            continue;
        }
        buffer->AddRef();
        user->GetSocket()->AsyncSendConstBuffer(buffer);
        ++target;
    }
    buffer->Release();
    g_Timer.EndJob(JOB_AsyncSendConstBuffer);
}

void CCreature::AddUserTargetsMe(User& user) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_InfectedSkillsSect);
        m_UsersTargetMe.push_back(static_cast<int>(user.m_nId));
    }
    SendAbnormalStatusToUserTargetsMe(user);
}

void CCreature::ResetListTargetMe() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InfectedSkillsSect);
    m_UsersTargetMe.clear();
}

void CCreature::DelUserTargetsMe(User& user) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InfectedSkillsSect);
    auto found = m_UsersTargetMe.begin();
    while (found != m_UsersTargetMe.end() && *found != static_cast<int>(user.m_nId)) {
        ++found;
    }
    if (found != m_UsersTargetMe.end()) {
        m_UsersTargetMe.erase(found);
    }
}

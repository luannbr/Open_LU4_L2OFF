#include "PledgeSkill.h"
#include "CreatureController.h"
#include "L2Packet.h"
#include "Pledge.h"
#include "PledgeMastery.h"
#include "PledgeSkillInfo.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/Config.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Skill.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include <array>
#include <cwchar>
#include <string>

namespace {


std::u16string FormatPledgeSkillBuilderMessage(
    const int skillId, const int skillLevel, const PledgeActiveStatus::Value status, const int remainSecond
) {
    std::array<char16_t, 128> message{};
    FormatUtf16To(
        message.data(), message.size(), u"(%d,%d): (%d/%d)", skillId, skillLevel, static_cast<int>(status), remainSecond
    );
    return std::u16string(message.data());
}

} // namespace

namespace PledgeSkillStuff::detail {

void Send_S_EX_PLEDGE_SKILL_ACTIVATE(User* const user, const int result) {
    // Current L2Server 0x009ca000.
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x63,
        //     L"(pUser != NULL)")
        // Noise: Asynchronous activation completion can outlive its requester. No reply can be sent. The callback separately
        // reports operation failures. L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x63,
        //     L"(pUser != NULL)")
        return;
    }
    protocol::S_EX_PLEDGE_SKILL_ACTIVATE packet{};
    packet.nResult = static_cast<char>(result);
    PledgeStuff::SendPacket(user, S_EX_PLEDGE_SKILL_ACTIVATE, packet);
}

void AddPledgeSkillInternal(
    CPledge* const pledge, const PledgeSkillInfo* const info, const bool sendAcquireMessage, const bool synchronizeRaid
) {
    // Current L2Server 0x009cb924.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x3e,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x3e,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot add a clan skill: the clan is unavailable.");
        return;
    }
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x3f,
        //     L"(pInfo != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x3f,
        //     L"(pInfo != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot add a skill to clan %u: its clan-skill definition is unavailable.",
            static_cast<unsigned>(pledge->GetDbId())
        );
        return;
    }

    const int remainSecond = pledge->GetSkill()->GetRemainSecond(info->skill_id, runtime::crt::Time32Now());
    const std::int32_t expiryTime = PledgeStuff::CalculateExpiryTime(info->expiry_time);
    if (pledge->AddPledgeSkillWithSaveAndNotify(
            info->skill_id, info->skill_level, remainSecond + expiryTime, synchronizeRaid, sendAcquireMessage, false
        )) {
        PledgeStuff::Write_LOG_PLEDGE_SKILL_ADD(pledge, info->skill_id, info->skill_level, remainSecond + info->expiry_time);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]AddPledgeSkillInternal failed: %d, %d, %d",
        //     L"PledgeSkill.cpp",
        //     0x50,
        //     static_cast<int>(pledge->GetDbId()),
        //     info->skill_id,
        //     info->skill_level)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]AddPledgeSkillInternal failed: %d, %d, %d",
        //     L"PledgeSkill.cpp",
        //     0x50,
        //     static_cast<int>(pledge->GetDbId()),
        //     info->skill_id,
        //     info->skill_level)
        L2_LOG_ERROR(
            Log, "Adding skill %d level %d to clan %u failed.", info->skill_id, info->skill_level,
            static_cast<unsigned>(pledge->GetDbId())
        );
    }
}

} // namespace PledgeSkillStuff::detail

// static_assert(L2ServerMemory::kPledgeSkillClassId == 231)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::PledgeSkill) == 231);

L2_DEFINE_OBJECT_CLASS_ID(PledgeSkill)

L2_DEFINE_OBJECT_CLASS_NAME(PledgeSkill, m_pClassName_PledgeSkill)

L2_DEFINE_OBJECT_NEW(PledgeSkill)

L2_DEFINE_OBJECT_ALLOCATOR(PledgeSkill, m_pClassName_PledgeSkill)

L2_DEFINE_OBJECT_ADD_REF(PledgeSkill)

L2_DEFINE_OBJECT_RELEASE(PledgeSkill)

PledgeSkill::PledgeSkill() : m_TransactionLock(0x1e) {}

PledgeSkill::~PledgeSkill() = default;

namespace PledgeSkillStuff {

bool IsPledgeSkill(const int skillId, const int skillLevel) {
    // Current L2Server 0x009c9f50.
    L2_GUARD_FUNCTION();
    return PledgeSkillDB::inst().FindSkill(skillId, skillLevel) != nullptr;
}

bool CanAddSkill(const User* user, const PledgeSkillInfo* skillInfo) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x323,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x323,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot check clan-skill eligibility: the user is unavailable; eligibility is denied.");
        return false;
    }
    if (skillInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x324,
        //     L"(pInfo != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x324,
        //     L"(pInfo != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check clan-skill eligibility: the skill definition is unavailable; eligibility is denied."
        );
        return false;
    }
    return static_cast<int>(skillInfo->social_class) <= const_cast<User*>(user)->GetSocialClass();
}

bool HasRequirement(const PledgeSkillInfo* const skillInfo, const int pledgeLevel, const PledgeMastery* const mastery) {
    // Current L2Server.exe 0x009c9cd4.
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x22c,
        //     L"(pInfo != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x22c,
        //     L"(pInfo != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot check clan-skill requirements: the skill definition is unavailable; requirements are treated as unmet."
        );
        return false;
    }
    if (mastery == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x22d,
        //     L"(pMastery != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x22d,
        //     L"(pMastery != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot check requirements for clan skill %d level %d: clan mastery data is unavailable; requirements are "
            "treated as unmet.",
            skillInfo->skill_id, skillInfo->skill_level
        );
        return false;
    }
    return skillInfo->Condition.CheckLevel(pledgeLevel) &&
        skillInfo->Condition.CheckMastery(mastery->GetPoint(skillInfo->Condition.mastery_id));
}

PledgeActiveStatus::Value CheckSkillStatus(
    const PledgeSkill* const skills,
    const int pledgeLevel,
    const PledgeMastery* const mastery,
    const PledgeSkillInfo* const skillInfo
) {
    // Current L2Server.exe 0x009cad5c.
    L2_GUARD_FUNCTION();
    if (skills == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x242,
        //     L"(pPledgeSkill != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x242,
        //     L"(pPledgeSkill != NULL)")
        L2_LOG_ERROR(Log, "Cannot determine clan-skill status: clan skill storage is unavailable; returning locked status.");
        return PledgeActiveStatus::locked;
    }
    if (skillInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x243,
        //     L"(pInfo != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x243,
        //     L"(pInfo != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot determine clan-skill status: the skill definition is unavailable; returning locked status."
        );
        return PledgeActiveStatus::locked;
    }
    if (skills->HasSkill(skillInfo->skill_id)) {
        return PledgeActiveStatus::activated;
    }
    return HasRequirement(skillInfo, pledgeLevel, mastery) ? PledgeActiveStatus::inactivated : PledgeActiveStatus::locked;
}

void AddPledgeSkillsByLevelAndMastery(CPledge* const pledge, const bool synchronizeRaid) {
    // Current L2Server 0x009cdc88.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr || pledge->GetSkill() == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x341,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x341,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot add automatic clan skills: %s is unavailable.",
            pledge == nullptr ? "the clan" : "clan skill storage"
        );
        return;
    }
    for (const PledgeSkillInfo* const info : PledgeSkillDB::inst().GetInfos()) {
        if (info != nullptr && info->add_type == PledgeAddType::AUTO &&
            HasRequirement(info, static_cast<int>(pledge->GetPledgeLevel()), pledge->GetMastery()) &&
            pledge->GetSkill()->CanAddSkill(info->skill_id, info->skill_level)) {
            detail::AddPledgeSkillInternal(pledge, info, false, synchronizeRaid);
        }
    }
}

void SendPledgeSkillInfoTo(User* const user, const int skillId, const int skillLevel) {
    // Current L2Server 0x009cd0b8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x259,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x259,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot send clan skill %d level %d information: the requesting user is unavailable.", skillId, skillLevel
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || pledge->GetSkill() == nullptr) {
        return;
    }

    const PledgeSkillInfo* const info = PledgeSkillDB::inst().FindSkill(skillId, skillLevel);
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No pledge skill info: %d, %d, %d",
        //     L"PledgeSkill.cpp",
        //     0x268,
        //     static_cast<int>(user->GetDBID()),
        //     skillId,
        //     skillLevel)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No pledge skill info: %d, %d, %d",
        //     L"PledgeSkill.cpp",
        //     0x268,
        //     static_cast<int>(user->GetDBID()),
        //     skillId,
        //     skillLevel)
        L2_LOG_ERROR(
            Log, "Cannot send clan skill %d level %d information to character %u: no clan-skill definition is registered.",
            skillId, skillLevel, static_cast<unsigned>(user->GetDBID())
        );
        return;
    }
    if (g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel), user) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]invalid skill for User: %d, %d, %d",
        //     L"PledgeSkill.cpp",
        //     0x26f,
        //     static_cast<int>(user->GetDBID()),
        //     skillId,
        //     skillLevel)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]invalid skill for User: %d, %d, %d",
        //     L"PledgeSkill.cpp",
        //     0x26f,
        //     static_cast<int>(user->GetDBID()),
        //     skillId,
        //     skillLevel)
        L2_LOG_ERROR(
            Log,
            "Cannot send clan skill %d level %d information to character %u: the general skill definition is unavailable.",
            skillId, skillLevel, static_cast<unsigned>(user->GetDBID())
        );
        return;
    }

    protocol::S_EX_PLEDGE_SKILL_INFO packet{};
    packet.nSkillID = skillId;
    packet.nSkillLevel = skillLevel;
    packet.nRemainSecond = pledge->GetSkill()->GetRemainSecond(skillId, runtime::crt::Time32Now());
    packet.nStatus =
        CheckSkillStatus(pledge->GetSkill(), static_cast<int>(pledge->GetPledgeLevel()), pledge->GetMastery(), info);
    user->GetSocket()->Send(S_EX, S_EX_PLEDGE_SKILL_INFO, packet);
}

void RequestToAddPledgeSkill(User* const user, const int skillId, const int skillLevel) {
    // Current L2Server 0x009cd274.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x285,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x285,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot request clan skill %d level %d activation: the requesting user is unavailable.", skillId, skillLevel
        );
        return;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || pledge->GetSkill() == nullptr || !user->HavePledgePower(PP_CONTROL_SYSTEM_FUNC)) {
        user->SendSystemMessage(SystemMessageId::NO_AUTHORITY_FOR_CLAN_DEVELOPMENT);
        return;
    }

    JobTransactionLong jobType = 1;
    JobTransactionLock& transaction = pledge->GetSkill()->GetTransactionLock();
    if (!transaction.BeginJob(&jobType)) {
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d, %d",
        //     L"PledgeSkill.cpp",
        //     0x29d,
        //     static_cast<int>(pledge->GetDbId()),
        //     static_cast<int>(jobType))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d, %d",
        //     L"PledgeSkill.cpp",
        //     0x29d,
        //     static_cast<int>(pledge->GetDbId()),
        //     static_cast<int>(jobType))
        L2_LOG_WARN(
            Log, "Clan %u rejected activation of skill %d level %d: transaction %d is already active.",
            static_cast<unsigned>(pledge->GetDbId()), skillId, skillLevel, static_cast<int>(jobType)
        );
        return;
    }

    bool asynchronous = false;
    if (user->GetController()->GetCurrentActionType() == ACT_SKILL) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_FUNCTION_WHILE_CASTING);
    } else {
        const PledgeSkillInfo* const info = PledgeSkillDB::inst().FindSkill(skillId, skillLevel);
        if (info == nullptr) {
            user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]FindSkill failed: %d, %d, %d",
            //     L"PledgeSkill.cpp",
            //     0x2b2,
            //     static_cast<int>(user->GetDBID()),
            //     skillId,
            //     skillLevel)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]FindSkill failed: %d, %d, %d",
            //     L"PledgeSkill.cpp",
            //     0x2b2,
            //     static_cast<int>(user->GetDBID()),
            //     skillId,
            //     skillLevel)
            L2_LOG_ERROR(
                Log, "Clan skill %d level %d activation for character %u stopped: no clan-skill definition is registered.",
                skillId, skillLevel, static_cast<unsigned>(user->GetDBID())
            );
        } else if (g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel), user) == nullptr) {
            user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]GetSkillInfo failed: %d, %d, %d",
            //     L"PledgeSkill.cpp",
            //     0x2bc,
            //     static_cast<int>(user->GetDBID()),
            //     skillId,
            //     skillLevel)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]GetSkillInfo failed: %d, %d, %d",
            //     L"PledgeSkill.cpp",
            //     0x2bc,
            //     static_cast<int>(user->GetDBID()),
            //     skillId,
            //     skillLevel)
            L2_LOG_ERROR(
                Log,
                "Clan skill %d level %d activation for character %u stopped: the general skill definition is unavailable.",
                skillId, skillLevel, static_cast<unsigned>(user->GetDBID())
            );
        } else {
            const int remainSecond = pledge->GetSkill()->GetRemainSecond(skillId, runtime::crt::Time32Now());
            if (remainSecond >= info->expiry_time) {
                user->SendSystemMessage(SystemMessageId::CANNOT_EXTEND_SEAL_ACTIVATION_TIME_FURTHER);
                detail::Send_S_EX_PLEDGE_SKILL_ACTIVATE(user, 2);
            } else if (pledge->GetNameValue() < info->Cost.name_value) {
                user->SendSystemMessage(SystemMessageId::SKILL_LEARNING_FAILED_NOT_ENOUGH_CLAN_REPUTATION);
            } else if (!HasRequirement(info, static_cast<int>(pledge->GetPledgeLevel()), pledge->GetMastery())) {
                user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[PLEDGE][%s][%d]HasRequirement failed: %d, %d, %d",
                //     L"PledgeSkill.cpp",
                //     0x2d4,
                //     static_cast<int>(user->GetDBID()),
                //     skillId,
                //     skillLevel)
                // L2_LOG_ERROR(Log,
                //     L"[PLEDGE][%s][%d]HasRequirement failed: %d, %d, %d",
                //     L"PledgeSkill.cpp",
                //     0x2d4,
                //     static_cast<int>(user->GetDBID()),
                //     skillId,
                //     skillLevel)
                L2_LOG_WARN(
                    Log,
                    "Clan skill %d level %d activation for character %u was rejected: clan level or mastery requirements "
                    "are unmet.",
                    skillId, skillLevel, static_cast<unsigned>(user->GetDBID())
                );
            } else {
                asynchronous = true;
                PledgeStuff::PledgeCostHelper helper(user, pledge);
                helper.SetCost(info->Cost);
                PledgeStuff::Write_LOG_PLEDGE_SKILL_ACTIVATE(
                    user, pledge, info->skill_id, info->skill_level, remainSecond, info->expiry_time,
                    PledgeStuff::ActionStep::Try
                );
                const UserCapture userCapture{user->Id(), user->m_data->GetWorldId(), static_cast<int>(user->GetDBID())};
                const PledgeCapture pledgeCapture{pledge->Id(), pledge->GetWorldId(), static_cast<int>(pledge->GetDbId())};
                helper.Do(MakeLambdaJob([userCapture, pledgeCapture, info, remainSecond, jobType](const int result) {
                    User* const requester = userCapture.GetUser();
                    detail::Send_S_EX_PLEDGE_SKILL_ACTIVATE(requester, result);
                    if (result != 0) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[PLEDGE][%s][%d]Failed to level up: %d, %d",
                        //     L"PledgeSkill.cpp",
                        //     0x306,
                        //     static_cast<int>(jobType),
                        //     result)
                        // L2_LOG_ERROR(Log,
                        //     L"[PLEDGE][%s][%d]Failed to level up: %d, %d",
                        //     L"PledgeSkill.cpp",
                        //     0x306,
                        //     static_cast<int>(jobType),
                        //     result)
                        L2_LOG_ERROR(
                            Log,
                            "Clan %u, character %u: skill-activation cost operation returned %d for job %d; this callback "
                            "stops before transaction cleanup.",
                            static_cast<unsigned>(pledgeCapture.m_dbid), static_cast<unsigned>(userCapture.m_dbid), result,
                            static_cast<int>(jobType)
                        );
                        return;
                    }
                    CPledge* const owner = pledgeCapture.GetPledge();
                    if (owner == nullptr) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[PLEDGE][%s][%d]No pledge for acquire skill: %d",
                        //     L"PledgeSkill.cpp",
                        //     0x2ef,
                        //     static_cast<int>(jobType))
                        // L2_LOG_ERROR(Log,
                        //     L"[PLEDGE][%s][%d]No pledge for acquire skill: %d",
                        //     L"PledgeSkill.cpp",
                        //     0x2ef,
                        //     static_cast<int>(jobType))
                        L2_LOG_ERROR(
                            Log,
                            "Clan %u is unavailable after the skill-activation cost operation (job %d, character %u); this "
                            "callback stops before adding the skill or cleaning up the transaction.",
                            static_cast<unsigned>(pledgeCapture.m_dbid), static_cast<int>(jobType),
                            static_cast<unsigned>(userCapture.m_dbid)
                        );
                        return;
                    }
                    owner->UpdateInfoWindowPacket();
                    detail::AddPledgeSkillInternal(owner, info, true, true);
                    PledgeStuff::SendSystemMessage_Skill(
                        owner, SystemMessageId::NAMED_CLAN_SPECIALIZATION_UNLOCKED, info->skill_id, info->skill_level, 0
                    );
                    SendPledgeSkillInfoTo(requester, info->skill_id, info->skill_level);
                    PledgeStuff::Write_LOG_PLEDGE_SKILL_ACTIVATE(
                        requester, owner, info->skill_id, info->skill_level, remainSecond, info->expiry_time,
                        PledgeStuff::ActionStep::Success
                    );
                    if (!owner->GetSkill()->GetTransactionLock().EndJob()) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"Failed to end job at %s(%d)",
                        //     L"PledgeSkill.cpp",
                        //     0x303)
                        // L2_LOG_ERROR(Log,
                        //     L"Failed to end job at %s(%d)",
                        //     L"PledgeSkill.cpp",
                        //     0x303)
                        L2_LOG_ERROR(
                            Log,
                            "Clan %u: the skill-activation callback reached cleanup with no active transaction (job %d).",
                            static_cast<unsigned>(pledgeCapture.m_dbid), static_cast<int>(jobType)
                        );
                    }
                }));
            }
        }
    }

    if (!asynchronous && !transaction.EndJob()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Failed to end job at %s(%d)",
        //     L"PledgeSkill.cpp",
        //     0x30d)
        // L2_LOG_ERROR(Log,
        //     L"Failed to end job at %s(%d)",
        //     L"PledgeSkill.cpp",
        //     0x30d)
        L2_LOG_ERROR(
            Log, "Clan %u: activation of skill %d level %d reached synchronous cleanup with no active transaction (job %d).",
            static_cast<unsigned>(pledge->GetDbId()), skillId, skillLevel, static_cast<int>(jobType)
        );
    }
}

namespace Builder {

void SendPledgeSkillListTo(User* const user) {
    // Current L2Server.exe 0x009cb71c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x364,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x364,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot send the builder clan-skill list: the requesting user is unavailable.");
        return;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || pledge->GetSkill() == nullptr) {
        return;
    }
    PledgeSkill* const skills = pledge->GetSkill();
    const PledgeMastery* const mastery = pledge->GetMastery();
    const int pledgeLevel = static_cast<int>(pledge->GetPledgeLevel());
    for (const PledgeSkillInfo* const info : PledgeSkillDB::inst().GetInfos()) {
        if (info == nullptr || info->add_type != PledgeAddType::MANUAL) {
            continue;
        }
        const PledgeActiveStatus::Value status = CheckSkillStatus(skills, pledgeLevel, mastery, info);
        const int remainSecond = skills->GetRemainSecond(info->skill_id, runtime::crt::Time32Now());
        PledgeStuff::SendSystemMessage(
            user, FormatPledgeSkillBuilderMessage(info->skill_id, info->skill_level, status, remainSecond)
        );
    }
}

} // namespace Builder

} // namespace PledgeSkillStuff

int PledgeSkill::_GetSkillLevel(const int skillId) const {
    L2_GUARD_FUNCTION();
    const auto found = m_Skills.find(skillId);
    return found == m_Skills.end() ? 0 : found->second.nLevel;
}

bool PledgeSkill::_CanAddSkill(const int skillId, const int skillLevel) const {
    L2_GUARD_FUNCTION();
    const int existingLevel = _GetSkillLevel(skillId);
    if (existingLevel < 1) {
        return skillLevel < 2;
    }
    return existingLevel + 1 == skillLevel;
}

bool PledgeSkill::CanAddSkill(const int skillId, const int skillLevel) const {
    L2_GUARD_FUNCTION();
    const bool result = [&]() -> bool {
        L2_READ_GUARD(m_skillLock);
        const bool result = _CanAddSkill(skillId, skillLevel);
        return result;
    }();
    return result;
}

bool PledgeSkill::AddSkill(const int skillId, const int skillLevel, const std::int32_t expiryTime) {
    // Current L2Server 0x009cbd40.
    L2_GUARD_FUNCTION();
    if (!PledgeSkillStuff::IsPledgeSkill(skillId, skillLevel)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Not pledge skill: %d, %d",
        //     L"PledgeSkill.cpp",
        //     0x167,
        //     skillId,
        //     skillLevel)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Not pledge skill: %d, %d",
        //     L"PledgeSkill.cpp",
        //     0x167,
        //     skillId,
        //     skillLevel)
        L2_LOG_ERROR(
            Log,
            "Cannot store clan skill %d level %d: no clan-skill definition is registered; existing skill records are "
            "retained.",
            skillId, skillLevel
        );
        return false;
    }

    {
        L2_WRITE_GUARD(m_skillLock);
        const auto found = m_Skills.find(skillId);
        if (found == m_Skills.end()) {
            m_Skills.emplace(skillId, SkillNode{skillLevel, expiryTime});
        } else {
            found->second.nLevel = skillLevel;
            found->second.tExpiryTime = expiryTime;
        }
    }
    return true;
}

bool PledgeSkill::HasSkill(const int skillId) const {
    // Current L2Server 0x009cab1c.
    L2_GUARD_FUNCTION();
    const bool result = [&]() -> bool {
        L2_READ_GUARD(m_skillLock);
        const bool result = m_Skills.find(skillId) != m_Skills.end();
        return result;
    }();
    return result;
}

int PledgeSkill::GetRemainSecond(const int skillId, const std::int32_t now) const {
    // Current L2Server 0x009cac14.
    L2_GUARD_FUNCTION();
    const int remain = [&]() -> int {
        L2_WRITE_GUARD(m_skillLock);
        const auto found = m_Skills.find(skillId);
        int remain = 0;
        if (found != m_Skills.end()) {
            remain = runtime::SubtractInt32Wrapping(found->second.tExpiryTime, now);
            if (remain < 1) {
                remain = 0;
            }
        }
        return remain;
    }();
    return remain;
}

bool PledgeSkill::RemoveSkill(const int skillId) {
    // Current L2Server 0x009cc174.
    L2_GUARD_FUNCTION();
    const bool removed = [&]() -> bool {
        L2_WRITE_GUARD(m_skillLock);
        const bool removed = m_Skills.erase(skillId) != 0;
        return removed;
    }();
    return removed;
}

void PledgeSkill::GetSkillIDs(std::vector<int>& skillIds, const bool masteryOnly) const {
    // Current L2Server 0x009cc240.
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_skillLock);
        skillIds.reserve(m_Skills.size());
        for (const auto& skill : m_Skills) {
            const PledgeSkillInfo* const info = PledgeSkillDB::inst().FindSkill(skill.first, skill.second.nLevel);
            if (info == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[PLEDGE][%s][%d]No skill info: %d, %d",
                //     L"PledgeSkill.cpp",
                //     0x1d0,
                //     skill.first,
                //     skill.second.nLevel)
                // L2_LOG_ERROR(Log,
                //     L"[PLEDGE][%s][%d]No skill info: %d, %d",
                //     L"PledgeSkill.cpp",
                //     0x1d0,
                //     skill.first,
                //     skill.second.nLevel)
                L2_LOG_ERROR(
                    Log,
                    "Cannot include clan skill %d level %d in the requested ID list: its clan-skill definition is "
                    "unavailable; the stored skill is retained.",
                    skill.first, skill.second.nLevel
                );
            } else if (!masteryOnly || info->Condition.HasMasteryCondition()) {
                skillIds.push_back(skill.first);
            }
        }
    }
}

void PledgeSkill::SelectExpiredSkillIDs(const std::int32_t now, std::vector<int>& skillIds) const {
    // Current L2Server 0x009cc3e8.
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_skillLock);
        skillIds.reserve(m_Skills.size());
        for (const auto& skill : m_Skills) {
            if (skill.second.tExpiryTime != 0 && skill.second.tExpiryTime <= now) {
                skillIds.push_back(skill.first);
            }
        }
    }
}

void PledgeSkill::Assemble(CPacketHelperEx<20480>& packet, int& skillCount) const {
    // Current L2Server 0x009ca844.
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_skillLock);
        for (const auto& skill : m_Skills) {
            packet.Assemble("dd", skill.first, skill.second.nLevel);
        }
        skillCount = static_cast<int>(m_Skills.size());
    }
}

void PledgeSkill::_ApplySkillsTo(
    User* user, const bool nameValueNegative, const bool sendAcquireMessage, const bool replaceSkill, const bool refresh
) const {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0xde,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0xde,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot apply clan skills: the target user is unavailable.");
        return;
    }

    bool changed = false;
    {
        L2_GUARD(*user->m_lock);
        for (const auto& skillEntry : m_Skills) {
            const int skillId = skillEntry.first;
            const int skillLevel = skillEntry.second.nLevel;
            const PledgeSkillInfo* const pledgeSkill = PledgeSkillDB::inst().FindSkill(skillId, skillLevel);
            if (pledgeSkill == nullptr) {
                continue;
            }

            if (PledgeSkillStuff::CanAddSkill(user, pledgeSkill)) {
                const bool alreadyHad = user->HaveSkill(skillId, skillLevel);
                const bool acquired = user->AcquireSkill(skillId, skillLevel, 0, replaceSkill, true);
                if (acquired) {
                    changed = true;
                    if (sendAcquireMessage && !alreadyHad) {
                        user->GetSocket()->Send(
                            "chccdd", 0x62, SystemMessageId::CLAN_SKILL_LEARNED, 1, 4, skillId,
                            static_cast<std::uint16_t>(skillLevel)
                        );
                    }
                }
                user->SetSkillLock(skillId, skillLevel, 0, nameValueNegative || user->IsOlympiadPlayer() ? 1 : 0);
            } else if (user->HaveSkill(skillId, skillLevel)) {
                changed = true;
                user->DeleteAcquireSkill(skillId);
            }
        }
    }

    // Current vslot +0xa70 consumes the parameter mask and ignores the
    // otherwise reconstructed force byte.
    user->ValidateChanged(VB_MAX);
    if (changed || refresh) {
        user->SetMaxMagicLevel();
        user->m_nParamChanged |= PCB_USER;
        if (nameValueNegative && !refresh) {
            user->SendSystemMessage(SystemMessageId::CLAN_SKILLS_DISABLED_NEGATIVE_REPUTATION);
        }
    }
}

void PledgeSkill::ApplySkillsTo(
    User* user, const bool nameValueNegative, const bool sendAcquireMessage, const bool replaceSkill, const bool refresh
) const {
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_skillLock);
        _ApplySkillsTo(user, nameValueNegative, sendAcquireMessage, replaceSkill, refresh);
    }
}

void PledgeSkill::ApplySkillsTo(
    const std::set<std::uint32_t>& memberIds,
    const bool nameValueNegative,
    const bool sendAcquireMessage,
    const bool replaceSkill,
    const bool refresh
) const {
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_skillLock);
        for (const std::uint32_t memberId : memberIds) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(memberId);
            User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
            if (user != nullptr) {
                _ApplySkillsTo(user, nameValueNegative, sendAcquireMessage, replaceSkill, refresh);
                user->m_nParamChanged |= PCB_USER;
            }
        }
    }
}

void PledgeSkill::RemoveSkillsFrom(User* user) const {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x14e,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeSkill.cpp",
        //     0x14e,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot remove clan skills: the target user is unavailable.");
        return;
    }

    {
        L2_READ_GUARD(m_skillLock);
        {
            L2_GUARD(user->m_AcquiredSkillsSect);
            for (const auto& skill : m_Skills) {
                if (user->DeleteAcquireSkill(skill.first)) {
                    user->SendDeletedPledgeSkillToDB(skill.first);
                }
            }
        }
    }
}

JobTransactionLock& PledgeSkill::GetTransactionLock() { return m_TransactionLock; }

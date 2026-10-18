#include "AirShip.h"
#include "Area.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "CWorld.h"
#include "Creature.h"
#include "CreatureController.h"
#include "HolyThing.h"
#include "Item.h"
#include "ItemOptionServer.h"
#include "NPC.h"
#include "NewAction.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Packet.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeGameSystem.h"
#include "Protocol/src/SystemMessageIds.h"
#include "SerializedMessage.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "SkillFxFunc.h"
#include "StaticObject.h"
#include "ThreadLocalWorldMap.h"
#include "Trap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/GeoData.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <numeric>

namespace {

CSkillInfo sNullSkillInfo;

// Current L2Server .rdata 0x01071b90.
constexpr std::int32_t kUltimateSkillLevelGauge[4] = {
    0,
    500,
    1000,
    1500,
};

// Current L2Server .data 0x0173dd1b8. The diagnostic is deliberately
// process-wide and stops logging after the first thirty short channel ticks.
std::int32_t dwTime_count = 0;

bool CheckIsMagicProblem(CCreature* const actor, const int magicType) {
    // Current L2Server 0x008721ec.
    L2_GUARD_FUNCTION();
    return actor != nullptr &&
        (actor->m_SkillMod.m_bCanSpell || (magicType != SIMT_MAGIC_SKILL && magicType != SIMT_MAGIC_SKILL_NOCANCEL)) &&
        (actor->m_SkillMod.m_bCanPSkill ||
            (magicType != SIMT_PHYSICAL_SKILL && magicType != SIMT_PHYSICAL_SKILL_NOCANCEL &&
                magicType != SIMT_SONGDANCE_SKILL)) &&
        (actor->m_SkillMod.m_bCanSSkill || magicType != SIMT_ETC_SKILL) &&
        (actor->m_SkillMod.m_bCanTSkill || magicType != SIMT_TRIGGER_SKILL);
}

bool CheckXMasTree(CCreature* const actor, const int skillId) {
    // Current L2Server 0x008722d0.
    L2_GUARD_FUNCTION();
    if (skillId != 0x85a || actor == nullptr || !actor->IsPC()) {
        return true;
    }
    if (CTreeLoc::CanUseTree(actor->GetPos(), actor->GetInZoneID())) {
        return true;
    }

    if (User* const user = actor->CastUser(); user != nullptr && user->GetSocket() != nullptr) {
        user->GetSocket()->Send("chccd", 0x62, SystemMessageId::CANNOT_SUMMON_DUPLICATE_NEARBY_OBJECT, 1, 3, 0x15b9);
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"can not summon xmastree. near xmas tree exists...")
    // Noise: A nearby Christmas tree is a normal placement restriction. The player already receives the matching system
    // message. L2_LOG_ERROR(Log,
    //     L"can not summon xmastree. near xmas tree exists...")
    return false;
}

bool IsTtpDiagnosticSkill(const int skillId) { return skillId == 0x3ce8 || skillId == 0x3ce7 || skillId == 0x3ba5; }

void WriteSkillStartFailure(const int skillId, const int, CCreature* const actor) {
    // Current L2Server 0x00872f34.
    if (!IsTtpDiagnosticSkill(skillId)) {
        return;
    }
    if (actor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Lind][TTP][67259] OnStart Fail skillId[%d], FILE[%s], LINE[%d], actor null",
        //     skillId,
        //     L"newaction_skill.cpp",
        //     line)
        // Noise: OnStart already reports the missing caster with skill context. This special three-skill trace would
        // duplicate that failure. L2_LOG_ERROR(Log,
        //     L"[Lind][TTP][67259] OnStart Fail skillId[%d], FILE[%s], LINE[%d], actor null",
        //     skillId,
        //     L"newaction_skill.cpp",
        //     line)
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[Lind][TTP][67259] OnStart Fail skillId[%d], FILE[%s], LINE[%d] id[%d]",
    //     skillId,
    //     L"newaction_skill.cpp",
    //     line,
    //     actor->m_nId)
    // Noise: This special three-skill trace repeats routine gameplay rejections such as death, blocked actions and
    // unavailable equipment, without an actionable cause. Specific operational failures keep their own diagnostics.
    // L2_LOG_ERROR(Log,
    //     L"[Lind][TTP][67259] OnStart Fail skillId[%d], FILE[%s], LINE[%d] id[%d]",
    //     skillId,
    //     L"newaction_skill.cpp",
    //     line,
    //     actor->m_nId)
}

void WriteSkillStartSuccess(const int skillId, const int, CCreature* const actor) {
    // Current L2Server 0x00872fb0.
    if (!IsTtpDiagnosticSkill(skillId)) {
        return;
    }
    if (actor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Lind][TTP][67259] OnUseSkillStart skillId[%d], FILE[%s], LINE[%d], actor null",
        //     skillId,
        //     L"newaction_skill.cpp",
        //     line)
        // L2_LOG_ERROR(Log,
        //     L"[Lind][TTP][67259] OnUseSkillStart skillId[%d], FILE[%s], LINE[%d], actor null",
        //     skillId,
        //     L"newaction_skill.cpp",
        //     line)
        L2_LOG_ERROR(Log, "Skill %d reached the start notification without a caster.", skillId);
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[Lind][TTP][67259] OnUseSkillStart skillId[%d], FILE[%s], LINE[%d] id[%d]",
    //     skillId,
    //     L"newaction_skill.cpp",
    //     line,
    //     actor->m_nId)
    // Noise: Successful start notifications for three hard-coded skills are routine per-cast tracing, not operator errors.
    // L2_LOG_ERROR(Log,
    //     L"[Lind][TTP][67259] OnUseSkillStart skillId[%d], FILE[%s], LINE[%d] id[%d]",
    //     skillId,
    //     L"newaction_skill.cpp",
    //     line,
    //     actor->m_nId)
}

void HolyThingSkill(
    CCreature* const actor, const CSmartID targetId, const int skillId, const int skillLevel, const int skillSubLevel
) {
    // Current L2Server 0x00874c34.
    L2_GUARD_FUNCTION();
    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(CCreature* const actor, const int skillId, const int skillLevel, const int skillSubLevel) :
            m_Actor(actor), m_SkillId(skillId), m_SkillLevel(skillLevel), m_SkillSubLevel(skillSubLevel) {}

        bool CanDo(CObject* const object, double) override {
            if (!object->IsPC()) {
                g_npcserver.Send(
                    "cddddddd", 0xe, static_cast<int>(m_Actor->m_nId), m_Actor->GetIndex(), static_cast<int>(object->m_nId),
                    m_SkillId, m_SkillLevel, m_SkillSubLevel, 0
                );
            }
            return true;
        }

    private:
        CCreature* m_Actor;
        int m_SkillId;
        int m_SkillLevel;
        int m_SkillSubLevel;
    };

    if (actor == nullptr || (skillId != 0xf6 && skillId != 0x4a5a && skillId != 0x4a5b)) {
        return;
    }

    CStaticObject* const target = targetId.GetStaticObject();
    if (target == nullptr || !target->IsHolyThing() || target->CastHolyThing() == nullptr) {
        return;
    }

    CTLMFilterRect filter(static_cast<int>(actor->GetInZoneID()), actor->GetPos(), 2048.0);
    TLMFunc function(actor, skillId, skillLevel, skillSubLevel);
    FOR_ALL_NEIGHBOR_CREATURE_RECT(&filter, &function);
}

bool CheckOnlyUseEquipedWeaponActiveSkill(CCreature* const actor, const int skillId) {
    // Current L2Server 0x00877a44.
    L2_GUARD_FUNCTION();
    if (actor == nullptr || !actor->IsPC() || !actor->m_ItemMod.m_bOnlyUseEquipedWeaponActiveSkill) {
        return true;
    }

    User* const user = actor->CastUser();
    CSharedItemData* const weapon = actor->GetEquipedWeapon();
    if (user == nullptr || weapon == nullptr) {
        return false;
    }
    CItem* const item = CItemFactory::Inst().GetObjectW(weapon->index);
    if (item == nullptr) {
        return false;
    }

    std::map<int, const CSkillInfo*> skills;
    user->GetSkillsFromItem(*item, skills, User::CCT_ANY_CRYSTAL_LEVEL);
    return skills.find(skillId) != skills.end();
}

} // namespace

void CCreatureController::__async_Skill(CExecutionArgumentPad* argument) {
    // Current L2Server 0x0087cc50.
    L2_GUARD_FUNCTION();

    SkillUseInfo skillUseInfo;
    bool forceAttack = false;
    bool surely = false;
    unsigned int usedItemSID = 0;
    std::memcpy(&skillUseInfo, argument->Buffer + 0x08, sizeof(skillUseInfo));
    std::memcpy(&forceAttack, argument->Buffer + 0x20, sizeof(forceAttack));
    std::memcpy(&surely, argument->Buffer + 0x21, sizeof(surely));
    std::memcpy(&usedItemSID, argument->Buffer + 0x24, sizeof(usedItemSID));

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return;
    }

    if (actor->m_data->m_bIsActive && actor->m_data->m_bIsAlive != 0) {
        if (!actor->m_SkillMod.m_bCanAct) {
            bool conditionAllowsSkill = false;
            {
                L2_GUARD(actor->m_SkillDurationMod.m_Lock);
                conditionAllowsSkill = actor->m_SkillDurationMod.m_SkillCondMap.find(skillUseInfo.m_skillID) !=
                    actor->m_SkillDurationMod.m_SkillCondMap.end();
            }
            if (!conditionAllowsSkill) {
                // actor->ActionFailed(0)
                actor->ActionFailed(AF_DEFAULT);
                CCreature* const target = actor->m_spTarget.GetCreature();
                if (target != nullptr && target->IsCreature()) {
                    actor->OnUseSkillFinished(
                        static_cast<int>(target->Id()), skillUseInfo.m_skillID, skillUseInfo.m_skillLevel,
                        skillUseInfo.m_skillSubLevel, 0
                    );
                }
                return;
            }
        }

        const auto runImmediateSkill = [&](const int skillLevel, const int skillSubLevel) {
            CSkillAction2 skillAction;
            skillUseInfo.m_skillLevel = skillLevel;
            skillUseInfo.m_skillSubLevel = skillSubLevel;
            skillAction.Set(m_spActor, actor->m_spTarget, skillUseInfo, forceAttack, surely, usedItemSID, 0);
            skillAction.OnStart();
            skillAction.OnEnd(true);
            CNewAction* const currentAction = GetCurrentAction(-1);
            if (currentAction->GetType() != ACT_SKILL) {
                currentAction->OnStart();
            }
        };

        int selectedLevel = 0;
        int selectedSubLevel = 0;
        bool enabled = false;
        CSkillInfo* selectedSkill = actor->GetToggleSkill(skillUseInfo.m_skillID, selectedLevel, selectedSubLevel, enabled);
        if (selectedSkill != nullptr && selectedSkill->m_NextAction == ACT_NOTHING) {
            runImmediateSkill(selectedLevel, selectedSubLevel);
            return;
        }

        selectedLevel = 0;
        selectedSubLevel = 0;
        enabled = false;
        selectedSkill = actor->GetAuraSkill(skillUseInfo.m_skillID, selectedLevel, selectedSubLevel, enabled);
        if (selectedSkill != nullptr && selectedSkill->m_NextAction == ACT_NOTHING) {
            runImmediateSkill(selectedLevel, selectedSubLevel);
            return;
        }

        const int transition = g_ActionTable[GetCurrentActionType()][ACT_SKILL];
        if (transition == 1) {
            if (GetCurrentActionType() == ACT_MOVE) {
                GetCurrentAction(-1)->SetInterruptActionType(ACT_SKILL);
            }
            actor->InitCoupleAction();
            EndCurrentAction(false);
            SetAsSkill(0, actor->m_spTarget, skillUseInfo, forceAttack, surely, usedItemSID);
            SetAsNothing(1);
            DoFirstAction();
            return;
        }

        if (transition == 2) {
            if (actor->IsDoubleCasting(skillUseInfo.m_skillID, skillUseInfo.m_skillLevel, skillUseInfo.m_skillSubLevel)) {
                int skillIndex = 0;
                CSkillAction2& skillAction = m_Action[0].GetSkillAction(skillIndex, false, -1);
                const bool samePostTarget =
                    !m_Action[0].m_spPostTarget.IsValid() || m_Action[0].m_spPostTarget.Sid() == actor->m_spTarget.Sid();
                if (actor->m_nLaunchedSkills.load(std::memory_order_seq_cst) == 1 && samePostTarget &&
                    skillAction.m_pSkillInfoA != nullptr && skillAction.m_ActionState == ActionState::End) {
                    skillAction.Set(
                        m_spActor, actor->m_spTarget, skillUseInfo, forceAttack, surely, usedItemSID, skillIndex
                    );
                    skillAction.OnStart();
                    return;
                }
            } else {
                if (actor->IsNPC() && m_Action[1].m_ActionType == ACT_SKILL) {
                    int pendingIndex = 0;
                    CSkillAction2& pendingSkill = m_Action[1].GetSkillAction(pendingIndex, true, -1);
                    CCreature* const pendingTarget = pendingSkill.m_spTarget.GetCreature();
                    if (pendingTarget != nullptr && pendingSkill.m_pSkillInfoA != nullptr) {
                        actor->OnUseSkillFinished(
                            static_cast<int>(pendingTarget->Id()), pendingSkill.m_pSkillInfoA->m_nSkillId,
                            pendingSkill.m_pSkillInfoA->m_nLevel, pendingSkill.m_pSkillInfoA->m_nSubLevel, 0
                        );
                    }
                }
                m_Action[1].m_spPostTarget = actor->m_spTarget;
                SetAsSkill(1, actor->m_spTarget, skillUseInfo, forceAttack, surely, usedItemSID);
            }
        }
    }

    // actor->ActionFailed(0)
    actor->ActionFailed(AF_DEFAULT);
}

CSkillAction2::CSkillAction2() :
    m_pSkillInfoA(&sNullSkillInfo),
    m_CurrentAction(SKL_NOT_STARTED),
    m_nHitCancelTime(0),
    m_nHitTime(0),
    m_nCoolTime(0),
    m_nMyIndex(0),
    m_Infected(false),
    m_nSlotNo(0),
    bCritical(false) {}

int CSkillAction2::GetSkillId() const { return m_pSkillInfoA->m_nSkillId; }

int CSkillAction2::GetSkillLevel() const { return m_pSkillInfoA->m_nLevel; }

int CSkillAction2::GetSkillSubLevel() const { return m_pSkillInfoA->m_nSubLevel; }

int CSkillAction2::GetHitCancelTime() const { return m_nHitCancelTime; }

int CSkillAction2::GetHitTime() const { return m_nHitTime; }

int CSkillAction2::GetMagicType() const {
    const int magicType = m_pSkillInfoA->m_nIsMagic;
    if (magicType >= 10 && magicType <= 13) {
        return 0;
    }
    if (magicType >= 20 && magicType <= 25) {
        return 1;
    }
    return magicType;
}

bool CSkillAction2::IsDoubleCastingAction() const { return m_pSkillInfoA != nullptr && m_pSkillInfoA->m_bIsDoubleCasting; }

int CSkillAction2::CalcPaybackMP(CCreature& creature, CSkillInfo& skillInfo, const double consumedMP) {
    // Current L2Server 0x00871ac0.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC() || skillInfo.m_nEffectPoint < 1 ||
        (skillInfo.m_OperateType != SKILL_INSTANT && skillInfo.m_OperateType != SKILL_DURATION) ||
        creature.m_SkillMod.m_dMpFriendlyPaybackPer <= 0.0 ||
        creature.m_SkillMod.m_dMpFriendlyPaybackProb <= ::genrand64_real1() * 100.0) {
        return 0;
    }
    const int payback = static_cast<int>(consumedMP * creature.m_SkillMod.m_dMpFriendlyPaybackPer / 100.0);
    return static_cast<int>(consumedMP) < payback ? 0 : payback;
}

double CSkillAction2::ApplyConsumeMPAwakenPenalty(int consumeMP, CCreature* creature, CSkillInfo* skillInfo) {
    // Current L2Server 0x00871bfc.
    if (!creature->IsPC()) {
        return static_cast<double>(consumeMP);
    }
    User* user = creature->GetUserOrMaster();
    ClassType classType = static_cast<ClassType>(user->GetClass());
    if (!User::IsInFifthClassGroup(classType)) {
        classType = static_cast<ClassType>(user->GetClass());
        if (!User::IsInSixthClassGroup(classType)) {
            return static_cast<double>(consumeMP);
        }
    }
    if (!skillInfo->m_bHasAwakenPenalty) {
        return static_cast<double>(consumeMP);
    }

    user = creature->GetUserOrMaster();
    if (user == nullptr) {
        return static_cast<double>(consumeMP);
    }

    const int level = user->Level();
    if (level < skillInfo->m_nAwakenPenaltyLevelBegin) {
        return static_cast<double>(consumeMP);
    }
    constexpr double kEuler = 2.7182818284590451;
    const double levelFactor = static_cast<double>(level) + 1.0;
    consumeMP = static_cast<int>(
        ((levelFactor - 85.0) * 0.05 + 1.0) * static_cast<double>(consumeMP) +
        (static_cast<double>(skillInfo->m_nAwakenPenaltyFactorOnMP) *
            std::pow(kEuler, levelFactor - static_cast<double>(skillInfo->m_nAwakenPenaltyLevelBegin)) / 200.0) *
            user->m_data->m_dBaseMP
    );
    return static_cast<double>(consumeMP);
}

int CSkillAction2::CalcConsumeMP(
    CCreature* creature, CSkillInfo* skillInfo, const int baseConsumeMP, const double consumeFactor
) {
    // Current L2Server 0x00873c14.
    const int modifierIndex = CCreatureSkillMod::MPCost0 + skillInfo->m_nIsMagic;
    int consumeMP = static_cast<int>(
        static_cast<double>(baseConsumeMP) * consumeFactor * creature->m_SkillMod.m_Mods[modifierIndex].m_Per +
        creature->m_SkillMod.m_Mods[modifierIndex].m_Diff
    );
    if (skillInfo->m_nAfterEffectType == 1) {
        consumeMP = static_cast<int>(
            static_cast<double>(consumeMP) * creature->m_SkillMod.m_ModMpCostAfterEffect[1].m_Per +
            creature->m_SkillMod.m_ModMpCostAfterEffect[1].m_Diff
        );
    }
    return static_cast<int>(ApplyConsumeMPAwakenPenalty(consumeMP, creature, skillInfo));
}

bool CSkillAction2::CheckConsumePoint(CCreature* creature, CSkillInfo* skillInfo) {
    // Current L2Server 0x00872610.
    L2_GUARD_FUNCTION();
    if (skillInfo->m_nPledgeNameValueConsume == 0 && skillInfo->m_nPVPPointConsume == 0) {
        return true;
    }
    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }

    bool result = false;
    if (skillInfo->m_nPledgeNameValueConsume > 0) {
        CPledge* const pledge = user->GetPledge();
        if (pledge == nullptr) {
            user->SendSystemMessage(SystemMessageId::NOT_IN_CLAN);
        } else if (skillInfo->m_nPledgeNameValueConsume <= pledge->GetNameValue()) {
            result = true;
        } else {
            user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_CLAN_REPUTATION);
        }
    }
    if (skillInfo->m_nPVPPointConsume > 0) {
        result = skillInfo->m_nPVPPointConsume <= user->GetPoint(5);
        if (!result) {
            user->SendSystemMessage(0x917);
        }
    }
    return result;
}

bool CSkillAction2::CheckAndConsumeAgathionEnergy(CCreature* creature, CSkillInfo* skillInfo, const double consumeFactor) {
    // Current L2Server 0x00872760.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || skillInfo == nullptr) {
        return false;
    }
    if (skillInfo->m_pConsumeEffect == nullptr || !skillInfo->m_pConsumeEffect->IsConsumeAgathionEnergyEffect()) {
        return true;
    }
    return skillInfo->m_pConsumeEffect->Consume(creature, consumeFactor);
}

bool CSkillAction2::CheckAndConsumeItemDuration(CCreature* creature) {
    // Current L2Server 0x00873ca8.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return true;
    }
    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }
    for (const int itemClassId : m_pSkillInfoA->m_vRelatedItems) {
        if (!user->CheckAndConsumeDuration(itemClassId)) {
            return false;
        }
    }
    return true;
}

bool CSkillAction2::CheckAndSetReuseDelay(CCreature* creature, int& reuseDelay) {
    // Current L2Server 0x00873dd8.
    L2_GUARD_FUNCTION();
    const double remaining =
        creature->GetRemainReuseDelaySec(m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, m_pSkillInfoA->m_nSubLevel);
    if (remaining <= 0.0) {
        if (!CheckAndConsumeItemDuration(creature)) {
            return false;
        }
        reuseDelay = creature->SetSkillReuseDelay(*m_pSkillInfoA);
        return true;
    }
    if (creature->IsPC()) {
        if (User* const user = creature->CastUser(); user != nullptr) {
            user->SendRemainTime(
                static_cast<int>(remaining) + 1, EST_SKILLID, m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel,
                m_pSkillInfoA->m_nSubLevel, static_cast<int>(m_pSkillInfoA->m_nReuseDelay)
            );
        }
    }
    return false;
}

bool CSkillAction2::CheckConditionsOnStart(CCreature* creature, const bool reportFailure, int& reuseDelay) {
    // Current L2Server 0x00874edc.
    L2_GUARD_FUNCTION();
    if (creature->m_AlwaysSkillSuccess) {
        return true;
    }

    const double remaining =
        creature->GetRemainReuseDelaySec(m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, m_pSkillInfoA->m_nSubLevel);
    if (remaining > 0.0) {
        if (creature->IsPC()) {
            creature->CastUser()->SendRemainTime(
                static_cast<int>(remaining) + 1, EST_SKILLID, m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel,
                m_pSkillInfoA->m_nSubLevel, static_cast<int>(m_pSkillInfoA->m_nReuseDelay)
            );
        }
        return false;
    }

    if (m_pSkillInfoA->m_UltimateSkillLv != 0 && !creature->m_bDoubleCastingMod &&
        kUltimateSkillLevelGauge[m_pSkillInfoA->m_UltimateSkillLv] > creature->m_data->m_UltimateSkillGauge) {
        return false;
    }
    if (reportFailure && creature->m_bDoubleCastingMod && m_pSkillInfoA->m_nToggleGroupId > 0) {
        return false;
    }
    return CheckAndConsumeHPMP(creature, m_pSkillInfoA, m_dConsumeFactor, reportFailure, true) &&
        CheckConsumePoint(creature, m_pSkillInfoA) && CheckEnergySoul(creature, m_pSkillInfoA, false) &&
        CheckCondition(creature) && CheckAndConsumeAgathionEnergy(creature, m_pSkillInfoA, m_dConsumeFactor) &&
        SpendItem(creature, BEFORE_REUSE_DELAY) && CheckAndSetReuseDelay(creature, reuseDelay) &&
        SpendItem(creature, AFTER_REUSE_DELAY) && CheckEnergySoul(creature, m_pSkillInfoA, true);
}

bool CSkillAction2::CompareByHP(CSafePointer2<CCreature>& first, CSafePointer2<CCreature>& second) {
    // Current L2Server 0x008758b8.
    L2_GUARD_FUNCTION();
    CSPointer<CCreature> firstPointer = first.FindObjectSP();
    CSPointer<CCreature> secondPointer = second.FindObjectSP();
    if (!firstPointer) {
        if (secondPointer) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"CompareByHP Fail, Target2 is not valid [%d]",
            //     secondPointer->GetDBID())
            // L2_LOG_ERROR(Log,
            //     L"CompareByHP Fail, Target2 is not valid [%d]",
            //     secondPointer->GetDBID())
            L2_LOG_WARN(
                Log,
                "Cannot compare target HP: the first target is unavailable; the second target has database ID %u. The "
                "comparison returns false.",
                static_cast<unsigned>(secondPointer->GetDBID())
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"CompareByHP Fail, all Target is not valid")
            // L2_LOG_ERROR(Log,
            //     L"CompareByHP Fail, all Target is not valid")
            L2_LOG_WARN(Log, "Cannot compare target HP: both targets are unavailable; the comparison returns false.");
        }
        return false;
    }
    if (!secondPointer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CompareByHP Fail, Target1 is not valid [%d]",
        //     firstPointer->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"CompareByHP Fail, Target1 is not valid [%d]",
        //     firstPointer->GetDBID())
        L2_LOG_WARN(
            Log,
            "Cannot compare target HP: the second target is unavailable; the first target has database ID %u. The "
            "comparison returns false.",
            static_cast<unsigned>(firstPointer->GetDBID())
        );
        return false;
    }
    return firstPointer->m_data->m_dHP / firstPointer->m_data->m_dBaseHP <
        secondPointer->m_data->m_dHP / secondPointer->m_data->m_dBaseHP;
}

bool CSkillAction2::CheckAndConsumeHPMP(
    CCreature* creature,
    CSkillInfo* skillInfo,
    const double consumeFactor,
    const bool reportFailure,
    const bool consumeFirstPhase
) {
    // Current L2Server 0x00873f38.
    L2_GUARD_FUNCTION();
    if (!reportFailure || !consumeFirstPhase) {
        if (creature->m_data->GetHP() < static_cast<double>(skillInfo->m_nHpConsume)) {
            if (reportFailure) {
                if (creature->IsPC()) {
                    creature->CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
                } else {
                    creature->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
                }
            }
            creature->SendSystemMessage(SystemMessageId::NOT_ENOUGH_HP);
            return false;
        }
        const int secondConsume = CalcConsumeMP(creature, skillInfo, skillInfo->m_nMpConsume2, consumeFactor);
        const double totalConsume =
            static_cast<double>(skillInfo->m_nMpConsume1) * consumeFactor + static_cast<double>(secondConsume);
        if (creature->m_data->GetMP() < totalConsume) {
            if (reportFailure) {
                if (creature->IsPC()) {
                    creature->CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
                } else {
                    creature->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
                }
            }
            creature->SendSystemMessage(SystemMessageId::NOT_ENOUGH_MP);
            return false;
        }
        return true;
    }

    if (creature->m_data->m_dBaseMP <= creature->m_data->GetMP() &&
        creature->m_data->GetMP() != creature->m_data->m_dBaseMP) {
        creature->m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
            creature->m_data->m_dMP, creature->m_data->m_dBaseMP, 0xc
        );
    }
    if (creature->ConsumeMP(static_cast<double>(skillInfo->m_nMpConsume1) * consumeFactor)) {
        return true;
    }
    creature->SendSystemMessage(SystemMessageId::NOT_ENOUGH_MP);
    return false;
}

bool CSkillAction2::CheckEnergySoul(CCreature* creature, CSkillInfo* skillInfo, const bool consume) {
    // Current L2Server 0x00874178.
    L2_GUARD_FUNCTION();

    User* user = nullptr;
    if (creature->IsPC()) {
        user = creature->CastUser();
        user->m_UsedSoulCount = 0;
    }

    if ((skillInfo->m_nEnergy > 0 || skillInfo->m_nSoul > 0) && consume) {
        if (skillInfo->m_nEnergy > 0 && user != nullptr) {
            const int usedEnergy = std::min(skillInfo->m_nEnergy, creature->m_nEnergyCount);
            if (!user->DecreaseEnergyCount(usedEnergy)) {
                return false;
            }
            user->m_UsedEnergyCount = usedEnergy;
        }

        if (skillInfo->m_nSoul < 1 || user == nullptr) {
            if (creature->IsPC()) {
                creature->CastUser()->SendETCBuffStatusInfo();
            }
        } else {
            const int usedSoul = std::min(skillInfo->m_nSoul, user->m_SoulCount);
            if (!user->DecreaseSoulCount(usedSoul)) {
                return false;
            }
            user->m_UsedSoulCount = usedSoul;
        }
    }
    return true;
}

bool CSkillAction2::CheckCondition(CCreature* creature) {
    // Current L2Server 0x00872b1c.
    L2_GUARD_FUNCTION();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    const CSkillInfo::OperateConditionTarget conditionTarget =
        m_pSkillInfoA->m_nIsMagic == 0 ? CSkillInfo::OCT_BOTH : CSkillInfo::OCT_CASTER;
    const int result = m_pSkillInfoA->CheckOperateCond(creature, target, conditionTarget);
    if (result == -2) {
        return true;
    }

    if (creature->IsPC()) {
        if (result < 0) {
            if (m_pSkillInfoA->m_nSkillId != 0x7fe && result == -1) {
                const int messageId = !m_pSkillInfoA->m_IsSummonSkill || m_pSkillInfoA->m_IsSummonSiegeSkill ||
                        m_pSkillInfoA->m_nSkillId == 0x1bbe
                    ? SystemMessageId::NAMED_ACTION_CONDITIONS_NOT_MET
                    : SystemMessageId::CANNOT_USE_SKILL_NOT_ENOUGH_SUMMON_POINTS;
                if (messageId == SystemMessageId::NAMED_ACTION_CONDITIONS_NOT_MET) {
                    creature->CastUser()->GetSocket()->Send(
                        "chccdd", 0x62, SystemMessageId::NAMED_ACTION_CONDITIONS_NOT_MET, 1, EST_SKILLID,
                        m_pSkillInfoA->m_nSkillId,
                        (m_pSkillInfoA->m_nSubLevel << 16) | static_cast<std::uint16_t>(m_pSkillInfoA->m_nLevel)
                    );
                } else {
                    creature->CastUser()->GetSocket()->Send(
                        "chccdd", 0x62, SystemMessageId::CANNOT_USE_SKILL_NOT_ENOUGH_SUMMON_POINTS, 1, EST_SKILLID,
                        m_pSkillInfoA->m_nSkillId,
                        (m_pSkillInfoA->m_nSubLevel << 16) | static_cast<std::uint16_t>(m_pSkillInfoA->m_nLevel)
                    );
                }
            }
        } else {
            creature->SendSystemMessage(result);
        }
    }
    return false;
}

bool CSkillAction2::CanUseBadSkill(CCreature* creature) {
    // Current L2Server 0x0087302c.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfoA->m_nEffectPoint >= 0) {
        return true;
    }

    CWorldObject* const target = m_spTarget.GetWorldObject();
    return target != nullptr && creature->CanUseBadSkill(target, true) && CheckAttackValidityInOlympia(creature, target) &&
        CheckAttackValidityInPledgeGame(creature, target);
}

bool CSkillAction2::IsChangeClassSkill() const {
    // Current L2Server 0x00872e8c.
    L2_GUARD_FUNCTION();
    return m_pSkillInfoA->m_nSkillId >= 0x61e && m_pSkillInfoA->m_nSkillId <= 0x621;
}

bool CSkillAction2::CheckValidActionState(CCreature& creature) {
    // Current L2Server 0x00873310.
    L2_GUARD_FUNCTION();
    return m_pSkillInfoA->m_NextAction != ACT_SIT || creature.m_data->m_nPrivateStoreType <= 0;
}

bool CSkillAction2::SetValidSkillPendingAction(CCreature* creature, const bool allowSit, const bool allowFakeDeath) {
    // Current L2Server 0x008733d0.
    L2_GUARD_FUNCTION();
    if (!CheckValidActionState(*creature)) {
        return false;
    }

    const ActionType nextAction = m_pSkillInfoA->m_NextAction;
    if (nextAction == ACT_STAND) {
        creature->GetController()->SetAsStand(1, 0x9f6);
        return true;
    }

    if (!m_bForce && !m_bSurely) {
        if (nextAction == ACT_ATTACK && m_spTarget.IsValid()) {
            CWorldObject* const target = m_spTarget.GetWorldObject();
            const bool surely = (creature->m_SkillMod.m_nBlockMoveCount != 0 || !creature->m_data->m_bCanMove) &&
                target != nullptr && target->IsEnemyTo(creature);
            creature->GetController()->SetAsAttack(1, m_spTarget, surely);
            return true;
        }
        if (nextAction == ACT_SIT) {
            if (allowSit && allowFakeDeath) {
                creature->GetController()->SetAsSit(1, 0x9f6, m_spTarget, m_pSkillInfoA->m_bIsFakeDeath);
            }
            return true;
        }
        if (nextAction == ACT_NOTHING) {
            creature->GetController()->SetAsNothing(1);
        }
        return true;
    }

    if (!m_bForce && nextAction == ACT_ATTACK && m_spTarget.IsValid()) {
        CWorldObject* const target = m_spTarget.GetWorldObject();
        if ((creature->m_SkillMod.m_nBlockMoveCount != 0 || !creature->m_data->m_bCanMove) && target != nullptr &&
            target->IsEnemyTo(creature)) {
            creature->GetController()->SetAsAttack(1, m_spTarget, true);
            return true;
        }
    } else if (nextAction == ACT_SIT) {
        if (allowSit && allowFakeDeath) {
            creature->GetController()->SetAsSit(1, 0x9f6, m_spTarget, m_pSkillInfoA->m_bIsFakeDeath);
        }
        return true;
    }

    creature->GetController()->SetAsNothing(1);
    return true;
}

bool CSkillAction2::CheckAttackable(
    const double range,
    const double actorRadius,
    const double targetRadius,
    const double squaredDistance,
    const double actorHeight,
    const double targetHeight,
    const int actorZ,
    const int targetZ
) {
    // Current L2Server 0x008738d8.
    L2_GUARD_FUNCTION();
    const double combinedRadius = actorRadius + targetRadius;
    const double actorCenter = static_cast<double>(actorZ) + actorHeight * 0.5;
    const double targetCenter = static_cast<double>(targetZ) + targetHeight * 0.5;
    const bool verticalOverlap =
        !(actorCenter + 25.0 < targetCenter - targetHeight || targetCenter + 25.0 < actorCenter - actorHeight);
    const double verticalGap = verticalOverlap
        ? 0.0
        : std::max(std::max(targetCenter - targetHeight - actorCenter, actorCenter - actorHeight - targetCenter), 0.0);

    if (combinedRadius * combinedRadius < squaredDistance) {
        if (verticalOverlap) {
            const double extendedRadius = combinedRadius + range;
            return squaredDistance <= extendedRadius * extendedRadius;
        }
        if (squaredDistance < 0.0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]distance is minus[%f],",
            //     "newaction_skill.cpp",
            //     0x12da,
            //     squaredDistance)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]distance is minus[%f],",
            //     "newaction_skill.cpp",
            //     0x12da,
            //     squaredDistance)
            L2_LOG_ERROR(
                Log,
                "Skill-range check produced negative squared distance %.17g; the horizontal-gap calculation uses zero "
                "before taking the square root.",
                squaredDistance
            );
        }
        const double horizontalGap = std::sqrt(std::max(squaredDistance, 0.0)) - actorRadius - targetRadius;
        return horizontalGap * horizontalGap + verticalGap * verticalGap <= range * range;
    }
    return verticalOverlap || verticalGap <= range;
}

bool CSkillAction2::BeReadyToActivate(
    CCreature* creature,
    int consumeMP,
    const int consumeHP,
    const int consumePledgeNameValue,
    const int,
    const bool recalculateMP
) {
    // Current L2Server 0x0087469c.
    L2_GUARD_FUNCTION();
    if (creature->m_AlwaysSkillSuccess) {
        return true;
    }
    unsigned int messageId = 0;
    if (consumeHP > 0 && !creature->ConsumeHP(static_cast<double>(consumeHP))) {
        messageId = SystemMessageId::NOT_ENOUGH_HP;
    } else {
        if (recalculateMP) {
            consumeMP = CalcConsumeMP(creature, m_pSkillInfoA, consumeMP, m_dConsumeFactor);
        }
        if (!creature->ConsumeMP(static_cast<double>(consumeMP))) {
            messageId = SystemMessageId::NOT_ENOUGH_MP;
        } else if (!creature->ConsumePledgeNameValue(consumePledgeNameValue)) {
            messageId = SystemMessageId::NOT_ENOUGH_CLAN_REPUTATION;
        } else {
            return true;
        }
    }
    // creature->ActionFailed(0)
    creature->ActionFailed(AF_DEFAULT);
    creature->SendSystemMessage(messageId);
    return false;
}

double CSkillAction2::CalcTimeFactor(CCreature& creature, CSkillInfo& skillInfo) {
    // Current L2Server 0x00871d74.
    L2_GUARD_FUNCTION();
    const int magicType = skillInfo.m_nIsMagic;
    if (magicType == SIMT_PHYSICAL_SKILL_NOCANCEL || magicType == SIMT_MAGIC_SKILL_NOCANCEL) {
        return 1.0;
    }

    double timeFactor = 0.0;
    if (magicType == SIMT_PHYSICAL_SKILL || magicType == SIMT_SONGDANCE_SKILL) {
        timeFactor = creature.m_data->m_dParam[6];
    } else if (magicType == SIMT_MAGIC_SKILL) {
        timeFactor = creature.m_data->m_dParam[5];
        timeFactor += timeFactor * creature.m_data->m_dSpiritShotSpeedBonusPer;
    } else if (magicType == SIMT_ETC_SKILL || magicType == SIMT_TRIGGER_SKILL) {
        timeFactor = 1.0;
    }

    if (skillInfo.m_OperateType >= SKILL_CHANNELLING_INSTANCE &&
        skillInfo.m_OperateType <= SKILL_CHANNELLING_DURATION_LEVEL_UP) {
        timeFactor = 1.0;
    }
    if (creature.m_data->m_dSkillHitTimeFactor > 0.0) {
        timeFactor /= creature.m_data->m_dSkillHitTimeFactor;
    }
    if (timeFactor == 0.0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Try to divide by 0. dTimeFactor is 0 / actor[%s] / skillId[%d]",
        //     creature.GetName(),
        //     skillInfo.m_nSkillId)
        // L2_LOG_ERROR(Log,
        //     L"Try to divide by 0. dTimeFactor is 0 / actor[%s] / skillId[%d]",
        //     creature.GetName(),
        //     skillInfo.m_nSkillId)
        // L2_LOG_ERROR(Log,
        //     "Skill %d level %d sublevel %d has zero casting-time factor for '%s'. Using 0.01 before applying further
        //     timing bonuses.", skillInfo.m_nSkillId, skillInfo.m_nLevel, skillInfo.m_nSubLevel,
        //     WideToUtf8(creature.GetName()).c_str())
        L2_LOG_ERROR(
            Log,
            "Skill %d level %d sublevel %d has zero casting-time factor for '%s'; using 0.01 before applying further timing "
            "bonuses.",
            skillInfo.m_nSkillId, skillInfo.m_nLevel, skillInfo.m_nSubLevel, Utf16ToUtf8(creature.GetName()).c_str()
        );
        timeFactor = 0.01;
    }
    return creature.CalcTimeFactorBonusForSkill(timeFactor, skillInfo);
}

double CSkillAction2::GetCancelTime(CCreature& creature, CSkillInfo& skillInfo) {
    // Current L2Server 0x00871f60.
    L2_GUARD_FUNCTION();
    if (skillInfo.m_nSkillId >= 0x641 && skillInfo.m_nSkillId <= 0x643) {
        return 0.0;
    }
    return std::max(500.0, skillInfo.m_dSkillHitCancelTime * 1000.0 / CalcTimeFactor(creature, skillInfo));
}

void CSkillAction2::CalcSkillTiming(
    CCreature& creature, CSkillInfo& skillInfo, int& hitTime, int& hitCancelTime, int& coolTime
) {
    // Current L2Server 0x00872080.
    L2_GUARD_FUNCTION();
    double cancelTime = GetCancelTime(creature, skillInfo);
    const double timeFactor = CalcTimeFactor(creature, skillInfo);
    if (skillInfo.m_OperateType >= SKILL_CHANNELLING_INSTANCE &&
        skillInfo.m_OperateType <= SKILL_CHANNELLING_DURATION_LEVEL_UP) {
        hitTime = static_cast<int>(skillInfo.m_dSkillHitTime * 1000.0 - cancelTime);
        cancelTime = skillInfo.m_dChannelingStart * 1000.0;
    } else {
        hitTime = static_cast<int>(skillInfo.m_dSkillHitTime * 1000.0 / timeFactor - cancelTime);
    }
    hitCancelTime = static_cast<int>(cancelTime);
    hitTime = std::max(hitTime, 0);
    coolTime = static_cast<int>(skillInfo.m_dSkillCoolTime * 1000.0 / timeFactor);
}

bool CSkillAction2::IsTargetInSameInZone() {
    // Current L2Server 0x008719a0.
    L2_GUARD_FUNCTION();
    CCreature* const actor = m_spActor.GetCreature();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (actor == nullptr || target == nullptr) {
        return false;
    }
    if (actor->IsInInZone(target->GetInZoneID())) {
        return true;
    }
    if (actor->IsPC()) {
        if (User* const user = actor->CastUser()) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_TARGET_IN_ANOTHER_INSTANCE);
        }
    }
    return false;
}

bool CSkillAction2::IsBlockedSkill(CCreature* const creature, const int skillId) {
    // Current L2Server 0x008769f4.
    L2_GUARD_FUNCTION();
    return creature != nullptr &&
        creature->m_SkillDurationMod.m_mapBlockSkillId.find(skillId) != creature->m_SkillDurationMod.m_mapBlockSkillId.end();
}

bool CSkillAction2::IsBlockedMagicType(CCreature* const creature, const int magicType) {
    // Current L2Server 0x00876b4c.
    L2_GUARD_FUNCTION();
    return creature != nullptr &&
        creature->m_SkillMod.m_setBlockMagicType.find(static_cast<SkillIsMagicType>(magicType)) !=
        creature->m_SkillMod.m_setBlockMagicType.end();
}

bool CSkillAction2::IsCondBlockActSkill(CCreature* const creature, const int skillId) {
    // Current L2Server 0x00876c7c.
    L2_GUARD_FUNCTION();
    return creature != nullptr &&
        creature->m_SkillDurationMod.m_SkillCondMap.find(skillId) != creature->m_SkillDurationMod.m_SkillCondMap.end();
}

CSkillInfo* CSkillAction2::GetSkillInfo(
    CCreature* creature,
    const int skillId,
    int& skillLevel,
    int& skillSubLevel,
    bool& activeSkill,
    bool& toggleSkill,
    bool& auraSkill,
    bool& databaseSkill
) {
    // Current L2Server 0x008724ac.
    L2_GUARD_FUNCTION();
    activeSkill = false;
    toggleSkill = false;
    auraSkill = false;
    databaseSkill = false;

    CSkillInfo* skillInfo = creature->GetActiveSkill(skillId, skillLevel, skillSubLevel);
    if (skillInfo != nullptr) {
        activeSkill = true;
    } else {
        skillInfo = creature->GetToggleSkill(skillId, skillLevel, skillSubLevel, toggleSkill);
        if (skillInfo == nullptr) {
            skillInfo = creature->GetAuraSkill(skillId, skillLevel, skillSubLevel, auraSkill);
            if (skillInfo == nullptr) {
                skillInfo =
                    const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, skillSubLevel), creature));
                databaseSkill = skillInfo != nullptr;
            }
        }
    }

    if (skillLevel < 1) {
        return nullptr;
    }
    return skillInfo;
}

bool CSkillAction2::ResolveSkillForSet(CCreature* creature, int& skillId, int& skillLevel, int& skillSubLevel) {
    bool activeSkill = false;
    bool toggleSkill = false;
    bool auraSkill = false;
    bool databaseSkill = false;
    m_pSkillInfoA =
        GetSkillInfo(creature, skillId, skillLevel, skillSubLevel, activeSkill, toggleSkill, auraSkill, databaseSkill);

    int replacementSkillId = 0;
    if (creature->m_SkillMod.m_bHasReplacedSkill) {
        L2_GUARD(creature->m_SkillMod.m_ReplacedSkillLock);
        const auto replacement = creature->m_SkillMod.m_ReplacedSkillMap.find(skillId);
        if (replacement != creature->m_SkillMod.m_ReplacedSkillMap.end()) {
            replacementSkillId = replacement->second.m_replaceSkillID;
            CSkillInfo* const replacementInfo = const_cast<CSkillInfo*>(
                g_SkillDB.GetSkillInfo(CSkillKey(replacementSkillId, skillLevel, skillSubLevel), nullptr)
            );
            if (replacementInfo == nullptr) {
                if (replacementSkillId > 0) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s] try to replace skill id[%d] with unregistered skill id[%d] level[%d] sublevel[%d]",
                    //     creature->GetName(),
                    //     skillId,
                    //     replacementSkillId,
                    //     skillLevel,
                    //     skillSubLevel)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s] try to replace skill id[%d] with unregistered skill id[%d] level[%d] sublevel[%d]",
                    //     creature->GetName(),
                    //     skillId,
                    //     replacementSkillId,
                    //     skillLevel,
                    //     skillSubLevel)
                    // L2_LOG_ERROR(Log,
                    //     "Cannot initialize skill %d for '%s': replacement skill %d level %d sublevel %d is not
                    //     registered.", skillId, WideToUtf8(creature->GetName()).c_str(), replacementSkillId, skillLevel,
                    //     skillSubLevel)
                    L2_LOG_ERROR(
                        Log,
                        "Cannot initialize skill %d for '%s': replacement skill %d level %d sublevel %d is not registered.",
                        skillId, Utf16ToUtf8(creature->GetName()).c_str(), replacementSkillId, skillLevel, skillSubLevel
                    );
                    return false;
                }
            } else if (static_cast<double>(static_cast<int>(::genrand64_real2() * 101.0 + 0.0)) <=
                replacement->second.m_dProb) {
                m_pSkillInfoA = replacementInfo;
            }

            if (m_pSkillInfoA != nullptr) {
                skillId = m_pSkillInfoA->m_nSkillId;
                skillLevel = m_pSkillInfoA->m_nLevel;
                skillSubLevel = m_pSkillInfoA->m_nSubLevel;
            }
        }
    }

    // A positive replacement id suppresses the intercept lookup even when
    // its probability roll did not replace the skill in the current image.
    if (replacementSkillId <= 0 && creature->m_SkillMod.m_bHasInterceptSkill) {
        L2_GUARD(creature->m_SkillMod.m_InterceptSkillLock);
        const auto intercept = creature->m_SkillMod.m_SkillInterceptMap.find(CSkillKey(skillId, skillLevel, skillSubLevel));
        if (intercept != creature->m_SkillMod.m_SkillInterceptMap.end() && intercept->second.m_pInterceptSkill != nullptr) {
            m_pSkillInfoA = intercept->second.m_pInterceptSkill;
        }
        if (m_pSkillInfoA != nullptr) {
            skillId = m_pSkillInfoA->m_nSkillId;
            skillLevel = m_pSkillInfoA->m_nLevel;
            skillSubLevel = m_pSkillInfoA->m_nSubLevel;
        }
    }
    return true;
}

bool CSkillAction2::IsTargetAttackable(CCreature* creature, CWorldObject* target, const int castRange) {
    // Current L2Server 0x00874308.
    L2_GUARD_FUNCTION();
    const FVector actorPosition = creature->GetPos();
    const SkillTargetTypeEnum targetType = m_pSkillInfoA->m_TargetType;
    const FVector targetPosition =
        targetType == STGT_GROUND || targetType == STGT_ARTILLERY || targetType == STGT_SHIP_CANNON
        ? creature->m_NonTargetSkillPos
        : target->GetPos();

    double targetRadius = 100.0;
    double targetHeight = 100.0;
    if (target->IsCreature()) {
        targetRadius = target->CastCreature()->m_data->m_dCollisionRadius;
        targetHeight = target->CastCreature()->m_data->m_dCollisionHeight;
    } else if (target->IsHolyThing()) {
        targetRadius = target->CastHolyThing()->m_data->m_dCollisionRadius;
        targetHeight = target->CastHolyThing()->m_data->m_dCollisionHeight;
    }

    const int actorZ = static_cast<int>(actorPosition.z);
    const int targetZ = target->IsStaticObject() && !target->IsHolyThing() ? actorZ : static_cast<int>(targetPosition.z);
    const bool verticalRejected =
        (target->IsHolyThing() && targetZ - actorZ > 25) || (target->IsFortressFlagpole() && targetZ - actorZ > 120);

    if (castRange != -2 && targetType != STGT_ARTILLERY && targetType != STGT_SHIP_CANNON) {
        const double dx = actorPosition.x - targetPosition.x;
        const double dy = actorPosition.y - targetPosition.y;
        if (!CheckAttackable(
                static_cast<double>(castRange), creature->m_data->m_dCollisionRadius, targetRadius, dx * dx + dy * dy,
                creature->m_data->m_dCollisionHeight, targetHeight, actorZ, targetZ
            ) ||
            verticalRejected) {
            creature->SendSystemMessage(SystemMessageId::CASTING_INTERRUPTED_TARGET_TOO_FAR);
            return false;
        }

        const bool checkWorldCollision = creature->IsPCOrPCRelatedNPC() && target->IsCreature();
        if (!CWorld::Inst().GetGeo()->CanSee(
                actorPosition, targetPosition, static_cast<int>(creature->GetInZoneID()), checkWorldCollision
            ) &&
            !target->IsFortressFlagpole()) {
            creature->SendSystemMessage(SystemMessageId::TARGET_NOT_VISIBLE);
            return false;
        }
    }
    return CanUseBadSkill(creature);
}

bool CSkillAction2::ValidateSkillTarget(CCreature* creature, const bool toggleSkill, const bool auraSkill) {
    // Current L2Server 0x00876e54.
    L2_GUARD_FUNCTION();
    const SkillTargetTypeEnum targetType = m_pSkillInfoA->m_TargetType;

    if (targetType == STGT_SELF || toggleSkill || auraSkill) {
        m_spTarget = m_spActor.GetObjectW();
        if (!CanUseBadSkill(creature)) {
            return false;
        }
    } else if (targetType == STGT_SUMMON) {
        if (!creature->IsPC()) {
            return false;
        }
        std::vector<CSummon*> summons;
        creature->CastUser()->GetAllSummonAndPet(summons);
        if (summons.empty()) {
            creature->SendSystemMessage(SystemMessageId::INVALID_TARGET);
            return false;
        }
        if (summons.front() == nullptr) {
            return false;
        }
        m_spTarget = summons.front()->CastCreature();
    } else {
        if (targetType == STGT_GROUND) {
            m_spTarget = m_spActor.GetObjectW();
        } else if (targetType == STGT_ARTILLERY) {
            if (!creature->IsSummonOrPet()) {
                return false;
            }
            CSummon* const summon = creature->CastSummon();
            User* const master = summon == nullptr ? nullptr : summon->GetMaster();
            if (master == nullptr || !master->IsAlive()) {
                return false;
            }
            m_spTarget = m_spActor.GetObjectW();
            const FVector actorPosition = creature->GetPos();
            const FVector masterPosition = master->GetPos();
            FVector direction{
                masterPosition.x - actorPosition.x, masterPosition.y - actorPosition.y, masterPosition.z - actorPosition.z};
            direction.NormalizeCube();
            const double range = static_cast<double>(m_pSkillInfoA->m_nEffectiveRange);
            FVector targetPosition{actorPosition.x + direction.x * range, actorPosition.y + direction.y * range, 16000.0};
            targetPosition.z = static_cast<double>(CWorld::Inst().GetGeo()->FindGround(
                static_cast<int>(targetPosition.x), static_cast<int>(targetPosition.y), static_cast<int>(targetPosition.z)
            ));
            creature->m_NonTargetSkillPos = targetPosition;
        } else if (targetType == STGT_SHIP_CANNON) {
            if (!creature->IsNPC()) {
                return false;
            }
            CAirShip* const airShip = creature->GetMyAirShip();
            if (airShip == nullptr || airShip->m_data->m_bIsInWorld) {
                return false;
            }
            creature->m_NonTargetSkillPos =
                airShip->GetTargetPos(creature, m_pSkillInfoA, static_cast<double>(m_nHitTime + m_nHitCancelTime));
        }

        if (!m_spTarget.IsValid()) {
            creature->SendSystemMessage(SystemMessageId::NO_TARGET);
            return false;
        }
        if (m_pSkillInfoA->m_TargetType == STGT_OTHERS && m_spTarget.GetId() == m_spActor.GetId()) {
            creature->SendSystemMessage(SystemMessageId::CANNOT_USE_ON_SELF);
            return false;
        }
        CWorldObject* const target = m_spTarget.GetWorldObject();
        if (!m_pSkillInfoA->IsValidTarget(creature, target, m_bForce)) {
            creature->SendSystemMessage(SystemMessageId::INVALID_TARGET);
            return false;
        }
        if (!IsTargetAttackable(creature, target, m_pSkillInfoA->m_nCastRange)) {
            return false;
        }
    }

    if (!IsTargetInSameInZone()) {
        return false;
    }

    const SkillOperateType operateType = m_pSkillInfoA->m_OperateType;
    const bool movementSkill = operateType == SKILL_CHARGE_INSTANT || operateType == SKILL_CHARGE_DURATION ||
        operateType == SKILL_BACK_STEP_INSTANT || operateType == SKILL_BACK_STEP_DURATION ||
        operateType == SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK;
    if (movementSkill) {
        if ((operateType == SKILL_CHARGE_INSTANT || operateType == SKILL_CHARGE_DURATION) &&
            m_pSkillInfoA->m_TargetType != STGT_SELF && m_spTarget.GetId() == m_spActor.GetId()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Invalid target. id [%d] level [%d]",
            //     m_pSkillInfoA->m_nSkillId,
            //     m_pSkillInfoA->m_nLevel)
            // L2_LOG_ERROR(Log,
            //     L"Invalid target. id [%d] level [%d]",
            //     m_pSkillInfoA->m_nSkillId,
            //     m_pSkillInfoA->m_nLevel)
            L2_LOG_ERROR(
                Log,
                "Cannot initialize charge skill %d level %d: its target is the caster, but its target type is not self.",
                m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel
            );
            return false;
        }
        if (creature->m_SkillMod.m_nBlockMoveCount != 0 || !creature->m_data->m_bCanMove ||
            !creature->m_SkillMod.m_bCanAct) {
            return false;
        }
        CWorldObject* const target = m_spTarget.GetWorldObject();
        if (target == nullptr) {
            return false;
        }
        const bool floating = creature->m_data->IsFlying() || creature->m_AreaMod.m_bIsUnderWater;
        if (!CWorld::Inst().GetGeo()->MoveStraightTest(creature->GetPos(), target->GetPos(), 4096.0, floating, 0)) {
            creature->SendSystemMessage(SystemMessageId::TARGET_POSITION_CANNOT_BE_CHARGED);
            return false;
        }
    }

    if (operateType == SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK) {
        CWorldObject* const targetObject = m_spTarget.GetWorldObject();
        CCreature* const target = targetObject == nullptr ? nullptr : targetObject->CastCreature();
        if (target == nullptr || target->m_SkillMod.m_nBlockMoveCount != 0 || !target->m_data->m_bCanMove ||
            !target->m_SkillMod.m_bCanAct) {
            return false;
        }
        if (m_pSkillInfoA->m_TargetType != STGT_ENEMY && m_pSkillInfoA->m_TargetType != STGT_ENEMY_ONLY) {
            creature->SendSystemMessage(SystemMessageId::TARGET_POSITION_CANNOT_BE_CHARGED);
            return false;
        }
        if (target->m_bzDefendingTraitPerfect[TRAIT_KNOCKBACK] || target->CheckBlockDebuff(creature)) {
            return false;
        }

        const FVector actorPosition = creature->GetPos();
        const FVector targetPosition = target->GetPos();
        FVector direction{
            targetPosition.x - actorPosition.x, targetPosition.y - actorPosition.y, targetPosition.z - actorPosition.z};
        direction.NormalizeSquare();
        const double castRange = static_cast<double>(m_pSkillInfoA->m_nCastRange);
        const FVector destination{targetPosition.x + direction.x * castRange, targetPosition.y + direction.y * castRange,
            targetPosition.z + direction.z * castRange};
        const bool floating = target->m_data->IsFlying() || target->m_AreaMod.m_bIsUnderWater;
        if (!CWorld::Inst().GetGeo()->MoveStraightTest(targetPosition, destination, 4096.0, floating, 0)) {
            creature->SendSystemMessage(SystemMessageId::TARGET_POSITION_CANNOT_BE_CHARGED);
            return false;
        }
    }

    creature->m_spActionTarget = m_spTarget.GetObjectW();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (m_spTarget.GetId() != m_spActor.GetId() && target != nullptr) {
        bool canChangeDirection = !creature->IsNPC();
        if (!canChangeDirection) {
            CNPC* const npc = creature->CastNPC();
            canChangeDirection = npc != nullptr && npc->CanChangeDir();
        }
        if (canChangeDirection) {
            const FVector actorPosition = creature->GetPos();
            const FVector targetPosition = target->GetPos();
            const FAngle direction{static_cast<int>(
                std::atan2(targetPosition.y - actorPosition.y, targetPosition.x - actorPosition.x) * 65535.0 /
                6.283185307179586
            )};
            creature->m_data->SetDir(direction);
        }
    }

    if (target == nullptr || target == creature || target->IsTargetable()) {
        return true;
    }
    // creature->ActionFailed(0)
    creature->ActionFailed(AF_DEFAULT);
    return false;
}

void CSkillAction2::Set(
    const CSmartID actor,
    const CSmartID target,
    int skillId,
    int skillLevel,
    int skillSubLevel,
    const bool force,
    const bool surely,
    const unsigned int usedItemSid,
    const int slotNo
) {
    // Current L2Server 0x00876258.
    L2_GUARD_FUNCTION();
    m_spActor = actor;
    m_spTarget = target;
    if (CCreature* const creature = m_spActor.GetCreature();
        creature != nullptr && !ResolveSkillForSet(creature, skillId, skillLevel, skillSubLevel)) {
        return;
    }

    m_bForce = force;
    m_bSurely = surely;
    m_nUsedItemSID = static_cast<int>(usedItemSid);
    m_dConsumeFactor = 1.0;
    m_nFatalBlowSuccess = -1;
    m_nSlotNo = slotNo;
    m_Infected = false;
    m_skillUseInfo.m_skillID = skillId;
    m_skillUseInfo.m_skillLevel = skillLevel;
    m_skillUseInfo.m_skillSubLevel = skillSubLevel;
    m_skillUseInfo.m_castRange = m_pSkillInfoA != nullptr ? m_pSkillInfoA->m_nCastRange : 0;
    m_skillUseInfo.m_type = 0;
    m_skillUseInfo.m_param1 = 0;
}

void CSkillAction2::Set(
    const CSmartID actor,
    const CSmartID target,
    const SkillUseInfo& skillUseInfo,
    const bool force,
    const bool surely,
    const unsigned int usedItemSid,
    const int slotNo
) {
    // Current L2Server 0x00876628.
    L2_GUARD_FUNCTION();
    int skillId = skillUseInfo.m_skillID;
    int skillLevel = skillUseInfo.m_skillLevel;
    int skillSubLevel = skillUseInfo.m_skillSubLevel;
    m_spActor = actor;
    m_spTarget = target;
    if (CCreature* const creature = m_spActor.GetCreature();
        creature != nullptr && !ResolveSkillForSet(creature, skillId, skillLevel, skillSubLevel)) {
        return;
    }

    m_bForce = force;
    m_bSurely = surely;
    m_nUsedItemSID = static_cast<int>(usedItemSid);
    m_dConsumeFactor = 1.0;
    m_nFatalBlowSuccess = -1;
    m_nSlotNo = slotNo;
    m_Infected = false;
    m_skillUseInfo.m_skillID = skillId;
    m_skillUseInfo.m_skillLevel = skillLevel;
    m_skillUseInfo.m_skillSubLevel = skillSubLevel;
    m_skillUseInfo.m_castRange = skillUseInfo.m_castRange;
    m_skillUseInfo.m_type = skillUseInfo.m_type;
    m_skillUseInfo.m_param1 = skillUseInfo.m_param1;
}

bool CSkillAction2::CheckOrDeleteItem(
    CCreature* creature, int itemClassId, std::int64_t count, SpenItemMement moment, ConsumeItemSearchType::Type searchType
) {
    // Current L2Server 0x0087285c.
    L2_GUARD_FUNCTION();
    if (itemClassId < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"item class id in skill is not valid. [%d],[%I64d]",
        //     itemClassId,
        //     count)
        // L2_LOG_ERROR(Log,
        //     L"item class id in skill is not valid. [%d],[%I64d]",
        //     itemClassId,
        //     count)
        L2_LOG_ERROR(
            Log,
            "Skill item check/deletion rejected negative item class ID %d (count %lld); no item is deleted by this call.",
            itemClassId, static_cast<long long>(count)
        );
        return false;
    }

    if (!creature->IsPC()) {
        if (creature->IsPet()) {
            if (searchType == ConsumeItemSearchType::CIST_MASTER_INVEN) {
                if (creature->GetUserOrMaster() != nullptr) {
                    creature = creature->GetUserOrMaster();
                }
            }
        } else {
            if (!creature->IsSummonOrPet()) {
                return false;
            }
            User* const master = creature->GetUserOrMaster();
            if (master == nullptr || !master->IsAlive()) {
                return false;
            }
            creature = master;
        }
    }

    CItem* item = nullptr;
    switch (moment) {
        case BEFORE_REUSE_DELAY:
            item = creature->m_inv.GetFirstItemByClassID(itemClassId, false, false);
            if (item == nullptr || item->m_data == nullptr || item->m_data->m_nCount < count) {
                creature->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_REQUIRED_ITEMS_MISSING);
                return false;
            }
            break;

        case AFTER_REUSE_DELAY: {
            CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
            if (object == nullptr) {
                return false;
            }
            item = static_cast<CItem*>(object);
            if (item->m_const == nullptr) {
                return false;
            }
            if (item->m_const->m_enumDefaultAction == ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS) {
                return true;
            }
            if (!creature->DeleteItemInInventory(itemClassId, count, nullptr, false, false)) {
                creature->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_REQUIRED_ITEMS_MISSING);
                return false;
            }
            if (!creature->IsPC()) {
                return true;
            }
            break;
        }

        case AFTER_INSTANT_EFFECT: {
            CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
            if (object == nullptr) {
                return false;
            }
            item = static_cast<CItem*>(object);
            if (item->m_const == nullptr || item->m_const->m_enumDefaultAction != ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS) {
                return false;
            }
            if (!creature->IsPC()) {
                return true;
            }
            User* const user = creature->CastUser();
            if (user == nullptr || !user->DeleteItemInInventory2(itemClassId, count, nullptr)) {
                return false;
            }
            break;
        }

        default:
            return true;
    }

    if (item != nullptr && item->m_const != nullptr) {
        const ItemDefaultActionEnum action = item->m_const->m_enumDefaultAction;
        if (action == ITEM_ACTION_SKILL_REDUCE || action == ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS) {
            creature->CastUser()->PledgeMissionCheckAndAddCount(static_cast<PledgeMission::GoalType>(0x12), itemClassId, 1);
        }
    }
    return true;
}

bool CSkillAction2::SpendItem(CCreature* creature, SpenItemMement moment) {
    // Current L2Server 0x00874dac.
    L2_GUARD_FUNCTION();
    if (m_nUsedItemSID != 0) {
        CItem* const item = g_ItemSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUsedItemSID));
        if (item != nullptr && item->m_const != nullptr && item->m_const->m_enumDefaultAction == ITEM_ACTION_SEED) {
            if (!CheckOrDeleteItem(creature, item->GetClassId(), 1, moment, ConsumeItemSearchType::CIST_MY_INVEN)) {
                return false;
            }
            m_nUsedItemClassId = item->GetClassId();
        }
    }

    return m_pSkillInfoA->m_nConsumeItemCount < 1 ||
        CheckOrDeleteItem(
            creature, m_pSkillInfoA->m_nConsumeItem, m_pSkillInfoA->m_nConsumeItemCount, moment,
            m_pSkillInfoA->m_eConsumeItemSearchType
        );
}

void CSkillAction2::OnInstantEffectSuccess(CCreature* creature) {
    // Current L2Server 0x00875604.
    L2_GUARD_FUNCTION();
    if (creature != nullptr) {
        SpendItem(creature, AFTER_INSTANT_EFFECT);
    }
}

void CSkillAction2::ActivateAttachedItemSkill(
    CCreature& creature,
    CWorldObject& target,
    const CSkillInfo* skillInfo,
    const float probability,
    const CSkillInfo::EffectActivateTiming timing
) {
    // Current L2Server 0x00873128.
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr || !skillInfo->IsValidTarget(&creature, &target, false) ||
        static_cast<float>(::genrand64_real1() * 100.0) >= probability ||
        m_pSkillInfoA->m_nEffectPoint * skillInfo->m_nEffectPoint <= 0 || skillInfo->IsCursedRelation(creature, target)) {
        return;
    }

    if (User* const user = creature.CastUser(); user != nullptr) {
        user->GetSocket()->Send(
            "chccdd", 0x62, SystemMessageId::NAMED_EFFECT_ACTIVATED, 1, EST_SKILLID, skillInfo->m_nSkillId,
            (skillInfo->m_nSubLevel << 16) | static_cast<std::uint16_t>(skillInfo->m_nLevel)
        );
    }

    skillInfo->ActivateSkill(creature.CastCreature(), target.CastWorldObject(), 0.0, nullptr, timing, false);
}

void CSkillAction2::ActivateAttachedItemSkills(
    CCreature& creature, CWorldObject& target, const CSkillInfo::EffectActivateTiming timing
) {
    // Current L2Server 0x008756b8.
    L2_GUARD_FUNCTION();
    const SkillAffectScopeEnum scope = m_pSkillInfoA->m_AffectScope;
    if (scope == SAFS_POINT_BLANK || scope == SAFS_FAN_PB || scope == SAFS_SQUARE_PB || scope == SAFS_RING_RANGE ||
        (m_pSkillInfoA->m_nIsMagic != SIMT_MAGIC_SKILL && m_pSkillInfoA->m_nIsMagic != SIMT_MAGIC_SKILL_NOCANCEL) ||
        !target.IsCreature()) {
        return;
    }

    CSharedItemData* const weapon = creature.GetEquipedWeapon();
    if (weapon == nullptr) {
        return;
    }
    CSharedItemConstant* const constant = weapon->GetConstant();
    if (constant == nullptr || constant->GetCrystalType() > creature.GetMaxCrystal()) {
        return;
    }

    ActivateAttachedItemSkill(
        creature, target, static_cast<const CSkillInfo*>(constant->m_pMagicSkill),
        static_cast<float>(constant->m_nMagicSkillProb), timing
    );

    CItem* const item = CItemFactory::Inst().GetObjectW(weapon->index);
    if (item != nullptr) {
        ItemOptionHandler::ActivateSkillInfo info(item, &creature, &target);
        ItemOptionHandler::ActivateMagicSkills(info, *this, timing);
    }
}

void CSkillAction2::CalcMeteoPointList() {
    // Current L2Server 0x00877eec.
    L2_GUARD_FUNCTION();
    m_vMeteoPointList.clear();

    CCreature* const actor = m_spActor.GetCreature();
    const FVector actorPosition = actor->GetPos();
    int sectorAngle = 0;
    for (int sector = 0; sector < 6; ++sector) {
        int retry = 0;
        for (;;) {
            const int angle = static_cast<int>(::genrand64_real2() * 15001.0) + sectorAngle;
            const double radius = static_cast<double>(static_cast<int>(::genrand64_real1() * 350.0) + 500);
            const FVector radial{radius, 0.0, 0.0};
            const FVector axis{0.0, 0.0, 1.0};
            const FVector rotated = FVector::RotateAngleAxis(&radial, angle, &axis);
            const FVector candidate{
                actorPosition.x + rotated.x,
                actorPosition.y + rotated.y,
                actorPosition.z + rotated.z,
            };

            bool overlaps = false;
            for (const FVector& existing : m_vMeteoPointList) {
                const double dx = existing.x - candidate.x;
                const double dy = existing.y - candidate.y;
                if (dx * dx + dy * dy < 160000.0) {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps) {
                m_vMeteoPointList.push_back(candidate);
                break;
            }
            ++retry;
            if (retry >= 11) {
                break;
            }
        }
        sectorAngle += 0x2aaa;
    }

    CGeoData* const geo = CWorld::Inst().GetGeo();
    for (FVector& point : m_vMeteoPointList) {
        point.z = static_cast<double>(
            geo->FindGround(static_cast<int>(point.x), static_cast<int>(point.y), static_cast<int>(point.z) + 500)
        );
    }
}

void CSkillAction2::BroadcastSkillUse(CCreature* creature, const int reuseDelay) {
    // Current L2Server 0x00878208.
    L2_GUARD_FUNCTION();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (target == nullptr) {
        return;
    }

    std::vector<FVector>* points = nullptr;
    if (m_pSkillInfoA->m_AffectScope == SAFS_BALAKAS_SCOPE) {
        CalcMeteoPointList();
        points = &m_vMeteoPointList;
    } else if (m_pSkillInfoA->m_nSkillId == 0x1397) {
        creature->GetController()->BroadcastFlySelfDestination(m_pSkillInfoA, static_cast<int>(target->m_nId));
    }

    creature->GetController()->ActionBroadcastSkillUse(
        *target, m_pSkillInfoA, m_nHitTime + m_nHitCancelTime, reuseDelay, this, points, m_skillUseInfo
    );
}

bool CSkillAction2::OnStart() {
    // Current L2Server 0x0087838c.
    L2_GUARD_FUNCTION();
    CNewAction::OnStart();
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__SKILL_ONSTART);

    if (m_pSkillInfoA == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CSkillAction2::OnStart() m_pSkillInfoA is NULL")
        // L2_LOG_ERROR(Log,
        //     L"CSkillAction2::OnStart() m_pSkillInfoA is NULL")
        L2_LOG_ERROR(Log, "Cannot start a skill action: its skill definition is unavailable.");
        return false;
    }

    const int skillId = m_pSkillInfoA->m_nSkillId;
    const int skillLevel = m_pSkillInfoA->m_nLevel;
    const int skillSubLevel = m_pSkillInfoA->m_nSubLevel;
    const auto fail = [skillId](const int line, CCreature* const actor) {
        WriteSkillStartFailure(skillId, line, actor);
        return false;
    };

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CSkillAction2::OnStart() pActor is NULL")
        // L2_LOG_ERROR(Log,
        //     L"CSkillAction2::OnStart() pActor is NULL")
        L2_LOG_ERROR(
            Log, "Cannot start skill %d level %d sublevel %d: the caster is unavailable.", skillId, skillLevel, skillSubLevel
        );
        return fail(0x7aa, nullptr);
    }

    const int threadId = CThreadManager::GetThreadID();
    g_nSkillActorDBID[threadId] = static_cast<int>(actor->GetDBID());
    g_nSkillID[threadId] = skillId;
    g_nSkillTargetCount[threadId] = 0;
    actor->m_nLastSkillId = skillId;

    const int initialMagicType = GetMagicType();
    if (actor->m_SkillDurationMod.m_nWrongCasting[initialMagicType] > 0 &&
        static_cast<int>(::genrand64_real2() * 100.0 + 1.0) <=
            actor->m_SkillDurationMod.m_nWrongCastingProb[initialMagicType]) {
        actor->GetController()->ActionBroadcastSkillCanceled(this);
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        if (actor->IsPC()) {
            actor->SendSystemMessage(SystemMessageId::CASTING_INTERRUPTED);
            CLogFormatter formatter{};
            formatter.WriteLog2_1(0x195, actor, nullptr, skillId, 2, 0, 0, 0, 0, 0);
        }
        return fail(0x7ce, actor);
    }

    if (!actor->IsAlive()) {
        return fail(0x7d8, actor);
    }
    if (!actor->m_SkillMod.m_bCanAct && !IsCondBlockActSkill(actor, skillId)) {
        return fail(0x7e1, actor);
    }
    if (actor->GetMorphID() != 0) {
        return fail(0x7ea, actor);
    }

    if (actor->IsPC()) {
        User* const actorUser = actor->CastUser();
        if (actorUser != nullptr && actorUser->IsGMInvisible()) {
            return fail(0x7f6, actor);
        }
        if (actorUser != nullptr) {
            unsigned int failureMessage = 0;
            if (actorUser->m_data->m_StopMode == STP_SIT) {
                failureMessage = actorUser->m_data->m_bIsFakeDeath && !m_pSkillInfoA->m_bIsFakeDeath
                    ? SystemMessageId::CANNOT_USE_WHILE_FEIGNING_DEATH
                    : SystemMessageId::CANNOT_ACT_WHILE_SITTING;
                if (actorUser->m_data->m_bIsFakeDeath && m_pSkillInfoA->m_bIsFakeDeath) {
                    failureMessage = 0;
                }
            } else if (actorUser->m_data->m_StopMode == STP_STORE) {
                failureMessage = SystemMessageId::CANNOT_ACT_WHILE_SITTING;
            }
            if (failureMessage != 0) {
                actorUser->SendSystemMessage(failureMessage);
                // actorUser->ActionFailed(0)
                actorUser->ActionFailed(AF_DEFAULT);
                return false;
            }
        }
    }

    if (!CheckXMasTree(actor, skillId)) {
        return fail(0x822, actor);
    }

    const SkillOperateType operateType = m_pSkillInfoA->m_OperateType;
    const bool toggleSkill = operateType == SKILL_TOGGLE || operateType == SKILL_GROUP_TOGGLE;
    bool toggleEnabled = false;
    if (toggleSkill) {
        int toggleLevel = 0;
        int toggleSubLevel = 0;
        actor->GetToggleSkill(skillId, toggleLevel, toggleSubLevel, toggleEnabled);
    }

    const bool auraSkill = operateType == SKILL_AURA;
    bool auraEnabled = false;
    if (auraSkill) {
        int auraLevel = 0;
        int auraSubLevel = 0;
        actor->GetAuraSkill(skillId, auraLevel, auraSubLevel, auraEnabled);
    }

    if (!CondForHiddenTarget(m_spTarget.GetWorldObject())) {
        return fail(0x842, actor);
    }
    if (actor->IsPC()) {
        User* const actorUser = actor->CastUser();
        if (actorUser != nullptr && actorUser->m_ShowSkillInfo) {
            m_pSkillInfoA->ShowSkillInfo(*actorUser);
        }
    }
    if (IsBlockedSkill(actor, skillId)) {
        return fail(0x855, actor);
    }

    m_CurrentAction = SKL_NOT_STARTED;
    if (!m_pSkillInfoA->CanUsedBy(actor, false)) {
        return fail(0x867, actor);
    }

    const int magicType = GetMagicType();
    m_dConsumeFactor = m_pSkillInfoA->GetConsumeFactor(actor);
    if (!CheckIsMagicProblem(actor, magicType)) {
        return fail(0x870, actor);
    }
    if (IsBlockedMagicType(actor, magicType)) {
        if (IsChangeClassSkill() && actor->IsPC()) {
            User* const actorUser = actor->CastUser();
            if (actorUser != nullptr && actorUser->GetSocket() != nullptr) {
                actorUser->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_CHANGE_CLASS_DUE_TO_IDENTITY_CRISIS);
            }
        }
        return fail(0x884, actor);
    }
    if (!CheckOnlyUseEquipedWeaponActiveSkill(actor, skillId)) {
        return fail(0x88b, actor);
    }

    CalcSkillTiming(*actor, *m_pSkillInfoA, m_nHitCancelTime, m_nHitTime, m_nCoolTime);
    if (!ValidateSkillTarget(actor, toggleSkill, auraSkill)) {
        return fail(0x893, actor);
    }

    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (!CheckValidActionState(*actor)) {
        return fail(0x89f, actor);
    }

    int reuseDelay = 0;
    bool actionSucceeded = true;
    CSerializedMessage* serializedMessage = nullptr;

    if (!toggleSkill || !toggleEnabled) {
        if (auraSkill && auraEnabled) {
            if (!actor->AuraSkillOnOff(skillId, false)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"AuraSkillOnOff Failed. %d",
                //     skillId)
                // L2_LOG_ERROR(Log,
                //     L"AuraSkillOnOff Failed. %d",
                //     skillId)
                L2_LOG_ERROR(
                    Log, "Cannot disable aura skill %d level %d sublevel %d for object %u; skill startup stops.", skillId,
                    skillLevel, skillSubLevel, static_cast<unsigned>(actor->Id())
                );
                return fail(0x8d1, actor);
            }
            auraEnabled = false;
            const CSkillInfo* const serializedSkill =
                g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, skillSubLevel), actor);
            serializedMessage = new CSerializedPumpEndMessage(actor, actor, *serializedSkill, SPET_AURA);
        } else if (!toggleSkill) {
            if (auraSkill) {
                if (CheckConditionsOnStart(actor, auraSkill, reuseDelay)) {
                    const int groupId = m_pSkillInfoA->m_nToggleGroupId;
                    if (groupId < 1) {
                        if (!actor->AuraSkillOnOff(skillId, true)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"AuraSkillOnOff Failed. %d",
                            //     skillId)
                            // L2_LOG_ERROR(Log,
                            //     L"AuraSkillOnOff Failed. %d",
                            //     skillId)
                            L2_LOG_ERROR(
                                Log, "Cannot enable aura skill %d level %d sublevel %d for object %u; skill startup stops.",
                                skillId, skillLevel, skillSubLevel, static_cast<unsigned>(actor->Id())
                            );
                            return fail(0x94b, actor);
                        }
                    } else if (!actor->GroupAuraSkillOnPumpEnd(skillId, groupId) && actor->IsPC()) {
                        User* const actorUser = actor->CastUser();
                        if (actorUser != nullptr) {
                            if (!actorUser->IsBuilder()) {
                                // Log.Add(
                                //     LOG_ERROR,
                                //     L"[%s]GroupAuraSkillOnOff Failed. %d",
                                //     actorUser->GetName(),
                                //     skillId)
                                // L2_LOG_ERROR(Log,
                                //     L"[%s]GroupAuraSkillOnOff Failed. %d",
                                //     actorUser->GetName(),
                                //     skillId)
                                // L2_LOG_ERROR(Log,
                                //     "Cannot enable group aura skill %d (group %d) for '%s': the group-skill check failed.
                                //     skill startup stops.", skillId, groupId, WideToUtf8(actorUser->GetName()).c_str())
                                L2_LOG_ERROR(
                                    Log,
                                    "Cannot enable group aura skill %d (group %d) for '%s': the group-skill check failed; "
                                    "skill startup stops.",
                                    skillId, groupId, Utf16ToUtf8(actorUser->GetName()).c_str()
                                );
                                return fail(0x93e, actor);
                            }
                            if (actorUser->GetSocket() != nullptr) {
                                actorUser->GetSocket()->SendSystemMessage(u"SYS", u"you must have all GroupAuraSkills");
                            }
                        }
                    }
                    auraEnabled = true;
                    m_pSkillInfoA->PostActivateSkillEffects(actor, actor, CSkillInfo::EAT_SELF);
                    const CSkillInfo* const serializedSkill =
                        g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, skillSubLevel), actor);
                    serializedMessage =
                        new CSerializedOnInfectedSkillByMessage(actor, actor, *serializedSkill, SPST_AURA, false);
                } else {
                    actionSucceeded = false;
                }
            } else if (CheckConditionsOnStart(actor, false, reuseDelay)) {
                if (actor->m_data->m_dBaseMP <= actor->m_data->m_dMP && actor->m_data->m_dMP != actor->m_data->m_dBaseMP) {
                    actor->m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
                        actor->m_data->m_dMP, actor->m_data->m_dBaseMP, 0xc
                    );
                }
                const int consumedMP = CalcConsumeMP(actor, m_pSkillInfoA, m_pSkillInfoA->m_nMpConsume1, m_dConsumeFactor);
                const int paybackMP = CalcPaybackMP(*actor, *m_pSkillInfoA, static_cast<double>(consumedMP));
                actor->ConsumeMP(static_cast<double>(consumedMP - paybackMP));
            } else {
                actionSucceeded = false;
            }
        } else if (CheckConditionsOnStart(actor, toggleSkill, reuseDelay)) {
            const int groupId = m_pSkillInfoA->m_nToggleGroupId;
            if (groupId < 1) {
                if (!actor->ToggleSkillOnOff(skillId, true)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"ToggleSkillOnOff Failed. %d",
                    //     skillId)
                    // L2_LOG_ERROR(Log,
                    //     L"ToggleSkillOnOff Failed. %d",
                    //     skillId)
                    L2_LOG_ERROR(
                        Log, "Cannot enable toggle skill %d level %d sublevel %d for object %u; skill startup stops.",
                        skillId, skillLevel, skillSubLevel, static_cast<unsigned>(actor->Id())
                    );
                    return fail(0x90b, actor);
                }
            } else if (!actor->GroupToggleSkillOnPumpEnd(skillId, groupId) && actor->IsPC()) {
                User* const actorUser = actor->CastUser();
                if (actorUser != nullptr) {
                    if (!actorUser->IsBuilder()) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s]GroupToggleSkillOnOff Failed. %d",
                        //     actorUser->GetName(),
                        //     skillId)
                        // L2_LOG_ERROR(Log,
                        //     L"[%s]GroupToggleSkillOnOff Failed. %d",
                        //     actorUser->GetName(),
                        //     skillId)
                        // L2_LOG_ERROR(Log,
                        //     "Cannot enable group toggle skill %d (group %d) for '%s': the group-skill check failed. Skill
                        //     startup stops.", skillId, groupId, WideToUtf8(actorUser->GetName()).c_str())
                        L2_LOG_ERROR(
                            Log,
                            "Cannot enable group toggle skill %d (group %d) for '%s': the group-skill check failed; skill "
                            "startup stops.",
                            skillId, groupId, Utf16ToUtf8(actorUser->GetName()).c_str()
                        );
                        return fail(0x8fd, actor);
                    }
                    if (actorUser->GetSocket() != nullptr) {
                        actorUser->GetSocket()->SendSystemMessage(u"SYS", u"you must have all GroupToggleSkills");
                    }
                }
            }
            toggleEnabled = true;
            m_pSkillInfoA->PostActivateSkillEffects(actor, actor, CSkillInfo::EAT_SELF);
            const CSkillInfo* const serializedSkill =
                g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, skillSubLevel), actor);
            serializedMessage = new CSerializedOnInfectedSkillByMessage(actor, actor, *serializedSkill, SPST_TOGGLE, false);
        } else {
            actionSucceeded = false;
        }
    } else if (m_pSkillInfoA->m_nToggleGroupId < 1 || !m_pSkillInfoA->m_bIsNeccessaryTG) {
        if (!actor->ToggleSkillOnOff(skillId, false)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"ToggleSkillOnOff Failed. %d",
            //     skillId)
            // L2_LOG_ERROR(Log,
            //     L"ToggleSkillOnOff Failed. %d",
            //     skillId)
            L2_LOG_ERROR(
                Log, "Cannot disable toggle skill %d level %d sublevel %d for object %u; skill startup stops.", skillId,
                skillLevel, skillSubLevel, static_cast<unsigned>(actor->Id())
            );
            return fail(0x8b0, actor);
        }
        toggleEnabled = false;
        const CSkillInfo* const serializedSkill = g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, 0), actor);
        serializedMessage = new CSerializedPumpEndMessage(actor, actor, *serializedSkill, SPET_TOGGLE);
    }

    if (serializedMessage != nullptr) {
        actor->PushSerializedMessage(serializedMessage);
        if (actor->IsPC()) {
            actor->CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        } else {
            actor->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        }
    }

    SetValidSkillPendingAction(actor, toggleSkill, toggleEnabled);
    if (!actionSucceeded) {
        if (!toggleSkill) {
            actor->GetController()->ActionBroadcastStopMove();
        }
        fail(0x988, actor);
        return false;
    }

    if ((!actor->m_SkillMod.m_bCanPreserveHide || m_pSkillInfoA->m_nEffectPoint < 1) && actor->IsInvisibleBySkill()) {
        actor->SetVisibleBySkill(true, true);
    }
    actor->DispelBySlot(ABNS_INVINCIBILITY, nullptr, -1, nullptr, SPET_ATTACK_OR_SKILL, false);

    if (m_pSkillInfoA->m_nEventType == 1 && target != nullptr) {
        actor->OnUseSkillStarted(static_cast<int>(target->m_nId), skillId, skillLevel, skillSubLevel);
        WriteSkillStartSuccess(skillId, 0x99e, actor);
    }
    if (!toggleSkill && !auraSkill) {
        BroadcastSkillUse(actor, reuseDelay);
    }
    HolyThingSkill(actor, m_spTarget, skillId, skillLevel, skillSubLevel);

    if (m_pSkillInfoA->m_UltimateSkillLv == 0) {
        actor->SendStatusUpdate(VCP_MP, 0, 0);
    } else {
        actor->UltimateSkillGaugeInc(-kUltimateSkillLevelGauge[m_pSkillInfoA->m_UltimateSkillLv]);
        actor->SendStatusUpdate(VCP_MP, 0, 0);
        actor->SendStatusUpdate(VCP_ULTIMATE_SKILL_POINT, 0, 0);
    }
    actor->m_data->m_NeedUpdatePacketBitSet.ClearBit(0xc);

    if (actor->IsPC()) {
        User* const actorUser = actor->CastUser();
        if (actorUser != nullptr && actorUser->GetSocket() != nullptr && target != nullptr) {
            CUserSocket* const socket = actorUser->GetSocket();
            const int encodedLevel = (skillSubLevel << 16) | (skillLevel & 0xffff);
            if (!toggleSkill) {
                if (auraSkill) {
                    socket->Send(
                        "chccdd", 0x62, auraEnabled ? SystemMessageId::USING_SKILL : SystemMessageId::NAMED_ACTION_ABORTED,
                        1, 4, skillId, encodedLevel
                    );
                    actorUser->m_bAbnormalStatusChanged = true;
                    if (!m_pSkillInfoA->m_AbnormalVisualEffect.empty()) {
                        actorUser->CheckAbnormalVisualEffect();
                    }
                } else {
                    if (skillId == 0x7fe) {
                        actorUser->SendSystemMessage(SystemMessageId::SUMMONING_PET);
                    } else {
                        socket->Send("chccdd", 0x62, SystemMessageId::USING_SKILL, 1, 4, skillId, encodedLevel);
                    }
                    const int totalTime = m_nHitTime + m_nHitCancelTime;
                    socket->Send(
                        "cdddd", 0x6b, actorUser->GetServerId(),
                        // 0,
                        GaugeType::SKILL, totalTime, totalTime
                    );
                }
            } else {
                socket->Send(
                    "chccdd", 0x62, toggleEnabled ? SystemMessageId::USING_SKILL : SystemMessageId::NAMED_ACTION_ABORTED, 1,
                    4, skillId, encodedLevel
                );
                actorUser->m_bAbnormalStatusChanged = true;
                if (!m_pSkillInfoA->m_AbnormalVisualEffect.empty()) {
                    actorUser->CheckAbnormalVisualEffect();
                }
            }

            CLogFormatter formatter{};
            formatter.WriteLog2_1(
                0x193, actorUser, target, skillId, encodedLevel, actorUser->m_data->m_nBuilder, target->IsNPC(), 0, 0, 0
            );
        }
    }

    if (toggleSkill || auraSkill) {
        m_CurrentAction = SKL_COOLTIME;
        return false;
    }

    actor->m_nLaunchedSkills.fetch_add(1, std::memory_order_seq_cst);
    if (m_pSkillInfoA->m_NextAction == ACT_SKILL && m_pController != nullptr) {
        m_pController->SetAsSkill(1, m_spTarget, m_skillUseInfo, false, false, 0);
    }
    m_pSkillInfoA->SetAvoidSkillCanceling(*actor, true);

    int delay = 0;
    if (operateType >= SKILL_CHANNELLING_INSTANCE && operateType <= SKILL_CHANNELLING_DURATION_LEVEL_UP) {
        m_CurrentAction = SKL_CHANNELLING_CASTING;
        delay = m_nHitTime;
    } else {
        m_CurrentAction = SKL_HITCANCELTIME;
        delay = m_nHitCancelTime;
    }
    if (delay > 0x4f) {
        AddTimer(delay, m_nMyIndex, 0);
        return true;
    }
    return OnTick();
}

bool CSkillAction2::Activate(CCreature& creature, CWorldObject& target, const CSkillInfo::EffectActivateTiming timing) {
    // Current L2Server 0x008797a0.
    L2_GUARD_FUNCTION();
    const int skillId = m_pSkillInfoA->m_nSkillId;
    const int skillLevel = m_pSkillInfoA->m_nLevel;
    const int skillSubLevel = m_pSkillInfoA->m_nSubLevel;

    if (creature.m_vSkillTarget.c.size() != creature.m_vSkillTarget.d.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] m_vSkillTarget count mismatch between c and d c[%d] d[%d]",
        //     L"newaction_skill.cpp",
        //     0xc13,
        //     static_cast<int>(creature.m_vSkillTarget.c.size()),
        //     static_cast<int>(creature.m_vSkillTarget.d.size()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] m_vSkillTarget count mismatch between c and d c[%d] d[%d]",
        //     L"newaction_skill.cpp",
        //     0xc13,
        //     static_cast<int>(creature.m_vSkillTarget.c.size()),
        //     static_cast<int>(creature.m_vSkillTarget.d.size()))
        L2_LOG_ERROR(
            Log,
            "Skill %d level %d sublevel %d activation has mismatched target arrays: %llu creatures and %llu distance "
            "entries; processing continues.",
            skillId, skillLevel, skillSubLevel, static_cast<unsigned long long>(creature.m_vSkillTarget.c.size()),
            static_cast<unsigned long long>(creature.m_vSkillTarget.d.size())
        );
    }

    for (std::size_t index = 0; index < creature.m_vSkillTarget.c.size();) {
        CSPointer<CCreature> targetPointer = runtime::CheckedVectorAt(creature.m_vSkillTarget.c, index).FindObjectSP();
        CCreature* const targetCreature = targetPointer.get();
        bool erase = targetCreature == nullptr;
        if (!erase && targetCreature->IsPC()) {
            erase = targetCreature->CastUser()->m_bFlyMoving || targetCreature->CastUser()->m_bBeautyShop;
        }
        if (erase) {
            creature.m_vSkillTarget.c.erase(creature.m_vSkillTarget.c.begin() + index);
            if (index < creature.m_vSkillTarget.d.size()) {
                creature.m_vSkillTarget.d.erase(creature.m_vSkillTarget.d.begin() + index);
            }
            continue;
        }
        ++index;
    }

    bool pvpRelation = false;
    bool initiativeViolet = false;
    bool multiTarget = false;
    const SkillAffectScopeEnum affectScope = m_pSkillInfoA->m_AffectScope;

    if (affectScope == SAFS_SINGLE || affectScope == SAFS_RANGE_SORT_BY_HP) {
        if (!m_pSkillInfoA->IsCursedRelation(creature, target)) {
            if (target.IsCreature()) {
                CCreature* const targetCreature = target.CastCreature();
                if (targetCreature != nullptr) {
                    targetCreature->m_bAttackedByRange = false;
                }
            }
            if (target.IsPC() && (target.CastUser()->m_bFlyMoving || target.CastUser()->m_bBeautyShop)) {
                return false;
            }

            const bool activated =
                m_pSkillInfoA->ActivateSkill(creature.CastCreature(), target.CastWorldObject(), 0.0, this, timing, false);
            if (!activated) {
                return false;
            }

            if (m_pSkillInfoA->m_OperateType == SKILL_CHANNELLING_DURATION ||
                m_pSkillInfoA->m_OperateType == SKILL_CHANNELLING_DURATION_LEVEL_UP) {
                const CSkillInfo* const noticeSkill = m_pSkillInfoA->m_pAttachedSkill;
                g_npcserver.Send(
                    "cddddddd", 0x0e, static_cast<int>(creature.m_nId), creature.GetIndex(), static_cast<int>(target.m_nId),
                    noticeSkill->m_nSkillId, noticeSkill->m_nLevel, noticeSkill->m_nSubLevel, noticeSkill->m_nNpcNotice
                );
            } else {
                g_npcserver.Send(
                    "cddddddd", 0x0e, static_cast<int>(creature.m_nId), creature.GetIndex(), static_cast<int>(target.m_nId),
                    skillId, skillLevel, skillSubLevel, m_pSkillInfoA->m_nNpcNotice
                );
            }

            if (target.IsCreature()) {
                CCreature* const targetCreature = target.CastCreature();
                if (targetCreature != nullptr) {
                    const bool positiveEffect = m_pSkillInfoA->m_nEffectPoint > 0;
                    pvpRelation =
                        targetCreature->CastCreature()->IsMakeAttackerGuilty(creature.CastCreature(), positiveEffect);
                    if (pvpRelation && !positiveEffect && !targetCreature->CastCreature()->IsInDuel()) {
                        initiativeViolet = true;
                    }
                }
            }
        }

        creature.TriggerSkillBySkill(skillId, target, bCritical);
        if (target.IsCreature()) {
            CCreature* const targetCreature = target.CastCreature();
            if (targetCreature != nullptr) {
                targetCreature->TriggerSkillByAttackedSkill(skillId, &creature);
            }
        }
    } else {
        multiTarget = true;
        const bool friendly = m_pSkillInfoA->m_AffectObjectType == SAFOT_FRIEND ||
            m_pSkillInfoA->m_AffectObjectType == SAFOT_FRIEND_PC || m_pSkillInfoA->m_nEffectPoint > 0;
        creature.m_nChainAttackCount = 0;

        for (std::size_t index = 0; index < creature.m_vSkillTarget.c.size(); ++index) {
            CSPointer<CCreature> targetPointer = runtime::CheckedVectorAt(creature.m_vSkillTarget.c, index).FindObjectSP();
            CCreature* const targetCreature = targetPointer.get();
            if (targetCreature == nullptr || !targetCreature->IsCreature() ||
                m_pSkillInfoA->IsCursedRelation(creature, target)) {
                continue;
            }

            if (m_pSkillInfoA->m_OperateType == SKILL_CHANNELLING_DURATION ||
                m_pSkillInfoA->m_OperateType == SKILL_CHANNELLING_DURATION_LEVEL_UP) {
                creature.m_vChannelingSkillTarget.c.push_back(runtime::CheckedVectorAt(creature.m_vSkillTarget.c, index));
            }
            if (m_pSkillInfoA->CheckOperateCond(creature.CastCreature(), targetCreature, CSkillInfo::OCT_TARGET) != -2) {
                continue;
            }

            targetCreature->CastCreature()->m_bAttackedByRange = true;
            const double distance =
                index < creature.m_vSkillTarget.d.size() ? runtime::CheckedVectorAt(creature.m_vSkillTarget.d, index) : 0.0;
            m_pSkillInfoA->ActivateSkill(creature.CastCreature(), targetCreature, distance, this, timing, false);
            g_npcserver.Send(
                "cddddddd", 0x0e, static_cast<int>(creature.m_nId), creature.GetIndex(),
                static_cast<int>(targetCreature->m_nId), skillId, skillLevel, skillSubLevel, m_pSkillInfoA->m_nNpcNotice
            );

            if (!pvpRelation) {
                pvpRelation = targetCreature->CastCreature()->IsMakeAttackerGuilty(creature.CastCreature(), friendly);
            }
            if (!friendly && !initiativeViolet && !targetCreature->CastCreature()->IsInDuel()) {
                initiativeViolet = true;
            }
            ++creature.m_nChainAttackCount;
        }

        for (std::size_t index = 0; index < creature.m_vSkillStaticTarget.size(); ++index) {
            CSPointer<CStaticObject> targetPointer =
                runtime::CheckedVectorAt(creature.m_vSkillStaticTarget, index).FindObjectSP();
            CStaticObject* const staticTarget = targetPointer.get();
            if (staticTarget == nullptr || !staticTarget->IsStaticObject() ||
                m_pSkillInfoA->CheckOperateCond(creature.CastCreature(), staticTarget, CSkillInfo::OCT_TARGET) != -2) {
                continue;
            }
            const double distance = index < creature.m_vSkillStaticTargetDistance.size()
                ? runtime::CheckedVectorAt(creature.m_vSkillStaticTargetDistance, index)
                : 0.0;
            m_pSkillInfoA->ActivateSkill(creature.CastCreature(), staticTarget, distance, this, timing, false);
        }
    }

    creature.TriggerSkillByMagicType(GetMagicType(), target, m_pSkillInfoA->m_nEffectPoint < 0, multiTarget);
    m_pSkillInfoA->PostActivateSkillEffects(&creature, &creature, CSkillInfo::EAT_SELF);

    if (pvpRelation) {
        CCreature* violetOwner = nullptr;
        if (creature.IsSummonOrPet()) {
            if (CSummon* const summon = creature.CastSummon(); summon != nullptr) {
                violetOwner = summon->GetMaster();
            }
        } else if (creature.IsTrap()) {
            if (CTrap* const trap = creature.CastTrap(); trap != nullptr) {
                violetOwner = trap->GetMaster();
            }
        } else if (creature.IsNPC()) {
            if (CNPC* const npc = creature.CastNPC(); npc != nullptr && npc->HasSummoner()) {
                violetOwner = creature.CastNPC()->GetMaster();
            }
        } else {
            violetOwner = creature.CastCreature();
        }
        if (violetOwner == nullptr) {
            violetOwner = creature.CastCreature();
        }
        if (initiativeViolet) {
            violetOwner->m_bIsInitiativeViolet = true;
        }
        CIOObject::SetTimeStamp(&violetOwner->m_data->m_nPKTimeStamp);
    }

    m_pSkillInfoA->CheckAndTurnOffSoulSpiritShot(creature);
    ActivateAttachedItemSkills(creature, target, timing);

    const bool spellFromFirstTarget = affectScope == SAFS_POINT_BLANK || affectScope == SAFS_FAN_PB ||
        affectScope == SAFS_SQUARE_PB || affectScope == SAFS_RING_RANGE;
    if (spellFromFirstTarget) {
        if (m_pSkillInfoA->m_TargetType == STGT_SELF) {
            for (CSafePointer2<CCreature>& targetReference : creature.m_vSkillTarget.c) {
                CSPointer<CCreature> targetPointer = targetReference.FindObjectSP();
                CCreature* const spellTarget = targetPointer.get();
                if (spellTarget != nullptr && spellTarget->IsCreature()) {
                    CWorld::Inst().Spell(creature, *spellTarget, skillId, skillLevel, skillSubLevel);
                    break;
                }
            }
        }
    } else {
        CWorld::Inst().Spell(creature, target, skillId, skillLevel, skillSubLevel);
    }

    if (creature.IsPC() && target.IsCreature()) {
        CCreature* targetCreature = target.CastCreature();
        CContributeData* contribute = nullptr;
        if (targetCreature != nullptr && m_pSkillInfoA->m_nEffectPoint < 0) {
            if (targetCreature->IsNPC() && creature.IsEnemyTo(targetCreature)) {
                contribute = targetCreature->m_pContribute;
            }
        } else if (targetCreature != nullptr && m_pSkillInfoA->m_nEffectPoint > 0) {
            if (targetCreature->IsSummonOrPet()) {
                CSummon* const summon = targetCreature->CastSummon();
                targetCreature = summon != nullptr ? summon->GetMaster() : nullptr;
            }
            if (targetCreature != nullptr && targetCreature->IsPC()) {
                CCreature* const combatCreature = targetCreature->m_spTarget.GetCreature();
                if (combatCreature != nullptr && targetCreature->IsEnemyTo(combatCreature)) {
                    contribute = combatCreature->m_pContribute;
                }
            }
        }
        if (contribute != nullptr) {
            contribute->AccumulateSubContribute(&creature, 1);
        }
    }

    if (!creature.IsDoubleCasting(skillId, skillLevel, skillSubLevel)) {
        creature.m_vSkillTarget.c.clear();
        creature.m_vSkillTarget.d.clear();
        creature.m_vSkillStaticTarget.clear();
        creature.m_vSkillStaticTargetDistance.clear();
    }
    return true;
}

void CSkillAction2::GatherSkillTarget(CCreature* actor, CWorldObject* target) {
    // Current L2Server 0x0087a584.
    L2_GUARD_FUNCTION();

    const int threadId = CThreadManager::GetThreadID();
    g_nSkillActorDBID[threadId] = static_cast<int>(actor->GetDBID());
    g_nSkillID[threadId] = m_pSkillInfoA->m_nSkillId;
    g_nSkillTargetCount[threadId] = 0;

    if (m_pSkillInfoA->m_AffectScope == SAFS_SINGLE || !target->IsCreature()) {
        actor->GetController()->ActionBroadcastSkillLaunched(
            m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, m_pSkillInfoA->m_nSubLevel, static_cast<int>(target->m_nId),
            m_nSlotNo
        );
        return;
    }

    CCreature* const targetCreature = target->CastCreature();
    CCreature::SkillTargetData& targetData = actor->m_vSkillTarget;
    targetData.c.clear();
    targetData.d.clear();
    actor->m_vSkillStaticTarget.clear();
    actor->m_vSkillStaticTargetDistance.clear();

    RangeTargetType rangeTarget = AreaBasedRule::RangeTargetOfSkill(actor, targetCreature, m_pSkillInfoA->m_TargetType);
    const int affectLimit =
        static_cast<int>(::genrand64_real2() * static_cast<double>(m_pSkillInfoA->m_nAffectLimitRandom + 1) + 0.0) +
        m_pSkillInfoA->m_nAffectLimitBase;

    FVector center{};
    bool checkCanSee = true;
    bool requireBattleField = false;
    CCreature* exceptCreature = nullptr;
    switch (m_pSkillInfoA->m_TargetType) {
        case STGT_GROUND:
            center = actor->m_NonTargetSkillPos;
            break;
        case STGT_ARTILLERY:
            center = actor->m_NonTargetSkillPos;
            checkCanSee = false;
            requireBattleField = g_bIsNonPKServer;
            rangeTarget = RTT_WHITE;
            break;
        case STGT_SHIP_CANNON:
            center = actor->m_NonTargetSkillPos;
            rangeTarget = RTT_WHITE;
            break;
        default:
            center = targetCreature->m_data->m_Pos;
            exceptCreature = targetCreature;
            if (m_pSkillInfoA->m_TargetType == STGT_EXCEPTION_SELF) {
                rangeTarget = RTT_WHITE;
            }
            break;
    }

    const auto appendExceptCreature = [&]() {
        if (exceptCreature != nullptr) {
            targetData.c.push_back(CSafePointer2<CCreature>(exceptCreature));
            targetData.d.push_back(0.0);
        }
    };
    const auto gatherCreature =
        [&, this](CCreature::SkillTargetData& output, const int limit, const bool limitToPc, CCreature* except) {
            targetCreature->GatherNeighborCreature(
                center, static_cast<int>(rangeTarget), limit, limitToPc, output, m_pSkillInfoA->m_nAffectRange,
                m_pSkillInfoA->m_nAffectScopeMinZ, m_pSkillInfoA->m_nAffectScopeMaxZ, m_pSkillInfoA->m_AffectObjectType,
                actor, except, checkCanSee, requireBattleField
            );
        };
    const auto gatherAirShip = [&]() {
        if (m_pSkillInfoA->m_TargetType == STGT_SHIP_CANNON) {
            targetCreature->GatherNeighborAirShip(center, actor, m_pSkillInfoA, checkCanSee);
        }
    };

    switch (m_pSkillInfoA->m_AffectScope) {
        case SAFS_RANGE:
            gatherAirShip();
            gatherCreature(targetData, affectLimit, m_pSkillInfoA->m_AffectLimitToPC, exceptCreature);
            appendExceptCreature();
            break;
        case SAFS_RANGE_SORT_BY_HP: {
            gatherAirShip();
            gatherCreature(targetData, affectLimit, m_pSkillInfoA->m_AffectLimitToPC, exceptCreature);
            std::sort(targetData.c.begin(), targetData.c.end(), CompareByHP);
            if (targetData.c.size() != targetData.d.size()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] m_vSkillTarget count mismatch between c and d c[%d] d[%d]",
                //     L"newaction_skill.cpp",
                //     0xdda,
                //     static_cast<int>(targetData.c.size()),
                //     static_cast<int>(targetData.d.size()))
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] m_vSkillTarget count mismatch between c and d c[%d] d[%d]",
                //     L"newaction_skill.cpp",
                //     0xdda,
                //     static_cast<int>(targetData.c.size()),
                //     static_cast<int>(targetData.d.size()))
                L2_LOG_ERROR(
                    Log,
                    "HP-sorted skill targets have mismatched arrays: %llu creatures and %llu distance entries; target "
                    "limiting continues.",
                    static_cast<unsigned long long>(targetData.c.size()),
                    static_cast<unsigned long long>(targetData.d.size())
                );
            }
            const std::size_t count = static_cast<std::size_t>(static_cast<std::int64_t>(affectLimit) + 1);
            if (count < targetData.c.size()) {
                targetData.c.resize(count);
                targetData.d.resize(count);
            }
            appendExceptCreature();
            break;
        }
        case SAFS_RANGE_SORT_BY_DIST: {
            gatherAirShip();
            CCreature::SkillTargetData gathered;
            gatherCreature(gathered, 0, false, exceptCreature);
            if (exceptCreature != nullptr) {
                gathered.c.push_back(CSafePointer2<CCreature>(exceptCreature));
                gathered.d.push_back(0.0);
            }
            std::vector<int> order(gathered.d.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&gathered](const int lhs, const int rhs) {
                return runtime::CheckedVectorAt(gathered.d, static_cast<std::size_t>(lhs)) <
                    runtime::CheckedVectorAt(gathered.d, static_cast<std::size_t>(rhs));
            });
            if (static_cast<std::size_t>(affectLimit) < order.size()) {
                order.resize(static_cast<std::size_t>(affectLimit));
            }
            targetData.c.resize(order.size());
            targetData.d.resize(order.size());
            for (std::size_t index = 0; index < order.size(); ++index) {
                const std::size_t source = static_cast<std::size_t>(order[index]);
                runtime::CheckedVectorAt(targetData.c, index) = runtime::CheckedVectorAt(gathered.c, source);
                runtime::CheckedVectorAt(targetData.d, index) = runtime::CheckedVectorAt(gathered.d, source);
            }
            break;
        }
        case SAFS_FAN:
        case SAFS_FAN_PB: {
            const int angle = static_cast<int>((static_cast<double>(m_pSkillInfoA->m_DamageRange.angle) / 360.0) * 65535.0);
            targetCreature->GatherNeighborCreatureArc(
                static_cast<int>(rangeTarget), affectLimit, m_pSkillInfoA->m_AffectLimitToPC, targetData,
                m_pSkillInfoA->m_DamageRange, m_pSkillInfoA->m_nAffectScopeMinZ, m_pSkillInfoA->m_nAffectScopeMaxZ,
                m_pSkillInfoA->m_AffectObjectType, actor,
                m_pSkillInfoA->m_AffectScope == SAFS_FAN ? exceptCreature : targetCreature, center, angle
            );
            if (m_pSkillInfoA->m_AffectScope == SAFS_FAN) {
                appendExceptCreature();
            }
            break;
        }
        case SAFS_PARTY:
        case SAFS_DEAD_PARTY:
            targetCreature->GatherNeighborParty(
                targetData, m_pSkillInfoA->m_nAffectRange, m_pSkillInfoA->m_nAffectScopeMinZ,
                m_pSkillInfoA->m_nAffectScopeMaxZ, actor, m_pSkillInfoA->m_AffectScope == SAFS_DEAD_PARTY
            );
            break;
        case SAFS_UNION:
        case SAFS_DEAD_UNION:
            targetCreature->GatherNeighborMPCC(
                targetData, m_pSkillInfoA->m_nAffectRange, actor, m_pSkillInfoA->m_AffectScope == SAFS_DEAD_UNION
            );
            break;
        case SAFS_PLEDGE:
        case SAFS_DEAD_PLEDGE:
            targetCreature->GatherNeighborPledge(
                targetData, m_pSkillInfoA->m_nAffectRange, m_pSkillInfoA->m_nAffectScopeMinZ,
                m_pSkillInfoA->m_nAffectScopeMaxZ, actor, m_pSkillInfoA->m_AffectScope == SAFS_DEAD_PLEDGE
            );
            break;
        case SAFS_POINT_BLANK:
        case SAFS_WYVERN_SCOPE:
            gatherCreature(targetData, affectLimit, m_pSkillInfoA->m_AffectLimitToPC, exceptCreature);
            break;
        case SAFS_SQUARE:
        case SAFS_SQUARE_PB:
            targetCreature->GatherNeighborCreatureSquare(
                static_cast<int>(rangeTarget), affectLimit, m_pSkillInfoA->m_AffectLimitToPC, targetData,
                m_pSkillInfoA->m_DamageRange, m_pSkillInfoA->m_nAffectScopeMinZ, m_pSkillInfoA->m_nAffectScopeMaxZ,
                m_pSkillInfoA->m_AffectObjectType, actor,
                m_pSkillInfoA->m_AffectScope == SAFS_SQUARE ? exceptCreature : targetCreature, center
            );
            if (m_pSkillInfoA->m_AffectScope == SAFS_SQUARE) {
                appendExceptCreature();
            }
            break;
        case SAFS_BALAKAS_SCOPE:
            gatherCreature(targetData, affectLimit, m_pSkillInfoA->m_AffectLimitToPC, targetCreature);
            for (const FVector& point : m_vMeteoPointList) {
                targetCreature->GatherNeighborCreatureByPoint(
                    static_cast<int>(rangeTarget), targetData, 200, m_pSkillInfoA->m_nAffectScopeMinZ,
                    m_pSkillInfoA->m_nAffectScopeMaxZ, m_pSkillInfoA->m_AffectObjectType, actor, point
                );
            }
            break;
        case SAFS_STATIC_OBJECT:
            targetCreature->GatherNeighborStaticObject(
                center, actor->m_vSkillStaticTarget, actor->m_vSkillStaticTargetDistance, m_pSkillInfoA->m_nAffectRange,
                m_pSkillInfoA->m_nAffectScopeMinZ, m_pSkillInfoA->m_nAffectScopeMaxZ, m_pSkillInfoA->m_AffectObjectType,
                actor
            );
            break;
        case SAFS_CHARGE:
            targetCreature->GatherNeighborChargeTarget(
                static_cast<int>(rangeTarget), affectLimit, targetData, m_pSkillInfoA->m_nAffectRange,
                m_pSkillInfoA->m_nAffectScopeMinZ, m_pSkillInfoA->m_nAffectScopeMaxZ, m_pSkillInfoA->m_AffectObjectType,
                actor, targetCreature
            );
            appendExceptCreature();
            break;
        case SAFS_RING_RANGE:
            targetCreature->GatherNeighborCreatureRingRange(
                center, static_cast<int>(rangeTarget), affectLimit, targetData, m_pSkillInfoA->m_nAffectRange,
                m_pSkillInfoA->m_DamageRange, m_pSkillInfoA->m_nAffectScopeMinZ, m_pSkillInfoA->m_nAffectScopeMaxZ,
                m_pSkillInfoA->m_AffectObjectType, actor, exceptCreature, checkCanSee, requireBattleField
            );
            break;
        case SAFS_PARTY_PLEDGE:
        case SAFS_DEAD_PARTY_PLEDGE: {
            const bool dead = m_pSkillInfoA->m_AffectScope == SAFS_DEAD_PARTY_PLEDGE;
            targetCreature->GatherNeighborParty(
                targetData, m_pSkillInfoA->m_nAffectRange, m_pSkillInfoA->m_nAffectScopeMinZ,
                m_pSkillInfoA->m_nAffectScopeMaxZ, actor, dead
            );
            targetCreature->GatherNeighborPledge(
                targetData, m_pSkillInfoA->m_nAffectRange, m_pSkillInfoA->m_nAffectScopeMinZ,
                m_pSkillInfoA->m_nAffectScopeMaxZ, actor, dead
            );
            break;
        }
        case SAFS_ALL_SUMMON_AND_MASTER:
        case SAFS_ALL_SUMMON_EXCEPT_MASTER:
            targetCreature->GatherAllSummonAndMaster(
                targetData, m_pSkillInfoA->m_AffectScope == SAFS_ALL_SUMMON_EXCEPT_MASTER
            );
            break;
        default:
            break;
    }

    CCreatureController::CreatureSP2VectorType launchedTargets(targetData.c);
    actor->GetController()->ActionBroadcastSkillLaunched(
        m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, m_pSkillInfoA->m_nSubLevel, launchedTargets, m_nSlotNo
    );
    if (m_pSkillInfoA->m_TargetType == STGT_SHIP_CANNON) {
        actor->GetController()->ActionBroadcastEarthQuake(center, 15, 2);
    }
    if (actor->m_data->m_nSoulShot != 0) {
        actor->m_sNowSoulShot = 1;
    }
}

bool CSkillAction2::OnTickHandle() {
    // Current L2Server 0x0087b4cc.
    L2_GUARD_FUNCTION();
    CNewAction::OnTick();

    CCreature* const actor = m_spActor.GetCreature();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (actor == nullptr || !actor->IsAlive() || target == nullptr || !target->IsObject()) {
        return false;
    }

    const auto cancelSkill = [this, actor]() {
        actor->GetController()->ActionBroadcastSkillCanceled(this);
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
    };
    const auto sendSkillDebug = [actor](const char16_t* format, const int milliseconds) {
        if (!actor->IsSkillDebugMode()) {
            return;
        }
        std::array<char16_t, 128> message{};
        FormatUtf16To(message.data(), message.size(), format, milliseconds / 1000);
        actor->SendSystemMessage(u"SYS", message.data());
    };

    const int magicType = GetMagicType();
    if (actor->m_SkillDurationMod.m_nWrongCasting[magicType] > 0 &&
        static_cast<int>(::genrand64_real2() * 100.0 + 1.0) <= actor->m_SkillDurationMod.m_nWrongCastingProb[magicType]) {
        cancelSkill();
        if (actor->IsPC()) {
            actor->SendSystemMessage(SystemMessageId::CASTING_INTERRUPTED);
            CLogFormatter formatter{};
            formatter.WriteLog2_1(0x195, actor, nullptr, m_pSkillInfoA->m_nSkillId, 2, 0, 0, 0, 0, 0);
        }
        return false;
    }
    if (!IsTargetInSameInZone()) {
        return false;
    }
    if (target != actor && !target->IsTargetable()) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (!CondForHiddenTarget(target)) {
        return false;
    }
    if (!actor->m_SkillMod.m_bCanAct && !IsCondBlockActSkill(actor, m_pSkillInfoA->m_nSkillId)) {
        return false;
    }

    const bool blockedByMagicType =
        (!actor->m_SkillMod.m_bCanSpell && (magicType == SIMT_MAGIC_SKILL || magicType == SIMT_MAGIC_SKILL_NOCANCEL)) ||
        (!actor->m_SkillMod.m_bCanPSkill &&
            (magicType == SIMT_PHYSICAL_SKILL || magicType == SIMT_SONGDANCE_SKILL ||
                magicType == SIMT_PHYSICAL_SKILL_NOCANCEL)) ||
        (!actor->m_SkillMod.m_bCanSSkill && magicType == SIMT_ETC_SKILL) ||
        (!actor->m_SkillMod.m_bCanTSkill && magicType == SIMT_TRIGGER_SKILL) || IsBlockedMagicType(actor, magicType);
    if (blockedByMagicType) {
        if (m_CurrentAction != SKL_COOLTIME) {
            actor->SendSystemMessage(SystemMessageId::CASTING_INTERRUPTED);
        }
        return false;
    }

    const SkillOperateType operateType = m_pSkillInfoA->m_OperateType;
    if (m_CurrentAction == SKL_HITCANCELTIME) {
        m_CurrentAction = SKL_HITTIME;
        if (!IsTargetAttackable(actor, target, m_pSkillInfoA->m_nEffectiveRange)) {
            cancelSkill();
            return false;
        }

        if (operateType == SKILL_CHARGE_INSTANT || operateType == SKILL_CHARGE_DURATION) {
            const FVector source = actor->GetPos();
            FVector destination{};
            double distance = 0.0;
            if (m_pSkillInfoA->m_TargetType == STGT_SELF) {
                distance = target->GetCollisionRadius() + 20.0;
                const FVector direction = actor->GetNormalizedDirVector(0);
                const double range = static_cast<double>(m_pSkillInfoA->m_nCastRange);
                destination =
                    FVector{source.x + direction.x * range, source.y + direction.y * range, source.z + direction.z * range};
            } else {
                switch (m_pSkillInfoA->m_eChargeDirection) {
                    case SFPT_BACK:
                        distance = -(target->GetCollisionRadius() + 20.0);
                        destination = target->GetPos();
                        break;
                    case SFPT_LEFT:
                        actor->GetSidestepPos(target->CastCreature(), destination, SFPT_LEFT);
                        break;
                    case SFPT_RIGHT:
                        actor->GetSidestepPos(target->CastCreature(), destination, SFPT_RIGHT);
                        break;
                    default:
                        distance = target->GetCollisionRadius() + 20.0;
                        destination = target->GetPos();
                        break;
                }
            }
            const bool floating = actor->m_data->IsFlying() || actor->m_AreaMod.m_bIsUnderWater;
            if (!CWorld::Inst().GetValidJumpPos(
                    source, destination, actor->m_ChargeData, distance, static_cast<int>(actor->GetInZoneID()), floating
                )) {
                cancelSkill();
                return false;
            }
            const FVector jumpPosition = actor->m_ChargeData.GetTo();
            actor->BroadcastFlyToLocation(
                static_cast<int>(jumpPosition.x), static_cast<int>(jumpPosition.y), static_cast<int>(jumpPosition.z),
                FTLT_CHARGE, m_nHitTime, 0, 0
            );
        } else if (operateType == SKILL_BACK_STEP_INSTANT || operateType == SKILL_BACK_STEP_DURATION ||
            operateType == SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK) {
            const FVector source = actor->GetPos();
            const FVector direction = actor->GetNormalizedDirVector(0xb4);
            const double range = static_cast<double>(m_pSkillInfoA->m_nCastRange);
            FVector destination{
                source.x + direction.x * range, source.y + direction.y * range, source.z + direction.z * range};
            const bool floating = actor->m_data->IsFlying() || actor->m_AreaMod.m_bIsUnderWater;
            if (!CWorld::Inst().GetValidJumpPos(
                    source, destination, actor->m_ChargeData, actor->GetCollisionRadius() + 20.0,
                    static_cast<int>(actor->GetInZoneID()), floating
                )) {
                cancelSkill();
                return false;
            }
            const FVector jumpPosition = actor->m_ChargeData.GetTo();
            actor->BroadcastFlyToLocation(
                static_cast<int>(jumpPosition.x), static_cast<int>(jumpPosition.y), static_cast<int>(jumpPosition.z),
                FTLT_BACKSTEP, m_nHitTime, 0, 0
            );
        }

        if (operateType == SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK) {
            CCreature* const targetCreature = target->CastCreature();
            const FVector actorPosition = actor->GetPos();
            const FVector targetPosition = target->GetPos();
            FVector direction{
                targetPosition.x - actorPosition.x, targetPosition.y - actorPosition.y, targetPosition.z - actorPosition.z};
            direction.NormalizeSquare();
            const double range = static_cast<double>(m_pSkillInfoA->m_nCastRange);
            FVector destination{targetPosition.x + direction.x * range, targetPosition.y + direction.y * range,
                targetPosition.z + direction.z * range};
            const SkillTargetTypeEnum targetType = m_pSkillInfoA->m_TargetType;
            if (targetCreature == nullptr || targetCreature->m_nDead != 0 ||
                targetCreature->m_SkillMod.m_nBlockMoveCount != 0 || !targetCreature->m_data->m_bCanMove ||
                !targetCreature->m_SkillMod.m_bCanAct || (targetType != STGT_ENEMY && targetType != STGT_ENEMY_ONLY) ||
                targetCreature->m_bzDefendingTraitPerfect[TRAIT_KNOCKBACK] || targetCreature->CheckBlockDebuff(actor)) {
                cancelSkill();
                return false;
            }
            const bool floating = targetCreature->m_data->IsFlying() || targetCreature->m_AreaMod.m_bIsUnderWater;
            if (!CWorld::Inst().GetValidJumpPos(
                    targetPosition, destination, targetCreature->m_TargetChargeData, 0.0,
                    static_cast<int>(targetCreature->GetInZoneID()), floating
                )) {
                cancelSkill();
                return false;
            }

            const FVector jumpPosition = targetCreature->m_TargetChargeData.GetTo();
            L2SkillFunc::CancelAllActionForMove(targetCreature);
            const FVector currentPosition = targetCreature->GetPos();
            FVector facing{
                jumpPosition.x - currentPosition.x, jumpPosition.y - currentPosition.y, jumpPosition.z - currentPosition.z};
            facing.NormalizeSquare();
            const double radians = std::atan2(facing.y, facing.x);
            const int targetYaw =
                static_cast<int>(static_cast<float>(static_cast<int>(radians * 65535.0 / 6.283185307179586)) * -1.0F);
            targetCreature->m_data->SetDir(FAngle{targetYaw});
            targetCreature->BroadcastFlyToLocation(
                static_cast<int>(jumpPosition.x), static_cast<int>(jumpPosition.y), static_cast<int>(jumpPosition.z),
                FTLT_BACKSTEP, m_nHitTime, 0, 0
            );
        }

        GatherSkillTarget(actor, target);
        if (m_nHitTime > 0x4f) {
            sendSkillDebug(u"[SkillDebug] HitTime [%d]sec.", m_nHitTime);
            AddTimer(m_nHitTime, m_nMyIndex, 0);
            return true;
        }
        // A zero/short hit interval executes the hit phase immediately.
    } else if (m_CurrentAction == SKL_CHANNELLING_CASTING) {
        m_nChannellingStartTime = static_cast<int>(runtime::GetTickCount());
        if (target->IsCreature() || target->IsFortressFlagpole()) {
            GatherSkillTarget(actor, target);
            if (!Activate(*actor, *target, CSkillInfo::EAT_START)) {
                return false;
            }
        }
        actor->GetController()->ActionBroadcastSkillLaunched(
            m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, m_pSkillInfoA->m_nSubLevel, static_cast<int>(target->m_nId),
            m_nSlotNo
        );
        m_CurrentAction = SKL_CHANNELLING_TICK;
        const int tickInterval = static_cast<int>(m_pSkillInfoA->m_dTickInterval);
        if (tickInterval < 0x50 && dwTime_count++ < 0x1e) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"dwTime[%d] skill_id[%d]",
            //     tickInterval,
            //     m_pSkillInfoA->m_nSkillId)
            // L2_LOG_ERROR(Log,
            //     L"dwTime[%d] skill_id[%d]",
            //     tickInterval,
            //     m_pSkillInfoA->m_nSkillId)
            L2_LOG_WARN(
                Log,
                "Channelled skill %d level %d has tick interval %d ms, below 80 ms; the timer still receives this interval.",
                m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, tickInterval
            );
        }
        AddTimer(tickInterval, m_nMyIndex, 0);
        return true;
    } else if (m_CurrentAction == SKL_CHANNELLING_TICK) {
        if ((!target->IsCreature() || target->IsAlive()) &&
            IsTargetAttackable(actor, target, m_pSkillInfoA->m_nEffectiveRange) &&
            // This unusual argument order is literal current behavior.
            BeReadyToActivate(
                actor, m_pSkillInfoA->m_nMpConsumeTick, m_pSkillInfoA->m_nPledgeNameValueConsume,
                m_pSkillInfoA->m_nPVPPointConsume, m_pSkillInfoA->m_nHpConsumeTick, true
            )) {
            if (operateType == SKILL_CHANNELLING_INSTANCE) {
                GatherSkillTarget(actor, target);
                if (!Activate(*actor, *target, CSkillInfo::EAT_TICK)) {
                    cancelSkill();
                    return false;
                }
                actor->GetController()->ActionBroadcastSkillLaunched(
                    m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, m_pSkillInfoA->m_nSubLevel,
                    static_cast<int>(target->m_nId), m_nSlotNo
                );
            }

            const double tickInterval = m_pSkillInfoA->m_dTickInterval;
            const int elapsed =
                runtime::SignedDifference32(runtime::GetTickCount(), static_cast<std::uint32_t>(m_nChannellingStartTime));
            if (static_cast<double>(m_nHitCancelTime) <= static_cast<double>(elapsed) + tickInterval) {
                m_CurrentAction = SKL_HITTIME;
                return OnTick();
            }
            AddTimer(static_cast<int>(std::max(tickInterval, 80.0)), m_nMyIndex, 0);
            return true;
        }
        cancelSkill();
        return false;
    } else if (m_CurrentAction != SKL_HITTIME) {
        return false;
    }

    const bool movementSkill = operateType == SKILL_CHARGE_INSTANT || operateType == SKILL_CHARGE_DURATION ||
        operateType == SKILL_BACK_STEP_INSTANT || operateType == SKILL_BACK_STEP_DURATION ||
        operateType == SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK;
    if (movementSkill) {
        if (actor->m_ChargeData.IsZero()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"pActor->m_ChargeData is Zero")
            // L2_LOG_ERROR(Log,
            //     L"pActor->m_ChargeData is Zero")
            L2_LOG_ERROR(
                Log, "Skill %d level %d stopped at hit time: caster object %u has empty charge data.",
                m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, static_cast<unsigned>(actor->Id())
            );
            return false;
        }
        const FVector destination = actor->m_ChargeData.GetTo();
        if (!CWorld::Inst().MoveTo(actor, destination, false)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"CWorld::Inst().MoveTo Failed in SKL_HITTIME. \uc774\ubbf8 damage \ub4e4\uc5b4\uac00 \ubc84\ub824\uc11c
            //     \ud760...")
            // L2_LOG_ERROR(Log,
            //     L"CWorld::Inst().MoveTo Failed in SKL_HITTIME. \uc774\ubbf8 damage \ub4e4\uc5b4\uac00 \ubc84\ub824\uc11c
            //     \ud760...")
            L2_LOG_ERROR(
                Log,
                "Skill %d level %d failed to move caster object %u to (%.17g, %.17g, %.17g) at hit time; location "
                "validation and skill processing continue.",
                m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, static_cast<unsigned>(actor->Id()), destination.x,
                destination.y, destination.z
            );
        }
        actor->ValidateLocation();
    }

    if (operateType == SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK) {
        CCreature* const targetCreature = target->CastCreature();
        if (targetCreature == nullptr || targetCreature->m_SkillMod.m_nBlockMoveCount != 0 ||
            !targetCreature->m_data->m_bCanMove || !targetCreature->m_SkillMod.m_bCanAct) {
            // Log.Add(LOG_ERROR, L"pTargetCreature is NULL")
            // Noise: A non-creature target or a target unable to move/act is a normal knockback exclusion. This branch
            // returns success without applying knockback. L2_LOG_ERROR(Log, L"pTargetCreature is NULL")
            return true;
        }
        const SkillTargetTypeEnum targetType = m_pSkillInfoA->m_TargetType;
        if ((targetType != STGT_ENEMY && targetType != STGT_ENEMY_ONLY) ||
            targetCreature->m_bzDefendingTraitPerfect[TRAIT_KNOCKBACK] || targetCreature->CheckBlockDebuff(actor)) {
            return true;
        }
        if (targetCreature->m_TargetChargeData.IsZero()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"pTargetCreature->m_TargetChargeData is Zero")
            // L2_LOG_ERROR(Log,
            //     L"pTargetCreature->m_TargetChargeData is Zero")
            L2_LOG_WARN(
                Log, "Skill %d level %d skipped knockback: target object %u has empty target-charge data.",
                m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, static_cast<unsigned>(targetCreature->Id())
            );
            return true;
        }
        const FVector destination = targetCreature->m_TargetChargeData.GetTo();
        if (!CWorld::Inst().MoveTo(targetCreature, destination, false)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"CWorld::Inst().MoveTo Failed in SKL_HITTIME. \uc774\ubbf8 damage \ub4e4\uc5b4\uac00 \ubc84\ub824\uc11c
            //     \ud760...")
            // L2_LOG_ERROR(Log,
            //     L"CWorld::Inst().MoveTo Failed in SKL_HITTIME. \uc774\ubbf8 damage \ub4e4\uc5b4\uac00 \ubc84\ub824\uc11c
            //     \ud760...")
            L2_LOG_ERROR(
                Log,
                "Skill %d level %d failed to move knockback target object %u to (%.17g, %.17g, %.17g) at hit time; location "
                "validation and skill processing continue.",
                m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, static_cast<unsigned>(targetCreature->Id()),
                destination.x, destination.y, destination.z
            );
        }
        targetCreature->ValidateLocation();
    }

    const bool channelDuration =
        operateType == SKILL_CHANNELLING_DURATION || operateType == SKILL_CHANNELLING_DURATION_LEVEL_UP;
    const bool activationSucceeded = channelDuration ||
        (BeReadyToActivate(
             actor, m_pSkillInfoA->m_nMpConsume2, m_pSkillInfoA->m_nHpConsume, m_pSkillInfoA->m_nPledgeNameValueConsume,
             m_pSkillInfoA->m_nPVPPointConsume, true
         ) &&
            Activate(*actor, *target, CSkillInfo::EAT_SUCCESS));
    actor->m_ChargeData.SetZero();
    actor->m_TargetChargeData.SetZero();
    if (!activationSucceeded) {
        return false;
    }

    m_CurrentAction = SKL_COOLTIME;
    if (m_nCoolTime < 0x50) {
        actor->m_data->m_nLastAttackTick = static_cast<int>(runtime::GetTickCount());
        return false;
    }
    sendSkillDebug(u"[SkillDebug] CoolTime [%d]sec.", m_nCoolTime);
    AddTimer(m_nCoolTime, m_nMyIndex, 0);
    return true;
}

bool CSkillAction2::OnTick() {
    // Current L2Server 0x0087d35c.
    L2_GUARD_FUNCTION();
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }

    bool result = OnTickHandle();
    if (m_pSkillInfoA->m_bIsDoubleCasting && !result) {
        OnEnd(true);
        result = actor->m_nLaunchedSkills.load(std::memory_order_seq_cst) != 0;
    }
    return result;
}

bool CSkillAction2::OnLostTarget(int, const bool cancelAction) {
    // Current L2Server 0x008736ec.
    L2_GUARD_FUNCTION();
    CCreature* const actor = m_spActor.GetCreature();
    if (actor != nullptr && actor->IsAlive() && m_spTarget.IsValid() && cancelAction) {
        CCreatureController* const controller = actor->GetController();
        controller->SetAsNothing(1);
        if (m_CurrentAction >= SKL_HITCANCELTIME && m_CurrentAction <= SKL_CHANNELLING_TICK) {
            controller->ActionBroadcastSkillCanceled(this);
            return false;
        }
    }
    return true;
}

bool CSkillAction2::OnDisappear() {
    // Current L2Server 0x00873810.
    L2_GUARD_FUNCTION();
    CCreature* const actor = m_spActor.GetCreature();
    if (actor != nullptr) {
        actor->GetController()->ActionBroadcastSkillCanceled(this);
    }
    return actor == nullptr;
}

bool CSkillAction2::OnDamaged(const CSmartID attackerId, const int damage) {
    // Current L2Server 0x00874818.
    L2_GUARD_FUNCTION();
    CCreature* const actor = m_spActor.GetCreature();
    CCreature* const attacker = attackerId.GetCreature();
    if (actor == nullptr || attacker == nullptr || m_pSkillInfoA == nullptr) {
        return false;
    }

    const int magicType = m_pSkillInfoA->m_nIsMagic;
    const bool canBeCanceled = !m_pSkillInfoA->m_IsSummonSkill && magicType >= 0 && magicType != 0 && magicType != 3 &&
        magicType != 6 && magicType != 7 && !(magicType >= 10 && magicType <= 13);
    if (!canBeCanceled || actor->m_data->m_dBaseHP <= 0.0) {
        return true;
    }

    const double damageRatio = static_cast<double>(damage) / actor->m_data->m_dBaseHP;
    if (damageRatio < 0.1) {
        return true;
    }

    int concentration = static_cast<int>(
        (static_cast<double>(actor->m_data->m_Stat.m_N[3]) / 20.0) * actor->m_SkillMod.m_Mods[0x39].m_Per +
        actor->m_SkillMod.m_Mods[0x39].m_Diff
    );
    concentration = std::clamp(concentration, 0, 0x5f);
    const int cancelProbability = std::clamp(std::min(static_cast<int>(damageRatio * 100.0), 0x5f) - concentration, 0, 0x5f);
    if (static_cast<int>(::genrand64_real2() * 100.0 + 1.0) > cancelProbability) {
        return true;
    }

    for (const bool avoid : m_pSkillInfoA->m_bAvoidCancelingSkill) {
        if (avoid) {
            return true;
        }
    }
    if (m_CurrentAction >= SKL_HITCANCELTIME && m_CurrentAction <= SKL_CHANNELLING_TICK) {
        actor->SendSystemMessage(SystemMessageId::CASTING_INTERRUPTED);
        actor->GetController()->ActionBroadcastSkillCanceled(this);
        return false;
    }
    return true;
}

void CSkillAction2::OnEnd(const bool success) {
    // Current L2Server 0x0087c814.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfoA == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CSkillAction2::OnEnd() m_pSkillInfoA is NULL")
        // L2_LOG_ERROR(Log,
        //     L"CSkillAction2::OnEnd() m_pSkillInfoA is NULL")
        L2_LOG_ERROR(
            Log,
            "Cannot finish skill action cleanup: its skill definition is unavailable; returning before base-action cleanup."
        );
        return;
    }

    CNewAction::OnEnd(success);
    CCreature* const actor = m_spActor.GetCreature();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (actor == nullptr) {
        return;
    }

    const auto decrementLaunchedSkills = [actor]() {
        const int remaining = actor->m_nLaunchedSkills.fetch_sub(1, std::memory_order_seq_cst) - 1;
        if (remaining < 0) {
            actor->m_nLaunchedSkills.store(0, std::memory_order_seq_cst);
        }
    };
    const SkillOperateType operateType = m_pSkillInfoA->m_OperateType;
    const bool countedLaunch = operateType != SKILL_TOGGLE && operateType != SKILL_AURA && operateType != SKILL_GROUP_TOGGLE;

    if (target == nullptr) {
        if (countedLaunch) {
            decrementLaunchedSkills();
        }
        return;
    }

    if (countedLaunch) {
        decrementLaunchedSkills();
    }
    if (!success) {
        actor->GetController()->ActionBroadcastSkillCanceled(this);
    }

    if (operateType >= SKILL_CHANNELLING_INSTANCE && operateType <= SKILL_CHANNELLING_DURATION_LEVEL_UP &&
        m_CurrentAction == SKL_CHANNELLING_TICK) {
        CCreature* const actionTarget = actor->m_spActionTarget.GetCreature();
        if (actionTarget != nullptr) {
            if (operateType == SKILL_CHANNELLING_INSTANCE) {
                if (!success) {
                    m_pSkillInfoA->PostActivateSkillEffects(actor, actionTarget, CSkillInfo::EAT_CANCEL);
                }
            } else {
                for (CSafePointer2<CCreature>& targetReference : actor->m_vChannelingSkillTarget.c) {
                    CSPointer<CCreature> channelTargetPointer = targetReference.FindObjectSP();
                    CCreature* const channelTarget = channelTargetPointer.get();
                    if (channelTarget != nullptr && channelTarget->m_nId != actionTarget->m_nId) {
                        channelTarget->DeleteChannelingAbnormalStatus(
                            actor, m_pSkillInfoA->m_pAttachedSkill, false, SPET_NONE
                        );
                    }
                }
                actionTarget->DetachedAbnormalStatusByChannelling(actor, m_pSkillInfoA);
            }
        }
    }

    m_CurrentAction = SKL_NOT_STARTED;
    actor->OnUseSkillFinished(
        static_cast<int>(target->m_nId), m_pSkillInfoA->m_nSkillId, m_pSkillInfoA->m_nLevel, m_pSkillInfoA->m_nSubLevel,
        success ? 1 : 0
    );
    actor->m_vChannelingSkillTarget.c.clear();

    if (actor->m_nLaunchedSkills.load(std::memory_order_seq_cst) == 0) {
        actor->m_vSkillTarget.c.clear();
        actor->m_vSkillTarget.d.clear();
        actor->m_vSkillStaticTarget.clear();
        actor->m_vSkillStaticTargetDistance.clear();
    }
    if (!actor->m_vSkillStaticTarget.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] actor dbid: %d, skill id: %d, target count: %d",
        //     L"newaction_skill.cpp",
        //     0x11e7,
        //     actor->GetDBID(),
        //     m_pSkillInfoA->m_nSkillId,
        //     static_cast<int>(actor->m_vSkillStaticTarget.size()))
        // Noise: Static target arrays are kept while other launched skills remain. Their nonempty size alone does not
        // identify an error. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] actor dbid: %d, skill id: %d, target count: %d",
        //     L"newaction_skill.cpp",
        //     0x11e7,
        //     actor->GetDBID(),
        //     m_pSkillInfoA->m_nSkillId,
        //     static_cast<int>(actor->m_vSkillStaticTarget.size()))
    }
    m_pSkillInfoA->SetAvoidSkillCanceling(*actor, false);
}

bool CSkillAction2::OnTargetOutOfSight(int) { return true; }

bool CSkillAction2::OnStopMove() { return true; }

bool CSkillAction2::CondForHiddenTarget(CWorldObject* target) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }

    if (target != nullptr && target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        if (!targetCreature->IsVisible()) {
            if (actor->m_nId == target->m_nId && m_pSkillInfoA->m_nEffectPoint >= 0) {
                return true;
            }

            if (actor->IsPC()) {
                if (actor->CastUser()->m_PledgeGameUser.IsWaiting() && targetCreature->IsSummonOrPet()) {
                    User* const actorUser = actor->CastUser();
                    CSummon* const summon = target->CastSummon();
                    if (actorUser != nullptr && summon != nullptr && summon->GetMaster() == actorUser) {
                        return true;
                    }
                }
            }
            return CNewAction::CondForHiddenTarget(target);
        }
    }
    return true;
}

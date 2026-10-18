#include "SerializedMessage.h"
#include "Creature.h"
#include "CreatureController.h"
#include "NpcServer.h"
#include "Shared/src/UserConst.h"
#include "Skill.h"
#include "User.h"

#include <cstring>

CSerializedLostTargetMessage::CSerializedLostTargetMessage(CCreature* creature, CObject* target) :
    m_pCreature(creature), m_pTarget(target) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(m_pCreature, ObjectRefType::SerializedMessage);
    if (m_pTarget != nullptr) {
        L2_ADD_REF(m_pTarget, ObjectRefType::SerializedMessage);
    }
}

void CSerializedLostTargetMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    const std::uint32_t targetId = m_pTarget->m_nId;
    if (m_pCreature->m_spTarget.Sid() == targetId) {
        m_pCreature->GetController()->LostTarget(static_cast<int>(targetId), false);
    }
    Clear();
}

void CSerializedLostTargetMessage::Clear() {
    L2_GUARD_FUNCTION();
    if (m_pTarget != nullptr) {
        L2_RELEASE(m_pTarget, ObjectRefType::SerializedMessage);
    }
    L2_RELEASE(m_pCreature, ObjectRefType::SerializedMessage);
}

CSerializedDamageMessage::CSerializedDamageMessage(CCreature* attacker, CCreature* victim, int damage) :
    m_pAttacker(attacker), m_pVictim(victim), m_nDamage(damage) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(m_pAttacker, ObjectRefType::SerializedMessage);
    L2_ADD_REF(m_pVictim, ObjectRefType::SerializedMessage);
}

void CSerializedDamageMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    m_pVictim->GetController()->OnDamagedMessage(m_pAttacker->GetSmartID(), m_nDamage);
    Clear();
}

void CSerializedDamageMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pVictim, ObjectRefType::SerializedMessage);
    L2_RELEASE(m_pAttacker, ObjectRefType::SerializedMessage);
}

CSerializedLookNeighborMessage::CSerializedLookNeighborMessage(
    CCreature* actor, bool reset, const FVector& previousLookPosition
) :
    m_pActor(actor), m_bReset(reset), m_PrevLookPos(previousLookPosition) {
    m_PrevLookPos.IsZero();
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
}

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CCreature, __async_arg_ResetNeighbor)

CCreature::__async_arg_LookNeighbor::__async_arg_LookNeighbor() = default;

CCreature::__async_arg_LookNeighbor::__async_arg_LookNeighbor(const FVector& previousPosition) : __a1(previousPosition) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CCreature, __async_arg_LookNeighbor)

void CSerializedLookNeighborMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();

    if (m_bReset) {
        CCreature::__async_arg_ResetNeighbor argument;
        m_pActor->m_LookNeighborSerializer.AddExecutable(
            m_pActor, [](CCreature* owner, CExecutionArgumentPad*) { owner->ResetNeighbor(); }, &argument, L"ResetNeighbor"
        );
    }

    CCreature::__async_arg_LookNeighbor argument(m_PrevLookPos);
    m_pActor->m_LookNeighborSerializer.AddExecutable(
        m_pActor, [](CCreature* owner, CExecutionArgumentPad* pad) { owner->__async_LookNeighbor(pad); }, &argument,
        L"LookNeighbor"
    );
    Clear();
}

void CSerializedLookNeighborMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
}

CSerializedChangeTargetMessage::CSerializedChangeTargetMessage(CCreature* actor) : m_pActor(actor) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
}

void CSerializedChangeTargetMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    if (m_pActor->IsCreature()) {
        m_pActor->ChangeTarget(nullptr, CTR_BY_SKILL);
    }
    Clear();
}

void CSerializedChangeTargetMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
}

CSerializedCreatureResetVisiblity::CSerializedCreatureResetVisiblity(CCreature* creature) : m_pCreature(creature) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(m_pCreature, ObjectRefType::SerializedMessage);
}

void CSerializedCreatureResetVisiblity::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    if ((m_pCreature->IsPC() || m_pCreature->IsNPC()) && m_pCreature->IsVisible()) {
        m_pCreature->SetVisible(false);
        m_pCreature->SetVisible(true);
    }
    Clear();
}

void CSerializedCreatureResetVisiblity::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pCreature, ObjectRefType::SerializedMessage);
}

CSerializedPumpEndMessage::CSerializedPumpEndMessage(
    CCreature* actor, CCreature* target, const CSkillInfo& skillInfo, SkillPumpEndType pumpEndType
) :
    m_pActor(actor), m_pTarget(target), m_SkillInfo(skillInfo), m_PumpEndType(pumpEndType) {
    if (m_pActor != nullptr) {
        L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
    }
    L2_ADD_REF(m_pTarget, ObjectRefType::SerializedMessage);
}

void CSerializedPumpEndMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    m_SkillInfo.PumpEnd(*m_pTarget, m_pActor, m_PumpEndType);

    if (m_pTarget->IsPC()) {
        m_pTarget->CastUser()->ValidateChanged(
            VB_BASEDATA | VB_BASEATTR | VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED
        );
    } else {
        m_pTarget->ValidateChanged(VB_BASEDATA | VB_BASEATTR | VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
    }
    Clear();
}

void CSerializedPumpEndMessage::Clear() {
    L2_GUARD_FUNCTION();
    if (m_pActor != nullptr) {
        L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
    }
    L2_RELEASE(m_pTarget, ObjectRefType::SerializedMessage);
}

CSerializedTriggerSkillMessage::CSerializedTriggerSkillMessage(
    CCreature* actor, CWorldObject* target, const CSkillInfo& skillInfo, SkillTargetTypeEnum targetType
) :
    m_pActor(actor), m_pTarget(target), m_SkillInfo(skillInfo), m_TargetType(targetType) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
    if (m_pTarget != nullptr) {
        L2_ADD_REF(m_pTarget, ObjectRefType::SerializedMessage);
    }
}

void CSerializedTriggerSkillMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    m_SkillInfo.ActivateTriggerSkill(*m_pActor, m_pTarget, m_TargetType);
    m_SkillInfo.CheckAndTurnOffSoulSpiritShot(*m_pActor);
    Clear();
}

void CSerializedTriggerSkillMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
    if (m_pTarget != nullptr) {
        L2_RELEASE(m_pTarget, ObjectRefType::SerializedMessage);
    }
}

CSerializedAuraSkillMessage::CSerializedAuraSkillMessage(
    CCreature* actor, const CSkillInfo& skillInfo, SkillTargetTypeEnum targetType
) :
    m_pActor(actor), m_SkillInfo(skillInfo), m_TargetType(targetType) {
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
}

void CSerializedAuraSkillMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    m_SkillInfo.ActivateAuraSkill(*m_pActor, m_TargetType);
    Clear();
}

void CSerializedAuraSkillMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
}

CSerializedActivateSkillEffectsMessage::CSerializedActivateSkillEffectsMessage(
    CCreature* actor, CWorldObject* target, const CSkillInfo& skillInfo, CSkillInfo::EffectActivateTiming timing
) :
    m_pActor(actor), m_pTarget(target), m_SkillInfo(skillInfo), m_Timing(timing) {
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
    L2_ADD_REF(m_pTarget, ObjectRefType::SerializedMessage);
}

void CSerializedActivateSkillEffectsMessage::OnHandle(CCreature*) {
    // Current L2Server 0x00a3fa40.
    L2_GUARD_FUNCTION();
    if (m_SkillInfo.CheckActivateSkillEffects(m_pActor, m_pTarget, m_Timing)) {
        m_SkillInfo.ActivateSkillEffects(m_pActor, m_pTarget, m_Timing, nullptr, 0.0);
    }
    Clear();
}

void CSerializedActivateSkillEffectsMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
    L2_RELEASE(m_pTarget, ObjectRefType::SerializedMessage);
}

CSerializedAcquirePassiveBonusSkill::CSerializedAcquirePassiveBonusSkill(
    CCreature* creature, const CSkillInfo& skillInfo, CSkillKey requireSkillKey, CSkillKey acquireSkillKey
) :
    m_pCreature(creature), m_SkillInfo(skillInfo), m_RequireSkillKey(requireSkillKey), m_AcquireSkillKey(acquireSkillKey) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(m_pCreature, ObjectRefType::SerializedMessage);
}

void CSerializedAcquirePassiveBonusSkill::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    if (m_pCreature->IsPC() && g_SkillDB.GetSkillInfo(m_AcquireSkillKey, m_pCreature) != nullptr &&
        m_pCreature->HasPassiveSkill(
            m_RequireSkillKey.m_nSkillId, m_RequireSkillKey.m_nLevel, m_RequireSkillKey.m_nSubLevel
        ) &&
        !m_pCreature->HaveSkill(m_AcquireSkillKey.m_nSkillId, m_AcquireSkillKey.m_nLevel, m_AcquireSkillKey.m_nSubLevel) &&
        m_pCreature->AcquireSkill(
            m_AcquireSkillKey.m_nSkillId, m_AcquireSkillKey.m_nLevel, m_AcquireSkillKey.m_nSubLevel, false, false
        )) {
        m_pCreature->ValidateChanged(VB_MAX);
        m_pCreature->SetMaxMagicLevel();
        m_pCreature->ParamChanged(std::int64_t{PCB_USER});
    }
    Clear();
}

void CSerializedAcquirePassiveBonusSkill::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pCreature, ObjectRefType::SerializedMessage);
}

CSerializedDeletePassiveBonusSkill::CSerializedDeletePassiveBonusSkill(
    CCreature* creature, const CSkillInfo& skillInfo, CSkillKey acquireSkillKey
) :
    m_pCreature(creature), m_SkillInfo(skillInfo), m_AcquireSkillKey(acquireSkillKey) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(m_pCreature, ObjectRefType::SerializedMessage);
}

void CSerializedDeletePassiveBonusSkill::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    if (m_pCreature->IsPC() && g_SkillDB.GetSkillInfo(m_AcquireSkillKey, m_pCreature) != nullptr &&
        m_pCreature->HaveSkill(m_AcquireSkillKey.m_nSkillId, m_AcquireSkillKey.m_nLevel, m_AcquireSkillKey.m_nSubLevel) &&
        m_pCreature->DeleteAcquireSkill(m_AcquireSkillKey.m_nSkillId)) {
        m_pCreature->ValidateChanged(VB_MAX);
        m_pCreature->SetMaxMagicLevel();
        m_pCreature->ParamChanged(std::int64_t{PCB_USER});
    }
    Clear();
}

void CSerializedDeletePassiveBonusSkill::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pCreature, ObjectRefType::SerializedMessage);
}

CSerializedOnInfectedSkillByMessage::CSerializedOnInfectedSkillByMessage(
    CCreature* actor,
    CCreature* target,
    const CSkillInfo& skillInfo,
    SkillPumpStartType pumpStartType,
    bool needSendVisualEffect
) :
    m_pActor(actor),
    m_pTarget(target),
    m_SkillInfo(skillInfo),
    m_PumpStartType(pumpStartType),
    m_bNeedSendVisualEffect(needSendVisualEffect) {
    L2_GUARD_FUNCTION();
    if (m_pActor != nullptr) {
        L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
    }
    L2_ADD_REF(m_pTarget, ObjectRefType::SerializedMessage);
}

void CSerializedOnInfectedSkillByMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    m_SkillInfo.PumpStart(*m_pTarget, m_pActor, m_PumpStartType);
    m_pTarget->OnInfectedSkillBy(m_SkillInfo, m_bNeedSendVisualEffect);
    Clear();
}

void CSerializedOnInfectedSkillByMessage::Clear() {
    L2_GUARD_FUNCTION();
    if (m_pActor != nullptr) {
        L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
    }
    L2_RELEASE(m_pTarget, ObjectRefType::SerializedMessage);
}

CSerializedDominionChannelSetMessage::CSerializedDominionChannelSetMessage(CCreature* actor, bool set) :
    m_pActor(actor), m_bSet(set) {
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
}

void CSerializedDominionChannelSetMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    if (m_pActor->IsPC()) {
        m_pActor->CastUser()->SendDominionChannelSet(m_bSet);
    }
    Clear();
}

void CSerializedDominionChannelSetMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
}

CSerializedRenameByDominionSiegeMessage::CSerializedRenameByDominionSiegeMessage(CCreature* actor, bool renamed) :
    m_pActor(actor), m_bRenamed(renamed) {
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
}

void CSerializedRenameByDominionSiegeMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    if (m_pActor->IsPC()) {
        if (User* const user = m_pActor->CastUser()) {
            user->RenameByDominionSiege(m_bRenamed, true);
        }
    }
    Clear();
}

void CSerializedRenameByDominionSiegeMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
}

CSerializedDominionEventMessage::CSerializedDominionEventMessage(
    CCreature* actor, int eventType, int npcId, int dominionId, bool start
) :
    m_pActor(actor), m_nEventType(eventType), m_nNpcID(npcId), m_nDominionID(dominionId), m_bStart(start) {
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
}

void CSerializedDominionEventMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();

    if (m_pActor->IsPC()) {
        User* const user = m_pActor->CastUser();
        if (user != nullptr) {
            g_npcserver.Send("cdddd", 0x86, m_nEventType, m_nNpcID, m_nDominionID, static_cast<int>(user->m_nId));
            user->ParamChanged(PCB_USER | PCB_CHAR_DOMINION);
            user->GatherRelationChanged();
            user->SendDominionStartEnd(m_bStart, nullptr);
        }
    }
    Clear();
}

void CSerializedDominionEventMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
}

CSerializedDominionScriptEventMessage::CSerializedDominionScriptEventMessage(
    CCreature* actor, int arg1, int arg2, int arg3
) :
    m_pActor(actor), m_nArg1(arg1), m_nArg2(arg2), m_nArg3(arg3) {
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
}

void CSerializedDominionScriptEventMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    if (m_pActor->IsPC()) {
        User* const user = m_pActor->CastUser();
        if (user != nullptr) {
            g_npcserver.Send("cdddd", 0x8b, user->m_nId, m_nArg1, m_nArg2, m_nArg3);
        }
    }
    Clear();
}

void CSerializedDominionScriptEventMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
}

CSerializedSendVoiceEventMessage::CSerializedSendVoiceEventMessage(CCreature* actor, const unsigned char* packetValue) :
    m_pActor(actor), packet(packetValue) {
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
}

void CSerializedSendVoiceEventMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    if (m_pActor->IsPC()) {
        User* const user = m_pActor->CastUser();
        if (user != nullptr) {
            user->SendVoiceEventPacket(packet);
        }
    }
    Clear();
}

void CSerializedSendVoiceEventMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
}

CSerializedSendUSMEventMessage::CSerializedSendUSMEventMessage(CCreature* actor, int eventId) :
    m_pActor(actor), m_nEventId(eventId) {
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
}

void CSerializedSendUSMEventMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    if (m_pActor->IsPC()) {
        User* const user = m_pActor->CastUser();
        if (user != nullptr) {
            user->SendUSMEventPacket(m_nEventId);
        }
    }
    Clear();
}

void CSerializedSendUSMEventMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
}

CSerializedSendUIEventMessage::CSerializedSendUIEventMessage(CCreature* actor, const unsigned char* packetValue) :
    m_pActor(actor), packet(packetValue) {
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
}

void CSerializedSendUIEventMessage::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    if (m_pActor->IsPC()) {
        User* const user = m_pActor->CastUser();
        if (user != nullptr) {
            user->SendUIEventPacket(packet);
        }
    }
    Clear();
}

void CSerializedSendUIEventMessage::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
}

CSerializedTakeOffItems::CSerializedTakeOffItems(CCreature* actor, bool isByLogin, bool isByContainerChange) :
    m_pActor(actor), m_isByLogin(isByLogin), m_isByContainerChange(isByContainerChange) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(m_pActor, ObjectRefType::SerializedMessage);
}

void CSerializedTakeOffItems::OnHandle(CCreature*) {
    L2_GUARD_FUNCTION();
    if (m_isByLogin && m_pActor->IsPC()) {
        User* const user = m_pActor->CastUser();
        if (user != nullptr) {
            user->TakeOffItems();
            user->ActivateEquipSkills();
        }
    } else if (m_isByContainerChange) {
        m_pActor->TakeOffItems();
    }
    Clear();
}

void CSerializedTakeOffItems::Clear() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pActor, ObjectRefType::SerializedMessage);
}

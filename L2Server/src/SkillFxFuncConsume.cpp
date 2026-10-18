#include "Creature.h"
#include "CreatureController.h"
#include "Object.h"
#include "Protocol/src/SystemMessageIds.h"
#include "SkillFxFunc.h"
#include "User.h"


namespace {

CCreature* AliveCreature(CObject* target) {
    if (!L2Common::IsAliveCreature2(target)) {
        return nullptr;
    }
    return target->CastCreature();
}

bool IsResting(const CCreature& creature) {
    return creature.m_data->m_StopMode == STP_SIT || creature.m_data->m_StopMode == STP_STORE;
}

} // namespace

bool CSkillEffect_c_mp::Consume(CObject* target, double amount) {
    // Current L2Server 0x00a84cf0.
    L2_GUARD_FUNCTION();
    CCreature* const creature = AliveCreature(target);
    if (creature == nullptr) {
        return false;
    }

    const bool consumed = creature->ConsumeMP(-(amount * dAmount));
    if (!consumed && target->IsPC()) {
        target->CastUser()->SendSystemMessage(SystemMessageId::SKILL_DEACTIVATED_NOT_ENOUGH_MP);
    }
    return consumed;
}

bool CSkillEffect_c_hp::Consume(CObject* target, double amount) {
    // Current L2Server 0x00a84e00.
    L2_GUARD_FUNCTION();
    CCreature* const creature = AliveCreature(target);
    if (creature == nullptr) {
        return false;
    }

    const bool consumed = creature->ConsumeHP(-(amount * dAmount));
    if (!consumed && target->IsPC()) {
        target->CastUser()->SendSystemMessage(SystemMessageId::SKILL_CANCELLED_NOT_ENOUGH_HP);
    }
    return consumed;
}

bool CSkillEffect_c_rest::Consume(CObject* target, double amount) {
    // Current L2Server 0x00a84f10.
    L2_GUARD_FUNCTION();
    CCreature* const creature = AliveCreature(target);
    if (creature == nullptr || !IsResting(*creature)) {
        return false;
    }

    if (creature->m_data->m_dHP < creature->m_data->m_dOrgHP) {
        const bool consumed = creature->ConsumeMP(-(amount * dAmount));
        if (!consumed) {
            creature->SendSystemMessage(SystemMessageId::SKILL_DEACTIVATED_NOT_ENOUGH_MP);
        }
        return consumed;
    }

    creature->SendSystemMessage(SystemMessageId::SKILL_ENDED_AFTER_FULL_HP_RECOVERY);
    return false;
}

bool CSkillEffect_c_chameleon_rest::Consume(CObject* target, double amount) {
    // Current L2Server 0x00a8503c.
    L2_GUARD_FUNCTION();
    CCreature* const creature = AliveCreature(target);
    if (creature == nullptr || !IsResting(*creature)) {
        return false;
    }

    const bool consumed = creature->ConsumeMP(-(amount * dAmount));
    if (!consumed) {
        creature->SendSystemMessage(SystemMessageId::SKILL_DEACTIVATED_NOT_ENOUGH_MP);
    }
    return consumed;
}

bool CSkillEffect_c_mp_by_level::Consume(CObject* target, double amount) {
    // Current L2Server 0x00a85148.
    L2_GUARD_FUNCTION();
    CCreature* const creature = AliveCreature(target);
    if (creature == nullptr) {
        return false;
    }

    const double level = creature->m_data->m_nLevel;
    const double levelCost = level * 0.0027 * level - level * 0.00002 * level * level + level * 0.0203 + 1.2171;
    const bool consumed = creature->ConsumeMP(-(levelCost * dAmount * amount));
    if (!consumed && target->IsPC()) {
        target->CastUser()->SendSystemMessage(SystemMessageId::SKILL_DEACTIVATED_NOT_ENOUGH_MP);
    }
    return consumed;
}

bool CSkillEffect_c_fake_death::Consume(CObject* target, double amount) {
    // Current L2Server 0x00a852a8. Preserve the unsigned tick comparison
    // used by GetTickCount, including its natural 32-bit wraparound.
    L2_GUARD_FUNCTION();
    CCreature* const creature = AliveCreature(target);
    if (creature == nullptr) {
        return false;
    }

    const std::uint32_t now = runtime::GetTickCount();
    const bool inInitialWindow = now <= creature->m_dwFakeDeathStartTime + 1500U;
    const bool inFakeDeath = creature->m_data->m_bIsFakeDeath && creature->m_data->m_StopMode == STP_SIT;
    if (!inInitialWindow && !inFakeDeath) {
        return false;
    }

    const bool consumed = creature->ConsumeMP(-(amount * dAmount));
    if (!consumed && target->IsPC()) {
        creature->SendSystemMessage(SystemMessageId::SKILL_DEACTIVATED_NOT_ENOUGH_MP);
    }
    return consumed;
}

bool CSkillEffect_c_fake_death::PumpEnd(CCreature&, CCreature* source, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00a854e0.
    L2_GUARD_FUNCTION();
    if (source != nullptr && source->IsPC()) {
        source->GetController()->Stand(0);
    }
    return true;
}

bool CSkillEffect_c_agathion_energy::Consume(CObject* target, double amount) {
    // Current L2Server 0x00a853d8.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(target)) {
        return false;
    }

    const bool consumed = target->CastUser()->ConsumeAgathionEnergy(-(amount * dAmount));
    if (!consumed) {
        target->CastUser()->SendSystemMessage(SystemMessageId::SKILL_CANCELLED_NOT_ENOUGH_ENERGY);
    }
    return consumed;
}

bool CSkillEffect_c_agathion_energy::IsConsumeAgathionEnergyEffect() const { return true; }

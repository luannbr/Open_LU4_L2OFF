#include "NewAction.h"

#include "Area.h"
#include "CConfig.h"
#include "CWorld.h"
#include "Chair.h"
#include "Creature.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "Default.h"
#include "Door.h"
#include "Doppelganger.h"
#include "Item.h"
#include "ItemOptionServer.h"
#include "NewActionHelper.h"
#include "NpcServer.h"
#include "Party.h"
#include "Pet.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"
#include "StaticObject.h"
#include "User.h"
#include "UserSocket.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

int g_ActionTable[18][18] = {
    {0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
    {1, 0, 2, 2, 2, 2, 1, 0, 2, 2, 2, 1, 2, 2, 1, 2, 0, 0},
    {1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0},
    {1, 1, 2, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 1, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 0, 2, 0, 0},
    {1, 2, 2, 1, 2, 2, 0, 0, 2, 2, 0, 1, 2, 2, 1, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 0, 2, 2, 0, 1, 2, 2, 1, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 0, 0, 2, 2, 1, 2, 2, 1, 2, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 0, 2, 0, 2, 1, 2, 2, 1, 2, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0},
    {1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
};

// Current NewAction.obj storage at 0x0173dd164. The counter is incremented
// for every sub-80 ms attack and limits the diagnostic to the first ten.
int hitcount = 0;

::std::map<int, FVector> CTreeLoc::m_treelocs;
::CYieldLock CTreeLoc::m_lock;

// Current MSVC xvector::clear/erase code in NewAction.obj creates this
// guarded assignment even though the full-range clear path does not execute
// it. libc++ implements clear without that helper, so keep the physical
// template publication in the same production owner.
template CSafePointer2<CStaticObject>& CSafePointer2<CStaticObject>::operator=(const CSafePointer2<CStaticObject>&);

bool CTreeLoc::_NearTree(FVector position, const int inZoneId) {
    // Current L2Server 0x008633d0.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_treelocs) {
        const FVector& treePosition = entry.second;
        const double dx = position.x - treePosition.x;
        const double dy = position.y - treePosition.y;
        const double dz = position.z - treePosition.z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) >= 1024.0) {
            continue;
        }

        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(entry.first));
        CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
        if (npc != nullptr && static_cast<int>(npc->GetInZoneID()) == inZoneId) {
            return true;
        }
    }
    return false;
}

bool CTreeLoc::CanUseTree(FVector position, const int inZoneId) {
    // Current L2Server 0x0086357c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    return !_NearTree(position, inZoneId);
}

void CTreeLoc::Init() {
    // Current L2Server 0x008639e4.
    L2_GUARD_FUNCTION();
    m_treelocs.clear();
}

void CTreeLoc::DeleteTree(const int objectId) {
    // Current L2Server 0x00863ed8.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    m_treelocs.erase(objectId);
}

bool CTreeLoc::UseTree(const int objectId, FVector position, const int inZoneId) {
    // Current L2Server 0x00863fec.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (m_treelocs.find(objectId) != m_treelocs.end() || _NearTree(position, inZoneId)) {
        return false;
    }
    m_treelocs.emplace(objectId, position);
    return true;
}

CNewAction::CNewAction() :
    m_ActionState(ActionState::End), m_pController(nullptr), m_eInterruptActionType(ACT_MAX) {}

CNewAction::~CNewAction() { m_pController = nullptr; }

bool CNewAction::CheckAttackValidityInOlympia(CCreature* creature, CObject* target) {
    // Current L2Server 0x0085e8d0.
    const auto canAttack = [](CObject* object) {
        User* user = nullptr;
        if (object->IsPC()) {
            user = object->CastUser();
        } else if (object->IsPet()) {
            CPet* const pet = object->CastPet();
            if (pet != nullptr) {
                user = pet->GetMaster();
            }
        }
        return user == nullptr || !user->IsOlympiadPlayer() || user->CanAttackInOlympiad();
    };
    return canAttack(creature) && canAttack(target);
}

bool CNewAction::CheckAttackValidityInPledgeGame(CCreature* creature, CObject* target) {
    // Current L2Server 0x0085ea64.
    const auto canAttack = [](CObject* object) {
        User* user = nullptr;
        if (object->IsPC()) {
            user = object->CastUser();
        } else if (object->IsPet()) {
            CPet* const pet = object->CastPet();
            if (pet != nullptr) {
                user = pet->GetMaster();
            }
        }
        return user == nullptr || !user->m_PledgeGameUser.IsPlayer() ||
            (user->m_PledgeGameUser.CanAttack() && user->m_PledgeGameUser.IsAlive());
    };
    return canAttack(creature) && canAttack(target);
}

void CNewAction::AddTimer(int delayMilliseconds, int actionNumber, int actionSequence) {
    L2_GUARD_FUNCTION();
    if (m_pController == nullptr) {
        return;
    }

    const std::uint64_t packedBits = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(actionSequence)) << 32U) |
        static_cast<std::uint32_t>(actionNumber);
    std::int64_t packedActionNumber = 0;
    static_assert(sizeof(packedActionNumber) == sizeof(packedBits));
    std::memcpy(&packedActionNumber, &packedBits, sizeof(packedActionNumber));
    m_pController->AddTimer2(delayMilliseconds, packedActionNumber);
}

bool CNewAction::CondForHiddenTarget(CWorldObject* target) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }

    if (target != nullptr && target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        if (!targetCreature->IsVisible() && !target->IsTrap()) {
            if (!actor->IsPC() || !target->IsPC()) {
                // actor->ActionFailed(0)
                actor->ActionFailed(AF_DEFAULT);
                return false;
            }

            User* const targetUser = target->CastUser();
            User* const actorUser = actor->CastUser();
            if (actorUser->m_nPartySid == 0 || targetUser->m_nPartySid != actorUser->m_nPartySid) {
                // actor->ActionFailed(0)
                actor->ActionFailed(AF_DEFAULT);
                return false;
            }
        }
    }
    return true;
}

void CNothingAction::Set(CSmartID actor) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
}

bool CNothingAction::OnStart() {
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Start;
    if (m_spActor.IsValid()) {
        CCreature* const actor = m_spActor.GetCreature();
        if (actor != nullptr) {
            // Current L2Server 0x0085ed44 clears CCreature+0x2cdc,
            // m_spActionTarget. Clearing m_spTarget (+0x2cc8) here loses
            // the player's selected target every time the controller enters
            // CNothingAction and desynchronizes it from the client.
            actor->m_spActionTarget = nullptr;
            if (actor->IsPC()) {
                return true;
            }
        }
        g_npcserver.Send("cd", 0x3b, static_cast<int>(m_spActor.Sid()));
    }
    return true;
}

bool CNothingAction::OnDamaged(CSmartID, int) {
    L2_GUARD_FUNCTION();

    // Current 0x0085eec5 calls CSmartID::GetCreature, not GetObjectW.
    return m_spActor.GetCreature() != nullptr;
}

void CStandAction::Set(CSmartID actor, int delayTime) {
    L2_GUARD_FUNCTION();

    if (delayTime < 1) {
        delayTime = 0x9f6;
    }
    m_spActor = actor;
    m_nDelayTime = delayTime;
}

bool CStandAction::OnStart() {
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Start;
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }

    actor->m_spActionTarget = nullptr;
    if (!actor->IsAlive() || actor->GetMorphID() != 0 || !actor->m_SkillMod.m_bCanAct) {
        return false;
    }

    bool chairCleared = false;
    bool standStarted = false;
    {
        L2_GUARD(*actor->m_lock);
        if (actor->m_data->m_StopMode != STP_STAND && actor->m_data->m_StopMode != STP_STORE) {
            standStarted = true;
            actor->m_data->m_StopMode = STP_STAND;
            actor->ValidateOrgSpeed();
            actor->ValidateHPRegen();

            if (actor->IsPC()) {
                CStaticObject* const sittingObject = actor->m_spSittingChair.GetStaticObject();
                CChair* const chair = sittingObject != nullptr ? sittingObject->CastChair() : nullptr;
                if (chair != nullptr) {
                    chairCleared = true;
                    chair->ClearBy(actor->GetSmartID());
                    actor->m_spSittingChair = nullptr;
                }
            }
        }
    }

    if (!standStarted) {
        return false;
    }

    AddTimer(m_nDelayTime, 0, 0);
    actor->BroadcastWaitType(0);
    actor->m_data->m_bIsFakeDeath = false;

    if (actor->IsPC()) {
        User* const user = actor->CastUser();
        std::uint32_t sittingTime = 0;
        if (user->m_dwSitTick != 0) {
            sittingTime = (runtime::GetTickCount() - user->m_dwSitTick) / 1000U;
        }

        CLogFormatter formatter{};
        formatter.WriteLog2_1(0x336, user, nullptr, sittingTime, 0, 0, 0, 0, 0, 0);

        if (chairCleared) {
            actor->ValidateLocation();
        }
    }
    return true;
}

void CSitAction::Set(CSmartID actor, int delayTime, CSmartID target, int sitType) {
    L2_GUARD_FUNCTION();

    if (delayTime < 1) {
        delayTime = 0x9f6;
    }
    m_spActor = actor;
    m_spTarget = target;
    m_SitType = sitType;
    m_nDelayTime = delayTime;
}

bool CSitAction::OnStart() {
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Start;
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }

    actor->m_spActionTarget = nullptr;
    if (!actor->IsAlive() || actor->GetMorphID() != 0 || !actor->m_SkillMod.m_bCanAct ||
        actor->m_data->m_nPrivateStoreType > 0) {
        return false;
    }

    CChair* occupiedChair = nullptr;
    bool sitStarted = false;
    {
        L2_GUARD(*actor->m_lock);
        if (actor->m_data->m_StopMode != STP_SIT && actor->m_data->m_StopMode != STP_STORE) {
            sitStarted = true;
            actor->m_data->m_StopMode = STP_SIT;
            actor->ValidateOrgSpeed();
            actor->ValidateHPRegen();

            if (actor->IsPC()) {
                CStaticObject* const sittingObject = m_spTarget.GetStaticObject();
                CChair* const chair = sittingObject != nullptr ? sittingObject->CastChair() : nullptr;
                if (chair != nullptr) {
                    if (chair->GetGroupType() == 1) {
                        if (!chair->m_lock->IsEntered()) {
                            L2_GUARD(*chair->m_lock);
                            CParty* const party = actor->GetParty();
                            if (party != nullptr && static_cast<std::int32_t>(party->m_nId) == chair->m_nOccupiedGroupId) {
                                const FVector actorPosition = actor->GetPos();
                                const FVector chairPosition = chair->GetPos();
                                const double deltaX = chairPosition.x - actorPosition.x;
                                const double deltaY = chairPosition.y - actorPosition.y;
                                const double sitRange = chair->GetSitRange();
                                if (deltaX * deltaX + deltaY * deltaY < sitRange * sitRange &&
                                    chair->Occupy(actor->GetSmartID())) {
                                    actor->m_spSittingChair = m_spTarget;
                                    occupiedChair = chair;
                                }
                            }
                        }
                    } else if (chair->GetGroupType() == 0) {
                        const FVector actorPosition = actor->GetPos();
                        const FVector chairPosition = chair->GetPos();
                        const double deltaX = chairPosition.x - actorPosition.x;
                        const double deltaY = chairPosition.y - actorPosition.y;
                        if (deltaX * deltaX + deltaY * deltaY < 10000.0 && chair->Occupy(actor->GetSmartID())) {
                            actor->m_spSittingChair = m_spTarget;
                            occupiedChair = chair;
                        }
                    }
                }
            }
        }
    }

    if (!sitStarted) {
        return false;
    }

    AddTimer(m_nDelayTime, 0, 0);
    actor->ValidateParameters(
        VB_WEAPONARMOR | VB_ITEMSKILL | VB_SKILLLIST | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED, false
    );
    actor->BroadcastWaitType(m_SitType);
    if (occupiedChair != nullptr) {
        occupiedChair->BroadcastSittingUser();
    }

    if (actor->IsPC()) {
        User* const user = actor->CastUser();
        user->m_dwSitTick = runtime::GetTickCount();

        CLogFormatter formatter{};
        formatter.WriteLog2_1(0x337, user, nullptr, 0, 0, 0, 0, 0, 0, 0);
    }
    return true;
}

void CAttackAction::Set(CSmartID actor, CSmartID target, bool surely) {
    L2_GUARD_FUNCTION();

    m_bIsSoulShot = false;
    m_spActor = actor;
    m_spTarget = target;
    m_bSurely = surely;
}

bool CAttackAction::CheckAttackable(
    double attackRange,
    double sourceCollisionRadius,
    double targetCollisionRadius,
    double squaredPlanarDistance,
    double sourceCollisionHeight,
    double targetCollisionHeight,
    int sourceZ,
    int targetZ,
    AttackType attackType
) {
    L2_GUARD_FUNCTION();

    return CAttackActionHelper::CheckAttackable(
        attackRange, sourceCollisionRadius, targetCollisionRadius, squaredPlanarDistance, sourceCollisionHeight,
        targetCollisionHeight, sourceZ, targetZ, attackType
    );
}

double CAttackAction::CalcCriticalHeightBonus(const FVector& source, const FVector& target) {
    L2_GUARD_FUNCTION();

    double heightDifference = static_cast<double>(static_cast<int>(source.z) - static_cast<int>(target.z));
    if (heightDifference < -25.0) {
        heightDifference = -25.0;
    } else if (heightDifference > 25.0) {
        heightDifference = 25.0;
    }
    return ((heightDifference * 4.0) / 5.0 + 10.0) / 100.0 + 1.0;
}

double CAttackAction::CalcCriticalLocBonus(
    CCreature* creature, const FVector& source, const FVector& target, const FAngle& direction
) {
    L2_GUARD_FUNCTION();

    if (creature == nullptr || !creature->IsCreature()) {
        return 1.0;
    }

    const double radians = std::atan2(target.y - source.y, target.x - source.x);
    const FAngle directionAt{
        runtime::Win64CvttDoubleToInt32(radians * 65535.0 / 6.283185307179586),
    };
    FAngle currentDirection = direction;
    FAngle::Enum angle = currentDirection.GetAngleDiff(directionAt);
    if (creature->m_SkillMod.m_eChangeAttackPosition != SFPT_MAX) {
        angle = ConvertSkillFxPositionTypeToAngleDir(creature->m_SkillMod.m_eChangeAttackPosition);
    }

    if (angle == FAngle::Side) {
        return creature->m_SkillMod.m_Mods[CCreatureSkillMod::CriticalPositionSide].m_Per * 1.1;
    }
    if (angle == FAngle::Back) {
        return creature->m_SkillMod.m_Mods[CCreatureSkillMod::CriticalPositionBack].m_Per * 1.3;
    }
    return creature->m_SkillMod.m_Mods[CCreatureSkillMod::CriticalPositionFront].m_Per;
}

void CAttackAction::SetPositionBonus(
    CCreature* creature,
    CObject* target,
    double& positionBonusRate,
    double& criticalDamagePositionBonusPer,
    double& criticalDamagePositionBonusDiff
) {
    // Current L2Server 0x008612b8.
    L2_GUARD_FUNCTION();

    if (!target->IsCreature()) {
        return;
    }

    CCreature* const targetCreature = target->CastCreature();
    SkillFxPositionType position = creature->GetDirectionAt(targetCreature);
    if (creature->m_SkillMod.m_eChangeAttackPosition != SFPT_MAX) {
        position = creature->m_SkillMod.m_eChangeAttackPosition;
    }

    if (position == SFPT_FRONT) {
        const CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_Mods[CCreatureSkillMod::CriticalDamagePositionFront];
        criticalDamagePositionBonusPer = mod.m_Per;
        criticalDamagePositionBonusDiff = mod.m_Diff;
        return;
    }
    if (position == SFPT_SIDE) {
        const CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_Mods[CCreatureSkillMod::CriticalDamagePositionSide];
        criticalDamagePositionBonusPer = mod.m_Per;
        criticalDamagePositionBonusDiff = mod.m_Diff;
        positionBonusRate = 0.05;
        return;
    }
    if (position == SFPT_BACK) {
        const CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_Mods[CCreatureSkillMod::CriticalDamagePositionBack];
        criticalDamagePositionBonusPer = mod.m_Per;
        criticalDamagePositionBonusDiff = mod.m_Diff;
        positionBonusRate = 0.2;
    }
}

double CAttackAction::CalcFatalBlowSuccessProb(double probability, CCreature* attacker, CCreature* target, double bonus) {
    // Current L2Server 0x00861f80. The shipped leaf has no guard envelope.
    const FVector attackerPosition = attacker->GetPos();
    const FVector targetPosition = target->GetPos();
    const FAngle targetDirection = target->GetDir();
    const double locationBonus = CalcCriticalLocBonus(attacker, attackerPosition, targetPosition, targetDirection);
    const double heightBonus = CalcCriticalHeightBonus(attackerPosition, targetPosition);
    return locationBonus * heightBonus * probability * ((bonus + 100.0) / 100.0) *
        attacker->m_SkillMod.m_Mods[CCreatureSkillMod::FatalBlow].m_Per * target->m_DefenceFatalBlowSuccessBonus.m_Per +
        target->m_DefenceFatalBlowSuccessBonus.m_Diff;
}

double CAttackAction::CalcHitLocBonus(
    const FVector& source, const FVector& target, const FAngle& direction, CCreature* creature
) {
    L2_GUARD_FUNCTION();

    const double radians = std::atan2(target.y - source.y, target.x - source.x);
    const FAngle directionAt{
        runtime::Win64CvttDoubleToInt32(radians * 65535.0 / 6.283185307179586),
    };
    FAngle currentDirection = direction;
    FAngle::Enum angle = currentDirection.GetAngleDiff(directionAt);
    if (creature != nullptr && creature->m_SkillMod.m_eChangeAttackPosition != SFPT_MAX) {
        angle = ConvertSkillFxPositionTypeToAngleDir(creature->m_SkillMod.m_eChangeAttackPosition);
    }

    if (angle == FAngle::Side) {
        return 1.2;
    }
    if (angle == FAngle::Back) {
        return 1.3;
    }
    return 1.0;
}

double CAttackAction::CalcHitHeightBonus(const FVector& source, const FVector& target) {
    L2_GUARD_FUNCTION();

    double heightDifference = static_cast<double>(static_cast<int>(target.z) - static_cast<int>(source.z));
    if (heightDifference <= -1100.0) {
        return 0.0;
    }
    if (heightDifference <= -300.0) {
        return (heightDifference * 0.125 + 37.5) / 100.0 + 1.0;
    }
    if (heightDifference <= 100.0) {
        return 1.0;
    }
    if (heightDifference > 500.0) {
        return 0.0;
    }
    return (heightDifference * -0.25 + 25.0) / 100.0 + 1.0;
}

double CAttackAction::GetAttackCriticalProb(
    CCreature* creature,
    int targetLevel,
    double criticalProbability,
    const FVector& source,
    const FVector& target,
    const FAngle& direction,
    bool applyLevelDifference
) {
    L2_GUARD_FUNCTION();

    double probability = CalcCriticalLocBonus(creature, source, target, direction) * criticalProbability *
        CalcCriticalHeightBonus(source, target);
    const int creatureLevel = creature->m_data->m_nLevel;
    if (applyLevelDifference && (creatureLevel > 77 || targetLevel > 77)) {
        probability +=
            std::sqrt(static_cast<double>(creatureLevel)) * static_cast<double>(creatureLevel - targetLevel) * 0.125;
    }

    if (probability < 3.0) {
        return 3.0;
    }
    if (probability > 97.0) {
        return 97.0;
    }
    return probability;
}

double CAttackAction::GetHitRatio(
    bool flag,
    double physicalHit,
    double physicalAvoid,
    const FVector& source,
    const FVector& target,
    const FAngle& direction,
    CCreature* creature
) {
    L2_GUARD_FUNCTION();

    const double divisor = flag ? 52.0 : 58.0;
    const double raw = -((physicalAvoid - divisor * 0.8006559766763849 - physicalHit) * (857375.0 / divisor));
    double nonnegative = 0.0;
    if (!(0.0 > raw)) {
        nonnegative = raw;
    }

    double ratio = CalcHitLocBonus(source, target, direction, creature) * (std::pow(nonnegative, 1.0 / 3.0) + 2.63) *
        CalcHitHeightBonus(source, target);
    if (ratio < 30.0) {
        ratio = 30.0;
    } else if (ratio > 98.0) {
        ratio = 98.0;
    }
    return ratio;
}

bool CAttackAction::CheckIsHit(
    bool flag,
    double physicalHit,
    double physicalAvoid,
    const FVector& source,
    const FVector& target,
    const FAngle& direction,
    CCreature* creature
) {
    L2_GUARD_FUNCTION();

    const double roll = ::genrand64_real1() * 100.0 + 0.0;
    return roll < GetHitRatio(flag, physicalHit, physicalAvoid, source, target, direction, creature);
}

double CAttackAction::GetHitTimeFactor(CCreature& creature) {
    L2_GUARD_FUNCTION();

    CSharedItemData* itemData = nullptr;
    const AttackType attackType = creature.GetAttackType(&itemData);
    double factor = 0x1.49f49f49f49f5p-1;
    if (!creature.IsPC()) {
        factor = creature.m_data->m_dHitTimeFactor;
    } else if (itemData != nullptr) {
        if ((static_cast<std::uint64_t>(itemData->m_Slot) & (std::uint64_t{1} << 14U)) != 0) {
            factor = 0x1.7777777777777p-1;
        }
        if (attackType == AT_BOW || attackType == AT_CROSSBOW || attackType == AT_TWOHANDCROSSBOW) {
            factor = 0x1.f49f49f49f49fp-1;
        }
        if (attackType == AT_DUAL || attackType == AT_DUALFIST || attackType == AT_DUALDAGGER ||
            attackType == AT_DUALBLUNT) {
            factor = 0x1.3333333333333p-1;
        }
    }
    return factor;
}

void CAttackAction::GetTimeData(CCreature& creature, TimeData& timeData) {
    L2_GUARD_FUNCTION();

    const double factor = GetHitTimeFactor(creature);
    const double fullTime = creature.m_data->m_dAttackTime * 1000.0;
    timeData.m_nHitTime = static_cast<unsigned int>(runtime::Win64CvttDoubleToInt64(fullTime * factor));
    if (fullTime <= static_cast<double>(timeData.m_nHitTime)) {
        timeData.m_nCoolTime = 0;
    } else {
        timeData.m_nCoolTime = static_cast<unsigned int>(runtime::Win64CvttDoubleToInt64(fullTime)) - timeData.m_nHitTime;
    }

    const AttackType attackType = creature.GetAttackType();
    if (attackType == AT_DUAL || attackType == AT_DUALFIST || attackType == AT_DUALDAGGER || attackType == AT_DUALBLUNT) {
        const double secondHitTime = creature.IsPC() ? static_cast<double>(timeData.m_nHitTime) * 0.6 / 1.1
                                                     : static_cast<double>(timeData.m_nHitTime) * 17.0 / 39.0;
        timeData.m_nSecondHitTime = static_cast<unsigned int>(runtime::Win64CvttDoubleToInt64(secondHitTime));
        timeData.m_nHitTime -= timeData.m_nSecondHitTime;
    } else {
        timeData.m_nSecondHitTime = 0;
    }
}

bool CAttackAction::IsTargetInSameInZone() {
    // Current L2Server 0x0085f41c.
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    return actor != nullptr && target != nullptr && actor->IsInInZone(target->GetInZoneID());
}

bool CAttackAction::CheckBowCondition(
    CCreature* const creature,
    const int consumeMP,
    const int reuseDelay,
    const bool surely,
    CSharedItemData* const weaponData
) {
    // Current L2Server 0x00860e70.
    L2_GUARD_FUNCTION();

    if (m_eAttackType != AT_BOW && m_eAttackType != AT_CROSSBOW && m_eAttackType != AT_TWOHANDCROSSBOW) {
        return true;
    }

    const std::uint32_t bowDelay = runtime::raw_atomic::LoadU32Relaxed(&creature->m_nBowDelay);
    const std::uint32_t currentTick = runtime::GetTickCount();
    if (bowDelay != 0 && currentTick < bowDelay) {
        const int remaining = static_cast<int>(bowDelay - runtime::GetTickCount());
        // creature->ActionFailed(0)
        creature->ActionFailed(AF_DEFAULT);
        m_pController->SetAsAttackWait(1, m_spTarget, remaining < 0x51 ? 0x50 : remaining, surely);
        return false;
    }

    if (creature->m_data->m_dMP < static_cast<double>(consumeMP)) {
        creature->SendSystemMessage(SystemMessageId::NOT_ENOUGH_MP);
        return false;
    }

    if (creature->IsPC() && creature->CastUser()->IsTransformedExceptModeChange() == nullptr) {
        if (m_eAttackType == AT_BOW && creature->m_inv.GetArrowCountByCrystal(m_eWeaponCrystal) == 0) {
            creature->m_data->m_nSlotIndex[ST_LHAND] = -1;
            creature->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARROWS);
            return false;
        }
        if ((m_eAttackType == AT_CROSSBOW || m_eAttackType == AT_TWOHANDCROSSBOW) &&
            creature->m_inv.GetBoltCountByCrystal(m_eWeaponCrystal) == 0) {
            creature->m_data->m_nSlotIndex[ST_LHAND] = -1;
            creature->SendSystemMessage(SystemMessageId::NOT_ENOUGH_BOLTS);
            return false;
        }
    }

    int realConsumeMP = 0;
    if (weaponData != nullptr && !creature->m_SkillMod.m_bNoMPConsumeByCheapshot) {
        CSharedItemConstant* const constant = weaponData->GetConstant();
        if (runtime::Win64CvttDoubleToInt32(::genrand64_real1() * 100.0) < constant->m_Reduced_mp_consume_prob) {
            realConsumeMP = constant->m_Reduced_mp_consume;
        } else {
            realConsumeMP = constant->m_nMpConsume;
        }
    }
    if (!creature->ConsumeMP(static_cast<double>(realConsumeMP))) {
        creature->SendSystemMessage(SystemMessageId::NOT_ENOUGH_MP);
        return false;
    }

    if (creature->IsPC() && creature->CastUser()->IsTransformedExceptModeChange() == nullptr) {
        if (m_eAttackType == AT_BOW && creature->m_inv.UseArrow(m_eWeaponCrystal, 1) == 0) {
            creature->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARROWS);
            return false;
        }
        if ((m_eAttackType == AT_CROSSBOW || m_eAttackType == AT_TWOHANDCROSSBOW) &&
            !creature->m_inv.UseBolt(m_eWeaponCrystal, 1)) {
            creature->SendSystemMessage(SystemMessageId::NOT_ENOUGH_BOLTS);
            return false;
        }
    }

    const int reuseComponent =
        runtime::Win64CvttDoubleToInt32(static_cast<double>(reuseDelay) / creature->m_data->m_dParam[6]);
    const int totalTime = static_cast<int>(m_Time.m_nHitTime + m_Time.m_nCoolTime) + reuseComponent;
    const std::uint32_t candidate = runtime::GetTickCount() + static_cast<std::uint32_t>(totalTime);
    if (runtime::raw_atomic::LoadU32Relaxed(&creature->m_nBowDelay) < candidate) {
        runtime::raw_atomic::ExchangeU32(&creature->m_nBowDelay, candidate);
    }

    if (creature->IsPC()) {
        User* const serverIdUser = creature->CastUser();
        CUserSocket* const socket = creature->CastUser()->GetSocket();
        socket->Send(
            "cdddd", 0x6b, serverIdUser->GetServerId(),
            // 1,
            GaugeType::BOW, totalTime, totalTime
        );
    }
    return true;
}

void CAttackAction::SetDefencePositionBonus(
    CCreature* creature, CObject* target, double& defencePositionBonusPer, double& defencePositionBonusDiff
) {
    // Current L2Server 0x0086141c.
    L2_GUARD_FUNCTION();

    if (!target->IsCreature()) {
        return;
    }

    SkillFxPositionType position = creature->GetDirectionAt(target->CastCreature());
    if (creature->m_SkillMod.m_eChangeAttackPosition != SFPT_MAX) {
        position = creature->m_SkillMod.m_eChangeAttackPosition;
    }

    if (position == SFPT_FRONT) {
        defencePositionBonusPer =
            target->CastCreature()->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceDamagePositionFront].m_Per;
        defencePositionBonusDiff =
            target->CastCreature()->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceDamagePositionFront].m_Diff;
    } else if (position == SFPT_SIDE) {
        defencePositionBonusPer =
            target->CastCreature()->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceDamagePositionSide].m_Per;
        defencePositionBonusDiff =
            target->CastCreature()->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceDamagePositionSide].m_Diff;
    } else if (position == SFPT_BACK) {
        defencePositionBonusPer =
            target->CastCreature()->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceDamagePositionBack].m_Per;
        defencePositionBonusDiff =
            target->CastCreature()->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceDamagePositionBack].m_Diff;
    }
}

void CAttackAction::GenerateAttackTargetData(
    CCreature* const creature,
    CWorldObject* const target,
    AttackTargetDataType& targetData,
    bool& soulShot,
    const double damageFactor,
    const bool surelyCritical
) {
    // Current L2Server 0x0086249c.
    L2_GUARD_FUNCTION();

    const AttackType attackType = creature->GetAttackType();
    targetData.nTargetId = static_cast<int>(target->Id());

    CCreature* targetCreature = nullptr;
    double physicalDefence = 0.0;
    double physicalAvoid = 0.0;
    double shieldDefenceRate = 0.0;
    double shieldDefence = 0.0;
    if (target->IsCreature()) {
        targetCreature = target->CastCreature();
        physicalDefence = targetCreature->GetPDefend() * creature->m_SkillMod.m_dArmorPenetrateFactor;
        physicalAvoid = targetCreature->GetPAvoid();
        shieldDefenceRate = targetCreature->GetShieldDefenseRate();
        shieldDefence = targetCreature->GetShieldDefense() * creature->m_SkillMod.m_dShieldPenetrateFactor;
    } else if (target->IsStaticObject()) {
        physicalDefence = target->CastStaticObject()->GetPDefend();
    } else {
        physicalDefence = 2100.0;
    }

    if ((targetCreature != nullptr && targetCreature->m_data->m_bAbsoluteDefence) || creature->m_data->m_bNoAttackDamage) {
        targetData.SetNoDamage(true);
        return;
    }

    if (targetCreature != nullptr) {
        const MoveModeEnum moveMode = targetCreature->GetMoveMode();
        const CCreatureSkillMod::Mod& avoidMoveMode =
            targetCreature->m_SkillMod.m_ModsMove[CCreatureSkillMod::PAvoidMoveMode][moveMode];
        physicalAvoid = physicalAvoid * avoidMoveMode.m_Per + avoidMoveMode.m_Diff;
    }

    const bool pvp = target->IsPvpTo(creature);
    const double physicalHit = creature->GetPHit();
    const FVector sourcePosition = creature->GetPos();
    const FVector targetPosition = target->GetPos();
    const FAngle targetDirection = target->GetDir();
    targetData.bIsHit =
        CheckIsHit(pvp, physicalHit, physicalAvoid, sourcePosition, targetPosition, targetDirection, creature);
    if (!targetData.bIsHit) {
        targetData.SetNoDamage(false);
        return;
    }

    double criticalRate = creature->CalcBasicCriticalRate(target);
    if (creature->IsSummon()) {
        CSummon* const summon = creature->CastSummon();
        User* const master = summon->GetMaster();
        if (master == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][CRASH] MasterDBID: %d",
            //     L"NewAction.cpp",
            //     0xa02,
            //     static_cast<int>(
            //         creature->CastSummon()->m_MasterUserDBID))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][CRASH] MasterDBID: %d",
            //     L"NewAction.cpp",
            //     0xa02,
            //     static_cast<int>(
            //         creature->CastSummon()->m_MasterUserDBID))
            L2_LOG_ERROR(
                Log, "Summon attack has no master for player DB ID %u; the following master access is unsafe.",
                static_cast<unsigned>(summon->m_MasterUserDBID)
            );
        }
        criticalRate += criticalRate * master->m_SkillMod.m_ModSummonBonus[CCreatureSkillMod::SBMN_CriticalProb] / 100.0;
    }
    criticalRate = std::max(0.0, std::min(criticalRate, 50.0));
    const double criticalProbability = GetAttackCriticalProb(
        creature, target->Level(), criticalRate, sourcePosition, targetPosition, targetDirection, target->IsCreature()
    );
    targetData.bIsCritical = surelyCritical || ::genrand64_real1() * 100.0 <= criticalProbability;
    const double criticalMultiplier = targetData.bIsCritical ? 2.0 : 1.0;

    targetData.eShieldDefense = AttackTargetDataType::SDT_NONE;
    targetData.nIsSoulShot = 0;
    if (targetCreature != nullptr && targetCreature->IsShieldDefend2(*creature, shieldDefenceRate)) {
        targetData.eShieldDefense = AttackTargetDataType::SDT_NORMAL;
    }
    if (targetData.eShieldDefense == AttackTargetDataType::SDT_NONE) {
        shieldDefence = 0.0;
    } else {
        if (targetCreature != nullptr && ::genrand64_real1() * 100.0 <= targetCreature->DexBonusF() * 2.0) {
            targetData.eShieldDefense = AttackTargetDataType::SDT_PERFECT;
            targetData.dDamage = 1.0;
        }
        if (target->IsPC()) {
            target->CastUser()->SendSystemMessage(SystemMessageId::SHIELD_DEFENSE_SUCCEEDED);
        }
    }
    if (targetData.eShieldDefense == AttackTargetDataType::SDT_PERFECT) {
        return;
    }

    const double weaponRandomDamage = creature->GetWeaponRandomDamage();
    double physicalAttack = creature->GetPAttack();
    targetData.nIsSoulShot = creature->m_data->m_nSoulShot;
    if (creature->IsDoppelganger()) {
        CDoppelganger* const doppelganger = static_cast<CDoppelganger*>(creature->CastNPC());
        if (doppelganger != nullptr) {
            User* const master = doppelganger->GetMaster();
            if (master != nullptr) {
                targetData.nIsSoulShot = master->m_data->m_nSoulShot;
            }
        }
    }
    soulShot = targetData.nIsSoulShot != 0;
    if (soulShot) {
        physicalAttack += physicalAttack * creature->m_data->m_dSoulShotAttackBonusPer;
        physicalAttack += creature->GetSoulShotExtraBonus() * physicalAttack;
    }

    double traitBonus = 1.0;
    if (targetCreature != nullptr) {
        traitBonus = creature->GetAttackTraitBonus(*targetCreature);
    } else if (target->IsDoor() &&
        (attackType == AT_BLUNT || attackType == AT_TWOHANDBLUNT || attackType == AT_STAFF ||
            attackType == AT_TWOHANDSTAFF || attackType == AT_POLE)) {
        traitBonus = 5.0;
    }

    double positionBonusRate = 0.0;
    double criticalDamagePositionBonusPer = 1.0;
    double criticalDamagePositionBonusDiff = 0.0;
    SetPositionBonus(creature, target, positionBonusRate, criticalDamagePositionBonusPer, criticalDamagePositionBonusDiff);

    const double basicAttack = physicalAttack * damageFactor * weaponRandomDamage;
    const double positionDamage = basicAttack * positionBonusRate;

    double defenceCriticalDamagePer = 1.0;
    double defenceCriticalDamageDiff = 0.0;
    double defencePositionBonusPer = 1.0;
    double defencePositionBonusDiff = 0.0;
    if (targetCreature != nullptr) {
        const CCreatureSkillMod::Mod& criticalDamageDefence =
            targetCreature->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceCriticalDamage];
        defenceCriticalDamagePer = criticalDamageDefence.m_Per;
        defenceCriticalDamageDiff = criticalDamageDefence.m_Diff;
        SetDefencePositionBonus(creature, target, defencePositionBonusPer, defencePositionBonusDiff);
    }

    double criticalDamagePer = 1.0;
    double criticalDamageDiff = 0.0;
    if (targetData.bIsCritical) {
        criticalDamagePer = creature->GetCriticalDamagePer() * defenceCriticalDamagePer * criticalDamagePositionBonusPer;
        criticalDamageDiff = creature->GetCriticalDamageDiff() + defenceCriticalDamageDiff + criticalDamagePositionBonusDiff;
        const int soulShotCount = creature->EquipedWeaponSoulShotCount();
        if (soulShotCount > 0) {
            criticalDamagePer *= 1.0 - static_cast<double>(soulShotCount) * 0.1;
        }
    }

    const bool rangedWeapon = attackType == AT_BOW || attackType == AT_CROSSBOW || attackType == AT_TWOHANDCROSSBOW;
    const double rangedDamage = rangedWeapon ? basicAttack : 0.0;
    const double attributeBonus = creature->GetAttributeBonus(target, nullptr);
    const double damageConstant = rangedWeapon ? 70.0 : 77.0;
    const double defence = std::max(shieldDefence + physicalDefence, 1.0);
    targetData.dDamage = (basicAttack * criticalMultiplier * criticalDamagePer * defencePositionBonusPer +
                             criticalDamageDiff + rangedDamage + positionDamage + defencePositionBonusDiff) *
        damageConstant * traitBonus * attributeBonus / defence;

    if (!pvp) {
        targetData.dDamage *= target->IsBoss() ? creature->m_SkillMod.m_dPvBossPhysicalAttackBonusPer
                                               : creature->m_SkillMod.m_dPvePhysicalAttackBonusPer;
    } else {
        targetData.dDamage *= creature->m_SkillMod.m_dPvpPhysicalAttackBonusPer;
        User* const targetUser = target->GetUserOrMaster();
        if (targetUser != nullptr) {
            targetData.dDamage *= targetUser->m_SkillMod.m_dPvpPhysicalAttackDefenceBonusPer;
        }
    }
}

bool CAttackAction::CheckAttackValidity(CCreature* const creature, CObject* const target) {
    // Current L2Server 0x00864528.
    L2_GUARD_FUNCTION();

    if (!creature->m_SkillMod.m_bCanAct || !creature->m_SkillMod.m_bCanAttack) {
        return false;
    }
    if (creature->m_ItemMod.m_bNoAttackWeapon) {
        creature->SendSystemMessage(SystemMessageId::WEAPON_CANNOT_ATTACK);
        return false;
    }
    if (creature == target) {
        creature->SendSystemMessage(SystemMessageId::CANNOT_USE_ON_SELF);
        return false;
    }
    if (creature->m_data->m_StopMode == STP_SIT || creature->m_data->m_StopMode == STP_STORE) {
        creature->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_SITTING);
        // creature->ActionFailed(0)
        creature->ActionFailed(AF_DEFAULT);
        return false;
    }

    if (creature->IsPCOrPCRelatedNPC() && target->IsPCOrPCRelatedNPC()) {
        CCreature* relatedCreature = nullptr;
        if (creature->IsSummonOrPet()) {
            relatedCreature = creature->GetUserOrMaster();
        }
        CCreature* const targetCreature = target->CastCreature();
        const int message = AreaBasedRule::Attackable(relatedCreature, creature, targetCreature);
        if (message != 0) {
            if (relatedCreature == nullptr) {
                creature->SendSystemMessage(message);
            } else {
                relatedCreature->SendSystemMessage(message);
            }
            return false;
        }
    }

    if (target->IsCreature() && creature->IsPCOrSummon()) {
        CCreature* const targetCreature = target->CastCreature();
        if (creature->GetPrivateID() != targetCreature->GetPrivateID()) {
            return false;
        }
    }
    if (!CheckAttackValidityInOlympia(creature, target) || !CheckAttackValidityInPledgeGame(creature, target)) {
        return false;
    }

    User* const creatureUser = creature->GetUserOrMaster();
    User* const targetUser = target->GetUserOrMaster();
    if (creatureUser != nullptr && targetUser != nullptr) {
        static_cast<void>(CCursedWeaponMgr::inst());
        const int creatureCursedWeapon = creatureUser->m_nCursedWeaponClassId;
        static_cast<void>(CCursedWeaponMgr::inst());
        const bool targetHasNoCursedWeapon = targetUser->m_nCursedWeaponClassId < 1;
        if (creatureCursedWeapon < 1) {
            if (!targetHasNoCursedWeapon) {
                return creatureUser->m_data->m_nLevel >= 20;
            }
        } else if (targetUser->m_data->m_nLevel < 20) {
            return false;
        }
        if (!targetHasNoCursedWeapon) {
            return creatureUser->m_data->m_nLevel >= 20;
        }
    }

    if (g_bIsNonPKServer && target->IsBattleCamp() && creature->IsSummonOrPet()) {
        CSummon* const summon = creature->CastSummon();
        User* const master = summon != nullptr ? summon->GetMaster() : nullptr;
        if (master != nullptr) {
            const std::uint32_t castleIndex = static_cast<std::uint32_t>(master->m_nCurCastleId - 1);
            int castleSiegeStatus = 0;
            if (castleIndex < 9U) {
                castleSiegeStatus = master->m_nCastleSiegeStatus[castleIndex];
            } else if (castleIndex == 9U) {
                // Current accepts index nine and reads the next
                // dword after the nine-element castle-status array.
                castleSiegeStatus = master->m_nFortressUnderOffenceSiegeId;
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] castle_index is wrong!! [%d]",
                //     L"c:\\work\\lineage2build\\server\\main\\l2server\\User.h",
                //     0xd3d,
                //     castleIndex)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] castle_index is wrong!! [%d]",
                //     L"c:\\work\\lineage2build\\server\\main\\l2server\\User.h",
                //     0xd3d,
                //     castleIndex)
                L2_LOG_ERROR(
                    Log, "Invalid zero-based castle index %u during summon attack checks; siege status remains zero.",
                    static_cast<unsigned>(castleIndex)
                );
                runtime::process::ReportCallStack(2, false);
            }
            if (castleSiegeStatus == 0 && (master->m_AreaMod.m_bIsInPvPZone || !master->m_AreaMod.m_bIsInBattleField)) {
                return false;
            }
        }
    }
    return true;
}

bool CAttackAction::OnStart() {
    // Current L2Server 0x008648c4.
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Start;
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__ATTACK_ONSTART);

    CCreature* const actor = m_spActor.GetCreature();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (actor == nullptr || target == nullptr || !actor->IsAlive()) {
        return false;
    }
    if (!target->IsAlive()) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (actor->GetMorphID() != 0 || !IsTargetInSameInZone()) {
        return false;
    }
    if (target != actor && !target->IsTargetable()) {
        actor->ChangeTarget(nullptr, CTR_DEFAULT);
        return false;
    }

    if (actor->IsPC()) {
        User* const user = actor->CastUser();
        if (!user->CanNormalAttack()) {
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
            return false;
        }
    }
    if (actor->IsNPC() && !actor->IsSummonOrPet() && !actor->IsInRealWorld() &&
        !CWorld::Inst().GetGeo()->CanSee(actor->CastWorldObject(), target->CastWorldObject(), true)) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return false;
    }

    actor->m_spActionTarget = m_spTarget;
    if (!CheckAttackValidity(actor, target)) {
        if (m_bSurely) {
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
        }
        return false;
    }
    if (!CondForHiddenTarget(target)) {
        actor->ChangeTarget(nullptr, CTR_DEFAULT);
        return false;
    }

    const FVector actorPosition = actor->GetPos();
    FVector targetPosition = target->IsCreature() ? target->CastCreature()->GetDetailLocation() : target->GetPos();
    if (target->IsDoor()) {
        targetPosition.z = actorPosition.z;
    }

    const double targetRadians = std::atan2(targetPosition.y - actorPosition.y, targetPosition.x - actorPosition.x);
    actor->m_data->SetDir(FAngle{runtime::Win64CvttDoubleToInt32(targetRadians * 65535.0 / 6.283185307179586)});
    const FAngle targetDirection = target->GetDir();

    const double attackRange = static_cast<double>(actor->m_data->m_nAttackRange);
    const double actorCollisionRadius = actor->m_data->m_dCollisionRadius;
    const double actorCollisionHeight = actor->m_data->m_dCollisionHeight;
    const double dx = actorPosition.x - targetPosition.x;
    const double dy = actorPosition.y - targetPosition.y;
    const double squaredPlanarDistance = dx * dx + dy * dy;
    m_eAttackType = actor->GetAttackType();

    double targetCollisionRadius = 100.0;
    double targetCollisionHeight = 100.0;
    if (target->IsCreature()) {
        targetCollisionRadius = target->CastCreature()->m_data->m_dCollisionRadius;
        targetCollisionHeight = target->CastCreature()->m_data->m_dCollisionHeight;
    }

    const auto isAttackable = [&]() {
        return CheckAttackable(
            attackRange, actorCollisionRadius, targetCollisionRadius, squaredPlanarDistance, actorCollisionHeight,
            targetCollisionHeight, runtime::Win64CvttDoubleToInt32(actorPosition.z),
            runtime::Win64CvttDoubleToInt32(targetPosition.z), m_eAttackType
        );
    };
    if (!(m_bSurely && actor->IsNPC()) && !isAttackable()) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);

        const FVector ordinaryTargetPosition = target->GetPos();
        const FVector ordinaryActorPosition = actor->GetPos();
        const double ordinaryDx = ordinaryActorPosition.x - ordinaryTargetPosition.x;
        const double ordinaryDy = ordinaryActorPosition.y - ordinaryTargetPosition.y;
        CCreatureController* const controller = actor->GetController();
        if (ordinaryDx * ordinaryDx + ordinaryDy * ordinaryDy <= 4194304.0) {
            if (!actor->IsPC()) {
                controller->SetAsMoveToTarget(1, m_spTarget, false, 0, 0, 0, 0, 0);
            } else {
                const CNPC* const targetNpc = target->IsNPC() ? target->CastNPC() : nullptr;
                if (targetNpc == nullptr || targetNpc->m_data->m_nClassID != 0x54) {
                    if (actor->IsInAirShip() && target->IsInAirShip()) {
                        if (!m_bSurely && actor->GetMyAirShipID() == target->GetMyAirShipID()) {
                            controller->SetAsMoveToTargetRelative(
                                1, m_spTarget, false, 0, 0, 0, 0, 0, CSmartID(actor->GetMyAirShipID())
                            );
                        }
                    } else {
                        actor->CastUser()->GetSocket()->Send(
                            "cdddddc", 0x79, target->Id(), runtime::Win64CvttDoubleToInt32(targetPosition.x),
                            runtime::Win64CvttDoubleToInt32(targetPosition.y),
                            runtime::Win64CvttDoubleToInt32(targetPosition.z),
                            static_cast<unsigned int>(static_cast<std::uint16_t>(targetDirection.Yaw)), -1
                        );
                        if (!m_bSurely) {
                            controller->SetAsMoveToTarget(1, m_spTarget, false, 0, 0, 0, 0, 0);
                        }
                    }
                }
            }
        } else {
            controller->SetAsNothing(1);
        }
        return false;
    }

    CSharedItemData* weapon = nullptr;
    const AttackType itemAttackType = actor->GetAttackType(&weapon);
    bool areaWeapon = false;
    m_eWeaponCrystal = CRT_NONE;
    double dualFactor = 0.5;
    if (!actor->IsPC() || weapon == nullptr) {
        m_WeaponClassID = 0;
    } else {
        m_WeaponClassID = weapon->m_nClassID;
        CSharedItemConstant* const constant = weapon->GetConstant();
        m_eWeaponCrystal = constant->GetCrystalType();

        int slot = ST_UNDERWEAR;
        while (slot < ST_MAX && (ST2SBT[slot] & weapon->m_Slot) == 0) {
            ++slot;
        }
        m_eSlotType = static_cast<SlotType>(slot);

        areaWeapon = m_eAttackType == AT_POLE || m_eAttackType == AT_TWOHANDSWORD || m_eAttackType == AT_TWOHANDBLUNT ||
            m_eAttackType == AT_TWOHANDSTAFF || (m_eAttackType == AT_ANCIENTSWORD && m_eSlotType == ST_RLHAND);
        if (itemAttackType == AT_DUAL || itemAttackType == AT_DUALFIST || itemAttackType == AT_DUALDAGGER ||
            itemAttackType == AT_DUALBLUNT) {
            dualFactor = static_cast<double>(constant->m_cDualRate) / 100.0;
        }
        if (areaWeapon) {
            m_DamageRange = constant->m_DamageRange;
            const double extraRange = attackRange - static_cast<double>(constant->m_nAttackRange);
            if (extraRange > 0.0) {
                const std::uint32_t wrappedDistance = static_cast<std::uint32_t>(m_DamageRange.distance) +
                    static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(extraRange));
                std::memcpy(&m_DamageRange.distance, &wrappedDistance, sizeof(wrappedDistance));
            }
        }
    }

    GetTimeData(*actor, m_Time);
    const bool rangedAttack = m_eAttackType == AT_BOW || m_eAttackType == AT_CROSSBOW || m_eAttackType == AT_TWOHANDCROSSBOW;
    if (rangedAttack) {
        int consumeMP = 0;
        int reuseDelay = actor->m_data->m_nBaseReuseDelay;
        const bool rangedItem =
            itemAttackType == AT_BOW || itemAttackType == AT_CROSSBOW || itemAttackType == AT_TWOHANDCROSSBOW;
        if (weapon != nullptr && rangedItem) {
            CSharedItemConstant* const constant = weapon->GetConstant();
            if (!actor->m_SkillMod.m_bNoMPConsumeByCheapshot) {
                consumeMP = constant->m_nMpConsume;
            }
            reuseDelay = constant->m_nReuseDelay;
        }

        User* const user = actor->IsPC() ? actor->CastUser() : nullptr;
        if (user != nullptr) {
            if (!CWorld::Inst().GetGeo()->CanSee(user, target, target->IsCreature())) {
                user->SendSystemMessage(SystemMessageId::TARGET_NOT_VISIBLE);
                // actor->ActionFailed(0)
                actor->ActionFailed(AF_DEFAULT);
                return false;
            }
        } else if (!CWorld::Inst().GetGeo()->CanSee(actor, target, actor->IsSummonOrPet() ? target->IsCreature() : false)) {
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
            return false;
        }
        if (!CheckBowCondition(actor, consumeMP, reuseDelay, m_bSurely, weapon)) {
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
            return false;
        }
    }

    m_nMultiCount = 0;
    if (!target->IsMakeAttackerGuilty(actor, false)) {
        if (isAttackable()) {
            m_pController->SetAsAttack(1, m_spTarget, m_bSurely);
        } else if (actor->IsSummonOrPet()) {
            actor->GetController()->SetAsMoveToTarget(1, m_spTarget, false, 0, 0, 0, 0, 0);
        } else {
            actor->GetController()->SetAsNothing(1);
        }
    } else {
        actor->GetController()->SetAsNothing(1);
    }

    m_bIsSoulShot = false;
    const bool dualAttack = m_eAttackType == AT_DUAL || m_eAttackType == AT_DUALFIST || m_eAttackType == AT_DUALDAGGER ||
        m_eAttackType == AT_DUALBLUNT;
    if (!areaWeapon) {
        if (dualAttack) {
            std::memset(m_MultiTargetData, 0, sizeof(m_MultiTargetData));
            GenerateAttackTargetData(actor, target, m_TargetData, m_bIsSoulShot, dualFactor, false);
            GenerateAttackTargetData(actor, target, m_MultiTargetData[0], m_bIsSoulShot, 1.0 - dualFactor, false);
            m_nMultiCount = 1;
        } else {
            GenerateAttackTargetData(actor, target, m_TargetData, m_bIsSoulShot, 1.0, false);
        }
    } else {
        GenerateAttackTargetData(actor, target, m_TargetData, m_bIsSoulShot, 1.0, false);
        const int targetCount =
            runtime::Win64CvttDoubleToInt32(actor->m_SkillMod.m_Mods[CCreatureSkillMod::HitNumber].m_Diff);
        if (m_eAttackType != AT_POLE || !actor->m_SkillMod.m_bIsSingleTarget) {
            actor->m_vSkillTarget.c.clear();
            actor->m_vSkillTarget.d.clear();
            actor->m_vSkillStaticTarget.clear();
            actor->m_vSkillStaticTargetDistance.clear();
            if (targetCount > 0) {
                int rangeTargetType = RTT_WHITE;
                if (actor->IsPCOrSummon() && !actor->m_AreaMod.m_bIsInBattleField) {
                    if (!target->IsPCOrSummon()) {
                        rangeTargetType = RTT_CHAO;
                    } else {
                        if (target->CastCreature()->m_data->m_nKarma < 0) {
                            rangeTargetType = RTT_CHAO;
                        } else if (target->CastCreature()->IsGuilty()) {
                            rangeTargetType = RTT_VIOLET;
                        }
                    }
                }
                actor->GatherNeighborCreatureMelee(
                    rangeTargetType, targetCount, false, actor->m_vSkillTarget, m_DamageRange, -(m_DamageRange.distance / 2),
                    m_DamageRange.distance / 2, SAFOT_NOT_FRIEND, actor, actor, targetPosition,
                    runtime::Win64CvttDoubleToInt32((static_cast<double>(m_DamageRange.angle) / 360.0) * 65535.0)
                );
                ConvertSkillTargetToMultiTarget(actor);
            }
        }
    }

    m_pController->ActionBroadcastAttack(m_TargetData, m_nMultiCount, m_MultiTargetData, *target);
    m_CurrentAction = ATK_HITTIME;
    AddTimer(static_cast<int>(m_Time.m_nHitTime), 0, 0);

    if (m_Time.m_nHitTime < 0x50U) {
        const bool shouldLog = hitcount < 10;
        const std::uint32_t wrappedHitcount = static_cast<std::uint32_t>(hitcount) + 1U;
        std::memcpy(&hitcount, &wrappedHitcount, sizeof(wrappedHitcount));
        if (shouldLog) {
            const std::wstring nameStorage = actor->IsPC() || actor->IsNPC() ? Utf16ToWide(actor->GetName()) : L"";
            const wchar_t* const name = nameStorage.c_str();
            const double hitTimeFactor = GetHitTimeFactor(*actor);
            std::uint64_t hitTimeFactorBits = 0;
            static_assert(sizeof(hitTimeFactorBits) == sizeof(hitTimeFactor));
            std::memcpy(&hitTimeFactorBits, &hitTimeFactor, sizeof(hitTimeFactorBits));
            // Log.Add(
            //     LOG_ERROR,
            //     L"m_nHitTime[%d] m_nSecondHitTime[%d] "
            //     L"dHitTimeFactor[%d] [%s]",
            //     static_cast<int>(m_Time.m_nHitTime),
            //     static_cast<int>(m_Time.m_nSecondHitTime),
            //     static_cast<int>(
            //         static_cast<std::uint32_t>(
            //             hitTimeFactorBits)),
            //     name)
            // L2_LOG_ERROR(Log,
            //     L"m_nHitTime[%d] m_nSecondHitTime[%d] "
            //     L"dHitTimeFactor[%d] [%s]",
            //     static_cast<int>(m_Time.m_nHitTime),
            //     static_cast<int>(m_Time.m_nSecondHitTime),
            //     static_cast<int>(
            //         static_cast<std::uint32_t>(
            //             hitTimeFactorBits)),
            //     name)
            L2_LOG_ERROR(
                Log,
                "Unusually short attack hit time for '%s': first %u ms, second %u ms, factor %.17g (raw bits 0x%016llx); "
                "attack continues.",
                WideToUtf8(name).c_str(), static_cast<unsigned>(m_Time.m_nHitTime),
                static_cast<unsigned>(m_Time.m_nSecondHitTime), hitTimeFactor,
                static_cast<unsigned long long>(hitTimeFactorBits)
            );
        }
    }
    actor->SetInCombatMode(true);
    actor->DispelBySlot(ABNS_INVINCIBILITY, nullptr, -1, nullptr, SPET_ATTACK_OR_SKILL, false);
    return true;
}

void CAttackAction::ConvertSkillTargetToMultiTarget(CCreature* creature) {
    // Current L2Server 0x00862d5c.
    L2_GUARD_FUNCTION();

    std::memset(m_MultiTargetData, 0, sizeof(m_MultiTargetData));
    for (CSafePointer2<CCreature>& targetReference : creature->m_vSkillTarget.c) {
        if (m_nMultiCount >= 20) {
            return;
        }

        CSPointer<CCreature> targetPointer = targetReference.FindObjectSP();
        CCreature* const targetCreature = targetPointer.get();
        if (targetCreature == nullptr || !targetCreature->IsCreature() || !targetCreature->m_data->m_bIsActive ||
            !targetCreature->IsAlive() || targetCreature->GetSmartID().Sid() == m_spTarget.Sid()) {
            continue;
        }

        GenerateAttackTargetData(
            creature, targetCreature->CastCreature(), m_MultiTargetData[m_nMultiCount], m_bIsSoulShot, 1.0, false
        );
        ++m_nMultiCount;
    }
}

void CAttackAction::CheckItemSkill(int targetId, bool critical) {
    // Current L2Server 0x00862fcc.
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    CSharedItemData* const weapon = actor != nullptr ? actor->GetEquipedWeapon() : nullptr;
    if (weapon == nullptr) {
        return;
    }

    CItem* const item = CItemFactory::Inst().GetObjectW(weapon->index);
    CCreature* const target = CSmartID(static_cast<std::uint32_t>(targetId)).GetCreature();
    CSharedItemConstant* const constant = weapon->GetConstant();
    if (item == nullptr || target == nullptr || constant == nullptr || constant->GetCrystalType() > actor->GetMaxCrystal()) {
        return;
    }

    const CSkillInfo* const attackSkill = static_cast<const CSkillInfo*>(constant->m_pAttackSkill);
    if (attackSkill != nullptr && runtime::Win64CvttDoubleToInt32(::genrand64_real1() * 100.0) < 10) {
        attackSkill->ActivateSkill(actor, target, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        attackSkill->CheckAndTurnOffSoulSpiritShot(*actor);
    }

    const CSkillInfo* const criticalSkill = static_cast<const CSkillInfo*>(constant->m_pCriticalSkill);
    if (critical && criticalSkill != nullptr) {
        criticalSkill->ActivateSkill(actor, target, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        criticalSkill->CheckAndTurnOffSoulSpiritShot(*actor);
    }

    ItemOptionHandler::ActivateSkillInfo info(item, actor, target);
    ItemOptionHandler::ActivateAttackSkills(info);
    if (critical) {
        ItemOptionHandler::ActivateCriticalSkills(info);
    }
}

void CAttackAction::OnHitTarget(int targetId, bool critical, bool hit) {
    // Current L2Server 0x00863314.
    L2_GUARD_FUNCTION();
    if (hit) {
        CheckItemSkill(targetId, critical);
    }
}

void CAttackAction::OnHitTimeNotDual() {
    // Current L2Server 0x008636a4.
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    actor->GetController()->DoAttack(
        m_TargetData, m_nMultiCount, m_MultiTargetData, m_WeaponClassID, m_eAttackType, m_eSlotType
    );
    OnHitTarget(m_TargetData.nTargetId, m_TargetData.bIsCritical, m_TargetData.bIsHit);
    for (int index = 0; index < m_nMultiCount; ++index) {
        const AttackTargetDataType& target = m_MultiTargetData[index];
        OnHitTarget(target.nTargetId, target.bIsCritical, target.bIsHit);
    }
    m_CurrentAction = ATK_COOLTIME;
}

void CAttackAction::OnFirstHitTimeForDual() {
    // Current L2Server 0x008637d0.
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor != nullptr) {
        actor->GetController()->DoAttack(m_TargetData, 0, nullptr, m_WeaponClassID, m_eAttackType, m_eSlotType);
        OnHitTarget(m_TargetData.nTargetId, m_TargetData.bIsCritical, m_TargetData.bIsHit);
        m_CurrentAction = ATK_SECONDHITTIME;
    }
}

void CAttackAction::OnSecondHitTimeForDual() {
    // Current L2Server 0x008638c8.
    L2_GUARD_FUNCTION();

    if ((m_eAttackType == AT_DUAL || m_eAttackType == AT_DUALFIST || m_eAttackType == AT_DUALDAGGER ||
            m_eAttackType == AT_DUALBLUNT) &&
        m_nMultiCount == 1) {
        CCreature* const actor = m_spActor.GetCreature();
        actor->GetController()->DoAttack(m_MultiTargetData[0], 0, nullptr, m_WeaponClassID, m_eAttackType, m_eSlotType);
        OnHitTarget(m_MultiTargetData[0].nTargetId, m_MultiTargetData[0].bIsCritical, m_MultiTargetData[0].bIsHit);
    }
    m_CurrentAction = ATK_COOLTIME;
}

bool CAttackAction::OnTick() {
    // Current L2Server 0x00863a7c.
    L2_GUARD_FUNCTION();
    m_ActionState = ActionState::Tick;

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__ATTACK_ONTICK);

    if (!actor->IsAlive() || !m_spTarget.IsValid() || !IsTargetInSameInZone()) {
        return false;
    }

    CObject* const targetObject = m_spTarget.GetObjectW();
    if (targetObject == nullptr) {
        return false;
    }
    if (targetObject != actor && !targetObject->IsTargetable()) {
        actor->ChangeTarget(nullptr, CTR_DEFAULT);
        return false;
    }

    CWorldObject* const target = targetObject->CastWorldObject();
    if (!CondForHiddenTarget(target)) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (!actor->m_SkillMod.m_bCanAct || !actor->m_SkillMod.m_bCanAttack || actor->m_ItemMod.m_bNoAttackWeapon) {
        return false;
    }

    if (actor->IsNPC() && !actor->IsSummonOrPet() && !actor->IsInRealWorld() &&
        !CWorld::Inst().GetGeo()->CanSee(actor->CastWorldObject(), targetObject->CastWorldObject(), true)) {
        return false;
    }

    const bool dualAttack = m_eAttackType == AT_DUAL || m_eAttackType == AT_DUALFIST || m_eAttackType == AT_DUALDAGGER ||
        m_eAttackType == AT_DUALBLUNT;
    for (;;) {
        if (m_CurrentAction == ATK_HITTIME) {
            if (m_bIsSoulShot) {
                actor->TurnOffSoulshot(false);
                m_bIsSoulShot = false;
            }
            if (dualAttack) {
                OnFirstHitTimeForDual();
                if (m_Time.m_nSecondHitTime == 0) {
                    return false;
                }
                if (m_Time.m_nSecondHitTime > 0x4fU) {
                    AddTimer(m_Time.m_nSecondHitTime, 0, 0);
                    return true;
                }
            } else {
                OnHitTimeNotDual();
                if (m_Time.m_nCoolTime == 0) {
                    return false;
                }
                if (m_Time.m_nCoolTime > 0x4fU) {
                    AddTimer(m_Time.m_nCoolTime, 0, 0);
                    return true;
                }
            }
            continue;
        }
        if (m_CurrentAction == ATK_SECONDHITTIME) {
            OnSecondHitTimeForDual();
            if (m_Time.m_nCoolTime == 0) {
                return false;
            }
            if (m_Time.m_nCoolTime > 0x4fU) {
                AddTimer(m_Time.m_nCoolTime, 0, 0);
                return true;
            }
            continue;
        }
        // Current L2Server 0x00863e80 returns false for ATK_COOLTIME so
        // AsyncTimerExpired2 advances to the prepared pending attack.
        if (m_CurrentAction == ATK_COOLTIME) {
            return false;
        }
        return true;
    }
}

void CAttackAction::OnEnd(bool success) {
    // Current L2Server 0x0085f7a8.
    L2_GUARD_FUNCTION();
    static_cast<void>(success);
    m_ActionState = ActionState::End;

    User* const user = m_spActor.GetUser();
    if (user != nullptr && user->m_PendingEquipItemID != 0) {
        user->OnUseItemPacket(static_cast<std::uint32_t>(user->m_PendingEquipItemID), 0);
    }
}

void CTalkAction::Set(CSmartID actor, CSmartID target) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
    m_spTarget = target;
}

void CGeneralAction::Set(CSmartID actor, CSmartID target, GeneralActionType actionType) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
    m_spTarget = target;
    type = actionType;
}

void CGetItemAction2::Set(CSmartID actor, CSmartID target) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
    m_spTarget = target;
}

bool CGetItemAction2::OnStart() {
    // Current L2Server 0x00864340.
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Start;
    CCreature* const actor = m_spActor.GetCreature();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (actor == nullptr || target == nullptr || !actor->IsAlive() || !actor->m_SkillMod.m_bCanAct ||
        actor->GetMorphID() != 0 || !target->IsItem()) {
        return false;
    }

    CItem* const item = target->CastItem();
    if (item == nullptr) {
        return false;
    }

    if (actor->IsPC()) {
        User* const user = actor->CastUser();
        if (user == nullptr) {
            return false;
        }
        if (user->m_ShowItemInfo) {
            item->ShowItemInfo(*user);
        }
        if (user->IsGMInvisible()) {
            return false;
        }

        CCursedWeaponMgr& cursedWeaponManager = CCursedWeaponMgr::inst();
        if ((cursedWeaponManager.FindConst(item->m_data->m_nClassID) != nullptr && actor->IsRiding()) ||
            user->m_data->m_nWorldId != g_nWorldId) {
            return false;
        }
    }

    actor->m_spActionTarget = m_spTarget;
    actor->GetItem(item, false);
    return false;
}

void COpenCloseAction2::Set(CSmartID actor, CSmartID target) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
    m_spTarget = target;
}

bool COpenCloseAction2::OnStart() {
    // Current L2Server 0x008658c8.
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Start;
    CCreature* const actor = m_spActor.GetCreature();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (actor == nullptr || target == nullptr || !actor->IsAlive() || actor->GetMorphID() != 0) {
        return false;
    }

    actor->m_spActionTarget = m_spTarget;
    if (!actor->m_SkillMod.m_bCanAct || !target->IsDoor()) {
        return false;
    }

    CDoor* const door = target->CastDoor();
    if (door == nullptr || !door->CanBeOpenedBy(actor)) {
        return false;
    }

    User* const user = actor->CastUser();
    if (user == nullptr) {
        return false;
    }

    const std::int32_t messageId = door->m_data->m_bIsOpen ? 0x475 : 0x474;
    TalkChannelCache::inst().mChannels[6].Cache(m_spActor.Sid(), m_spTarget.Sid(), messageId);
    user->GetSocket()->Send("cdddd", 0xf3, messageId, 0, 0, 0);
    return false;
}

void CAttackWaitAction::Set(CSmartID actor, CSmartID target, int delay, bool surely) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
    m_spTarget = target;
    m_nDelay = delay;
    m_bSurely = surely;
}

bool CAttackWaitAction::OnStart() {
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Start;
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }
    actor->m_spActionTarget = m_spTarget;
    if (!actor->m_SkillMod.m_bCanAct) {
        return false;
    }
    AddTimer(m_nDelay, 0, 0);
    return true;
}

bool CAttackWaitAction::OnTick() {
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Tick;
    m_pController->SetAsAttack(1, m_spTarget, m_bSurely);
    return false;
}

void CPrivateStoreAction::Set(CSmartID actor, int mode) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
    m_nMode = mode;
}

bool CPrivateStoreAction::OnStart() {
    // Current L2Server 0x00861650.
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Start;
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->IsAlive() || !actor->m_SkillMod.m_bCanAct || actor->GetMorphID() != 0) {
        return false;
    }

    actor->m_spActionTarget = nullptr;
    if (!actor->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Not a user try private store action -_-",
        //     L"NewAction.cpp",
        //     0xb60)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Not a user try private store action -_-",
        //     L"NewAction.cpp",
        //     0xb60)
        L2_LOG_ERROR(Log, "Cannot start private-store action: actor is not a player; action is rejected.");
        return false;
    }

    if (actor->CastUser()->m_data->m_bIsFakeDeath) {
        return false;
    }

    bool stopModeChanged = false;
    if (actor->CastUser()->GetPrivateStoreType() == PST_NONE && actor->IsPC()) {
        runtime::raw_atomic::IncrementI32(&g_nStoreUserCount);
        actor->CastUser()->m_dwPrivateStoreTick = runtime::GetTickCount();
        actor->GetController()->ActionBroadcastStopMove();
    }

    std::int32_t privateStoreType = PST_NONE;
    bool updatePrivateStoreType = true;
    switch (m_nMode) {
        // case 1:
        case PSA_SELL:
            privateStoreType = PST_OPEN;
            actor->m_data->m_StopMode = STP_STORE;
            stopModeChanged = true;
            break;
        // case 2:
        case PSA_BUY:
            privateStoreType = PST_BUY_OPEN;
            actor->m_data->m_StopMode = STP_STORE;
            stopModeChanged = true;
            break;
        // case 7:
        case PSA_WHOLE_SELL:
            privateStoreType = PST_WHOLESALE_OPEN;
            actor->m_data->m_StopMode = STP_STORE;
            stopModeChanged = true;
            break;
        // case 5:
        case PSA_RECIPE:
            privateStoreType = PST_RECIPE_SHOP_OPEN;
            actor->m_data->m_StopMode = STP_STORE;
            stopModeChanged = true;
            break;
        // case 3:
        case PSA_SELL_MANAGE:
            privateStoreType = PST_MANAGE;
            stopModeChanged = actor->m_data->m_StopMode != STP_STAND;
            actor->m_data->m_StopMode = STP_STAND;
            break;
        // case 4:
        case PSA_BUY_MANAGE:
            privateStoreType = PST_BUY_MANAGE;
            stopModeChanged = actor->m_data->m_StopMode != STP_STAND;
            actor->m_data->m_StopMode = STP_STAND;
            break;
        // case 8:
        case PSA_WHOLE_SELL_MANAGE:
            privateStoreType = PST_WHOLESALE_MANAGE;
            stopModeChanged = actor->m_data->m_StopMode != STP_STAND;
            actor->m_data->m_StopMode = STP_STAND;
            break;
        // case 6:
        case PSA_RECIPE_MANAGE:
            privateStoreType = PST_RECIPE_SHOP_MANAGE;
            stopModeChanged = actor->m_data->m_StopMode != STP_STAND;
            actor->m_data->m_StopMode = STP_STAND;
            break;
        default:
            updatePrivateStoreType = false;
            break;
    }

    if (updatePrivateStoreType) {
        actor->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            actor->m_data->m_nPrivateStoreType, privateStoreType, 0x1a
        );
    }

    User* const user = actor->CastUser();
    user->BroadcastWaitType(0);
    user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_PRIVATESTORE});
    AddTimer(stopModeChanged ? 0x9f6 : 0x50, 0, 0);
    return true;
}

bool CGeneralAction::OnStart() {
    // Current L2Server 0x008600b4.
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Start;
    CCreature* const actor = m_spActor.GetCreature();
    CCreature* const target = m_spTarget.GetCreature();
    if (actor == nullptr || target == nullptr || !actor->IsPC() || !target->IsPC()) {
        return false;
    }

    const FVector actorPosition = actor->m_data->GetPos();
    const FVector targetPosition = target->m_data->GetPos();
    const double dx = actorPosition.x - targetPosition.x;
    const double dy = actorPosition.y - targetPosition.y;
    if (dx * dx + dy * dy > 4194304.0) {
        return false;
    }

    User* const actorUser = actor->CastUser();
    User* const targetUser = target->CastUser();
    if (actorUser == nullptr || targetUser == nullptr) {
        return false;
    }
    actor->m_spActionTarget = m_spTarget;
    if (actorUser->GetDBID() == targetUser->GetDBID() || !actor->m_SkillMod.m_bCanAct) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (actor->GetMorphID() != 0) {
        return false;
    }

    const int targetStoreType = targetUser->GetPrivateStoreType();
    if (targetStoreType == PST_OPEN || targetStoreType == PST_WHOLESALE_OPEN) {
        if (actorUser->GetPrivateStoreType() == PST_NONE) {
            targetUser->TradeCancel(false);
            actorUser->TradeCancel(false);
            targetUser->PrivateStoreListReq(static_cast<int>(actor->Id()), targetStoreType);
        }
        return false;
    }
    if (targetStoreType == PST_BUY_OPEN) {
        if (actorUser->GetPrivateStoreType() == PST_NONE) {
            targetUser->TradeCancel(false);
            actorUser->TradeCancel(false);
            targetUser->PrivateStoreBuyListReq(static_cast<int>(actor->Id()));
        }
        return false;
    }
    if (targetStoreType == PST_RECIPE_SHOP_OPEN) {
        if (actorUser->GetPrivateStoreType() == PST_NONE) {
            targetUser->TradeCancel(false);
            actorUser->TradeCancel(false);
            targetUser->RecipeStoreListReq(static_cast<int>(actor->Id()));
        }
        return false;
    }
    if (targetUser->IsShopManaging()) {
        actorUser->GetSocket()->SendSystemMessage(SystemMessageId::PREPARING_PRIVATE_STORE);
    } else if (targetStoreType == PST_RECIPE_SHOP_MANAGE) {
        actorUser->GetSocket()->SendSystemMessage(SystemMessageId::PREPARING_PRIVATE_WORKSHOP);
    }
    return false;
}

bool CTalkAction::OnStart() {
    // Current L2Server 0x0085f938.
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Start;
    CCreature* const actor = m_spActor.GetCreature();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (actor == nullptr || !actor->IsAlive() || target == nullptr ||
        (!target->IsCreature() && !target->IsSignBoard() && !target->IsStatue())) {
        return false;
    }

    CNPC* const targetNpc = target->CastNPC();
    User* const actorUser = actor->CastUser();
    const bool ownSummon =
        actorUser != nullptr && targetNpc != nullptr && targetNpc->IsSummonOrPet() && targetNpc->GetMaster() == actorUser;
    if (!ownSummon && !target->IsAlive()) {
        return false;
    }

    // CTalkAction is only created for user actors. The current body relies
    // on that invariant and immediately consumes the CastUser result here.
    const std::int32_t targetSid = static_cast<std::int32_t>(m_spTarget.GetId());
    if (actorUser->m_nPrevTalkSid == targetSid) {
        const std::int32_t elapsed =
            static_cast<std::int32_t>(runtime::GetTickCount() - static_cast<std::uint32_t>(actorUser->m_nPrevTalkTick));
        if (elapsed < 0x5dc) {
            return false;
        }
    } else {
        actorUser->m_nPrevTalkSid = targetSid;
        actorUser->m_nPrevTalkTick = static_cast<std::int32_t>(runtime::GetTickCount());
    }

    actor->m_spActionTarget = m_spTarget;
    if (!actor->m_SkillMod.m_bCanAct || (actor->IsPC() && actor->IsRidingWyvern())) {
        return false;
    }

    const FVector actorPosition = actor->m_data->GetPos();
    const FVector targetPosition = target->GetPos();
    const double dx = actorPosition.x - targetPosition.x;
    const double dy = actorPosition.y - targetPosition.y;
    const double dz = actorPosition.z - targetPosition.z;
    const double distanceSquared = target->IsStatue() ? dx * dx + dy * dy : dx * dx + dy * dy + dz * dz;
    if (distanceSquared >= 62500.0) {
        return false;
    }

    actor->ChangeTalkTarget(target);
    if (targetNpc != nullptr && targetNpc->IsSummonOrPet() && targetNpc->CastSummon()->Controlable() &&
        targetNpc->GetMaster() == actorUser) {
        actorUser->GetSocket()->Send(
            "cdd", 0xb1, static_cast<std::int32_t>(targetNpc->m_data->m_SummonType), targetNpc->m_data->m_nId
        );
    } else if (!actor->IsPC() || !target->IsNPC()) {
        if (target->IsStaticObject()) {
            target->CastStaticObject()->Interact(actor);
        }
    } else {
        g_npcserver.Send("cddd", 6, static_cast<std::int32_t>(m_spActor.GetId()), actor->m_data->index, targetSid);
    }

    // actor->ActionFailed(0)
    actor->ActionFailed(AF_DEFAULT);
    actor->GetController()->ActionBroadcastStopMove();
    AddTimer(0x5dc, 0, 0);
    return true;
}

void CCoupleAction::Set(CSmartID actor, CSmartID target, int delayTime) {
    L2_GUARD_FUNCTION();

    if (delayTime < 1) {
        delayTime = 0x9f6;
    }
    m_spActor = actor;
    m_spTarget = target;
    m_nDelayTime = delayTime;
}

bool CCoupleAction::OnStart() {
    L2_GUARD_FUNCTION();

    m_ActionState = ActionState::Start;
    CCreature* const actorCreature = m_spActor.GetCreature();
    CCreature* const targetCreature = m_spTarget.GetCreature();
    if (actorCreature == nullptr || targetCreature == nullptr) {
        return false;
    }

    User* const actor = actorCreature->CastUser();
    User* const target = targetCreature->CastUser();
    if (actor == nullptr || target == nullptr) {
        return false;
    }

    actor->StopMove();
    actor->GetController()->ActionBroadcastStopMove();
    target->StopMove();
    target->GetController()->ActionBroadcastStopMove();
    actor->SocialAction(actor->m_nCoupleOption, 0x800, false);
    target->SocialAction(target->m_nCoupleOption, 0x800, false);
    actor->InitCoupleParam();
    target->InitCoupleParam();
    AddTimer(m_nDelayTime, 0, 0);
    return false;
}
